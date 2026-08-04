// Copyright (c) 2026 Astra68 contributors
//
// Builds one complete INDEX4/INDEX8 tile-layer scanline from ordered 64-bit
// AXI reads. A line slot becomes visible only after every descriptor and
// pattern response succeeds.
`timescale 1ns/1ps
`default_nettype none

module astra_tile_line_builder #(
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = {AXI_ID_WIDTH{1'b0}},
    parameter integer TRUSTED_CONFIG = 0
) (
    input  wire                         build_clk,
    input  wire                         build_reset,

    input  wire                         start,
    input  wire [1:0]                   build_slot,
    input  wire [10:0]                  line_y,
    input  wire signed [31:0]           scroll_x,
    input  wire signed [31:0]           scroll_y,
    input  wire                         tile_16,
    input  wire                         index_8,
    input  wire [3:0]                   map_width_log2,
    input  wire [3:0]                   map_height_log2,
    input  wire                         wrap_x,
    input  wire                         wrap_y,
    input  wire                         transparent_enable,
    input  wire [7:0]                   transparent_index,
    input  wire [31:0]                  map_base,
    input  wire [31:0]                  pattern_base,
    input  wire [16:0]                  tile_count,
    input  wire [31:0]                  arena_base,
    input  wire [31:0]                  arena_limit,

    output reg                          busy,
    output reg                          done,
    output reg                          line_complete,
    output reg  [1:0]                   completed_slot,
    output reg  [3:0]                   slot_valid,
    output reg                          config_error,
    output reg                          descriptor_error,
    output reg                          fetch_error,
    output reg  [31:0]                  build_cycles,
    output reg  [31:0]                  map_read_bytes,
    output reg  [31:0]                  pattern_read_bytes,

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
    output wire [3:0]                   pixel_palette_bank,
    output wire [7:0]                   pixel_index
);
    localparam [3:0] ST_IDLE = 4'd0;
    localparam [3:0] ST_WALK_START = 4'd1;
    localparam [3:0] ST_MAP = 4'd2;
    localparam [3:0] ST_MAP_DRAIN = 4'd3;
    localparam [3:0] ST_PATTERN_LOAD = 4'd4;
    localparam [3:0] ST_PATTERN_ISSUE = 4'd5;
    localparam [3:0] ST_PATTERN_DRAIN = 4'd6;
    localparam [3:0] ST_COMPOSE_PRIME0 = 4'd7;
    localparam [3:0] ST_COMPOSE_PRIME1 = 4'd8;
    localparam [3:0] ST_COMPOSE_PRIME2 = 4'd9;
    localparam [3:0] ST_COMPOSE = 4'd10;
localparam [3:0] ST_COMPOSE_DRAIN = 4'd11;
localparam [3:0] ST_FINISH = 4'd12;
localparam [3:0] ST_PATTERN_ADDRESS = 4'd13;
localparam [3:0] ST_COMPOSE_DRAIN2 = 4'd14;
localparam [3:0] ST_VALIDATE = 4'd15;

    localparam integer TAG_DEPTH = 16;
    localparam integer TAG_WIDTH = 12;
    localparam integer TAG_KIND = 11;
    localparam integer TAG_SLOT_HI = 10;
    localparam integer TAG_SLOT_LO = 3;
    localparam integer TAG_MAP_HALF = 2;
    localparam integer TAG_TWO_BEAT = 1;

    function automatic [31:0] big_endian_word(
        input [63:0] data,
        input        upper_half
    );
        reg [31:0] lanes;
        begin
            lanes = upper_half ? data[63:32] : data[31:0];
            big_endian_word = {
                lanes[7:0], lanes[15:8], lanes[23:16], lanes[31:24]
            };
        end
    endfunction

    function automatic [7:0] pattern_byte(
        input [63:0] low_word,
        input [63:0] high_word,
        input [4:0]  byte_index
    );
        begin
            if (byte_index < 5'd8)
                pattern_byte = low_word[byte_index * 8 +: 8];
            else
                pattern_byte = high_word[(byte_index - 5'd8) * 8 +: 8];
        end
    endfunction

    reg [3:0] state;

    reg [1:0] build_slot_q;
    reg [10:0] line_y_q;
    reg signed [31:0] scroll_x_q;
    reg signed [31:0] scroll_y_q;
    reg tile_16_q;
    reg index_8_q;
    reg [3:0] map_width_log2_q;
    reg [3:0] map_height_log2_q;
    reg wrap_x_q;
    reg wrap_y_q;
    reg transparent_enable_q;
    reg [7:0] transparent_index_q;
    reg [31:0] map_base_q;
    reg [31:0] pattern_base_q;
    reg [16:0] tile_count_q;

    wire validator_start = state == ST_IDLE && start;
    wire validator_busy;
    wire validator_done;
    wire validator_config_valid;
    generate
        if (TRUSTED_CONFIG == 0) begin : generate_validator
            astra_tile_config_validator config_validator_i (
                .clk(build_clk),
                .reset(build_reset),
                .start(validator_start),
                .tile_16(tile_16),
                .index_8(index_8),
                .map_width_log2(map_width_log2),
                .map_height_log2(map_height_log2),
                .map_base(map_base),
                .pattern_base(pattern_base),
                .tile_count(tile_count),
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

    wire walker_start = state == ST_WALK_START;
    wire walker_busy;
    wire walker_done;
    wire walker_config_error;
    wire walker_span_valid;
    wire walker_span_ready;
    wire walker_span_mapped;
    wire [7:0] walker_span_slot;
    wire [10:0] walker_span_screen_x;
    wire [4:0] walker_span_pixels;
    wire [3:0] walker_span_tile_x;
    wire [3:0] walker_span_tile_y;
    wire [19:0] walker_span_map_byte_offset;
    wire span_ready;
    reg span_buffer_valid;
    reg span_buffer_mapped;
    reg [7:0] span_buffer_slot;
    reg [10:0] span_buffer_screen_x;
    reg [4:0] span_buffer_pixels;
    reg [3:0] span_buffer_tile_x;
    reg [3:0] span_buffer_tile_y;
    reg [19:0] span_buffer_map_byte_offset;
    reg walker_finished;
    wire span_valid = span_buffer_valid;
    wire span_mapped = span_buffer_mapped;
    wire [7:0] span_slot = span_buffer_slot;
    wire [10:0] span_screen_x = span_buffer_screen_x;
    wire [4:0] span_pixels = span_buffer_pixels;
    wire [3:0] span_tile_x = span_buffer_tile_x;
    wire [3:0] span_tile_y = span_buffer_tile_y;
    wire [19:0] span_map_byte_offset = span_buffer_map_byte_offset;
    wire span_first_unused;
    wire span_last_unused;
    wire [8:0] span_map_x_unused;
    wire [8:0] span_map_y_unused;
    wire [17:0] span_map_index_unused;
    wire unused_walker_outputs = &{
        1'b0,
        span_first_unused,
        span_last_unused,
        span_map_x_unused,
        span_map_y_unused,
        span_map_index_unused
    };

    astra_tile_span_walker #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH)
    ) span_walker_i (
        .clk(build_clk),
        .rst(build_reset),
        .start(walker_start),
        .line_y(line_y_q),
        .scroll_x(scroll_x_q),
        .scroll_y(scroll_y_q),
        .tile_16(tile_16_q),
        .map_width_log2(map_width_log2_q),
        .map_height_log2(map_height_log2_q),
        .wrap_x(wrap_x_q),
        .wrap_y(wrap_y_q),
        .busy(walker_busy),
        .done(walker_done),
        .config_error(walker_config_error),
        .span_valid(walker_span_valid),
        .span_ready(walker_span_ready),
        .span_first(span_first_unused),
        .span_last(span_last_unused),
        .span_mapped(walker_span_mapped),
        .span_slot(walker_span_slot),
        .span_screen_x(walker_span_screen_x),
        .span_pixels(walker_span_pixels),
        .span_tile_x(walker_span_tile_x),
        .span_tile_y(walker_span_tile_y),
        .span_map_x(span_map_x_unused),
        .span_map_y(span_map_y_unused),
        .span_map_index(span_map_index_unused),
        .span_map_byte_offset(walker_span_map_byte_offset)
    );

    // Span geometry and decoded descriptors are small, bounded LUT memories.
    // Pattern rows use block RAM because every slot reserves the 16-byte
    // INDEX8/16x16 worst case.
    (* ram_style = "distributed" *) reg [24:0] span_mem [0:255];
    (* ram_style = "distributed" *) reg [22:0] descriptor_mem [0:255];
    (* ram_style = "distributed" *) reg        pattern_half_mem [0:255];
    (* ram_style = "block" *) reg [63:0] pattern_low_mem [0:255];
    (* ram_style = "block" *) reg [63:0] pattern_high_mem [0:255];
    reg [8:0] span_count;

    // {mapped, screen_x, pixels, tile_x, tile_y}
    wire [24:0] span_write_data = {
        span_mapped, span_screen_x, span_pixels, span_tile_x, span_tile_y
    };

    (* ram_style = "distributed" *) reg [TAG_WIDTH-1:0]
        tag_fifo [0:TAG_DEPTH-1];
    reg [TAG_WIDTH-1:0] tag_head_q;
    reg [3:0] tag_write_ptr;
    reg [3:0] tag_read_ptr;
    reg [4:0] tag_count;
    reg [4:0] request_count_q;
    reg [2:0] response_beat;
    wire tag_empty = tag_count == 5'd0;
    wire [TAG_WIDTH-1:0] tag_head = tag_head_q;
    wire tag_head_pattern = tag_head[TAG_KIND];
    wire [7:0] tag_head_slot = tag_head[TAG_SLOT_HI:TAG_SLOT_LO];
    wire tag_head_map_half = tag_head[TAG_MAP_HALF];
    wire tag_head_two_beat = tag_head[TAG_TWO_BEAT];
    wire expected_last = tag_head_two_beat ?
        response_beat == 3'd1 : response_beat == 3'd0;

    wire [31:0] map_request_full =
        map_base_q + {12'd0, span_map_byte_offset};
    wire [31:0] map_request_address =
        {map_request_full[31:3], 3'b000};
    wire map_request_half = map_request_full[2];

    reg [7:0] pattern_issue_slot;
    reg pattern_issue_needed;
    reg [31:0] pattern_issue_address;
    reg pattern_issue_half;
    reg pattern_issue_two_beat;
    reg [15:0] pattern_issue_tile_index;
    reg [3:0] pattern_issue_row;

    wire pattern_load_mapped = span_mem[pattern_issue_slot][24];
    wire [3:0] pattern_load_tile_y =
        span_mem[pattern_issue_slot][3:0];
    wire pattern_load_descriptor_valid =
        descriptor_mem[pattern_issue_slot][22];
    wire [15:0] pattern_load_tile_index =
        descriptor_mem[pattern_issue_slot][21:6];
    wire pattern_load_flip_y = descriptor_mem[pattern_issue_slot][0];
    wire [3:0] pattern_load_row = pattern_load_flip_y ?
        (tile_16_q ? 4'd15 - pattern_load_tile_y :
                     {1'b0, 3'd7 - pattern_load_tile_y[2:0]}) :
        pattern_load_tile_y;
    wire [3:0] pattern_stride_shift =
        index_8_q ? (tile_16_q ? 4'd8 : 4'd6) :
                    (tile_16_q ? 4'd7 : 4'd5);
    wire [4:0] pattern_row_shift =
        index_8_q ? (tile_16_q ? 5'd4 : 5'd3) :
                    (tile_16_q ? 5'd3 : 5'd2);
    wire [31:0] pattern_issue_full =
        pattern_base_q +
        ({16'd0, pattern_issue_tile_index} << pattern_stride_shift) +
        ({28'd0, pattern_issue_row} << pattern_row_shift);

    reg arvalid_q;
    reg [31:0] araddr_q;
    reg [7:0] arlen_q;
    reg [TAG_WIDTH-1:0] ar_tag_q;
    reg ar_pattern_half_q;

    wire request_has_capacity = request_count_q < TAG_DEPTH;
    wire request_stage_ready = !arvalid_q || m_axi_arready;
    wire map_request_enqueue = state == ST_MAP && span_valid && span_mapped &&
                               request_has_capacity && request_stage_ready;
    wire pattern_request_enqueue = state == ST_PATTERN_ISSUE &&
                                   pattern_issue_needed &&
                                   request_has_capacity &&
                                   request_stage_ready;
    wire request_enqueue = map_request_enqueue || pattern_request_enqueue;

    assign m_axi_arvalid = arvalid_q;
    assign m_axi_araddr = araddr_q;
    assign m_axi_arlen = arlen_q;
    assign m_axi_arid = AXI_ID;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b1111;
    wire axi_request_accept = m_axi_arvalid && m_axi_arready;

    assign span_ready = state == ST_MAP &&
        (span_mapped ? (request_has_capacity && request_stage_ready) : 1'b1);
    wire span_accept = span_valid && span_ready;
    assign walker_span_ready = !span_buffer_valid || span_accept;

    // Register the geometry/fetch boundary. The walker can still produce one
    // span per clock, but signed bounds arithmetic no longer drives AXI,
    // cache-write enables, and accounting in the same cycle.
    always @(posedge build_clk) begin
        if (build_reset) begin
            span_buffer_valid <= 1'b0;
            span_buffer_mapped <= 1'b0;
            span_buffer_slot <= 8'd0;
            span_buffer_screen_x <= 11'd0;
            span_buffer_pixels <= 5'd0;
            span_buffer_tile_x <= 4'd0;
            span_buffer_tile_y <= 4'd0;
            span_buffer_map_byte_offset <= 20'd0;
            walker_finished <= 1'b0;
        end else if (walker_start) begin
            span_buffer_valid <= 1'b0;
            walker_finished <= 1'b0;
        end else begin
            if (walker_done)
                walker_finished <= 1'b1;
            if (walker_span_ready) begin
                span_buffer_valid <= walker_span_valid;
                if (walker_span_valid) begin
                    span_buffer_mapped <= walker_span_mapped;
                    span_buffer_slot <= walker_span_slot;
                    span_buffer_screen_x <= walker_span_screen_x;
                    span_buffer_pixels <= walker_span_pixels;
                    span_buffer_tile_x <= walker_span_tile_x;
                    span_buffer_tile_y <= walker_span_tile_y;
                    span_buffer_map_byte_offset <=
                        walker_span_map_byte_offset;
                end
            end
        end
    end

    assign m_axi_rready = busy;
    wire axi_response_accept = m_axi_rvalid && m_axi_rready;
    wire tag_push = axi_request_accept;
    wire tag_pop = axi_response_accept && !tag_empty && m_axi_rlast;
    wire [TAG_WIDTH-1:0] pushed_tag = ar_tag_q;

    // Decouple the state/address planner from both SmartConnect and the tag
    // FIFO write enable. A simultaneous downstream handshake and replacement
    // keeps the launch stage at one request per build clock.
    always @(posedge build_clk) begin
        if (build_reset || (state == ST_IDLE && start)) begin
            arvalid_q <= 1'b0;
            araddr_q <= 32'd0;
            arlen_q <= 8'd0;
            ar_tag_q <= {TAG_WIDTH{1'b0}};
            ar_pattern_half_q <= 1'b0;
        end else begin
            if (axi_request_accept)
                arvalid_q <= 1'b0;
            if (request_enqueue) begin
                arvalid_q <= 1'b1;
                if (pattern_request_enqueue) begin
                    araddr_q <= pattern_issue_address;
                    arlen_q <= pattern_issue_two_beat ? 8'd1 : 8'd0;
                    ar_tag_q <= {
                        1'b1, pattern_issue_slot, 1'b0,
                        pattern_issue_two_beat, 1'b0
                    };
                    ar_pattern_half_q <= pattern_issue_half;
                end else begin
                    araddr_q <= map_request_address;
                    arlen_q <= 8'd0;
                    ar_tag_q <= {
                        1'b0, span_slot, map_request_half, 1'b0, 1'b0
                    };
                    ar_pattern_half_q <= 1'b0;
                end
            end
        end
    end

    // Keep the active response tag in a register so response retirement never
    // depends on an asynchronous FIFO lookup. Tags behind it remain in the
    // bounded ring and are promoted only when the active transaction retires.
    always @(posedge build_clk) begin
        if (build_reset || (state == ST_IDLE && start)) begin
            tag_head_q <= {TAG_WIDTH{1'b0}};
        end else if (tag_pop && tag_count > 5'd1) begin
            tag_head_q <= tag_fifo[tag_read_ptr];
        end else if (tag_push && (tag_empty || tag_pop)) begin
            tag_head_q <= pushed_tag;
        end
    end

    wire [31:0] response_map_entry =
        big_endian_word(m_axi_rdata, tag_head_map_half);
    reg map_response_pending;
    reg [7:0] map_response_slot_q;
    reg [31:0] map_response_entry_q;
    reg map_response_transport_valid_q;
    wire map_response_descriptor_valid =
        map_response_transport_valid_q &&
        map_response_entry_q[9:0] == 10'd0 &&
        {1'b0, map_response_entry_q[31:16]} < tile_count_q;
    wire [22:0] map_response_descriptor = {
        map_response_descriptor_valid,
        map_response_entry_q[31:16],
        map_response_entry_q[15:12],
        map_response_entry_q[11],
        map_response_entry_q[10]
    };

    reg [7:0] compose_read_slot;
    reg [24:0] compose_span_q;
    reg [22:0] compose_descriptor_q;
    reg compose_pattern_half_q;
    reg [63:0] compose_pattern_low_q;
    reg [63:0] compose_pattern_high_q;

    always @(posedge build_clk) begin
        compose_span_q <= span_mem[compose_read_slot];
        compose_descriptor_q <= descriptor_mem[compose_read_slot];
        compose_pattern_half_q <= pattern_half_mem[compose_read_slot];
        compose_pattern_low_q <= pattern_low_mem[compose_read_slot];
        compose_pattern_high_q <= pattern_high_mem[compose_read_slot];
    end

    reg [24:0] compose_active_span;
    reg [22:0] compose_active_descriptor;
    reg compose_active_pattern_half;
    reg [63:0] compose_active_pattern_low;
    reg [63:0] compose_active_pattern_high;
    reg [10:0] compose_x;
    reg [4:0] compose_pixels_left;

    function automatic [4:0] source_column(
        input [24:0] span,
        input [22:0] descriptor,
        input [10:0] screen_x
    );
        reg [4:0] natural_x;
        begin
            // The active span guarantees a 0..15 screen-space delta. Keeping
            // this arithmetic at five bits avoids an unnecessary 11-bit
            // subtractor in every lane.
            natural_x = {1'b0, span[7:4]} +
                        (screen_x[4:0] - span[17:13]);
            source_column = descriptor[1] ?
                (tile_16_q ? 5'd15 - natural_x : 5'd7 - natural_x) :
                natural_x;
        end
    endfunction

    function automatic [7:0] selected_pattern_byte(
        input [4:0]  source_x,
        input        row_half,
        input [63:0] pattern_low,
        input [63:0] pattern_high,
        input        index_8
    );
        reg [4:0] byte_index;
        begin
            byte_index = index_8 ? source_x :
                ({4'd0, row_half} << 2) + {1'b0, source_x[4:1]};
            selected_pattern_byte = pattern_byte(
                pattern_low, pattern_high, byte_index);
        end
    endfunction

    function automatic [12:0] indexed_pixel(
        input [7:0] source_byte,
        input       low_nibble,
        input [3:0] palette_bank,
        input       eligible,
        input       index_8,
        input       transparent_enable,
        input [7:0] transparent_index
    );
        reg [7:0] palette_index;
        reg visible;
        begin
            palette_index = index_8 ? source_byte :
                (low_nibble ? {4'd0, source_byte[3:0]} :
                                {4'd0, source_byte[7:4]});
            visible = eligible &&
                (!transparent_enable || palette_index != transparent_index);
            indexed_pixel = {visible, palette_bank, palette_index};
        end
    endfunction

    wire [11:0] compose_next_x = {1'b0, compose_x} + 12'd4;
    wire compose_advance_span = compose_pixels_left <= 5'd4;
    wire compose_last_quad = compose_next_x >= OUTPUT_WIDTH;
    wire [10:0] compose_x0 = compose_x;
    wire [10:0] compose_x1 = compose_x + 11'd1;
    wire [10:0] compose_x2 = compose_x + 11'd2;
    wire [10:0] compose_x3 = compose_x + 11'd3;
    wire compose_lane0_next = 1'b0;
    wire compose_lane1_next = compose_pixels_left <= 5'd1;
    wire compose_lane2_next = compose_pixels_left <= 5'd2;
    wire compose_lane3_next = compose_pixels_left <= 5'd3;

    wire [24:0] compose_span0 = compose_lane0_next ? compose_span_q :
                                                    compose_active_span;
    wire [24:0] compose_span1 = compose_lane1_next ? compose_span_q :
                                                    compose_active_span;
    wire [24:0] compose_span2 = compose_lane2_next ? compose_span_q :
                                                    compose_active_span;
    wire [24:0] compose_span3 = compose_lane3_next ? compose_span_q :
                                                    compose_active_span;
    wire [22:0] compose_descriptor0 = compose_lane0_next ?
        compose_descriptor_q : compose_active_descriptor;
    wire [22:0] compose_descriptor1 = compose_lane1_next ?
        compose_descriptor_q : compose_active_descriptor;
    wire [22:0] compose_descriptor2 = compose_lane2_next ?
        compose_descriptor_q : compose_active_descriptor;
    wire [22:0] compose_descriptor3 = compose_lane3_next ?
        compose_descriptor_q : compose_active_descriptor;
    wire compose_pattern_half0 = compose_lane0_next ?
        compose_pattern_half_q : compose_active_pattern_half;
    wire compose_pattern_half1 = compose_lane1_next ?
        compose_pattern_half_q : compose_active_pattern_half;
    wire compose_pattern_half2 = compose_lane2_next ?
        compose_pattern_half_q : compose_active_pattern_half;
    wire compose_pattern_half3 = compose_lane3_next ?
        compose_pattern_half_q : compose_active_pattern_half;
    wire [63:0] compose_pattern_low0 = compose_lane0_next ?
        compose_pattern_low_q : compose_active_pattern_low;
    wire [63:0] compose_pattern_low1 = compose_lane1_next ?
        compose_pattern_low_q : compose_active_pattern_low;
    wire [63:0] compose_pattern_low2 = compose_lane2_next ?
        compose_pattern_low_q : compose_active_pattern_low;
    wire [63:0] compose_pattern_low3 = compose_lane3_next ?
        compose_pattern_low_q : compose_active_pattern_low;
    wire [63:0] compose_pattern_high0 = compose_lane0_next ?
        compose_pattern_high_q : compose_active_pattern_high;
    wire [63:0] compose_pattern_high1 = compose_lane1_next ?
        compose_pattern_high_q : compose_active_pattern_high;
    wire [63:0] compose_pattern_high2 = compose_lane2_next ?
        compose_pattern_high_q : compose_active_pattern_high;
    wire [63:0] compose_pattern_high3 = compose_lane3_next ?
        compose_pattern_high_q : compose_active_pattern_high;

    wire compose_write = state == ST_COMPOSE;

    reg compose_lookup_valid;
    reg [1:0] compose_lookup_slot;
    reg [8:0] compose_lookup_quad;
    reg [4:0] compose_lookup_source0;
    reg [4:0] compose_lookup_source1;
    reg [4:0] compose_lookup_source2;
    reg [4:0] compose_lookup_source3;
    reg [3:0] compose_lookup_bank0;
    reg [3:0] compose_lookup_bank1;
    reg [3:0] compose_lookup_bank2;
    reg [3:0] compose_lookup_bank3;
    reg compose_lookup_eligible0;
    reg compose_lookup_eligible1;
    reg compose_lookup_eligible2;
    reg compose_lookup_eligible3;
    reg compose_lookup_half0;
    reg compose_lookup_half1;
    reg compose_lookup_half2;
    reg compose_lookup_half3;
    reg [63:0] compose_lookup_low0;
    reg [63:0] compose_lookup_low1;
    reg [63:0] compose_lookup_low2;
    reg [63:0] compose_lookup_low3;
    reg [63:0] compose_lookup_high0;
    reg [63:0] compose_lookup_high1;
    reg [63:0] compose_lookup_high2;
    reg [63:0] compose_lookup_high3;

    wire [7:0] compose_lookup_byte0 = selected_pattern_byte(
        compose_lookup_source0, compose_lookup_half0,
        compose_lookup_low0, compose_lookup_high0, index_8_q);
    wire [7:0] compose_lookup_byte1 = selected_pattern_byte(
        compose_lookup_source1, compose_lookup_half1,
        compose_lookup_low1, compose_lookup_high1, index_8_q);
    wire [7:0] compose_lookup_byte2 = selected_pattern_byte(
        compose_lookup_source2, compose_lookup_half2,
        compose_lookup_low2, compose_lookup_high2, index_8_q);
    wire [7:0] compose_lookup_byte3 = selected_pattern_byte(
        compose_lookup_source3, compose_lookup_half3,
        compose_lookup_low3, compose_lookup_high3, index_8_q);

    reg compose_palette_valid;
    reg [1:0] compose_palette_slot;
    reg [8:0] compose_palette_quad;
    reg [7:0] compose_palette_byte0;
    reg [7:0] compose_palette_byte1;
    reg [7:0] compose_palette_byte2;
    reg [7:0] compose_palette_byte3;
    reg compose_palette_low_nibble0;
    reg compose_palette_low_nibble1;
    reg compose_palette_low_nibble2;
    reg compose_palette_low_nibble3;
    reg [3:0] compose_palette_bank0;
    reg [3:0] compose_palette_bank1;
    reg [3:0] compose_palette_bank2;
    reg [3:0] compose_palette_bank3;
    reg compose_palette_eligible0;
    reg compose_palette_eligible1;
    reg compose_palette_eligible2;
    reg compose_palette_eligible3;

    wire [12:0] compose_palette_pixel0 = indexed_pixel(
        compose_palette_byte0, compose_palette_low_nibble0,
        compose_palette_bank0, compose_palette_eligible0, index_8_q,
        transparent_enable_q, transparent_index_q);
    wire [12:0] compose_palette_pixel1 = indexed_pixel(
        compose_palette_byte1, compose_palette_low_nibble1,
        compose_palette_bank1, compose_palette_eligible1, index_8_q,
        transparent_enable_q, transparent_index_q);
    wire [12:0] compose_palette_pixel2 = indexed_pixel(
        compose_palette_byte2, compose_palette_low_nibble2,
        compose_palette_bank2, compose_palette_eligible2, index_8_q,
        transparent_enable_q, transparent_index_q);
    wire [12:0] compose_palette_pixel3 = indexed_pixel(
        compose_palette_byte3, compose_palette_low_nibble3,
        compose_palette_bank3, compose_palette_eligible3, index_8_q,
        transparent_enable_q, transparent_index_q);

    reg compose_write_valid_q;
    reg [1:0] compose_write_slot_q;
    reg [8:0] compose_write_quad_q;
    reg [51:0] compose_write_pixels_q;

    always @(posedge build_clk) begin
        if (build_reset) begin
            compose_lookup_valid <= 1'b0;
            compose_lookup_slot <= 2'd0;
            compose_lookup_quad <= 9'd0;
            compose_lookup_source0 <= 5'd0;
            compose_lookup_source1 <= 5'd0;
            compose_lookup_source2 <= 5'd0;
            compose_lookup_source3 <= 5'd0;
            compose_lookup_bank0 <= 4'd0;
            compose_lookup_bank1 <= 4'd0;
            compose_lookup_bank2 <= 4'd0;
            compose_lookup_bank3 <= 4'd0;
            compose_lookup_eligible0 <= 1'b0;
            compose_lookup_eligible1 <= 1'b0;
            compose_lookup_eligible2 <= 1'b0;
            compose_lookup_eligible3 <= 1'b0;
            compose_lookup_half0 <= 1'b0;
            compose_lookup_half1 <= 1'b0;
            compose_lookup_half2 <= 1'b0;
            compose_lookup_half3 <= 1'b0;
            compose_lookup_low0 <= 64'd0;
            compose_lookup_low1 <= 64'd0;
            compose_lookup_low2 <= 64'd0;
            compose_lookup_low3 <= 64'd0;
            compose_lookup_high0 <= 64'd0;
            compose_lookup_high1 <= 64'd0;
            compose_lookup_high2 <= 64'd0;
            compose_lookup_high3 <= 64'd0;
            compose_palette_valid <= 1'b0;
            compose_palette_slot <= 2'd0;
            compose_palette_quad <= 9'd0;
            compose_palette_byte0 <= 8'd0;
            compose_palette_byte1 <= 8'd0;
            compose_palette_byte2 <= 8'd0;
            compose_palette_byte3 <= 8'd0;
            compose_palette_low_nibble0 <= 1'b0;
            compose_palette_low_nibble1 <= 1'b0;
            compose_palette_low_nibble2 <= 1'b0;
            compose_palette_low_nibble3 <= 1'b0;
            compose_palette_bank0 <= 4'd0;
            compose_palette_bank1 <= 4'd0;
            compose_palette_bank2 <= 4'd0;
            compose_palette_bank3 <= 4'd0;
            compose_palette_eligible0 <= 1'b0;
            compose_palette_eligible1 <= 1'b0;
            compose_palette_eligible2 <= 1'b0;
            compose_palette_eligible3 <= 1'b0;
            compose_write_valid_q <= 1'b0;
            compose_write_slot_q <= 2'd0;
            compose_write_quad_q <= 9'd0;
            compose_write_pixels_q <= 52'd0;
        end else begin
            compose_lookup_valid <= compose_write;
            compose_palette_valid <= compose_lookup_valid;
            compose_write_valid_q <= compose_palette_valid;

            if (compose_write) begin
                compose_lookup_slot <= build_slot_q;
                compose_lookup_quad <= compose_x[10:2];
                compose_lookup_source0 <= source_column(
                    compose_span0, compose_descriptor0, compose_x0);
                compose_lookup_source1 <= source_column(
                    compose_span1, compose_descriptor1, compose_x1);
                compose_lookup_source2 <= source_column(
                    compose_span2, compose_descriptor2, compose_x2);
                compose_lookup_source3 <= source_column(
                    compose_span3, compose_descriptor3, compose_x3);
                compose_lookup_bank0 <= compose_descriptor0[5:2];
                compose_lookup_bank1 <= compose_descriptor1[5:2];
                compose_lookup_bank2 <= compose_descriptor2[5:2];
                compose_lookup_bank3 <= compose_descriptor3[5:2];
                compose_lookup_eligible0 <= compose_x0 < OUTPUT_WIDTH &&
                    compose_span0[24] && compose_descriptor0[22];
                compose_lookup_eligible1 <= compose_x1 < OUTPUT_WIDTH &&
                    compose_span1[24] && compose_descriptor1[22];
                compose_lookup_eligible2 <= compose_x2 < OUTPUT_WIDTH &&
                    compose_span2[24] && compose_descriptor2[22];
                compose_lookup_eligible3 <= compose_x3 < OUTPUT_WIDTH &&
                    compose_span3[24] && compose_descriptor3[22];
                compose_lookup_half0 <= compose_pattern_half0;
                compose_lookup_half1 <= compose_pattern_half1;
                compose_lookup_half2 <= compose_pattern_half2;
                compose_lookup_half3 <= compose_pattern_half3;
                compose_lookup_low0 <= compose_pattern_low0;
                compose_lookup_low1 <= compose_pattern_low1;
                compose_lookup_low2 <= compose_pattern_low2;
                compose_lookup_low3 <= compose_pattern_low3;
                compose_lookup_high0 <= compose_pattern_high0;
                compose_lookup_high1 <= compose_pattern_high1;
                compose_lookup_high2 <= compose_pattern_high2;
                compose_lookup_high3 <= compose_pattern_high3;
            end

            if (compose_lookup_valid) begin
                compose_palette_slot <= compose_lookup_slot;
                compose_palette_quad <= compose_lookup_quad;
                compose_palette_byte0 <= compose_lookup_byte0;
                compose_palette_byte1 <= compose_lookup_byte1;
                compose_palette_byte2 <= compose_lookup_byte2;
                compose_palette_byte3 <= compose_lookup_byte3;
                compose_palette_low_nibble0 <= compose_lookup_source0[0];
                compose_palette_low_nibble1 <= compose_lookup_source1[0];
                compose_palette_low_nibble2 <= compose_lookup_source2[0];
                compose_palette_low_nibble3 <= compose_lookup_source3[0];
                compose_palette_bank0 <= compose_lookup_bank0;
                compose_palette_bank1 <= compose_lookup_bank1;
                compose_palette_bank2 <= compose_lookup_bank2;
                compose_palette_bank3 <= compose_lookup_bank3;
                compose_palette_eligible0 <= compose_lookup_eligible0;
                compose_palette_eligible1 <= compose_lookup_eligible1;
                compose_palette_eligible2 <= compose_lookup_eligible2;
                compose_palette_eligible3 <= compose_lookup_eligible3;
            end

            if (compose_palette_valid) begin
                compose_write_slot_q <= compose_palette_slot;
                compose_write_quad_q <= compose_palette_quad;
                compose_write_pixels_q <= {
                    compose_palette_pixel3, compose_palette_pixel2,
                    compose_palette_pixel1, compose_palette_pixel0
                };
            end
        end
    end

    astra_tile_line_store #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH)
    ) line_store_i (
        .build_clk(build_clk),
        .write_enable(compose_write_valid_q),
        .write_slot(compose_write_slot_q),
        .write_quad(compose_write_quad_q),
        .write_pixels(compose_write_pixels_q),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .read_slot(pixel_read_slot),
        .read_x(pixel_read_x),
        .read_valid(pixel_valid),
        .read_palette_bank(pixel_palette_bank),
        .read_index(pixel_index)
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
            descriptor_error <= 1'b0;
            fetch_error <= 1'b0;
            build_cycles <= 32'd0;
            map_read_bytes <= 32'd0;
            pattern_read_bytes <= 32'd0;
            build_slot_q <= 2'd0;
            line_y_q <= 11'd0;
            scroll_x_q <= 32'sd0;
            scroll_y_q <= 32'sd0;
            tile_16_q <= 1'b0;
            index_8_q <= 1'b0;
            map_width_log2_q <= 4'd0;
            map_height_log2_q <= 4'd0;
            wrap_x_q <= 1'b0;
            wrap_y_q <= 1'b0;
            transparent_enable_q <= 1'b0;
            transparent_index_q <= 8'd0;
            map_base_q <= 32'd0;
            pattern_base_q <= 32'd0;
            tile_count_q <= 17'd0;
            span_count <= 9'd0;
            tag_write_ptr <= 4'd0;
            tag_read_ptr <= 4'd0;
            tag_count <= 5'd0;
            request_count_q <= 5'd0;
            response_beat <= 3'd0;
            map_response_pending <= 1'b0;
            map_response_slot_q <= 8'd0;
            map_response_entry_q <= 32'd0;
            map_response_transport_valid_q <= 1'b0;
            pattern_issue_slot <= 8'd0;
            pattern_issue_needed <= 1'b0;
            pattern_issue_address <= 32'd0;
            pattern_issue_half <= 1'b0;
            pattern_issue_two_beat <= 1'b0;
            pattern_issue_tile_index <= 16'd0;
            pattern_issue_row <= 4'd0;
            compose_read_slot <= 8'd0;
            compose_active_span <= 25'd0;
            compose_active_descriptor <= 23'd0;
            compose_active_pattern_half <= 1'b0;
            compose_active_pattern_low <= 64'd0;
            compose_active_pattern_high <= 64'd0;
            compose_x <= 11'd0;
            compose_pixels_left <= 5'd0;
        end else begin
            done <= 1'b0;
            line_complete <= 1'b0;

            if (busy)
                build_cycles <= build_cycles + 32'd1;

            // Register map-response selection and transport status before the
            // descriptor RAM write. This keeps the active response-tag mux
            // out of the descriptor data/address path while sustaining one
            // completed map response per build clock.
            if (map_response_pending) begin
                descriptor_mem[map_response_slot_q] <=
                    map_response_descriptor;
                if (map_response_transport_valid_q &&
                    (map_response_entry_q[9:0] != 10'd0 ||
                     {1'b0, map_response_entry_q[31:16]} >= tile_count_q))
                    descriptor_error <= 1'b1;
                map_response_pending <= 1'b0;
            end

            if (span_accept) begin
                span_mem[span_slot] <= span_write_data;
                span_count <= {1'b0, span_slot} + 9'd1;
            end

            if (tag_push) begin
                if (ar_tag_q[TAG_KIND]) begin
                    pattern_half_mem[ar_tag_q[TAG_SLOT_HI:TAG_SLOT_LO]] <=
                        ar_pattern_half_q;
                    pattern_read_bytes <= pattern_read_bytes +
                        (ar_tag_q[TAG_TWO_BEAT] ? 32'd16 : 32'd8);
                end else begin
                    map_read_bytes <= map_read_bytes + 32'd8;
                end
            end

            case ({request_enqueue, tag_pop})
                2'b10: request_count_q <= request_count_q + 5'd1;
                2'b01: request_count_q <= request_count_q - 5'd1;
                default: request_count_q <= request_count_q;
            endcase

            if (axi_response_accept) begin
                if (tag_empty) begin
                    fetch_error <= 1'b1;
                end else begin
                    if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        m_axi_rlast != expected_last)
                        fetch_error <= 1'b1;

                    if (tag_head_pattern) begin
                        if (response_beat == 3'd0)
                            pattern_low_mem[tag_head_slot] <= m_axi_rdata;
                        else if (response_beat == 3'd1)
                            pattern_high_mem[tag_head_slot] <= m_axi_rdata;
                    end else if (response_beat == 3'd0) begin
                        map_response_pending <= 1'b1;
                        map_response_slot_q <= tag_head_slot;
                        map_response_entry_q <= response_map_entry;
                        map_response_transport_valid_q <=
                            m_axi_rid == AXI_ID &&
                            m_axi_rresp == 2'b00 &&
                            expected_last && m_axi_rlast;
                    end

                    if (m_axi_rlast) begin
                        response_beat <= 3'd0;
                    end else if (response_beat != 3'd7) begin
                        response_beat <= response_beat + 3'd1;
                    end
                end
            end

            case ({tag_push, tag_pop})
                2'b10: begin
                    tag_count <= tag_count + 5'd1;
                    if (!tag_empty) begin
                        tag_fifo[tag_write_ptr] <= pushed_tag;
                        tag_write_ptr <= tag_write_ptr + 4'd1;
                    end
                end
                2'b01: begin
                    tag_count <= tag_count - 5'd1;
                    if (tag_count > 5'd1) begin
                        tag_read_ptr <= tag_read_ptr + 4'd1;
                    end
                end
                2'b11: begin
                    if (tag_count > 5'd1) begin
                        tag_read_ptr <= tag_read_ptr + 4'd1;
                        tag_fifo[tag_write_ptr] <= pushed_tag;
                        tag_write_ptr <= tag_write_ptr + 4'd1;
                    end
                end
                default: tag_count <= tag_count;
            endcase

            case (state)
                ST_IDLE: begin
                    if (start) begin
                        config_error <= 1'b0;
                        descriptor_error <= 1'b0;
                        fetch_error <= 1'b0;
                        build_cycles <= 32'd0;
                        map_read_bytes <= 32'd0;
                        pattern_read_bytes <= 32'd0;
                        completed_slot <= build_slot;
                        slot_valid[build_slot] <= 1'b0;
                        span_count <= 9'd0;
                        tag_write_ptr <= 4'd0;
                        tag_read_ptr <= 4'd0;
                        tag_count <= 5'd0;
                        request_count_q <= 5'd0;
                        response_beat <= 3'd0;
                        map_response_pending <= 1'b0;
                        build_slot_q <= build_slot;
                        line_y_q <= line_y;
                        scroll_x_q <= scroll_x;
                        scroll_y_q <= scroll_y;
                        tile_16_q <= tile_16;
                        index_8_q <= index_8;
                        map_width_log2_q <= map_width_log2;
                        map_height_log2_q <= map_height_log2;
                        wrap_x_q <= wrap_x;
                        wrap_y_q <= wrap_y;
                        transparent_enable_q <= transparent_enable;
                        transparent_index_q <= transparent_index;
                        map_base_q <= map_base;
                        pattern_base_q <= pattern_base;
                        tile_count_q <= tile_count;
                        busy <= 1'b1;
                        if (TRUSTED_CONFIG != 0)
                            state <= ST_WALK_START;
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
                            state <= ST_WALK_START;
                        end
                    end
                end

                ST_WALK_START: state <= ST_MAP;

                ST_MAP: begin
                    if (walker_config_error) begin
                        config_error <= 1'b1;
                        state <= ST_MAP_DRAIN;
                    end else if (walker_finished && !span_buffer_valid) begin
                        state <= ST_MAP_DRAIN;
                    end
                end

                ST_MAP_DRAIN: begin
                    if (request_count_q == 5'd0 && !walker_busy &&
                        !map_response_pending) begin
                        pattern_issue_slot <= 8'd0;
                        state <= ST_PATTERN_LOAD;
                    end
                end

                ST_PATTERN_LOAD: begin
                    if ({1'b0, pattern_issue_slot} >= span_count) begin
                        state <= ST_PATTERN_DRAIN;
                    end else begin
                        pattern_issue_needed <= pattern_load_mapped &&
                            pattern_load_descriptor_valid;
                        pattern_issue_tile_index <= pattern_load_tile_index;
                        pattern_issue_row <= pattern_load_row;
                        pattern_issue_two_beat <= index_8_q && tile_16_q;
                        state <= ST_PATTERN_ADDRESS;
                    end
                end

                ST_PATTERN_ADDRESS: begin
                    pattern_issue_address <= {
                        pattern_issue_full[31:3], 3'b000
                    };
                    pattern_issue_half <= pattern_issue_full[2];
                    state <= ST_PATTERN_ISSUE;
                end

                ST_PATTERN_ISSUE: begin
                    if (!pattern_issue_needed || pattern_request_enqueue) begin
                        pattern_issue_slot <= pattern_issue_slot + 8'd1;
                        state <= ST_PATTERN_LOAD;
                    end
                end

                ST_PATTERN_DRAIN: begin
                    if (request_count_q == 5'd0) begin
                        compose_read_slot <= 8'd0;
                        state <= ST_COMPOSE_PRIME0;
                    end
                end

                ST_COMPOSE_PRIME0: state <= ST_COMPOSE_PRIME1;

                ST_COMPOSE_PRIME1: begin
                    compose_active_span <= compose_span_q;
                    compose_active_descriptor <= compose_descriptor_q;
                    compose_active_pattern_half <= compose_pattern_half_q;
                    compose_active_pattern_low <= compose_pattern_low_q;
                    compose_active_pattern_high <= compose_pattern_high_q;
                    compose_x <= 11'd0;
                    compose_pixels_left <= compose_span_q[12:8];
                    compose_read_slot <= 8'd1;
                    state <= ST_COMPOSE_PRIME2;
                end

                ST_COMPOSE_PRIME2: state <= ST_COMPOSE;

                ST_COMPOSE: begin
                    if (compose_last_quad) begin
                        state <= ST_COMPOSE_DRAIN;
                    end else begin
                        compose_x <= compose_x + 11'd4;
                        if (compose_advance_span) begin
                            compose_pixels_left <= compose_span_q[12:8] -
                                (5'd4 - compose_pixels_left);
                            compose_active_span <= compose_span_q;
                            compose_active_descriptor <=
                                compose_descriptor_q;
                            compose_active_pattern_half <=
                                compose_pattern_half_q;
                            compose_active_pattern_low <=
                                compose_pattern_low_q;
                            compose_active_pattern_high <=
                                compose_pattern_high_q;
                            compose_read_slot <= compose_read_slot + 8'd1;
                        end else begin
                            compose_pixels_left <= compose_pixels_left - 5'd4;
                        end
                    end
                end

                ST_COMPOSE_DRAIN: state <= ST_COMPOSE_DRAIN2;

                ST_COMPOSE_DRAIN2: state <= ST_FINISH;

                ST_FINISH: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    completed_slot <= build_slot_q;
                    if (!config_error && !descriptor_error && !fetch_error) begin
                        slot_valid[build_slot_q] <= 1'b1;
                        line_complete <= 1'b1;
                    end
                    state <= ST_IDLE;
                end

                default: begin
                    busy <= 1'b0;
                    config_error <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end

    wire unused_validator_busy = validator_busy;
endmodule

`default_nettype wire
