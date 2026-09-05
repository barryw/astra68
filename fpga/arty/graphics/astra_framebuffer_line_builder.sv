// Copyright (c) 2026 Astra68 contributors
//
// Builds one complete framebuffer scanline from bounded, ordered 64-bit AXI
// bursts. Surface bytes use Astra's explicit byte order, independent of the
// little-endian ARM host and AXI byte lanes.
`timescale 1ns/1ps
`default_nettype none

module astra_framebuffer_line_builder #(
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720,
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = {AXI_ID_WIDTH{1'b0}},
    parameter integer MAX_BUILD_CYCLES = 20000,
    parameter integer TRUSTED_CONFIG = 0
) (
    input  wire                         build_clk,
    input  wire                         build_reset,

    input  wire                         start,
    input  wire [1:0]                   build_slot,
    input  wire [9:0]                   line_y,
    input  wire [1:0]                   format,
    input  wire [31:0]                  framebuffer_base,
    input  wire [31:0]                  pitch,
    input  wire [12:0]                  virtual_width,
    input  wire [12:0]                  virtual_height,
    input  wire signed [31:0]           viewport_x,
    input  wire signed [31:0]           viewport_y,
    input  wire                         wrap_x,
    input  wire                         wrap_y,
    input  wire [31:0]                  arena_base,
    input  wire [31:0]                  arena_limit,

    output reg                          busy,
    output reg                          done,
    output reg                          line_complete,
    output reg  [1:0]                   completed_slot,
    output reg  [3:0]                   slot_valid,
    output reg                          config_error,
    output reg                          fetch_error,
    output reg                          deadline_error,
    output reg  [31:0]                  build_cycles,
    output reg  [31:0]                  read_bytes,
    output wire [31:0]                  axi_debug_status,
    output reg  [31:0]                  axi_ar_accept_count,
    output reg  [31:0]                  axi_r_accept_count,
    output reg  [31:0]                  axi_last_ar_address,
    output reg  [31:0]                  axi_response_stall_cycles,

    output wire [AXI_ID_WIDTH-1:0]      m_axi_arid,
    output wire [31:0]                  m_axi_araddr,
    output wire [7:0]                   m_axi_arlen,
    output wire [2:0]                   m_axi_arsize,
    output wire [1:0]                   m_axi_arburst,
    output wire [3:0]                   m_axi_arcache,
    output wire [2:0]                   m_axi_arprot,
    output wire [3:0]                   m_axi_arqos,
    output wire                         m_axi_arvalid,
    input  wire                         m_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      m_axi_rid,
    input  wire [63:0]                  m_axi_rdata,
    input  wire [1:0]                   m_axi_rresp,
    input  wire                         m_axi_rlast,
    input  wire                         m_axi_rvalid,
    output wire                         m_axi_rready,

    input  wire                         pixel_clk,
    input  wire                         pixel_reset,
    input  wire [1:0]                   pixel_read_slot,
    input  wire [10:0]                  pixel_read_x,
    output wire                         pixel_valid,
    output wire [31:0]                  pixel_value
);
    localparam [1:0] FORMAT_INDEX8 = 2'd0;
    localparam [1:0] FORMAT_RGB565 = 2'd1;
    localparam [1:0] FORMAT_XRGB8888 = 2'd2;

    localparam [4:0] ST_IDLE = 5'd0;
    localparam [4:0] ST_VALIDATE = 5'd1;
    localparam [4:0] ST_PLAN_Y = 5'd2;
    localparam [4:0] ST_PLAN_Y_MULTIPLY = 5'd3;
    localparam [4:0] ST_PLAN_Y_COMBINE = 5'd4;
    localparam [4:0] ST_PLAN_Y_ADDRESS = 5'd5;
    localparam [4:0] ST_PLAN_X_SIGN = 5'd6;
    localparam [4:0] ST_PLAN_X_CLIP = 5'd7;
    localparam [4:0] ST_PLAN_X_AVAILABLE = 5'd8;
    localparam [4:0] ST_PLAN_X_COUNTS = 5'd9;
    localparam [4:0] ST_PLAN_X_RIGHT = 5'd10;
    localparam [4:0] ST_PLAN_X_DISPATCH = 5'd11;
    localparam [4:0] ST_FILL_LEFT = 5'd12;
    localparam [4:0] ST_SEGMENT_ADDRESS = 5'd13;
    localparam [4:0] ST_SEGMENT = 5'd14;
    localparam [4:0] ST_SEGMENT_DRAIN = 5'd15;
    localparam [4:0] ST_FILL_RIGHT = 5'd16;
    localparam [4:0] ST_FINISH = 5'd17;
    localparam [4:0] ST_PLAN_Y_RESOLVE = 5'd18;
    localparam [4:0] ST_SEGMENT_COUNTS = 5'd19;

    localparam integer BURST_FIFO_DEPTH = 8;
    localparam integer BEAT_FIFO_DEPTH = 32;

    function automatic [7:0] beat_byte(
        input [63:0] beat,
        input [2:0]  byte_index
    );
        begin
            beat_byte = beat[byte_index * 8 +: 8];
        end
    endfunction

    reg [4:0] state;
    reg [1:0] build_slot_q;
    reg [9:0] line_y_q;
    reg [1:0] format_q;
    reg [1:0] bytes_shift_q;
    reg [2:0] bytes_per_pixel_q;
    reg [31:0] framebuffer_base_q;
    reg [31:0] pitch_q;
    reg [12:0] virtual_width_q;
    reg [12:0] virtual_height_q;
    reg signed [31:0] viewport_x_q;
    reg signed [31:0] viewport_y_q;
    reg wrap_x_q;
    reg wrap_y_q;

    wire [2:0] config_bytes_per_pixel =
        format == FORMAT_INDEX8 ? 3'd1 :
        format == FORMAT_RGB565 ? 3'd2 : 3'd4;
    wire [1:0] config_bytes_shift =
        format == FORMAT_INDEX8 ? 2'd0 :
        format == FORMAT_RGB565 ? 2'd1 : 2'd2;
    wire validator_start = state == ST_IDLE && start;
    wire validator_busy;
    wire validator_done;
    wire validator_config_valid;
    generate
        if (TRUSTED_CONFIG == 0) begin : generate_validator
            astra_framebuffer_config_validator #(
                .OUTPUT_WIDTH(OUTPUT_WIDTH),
                .OUTPUT_HEIGHT(OUTPUT_HEIGHT)
            ) config_validator_i (
                .clk(build_clk),
                .reset(build_reset),
                .start(validator_start),
                .format(format),
                .framebuffer_base(framebuffer_base),
                .pitch(pitch),
                .virtual_width(virtual_width),
                .virtual_height(virtual_height),
                .viewport_x(viewport_x),
                .viewport_y(viewport_y),
                .wrap_x(wrap_x),
                .wrap_y(wrap_y),
                .arena_base(arena_base),
                .arena_limit(arena_limit),
                .busy(validator_busy),
                .done(validator_done),
                .config_valid(validator_config_valid)
            );
        end else begin : generate_trusted_config
            assign validator_busy = 1'b0;
            assign validator_done = 1'b0;
            assign validator_config_valid = 1'b1;
        end
    endgenerate

    wire signed [32:0] planned_world_y =
        $signed({viewport_y_q[31], viewport_y_q}) +
        $signed({23'd0, line_y_q});
    wire [13:0] wrapped_y_sum =
        {1'b0, viewport_y_q[12:0]} + {4'd0, line_y_q};
    wire viewport_x_negative = viewport_x_q[31];
    wire [31:0] viewport_x_magnitude = ~viewport_x_q + 32'd1;
    wire [31:0] planned_source_x_wide = viewport_x_negative ?
        32'd0 : viewport_x_q;

    reg line_y_mapped_q;
    reg signed [32:0] planned_world_y_q;
    reg [13:0] wrapped_y_sum_q;
    reg [12:0] planned_source_y_q;
    reg [28:0] row_product_low_q;
    reg [28:0] row_product_high_q;
    reg [44:0] row_product_q;
    reg plan_x_negative_q;
    reg [31:0] plan_x_magnitude_q;
    reg [31:0] plan_source_x_wide_q;
    reg [10:0] plan_left_pixels_q;
    reg plan_source_inside_q;
    reg [12:0] plan_source_available_q;
    reg [11:0] plan_output_available_q;
    reg [10:0] plan_mapped_pixels_q;
    reg [10:0] plan_right_pixels_q;
    reg [31:0] row_address_q;
    reg [12:0] source_x_q;
    reg [10:0] output_x_q;
    reg [10:0] mapped_pixels_q;
    reg [10:0] right_pixels_q;
    reg [10:0] fill_pixels_q;
    reg [10:0] segment_pixels_remaining;
    reg segment_pixels_active_q;
    reg segment_last_pixel_q;

    wire [31:0] segment_byte_address =
        row_address_q + ({19'd0, source_x_q} << bytes_shift_q);
    wire [13:0] segment_payload_bytes =
        {3'd0, mapped_pixels_q} << bytes_shift_q;

    reg [31:0] issue_address;
    reg [10:0] issue_beats_remaining;
    reg [2:0] first_byte_offset;
    reg [13:0] segment_payload_bytes_q;
    reg ar_request_valid;
    reg [31:0] ar_request_address;
    reg [4:0] ar_request_beats;

    // Requests are always eight-byte aligned and no longer than 16 beats.
    // Only the final 128 bytes of a 4 KiB page can shorten a request, so keep
    // the boundary calculation to five bits instead of a 13-bit subtractor.
    wire [4:0] issue_beats_to_4k_capped =
        &issue_address[11:7] ?
            5'd16 - {1'b0, issue_address[6:3]} : 5'd16;
    wire [4:0] selected_burst_beats =
        issue_beats_remaining < 11'd16 ? issue_beats_remaining[4:0] :
        issue_beats_to_4k_capped;

    reg [4:0] burst_fifo [0:BURST_FIFO_DEPTH-1];
    reg [4:0] burst_head_beats;
    reg [2:0] burst_write_ptr;
    reg [2:0] burst_read_ptr;
    reg [3:0] burst_count;
    reg [5:0] reserved_beats;
    reg response_active;
    reg [4:0] response_beats_left;

    wire [5:0] selected_burst_beats_wide = {1'b0, selected_burst_beats};
    wire issue_credit_available =
        reserved_beats + selected_burst_beats_wide <= BEAT_FIFO_DEPTH;
    wire ar_plan = state == ST_SEGMENT && !ar_request_valid &&
        issue_beats_remaining != 11'd0 &&
        burst_count < BURST_FIFO_DEPTH && issue_credit_available;
    assign m_axi_arvalid = ar_request_valid;
    assign m_axi_arid = AXI_ID;
    assign m_axi_araddr = ar_request_address;
    assign m_axi_arlen = {3'd0, ar_request_beats} - 8'd1;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    // The memory controller may implement QoS as strict priority.  The
    // framebuffer is continuous traffic, so a nonzero value can starve HPS
    // accesses indefinitely even when memory bandwidth remains available.
    assign m_axi_arqos = 4'b0000;
    wire ar_accept = m_axi_arvalid && m_axi_arready;

    reg [63:0] beat_fifo [0:BEAT_FIFO_DEPTH-1];
    reg [4:0] beat_write_ptr;
    reg [4:0] beat_read_ptr;
    reg [5:0] beat_count;

    wire [4:0] expected_response_beats = response_active ?
        response_beats_left : burst_head_beats;
    wire expected_response_last = expected_response_beats == 5'd1;
    // AXI reads cannot be cancelled after AR acceptance.  A completed or
    // timed-out line therefore keeps RREADY asserted while discarding every
    // remaining response before the builder can be reused.
    assign m_axi_rready = burst_count != 4'd0 &&
        (state == ST_SEGMENT || state == ST_SEGMENT_DRAIN);
    wire response_accept = m_axi_rvalid && m_axi_rready;
    wire burst_pop = response_accept &&
        (m_axi_rlast || expected_response_last);
    wire beat_push = response_accept && state == ST_SEGMENT;

    assign axi_debug_status = {
        issue_beats_remaining != 11'd0,
        beat_active,
        state == ST_SEGMENT_DRAIN,
        response_active,
        beat_count,
        reserved_beats,
        burst_count,
        m_axi_rready,
        m_axi_rvalid,
        m_axi_arready,
        m_axi_arvalid,
        fetch_error,
        deadline_error,
        busy,
        state
    };

    reg beat_active;
    reg first_beat;
    // Keep this register outside the BRAM output stage. The integrated route
    // otherwise absorbs it into DO_REG and leaves byte selection on the
    // BRAM's 2.45 ns clock-to-output path.
    (* dont_touch = "yes" *) reg [63:0] active_beat;
    reg [2:0] active_byte;
    wire beat_pop = state == ST_SEGMENT && !beat_active &&
                    beat_count != 6'd0;

    wire [7:0] active_byte0 = beat_byte(active_beat, active_byte);
    wire [7:0] active_byte1 = beat_byte(active_beat, active_byte + 3'd1);
    wire [7:0] active_byte2 = beat_byte(active_beat, active_byte + 3'd2);
    wire [7:0] active_byte3 = beat_byte(active_beat, active_byte + 3'd3);

    wire mapped_write = state == ST_SEGMENT && beat_active &&
                        segment_pixels_active_q;
    wire invalid_write = (state == ST_FILL_LEFT ||
                          state == ST_FILL_RIGHT) && fill_pixels_q != 11'd0;
    wire line_write = mapped_write || invalid_write;

    // Byte selection is the expensive half of source decoding. Register it
    // separately from the format mux so neither stage exceeds two LUT levels.
    reg        line_write_valid_q;
    reg        line_write_mapped_q;
    reg [1:0]  line_write_slot_q;
    reg [10:0] line_write_x_q;
    reg [1:0]  line_write_format_q;
    reg [7:0]  line_write_byte0_q;
    reg [7:0]  line_write_byte1_q;
    reg [7:0]  line_write_byte2_q;
    reg [7:0]  line_write_byte3_q;

    wire [31:0] line_write_decoded =
        line_write_format_q == FORMAT_INDEX8 ?
            {24'd0, line_write_byte0_q} :
        line_write_format_q == FORMAT_RGB565 ?
            {16'd0, line_write_byte0_q, line_write_byte1_q} :
            {8'hff, line_write_byte1_q, line_write_byte2_q,
             line_write_byte3_q};
    wire [32:0] line_write_pixel = line_write_mapped_q ?
        {1'b1, line_write_decoded} : 33'd0;

    astra_framebuffer_line_store #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH)
    ) line_store_i (
        .build_clk(build_clk),
        .write_enable(line_write_valid_q),
        .write_slot(line_write_slot_q),
        .write_x(line_write_x_q),
        .write_pixel(line_write_pixel),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .read_slot(pixel_read_slot),
        .read_x(pixel_read_x),
        .read_valid(pixel_valid),
        .read_pixel(pixel_value)
    );

    always @(posedge build_clk) begin
        if (build_reset) begin
            state <= ST_IDLE;
            busy <= 1'b0;
            done <= 1'b0;
            line_complete <= 1'b0;
            completed_slot <= 2'd0;
            slot_valid <= 4'd0;
            config_error <= 1'b0;
            fetch_error <= 1'b0;
            deadline_error <= 1'b0;
            build_cycles <= 32'd0;
            read_bytes <= 32'd0;
            axi_ar_accept_count <= 32'd0;
            axi_r_accept_count <= 32'd0;
            axi_last_ar_address <= 32'd0;
            axi_response_stall_cycles <= 32'd0;
            build_slot_q <= 2'd0;
            line_y_q <= 10'd0;
            format_q <= FORMAT_INDEX8;
            bytes_shift_q <= 2'd0;
            bytes_per_pixel_q <= 3'd1;
            framebuffer_base_q <= 32'd0;
            pitch_q <= 32'd0;
            virtual_width_q <= 13'd0;
            virtual_height_q <= 13'd0;
            viewport_x_q <= 32'sd0;
            viewport_y_q <= 32'sd0;
            wrap_x_q <= 1'b0;
            wrap_y_q <= 1'b0;
            line_y_mapped_q <= 1'b0;
            planned_world_y_q <= 33'sd0;
            wrapped_y_sum_q <= 14'd0;
            planned_source_y_q <= 13'd0;
            row_product_low_q <= 29'd0;
            row_product_high_q <= 29'd0;
            row_product_q <= 45'd0;
            plan_x_negative_q <= 1'b0;
            plan_x_magnitude_q <= 32'd0;
            plan_source_x_wide_q <= 32'd0;
            plan_left_pixels_q <= 11'd0;
            plan_source_inside_q <= 1'b0;
            plan_source_available_q <= 13'd0;
            plan_output_available_q <= 12'd0;
            plan_mapped_pixels_q <= 11'd0;
            plan_right_pixels_q <= 11'd0;
            row_address_q <= 32'd0;
            source_x_q <= 13'd0;
            output_x_q <= 11'd0;
            mapped_pixels_q <= 11'd0;
            right_pixels_q <= 11'd0;
            fill_pixels_q <= 11'd0;
            segment_pixels_remaining <= 11'd0;
            segment_pixels_active_q <= 1'b0;
            segment_last_pixel_q <= 1'b0;
            issue_address <= 32'd0;
            issue_beats_remaining <= 11'd0;
            first_byte_offset <= 3'd0;
            segment_payload_bytes_q <= 14'd0;
            ar_request_valid <= 1'b0;
            ar_request_address <= 32'd0;
            ar_request_beats <= 5'd0;
            burst_head_beats <= 5'd0;
            burst_write_ptr <= 3'd0;
            burst_read_ptr <= 3'd0;
            burst_count <= 4'd0;
            reserved_beats <= 6'd0;
            response_active <= 1'b0;
            response_beats_left <= 5'd0;
            beat_write_ptr <= 5'd0;
            beat_read_ptr <= 5'd0;
            beat_count <= 6'd0;
            beat_active <= 1'b0;
            first_beat <= 1'b0;
            active_beat <= 64'd0;
            active_byte <= 3'd0;
            line_write_valid_q <= 1'b0;
            line_write_mapped_q <= 1'b0;
            line_write_slot_q <= 2'd0;
            line_write_x_q <= 11'd0;
            line_write_format_q <= FORMAT_INDEX8;
            line_write_byte0_q <= 8'd0;
            line_write_byte1_q <= 8'd0;
            line_write_byte2_q <= 8'd0;
            line_write_byte3_q <= 8'd0;
        end else begin
            done <= 1'b0;
            line_complete <= 1'b0;

            line_write_valid_q <= line_write;
            if (line_write) begin
                line_write_mapped_q <= mapped_write;
                line_write_slot_q <= build_slot_q;
                line_write_x_q <= output_x_q;
                line_write_format_q <= format_q;
                line_write_byte0_q <= active_byte0;
                line_write_byte1_q <= active_byte1;
                line_write_byte2_q <= active_byte2;
                line_write_byte3_q <= active_byte3;
            end

            if (busy && !deadline_error)
                build_cycles <= build_cycles + 32'd1;

            if (ar_accept) begin
                axi_ar_accept_count <= axi_ar_accept_count + 32'd1;
                axi_last_ar_address <= m_axi_araddr;
            end
            if (response_accept) begin
                axi_r_accept_count <= axi_r_accept_count + 32'd1;
                axi_response_stall_cycles <= 32'd0;
            end else if (burst_count != 4'd0 &&
                         axi_response_stall_cycles != 32'hffffffff) begin
                axi_response_stall_cycles <=
                    axi_response_stall_cycles + 32'd1;
            end else if (burst_count == 4'd0) begin
                axi_response_stall_cycles <= 32'd0;
            end

            if (response_accept) begin
                if (state == ST_SEGMENT) begin
                    beat_fifo[beat_write_ptr] <= m_axi_rdata;
                    beat_write_ptr <= beat_write_ptr + 5'd1;
                end
                if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                    m_axi_rlast != expected_response_last)
                    fetch_error <= 1'b1;

                if (burst_pop) begin
                    response_active <= 1'b0;
                    response_beats_left <= 5'd0;
                end else begin
                    response_active <= 1'b1;
                    response_beats_left <= expected_response_beats - 5'd1;
                end
            end

            case ({ar_accept, burst_pop})
                2'b10: begin
                    burst_count <= burst_count + 4'd1;
                    if (burst_count == 4'd0) begin
                        burst_head_beats <= ar_request_beats;
                    end else begin
                        burst_fifo[burst_write_ptr] <= ar_request_beats;
                        burst_write_ptr <= burst_write_ptr + 3'd1;
                    end
                end
                2'b01: begin
                    burst_count <= burst_count - 4'd1;
                    if (burst_count > 4'd1) begin
                        burst_head_beats <= burst_fifo[burst_read_ptr];
                        burst_read_ptr <= burst_read_ptr + 3'd1;
                    end else begin
                        burst_head_beats <= 5'd0;
                    end
                end
                2'b11: begin
                    if (burst_count > 4'd1) begin
                        burst_head_beats <= burst_fifo[burst_read_ptr];
                        burst_read_ptr <= burst_read_ptr + 3'd1;
                        burst_fifo[burst_write_ptr] <= ar_request_beats;
                        burst_write_ptr <= burst_write_ptr + 3'd1;
                    end else begin
                        burst_head_beats <= ar_request_beats;
                    end
                end
                default: begin end
            endcase

            case ({ar_accept, beat_pop})
                2'b10: reserved_beats <=
                    reserved_beats + {1'b0, ar_request_beats};
                2'b01: reserved_beats <= reserved_beats - 6'd1;
                2'b11: reserved_beats <=
                    reserved_beats + {1'b0, ar_request_beats} - 6'd1;
                default: begin end
            endcase

            if (state == ST_SEGMENT) begin
                if (ar_plan) begin
                    ar_request_valid <= 1'b1;
                    ar_request_address <= issue_address;
                    ar_request_beats <= selected_burst_beats;
                end

                if (ar_accept) begin
                    ar_request_valid <= 1'b0;
                    issue_address <= issue_address +
                        ({27'd0, ar_request_beats} << 3);
                    issue_beats_remaining <= issue_beats_remaining -
                        ar_request_beats;
                    read_bytes <= read_bytes +
                        ({27'd0, ar_request_beats} << 3);
                end

                if (beat_pop)
                    beat_read_ptr <= beat_read_ptr + 5'd1;
                case ({beat_push, beat_pop})
                    2'b10: beat_count <= beat_count + 6'd1;
                    2'b01: beat_count <= beat_count - 6'd1;
                    default: begin end
                endcase
            end

            case (state)
                ST_IDLE: begin
                    if (start) begin
                        config_error <= 1'b0;
                        fetch_error <= 1'b0;
                        deadline_error <= 1'b0;
                        build_cycles <= 32'd0;
                        read_bytes <= 32'd0;
                        completed_slot <= build_slot;
                        slot_valid[build_slot] <= 1'b0;
                        build_slot_q <= build_slot;
                        line_y_q <= line_y;
                        format_q <= format;
                        bytes_shift_q <= config_bytes_shift;
                        bytes_per_pixel_q <= config_bytes_per_pixel;
                        framebuffer_base_q <= framebuffer_base;
                        pitch_q <= pitch;
                        virtual_width_q <= virtual_width;
                        virtual_height_q <= virtual_height;
                        viewport_x_q <= viewport_x;
                        viewport_y_q <= viewport_y;
                        wrap_x_q <= wrap_x;
                        wrap_y_q <= wrap_y;
                        output_x_q <= 11'd0;
                        busy <= 1'b1;
                        if (TRUSTED_CONFIG != 0)
                            state <= ST_PLAN_Y;
                        else
                            state <= ST_VALIDATE;
                    end
                end

                ST_VALIDATE: begin
                    if (validator_done) begin
                        if (!validator_config_valid) begin
                            config_error <= 1'b1;
                            busy <= 1'b0;
                            done <= 1'b1;
                            state <= ST_IDLE;
                        end else begin
                            state <= ST_PLAN_Y;
                        end
                    end
                end

                ST_PLAN_Y: begin
                    planned_world_y_q <= planned_world_y;
                    wrapped_y_sum_q <= wrapped_y_sum;
                    state <= ST_PLAN_Y_RESOLVE;
                end

                ST_PLAN_Y_RESOLVE: begin
                    line_y_mapped_q <= wrap_y_q ||
                        (!planned_world_y_q[32] &&
                         planned_world_y_q[31:13] == 19'd0 &&
                         planned_world_y_q[12:0] < virtual_height_q);
                    planned_source_y_q <= wrap_y_q ?
                        (wrapped_y_sum_q >= {1'b0, virtual_height_q} ?
                            wrapped_y_sum_q - {1'b0, virtual_height_q} :
                            wrapped_y_sum_q[12:0]) :
                        planned_world_y_q[12:0];
                    state <= ST_PLAN_Y_MULTIPLY;
                end

                ST_PLAN_Y_MULTIPLY: begin
                    row_product_low_q <=
                        pitch_q[15:0] * planned_source_y_q;
                    row_product_high_q <=
                        pitch_q[31:16] * planned_source_y_q;
                    state <= ST_PLAN_Y_COMBINE;
                end

                ST_PLAN_Y_COMBINE: begin
                    row_product_q <= {row_product_high_q, 16'd0} +
                                     {16'd0, row_product_low_q};
                    state <= ST_PLAN_Y_ADDRESS;
                end

                ST_PLAN_Y_ADDRESS: begin
                    row_address_q <= framebuffer_base_q +
                                     row_product_q[31:0];
                    state <= ST_PLAN_X_SIGN;
                end

                ST_PLAN_X_SIGN: begin
                    plan_x_negative_q <= viewport_x_negative;
                    plan_x_magnitude_q <= viewport_x_magnitude;
                    plan_source_x_wide_q <= planned_source_x_wide;
                    state <= ST_PLAN_X_CLIP;
                end

                ST_PLAN_X_CLIP: begin
                    plan_left_pixels_q <= plan_x_negative_q ?
                        (plan_x_magnitude_q >= OUTPUT_WIDTH ?
                            OUTPUT_WIDTH[10:0] :
                            plan_x_magnitude_q[10:0]) : 11'd0;
                    plan_source_inside_q <=
                        plan_source_x_wide_q < {19'd0, virtual_width_q};
                    state <= ST_PLAN_X_AVAILABLE;
                end

                ST_PLAN_X_AVAILABLE: begin
                    plan_source_available_q <= plan_source_inside_q ?
                        virtual_width_q - plan_source_x_wide_q[12:0] :
                        13'd0;
                    plan_output_available_q <=
                        OUTPUT_WIDTH - {1'b0, plan_left_pixels_q};
                    state <= ST_PLAN_X_COUNTS;
                end

                ST_PLAN_X_COUNTS: begin
                    plan_mapped_pixels_q <= plan_source_inside_q ?
                        (plan_source_available_q <
                            plan_output_available_q ?
                            plan_source_available_q[10:0] :
                            plan_output_available_q[10:0]) : 11'd0;
                    state <= ST_PLAN_X_RIGHT;
                end

                ST_PLAN_X_RIGHT: begin
                    plan_right_pixels_q <=
                        plan_output_available_q -
                        {1'b0, plan_mapped_pixels_q};
                    state <= ST_PLAN_X_DISPATCH;
                end

                ST_PLAN_X_DISPATCH: begin
                    output_x_q <= 11'd0;
                    if (!line_y_mapped_q) begin
                        mapped_pixels_q <= 11'd0;
                        right_pixels_q <= 11'd0;
                        fill_pixels_q <= OUTPUT_WIDTH[10:0];
                        state <= ST_FILL_LEFT;
                    end else if (wrap_x_q) begin
                        source_x_q <= plan_source_x_wide_q[12:0];
                        mapped_pixels_q <= plan_mapped_pixels_q;
                        right_pixels_q <= 11'd0;
                        state <= ST_SEGMENT_ADDRESS;
                    end else begin
                        source_x_q <= plan_source_x_wide_q[12:0];
                        mapped_pixels_q <= plan_mapped_pixels_q;
                        right_pixels_q <= plan_right_pixels_q;
                        if (plan_left_pixels_q != 11'd0) begin
                            fill_pixels_q <= plan_left_pixels_q;
                            state <= ST_FILL_LEFT;
                        end else if (plan_mapped_pixels_q != 11'd0) begin
                            state <= ST_SEGMENT_ADDRESS;
                        end else begin
                            fill_pixels_q <= plan_right_pixels_q;
                            state <= ST_FILL_RIGHT;
                        end
                    end
                end

                ST_FILL_LEFT: begin
                    if (fill_pixels_q != 11'd0) begin
                        output_x_q <= output_x_q + 11'd1;
                        fill_pixels_q <= fill_pixels_q - 11'd1;
                        if (fill_pixels_q == 11'd1) begin
                            if (mapped_pixels_q != 11'd0)
                                state <= ST_SEGMENT_ADDRESS;
                            else if (right_pixels_q != 11'd0) begin
                                fill_pixels_q <= right_pixels_q;
                                state <= ST_FILL_RIGHT;
                            end else
                                state <= ST_FINISH;
                        end
                    end else begin
                        state <= ST_FINISH;
                    end
                end

                ST_SEGMENT_ADDRESS: begin
                    issue_address <= {segment_byte_address[31:3], 3'b000};
                    first_byte_offset <= segment_byte_address[2:0];
                    segment_payload_bytes_q <= segment_payload_bytes;
                    state <= ST_SEGMENT_COUNTS;
                end

                ST_SEGMENT_COUNTS: begin
                    // Keep address generation and the rounded byte-to-beat
                    // conversion on separate 200 MHz cycles.
                    issue_beats_remaining <=
                        ({1'b0, segment_payload_bytes_q} +
                         {12'd0, first_byte_offset} + 15'd7) >> 3;
                    ar_request_valid <= 1'b0;
                    segment_pixels_remaining <= mapped_pixels_q;
                    segment_pixels_active_q <= mapped_pixels_q != 11'd0;
                    segment_last_pixel_q <= mapped_pixels_q == 11'd1;
                    burst_write_ptr <= 3'd0;
                    burst_read_ptr <= 3'd0;
                    burst_count <= 4'd0;
                    reserved_beats <= 6'd0;
                    burst_head_beats <= 5'd0;
                    response_active <= 1'b0;
                    response_beats_left <= 5'd0;
                    beat_write_ptr <= 5'd0;
                    beat_read_ptr <= 5'd0;
                    beat_count <= 6'd0;
                    beat_active <= 1'b0;
                    first_beat <= 1'b1;
                    active_byte <= 3'd0;
                    state <= ST_SEGMENT;
                end

                ST_SEGMENT: begin
                    if (!beat_active && beat_count != 6'd0) begin
                        active_beat <= beat_fifo[beat_read_ptr];
                        active_byte <= first_beat ? first_byte_offset : 3'd0;
                        first_beat <= 1'b0;
                        beat_active <= 1'b1;
                    end else if (beat_active && segment_pixels_active_q) begin
                        output_x_q <= output_x_q + 11'd1;
                        segment_pixels_remaining <=
                            segment_pixels_remaining - 11'd1;
                        if (segment_last_pixel_q) begin
                            segment_pixels_active_q <= 1'b0;
                            segment_last_pixel_q <= 1'b0;
                            beat_active <= 1'b0;
                            ar_request_valid <= 1'b0;
                            issue_beats_remaining <= 11'd0;
                            beat_count <= 6'd0;
                            state <= ST_SEGMENT_DRAIN;
                        end else begin
                            segment_last_pixel_q <=
                                segment_pixels_remaining == 11'd2;
                            if ({1'b0, active_byte} +
                                {1'b0, bytes_per_pixel_q} >= 4'd8)
                                beat_active <= 1'b0;
                            else
                                active_byte <= active_byte +
                                    bytes_per_pixel_q;
                        end
                    end
                end

                ST_SEGMENT_DRAIN: begin
                    if (burst_count == 4'd0 && !response_active) begin
                        if (deadline_error) begin
                            busy <= 1'b0;
                            done <= 1'b1;
                            state <= ST_IDLE;
                        end else if (wrap_x_q && output_x_q < OUTPUT_WIDTH) begin
                            source_x_q <= 13'd0;
                            mapped_pixels_q <= OUTPUT_WIDTH - output_x_q;
                            state <= ST_SEGMENT_ADDRESS;
                        end else if (right_pixels_q != 11'd0) begin
                            fill_pixels_q <= right_pixels_q;
                            state <= ST_FILL_RIGHT;
                        end else begin
                            state <= ST_FINISH;
                        end
                    end
                end

                ST_FILL_RIGHT: begin
                    if (fill_pixels_q != 11'd0) begin
                        output_x_q <= output_x_q + 11'd1;
                        fill_pixels_q <= fill_pixels_q - 11'd1;
                        if (fill_pixels_q == 11'd1)
                            state <= ST_FINISH;
                    end else begin
                        state <= ST_FINISH;
                    end
                end

                ST_FINISH: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    completed_slot <= build_slot_q;
                    if (!config_error && !fetch_error && !deadline_error) begin
                        slot_valid[build_slot_q] <= 1'b1;
                        line_complete <= 1'b1;
                    end
                    state <= ST_IDLE;
                end

                default: begin
                    busy <= 1'b0;
                    config_error <= 1'b1;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase

            if (busy && build_cycles == MAX_BUILD_CYCLES - 1) begin
                fetch_error <= 1'b1;
                deadline_error <= 1'b1;
                slot_valid[build_slot_q] <= 1'b0;
                ar_request_valid <= 1'b0;
                issue_beats_remaining <= 11'd0;
                beat_count <= 6'd0;
                beat_active <= 1'b0;
                segment_pixels_active_q <= 1'b0;
                state <= ST_SEGMENT_DRAIN;
            end
        end
    end

    wire unused_validator_busy = validator_busy;
endmodule

`default_nettype wire
