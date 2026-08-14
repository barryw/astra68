// Copyright (c) 2026 Astra68 contributors
//
// Bounded scanline flood fill. Pending seeds live in the caller-provided
// validated workspace; the shared writer barrier orders workspace and pixel
// writes before dependent reads.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_flood #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = {AXI_ID_WIDTH{1'b0}}
) (
    input  wire                         clk,
    input  wire                         reset,
    input  wire                         start,
    input  wire                         abort,
    input  wire [31:0]                  arena_base,
    input  wire signed [15:0]           clip_left,
    input  wire signed [15:0]           clip_top,
    input  wire signed [15:0]           clip_right,
    input  wire signed [15:0]           clip_bottom,
    input  wire signed [15:0]           seed_x,
    input  wire signed [15:0]           seed_y,
    input  wire [31:0]                  replacement,
    input  wire [31:0]                  destination_data_offset,
    input  wire [31:0]                  destination_pitch,
    input  wire [15:0]                  destination_width,
    input  wire [15:0]                  destination_height,
    input  wire [7:0]                   destination_format,
    input  wire [2:0]                   destination_bytes_per_pixel,
    input  wire [31:0]                  workspace_data_offset,
    input  wire [31:0]                  workspace_data_bytes,

    output reg                          busy,
    output reg                          done,
    output reg  [15:0]                  status,
    output reg  [31:0]                  fault_detail,
    output reg  [31:0]                  completed_pixels,

    output reg                          writer_start,
    output reg                          writer_abort,
    output reg                          writer_flush,
    input  wire                         writer_flush_ready,
    output reg                          writer_barrier,
    input  wire                         writer_barrier_ready,
    input  wire                         writer_barrier_done,
    input  wire                         writer_done,
    input  wire                         writer_aborted,
    input  wire                         writer_error,
    input  wire [31:0]                  writer_fault_detail,
    output reg                          pixel_valid,
    input  wire                         pixel_ready,
    output reg  [31:0]                  pixel_address,
    output reg  [7:0]                   pixel_format,
    output reg  [31:0]                  pixel_value,

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
    output wire                         m_axi_rready
);
    localparam [5:0] ST_IDLE = 6'd0;
    localparam [5:0] ST_BOUNDS = 6'd1;
    localparam [5:0] ST_SEED = 6'd2;
    localparam [5:0] ST_ADDRESS_MULTIPLY = 6'd3;
    localparam [5:0] ST_ADDRESS_SUM = 6'd4;
    localparam [5:0] ST_READ_REQUEST = 6'd5;
    localparam [5:0] ST_READ_RESPONSE = 6'd6;
    localparam [5:0] ST_READ_DISPATCH = 6'd7;
    localparam [5:0] ST_TARGET = 6'd8;
    localparam [5:0] ST_LEFT = 6'd9;
    localparam [5:0] ST_LEFT_RESULT = 6'd10;
    localparam [5:0] ST_RIGHT = 6'd11;
    localparam [5:0] ST_RIGHT_RESULT = 6'd12;
    localparam [5:0] ST_WRITE_SPAN = 6'd13;
    localparam [5:0] ST_NEIGHBOR_ROW = 6'd14;
    localparam [5:0] ST_NEIGHBOR = 6'd15;
    localparam [5:0] ST_NEIGHBOR_RESULT = 6'd16;
    localparam [5:0] ST_PUSH = 6'd17;
    localparam [5:0] ST_NEIGHBOR_NEXT = 6'd18;
    localparam [5:0] ST_BARRIER = 6'd19;
    localparam [5:0] ST_BARRIER_WAIT = 6'd20;
    localparam [5:0] ST_POP = 6'd21;
    localparam [5:0] ST_WORKSPACE_REQUEST = 6'd22;
    localparam [5:0] ST_WORKSPACE_RESPONSE = 6'd23;
    localparam [5:0] ST_WORKSPACE_DECODE = 6'd24;
    localparam [5:0] ST_FLUSH = 6'd25;
    localparam [5:0] ST_WRITER_DONE = 6'd26;
    localparam [5:0] ST_ABORT = 6'd27;
    localparam [5:0] ST_PUSH_PREP = 6'd28;
    localparam [5:0] ST_READ_CLASSIFY = 6'd29;
    localparam [5:0] ST_OVERFLOW = 6'd30;
    localparam [5:0] ST_NEIGHBOR_ROW_CLASSIFY = 6'd31;
    localparam [5:0] ST_SEED_ACTIVATE = 6'd32;
    localparam [5:0] ST_ADDRESS_COLUMN = 6'd33;
    localparam [5:0] ST_NEIGHBOR_CLASSIFY = 6'd34;
    localparam [5:0] ST_NEIGHBOR_ROW_DECIDE = 6'd35;

    localparam [2:0] READ_TARGET = 3'd0;
    localparam [2:0] READ_LEFT = 3'd1;
    localparam [2:0] READ_RIGHT = 3'd2;
    localparam [2:0] READ_NEIGHBOR = 3'd3;

    function automatic [7:0] beat_byte(
        input [63:0] beat,
        input [2:0] lane
    );
        begin
            beat_byte = beat[lane * 8 +: 8];
        end
    endfunction

    (* fsm_encoding = "one_hot" *) reg [5:0] state;
    (* extract_reset = "no" *) reg signed [16:0] left_bound_q, top_bound_q;
    (* extract_reset = "no" *) reg signed [16:0] right_bound_q, bottom_bound_q;
    reg signed [16:0] seed_x_q, seed_y_q;
    reg signed [16:0] active_x_q;
    reg left_scan_exhausted_q;
    reg right_scan_exhausted_q;
    reg signed [16:0] span_left_q, span_right_q;
    reg signed [16:0] neighbor_x_q, neighbor_y_q;
    reg neighbor_row_q, neighbor_row_in_bounds_q, neighbor_run_q;
    reg seed_admitted_q;
    reg writer_barrier_ready_q;
    reg [31:0] replacement_q, target_q;
    reg target_valid_q;
    reg signed [15:0] clip_left_q, clip_top_q, clip_right_q, clip_bottom_q;
    reg [31:0] arena_base_q;
    reg [31:0] destination_surface_base_q, destination_row_address_q;
    reg [31:0] destination_pitch_q;
    reg [15:0] destination_width_q, destination_height_q;
    reg [7:0] destination_format_q;
    reg [2:0] destination_bpp_q;
    reg [31:0] workspace_data_offset_q, workspace_capacity_q;
    reg stack_has_capacity_q;
    reg stack_nonempty_q;
    reg neighbor_past_span_q;
    (* extract_reset = "no" *) reg [31:0] stack_count_q;

    reg signed [16:0] read_x_q, read_y_q;
    reg [2:0] read_return_q;
    // This is an intentional timing boundary. If it is merged back into the
    // conditionally written read_y_q, Vivado recreates an FSM-driven DSP CE.
    (* dont_touch = "yes" *) reg [15:0] row_y_operand_q;
    reg [15:0] row_pitch_low_operand_q, row_pitch_high_operand_q;
    reg [31:0] row_low_product_q, row_high_product_q;
    reg [31:0] row_offset_q;
    reg [31:0] column_offset_q;
    reg address_start_q, address_operand_valid_q, address_valid_q;
    reg address_sum_valid_q;
    reg address_done_q;
    reg [31:0] read_pixel_address_q, read_beat_address_q;
    (* extract_reset = "no" *) reg [31:0] read_pixel_q;
    reg read_is_target_q, read_is_replacement_q;
    reg [63:0] read_data_q;
    reg [31:0] workspace_read_address_q;
    reg arvalid_q;

    wire [2:0] read_lane = read_pixel_address_q[2:0];
    wire [31:0] decoded_pixel =
        destination_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
            {24'd0, beat_byte(read_data_q, read_lane)} :
        destination_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            {16'd0, beat_byte(read_data_q, read_lane),
             beat_byte(read_data_q, read_lane + 3'd1)} :
            {beat_byte(read_data_q, read_lane),
             beat_byte(read_data_q, read_lane + 3'd1),
             beat_byte(read_data_q, read_lane + 3'd2),
             beat_byte(read_data_q, read_lane + 3'd3)};
    wire read_is_target =
        destination_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
            read_pixel_q[7:0] == target_q[7:0] :
        destination_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            read_pixel_q[15:0] == target_q[15:0] :
            read_pixel_q == target_q;
    wire read_is_replacement =
        destination_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
            replacement_q[7:0] == read_pixel_q[7:0] :
        destination_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            replacement_q[15:0] == read_pixel_q[15:0] :
            replacement_q == read_pixel_q;
    wire [31:0] computed_pixel_address =
        destination_row_address_q + column_offset_q;
    (* use_dsp = "no" *) wire [31:0] row_offset_sum =
        row_low_product_q + {row_high_product_q[15:0], 16'd0};
    wire [31:0] computed_workspace_read_address = arena_base_q +
        workspace_data_offset_q + (stack_count_q << 2);

    assign m_axi_arid = AXI_ID;
    assign m_axi_araddr = read_beat_address_q;
    assign m_axi_arlen = 8'd0;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b0000;
    assign m_axi_arvalid = arvalid_q;
    assign m_axi_rready = 1'b1;

    task automatic issue_pixel_read;
        input signed [16:0] x;
        input signed [16:0] y;
        input [2:0] return_code;
        begin
            read_x_q <= x;
            read_y_q <= y;
            read_return_q <= return_code;
            state <= ST_ADDRESS_MULTIPLY;
        end
    endtask

    // Keep the arithmetic pipeline free-running. The registered valid chain
    // controls when its result is consumed without putting the FSM on DSP CEs.
    always @(posedge clk) begin
        if (reset) begin
            address_valid_q <= 1'b0;
            address_operand_valid_q <= 1'b0;
            address_sum_valid_q <= 1'b0;
            address_done_q <= 1'b0;
            row_y_operand_q <= 16'd0;
            row_pitch_low_operand_q <= 16'd0;
            row_pitch_high_operand_q <= 16'd0;
            row_low_product_q <= 32'd0;
            row_high_product_q <= 32'd0;
            row_offset_q <= 32'd0;
            column_offset_q <= 32'd0;
        end else begin
            // Keep the DSP input registers free-running. Gating them lets
            // synthesis absorb the address FSM into the DSP A/B clock enable.
            row_y_operand_q <= read_y_q[15:0];
            row_pitch_low_operand_q <= destination_pitch_q[15:0];
            row_pitch_high_operand_q <= destination_pitch_q[31:16];
            column_offset_q <= read_x_q[15:0] * destination_bpp_q;
            row_low_product_q <= row_y_operand_q *
                row_pitch_low_operand_q;
            row_high_product_q <= row_y_operand_q *
                row_pitch_high_operand_q;
            row_offset_q <= row_offset_sum;
            address_operand_valid_q <= address_start_q;
            address_valid_q <= address_operand_valid_q;
            address_sum_valid_q <= address_valid_q;
            address_done_q <= address_sum_valid_q;
        end
    end

    always @(posedge clk) begin
        writer_start <= 1'b0;
        writer_abort <= 1'b0;
        writer_flush <= 1'b0;
        writer_barrier <= 1'b0;
        address_start_q <= 1'b0;
        done <= 1'b0;
        writer_barrier_ready_q <= writer_barrier_ready;

        if (reset) begin
            state <= ST_IDLE;
            busy <= 1'b0;
            status <= `ASTRA_RENDER_STATUS_OK;
            fault_detail <= 32'd0;
            completed_pixels <= 32'd0;
            pixel_valid <= 1'b0;
            arvalid_q <= 1'b0;
            neighbor_run_q <= 1'b0;
            seed_admitted_q <= 1'b0;
            writer_barrier_ready_q <= 1'b0;
            target_valid_q <= 1'b0;
            read_is_target_q <= 1'b0;
            read_is_replacement_q <= 1'b0;
            stack_has_capacity_q <= 1'b0;
            stack_nonempty_q <= 1'b0;
            neighbor_past_span_q <= 1'b0;
        end else if (abort && busy && state != ST_ABORT &&
                     state != ST_WRITER_DONE) begin
            pixel_valid <= 1'b0;
            arvalid_q <= 1'b0;
            writer_abort <= 1'b1;
            status <= `ASTRA_RENDER_STATUS_RESET;
            state <= ST_ABORT;
        end else if (writer_error && busy && state != ST_ABORT &&
                     state != ST_WRITER_DONE) begin
            pixel_valid <= 1'b0;
            arvalid_q <= 1'b0;
            writer_abort <= 1'b1;
            status <= `ASTRA_RENDER_STATUS_AXI_WRITE;
            fault_detail <= writer_fault_detail;
            state <= ST_ABORT;
        end else case (state)
            ST_IDLE: if (start) begin
                busy <= 1'b1;
                status <= `ASTRA_RENDER_STATUS_OK;
                fault_detail <= 32'd0;
                completed_pixels <= 32'd0;
                arena_base_q <= arena_base;
                destination_surface_base_q <= arena_base +
                    destination_data_offset;
                destination_pitch_q <= destination_pitch;
                destination_width_q <= destination_width;
                destination_height_q <= destination_height;
                destination_format_q <= destination_format;
                destination_bpp_q <= destination_bytes_per_pixel;
                workspace_data_offset_q <= workspace_data_offset;
                workspace_capacity_q <= workspace_data_bytes >> 2;
                seed_x_q <= seed_x;
                seed_y_q <= seed_y;
                replacement_q <= replacement;
                clip_left_q <= clip_left;
                clip_top_q <= clip_top;
                clip_right_q <= clip_right;
                clip_bottom_q <= clip_bottom;
                target_valid_q <= 1'b0;
                stack_count_q <= 32'd0;
                stack_nonempty_q <= 1'b0;
                writer_start <= 1'b1;
                state <= ST_BOUNDS;
            end

            ST_BOUNDS: begin
                left_bound_q <= $signed(clip_left_q) < 0 ? 17'sd0 :
                    $signed({clip_left_q[15], clip_left_q});
                top_bound_q <= $signed(clip_top_q) < 0 ? 17'sd0 :
                    $signed({clip_top_q[15], clip_top_q});
                right_bound_q <= $signed(clip_right_q) >
                    $signed({1'b0, destination_width_q}) ?
                    $signed({1'b0, destination_width_q}) :
                    $signed({clip_right_q[15], clip_right_q});
                bottom_bound_q <= $signed(clip_bottom_q) >
                    $signed({1'b0, destination_height_q}) ?
                    $signed({1'b0, destination_height_q}) :
                    $signed({clip_bottom_q[15], clip_bottom_q});
                state <= ST_SEED;
            end

            ST_SEED: begin
                seed_admitted_q <= seed_x_q >= left_bound_q &&
                    seed_x_q < right_bound_q && seed_y_q >= top_bound_q &&
                    seed_y_q < bottom_bound_q &&
                    workspace_capacity_q != 32'd0;
                state <= ST_SEED_ACTIVATE;
            end

            ST_SEED_ACTIVATE: begin
                if (!seed_admitted_q) begin
                    state <= ST_FLUSH;
                end else begin
                    active_x_q <= seed_x_q;
                    issue_pixel_read(seed_x_q, seed_y_q, READ_TARGET);
                end
            end

            ST_ADDRESS_MULTIPLY: begin
                address_start_q <= 1'b1;
                state <= ST_ADDRESS_SUM;
            end

            ST_ADDRESS_SUM: if (address_done_q) begin
                destination_row_address_q <= destination_surface_base_q +
                    row_offset_q;
                state <= ST_ADDRESS_COLUMN;
            end

            ST_ADDRESS_COLUMN: begin
                read_pixel_address_q <= computed_pixel_address;
                read_beat_address_q <= {computed_pixel_address[31:3], 3'b000};
                state <= ST_READ_REQUEST;
            end

            ST_READ_REQUEST: begin
                arvalid_q <= 1'b1;
                state <= ST_READ_RESPONSE;
            end

            ST_READ_RESPONSE: begin
                if (arvalid_q && m_axi_arready)
                    arvalid_q <= 1'b0;
                if (m_axi_rvalid) begin
                    if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        !m_axi_rlast) begin
                        writer_abort <= 1'b1;
                        status <= `ASTRA_RENDER_STATUS_AXI_READ;
                        fault_detail <= read_beat_address_q;
                        state <= ST_ABORT;
                    end else begin
                        read_data_q <= m_axi_rdata;
                        state <= ST_READ_DISPATCH;
                    end
                end
            end

            ST_READ_DISPATCH: begin
                read_pixel_q <= decoded_pixel;
                state <= ST_READ_CLASSIFY;
            end

            ST_READ_CLASSIFY: begin
                read_is_target_q <= read_is_target;
                read_is_replacement_q <= read_is_replacement;
                case (read_return_q)
                    READ_TARGET: state <= ST_TARGET;
                    READ_LEFT: state <= ST_LEFT_RESULT;
                    READ_RIGHT: state <= ST_RIGHT_RESULT;
                    default: state <= ST_NEIGHBOR_RESULT;
                endcase
            end

            ST_TARGET: begin
                if (!target_valid_q) begin
                    target_q <= read_pixel_q;
                    target_valid_q <= 1'b1;
                end
                if (!target_valid_q && read_is_replacement_q) begin
                    state <= ST_FLUSH;
                end else if (target_valid_q && !read_is_target_q) begin
                    if (!stack_nonempty_q) begin
                        state <= ST_FLUSH;
                    end else begin
                        stack_count_q <= stack_count_q - 32'd1;
                        stack_nonempty_q <= stack_count_q != 32'd1;
                        state <= ST_POP;
                    end
                end else begin
                    span_left_q <= active_x_q;
                    left_scan_exhausted_q <= active_x_q == left_bound_q;
                    active_x_q <= active_x_q - 17'sd1;
                    state <= ST_LEFT;
                end
            end

            ST_LEFT: begin
                if (left_scan_exhausted_q) begin
                    active_x_q <= seed_x_q;
                    right_scan_exhausted_q <= seed_x_q >= right_bound_q;
                    state <= ST_RIGHT;
                end else begin
                    issue_pixel_read(active_x_q, seed_y_q, READ_LEFT);
                end
            end

            ST_LEFT_RESULT: begin
                if (read_is_target_q) begin
                    span_left_q <= active_x_q;
                    left_scan_exhausted_q <= active_x_q == left_bound_q;
                    active_x_q <= active_x_q - 17'sd1;
                    state <= ST_LEFT;
                end else begin
                    active_x_q <= seed_x_q;
                    right_scan_exhausted_q <= seed_x_q >= right_bound_q;
                    state <= ST_RIGHT;
                end
            end

            ST_RIGHT: begin
                if (right_scan_exhausted_q) begin
                    span_right_q <= right_bound_q - 17'sd1;
                    neighbor_row_q <= 1'b0;
                    state <= ST_NEIGHBOR_ROW;
                end else begin
                    issue_pixel_read(active_x_q, seed_y_q, READ_RIGHT);
                end
            end

            ST_RIGHT_RESULT: begin
                if (read_is_target_q) begin
                    pixel_address <= read_pixel_address_q;
                    pixel_format <= destination_format_q;
                    pixel_value <= replacement_q;
                    pixel_valid <= 1'b1;
                    state <= ST_WRITE_SPAN;
                end else begin
                    span_right_q <= active_x_q - 17'sd1;
                    neighbor_row_q <= 1'b0;
                    state <= ST_NEIGHBOR_ROW;
                end
            end

            ST_WRITE_SPAN: if (pixel_valid && pixel_ready) begin
                pixel_valid <= 1'b0;
                completed_pixels <= completed_pixels + 32'd1;
                active_x_q <= active_x_q + 17'sd1;
                right_scan_exhausted_q <=
                    active_x_q + 17'sd1 >= right_bound_q;
                state <= ST_RIGHT;
            end

            ST_NEIGHBOR_ROW: begin
                neighbor_y_q <= neighbor_row_q ?
                    seed_y_q + 17'sd1 : seed_y_q - 17'sd1;
                neighbor_x_q <= span_left_q;
                neighbor_run_q <= 1'b0;
                state <= ST_NEIGHBOR_ROW_CLASSIFY;
            end

            ST_NEIGHBOR_ROW_CLASSIFY: begin
                neighbor_row_in_bounds_q <=
                    neighbor_y_q >= top_bound_q &&
                    neighbor_y_q < bottom_bound_q;
                state <= ST_NEIGHBOR_ROW_DECIDE;
            end

            ST_NEIGHBOR_ROW_DECIDE: begin
                if (!neighbor_row_in_bounds_q) begin
                    if (neighbor_row_q)
                        state <= ST_BARRIER;
                    else begin
                        neighbor_row_q <= 1'b1;
                        state <= ST_NEIGHBOR_ROW;
                    end
                end else begin
                    state <= ST_NEIGHBOR;
                end
            end

            ST_NEIGHBOR: begin
                stack_has_capacity_q <=
                    stack_count_q < workspace_capacity_q;
                neighbor_past_span_q <= neighbor_x_q > span_right_q;
                state <= ST_NEIGHBOR_CLASSIFY;
            end

            ST_NEIGHBOR_CLASSIFY: begin
                if (neighbor_past_span_q) begin
                    if (neighbor_row_q)
                        state <= ST_BARRIER;
                    else begin
                        neighbor_row_q <= 1'b1;
                        state <= ST_NEIGHBOR_ROW;
                    end
                end else begin
                    issue_pixel_read(neighbor_x_q, neighbor_y_q,
                                     READ_NEIGHBOR);
                end
            end

            ST_NEIGHBOR_RESULT: begin
                if (read_is_target_q && !neighbor_run_q) begin
                    neighbor_run_q <= 1'b1;
                    if (!stack_has_capacity_q) begin
                        state <= ST_OVERFLOW;
                    end else begin
                        state <= ST_PUSH_PREP;
                    end
                end else begin
                    if (!read_is_target_q)
                        neighbor_run_q <= 1'b0;
                    state <= ST_NEIGHBOR_NEXT;
                end
            end

            ST_OVERFLOW: begin
                writer_abort <= 1'b1;
                status <= `ASTRA_RENDER_STATUS_WORK_OVERFLOW;
                fault_detail <= stack_count_q;
                state <= ST_ABORT;
            end

            ST_PUSH_PREP: begin
                pixel_address <= arena_base_q + workspace_data_offset_q +
                    (stack_count_q << 2);
                pixel_format <= `ASTRA_RENDER_FORMAT_XRGB8888;
                pixel_value <= {neighbor_x_q[15:0], neighbor_y_q[15:0]};
                pixel_valid <= 1'b1;
                state <= ST_PUSH;
            end

            ST_PUSH: if (pixel_valid && pixel_ready) begin
                pixel_valid <= 1'b0;
                stack_count_q <= stack_count_q + 32'd1;
                stack_nonempty_q <= 1'b1;
                state <= ST_NEIGHBOR_NEXT;
            end

            ST_NEIGHBOR_NEXT: begin
                neighbor_x_q <= neighbor_x_q + 17'sd1;
                state <= ST_NEIGHBOR;
            end

            ST_BARRIER: if (writer_barrier_ready_q) begin
                writer_barrier <= 1'b1;
                state <= ST_BARRIER_WAIT;
            end

            ST_BARRIER_WAIT: if (writer_barrier_done) begin
                if (!stack_nonempty_q) begin
                    state <= ST_FLUSH;
                end else begin
                    stack_count_q <= stack_count_q - 32'd1;
                    stack_nonempty_q <= stack_count_q != 32'd1;
                    state <= ST_POP;
                end
            end

            ST_POP: begin
                workspace_read_address_q <= computed_workspace_read_address;
                read_beat_address_q <=
                    {computed_workspace_read_address[31:3], 3'b000};
                arvalid_q <= 1'b1;
                state <= ST_WORKSPACE_REQUEST;
            end

            ST_WORKSPACE_REQUEST: begin
                if (arvalid_q && m_axi_arready) begin
                    arvalid_q <= 1'b0;
                    state <= ST_WORKSPACE_RESPONSE;
                end
            end

            ST_WORKSPACE_RESPONSE: if (m_axi_rvalid) begin
                if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                    !m_axi_rlast) begin
                    writer_abort <= 1'b1;
                    status <= `ASTRA_RENDER_STATUS_AXI_READ;
                    fault_detail <= workspace_read_address_q;
                    state <= ST_ABORT;
                end else begin
                    read_data_q <= m_axi_rdata;
                    state <= ST_WORKSPACE_DECODE;
                end
            end

            ST_WORKSPACE_DECODE: begin
                if (workspace_read_address_q[2]) begin
                    seed_x_q <= {beat_byte(read_data_q, 3'd4),
                                 beat_byte(read_data_q, 3'd5)};
                    seed_y_q <= {beat_byte(read_data_q, 3'd6),
                                 beat_byte(read_data_q, 3'd7)};
                    active_x_q <= $signed({beat_byte(read_data_q, 3'd4),
                                           beat_byte(read_data_q, 3'd5)});
                end else begin
                    seed_x_q <= {beat_byte(read_data_q, 3'd0),
                                 beat_byte(read_data_q, 3'd1)};
                    seed_y_q <= {beat_byte(read_data_q, 3'd2),
                                 beat_byte(read_data_q, 3'd3)};
                    active_x_q <= $signed({beat_byte(read_data_q, 3'd0),
                                           beat_byte(read_data_q, 3'd1)});
                end
                state <= ST_SEED;
            end

            ST_FLUSH: begin
                writer_flush <= 1'b1;
                if (writer_flush && writer_flush_ready) begin
                    writer_flush <= 1'b0;
                    state <= ST_WRITER_DONE;
                end
            end

            ST_WRITER_DONE: if (writer_done || writer_aborted || writer_error) begin
                if (writer_error) begin
                    status <= `ASTRA_RENDER_STATUS_AXI_WRITE;
                    fault_detail <= writer_fault_detail;
                end else if (writer_aborted &&
                             status == `ASTRA_RENDER_STATUS_OK) begin
                    status <= `ASTRA_RENDER_STATUS_RESET;
                end
                busy <= 1'b0;
                done <= 1'b1;
                state <= ST_IDLE;
            end

            ST_ABORT: if (writer_done || writer_aborted || writer_error) begin
                busy <= 1'b0;
                done <= 1'b1;
                state <= ST_IDLE;
            end

            default: begin
                writer_abort <= 1'b1;
                status <= `ASTRA_RENDER_STATUS_RESET;
                fault_detail <= 32'h464c4f44;
                state <= ST_ABORT;
            end
        endcase
    end
endmodule

`default_nettype wire
