// Copyright (c) 2026 Astra68 contributors
//
// Builds one complete framebuffer scanline from bounded, ordered AXI
// bursts. Surface bytes use Astra's explicit byte order, independent of the
// little-endian ARM host and AXI byte lanes.
`timescale 1ns/1ps
`default_nettype none
`include "astra_window_scene_protocol.vh"

module astra_framebuffer_line_builder #(
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720,
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = {AXI_ID_WIDTH{1'b0}},
    parameter integer MAX_BUILD_CYCLES = 20000,
    parameter integer MAX_BURST_BEATS = 16,
    parameter integer AXI_DATA_WIDTH = 64,
    parameter integer TRUSTED_CONFIG = 0
) (
    input  wire                         build_clk,
    input  wire                         build_reset,

    input  wire                         start,
    input  wire                         scene_changed,
    input  wire [1:0]                   build_slot,
    input  wire [10:0]                  line_y,
    input  wire                         window_scene,
    input  wire [31:0]                  window_scene_bytes,
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
    input  wire [AXI_DATA_WIDTH-1:0]    m_axi_rdata,
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

    localparam [5:0] ST_IDLE = 6'd0;
    localparam [5:0] ST_VALIDATE = 6'd1;
    localparam [5:0] ST_PLAN_Y = 6'd2;
    localparam [5:0] ST_PLAN_Y_MULTIPLY = 6'd3;
    localparam [5:0] ST_PLAN_Y_COMBINE = 6'd4;
    localparam [5:0] ST_PLAN_Y_ADDRESS = 6'd5;
    localparam [5:0] ST_PLAN_X_SIGN = 6'd6;
    localparam [5:0] ST_PLAN_X_CLIP = 6'd7;
    localparam [5:0] ST_PLAN_X_AVAILABLE = 6'd8;
    localparam [5:0] ST_PLAN_X_COUNTS = 6'd9;
    localparam [5:0] ST_PLAN_X_RIGHT = 6'd10;
    localparam [5:0] ST_PLAN_X_DISPATCH = 6'd11;
    localparam [5:0] ST_FILL_LEFT = 6'd12;
    localparam [5:0] ST_SEGMENT_ADDRESS = 6'd13;
    localparam [5:0] ST_SEGMENT = 6'd14;
    localparam [5:0] ST_SEGMENT_DRAIN = 6'd15;
    localparam [5:0] ST_FILL_RIGHT = 6'd16;
    localparam [5:0] ST_FINISH = 6'd17;
    localparam [5:0] ST_PLAN_Y_RESOLVE = 6'd18;
    localparam [5:0] ST_SEGMENT_COUNTS = 6'd19;
    localparam [5:0] ST_SCENE_HEADER_REQUEST = 6'd20;
    localparam [5:0] ST_SCENE_HEADER_RESPONSE = 6'd21;
    localparam [5:0] ST_SCENE_HEADER_CHECK = 6'd22;
    localparam [5:0] ST_SCENE_LINE_REQUEST = 6'd23;
    localparam [5:0] ST_SCENE_LINE_RESPONSE = 6'd24;
    localparam [5:0] ST_SCENE_LINE_CHECK = 6'd25;
    localparam [5:0] ST_SCENE_STREAM = 6'd26;

    localparam integer BURST_FIFO_DEPTH = 8;
    localparam integer BEAT_BYTES = AXI_DATA_WIDTH / 8;
    localparam integer BEAT_SHIFT = $clog2(BEAT_BYTES);
    localparam integer BURST_BEAT_WIDTH = $clog2(MAX_BURST_BEATS + 1);
    // The beat FIFO is the read credit: a line writes four bytes a clock, so
    // the bytes in flight must cover that rate for a whole round trip. Two
    // bursts (1 KiB at 128 bits) covered ~250 cycles; under render load the
    // DE25's LPDDR4B round trip reaches ~300-500 build cycles (p99 321, max
    // 496 sampled), which held a 1920-pixel line to ~2.5 bytes a clock and
    // stretched builds past the line period for the length of each render
    // command. Eight bursts (4 KiB) cover ~1,000 cycles.
    localparam integer BEAT_FIFO_DEPTH = MAX_BURST_BEATS * 8;
    localparam integer BEAT_FIFO_ADDR_WIDTH = $clog2(BEAT_FIFO_DEPTH);
    localparam integer BEAT_FIFO_COUNT_WIDTH =
        $clog2(BEAT_FIFO_DEPTH + 1);

    // A window-scene line is built as a stream rather than span by span.
    // The line's span records are requested in bursts and decoded into a
    // descriptor ring as they arrive; an issue walker follows the ring and
    // requests each source span's pixels under the beat-FIFO credit, and the
    // writer follows it again, filling solid spans and writing source spans
    // from the FIFO. Responses return in request order on the single read
    // ID, so a per-burst tag sends record beats to the decoder and pixel
    // beats to the FIFO. A line then costs its write cycles plus about one
    // memory round trip for its records and one for its first segment. The
    // serial form paid two round trips a span, and eleven spans against
    // the DE25's LPDDR4B latency overran the line period.
    localparam integer SCENE_RING = 16;
    localparam integer RECORD_BEATS = 32 / BEAT_BYTES;
    localparam integer RECORDS_PER_BURST =
        MAX_BURST_BEATS / RECORD_BEATS < SCENE_RING ?
            MAX_BURST_BEATS / RECORD_BEATS : SCENE_RING;
    localparam [1:0] WRITE_IDLE = 2'd0;
    localparam [1:0] WRITE_SOLID = 2'd1;
    localparam [1:0] WRITE_SOURCE = 2'd2;

    function automatic [7:0] beat_byte(
        input [AXI_DATA_WIDTH-1:0] beat,
        input [BEAT_SHIFT-1:0]     byte_index
    );
        begin
            beat_byte = beat[byte_index * 8 +: 8];
        end
    endfunction

    function automatic [31:0] beat_be32(
        input [AXI_DATA_WIDTH-1:0] beat,
        input [1:0]                word_index
    );
        begin
            beat_be32 = {
                beat[word_index * 32 +: 8],
                beat[word_index * 32 + 8 +: 8],
                beat[word_index * 32 + 16 +: 8],
                beat[word_index * 32 + 24 +: 8]
            };
        end
    endfunction

    reg [5:0] state;
    reg [1:0] build_slot_q;
    reg [10:0] line_y_q;
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
    reg window_scene_q;
    reg [31:0] window_scene_bytes_q;

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
        $signed({22'd0, line_y_q});
    wire [13:0] wrapped_y_sum =
    {1'b0, viewport_y_q[12:0]} + {3'd0, line_y_q};
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

    reg [3:0] scene_metadata_beat_q;
    reg [31:0] scene_total_bytes_q;
    reg [31:0] scene_generation_q;
    reg [31:0] scene_width_height_q;
    reg [31:0] scene_line_count_q;
    reg [31:0] scene_line_offset_q;
    reg [31:0] scene_span_offset_q;
    reg [31:0] scene_span_count_q;
    reg [31:0] scene_flags_q;
    reg scene_header_reserved_valid_q;
    reg scene_header_valid_q;
    reg [31:0] scene_header_base_q;
    reg [31:0] scene_header_capacity_q;
    reg [31:0] scene_first_span_q;
    reg [31:0] scene_line_span_count_q;
    reg [31:0] scene_span_value_q;

    // Stream counters index the line's spans; continuity bounds a line to
    // OUTPUT_WIDTH spans.
    reg [31:0] scene_record_address_q;
    reg [11:0] scene_records_remaining_q;
    reg [11:0] scene_records_requested_q;
    reg [4:0]  scene_record_burst_q;
    reg [11:0] scene_decoded_q;
    reg [11:0] scene_issue_index_q;
    reg        scene_issue_active_q;
    reg [11:0] scene_write_index_q;
    reg [1:0]  scene_write_mode_q;

    // Record assembly and the decode pipeline. Records arrive at most one
    // beat a clock; every stage takes one clock, so a record needs no stall.
    reg [1:0]  record_beat_q;
    reg        record_valid_q;
    reg [31:0] record_w0_q, record_w1_q, record_w2_q, record_w3_q;
    reg [31:0] record_w4_q, record_w5_q, record_w6_q, record_w7_q;
    reg [10:0] decode_run_x_q;

    reg        decode1_valid_q;
    reg        decode1_solid_q;
    reg        decode1_solid_ok_q;
    reg        decode1_source_flags_ok_q;
    reg        decode1_reserved_ok_q;
    reg        decode1_continuous_q;
    reg [15:0] decode1_length_q;
    reg [16:0] decode1_destination_end_q;
    reg [15:0] decode1_value_q;
    reg [31:0] decode1_offset_q;
    reg [31:0] decode1_bytes_q;
    reg [31:0] decode1_pitch_q;
    reg [15:0] decode1_source_x_q;
    reg [16:0] decode1_row_pixels_q;
    reg [32:0] decode1_source_end_q;
    reg [31:0] decode1_product_low_q;
    reg [31:0] decode1_product_high_q;

    reg        decode2_valid_q;
    reg        decode2_error_q;
    reg        decode2_solid_q;
    reg [10:0] decode2_length_q;
    reg [15:0] decode2_value_q;
    reg [31:0] decode2_offset_q;
    reg [31:0] decode2_bytes_q;
    reg [15:0] decode2_source_x_q;
    reg [16:0] decode2_row_pixels_q;
    reg [47:0] decode2_product_q;

    reg        decode3_valid_q;
    reg        decode3_error_q;
    reg        decode3_solid_q;
    reg [10:0] decode3_length_q;
    reg [15:0] decode3_value_q;
    reg [31:0] decode3_row_address_q;

    reg        ring_solid [0:SCENE_RING-1];
    reg [15:0] ring_value [0:SCENE_RING-1];
    reg [10:0] ring_length [0:SCENE_RING-1];
    reg [31:0] ring_address [0:SCENE_RING-1];
    reg [BEAT_SHIFT-1:0] ring_first_byte [0:SCENE_RING-1];
    reg [10:0] ring_beats [0:SCENE_RING-1];

    wire [36:0] scene_line_table_end =
        {5'd0, scene_line_offset_q} +
        ({5'd0, scene_line_count_q} << 3);
    wire [37:0] scene_span_table_end =
        {6'd0, scene_span_offset_q} +
        ({6'd0, scene_span_count_q} << 5);
    wire [32:0] scene_line_span_end =
        {1'b0, scene_first_span_q} + {1'b0, scene_line_span_count_q};
    wire [32:0] scene_compiled_end =
        {1'b0, framebuffer_base_q} + {1'b0, window_scene_bytes_q};
    wire [31:0] scene_line_address = framebuffer_base_q +
        scene_line_offset_q + ({21'd0, line_y_q} << 3);

    wire [31:0] segment_byte_address =
        row_address_q + ({19'd0, source_x_q} << bytes_shift_q);
    wire [13:0] segment_payload_bytes =
        {3'd0, mapped_pixels_q} << bytes_shift_q;

    reg [31:0] issue_address;
    reg [10:0] issue_beats_remaining;
    reg [BEAT_SHIFT-1:0] first_byte_offset;
    reg [13:0] segment_payload_bytes_q;
    reg ar_request_valid;
    reg [31:0] ar_request_address;
    reg [BURST_BEAT_WIDTH-1:0] ar_request_beats;

    // Requests are beat-aligned and no longer than the selected
    // AXI port limit. Only the final maximum-burst region of a 4 KiB page can
    // shorten a request, so avoid a full 13-bit boundary subtractor.
    wire [BURST_BEAT_WIDTH-1:0] issue_beats_to_4k_capped =
        MAX_BURST_BEATS == 32 ?
            (BEAT_BYTES == 16 ?
                (&issue_address[11:9] ?
                    6'd32 - {1'b0, issue_address[8:4]} : 6'd32) :
                (&issue_address[11:8] ?
                    6'd32 - {1'b0, issue_address[7:3]} : 6'd32)) :
            (BEAT_BYTES == 16 ?
                (&issue_address[11:8] ?
                    5'd16 - {1'b0, issue_address[7:4]} : 5'd16) :
                (&issue_address[11:7] ?
                    5'd16 - {1'b0, issue_address[6:3]} : 5'd16));
    wire [BURST_BEAT_WIDTH-1:0] selected_burst_beats =
        issue_beats_remaining < MAX_BURST_BEATS ?
            issue_beats_remaining[BURST_BEAT_WIDTH-1:0] :
            issue_beats_to_4k_capped;

    reg [BURST_BEAT_WIDTH-1:0] burst_fifo [0:BURST_FIFO_DEPTH-1];
    reg [BURST_BEAT_WIDTH-1:0] burst_head_beats;
    reg [2:0] burst_write_ptr;
    reg [2:0] burst_read_ptr;
    reg [3:0] burst_count;
    reg [BEAT_FIFO_COUNT_WIDTH-1:0] reserved_beats;
    reg response_active;
    reg [BURST_BEAT_WIDTH-1:0] response_beats_left;
    // Metadata bursts (header, line entry, span records) carry no pixel
    // beats and take no beat-FIFO credit.
    reg burst_tag_fifo [0:BURST_FIFO_DEPTH-1];
    reg burst_head_meta;
    reg ar_request_meta;

    wire [BEAT_FIFO_COUNT_WIDTH-1:0] selected_burst_beats_wide =
        {{(BEAT_FIFO_COUNT_WIDTH-BURST_BEAT_WIDTH){1'b0}},
         selected_burst_beats};
    wire [BEAT_FIFO_COUNT_WIDTH-1:0] ar_request_beats_wide =
        {{(BEAT_FIFO_COUNT_WIDTH-BURST_BEAT_WIDTH){1'b0}},
         ar_request_beats};
    wire issue_credit_available =
        reserved_beats + selected_burst_beats_wide <= BEAT_FIFO_DEPTH;
    wire ar_plan = state == ST_SEGMENT && !ar_request_valid &&
        issue_beats_remaining != 11'd0 &&
        burst_count < BURST_FIFO_DEPTH && issue_credit_available;

    // A ring slot is free once both walkers have passed it.
    wire [11:0] scene_ring_consumed =
        scene_issue_index_q < scene_write_index_q ?
            scene_issue_index_q : scene_write_index_q;
    wire [4:0] scene_ring_used =
        scene_records_requested_q[4:0] - scene_ring_consumed[4:0];
    wire [4:0] scene_ring_free = SCENE_RING[4:0] - scene_ring_used;
    wire [4:0] scene_records_wanted =
        scene_records_remaining_q < RECORDS_PER_BURST ?
            scene_records_remaining_q[4:0] : RECORDS_PER_BURST[4:0];
    wire [7:0] scene_records_to_4k =
        8'd128 - {1'b0, scene_record_address_q[11:5]};
    wire [4:0] scene_records_capped =
        scene_records_wanted < scene_ring_free ?
            scene_records_wanted : scene_ring_free;
    wire [4:0] scene_record_burst =
        {3'd0, scene_records_capped} > scene_records_to_4k ?
            scene_records_to_4k[4:0] : scene_records_capped;
    wire scene_stream_abort = config_error || fetch_error;
    wire scene_record_plan = state == ST_SCENE_STREAM &&
        !scene_stream_abort && !ar_request_valid &&
        scene_record_burst != 5'd0 && burst_count < BURST_FIFO_DEPTH;
    wire scene_pixel_plan = state == ST_SCENE_STREAM &&
        !scene_stream_abort && !ar_request_valid && !scene_record_plan &&
        scene_issue_active_q && issue_beats_remaining != 11'd0 &&
        burst_count < BURST_FIFO_DEPTH && issue_credit_available;
    wire [3:0] scene_issue_slot = scene_issue_index_q[3:0];
    wire [3:0] scene_write_slot = scene_write_index_q[3:0];
    wire [3:0] scene_decode_slot = scene_decoded_q[3:0];
    assign m_axi_arvalid = ar_request_valid;
    assign m_axi_arid = AXI_ID;
    assign m_axi_araddr = ar_request_address;
    assign m_axi_arlen = ar_request_beats - 1'b1;
    assign m_axi_arsize = BEAT_SHIFT[2:0];
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    // The memory controller may implement QoS as strict priority.  The
    // framebuffer is continuous traffic, so a nonzero value can starve HPS
    // accesses indefinitely even when memory bandwidth remains available.
    assign m_axi_arqos = 4'b0000;
    wire ar_accept = m_axi_arvalid && m_axi_arready;

    reg [AXI_DATA_WIDTH-1:0] beat_fifo [0:BEAT_FIFO_DEPTH-1];
    reg [BEAT_FIFO_ADDR_WIDTH-1:0] beat_write_ptr;
    reg [BEAT_FIFO_ADDR_WIDTH-1:0] beat_read_ptr;
    reg [BEAT_FIFO_COUNT_WIDTH-1:0] beat_count;

    wire [BURST_BEAT_WIDTH-1:0] expected_response_beats = response_active ?
        response_beats_left : burst_head_beats;
    wire expected_response_last = expected_response_beats == 5'd1;
    // AXI reads cannot be cancelled after AR acceptance.  A completed or
    // timed-out line therefore keeps RREADY asserted while discarding every
    // remaining response before the builder can be reused.
    wire scene_metadata_response =
        state == ST_SCENE_HEADER_RESPONSE ||
        state == ST_SCENE_LINE_RESPONSE;
    assign m_axi_rready = burst_count != 4'd0 &&
        (state == ST_SEGMENT || state == ST_SEGMENT_DRAIN ||
         state == ST_SCENE_STREAM || scene_metadata_response);
    wire response_accept = m_axi_rvalid && m_axi_rready;
    wire burst_pop = response_accept &&
        (m_axi_rlast || expected_response_last);
    wire beat_push = response_accept && !burst_head_meta &&
        (state == ST_SEGMENT || state == ST_SCENE_STREAM);
    wire record_beat = response_accept && burst_head_meta &&
        state == ST_SCENE_STREAM;
    wire segment_writing = state == ST_SEGMENT ||
        (state == ST_SCENE_STREAM && scene_write_mode_q == WRITE_SOURCE);

    reg beat_active;

    wire [5:0] beat_count_debug = beat_count > 63 ? 6'd63 : beat_count[5:0];
    wire [5:0] reserved_beats_debug =
        reserved_beats > 63 ? 6'd63 : reserved_beats[5:0];
    assign axi_debug_status = {
        issue_beats_remaining != 11'd0,
        beat_active,
        state == ST_SEGMENT_DRAIN,
        response_active,
        beat_count_debug,
        reserved_beats_debug,
        burst_count,
        m_axi_rready,
        m_axi_rvalid,
        m_axi_arready,
        m_axi_arvalid,
        fetch_error,
        deadline_error,
        busy,
        state[4:0]
    };

    reg first_beat;
    // Keep this register outside the BRAM output stage. The integrated route
    // otherwise absorbs it into DO_REG and leaves byte selection on the
    // BRAM's 2.45 ns clock-to-output path.
    (* dont_touch = "yes" *) reg [AXI_DATA_WIDTH-1:0] active_beat;
    reg [BEAT_SHIFT-1:0] active_byte;
    wire mapped_write_two = segment_writing && beat_active &&
        segment_pixels_active_q && segment_pixels_remaining >= 11'd2 &&
        {1'b0, active_byte} + ({1'b0, bytes_per_pixel_q} << 1) <= BEAT_BYTES;
    wire [1:0] mapped_write_count = mapped_write_two ? 2'd2 : 2'd1;
    wire [BEAT_SHIFT:0] active_bytes_consumed = mapped_write_two ?
        ({1'b0, bytes_per_pixel_q} << 1) : {1'b0, bytes_per_pixel_q};
    wire [BEAT_SHIFT:0] active_byte_after_write =
        {1'b0, active_byte} + active_bytes_consumed;
    wire active_beat_exhausted = beat_active &&
        segment_pixels_active_q &&
        segment_pixels_remaining > mapped_write_count &&
        active_byte_after_write >= BEAT_BYTES;
    wire beat_pop = segment_writing && beat_count != 0 &&
                    (!beat_active || active_beat_exhausted);

    wire [7:0] active_byte0 = beat_byte(active_beat, active_byte);
    wire [7:0] active_byte1 = beat_byte(active_beat, active_byte + 3'd1);
    wire [7:0] active_byte2 = beat_byte(active_beat, active_byte + 3'd2);
    wire [7:0] active_byte3 = beat_byte(active_beat, active_byte + 3'd3);
    wire [BEAT_SHIFT-1:0] second_active_byte =
        active_byte + bytes_per_pixel_q;
    wire [7:0] second_byte0 = beat_byte(active_beat, second_active_byte);
    wire [7:0] second_byte1 = beat_byte(active_beat,
                                        second_active_byte + 3'd1);
    wire [7:0] second_byte2 = beat_byte(active_beat,
                                        second_active_byte + 3'd2);
    wire [7:0] second_byte3 = beat_byte(active_beat,
                                        second_active_byte + 3'd3);

    wire mapped_write = segment_writing && beat_active &&
                        segment_pixels_active_q;
    wire invalid_write = (state == ST_FILL_LEFT ||
                          state == ST_FILL_RIGHT) && fill_pixels_q != 11'd0;
    wire scene_solid_write = state == ST_SCENE_STREAM &&
                             scene_write_mode_q == WRITE_SOLID &&
                             fill_pixels_q != 11'd0;
    wire line_write = mapped_write || invalid_write || scene_solid_write;
    wire line_write_two = mapped_write ? mapped_write_two :
                          fill_pixels_q >= 11'd2;
    wire [1:0] fill_write_count = line_write_two ? 2'd2 : 2'd1;

    // Byte selection is the expensive half of source decoding. Register it
    // separately from the format mux so neither stage exceeds two LUT levels.
    reg [1:0]  line_write_enable_q;
    reg        line_write_mapped_q;
    reg [1:0]  line_write_slot_q;
    reg [10:0] line_write_x_q;
    reg [1:0]  line_write_format_q;
    reg [7:0]  line_write_byte0_q;
    reg [7:0]  line_write_byte1_q;
    reg [7:0]  line_write_byte2_q;
    reg [7:0]  line_write_byte3_q;
    reg [7:0]  line_write_second_byte0_q;
    reg [7:0]  line_write_second_byte1_q;
    reg [7:0]  line_write_second_byte2_q;
    reg [7:0]  line_write_second_byte3_q;

    wire [31:0] line_write_decoded =
        line_write_format_q == FORMAT_INDEX8 ?
            {24'd0, line_write_byte0_q} :
        line_write_format_q == FORMAT_RGB565 ?
            {16'd0, line_write_byte0_q, line_write_byte1_q} :
            {8'hff, line_write_byte1_q, line_write_byte2_q,
             line_write_byte3_q};
    wire [32:0] line_write_pixel = line_write_mapped_q ?
        {1'b1, line_write_decoded} : 33'd0;
    wire [31:0] line_write_second_decoded =
        line_write_format_q == FORMAT_INDEX8 ?
            {24'd0, line_write_second_byte0_q} :
        line_write_format_q == FORMAT_RGB565 ?
            {16'd0, line_write_second_byte0_q,
             line_write_second_byte1_q} :
            {8'hff, line_write_second_byte1_q,
             line_write_second_byte2_q, line_write_second_byte3_q};
    wire [32:0] line_write_second_pixel = line_write_mapped_q ?
        {1'b1, line_write_second_decoded} : 33'd0;

    astra_framebuffer_line_store #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH)
    ) line_store_i (
        .build_clk(build_clk),
        .write_enable(line_write_enable_q),
        .write_slot(line_write_slot_q),
        .write_x(line_write_x_q),
        .write_pixel0(line_write_pixel),
        .write_pixel1(line_write_second_pixel),
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
            line_y_q <= 11'd0;
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
            window_scene_q <= 1'b0;
            window_scene_bytes_q <= 32'd0;
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
            scene_metadata_beat_q <= 4'd0;
            scene_total_bytes_q <= 32'd0;
            scene_generation_q <= 32'd0;
            scene_width_height_q <= 32'd0;
            scene_line_count_q <= 32'd0;
            scene_line_offset_q <= 32'd0;
            scene_span_offset_q <= 32'd0;
            scene_span_count_q <= 32'd0;
            scene_flags_q <= 32'd0;
            scene_header_reserved_valid_q <= 1'b0;
            scene_header_valid_q <= 1'b0;
            scene_header_base_q <= 32'd0;
            scene_header_capacity_q <= 32'd0;
            scene_first_span_q <= 32'd0;
            scene_line_span_count_q <= 32'd0;
            scene_span_value_q <= 32'd0;
            scene_record_address_q <= 32'd0;
            scene_records_remaining_q <= 12'd0;
            scene_records_requested_q <= 12'd0;
            scene_record_burst_q <= 5'd0;
            scene_decoded_q <= 12'd0;
            scene_issue_index_q <= 12'd0;
            scene_issue_active_q <= 1'b0;
            scene_write_index_q <= 12'd0;
            scene_write_mode_q <= WRITE_IDLE;
            record_beat_q <= 2'd0;
            record_valid_q <= 1'b0;
            decode_run_x_q <= 11'd0;
            decode1_valid_q <= 1'b0;
            decode2_valid_q <= 1'b0;
            decode3_valid_q <= 1'b0;
            issue_address <= 32'd0;
            issue_beats_remaining <= 11'd0;
            first_byte_offset <= 0;
            segment_payload_bytes_q <= 14'd0;
            ar_request_valid <= 1'b0;
            ar_request_address <= 32'd0;
            ar_request_beats <= 0;
            burst_head_beats <= 0;
            burst_head_meta <= 1'b0;
            ar_request_meta <= 1'b0;
            burst_write_ptr <= 3'd0;
            burst_read_ptr <= 3'd0;
            burst_count <= 4'd0;
            reserved_beats <= 0;
            response_active <= 1'b0;
            response_beats_left <= 0;
            beat_write_ptr <= 0;
            beat_read_ptr <= 0;
            beat_count <= 0;
            beat_active <= 1'b0;
            first_beat <= 1'b0;
            active_beat <= {AXI_DATA_WIDTH{1'b0}};
            active_byte <= 0;
            line_write_enable_q <= 2'b00;
            line_write_mapped_q <= 1'b0;
            line_write_slot_q <= 2'd0;
            line_write_x_q <= 11'd0;
            line_write_format_q <= FORMAT_INDEX8;
            line_write_byte0_q <= 8'd0;
            line_write_byte1_q <= 8'd0;
            line_write_byte2_q <= 8'd0;
            line_write_byte3_q <= 8'd0;
            line_write_second_byte0_q <= 8'd0;
            line_write_second_byte1_q <= 8'd0;
            line_write_second_byte2_q <= 8'd0;
            line_write_second_byte3_q <= 8'd0;
        end else begin
            if (scene_changed)
                scene_header_valid_q <= 1'b0;
            done <= 1'b0;
            line_complete <= 1'b0;

            line_write_enable_q <= line_write ?
                (line_write_two ? 2'b11 : 2'b01) : 2'b00;
            if (line_write) begin
                line_write_mapped_q <= mapped_write || scene_solid_write;
                line_write_slot_q <= build_slot_q;
                line_write_x_q <= output_x_q;
                line_write_format_q <= scene_solid_write ?
                    FORMAT_RGB565 : format_q;
                line_write_byte0_q <= scene_solid_write ?
                    scene_span_value_q[15:8] : active_byte0;
                line_write_byte1_q <= scene_solid_write ?
                    scene_span_value_q[7:0] : active_byte1;
                line_write_byte2_q <= scene_solid_write ? 8'd0 : active_byte2;
                line_write_byte3_q <= scene_solid_write ? 8'd0 : active_byte3;
                line_write_second_byte0_q <= scene_solid_write ?
                    scene_span_value_q[15:8] : second_byte0;
                line_write_second_byte1_q <= scene_solid_write ?
                    scene_span_value_q[7:0] : second_byte1;
                line_write_second_byte2_q <= scene_solid_write ?
                    8'd0 : second_byte2;
                line_write_second_byte3_q <= scene_solid_write ?
                    8'd0 : second_byte3;
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

            record_valid_q <= 1'b0;
            if (record_beat) begin
                if (AXI_DATA_WIDTH == 128) begin
                    if (record_beat_q == 2'd0) begin
                        record_w0_q <= beat_be32(m_axi_rdata, 2'd0);
                        record_w1_q <= beat_be32(m_axi_rdata, 2'd1);
                        record_w2_q <= beat_be32(m_axi_rdata, 2'd2);
                        record_w3_q <= beat_be32(m_axi_rdata, 2'd3);
                    end else begin
                        record_w4_q <= beat_be32(m_axi_rdata, 2'd0);
                        record_w5_q <= beat_be32(m_axi_rdata, 2'd1);
                        record_w6_q <= beat_be32(m_axi_rdata, 2'd2);
                        record_w7_q <= beat_be32(m_axi_rdata, 2'd3);
                    end
                end else begin
                    case (record_beat_q)
                        2'd0: begin
                            record_w0_q <= beat_be32(m_axi_rdata, 2'd0);
                            record_w1_q <= beat_be32(m_axi_rdata, 2'd1);
                        end
                        2'd1: begin
                            record_w2_q <= beat_be32(m_axi_rdata, 2'd0);
                            record_w3_q <= beat_be32(m_axi_rdata, 2'd1);
                        end
                        2'd2: begin
                            record_w4_q <= beat_be32(m_axi_rdata, 2'd0);
                            record_w5_q <= beat_be32(m_axi_rdata, 2'd1);
                        end
                        default: begin
                            record_w6_q <= beat_be32(m_axi_rdata, 2'd0);
                            record_w7_q <= beat_be32(m_axi_rdata, 2'd1);
                        end
                    endcase
                end
                if (record_beat_q == RECORD_BEATS - 1) begin
                    record_beat_q <= 2'd0;
                    record_valid_q <= 1'b1;
                end else begin
                    record_beat_q <= record_beat_q + 2'd1;
                end
            end

            if (response_accept) begin
                if (beat_push) begin
                    beat_fifo[beat_write_ptr] <= m_axi_rdata;
                    beat_write_ptr <= beat_write_ptr + 1'b1;
                end
                if (scene_metadata_response) begin
                    scene_metadata_beat_q <= scene_metadata_beat_q + 4'd1;
                    case (state)
                        ST_SCENE_HEADER_RESPONSE: begin
                            if (AXI_DATA_WIDTH == 128) begin
                                case (scene_metadata_beat_q)
                                    4'd0: begin
                                        if (beat_be32(m_axi_rdata, 2'd0) !=
                                                `ASTRA_WINDOW_SCENE_COMPILED_MAGIC ||
                                            beat_be32(m_axi_rdata, 2'd1) !=
                                                `ASTRA_WINDOW_SCENE_COMPILED_VERSION)
                                            scene_header_reserved_valid_q <= 1'b0;
                                        scene_total_bytes_q <=
                                            beat_be32(m_axi_rdata, 2'd2);
                                        scene_generation_q <=
                                            beat_be32(m_axi_rdata, 2'd3);
                                    end
                                    4'd1: begin
                                        scene_width_height_q <=
                                            beat_be32(m_axi_rdata, 2'd0);
                                        scene_line_count_q <=
                                            beat_be32(m_axi_rdata, 2'd1);
                                        scene_line_offset_q <=
                                            beat_be32(m_axi_rdata, 2'd2);
                                        scene_span_offset_q <=
                                            beat_be32(m_axi_rdata, 2'd3);
                                    end
                                    4'd2: begin
                                        scene_span_count_q <=
                                            beat_be32(m_axi_rdata, 2'd0);
                                        scene_flags_q <=
                                            beat_be32(m_axi_rdata, 2'd1);
                                        if ((m_axi_rdata >> 64) != 0)
                                            scene_header_reserved_valid_q <= 1'b0;
                                    end
                                    default:
                                        if (m_axi_rdata != 128'd0)
                                            scene_header_reserved_valid_q <= 1'b0;
                                endcase
                            end else begin
                                case (scene_metadata_beat_q)
                                    4'd0: if (beat_be32(m_axi_rdata, 2'd0) !=
                                            `ASTRA_WINDOW_SCENE_COMPILED_MAGIC ||
                                        beat_be32(m_axi_rdata, 2'd1) !=
                                            `ASTRA_WINDOW_SCENE_COMPILED_VERSION)
                                        scene_header_reserved_valid_q <= 1'b0;
                                    4'd1: begin
                                        scene_total_bytes_q <=
                                            beat_be32(m_axi_rdata, 2'd0);
                                        scene_generation_q <=
                                            beat_be32(m_axi_rdata, 2'd1);
                                    end
                                    4'd2: begin
                                        scene_width_height_q <=
                                            beat_be32(m_axi_rdata, 2'd0);
                                        scene_line_count_q <=
                                            beat_be32(m_axi_rdata, 2'd1);
                                    end
                                    4'd3: begin
                                        scene_line_offset_q <=
                                            beat_be32(m_axi_rdata, 2'd0);
                                        scene_span_offset_q <=
                                            beat_be32(m_axi_rdata, 2'd1);
                                    end
                                    4'd4: begin
                                        scene_span_count_q <=
                                            beat_be32(m_axi_rdata, 2'd0);
                                        scene_flags_q <=
                                            beat_be32(m_axi_rdata, 2'd1);
                                    end
                                    default: if (m_axi_rdata[63:0] != 64'd0)
                                        scene_header_reserved_valid_q <= 1'b0;
                                endcase
                            end
                        end
                        ST_SCENE_LINE_RESPONSE: begin
                            scene_first_span_q <=
                                beat_be32(m_axi_rdata,
                                    AXI_DATA_WIDTH == 128 &&
                                    scene_line_address[3] ? 2'd2 : 2'd0);
                            scene_line_span_count_q <=
                                beat_be32(m_axi_rdata,
                                    AXI_DATA_WIDTH == 128 &&
                                    scene_line_address[3] ? 2'd3 : 2'd1);
                        end
                        default: begin end
                    endcase
                end
                if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                    m_axi_rlast != expected_response_last)
                    fetch_error <= 1'b1;

                if (burst_pop) begin
                    response_active <= 1'b0;
                    response_beats_left <= 0;
                end else begin
                    response_active <= 1'b1;
                    response_beats_left <= expected_response_beats - 1'b1;
                end
            end

            case ({ar_accept, burst_pop})
                2'b10: begin
                    burst_count <= burst_count + 4'd1;
                    if (burst_count == 4'd0) begin
                        burst_head_beats <= ar_request_beats;
                        burst_head_meta <= ar_request_meta;
                    end else begin
                        burst_fifo[burst_write_ptr] <= ar_request_beats;
                        burst_tag_fifo[burst_write_ptr] <= ar_request_meta;
                        burst_write_ptr <= burst_write_ptr + 3'd1;
                    end
                end
                2'b01: begin
                    burst_count <= burst_count - 4'd1;
                    if (burst_count > 4'd1) begin
                        burst_head_beats <= burst_fifo[burst_read_ptr];
                        burst_head_meta <= burst_tag_fifo[burst_read_ptr];
                        burst_read_ptr <= burst_read_ptr + 3'd1;
                    end else begin
                        burst_head_beats <= 0;
                        burst_head_meta <= 1'b0;
                    end
                end
                2'b11: begin
                    if (burst_count > 4'd1) begin
                        burst_head_beats <= burst_fifo[burst_read_ptr];
                        burst_head_meta <= burst_tag_fifo[burst_read_ptr];
                        burst_read_ptr <= burst_read_ptr + 3'd1;
                        burst_fifo[burst_write_ptr] <= ar_request_beats;
                        burst_tag_fifo[burst_write_ptr] <= ar_request_meta;
                        burst_write_ptr <= burst_write_ptr + 3'd1;
                    end else begin
                        burst_head_beats <= ar_request_beats;
                        burst_head_meta <= ar_request_meta;
                    end
                end
                default: begin end
            endcase

            case ({ar_accept && !ar_request_meta, beat_pop})
                2'b10: reserved_beats <=
                    reserved_beats + ar_request_beats_wide;
                2'b01: reserved_beats <= reserved_beats - 1'b1;
                2'b11: reserved_beats <=
                    reserved_beats + ar_request_beats_wide - 1'b1;
                default: begin end
            endcase

            if (state == ST_SEGMENT) begin
                if (ar_plan) begin
                    ar_request_valid <= 1'b1;
                    ar_request_meta <= 1'b0;
                    ar_request_address <= issue_address;
                    ar_request_beats <= selected_burst_beats;
                end

                if (ar_accept) begin
                    ar_request_valid <= 1'b0;
                    issue_address <= issue_address +
                        ({{(32-BURST_BEAT_WIDTH){1'b0}},
                          ar_request_beats} << BEAT_SHIFT);
                    issue_beats_remaining <= issue_beats_remaining -
                        ar_request_beats;
                    read_bytes <= read_bytes +
                        ({{(32-BURST_BEAT_WIDTH){1'b0}},
                          ar_request_beats} << BEAT_SHIFT);
                end
            end

            // An accepted address cannot be withdrawn; a drain waits for it.
            if (state == ST_SEGMENT_DRAIN && ar_accept)
                ar_request_valid <= 1'b0;

            if (state == ST_SEGMENT || state == ST_SCENE_STREAM) begin
                if (beat_pop)
                    beat_read_ptr <= beat_read_ptr + 1'b1;
                case ({beat_push, beat_pop})
                    2'b10: beat_count <= beat_count + 1'b1;
                    2'b01: beat_count <= beat_count - 1'b1;
                    default: begin end
                endcase
            end

            if (state == ST_SCENE_STREAM) begin
                if (scene_record_plan) begin
                    ar_request_valid <= 1'b1;
                    ar_request_meta <= 1'b1;
                    ar_request_address <= scene_record_address_q;
                    ar_request_beats <=
                        scene_record_burst * RECORD_BEATS;
                    scene_record_burst_q <= scene_record_burst;
                end else if (scene_pixel_plan) begin
                    ar_request_valid <= 1'b1;
                    ar_request_meta <= 1'b0;
                    ar_request_address <= issue_address;
                    ar_request_beats <= selected_burst_beats;
                end

                if (ar_accept) begin
                    ar_request_valid <= 1'b0;
                    read_bytes <= read_bytes +
                        ({{(32-BURST_BEAT_WIDTH){1'b0}},
                          ar_request_beats} << BEAT_SHIFT);
                    if (ar_request_meta) begin
                        scene_record_address_q <= scene_record_address_q +
                            {22'd0, scene_record_burst_q, 5'd0};
                        scene_records_remaining_q <=
                            scene_records_remaining_q -
                            {7'd0, scene_record_burst_q};
                        scene_records_requested_q <=
                            scene_records_requested_q +
                            {7'd0, scene_record_burst_q};
                    end else begin
                        issue_address <= issue_address +
                            ({{(32-BURST_BEAT_WIDTH){1'b0}},
                              ar_request_beats} << BEAT_SHIFT);
                        issue_beats_remaining <= issue_beats_remaining -
                            ar_request_beats;
                    end
                end

                // The issue walker passes solid spans and holds a source
                // span until its last burst is accepted.
                if (!scene_issue_active_q) begin
                    if (scene_issue_index_q != scene_decoded_q) begin
                        if (ring_solid[scene_issue_slot]) begin
                            scene_issue_index_q <= scene_issue_index_q + 12'd1;
                        end else begin
                            issue_address <= ring_address[scene_issue_slot];
                            issue_beats_remaining <=
                                ring_beats[scene_issue_slot];
                            scene_issue_active_q <= 1'b1;
                        end
                    end
                end else if (issue_beats_remaining == 11'd0 &&
                             !ar_request_valid) begin
                    scene_issue_active_q <= 1'b0;
                    scene_issue_index_q <= scene_issue_index_q + 12'd1;
                end
            end

            // Decode: one record a stage, checked as the serial path checked
            // it, so a scene that path refused is refused here.
            decode1_valid_q <= record_valid_q && state == ST_SCENE_STREAM;
            if (record_valid_q) begin
                decode1_solid_q <=
                    record_w5_q == `ASTRA_WINDOW_SCENE_SPAN_SOLID;
                decode1_solid_ok_q <= record_w0_q == 32'd0 &&
                    record_w1_q == 32'd0 && record_w2_q == 32'd0 &&
                    record_w3_q == 32'd0 && record_w6_q[31:16] == 16'd0;
                decode1_source_flags_ok_q <= record_w5_q == 32'd0 &&
                    record_w6_q == 32'd0;
                decode1_reserved_ok_q <= record_w7_q == 32'd0;
                decode1_continuous_q <=
                    record_w4_q[31:16] == {5'd0, decode_run_x_q};
                decode1_length_q <= record_w4_q[15:0];
                decode1_destination_end_q <=
                    {1'b0, record_w4_q[31:16]} + {1'b0, record_w4_q[15:0]};
                decode1_value_q <= record_w6_q[15:0];
                decode1_offset_q <= record_w0_q;
                decode1_bytes_q <= record_w1_q;
                decode1_pitch_q <= record_w2_q;
                decode1_source_x_q <= record_w3_q[31:16];
                decode1_row_pixels_q <=
                    {1'b0, record_w3_q[31:16]} + {1'b0, record_w4_q[15:0]};
                decode1_source_end_q <=
                    {1'b0, record_w0_q} + {1'b0, record_w1_q};
                decode1_product_low_q <=
                    record_w2_q[15:0] * record_w3_q[15:0];
                decode1_product_high_q <=
                    record_w2_q[31:16] * record_w3_q[15:0];
                decode_run_x_q <= decode_run_x_q + record_w4_q[10:0];
            end

            decode2_valid_q <= decode1_valid_q && state == ST_SCENE_STREAM;
            decode2_error_q <= !decode1_reserved_ok_q ||
                !decode1_continuous_q || decode1_length_q == 16'd0 ||
                decode1_destination_end_q > OUTPUT_WIDTH ||
                (decode1_solid_q ? !decode1_solid_ok_q :
                    (!decode1_source_flags_ok_q ||
                     decode1_offset_q[5:0] != 6'd0 ||
                     decode1_bytes_q == 32'd0 ||
                     decode1_pitch_q[0] ||
                     decode1_pitch_q <
                         {14'd0, decode1_row_pixels_q, 1'b0} ||
                     decode1_source_end_q >
                         {1'b0, arena_limit - arena_base} ||
                     !(({1'b0, arena_base} + decode1_source_end_q <=
                            {1'b0, framebuffer_base_q}) ||
                       ({1'b0, arena_base} + {1'b0, decode1_offset_q} >=
                            scene_compiled_end))));
            decode2_solid_q <= decode1_solid_q;
            decode2_length_q <= decode1_length_q[10:0];
            decode2_value_q <= decode1_value_q;
            decode2_offset_q <= decode1_offset_q;
            decode2_bytes_q <= decode1_bytes_q;
            decode2_source_x_q <= decode1_source_x_q;
            decode2_row_pixels_q <= decode1_row_pixels_q;
            decode2_product_q <= {decode1_product_high_q, 16'd0} +
                {16'd0, decode1_product_low_q};

            decode3_valid_q <= decode2_valid_q && state == ST_SCENE_STREAM;
            decode3_error_q <= decode2_error_q || (!decode2_solid_q &&
                {1'b0, decode2_product_q} +
                    {31'd0, decode2_row_pixels_q, 1'b0} >
                    {17'd0, decode2_bytes_q});
            decode3_solid_q <= decode2_solid_q;
            decode3_length_q <= decode2_length_q;
            decode3_value_q <= decode2_value_q;
            decode3_row_address_q <= arena_base + decode2_offset_q +
                decode2_product_q[31:0] + {15'd0, decode2_source_x_q, 1'b0};

            if (decode3_valid_q && state == ST_SCENE_STREAM) begin
                if (decode3_error_q)
                    config_error <= 1'b1;
                ring_solid[scene_decode_slot] <= decode3_solid_q;
                ring_value[scene_decode_slot] <= decode3_value_q;
                ring_length[scene_decode_slot] <= decode3_length_q;
                ring_address[scene_decode_slot] <=
                    decode3_row_address_q & ~(BEAT_BYTES - 1);
                ring_first_byte[scene_decode_slot] <=
                    decode3_row_address_q[BEAT_SHIFT-1:0];
                ring_beats[scene_decode_slot] <=
                    ({1'b0, decode3_length_q, 1'b0} +
                     decode3_row_address_q[BEAT_SHIFT-1:0] +
                     BEAT_BYTES - 1) >> BEAT_SHIFT;
                scene_decoded_q <= scene_decoded_q + 12'd1;
            end

            // The segment writer serves both paths. A framebuffer segment
            // drains its unused reads; a scene span has consumed exactly its
            // own beats, and the next span's are already in the FIFO.
            if (segment_writing) begin
                if (!beat_active && beat_count != 0) begin
                    active_beat <= beat_fifo[beat_read_ptr];
                    active_byte <= first_beat ? first_byte_offset : 0;
                    first_beat <= 1'b0;
                    beat_active <= 1'b1;
                end else if (beat_active && segment_pixels_active_q) begin
                    output_x_q <= output_x_q + mapped_write_count;
                    segment_pixels_remaining <=
                        segment_pixels_remaining - mapped_write_count;
                    if (segment_pixels_remaining <= mapped_write_count) begin
                        segment_pixels_active_q <= 1'b0;
                        beat_active <= 1'b0;
                        if (state == ST_SEGMENT) begin
                            ar_request_valid <= 1'b0;
                            issue_beats_remaining <= 11'd0;
                            beat_count <= 0;
                            state <= ST_SEGMENT_DRAIN;
                        end else begin
                            scene_write_mode_q <= WRITE_IDLE;
                            scene_write_index_q <=
                                scene_write_index_q + 12'd1;
                        end
                    end else begin
                        if (active_byte_after_write >= BEAT_BYTES) begin
                            if (beat_count != 0) begin
                                active_beat <= beat_fifo[beat_read_ptr];
                                active_byte <= 0;
                                beat_active <= 1'b1;
                            end else begin
                                beat_active <= 1'b0;
                            end
                        end else begin
                            active_byte <=
                                active_byte_after_write[BEAT_SHIFT-1:0];
                        end
                    end
                end
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
                        window_scene_q <= window_scene;
                        window_scene_bytes_q <= window_scene_bytes;
                        output_x_q <= 11'd0;
                        busy <= 1'b1;
                        if (window_scene) begin
                            format_q <= FORMAT_RGB565;
                            bytes_shift_q <= 2'd1;
                            bytes_per_pixel_q <= 3'd2;
                            scene_header_reserved_valid_q <= 1'b1;
                            if (format != FORMAT_RGB565 ||
                                framebuffer_base[5:0] != 6'd0 ||
                                window_scene_bytes <
                                    `ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES ||
                                framebuffer_base < arena_base ||
                                {1'b0, framebuffer_base} +
                                    {1'b0, window_scene_bytes} >
                                    {1'b0, arena_limit}) begin
                                config_error <= 1'b1;
                                busy <= 1'b0;
                                done <= 1'b1;
                            end else if (!scene_changed &&
                                         scene_header_valid_q &&
                                         scene_header_base_q ==
                                             framebuffer_base &&
                                         scene_header_capacity_q ==
                                             window_scene_bytes) begin
                                state <= ST_SCENE_LINE_REQUEST;
                            end else begin
                                state <= ST_SCENE_HEADER_REQUEST;
                            end
                        end else if (TRUSTED_CONFIG != 0)
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
                        output_x_q <= output_x_q + fill_write_count;
                        fill_pixels_q <= fill_pixels_q - fill_write_count;
                        if (fill_pixels_q <= fill_write_count) begin
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
                    issue_address <= segment_byte_address & ~(BEAT_BYTES - 1);
                    first_byte_offset <=
                        segment_byte_address[BEAT_SHIFT-1:0];
                    segment_payload_bytes_q <= segment_payload_bytes;
                    state <= ST_SEGMENT_COUNTS;
                end

                ST_SEGMENT_COUNTS: begin
                    // Keep address generation and the rounded byte-to-beat
                    // conversion on separate 200 MHz cycles.
                    issue_beats_remaining <=
                        ({1'b0, segment_payload_bytes_q} + first_byte_offset +
                         BEAT_BYTES - 1) >> BEAT_SHIFT;
                    ar_request_valid <= 1'b0;
                    segment_pixels_remaining <= mapped_pixels_q;
                    segment_pixels_active_q <= mapped_pixels_q != 11'd0;
                    burst_write_ptr <= 3'd0;
                    burst_read_ptr <= 3'd0;
                    burst_count <= 4'd0;
                    reserved_beats <= 0;
                    burst_head_beats <= 0;
                    response_active <= 1'b0;
                    response_beats_left <= 0;
                    beat_write_ptr <= 0;
                    beat_read_ptr <= 0;
                    beat_count <= 0;
                    beat_active <= 1'b0;
                    first_beat <= 1'b1;
                    active_byte <= 0;
                    state <= ST_SEGMENT;
                end

                ST_SEGMENT: begin
                    // The shared segment writer above advances this state.
                end

                ST_SEGMENT_DRAIN: begin
                    if (burst_count == 4'd0 && !response_active &&
                        !ar_request_valid) begin
                        if (deadline_error) begin
                            busy <= 1'b0;
                            done <= 1'b1;
                            state <= ST_IDLE;
                        end else if (window_scene_q) begin
                            state <= ST_FINISH;
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
                        output_x_q <= output_x_q + fill_write_count;
                        fill_pixels_q <= fill_pixels_q - fill_write_count;
                        if (fill_pixels_q <= fill_write_count)
                            state <= ST_FINISH;
                    end else begin
                        state <= ST_FINISH;
                    end
                end

                ST_SCENE_HEADER_REQUEST: begin
                    if (!ar_request_valid) begin
                        ar_request_valid <= 1'b1;
                        ar_request_meta <= 1'b1;
                        ar_request_address <= framebuffer_base_q;
                        ar_request_beats <= 64 / BEAT_BYTES;
                    end
                    if (ar_accept) begin
                        ar_request_valid <= 1'b0;
                        read_bytes <= read_bytes + 32'd64;
                        scene_metadata_beat_q <= 4'd0;
                        state <= ST_SCENE_HEADER_RESPONSE;
                    end
                end

                ST_SCENE_HEADER_RESPONSE: begin
                    if (burst_count == 4'd0 && !response_active)
                        state <= ST_SCENE_HEADER_CHECK;
                end

                ST_SCENE_HEADER_CHECK: begin
                    if (fetch_error || !scene_header_reserved_valid_q ||
                        scene_total_bytes_q <
                            `ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES ||
                        scene_total_bytes_q > window_scene_bytes_q ||
                        scene_generation_q == 32'd0 ||
                        scene_width_height_q !=
                            {OUTPUT_WIDTH[15:0], OUTPUT_HEIGHT[15:0]} ||
                        scene_line_count_q != OUTPUT_HEIGHT ||
                        scene_line_offset_q <
                            `ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES ||
                        scene_line_offset_q[2:0] != 3'd0 ||
                        scene_line_table_end >
                            {5'd0, scene_total_bytes_q} ||
                        scene_span_offset_q[5:0] != 6'd0 ||
                        {1'b0, scene_span_offset_q} <
                            scene_line_table_end ||
                        scene_span_table_end >
                            {6'd0, scene_total_bytes_q} ||
                        scene_flags_q != 32'd0) begin
                        config_error <= 1'b1;
                        state <= ST_FINISH;
                    end else begin
                        scene_header_valid_q <= 1'b1;
                        scene_header_base_q <= framebuffer_base_q;
                        scene_header_capacity_q <= window_scene_bytes_q;
                        state <= ST_SCENE_LINE_REQUEST;
                    end
                end

                ST_SCENE_LINE_REQUEST: begin
                    if (!ar_request_valid) begin
                        ar_request_valid <= 1'b1;
                        ar_request_meta <= 1'b1;
                        ar_request_address <=
                            scene_line_address & ~(BEAT_BYTES - 1);
                        ar_request_beats <= 5'd1;
                    end
                    if (ar_accept) begin
                        ar_request_valid <= 1'b0;
                        read_bytes <= read_bytes + BEAT_BYTES;
                        scene_metadata_beat_q <= 4'd0;
                        state <= ST_SCENE_LINE_RESPONSE;
                    end
                end

                ST_SCENE_LINE_RESPONSE: begin
                    if (burst_count == 4'd0 && !response_active)
                        state <= ST_SCENE_LINE_CHECK;
                end

                ST_SCENE_LINE_CHECK: begin
                    if (fetch_error || scene_line_span_count_q == 32'd0 ||
                        scene_line_span_count_q > OUTPUT_WIDTH ||
                        scene_line_span_end >
                            {1'b0, scene_span_count_q}) begin
                        config_error <= 1'b1;
                        state <= ST_FINISH;
                    end else begin
                        output_x_q <= 11'd0;
                        scene_record_address_q <= framebuffer_base_q +
                            scene_span_offset_q +
                            (scene_first_span_q << 5);
                        scene_records_remaining_q <=
                            scene_line_span_count_q[11:0];
                        scene_records_requested_q <= 12'd0;
                        scene_decoded_q <= 12'd0;
                        scene_issue_index_q <= 12'd0;
                        scene_issue_active_q <= 1'b0;
                        scene_write_index_q <= 12'd0;
                        scene_write_mode_q <= WRITE_IDLE;
                        issue_beats_remaining <= 11'd0;
                        record_beat_q <= 2'd0;
                        decode_run_x_q <= 11'd0;
                        reserved_beats <= 0;
                        beat_write_ptr <= 0;
                        beat_read_ptr <= 0;
                        beat_count <= 0;
                        beat_active <= 1'b0;
                        segment_pixels_active_q <= 1'b0;
                        state <= ST_SCENE_STREAM;
                    end
                end

                ST_SCENE_STREAM: begin
                    if (scene_stream_abort) begin
                        scene_write_mode_q <= WRITE_IDLE;
                        beat_active <= 1'b0;
                        segment_pixels_active_q <= 1'b0;
                        state <= ST_SEGMENT_DRAIN;
                    end else begin
                        case (scene_write_mode_q)
                            WRITE_IDLE: begin
                                if (scene_write_index_q ==
                                        scene_line_span_count_q[11:0]) begin
                                    if (output_x_q != OUTPUT_WIDTH)
                                        config_error <= 1'b1;
                                    state <= ST_SEGMENT_DRAIN;
                                end else if (scene_write_index_q !=
                                             scene_decoded_q) begin
                                    if (ring_solid[scene_write_slot]) begin
                                        fill_pixels_q <=
                                            ring_length[scene_write_slot];
                                        scene_span_value_q <= {16'd0,
                                            ring_value[scene_write_slot]};
                                        scene_write_mode_q <= WRITE_SOLID;
                                    end else begin
                                        segment_pixels_remaining <=
                                            ring_length[scene_write_slot];
                                        segment_pixels_active_q <= 1'b1;
                                        first_byte_offset <=
                                            ring_first_byte[scene_write_slot];
                                        first_beat <= 1'b1;
                                        scene_write_mode_q <= WRITE_SOURCE;
                                    end
                                end
                            end
                            WRITE_SOLID: begin
                                output_x_q <= output_x_q + fill_write_count;
                                fill_pixels_q <=
                                    fill_pixels_q - fill_write_count;
                                if (fill_pixels_q <= fill_write_count) begin
                                    scene_write_mode_q <= WRITE_IDLE;
                                    scene_write_index_q <=
                                        scene_write_index_q + 12'd1;
                                end
                            end
                            default: begin
                                // The shared segment writer above serves
                                // source spans.
                            end
                        endcase
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
                beat_count <= 0;
                beat_active <= 1'b0;
                segment_pixels_active_q <= 1'b0;
                state <= ST_SEGMENT_DRAIN;
            end
        end
    end

    wire unused_validator_busy = validator_busy;
endmodule

`default_nettype wire
