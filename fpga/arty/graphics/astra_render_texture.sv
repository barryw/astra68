// Copyright (c) 2026 Astra68 contributors
//
// Astraea texture engine: the TRIANGLES opcode (docs/TEXTURE_ENGINE.md).
// A read-only prepass validates every vertex before the shared writer
// starts, so a rejected command writes nothing. Each triangle is then set up
// once and rasterized over its clipped bounding box:
//
//   - coverage is the sign of three exact integer edge functions, stepped by
//     addition; the top-left rule is a -1 bias on non-top-left edges;
//   - every attribute (u, v, a, r, g, b) is the exact rounded barycentric
//     value floor((2N + A) / 2A), carried as a quotient/remainder pair and
//     stepped Bresenham-style, so no per-pixel division exists;
//   - covered pixels sample (nearest or four-tap linear), modulate, blend and
//     convert in a sequential pipeline that shares one multiplier bank.
//
// sw/userspace/graphics/src/texture_reference.c is the bit-exact model.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_texture #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = 6'd7
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
    input  wire [3:0]                   options,
    input  wire                         textured,
    input  wire [31:0]                  vertex_offset,
    input  wire [12:0]                  triangle_count,
    input  wire [31:0]                  destination_data_offset,
    input  wire [31:0]                  destination_pitch,
    input  wire [15:0]                  destination_width,
    input  wire [15:0]                  destination_height,
    input  wire [7:0]                   destination_format,
    input  wire [31:0]                  source_data_offset,
    input  wire [31:0]                  source_pitch,
    input  wire [15:0]                  source_width,
    input  wire [15:0]                  source_height,
    input  wire [7:0]                   source_format,
    input  wire [31:0]                  source_palette_offset,

    output reg                          busy,
    output reg                          done,
    output reg  [15:0]                  status,
    (* extract_enable = "no" *)
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
    output reg  [31:0]                  m_axi_araddr,
    output reg  [7:0]                   m_axi_arlen,
    output wire [2:0]                   m_axi_arsize,
    output wire [1:0]                   m_axi_arburst,
    output wire [3:0]                   m_axi_arcache,
    output wire [2:0]                   m_axi_arprot,
    output wire [3:0]                   m_axi_arqos,
    output reg                          m_axi_arvalid,
    input  wire                         m_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      m_axi_rid,
    input  wire [63:0]                  m_axi_rdata,
    input  wire [1:0]                   m_axi_rresp,
    input  wire                         m_axi_rlast,
    input  wire                         m_axi_rvalid,
    output reg                          m_axi_rready
);
    // Vertex x/y lie in [-2^23, 2^23): edge deltas are 25-bit, twice the
    // triangle area is at most 2^48, and every edge value inside the
    // bounding box fits EW bits. The divisor D = 2A is below 2^50.
    localparam integer EW = 52;
    localparam integer RW = 50;
    localparam integer AW = 88;

    localparam [6:0] ST_IDLE = 7'd0;
    localparam [6:0] ST_FORMAT = 7'd1;
    localparam [6:0] ST_VERTEX_AR = 7'd2;
    localparam [6:0] ST_VERTEX_R = 7'd3;
    localparam [6:0] ST_PREPASS_NEXT = 7'd4;
    localparam [6:0] ST_WRITER_START = 7'd5;
    localparam [6:0] ST_TRIANGLE_NEXT = 7'd6;
    localparam [6:0] ST_DELTAS = 7'd7;
    localparam [6:0] ST_MAC_ISSUE = 7'd8;
    localparam [6:0] ST_MAC_WAIT0 = 7'd9;
    localparam [6:0] ST_MAC_WAIT1 = 7'd10;
    localparam [6:0] ST_MAC_DONE = 7'd11;
    localparam [6:0] ST_AREA = 7'd12;
    localparam [6:0] ST_SWAP = 7'd13;
    localparam [6:0] ST_BOUNDS = 7'd14;
    localparam [6:0] ST_BOUNDS_CLIP = 7'd15;
    localparam [6:0] ST_BOUNDS_DECIDE = 7'd16;
    localparam [6:0] ST_BARRIER = 7'd17;
    localparam [6:0] ST_BARRIER_WAIT = 7'd18;
    localparam [6:0] ST_EDGE_PREPARE = 7'd19;
    localparam [6:0] ST_EDGE_STORE = 7'd20;
    localparam [6:0] ST_ATTRIBUTE_PREPARE = 7'd21;
    localparam [6:0] ST_ATTRIBUTE_DECIDE = 7'd22;
    localparam [6:0] ST_DIVIDE_PREPARE = 7'd23;
    localparam [6:0] ST_DIVIDE = 7'd24;
    localparam [6:0] ST_DIVIDE_FIX = 7'd25;
    localparam [6:0] ST_ATTRIBUTE_STORE = 7'd26;
    localparam [6:0] ST_RASTER_START = 7'd27;
    localparam [6:0] ST_RASTER = 7'd28;
    localparam [6:0] ST_ADVANCE = 7'd29;
    localparam [6:0] ST_STEP_ADD = 7'd30;
    localparam [6:0] ST_STEP_COMMIT = 7'd31;
    localparam [6:0] ST_PIXEL = 7'd32;
    localparam [6:0] ST_TAP_SETUP = 7'd33;
    localparam [6:0] ST_TAP = 7'd34;
    localparam [6:0] ST_TAP_ROW0 = 7'd35;
    localparam [6:0] ST_TAP_ROW1 = 7'd36;
    localparam [6:0] ST_TAP_ADDRESS = 7'd37;
    localparam [6:0] ST_TAP_FETCH = 7'd38;
    localparam [6:0] ST_TAP_DECODE = 7'd39;
    localparam [6:0] ST_PALETTE_FETCH = 7'd40;
    localparam [6:0] ST_PALETTE_DECODE = 7'd41;
    localparam [6:0] ST_TAP_MULTIPLY = 7'd42;
    localparam [6:0] ST_TAP_PRODUCT = 7'd43;
    localparam [6:0] ST_TAP_ACCUMULATE = 7'd44;
    localparam [6:0] ST_TAP_NEXT = 7'd45;
    localparam [6:0] ST_FILTER_ROUND = 7'd46;
    localparam [6:0] ST_MODULATE = 7'd47;
    localparam [6:0] ST_MODULATE_PRODUCT = 7'd48;
    localparam [6:0] ST_MODULATE_WAIT = 7'd49;
    localparam [6:0] ST_MODULATE_DIVIDE = 7'd50;
    localparam [6:0] ST_DESTINATION_FETCH = 7'd51;
    localparam [6:0] ST_DESTINATION_DECODE = 7'd52;
    localparam [6:0] ST_BLEND = 7'd53;
    localparam [6:0] ST_BLEND_PRODUCT = 7'd54;
    localparam [6:0] ST_BLEND_WAIT = 7'd55;
    localparam [6:0] ST_BLEND_DIVIDE = 7'd56;
    localparam [6:0] ST_BLEND_SUM = 7'd57;
    localparam [6:0] ST_PACK = 7'd58;
    localparam [6:0] ST_PACK_ROUND = 7'd59;
    localparam [6:0] ST_EMIT = 7'd60;
    localparam [6:0] ST_READ_AR = 7'd61;
    localparam [6:0] ST_READ_R = 7'd62;
    localparam [6:0] ST_FLUSH = 7'd63;
    localparam [6:0] ST_WAIT_WRITER = 7'd64;
    localparam [6:0] ST_ABORT = 7'd65;
    localparam [6:0] ST_FAIL = 7'd66;
    localparam [6:0] ST_FAIL_WAIT = 7'd67;
    localparam [6:0] ST_FINISH = 7'd68;
    localparam [6:0] ST_VERTEX_CHECK = 7'd69;
    localparam [6:0] ST_PACK_WAIT0 = 7'd70;
    localparam [6:0] ST_PACK_WAIT1 = 7'd71;
    localparam [6:0] ST_DIVIDE_SELECT = 7'd72;

    localparam [2:0] MAC_AREA = 3'd0;
    localparam [2:0] MAC_EDGE = 3'd1;
    localparam [2:0] MAC_ORIGIN = 3'd2;
    localparam [2:0] MAC_STEP_X = 3'd3;
    localparam [2:0] MAC_STEP_Y = 3'd4;

    localparam [1:0] READ_TEXEL = 2'd0;
    localparam [1:0] READ_PALETTE = 2'd1;
    localparam [1:0] READ_DESTINATION = 2'd2;

    localparam [2:0] BLEND_NONE = 3'd0;
    localparam [2:0] BLEND_BLEND = 3'd1;
    localparam [2:0] BLEND_ADD = 3'd2;
    localparam [2:0] BLEND_MOD = 3'd3;
    localparam [2:0] BLEND_MUL = 3'd4;

    assign m_axi_arid = AXI_ID;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b0000;

    function automatic [7:0] beat_byte(input [63:0] beat, input [2:0] lane);
        begin
            beat_byte = beat[lane * 8 +: 8];
        end
    endfunction

    function automatic [31:0] beat_word(input [63:0] beat, input [2:0] lane);
        begin
            beat_word = {beat_byte(beat, lane), beat_byte(beat, lane + 3'd1),
                         beat_byte(beat, lane + 3'd2),
                         beat_byte(beat, lane + 3'd3)};
        end
    endfunction

    function automatic [31:0] swap32(input [31:0] value);
        begin
            swap32 = {value[7:0], value[15:8], value[23:16], value[31:24]};
        end
    endfunction

    // Exact (x + 127) / 255 for x <= 65535, as the glyph engine's rounding.
    function automatic [7:0] divide_255_round(input [16:0] numerator);
        reg [17:0] adjusted;
        begin
            adjusted = {1'b0, numerator} + 18'd128;
            divide_255_round = (adjusted + (adjusted >> 8)) >> 8;
        end
    endfunction

    function automatic [31:0] expand_rgb565(input [15:0] pixel);
        begin
            expand_rgb565 = {8'hff, pixel[15:11], pixel[15:13],
                             pixel[10:5], pixel[10:9], pixel[4:0],
                             pixel[4:2]};
        end
    endfunction

    function automatic [11:0] clamp_coordinate(input signed [16:0] value,
                                               input [15:0] extent);
        begin
            if (value < 0)
                clamp_coordinate = 12'd0;
            else if ($signed({1'b0, value[15:0]}) >= $signed({1'b0, extent}))
                clamp_coordinate = extent[11:0] - 12'd1;
            else
                clamp_coordinate = value[11:0];
        end
    endfunction

    function automatic signed [26:0] low27(input [63:0] value);
        begin
            low27 = {1'b0, value[25:0]};
        end
    endfunction

    function automatic signed [26:0] min3_27(input signed [26:0] a,
                                             input signed [26:0] b,
                                             input signed [26:0] c);
        reg signed [26:0] m;
        begin
            m = a < b ? a : b;
            min3_27 = m < c ? m : c;
        end
    endfunction

    function automatic signed [26:0] max3_27(input signed [26:0] a,
                                             input signed [26:0] b,
                                             input signed [26:0] c);
        reg signed [26:0] m;
        begin
            m = a > b ? a : b;
            max3_27 = m > c ? m : c;
        end
    endfunction

    (* extract_reset = "no" *) reg [6:0] state;
    reg [6:0] read_return_q;
    reg [1:0] read_kind_q;
    reg abort_pending_q;
    reg writer_started_q;
    reg writer_barrier_ready_q;

    reg [31:0] base_address_q;
    reg [31:0] vertex_base_q;
    reg [31:0] vertex_address_q;
    reg [13:0] vertex_index_q;
    reg [13:0] vertex_last_q;
    reg [1:0] beat_index_q;
    reg vertex_bad_q;
    reg [1:0] corner_q;
    reg [12:0] triangle_index_q;
    reg [12:0] triangle_last_q;
    reg writes_pending_q;

    reg [2:0] blend_q;
    reg linear_q;
    reg textured_q;
    reg [7:0] destination_format_q;
    reg [7:0] source_format_q;
    reg [2:0] destination_bpp_q;
    reg [1:0] source_shift_q;
    reg [31:0] destination_pitch_q;
    reg [31:0] source_pitch_q;
    reg [15:0] source_width_q, source_height_q;
    reg [31:0] destination_base_q;
    reg [31:0] source_base_q;
    reg [31:0] palette_base_q;
    reg signed [16:0] clip_left_q, clip_top_q, clip_right_q, clip_bottom_q;

    // Current triangle, after the winding swap.
    reg signed [23:0] vertex_x_q [0:2];
    reg signed [23:0] vertex_y_q [0:2];
    reg [31:0] vertex_u_q [0:2];
    reg [31:0] vertex_v_q [0:2];
    reg [31:0] vertex_color_q [0:2];
    // Edge i is opposite vertex i: (v1, v2), (v2, v0), (v0, v1).
    reg signed [24:0] edge_dx_q [0:2];
    reg signed [24:0] edge_dy_q [0:2];
    reg signed [24:0] edge_rel_x_q, edge_rel_y_q;
    reg [1:0] edge_index_q;
    reg signed [EW-1:0] weight1_q, weight2_q;
    reg signed [EW-1:0] area_q;
    reg swapped_q;

    reg signed [16:0] box_left_q, box_right_q, box_top_q, box_bottom_q;
    reg [11:0] left_q, right_q, top_q, bottom_q;
    reg [11:0] pixel_x_q, pixel_y_q;
    reg last_x_q, last_y_q;
    reg [31:0] destination_row_q;
    reg [31:0] destination_address_q;

    // Shared 27x27 signed setup multiplier and wide accumulator.
    reg [2:0] mac_kind_q;
    reg [2:0] mac_index_q;
    reg [2:0] mac_last_q;
    reg signed [26:0] mac_a_q, mac_b_q;
    reg [1:0] mac_shift_q, mac_shift2_q;
    reg mac_negate_q, mac_negate2_q;
    reg mac_clear_q, mac_clear2_q;
    reg mac_valid_q, mac_valid2_q;
    (* multstyle = "dsp" *) reg signed [53:0] mac_product_q;
    reg signed [AW-1:0] accumulator_q;

    // Attribute setup: 0 u, 1 v, 2 a, 3 r, 4 g, 5 b.
    reg [2:0] attribute_q;
    reg [1:0] attribute_phase_q;
    reg signed [32:0] attribute_d1_q, attribute_d2_q;
    reg [31:0] attribute_a0_q;
    reg attribute_constant_q;

    // Sequential restoring divider for (quotient mod 2^32, remainder).
    reg signed [AW-1:0] divide_dividend_q;
    reg [AW-1:0] divide_magnitude_q;
    reg divide_negative_q;
    reg [RW-1:0] divide_remainder_q;
    reg [31:0] divide_quotient_q;
    reg [6:0] divide_count_q;
    wire [RW-1:0] divisor = {area_q[RW-2:0], 1'b0};

    // Attribute store/step controls, consumed by the per-attribute blocks.
    reg step_add_q;
    reg step_commit_q;
    reg step_row_q;

    // Pixel pipeline.
    reg [31:0] pixel_u_q, pixel_v_q, pixel_color_q;
    reg signed [16:0] tap_x0_q, tap_y0_q;
    reg [11:0] tap_cx0_q, tap_cx1_q, tap_cy0_q, tap_cy1_q;
    reg [8:0] weight_x0_q, weight_x1_q, weight_y0_q, weight_y1_q;
    reg [1:0] tap_q;
    reg [16:0] tap_weight_q;
    reg [11:0] tap_row_q, tap_column_q;
    reg [31:0] texel_address_q;
    reg [31:0] palette_address_q;
    (* multstyle = "dsp" *) reg [44:0] row_product_q;
    reg [12:0] row_operand_q;
    reg [31:0] row_pitch_q;
    reg [31:0] texel_q;
    reg [24:0] filter_q [0:3];
    reg [31:0] source_q;
    reg [31:0] destination_q;
    reg [7:0] blend_add_q [0:3];
    reg [9:0] blend_sum_q [0:3];
    reg [31:0] result_q;
    reg [31:0] output_q;

    reg texel_cache_valid_q [0:1];
    reg [28:0] texel_cache_tag_q [0:1];
    reg [63:0] texel_cache_data_q [0:1];
    reg palette_cache_valid_q;
    reg [28:0] palette_cache_tag_q;
    reg [63:0] palette_cache_data_q;
    reg destination_cache_valid_q;
    reg [28:0] destination_cache_tag_q;
    reg [63:0] destination_cache_data_q;

    // Eight shared 8x17 multipliers: taps, modulation and blending.
    reg [7:0] bank_a_q [0:7];
    reg [16:0] bank_b_q [0:7];
    (* multstyle = "dsp" *) reg [24:0] bank_product_q [0:7];
    reg [7:0] bank_quotient_q [0:7];

    integer lane_index;
    always @(posedge clk) begin
        for (lane_index = 0; lane_index < 8; lane_index = lane_index + 1) begin
            bank_product_q[lane_index] <=
                bank_a_q[lane_index] * bank_b_q[lane_index];
            bank_quotient_q[lane_index] <=
                divide_255_round(bank_product_q[lane_index][16:0]);
        end
        row_product_q <= row_operand_q * row_pitch_q;
        mac_product_q <= mac_a_q * mac_b_q;
    end

    // ---------------------------------------------------------------------
    // Edge functions: biased current and row-start values, stepped exactly.
    // ---------------------------------------------------------------------
    reg signed [EW-1:0] edge_current_q [0:2];
    reg signed [EW-1:0] edge_row_q [0:2];
    reg signed [EW-1:0] edge_next_q [0:2];
    reg signed [32:0] edge_step_x_q [0:2];
    reg signed [32:0] edge_step_y_q [0:2];
    wire covered = !edge_current_q[0][EW-1] && !edge_current_q[1][EW-1] &&
                   !edge_current_q[2][EW-1];

    // ---------------------------------------------------------------------
    // Attributes: (quotient, remainder) with 2N + A = Q * 2A + R.
    // ---------------------------------------------------------------------
    wire [31:0] attribute_quotient [0:5];
    wire attribute_store = state == ST_ATTRIBUTE_STORE;

    genvar gi;
    generate
        for (gi = 0; gi < 6; gi = gi + 1) begin : attribute_g
            localparam integer QW = gi < 2 ? 32 : 8;
            reg [QW-1:0] q_current, q_row, q_step_x, q_step_y, q_sum,
                q_sum_carry;
            reg [RW-1:0] r_current, r_row, r_step_x, r_step_y;
            reg [RW:0] r_plain;
            // r + step - 2A: non-negative means the remainder wraps and Q
            // carries. Computed at commit from r_plain, so no step - 2A
            // copies are kept.
            wire signed [RW+1:0] r_wrapped = $signed({1'b0, r_plain}) -
                $signed({2'b00, divisor});

            assign attribute_quotient[gi] = {{(32-QW){1'b0}}, q_current};

            always @(posedge clk) begin
                if (attribute_store && attribute_q == gi) begin
                    case (attribute_phase_q)
                        2'd0: begin
                            q_current <= attribute_a0_q[QW-1:0] +
                                divide_quotient_q[QW-1:0];
                            q_row <= attribute_a0_q[QW-1:0] +
                                divide_quotient_q[QW-1:0];
                            r_current <= divide_remainder_q;
                            r_row <= divide_remainder_q;
                        end
                        2'd1: begin
                            q_step_x <= divide_quotient_q[QW-1:0];
                            r_step_x <= divide_remainder_q;
                        end
                        default: begin
                            q_step_y <= divide_quotient_q[QW-1:0];
                            r_step_y <= divide_remainder_q;
                        end
                    endcase
                end
                if (step_add_q) begin
                    r_plain <= step_row_q ?
                        {1'b0, r_row} + {1'b0, r_step_y} :
                        {1'b0, r_current} + {1'b0, r_step_x};
                    q_sum <= step_row_q ? q_row + q_step_y :
                        q_current + q_step_x;
                    q_sum_carry <= step_row_q ? q_row + q_step_y + 1'b1 :
                        q_current + q_step_x + 1'b1;
                end
                if (step_commit_q) begin
                    q_current <= r_wrapped[RW+1] ? q_sum : q_sum_carry;
                    r_current <= r_wrapped[RW+1] ? r_plain[RW-1:0] :
                        r_wrapped[RW-1:0];
                    if (step_row_q) begin
                        q_row <= r_wrapped[RW+1] ? q_sum : q_sum_carry;
                        r_row <= r_wrapped[RW+1] ? r_plain[RW-1:0] :
                            r_wrapped[RW-1:0];
                    end
                end
            end
        end
    endgenerate

    // ---------------------------------------------------------------------
    // Setup multiplier operand selection.
    // ---------------------------------------------------------------------
    reg signed [26:0] mac_operand_a, mac_operand_b;
    reg [1:0] mac_operand_shift;
    reg mac_operand_negate;
    wire signed [26:0] d1_low = low27({31'd0, attribute_d1_q});
    wire signed [26:0] d2_low = low27({31'd0, attribute_d2_q});
    wire signed [26:0] d1_high = {{20{attribute_d1_q[32]}},
                                  attribute_d1_q[32:26]};
    wire signed [26:0] d2_high = {{20{attribute_d2_q[32]}},
                                  attribute_d2_q[32:26]};
    wire signed [26:0] w1_low = low27({12'd0, weight1_q});
    wire signed [26:0] w2_low = low27({12'd0, weight2_q});
    wire signed [26:0] w1_high = {weight1_q[EW-1], weight1_q[EW-1:26]};
    wire signed [26:0] w2_high = {weight2_q[EW-1], weight2_q[EW-1:26]};

    always @* begin
        mac_operand_a = 27'sd0;
        mac_operand_b = 27'sd0;
        mac_operand_shift = 2'd0;
        mac_operand_negate = 1'b0;
        case (mac_kind_q)
            MAC_AREA: begin
                // A2 = dy2 * dx1 - dx2 * dy1
                mac_operand_a = mac_index_q[0] ? edge_dx_q[2] : edge_dy_q[2];
                mac_operand_b = mac_index_q[0] ? edge_dy_q[1] : edge_dx_q[1];
                mac_operand_negate = mac_index_q[0];
            end
            MAC_EDGE: begin
                // E = dx * (cy - a.y) - dy * (cx - a.x)
                mac_operand_a = mac_index_q[0] ?
                    edge_dy_q[edge_index_q] : edge_dx_q[edge_index_q];
                mac_operand_b = mac_index_q[0] ? edge_rel_x_q : edge_rel_y_q;
                mac_operand_negate = mac_index_q[0];
            end
            MAC_ORIGIN: begin
                // d1 * w1 + d2 * w2 in four 27x27 partial products each.
                mac_operand_a = mac_index_q[1] ?
                    (mac_index_q[2] ? d2_high : d1_high) :
                    (mac_index_q[2] ? d2_low : d1_low);
                mac_operand_b = mac_index_q[0] ?
                    (mac_index_q[2] ? w2_high : w1_high) :
                    (mac_index_q[2] ? w2_low : w1_low);
                mac_operand_shift = {1'b0, mac_index_q[1]} +
                                    {1'b0, mac_index_q[0]};
            end
            default: begin
                // d1 * e1 + d2 * e2, e = dy (x step) or dx (y step).
                mac_operand_a = mac_index_q[0] ?
                    (mac_index_q[1] ? d2_high : d1_high) :
                    (mac_index_q[1] ? d2_low : d1_low);
                mac_operand_b = mac_kind_q == MAC_STEP_X ?
                    edge_dy_q[mac_index_q[1] ? 2 : 1] :
                    edge_dx_q[mac_index_q[1] ? 2 : 1];
                mac_operand_shift = {1'b0, mac_index_q[0]};
            end
        endcase
    end

    wire signed [AW-1:0] mac_term =
        $signed({{(AW-54){mac_product_q[53]}}, mac_product_q}) <<<
        (mac_shift2_q == 2'd2 ? 52 : mac_shift2_q == 2'd1 ? 26 : 0);

    // ---------------------------------------------------------------------
    // Combinational helpers.
    // ---------------------------------------------------------------------
    wire [63:0] rdata_words = {swap32(m_axi_rdata[63:32]),
                               swap32(m_axi_rdata[31:0])};
    wire [31:0] word_low = rdata_words[31:0];
    wire [31:0] word_high = rdata_words[63:32];
    wire signed [26:0] vx0 = vertex_x_q[0], vx1 = vertex_x_q[1],
                       vx2 = vertex_x_q[2];
    wire signed [26:0] vy0 = vertex_y_q[0], vy1 = vertex_y_q[1],
                       vy2 = vertex_y_q[2];
    wire signed [26:0] min_x = min3_27(vx0, vx1, vx2);
    wire signed [26:0] max_x = max3_27(vx0, vx1, vx2);
    wire signed [26:0] min_y = min3_27(vy0, vy1, vy2);
    wire signed [26:0] max_y = max3_27(vy0, vy1, vy2);
    wire signed [26:0] min_x_center = min_x + 27'sd127;
    wire signed [26:0] max_x_center = max_x - 27'sd128;
    wire signed [26:0] min_y_center = min_y + 27'sd127;
    wire signed [26:0] max_y_center = max_y - 27'sd128;

    wire [20:0] origin_x = {1'b0, left_q, 8'd128};
    wire [20:0] origin_y = {1'b0, top_q, 8'd128};
    wire [1:0] edge_a = edge_index_q == 2'd0 ? 2'd1 :
                        edge_index_q == 2'd1 ? 2'd2 : 2'd0;
    wire edge_top_left = edge_dy_q[edge_index_q] < 0 ||
        (edge_dy_q[edge_index_q] == 0 && edge_dx_q[edge_index_q] > 0);

    wire [31:0] attribute_vertex0 =
        attribute_q == 3'd0 ? vertex_u_q[0] :
        attribute_q == 3'd1 ? vertex_v_q[0] :
        {24'd0, vertex_color_q[0][31 - 8 * (attribute_q - 3'd2) -: 8]};
    wire [31:0] attribute_vertex1 =
        attribute_q == 3'd0 ? vertex_u_q[1] :
        attribute_q == 3'd1 ? vertex_v_q[1] :
        {24'd0, vertex_color_q[1][31 - 8 * (attribute_q - 3'd2) -: 8]};
    wire [31:0] attribute_vertex2 =
        attribute_q == 3'd0 ? vertex_u_q[2] :
        attribute_q == 3'd1 ? vertex_v_q[2] :
        {24'd0, vertex_color_q[2][31 - 8 * (attribute_q - 3'd2) -: 8]};
    wire attribute_signed = attribute_q < 3'd2;

    wire signed [AW-1:0] origin_dividend =
        $signed({{(AW-EW){area_q[EW-1]}}, area_q}) + (accumulator_q <<< 1);
    wire signed [AW-1:0] step_dividend = accumulator_q <<< 9;
    wire signed [AW-1:0] selected_dividend =
        attribute_phase_q == 2'd0 ? origin_dividend :
        attribute_phase_q == 2'd1 ? -step_dividend : step_dividend;
    wire [RW:0] divide_shifted = {divide_remainder_q, divide_magnitude_q[AW-1]};
    wire signed [RW+1:0] divide_difference =
        $signed({1'b0, divide_shifted}) - $signed({2'b00, divisor});

    // Sampling geometry at the interpolated (u, v).
    wire signed [32:0] u_linear = $signed({pixel_u_q[31], pixel_u_q}) -
                                  33'sd32768;
    wire signed [32:0] v_linear = $signed({pixel_v_q[31], pixel_v_q}) -
                                  33'sd32768;
    wire signed [16:0] sample_x0 = linear_q ? u_linear[32:16] :
                                   {pixel_u_q[31], pixel_u_q[31:16]};
    wire signed [16:0] sample_y0 = linear_q ? v_linear[32:16] :
                                   {pixel_v_q[31], pixel_v_q[31:16]};
    wire [7:0] sample_fx = linear_q ? u_linear[15:8] : 8'd0;
    wire [7:0] sample_fy = linear_q ? v_linear[15:8] : 8'd0;
    wire [8:0] tap_wx = tap_q[0] ? weight_x1_q : weight_x0_q;
    wire [8:0] tap_wy = tap_q[1] ? weight_y1_q : weight_y0_q;
    wire [16:0] tap_weight = tap_wx * tap_wy;

    wire texel_entry = tap_q[1];
    wire texel_hit = texel_cache_valid_q[texel_entry] &&
        texel_cache_tag_q[texel_entry] == texel_address_q[31:3];
    wire [63:0] texel_beat = texel_cache_data_q[texel_entry];
    wire [2:0] texel_lane = texel_address_q[2:0];
    wire [7:0] texel_byte = beat_byte(texel_beat, texel_lane);
    wire [31:0] texel_word = beat_word(texel_beat, texel_lane);
    wire [31:0] texel_argb =
        source_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            expand_rgb565({texel_byte, beat_byte(texel_beat,
                                                 texel_lane + 3'd1)}) :
        source_format_q == `ASTRA_RENDER_FORMAT_XRGB8888 ?
            {8'hff, texel_word[23:0]} :
        source_format_q == `ASTRA_RENDER_FORMAT_A8 ?
            {texel_byte, 24'hffffff} : texel_word;
    wire palette_hit = palette_cache_valid_q &&
        palette_cache_tag_q == palette_address_q[31:3];
    wire [31:0] palette_argb = beat_word(palette_cache_data_q,
                                         palette_address_q[2:0]);

    wire destination_hit = destination_cache_valid_q &&
        destination_cache_tag_q == destination_address_q[31:3];
    wire [2:0] destination_lane = destination_address_q[2:0];
    wire [31:0] destination_word = beat_word(destination_cache_data_q,
                                             destination_lane);
    wire [31:0] destination_argb =
        destination_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            expand_rgb565(destination_word[31:16]) :
        destination_format_q == `ASTRA_RENDER_FORMAT_XRGB8888 ?
            {8'hff, destination_word[23:0]} : destination_word;
    wire [63:0] emitted_beat =
        destination_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            (destination_cache_data_q &
             ~(64'hffff << (destination_lane * 8))) |
            ({48'd0, output_q[7:0], output_q[15:8]} <<
             (destination_lane * 8)) :
            (destination_cache_data_q &
             ~(64'hffffffff << (destination_lane * 8))) |
            ({32'd0, swap32(output_q)} << (destination_lane * 8));

    // Blend operand selection (docs/TEXTURE_ENGINE.md section 6). Channel 0
    // is alpha; out = min(255, m(x1, y1) + m(x2, y2) + add).
    wire [7:0] source_alpha = source_q[31:24];
    function automatic [7:0] argb_channel(input [31:0] value,
                                          input [1:0] channel);
        begin
            argb_channel = value[31 - 8 * channel -: 8];
        end
    endfunction

    // ---------------------------------------------------------------------
    // Engine control.
    // ---------------------------------------------------------------------
    task automatic start_read(input [31:0] address, input [1:0] kind,
                              input [6:0] return_state);
        begin
            m_axi_araddr <= {address[31:3], 3'b000};
            m_axi_arlen <= 8'd0;
            m_axi_arvalid <= 1'b1;
            read_kind_q <= kind;
            read_return_q <= return_state;
            state <= ST_READ_AR;
        end
    endtask

    task automatic fail(input [15:0] failure_status, input [31:0] detail);
        begin
            status <= failure_status;
            fault_detail <= detail;
            m_axi_arvalid <= 1'b0;
            m_axi_rready <= 1'b0;
            pixel_valid <= 1'b0;
            state <= ST_FAIL;
        end
    endtask

    task automatic start_mac(input [2:0] kind, input [2:0] last);
        begin
            mac_kind_q <= kind;
            mac_index_q <= 3'd0;
            mac_last_q <= last;
            state <= ST_MAC_ISSUE;
        end
    endtask

    integer i;
    always @(posedge clk) begin
        done <= 1'b0;
        writer_start <= 1'b0;
        writer_abort <= 1'b0;
        writer_flush <= 1'b0;
        writer_barrier <= 1'b0;
        writer_barrier_ready_q <= writer_barrier_ready;
        step_add_q <= 1'b0;
        step_commit_q <= 1'b0;
        mac_valid_q <= 1'b0;
        mac_valid2_q <= mac_valid_q;
        mac_shift2_q <= mac_shift_q;
        mac_negate2_q <= mac_negate_q;
        mac_clear2_q <= mac_clear_q;
        if (mac_valid2_q)
            accumulator_q <= (mac_clear2_q ? {AW{1'b0}} : accumulator_q) +
                (mac_negate2_q ? -mac_term : mac_term);

        if (reset) begin
            state <= ST_IDLE;
            busy <= 1'b0;
            status <= `ASTRA_RENDER_STATUS_OK;
            fault_detail <= 32'd0;
            completed_pixels <= 32'd0;
            pixel_valid <= 1'b0;
            m_axi_arvalid <= 1'b0;
            m_axi_rready <= 1'b0;
            abort_pending_q <= 1'b0;
            writer_started_q <= 1'b0;
            mac_valid2_q <= 1'b0;
            texel_cache_valid_q[0] <= 1'b0;
            texel_cache_valid_q[1] <= 1'b0;
            palette_cache_valid_q <= 1'b0;
            destination_cache_valid_q <= 1'b0;
        end else begin
            if (abort && busy)
                abort_pending_q <= 1'b1;

            if (abort_pending_q && state != ST_ABORT && state != ST_FAIL &&
                state != ST_FAIL_WAIT && state != ST_IDLE) begin
                m_axi_arvalid <= 1'b0;
                m_axi_rready <= 1'b0;
                pixel_valid <= 1'b0;
                writer_abort <= writer_started_q;
                state <= ST_ABORT;
            end else case (state)
                ST_IDLE: if (start) begin
                    busy <= 1'b1;
                    status <= `ASTRA_RENDER_STATUS_OK;
                    fault_detail <= 32'd0;
                    completed_pixels <= 32'd0;
                    abort_pending_q <= 1'b0;
                    writer_started_q <= 1'b0;
                    blend_q <= options[2:0];
                    linear_q <= options[3];
                    textured_q <= textured;
                    destination_format_q <= destination_format;
                    source_format_q <= source_format;
                    destination_bpp_q <=
                        destination_format == `ASTRA_RENDER_FORMAT_RGB565 ?
                            3'd2 : 3'd4;
                    source_shift_q <=
                        source_format == `ASTRA_RENDER_FORMAT_RGB565 ? 2'd1 :
                        source_format == `ASTRA_RENDER_FORMAT_XRGB8888 ||
                        source_format == `ASTRA_RENDER_FORMAT_ARGB8888 ?
                            2'd2 : 2'd0;
                    destination_pitch_q <= destination_pitch;
                    source_pitch_q <= source_pitch;
                    source_width_q <= source_width;
                    source_height_q <= source_height;
                    base_address_q <= arena_base;
                    vertex_base_q <= arena_base + vertex_offset;
                    vertex_address_q <= arena_base + vertex_offset;
                    corner_q <= 2'd0;
                    destination_base_q <= arena_base + destination_data_offset;
                    source_base_q <= arena_base + source_data_offset;
                    palette_base_q <= arena_base + source_palette_offset;
                    clip_left_q <= clip_left < 0 ? 17'sd0 :
                        $signed({clip_left[15], clip_left});
                    clip_top_q <= clip_top < 0 ? 17'sd0 :
                        $signed({clip_top[15], clip_top});
                    clip_right_q <= ($signed({clip_right[15], clip_right}) <
                        $signed({1'b0, destination_width}) ?
                            $signed({clip_right[15], clip_right}) :
                            $signed({1'b0, destination_width})) - 17'sd1;
                    clip_bottom_q <= ($signed({clip_bottom[15], clip_bottom}) <
                        $signed({1'b0, destination_height}) ?
                            $signed({clip_bottom[15], clip_bottom}) :
                            $signed({1'b0, destination_height})) - 17'sd1;
                    vertex_index_q <= 14'd0;
                    vertex_last_q <= {1'b0, triangle_count} * 14'd3 - 14'd1;
                    triangle_last_q <= triangle_count - 13'd1;
                    texel_cache_valid_q[0] <= 1'b0;
                    texel_cache_valid_q[1] <= 1'b0;
                    palette_cache_valid_q <= 1'b0;
                    destination_cache_valid_q <= 1'b0;
                    state <= ST_FORMAT;
                end

                // Unsupported formats fail before any DMA.
                ST_FORMAT: begin
                    if (!(destination_format_q == `ASTRA_RENDER_FORMAT_RGB565 ||
                          destination_format_q == `ASTRA_RENDER_FORMAT_XRGB8888 ||
                          destination_format_q == `ASTRA_RENDER_FORMAT_ARGB8888) ||
                        (textured_q &&
                         !(source_format_q == `ASTRA_RENDER_FORMAT_RGB565 ||
                           source_format_q == `ASTRA_RENDER_FORMAT_XRGB8888 ||
                           source_format_q == `ASTRA_RENDER_FORMAT_ARGB8888 ||
                           source_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ||
                           source_format_q == `ASTRA_RENDER_FORMAT_A8))) begin
                        fail(`ASTRA_RENDER_STATUS_UNSUPPORTED, 32'h000c0001);
                    end else begin
                        m_axi_araddr <= vertex_address_q;
                        m_axi_arlen <= 8'd3;
                        m_axi_arvalid <= 1'b1;
                        state <= ST_VERTEX_AR;
                    end
                end

                // Vertex bursts: prepass (checks) or triangle fetch (capture).
                ST_VERTEX_AR: if (m_axi_arvalid && m_axi_arready) begin
                    m_axi_arvalid <= 1'b0;
                    m_axi_rready <= 1'b1;
                    beat_index_q <= 2'd0;
                    vertex_bad_q <= 1'b0;
                    state <= ST_VERTEX_R;
                end

                ST_VERTEX_R: if (m_axi_rvalid && m_axi_rready) begin
                    if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        m_axi_rlast != (beat_index_q == 2'd3)) begin
                        fail(`ASTRA_RENDER_STATUS_AXI_READ,
                             {18'h00030, vertex_index_q});
                    end else begin
                        case (beat_index_q)
                            2'd0: begin
                                vertex_bad_q <= vertex_bad_q ||
                                    !(&word_low[31:23] || ~|word_low[31:23]) ||
                                    !(&word_high[31:23] ||
                                      ~|word_high[31:23]);
                                vertex_x_q[corner_q] <= word_low[23:0];
                                vertex_y_q[corner_q] <= word_high[23:0];
                            end
                            2'd1: begin
                                vertex_bad_q <= vertex_bad_q || (!textured_q &&
                                    (word_low != 32'd0 || word_high != 32'd0));
                                vertex_u_q[corner_q] <= word_low;
                                vertex_v_q[corner_q] <= word_high;
                            end
                            2'd2: begin
                                vertex_bad_q <= vertex_bad_q ||
                                    word_high != 32'd0;
                                vertex_color_q[corner_q] <= word_low;
                            end
                            default: begin
                                vertex_bad_q <= vertex_bad_q ||
                                    word_low != 32'd0 || word_high != 32'd0;
                            end
                        endcase
                        beat_index_q <= beat_index_q + 2'd1;
                        if (beat_index_q == 2'd3) begin
                            m_axi_rready <= 1'b0;
                            state <= ST_VERTEX_CHECK;
                        end
                    end
                end

                ST_VERTEX_CHECK: begin
                    if (!writer_started_q) begin
                        if (vertex_bad_q)
                            fail(`ASTRA_RENDER_STATUS_BAD_RANGE,
                                 {18'h00031, vertex_index_q});
                        else
                            state <= ST_PREPASS_NEXT;
                    end else if (corner_q == 2'd2) begin
                        corner_q <= 2'd0;
                        state <= ST_DELTAS;
                    end else begin
                        corner_q <= corner_q + 2'd1;
                        vertex_index_q <= vertex_index_q + 14'd1;
                        vertex_address_q <= vertex_address_q + 32'd32;
                        m_axi_araddr <= vertex_address_q + 32'd32;
                        m_axi_arvalid <= 1'b1;
                        state <= ST_VERTEX_AR;
                    end
                end

                ST_PREPASS_NEXT: begin
                    if (vertex_index_q == vertex_last_q) begin
                        state <= ST_WRITER_START;
                    end else begin
                        vertex_index_q <= vertex_index_q + 14'd1;
                        vertex_address_q <= vertex_address_q + 32'd32;
                        m_axi_araddr <= vertex_address_q + 32'd32;
                        m_axi_arvalid <= 1'b1;
                        state <= ST_VERTEX_AR;
                    end
                end

                ST_WRITER_START: begin
                    writer_start <= 1'b1;
                    writer_started_q <= 1'b1;
                    triangle_index_q <= 13'd0;
                    vertex_index_q <= 14'd0;
                    corner_q <= 2'd0;
                    writes_pending_q <= 1'b0;
                    swapped_q <= 1'b0;
                    vertex_address_q <= vertex_base_q;
                    m_axi_araddr <= vertex_base_q;
                    m_axi_arlen <= 8'd3;
                    m_axi_arvalid <= 1'b1;
                    state <= ST_VERTEX_AR;
                end

                // ---------------- triangle setup ----------------
                ST_DELTAS: begin
                    edge_dx_q[0] <= vertex_x_q[2] - vertex_x_q[1];
                    edge_dy_q[0] <= vertex_y_q[2] - vertex_y_q[1];
                    edge_dx_q[1] <= vertex_x_q[0] - vertex_x_q[2];
                    edge_dy_q[1] <= vertex_y_q[0] - vertex_y_q[2];
                    edge_dx_q[2] <= vertex_x_q[1] - vertex_x_q[0];
                    edge_dy_q[2] <= vertex_y_q[1] - vertex_y_q[0];
                    if (swapped_q)
                        state <= ST_BOUNDS;
                    else
                        start_mac(MAC_AREA, 3'd1);
                end

                ST_MAC_ISSUE: begin
                    mac_a_q <= mac_operand_a;
                    mac_b_q <= mac_operand_b;
                    mac_shift_q <= mac_operand_shift;
                    mac_negate_q <= mac_operand_negate;
                    mac_clear_q <= mac_index_q == 3'd0;
                    mac_valid_q <= 1'b1;
                    mac_index_q <= mac_index_q + 3'd1;
                    if (mac_index_q == mac_last_q)
                        state <= ST_MAC_WAIT0;
                end

                ST_MAC_WAIT0: state <= ST_MAC_WAIT1;
                ST_MAC_WAIT1: state <= ST_MAC_DONE;

                ST_MAC_DONE: begin
                    case (mac_kind_q)
                        MAC_AREA: state <= ST_AREA;
                        MAC_EDGE: state <= ST_EDGE_STORE;
                        default: state <= ST_DIVIDE_SELECT;
                    endcase
                end

                ST_AREA: begin
                    area_q <= accumulator_q[EW-1:0];
                    if (accumulator_q == {AW{1'b0}}) begin
                        state <= ST_TRIANGLE_NEXT;
                    end else if (accumulator_q[AW-1]) begin
                        state <= ST_SWAP;
                    end else begin
                        state <= ST_BOUNDS;
                    end
                end

                // Winding is irrelevant: make the interior positive.
                ST_SWAP: begin
                    vertex_x_q[1] <= vertex_x_q[2];
                    vertex_x_q[2] <= vertex_x_q[1];
                    vertex_y_q[1] <= vertex_y_q[2];
                    vertex_y_q[2] <= vertex_y_q[1];
                    vertex_u_q[1] <= vertex_u_q[2];
                    vertex_u_q[2] <= vertex_u_q[1];
                    vertex_v_q[1] <= vertex_v_q[2];
                    vertex_v_q[2] <= vertex_v_q[1];
                    vertex_color_q[1] <= vertex_color_q[2];
                    vertex_color_q[2] <= vertex_color_q[1];
                    area_q <= -area_q;
                    swapped_q <= 1'b1;
                    state <= ST_DELTAS;
                end

                ST_BOUNDS: begin
                    box_left_q <= min_x_center[24:8];
                    box_right_q <= max_x_center[24:8];
                    box_top_q <= min_y_center[24:8];
                    box_bottom_q <= max_y_center[24:8];
                    state <= ST_BOUNDS_CLIP;
                end

                ST_BOUNDS_CLIP: begin
                    if (box_left_q < clip_left_q)
                        box_left_q <= clip_left_q;
                    if (box_top_q < clip_top_q)
                        box_top_q <= clip_top_q;
                    if (box_right_q > clip_right_q)
                        box_right_q <= clip_right_q;
                    if (box_bottom_q > clip_bottom_q)
                        box_bottom_q <= clip_bottom_q;
                    state <= ST_BOUNDS_DECIDE;
                end

                ST_BOUNDS_DECIDE: begin
                    left_q <= box_left_q[11:0];
                    right_q <= box_right_q[11:0];
                    top_q <= box_top_q[11:0];
                    bottom_q <= box_bottom_q[11:0];
                    row_operand_q <= {1'b0, box_top_q[11:0]};
                    row_pitch_q <= destination_pitch_q;
                    if (box_left_q > box_right_q || box_top_q > box_bottom_q)
                        state <= ST_TRIANGLE_NEXT;
                    else if (blend_q != BLEND_NONE && writes_pending_q)
                        state <= ST_BARRIER;
                    else begin
                        edge_index_q <= 2'd0;
                        state <= ST_EDGE_PREPARE;
                    end
                end

                // Reads of this triangle's destination must observe every
                // earlier triangle's writes.
                ST_BARRIER: if (writer_barrier_ready_q) begin
                    writer_barrier <= 1'b1;
                    state <= ST_BARRIER_WAIT;
                end

                ST_BARRIER_WAIT: if (writer_barrier_done) begin
                    writes_pending_q <= 1'b0;
                    edge_index_q <= 2'd0;
                    state <= ST_EDGE_PREPARE;
                end

                ST_EDGE_PREPARE: begin
                    edge_rel_x_q <= $signed({4'd0, origin_x}) -
                        $signed(vertex_x_q[edge_a]);
                    edge_rel_y_q <= $signed({4'd0, origin_y}) -
                        $signed(vertex_y_q[edge_a]);
                    start_mac(MAC_EDGE, 3'd1);
                end

                ST_EDGE_STORE: begin
                    edge_current_q[edge_index_q] <= accumulator_q[EW-1:0] -
                        {{(EW-1){1'b0}}, !edge_top_left};
                    edge_row_q[edge_index_q] <= accumulator_q[EW-1:0] -
                        {{(EW-1){1'b0}}, !edge_top_left};
                    edge_step_x_q[edge_index_q] <=
                        -$signed({edge_dy_q[edge_index_q], 8'd0});
                    edge_step_y_q[edge_index_q] <=
                        $signed({edge_dx_q[edge_index_q], 8'd0});
                    if (edge_index_q == 2'd1)
                        weight1_q <= accumulator_q[EW-1:0];
                    if (edge_index_q == 2'd2) begin
                        weight2_q <= accumulator_q[EW-1:0];
                        attribute_q <= 3'd0;
                        attribute_phase_q <= 2'd0;
                        state <= ST_ATTRIBUTE_PREPARE;
                    end else begin
                        edge_index_q <= edge_index_q + 2'd1;
                        state <= ST_EDGE_PREPARE;
                    end
                end

                ST_ATTRIBUTE_PREPARE: begin
                    attribute_a0_q <= attribute_vertex0;
                    attribute_d1_q <= attribute_signed ?
                        $signed({attribute_vertex1[31], attribute_vertex1}) -
                        $signed({attribute_vertex0[31], attribute_vertex0}) :
                        $signed({1'b0, attribute_vertex1}) -
                        $signed({1'b0, attribute_vertex0});
                    attribute_d2_q <= attribute_signed ?
                        $signed({attribute_vertex2[31], attribute_vertex2}) -
                        $signed({attribute_vertex0[31], attribute_vertex0}) :
                        $signed({1'b0, attribute_vertex2}) -
                        $signed({1'b0, attribute_vertex0});
                    attribute_phase_q <= 2'd0;
                    state <= ST_ATTRIBUTE_DECIDE;
                end

                // A constant attribute is exact everywhere: Q = a0, R = A,
                // zero steps. Otherwise three divisions set up the plane.
                ST_ATTRIBUTE_DECIDE: begin
                    if (attribute_d1_q == 33'sd0 && attribute_d2_q == 33'sd0) begin
                        attribute_constant_q <= 1'b1;
                        divide_quotient_q <= 32'd0;
                        divide_remainder_q <= area_q[RW-1:0];
                        state <= ST_ATTRIBUTE_STORE;
                    end else begin
                        attribute_constant_q <= 1'b0;
                        start_mac(attribute_phase_q == 2'd0 ? MAC_ORIGIN :
                                  attribute_phase_q == 2'd1 ? MAC_STEP_X :
                                                              MAC_STEP_Y,
                                  attribute_phase_q == 2'd0 ? 3'd7 : 3'd3);
                    end
                end

                // Register the dividend before taking its magnitude so no
                // two wide adders are chained in one cycle.
                ST_DIVIDE_SELECT: begin
                    divide_dividend_q <= selected_dividend;
                    state <= ST_DIVIDE_PREPARE;
                end

                ST_DIVIDE_PREPARE: begin
                    divide_negative_q <= divide_dividend_q[AW-1];
                    divide_magnitude_q <= divide_dividend_q[AW-1] ?
                        -divide_dividend_q : divide_dividend_q;
                    divide_remainder_q <= {RW{1'b0}};
                    divide_quotient_q <= 32'd0;
                    divide_count_q <= AW[6:0];
                    state <= ST_DIVIDE;
                end

                ST_DIVIDE: begin
                    divide_magnitude_q <= divide_magnitude_q << 1;
                    if (!divide_difference[RW+1]) begin
                        divide_remainder_q <= divide_difference[RW-1:0];
                        divide_quotient_q <= {divide_quotient_q[30:0], 1'b1};
                    end else begin
                        divide_remainder_q <= divide_shifted[RW-1:0];
                        divide_quotient_q <= {divide_quotient_q[30:0], 1'b0};
                    end
                    divide_count_q <= divide_count_q - 7'd1;
                    if (divide_count_q == 7'd1)
                        state <= ST_DIVIDE_FIX;
                end

                // Floor division for negative dividends.
                ST_DIVIDE_FIX: begin
                    if (divide_negative_q) begin
                        if (divide_remainder_q != {RW{1'b0}}) begin
                            divide_quotient_q <= ~divide_quotient_q;
                            divide_remainder_q <= divisor - divide_remainder_q;
                        end else begin
                            divide_quotient_q <= -divide_quotient_q;
                        end
                    end
                    state <= ST_ATTRIBUTE_STORE;
                end

                ST_ATTRIBUTE_STORE: begin
                    if (attribute_constant_q) begin
                        // Store origin now; the zero steps follow.
                        attribute_constant_q <= attribute_phase_q != 2'd2;
                        attribute_phase_q <= attribute_phase_q + 2'd1;
                        divide_quotient_q <= 32'd0;
                        divide_remainder_q <= {RW{1'b0}};
                        if (attribute_phase_q == 2'd2)
                            state <= attribute_q == 3'd5 ? ST_RASTER_START :
                                ST_ATTRIBUTE_PREPARE;
                        if (attribute_phase_q == 2'd2)
                            attribute_q <= attribute_q + 3'd1;
                    end else if (attribute_phase_q == 2'd2) begin
                        attribute_q <= attribute_q + 3'd1;
                        state <= attribute_q == 3'd5 ? ST_RASTER_START :
                            ST_ATTRIBUTE_PREPARE;
                    end else begin
                        attribute_phase_q <= attribute_phase_q + 2'd1;
                        state <= ST_ATTRIBUTE_DECIDE;
                    end
                end

                ST_RASTER_START: begin
                    pixel_x_q <= left_q;
                    pixel_y_q <= top_q;
                    last_x_q <= left_q == right_q;
                    last_y_q <= top_q == bottom_q;
                    destination_row_q <= destination_base_q +
                        row_product_q[31:0];
                    destination_address_q <= destination_base_q +
                        row_product_q[31:0] +
                        ({20'd0, left_q} << (destination_bpp_q == 3'd2 ? 1 : 2));
                    state <= ST_RASTER;
                end

                // ---------------- rasterization ----------------
                ST_RASTER: begin
                    if (covered) begin
                        pixel_u_q <= attribute_quotient[0];
                        pixel_v_q <= attribute_quotient[1];
                        pixel_color_q <= {attribute_quotient[2][7:0],
                                          attribute_quotient[3][7:0],
                                          attribute_quotient[4][7:0],
                                          attribute_quotient[5][7:0]};
                        state <= ST_PIXEL;
                    end else begin
                        state <= ST_ADVANCE;
                    end
                end

                ST_ADVANCE: begin
                    if (last_x_q && last_y_q) begin
                        state <= ST_TRIANGLE_NEXT;
                    end else begin
                        step_row_q <= last_x_q;
                        step_add_q <= 1'b1;
                        for (i = 0; i < 3; i = i + 1)
                            edge_next_q[i] <= last_x_q ?
                                edge_row_q[i] + edge_step_y_q[i] :
                                edge_current_q[i] + edge_step_x_q[i];
                        if (last_x_q) begin
                            pixel_x_q <= left_q;
                            pixel_y_q <= pixel_y_q + 12'd1;
                            last_x_q <= left_q == right_q;
                            last_y_q <= pixel_y_q + 12'd1 == bottom_q;
                            destination_row_q <= destination_row_q +
                                destination_pitch_q;
                            destination_address_q <= destination_row_q +
                                destination_pitch_q +
                                ({20'd0, left_q} <<
                                 (destination_bpp_q == 3'd2 ? 1 : 2));
                        end else begin
                            pixel_x_q <= pixel_x_q + 12'd1;
                            last_x_q <= pixel_x_q + 12'd1 == right_q;
                            destination_address_q <= destination_address_q +
                                {29'd0, destination_bpp_q};
                        end
                        state <= ST_STEP_ADD;
                    end
                end

                ST_STEP_ADD: begin
                    step_commit_q <= 1'b1;
                    for (i = 0; i < 3; i = i + 1) begin
                        edge_current_q[i] <= edge_next_q[i];
                        if (step_row_q)
                            edge_row_q[i] <= edge_next_q[i];
                    end
                    state <= ST_STEP_COMMIT;
                end

                ST_STEP_COMMIT: state <= ST_RASTER;

                ST_TRIANGLE_NEXT: begin
                    swapped_q <= 1'b0;
                    if (triangle_index_q == triangle_last_q) begin
                        state <= ST_FLUSH;
                    end else begin
                        triangle_index_q <= triangle_index_q + 13'd1;
                        vertex_index_q <= vertex_index_q + 14'd1;
                        corner_q <= 2'd0;
                        vertex_address_q <= vertex_address_q + 32'd32;
                        m_axi_araddr <= vertex_address_q + 32'd32;
                        m_axi_arlen <= 8'd3;
                        m_axi_arvalid <= 1'b1;
                        state <= ST_VERTEX_AR;
                    end
                end

                // ---------------- pixel pipeline ----------------
                ST_PIXEL: begin
                    if (textured_q) begin
                        tap_x0_q <= sample_x0;
                        tap_y0_q <= sample_y0;
                        weight_x0_q <= 9'd256 - {1'b0, sample_fx};
                        weight_x1_q <= {1'b0, sample_fx};
                        weight_y0_q <= 9'd256 - {1'b0, sample_fy};
                        weight_y1_q <= {1'b0, sample_fy};
                        state <= ST_TAP_SETUP;
                    end else begin
                        source_q <= pixel_color_q;
                        state <= ST_DESTINATION_FETCH;
                    end
                end

                ST_TAP_SETUP: begin
                    tap_cx0_q <= clamp_coordinate(tap_x0_q, source_width_q);
                    tap_cx1_q <= clamp_coordinate(tap_x0_q + 17'sd1,
                                                  source_width_q);
                    tap_cy0_q <= clamp_coordinate(tap_y0_q, source_height_q);
                    tap_cy1_q <= clamp_coordinate(tap_y0_q + 17'sd1,
                                                  source_height_q);
                    for (i = 0; i < 4; i = i + 1)
                        filter_q[i] <= 25'd0;
                    tap_q <= 2'd0;
                    state <= ST_TAP;
                end

                // Taps with zero weight contribute nothing and are skipped.
                ST_TAP: begin
                    tap_weight_q <= tap_weight;
                    tap_column_q <= tap_q[0] ? tap_cx1_q : tap_cx0_q;
                    tap_row_q <= tap_q[1] ? tap_cy1_q : tap_cy0_q;
                    row_operand_q <= {1'b0, tap_q[1] ? tap_cy1_q : tap_cy0_q};
                    row_pitch_q <= source_pitch_q;
                    state <= tap_wx == 9'd0 || tap_wy == 9'd0 ?
                        ST_TAP_NEXT : ST_TAP_ROW0;
                end

                ST_TAP_ROW0: state <= ST_TAP_ROW1;
                ST_TAP_ROW1: state <= ST_TAP_ADDRESS;

                ST_TAP_ADDRESS: begin
                    texel_address_q <= source_base_q + row_product_q[31:0] +
                        ({20'd0, tap_column_q} << source_shift_q);
                    state <= ST_TAP_FETCH;
                end

                ST_TAP_FETCH: begin
                    if (texel_hit)
                        state <= ST_TAP_DECODE;
                    else
                        start_read(texel_address_q, READ_TEXEL, ST_TAP_DECODE);
                end

                ST_TAP_DECODE: begin
                    texel_q <= texel_argb;
                    palette_address_q <= palette_base_q +
                        ({24'd0, texel_byte} << 2);
                    state <= source_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
                        ST_PALETTE_FETCH : ST_TAP_MULTIPLY;
                end

                ST_PALETTE_FETCH: begin
                    if (palette_hit)
                        state <= ST_PALETTE_DECODE;
                    else
                        start_read(palette_address_q, READ_PALETTE,
                                   ST_PALETTE_DECODE);
                end

                ST_PALETTE_DECODE: begin
                    texel_q <= palette_argb;
                    state <= ST_TAP_MULTIPLY;
                end

                ST_TAP_MULTIPLY: begin
                    for (i = 0; i < 4; i = i + 1) begin
                        bank_a_q[i] <= argb_channel(texel_q, i[1:0]);
                        bank_b_q[i] <= tap_weight_q;
                    end
                    state <= ST_TAP_PRODUCT;
                end

                ST_TAP_PRODUCT: state <= ST_TAP_ACCUMULATE;

                ST_TAP_ACCUMULATE: begin
                    for (i = 0; i < 4; i = i + 1)
                        filter_q[i] <= filter_q[i] + bank_product_q[i];
                    state <= ST_TAP_NEXT;
                end

                ST_TAP_NEXT: begin
                    tap_q <= tap_q + 2'd1;
                    state <= tap_q == 2'd3 ? ST_FILTER_ROUND : ST_TAP;
                end

                ST_FILTER_ROUND: begin
                    for (i = 0; i < 4; i = i + 1)
                        filter_q[i] <= filter_q[i] + 25'd32768;
                    state <= ST_MODULATE;
                end

                // src = tex x color, per channel, exact /255.
                ST_MODULATE: begin
                    for (i = 0; i < 4; i = i + 1) begin
                        bank_a_q[i] <= filter_q[i][23:16];
                        bank_b_q[i] <= {9'd0, argb_channel(pixel_color_q,
                                                           i[1:0])};
                    end
                    state <= ST_MODULATE_PRODUCT;
                end

                ST_MODULATE_PRODUCT: state <= ST_MODULATE_WAIT;
                ST_MODULATE_WAIT: state <= ST_MODULATE_DIVIDE;

                ST_MODULATE_DIVIDE: begin
                    source_q <= {bank_quotient_q[0], bank_quotient_q[1],
                                 bank_quotient_q[2], bank_quotient_q[3]};
                    state <= ST_DESTINATION_FETCH;
                end

                ST_DESTINATION_FETCH: begin
                    if (blend_q == BLEND_NONE)
                        state <= ST_BLEND;
                    else if (destination_hit)
                        state <= ST_DESTINATION_DECODE;
                    else
                        start_read(destination_address_q, READ_DESTINATION,
                                   ST_DESTINATION_DECODE);
                end

                ST_DESTINATION_DECODE: begin
                    destination_q <= destination_argb;
                    state <= ST_BLEND;
                end

                ST_BLEND: begin
                    for (i = 0; i < 4; i = i + 1) begin
                        blend_add_q[i] <= 8'd0;
                        bank_b_q[4 + i] <= 17'd0;
                        bank_a_q[4 + i] <= argb_channel(destination_q,
                                                        i[1:0]);
                        if (i == 0) begin
                            bank_a_q[0] <= blend_q == BLEND_NONE ||
                                blend_q == BLEND_BLEND ? source_alpha :
                                destination_q[31:24];
                            bank_b_q[0] <= 17'd255;
                            if (blend_q == BLEND_BLEND)
                                bank_b_q[4] <= {9'd0, 8'd255 - source_alpha};
                        end else begin
                            bank_a_q[i] <= argb_channel(source_q, i[1:0]);
                            case (blend_q)
                                BLEND_BLEND, BLEND_ADD:
                                    bank_b_q[i] <= {9'd0, source_alpha};
                                BLEND_MOD, BLEND_MUL:
                                    bank_b_q[i] <= {9'd0, argb_channel(
                                        destination_q, i[1:0])};
                                default: bank_b_q[i] <= 17'd255;
                            endcase
                            if (blend_q == BLEND_BLEND || blend_q == BLEND_MUL)
                                bank_b_q[4 + i] <=
                                    {9'd0, 8'd255 - source_alpha};
                            if (blend_q == BLEND_ADD)
                                blend_add_q[i] <= argb_channel(destination_q,
                                                               i[1:0]);
                        end
                    end
                    state <= ST_BLEND_PRODUCT;
                end

                ST_BLEND_PRODUCT: state <= ST_BLEND_WAIT;
                ST_BLEND_WAIT: state <= ST_BLEND_DIVIDE;

                ST_BLEND_DIVIDE: begin
                    for (i = 0; i < 4; i = i + 1)
                        blend_sum_q[i] <= {2'd0, bank_quotient_q[i]} +
                            {2'd0, bank_quotient_q[4 + i]} +
                            {2'd0, blend_add_q[i]};
                    state <= ST_BLEND_SUM;
                end

                ST_BLEND_SUM: begin
                    for (i = 0; i < 4; i = i + 1)
                        result_q[31 - 8 * i -: 8] <=
                            blend_sum_q[i] > 10'd255 ? 8'hff :
                            blend_sum_q[i][7:0];
                    state <= ST_PACK;
                end

                ST_PACK: begin
                    // RGB565 rounds each channel: (c * 31 + 127) / 255.
                    bank_a_q[1] <= result_q[23:16];
                    bank_b_q[1] <= 17'd31;
                    bank_a_q[2] <= result_q[15:8];
                    bank_b_q[2] <= 17'd63;
                    bank_a_q[3] <= result_q[7:0];
                    bank_b_q[3] <= 17'd31;
                    if (destination_format_q == `ASTRA_RENDER_FORMAT_RGB565)
                        state <= ST_PACK_WAIT0;
                    else begin
                        output_q <= destination_format_q ==
                            `ASTRA_RENDER_FORMAT_XRGB8888 ?
                                {8'hff, result_q[23:0]} : result_q;
                        state <= ST_EMIT;
                    end
                end

                ST_PACK_WAIT0: state <= ST_PACK_WAIT1;
                ST_PACK_WAIT1: state <= ST_PACK_ROUND;

                ST_PACK_ROUND: begin
                    output_q <= {16'd0, bank_quotient_q[1][4:0],
                                 bank_quotient_q[2][5:0],
                                 bank_quotient_q[3][4:0]};
                    state <= ST_EMIT;
                end

                ST_EMIT: begin
                    if (!pixel_valid) begin
                        pixel_address <= destination_address_q;
                        pixel_format <= destination_format_q;
                        pixel_value <= output_q;
                        pixel_valid <= 1'b1;
                    end else if (pixel_ready) begin
                        pixel_valid <= 1'b0;
                        completed_pixels <= completed_pixels + 32'd1;
                        writes_pending_q <= 1'b1;
                        if (destination_hit)
                            destination_cache_data_q <= emitted_beat;
                        state <= ST_ADVANCE;
                    end
                end

                // ---------------- single-beat reads ----------------
                ST_READ_AR: if (m_axi_arvalid && m_axi_arready) begin
                    m_axi_arvalid <= 1'b0;
                    m_axi_rready <= 1'b1;
                    state <= ST_READ_R;
                end

                ST_READ_R: if (m_axi_rvalid && m_axi_rready) begin
                    m_axi_rready <= 1'b0;
                    if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        !m_axi_rlast) begin
                        fail(`ASTRA_RENDER_STATUS_AXI_READ,
                             {30'h00032000, read_kind_q});
                    end else begin
                        case (read_kind_q)
                            READ_TEXEL: begin
                                texel_cache_valid_q[texel_entry] <= 1'b1;
                                texel_cache_tag_q[texel_entry] <=
                                    m_axi_araddr[31:3];
                                texel_cache_data_q[texel_entry] <= m_axi_rdata;
                            end
                            READ_PALETTE: begin
                                palette_cache_valid_q <= 1'b1;
                                palette_cache_tag_q <= m_axi_araddr[31:3];
                                palette_cache_data_q <= m_axi_rdata;
                            end
                            default: begin
                                destination_cache_valid_q <= 1'b1;
                                destination_cache_tag_q <= m_axi_araddr[31:3];
                                destination_cache_data_q <= m_axi_rdata;
                            end
                        endcase
                        state <= read_return_q;
                    end
                end

                // ---------------- completion ----------------
                ST_FLUSH: begin
                    writer_flush <= 1'b1;
                    if (writer_flush && writer_flush_ready) begin
                        writer_flush <= 1'b0;
                        state <= ST_WAIT_WRITER;
                    end
                end

                ST_WAIT_WRITER: if (writer_done) begin
                    if (writer_error) begin
                        status <= `ASTRA_RENDER_STATUS_AXI_WRITE;
                        fault_detail <= writer_fault_detail;
                    end
                    state <= ST_FINISH;
                end

                ST_ABORT: if (!writer_started_q || writer_aborted) begin
                    status <= `ASTRA_RENDER_STATUS_RESET;
                    abort_pending_q <= 1'b0;
                    state <= ST_FINISH;
                end

                ST_FAIL: begin
                    if (writer_started_q) begin
                        writer_abort <= 1'b1;
                        state <= ST_FAIL_WAIT;
                    end else begin
                        state <= ST_FINISH;
                    end
                end

                ST_FAIL_WAIT: if (writer_aborted)
                    state <= ST_FINISH;

                ST_FINISH: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    writer_started_q <= 1'b0;
                    state <= ST_IDLE;
                end

                default: fail(`ASTRA_RENDER_STATUS_UNSUPPORTED, 32'h000cffff);
            endcase
        end
    end
endmodule

`default_nettype wire
