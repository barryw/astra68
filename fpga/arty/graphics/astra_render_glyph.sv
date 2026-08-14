// Copyright (c) 2026 Astra68 contributors
//
// Bounded AFNT positioned-glyph renderer. Every descriptor is checked in a
// read-only prepass before the shared writer is started. The execution pass
// then expands MASK1, A4, A8, INDEX4, and INDEX8 sources without CPU pixel
// loops.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_glyph #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = 6'd5
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
    input  wire [15:0]                  command_flags,
    input  wire [31:0]                  foreground,
    input  wire [31:0]                  background,
    input  wire [7:0]                   transparent_index,
    input  wire [31:0]                  descriptor_offset,
    input  wire [12:0]                  descriptor_count,
    input  wire [31:0]                  destination_data_offset,
    input  wire [31:0]                  destination_pitch,
    input  wire [15:0]                  destination_width,
    input  wire [15:0]                  destination_height,
    input  wire [7:0]                   destination_format,
    input  wire [2:0]                   destination_bytes_per_pixel,
    input  wire [31:0]                  source_data_offset,
    input  wire [31:0]                  source_data_bytes,
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
    output wire [7:0]                   m_axi_arlen,
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
    localparam [5:0] ST_IDLE = 6'd0;
    localparam [5:0] ST_DESC_AR = 6'd1;
    localparam [5:0] ST_DESC_R0 = 6'd2;
    localparam [5:0] ST_DESC_R1 = 6'd3;
    localparam [5:0] ST_DESC_CHECK = 6'd4;
    localparam [5:0] ST_PREPASS_NEXT = 6'd5;
    localparam [5:0] ST_WRITER_START = 6'd6;
    localparam [5:0] ST_EXEC_SETUP = 6'd7;
    localparam [5:0] ST_PIXEL_CLASSIFY = 6'd8;
    localparam [5:0] ST_SOURCE_AR = 6'd9;
    localparam [5:0] ST_SOURCE_R = 6'd10;
    localparam [5:0] ST_SOURCE_DECODE = 6'd11;
    localparam [5:0] ST_PALETTE_AR = 6'd12;
    localparam [5:0] ST_PALETTE_R = 6'd13;
    localparam [5:0] ST_PALETTE_DECODE = 6'd14;
    localparam [5:0] ST_DEST_AR = 6'd15;
    localparam [5:0] ST_DEST_R = 6'd16;
    localparam [5:0] ST_BLEND = 6'd17;
    localparam [5:0] ST_EMIT = 6'd18;
    localparam [5:0] ST_PIXEL_NEXT = 6'd19;
    localparam [5:0] ST_DESCRIPTOR_NEXT = 6'd20;
    localparam [5:0] ST_FLUSH = 6'd21;
    localparam [5:0] ST_WAIT_WRITER = 6'd22;
    localparam [5:0] ST_ABORT = 6'd23;
    localparam [5:0] ST_FAIL = 6'd24;
    localparam [5:0] ST_FAIL_WAIT = 6'd25;
    localparam [5:0] ST_BLEND_MULTIPLY = 6'd26;
    localparam [5:0] ST_BLEND_NORMALIZE = 6'd27;
    localparam [5:0] ST_BLEND_PACK = 6'd28;
    localparam [5:0] ST_DESC_RANGE_ROW = 6'd29;
    localparam [5:0] ST_DESC_RANGE_SUM = 6'd30;
    localparam [5:0] ST_DESC_RANGE_FINAL = 6'd31;
    localparam [5:0] ST_PIXEL_ADDRESS_SUM = 6'd32;
    localparam [5:0] ST_SOURCE_REQUEST = 6'd33;
    localparam [5:0] ST_BLEND_ACCUMULATE = 6'd34;
    localparam [5:0] ST_BLEND_DIVIDE = 6'd35;
    localparam [5:0] ST_PIXEL_ROW_COMBINE = 6'd36;
    localparam [5:0] ST_DESC_RANGE_MULTIPLY = 6'd38;
    localparam [5:0] ST_PIXEL_MULTIPLY = 6'd39;
    localparam [5:0] ST_PALETTE_REQUEST = 6'd40;
    localparam [5:0] ST_PIXEL_ADDRESS_BASE = 6'd41;
    localparam [5:0] ST_SOURCE_APPLY = 6'd42;
    localparam [5:0] ST_PIXEL_BOUNDS = 6'd43;
    localparam [5:0] ST_DESC_PREPARE = 6'd44;
    localparam [5:0] ST_DESC_RANGE_RESULT = 6'd45;
    localparam [5:0] ST_DESC_VALIDATE = 6'd46;
    localparam [5:0] ST_DESC_VALIDATE_RESULT = 6'd47;
    localparam [5:0] ST_BLEND_DIV_MULTIPLY = 6'd48;
    localparam [5:0] ST_PIXEL_BOUNDS_RESULT = 6'd49;
    localparam [5:0] ST_PIXEL_NEXT_DECIDE = 6'd50;
    localparam [5:0] ST_PALETTE_DECIDE = 6'd51;
    localparam [5:0] ST_BLEND_DIV_PIPE = 6'd52;
    localparam [5:0] ST_BLEND_PRODUCT_PIPE = 6'd53;
    localparam [5:0] ST_BLEND_ACCUMULATE_FINISH = 6'd54;

    localparam [1:0] READ_DESCRIPTOR = 2'd0;
    localparam [1:0] READ_SOURCE = 2'd1;
    localparam [1:0] READ_PALETTE = 2'd2;
    localparam [1:0] READ_DESTINATION = 2'd3;
    localparam [1:0] SOURCE_APPLY_MASK = 2'd0;
    localparam [1:0] SOURCE_APPLY_A4 = 2'd1;
    localparam [1:0] SOURCE_APPLY_A8 = 2'd2;
    localparam [1:0] SOURCE_APPLY_INDEX = 2'd3;

    // Keep ordinary next-state decode on D. Extracting it onto replicated
    // synchronous reset pins imposes the FDRE reset-pin setup penalty.
    (* extract_reset = "no" *) reg [5:0] state;
    reg prepass_q;
    reg [12:0] descriptor_index_q;
    reg [12:0] descriptor_last_index_q;
    reg descriptor_is_last_q;
    reg [1:0] read_kind_q;
    reg [63:0] descriptor_beat0_q;
    reg [63:0] descriptor_beat1_q;
    reg descriptor_first_error_q;
    reg [63:0] cache_q;
    reg [31:0] source_offset_q;
    reg [15:0] source_x_q, source_y_q;
    reg signed [15:0] destination_x_q, destination_y_q;
    reg signed [16:0] destination_pixel_x_q, destination_pixel_y_q;
    (* extract_enable = "no" *)
    reg signed [16:0] effective_clip_left_q, effective_clip_top_q;
    (* extract_enable = "no" *)
    reg signed [16:0] effective_clip_right_q, effective_clip_bottom_q;
reg [15:0] glyph_width_q, glyph_height_q;
reg [15:0] glyph_last_x_q, glyph_last_y_q;
    (* extract_enable = "no" *) reg [15:0] pixel_x_q, pixel_y_q;
    reg [31:0] source_address_q, destination_address_q, palette_address_q;
    reg [7:0] source_sample_q;
    reg [1:0] source_apply_kind_q;
    reg source_sample_zero_q, source_sample_full_q;
    reg source_sample_transparent_q;
    reg [31:0] source_color_q;
    reg [31:0] destination_color_q;
    (* extract_enable = "no" *) reg [31:0] output_pixel_q;
    reg source_visible_q;
    reg palette_transparent_q, palette_opaque_q;
    reg needs_destination_q;
    reg [7:0] coverage_q;
    reg [7:0] coverage_max_q;
    reg [7:0] blend_source_r_q, blend_source_g_q, blend_source_b_q;
    reg [7:0] blend_destination_r_q, blend_destination_g_q;
    reg [7:0] blend_destination_b_q;
    reg [7:0] blend_alpha_q, blend_inverse_alpha_q;
    reg [7:0] blend_multiply_source_r_q, blend_multiply_source_g_q;
    reg [7:0] blend_multiply_source_b_q;
    reg [7:0] blend_multiply_destination_r_q;
    reg [7:0] blend_multiply_destination_g_q;
    reg [7:0] blend_multiply_destination_b_q;
    reg [7:0] blend_multiply_alpha_q, blend_multiply_inverse_alpha_q;
    reg [15:0] blend_source_stage_r_q, blend_source_stage_g_q;
    reg [15:0] blend_source_stage_b_q;
    reg [15:0] blend_destination_stage_r_q, blend_destination_stage_g_q;
    reg [15:0] blend_destination_stage_b_q;
    (* use_dsp = "no" *) reg [16:0] blend_numerator_r_q;
    (* use_dsp = "no" *) reg [16:0] blend_numerator_g_q;
    (* use_dsp = "no" *) reg [16:0] blend_numerator_b_q;
    reg [16:0] blend_div_operand_r_q, blend_div_operand_g_q;
    reg [16:0] blend_div_operand_b_q;
    reg [16:0] blend_div_multiply_operand_r_q;
    reg [16:0] blend_div_multiply_operand_g_q;
    reg [16:0] blend_div_multiply_operand_b_q;
    (* use_dsp = "yes" *) reg [15:0] blend_source_product_r_q;
    (* use_dsp = "yes" *) reg [15:0] blend_source_product_g_q;
    (* use_dsp = "yes" *) reg [15:0] blend_source_product_b_q;
    (* use_dsp = "yes" *) reg [15:0] blend_destination_product_r_q;
    (* use_dsp = "yes" *) reg [15:0] blend_destination_product_g_q;
    (* use_dsp = "yes" *) reg [15:0] blend_destination_product_b_q;
    reg [25:0] blend_div_product_r_q, blend_div_product_g_q;
    reg [25:0] blend_div_product_b_q;
    reg [7:0] blend_result_r_q, blend_result_g_q, blend_result_b_q;
    reg blend_component_rgb565_q;
    reg [16:0] descriptor_source_end_x_q;
    reg [16:0] descriptor_source_end_y_q;
    reg [47:0] descriptor_last_row_q;
    reg [32:0] descriptor_last_bit_q;
    reg [48:0] descriptor_source_end_byte_q;
    reg descriptor_range_invalid_q;
    reg descriptor_bounds_invalid_q;
    reg destination_inside_q;
    reg pixel_last_x_q, pixel_last_y_q;
    (* extract_enable = "no" *)
    reg [47:0] descriptor_range_accumulator_q;
reg [47:0] descriptor_range_multiplicand_q;
reg [15:0] descriptor_range_multiplier_q;
    reg [3:0] descriptor_range_step_q;
    reg [31:0] source_base_q, destination_base_q;
    reg [31:0] command_descriptor_offset_q;
    reg [31:0] command_foreground_q, command_background_q;
    reg [7:0] command_transparent_index_q;
    reg [7:0] command_source_format_q, command_destination_format_q;
    reg command_destination_index8_q, command_destination_rgb565_q;
reg [15:0] source_row_operand_q, destination_row_operand_q;
    reg [47:0] source_row_product_q, destination_row_product_q;
    reg [31:0] source_row_column_q, destination_row_column_q;
    reg [31:0] source_row_product_low_q, source_row_product_high_q;
    reg [31:0] destination_row_product_low_q;
    reg [31:0] destination_row_product_high_q;
    // These are intentional timing boundaries. If Vivado merges them back
    // into the conditionally written row operands, it recreates FSM-driven
    // DSP clock enables on the row multipliers.
    (* dont_touch = "yes" *) reg [15:0] source_row_multiply_operand_q;
    (* dont_touch = "yes" *) reg [15:0] destination_row_multiply_operand_q;
    reg [15:0] source_pitch_low_operand_q, source_pitch_high_operand_q;
    reg [15:0] destination_pitch_low_operand_q;
    reg [15:0] destination_pitch_high_operand_q;
reg [31:0] source_column_byte_q, destination_column_byte_q;
    reg abort_pending_q;
    reg writer_started_q;

    (* use_dsp = "no" *) wire [25:0] blend_div_product_r_next =
        ({9'd0, blend_div_multiply_operand_r_q} << 8) +
        ({9'd0, blend_div_multiply_operand_r_q} << 4) +
        {9'd0, blend_div_multiply_operand_r_q};
    (* use_dsp = "no" *) wire [25:0] blend_div_product_g_next =
        ({9'd0, blend_div_multiply_operand_g_q} << 8) +
        ({9'd0, blend_div_multiply_operand_g_q} << 4) +
        {9'd0, blend_div_multiply_operand_g_q};
    (* use_dsp = "no" *) wire [25:0] blend_div_product_b_next =
        ({9'd0, blend_div_multiply_operand_b_q} << 8) +
        ({9'd0, blend_div_multiply_operand_b_q} << 4) +
        {9'd0, blend_div_multiply_operand_b_q};

    assign m_axi_arid = AXI_ID;
    assign m_axi_arlen = read_kind_q == READ_DESCRIPTOR ? 8'd1 : 8'd0;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b0000;

    function automatic [7:0] beat_byte(
        input [63:0] beat,
        input [2:0] lane
    );
        begin
            beat_byte = beat[lane * 8 +: 8];
        end
    endfunction

    function automatic [31:0] beat_word_be(
        input [63:0] beat,
        input word_high
    );
        reg [2:0] lane;
        begin
            lane = word_high ? 3'd4 : 3'd0;
            beat_word_be = {
                beat_byte(beat, lane),
                beat_byte(beat, lane + 3'd1),
                beat_byte(beat, lane + 3'd2),
                beat_byte(beat, lane + 3'd3)
            };
        end
    endfunction

    function automatic [7:0] divide_255_round(input [16:0] numerator);
        reg [17:0] adjusted;
        begin
            adjusted = {1'b0, numerator} + 18'd128;
            divide_255_round = (adjusted + (adjusted >> 8)) >> 8;
        end
    endfunction

    function automatic [7:0] blend_channel_255(
        input [7:0] source,
        input [7:0] destination,
        input [7:0] alpha
    );
        reg [16:0] numerator;
        begin
            numerator = source * alpha + destination * (8'd255 - alpha);
            blend_channel_255 = divide_255_round(numerator);
        end
    endfunction

    function automatic [7:0] blend_channel_15(
        input [7:0] source,
        input [7:0] destination,
        input [3:0] alpha
    );
        reg [12:0] numerator;
        begin
            numerator = source * alpha + destination * (4'd15 - alpha) + 7;
            blend_channel_15 = numerator / 15;
        end
    endfunction

    function automatic [31:0] decode_destination(
        input [7:0] format,
        input [63:0] beat,
        input [2:0] lane
    );
        reg [15:0] rgb565;
        reg [4:0] red5, blue5;
        reg [5:0] green6;
        begin
            rgb565 = {beat_byte(beat, lane), beat_byte(beat, lane + 3'd1)};
            red5 = rgb565[15:11];
            green6 = rgb565[10:5];
            blue5 = rgb565[4:0];
            case (format)
                `ASTRA_RENDER_FORMAT_RGB565: decode_destination = {
                    8'hff, red5, red5[4:2], green6, green6[5:4],
                    blue5, blue5[4:2]
                };
                default: decode_destination = {
                    8'hff,
                    beat_byte(beat, lane + 3'd1),
                    beat_byte(beat, lane + 3'd2),
                    beat_byte(beat, lane + 3'd3)
                };
            endcase
        end
    endfunction

    function automatic [31:0] pack_destination(
        input [7:0] format,
        input [31:0] argb
    );
        begin
            case (format)
                `ASTRA_RENDER_FORMAT_INDEX8:
                    pack_destination = {24'd0, argb[7:0]};
                `ASTRA_RENDER_FORMAT_RGB565:
                    pack_destination = {16'd0, argb[23:19],
                        argb[15:10], argb[7:3]};
                default: pack_destination = {8'hff, argb[23:0]};
            endcase
        end
    endfunction

    function automatic [31:0] expand_destination_color(
        input [7:0] format,
        input [31:0] pixel
    );
        reg [4:0] red5, blue5;
        reg [5:0] green6;
        begin
            red5 = pixel[15:11];
            green6 = pixel[10:5];
            blue5 = pixel[4:0];
            case (format)
                `ASTRA_RENDER_FORMAT_RGB565:
                    expand_destination_color = {8'hff,
                        red5, red5[4:2], green6, green6[5:4],
                        blue5, blue5[4:2]};
                default: expand_destination_color = {8'hff, pixel[23:0]};
            endcase
        end
    endfunction

    function automatic [31:0] blend_coverage_pixel(
        input [7:0] format,
        input [31:0] foreground_pixel,
        input [63:0] destination_beat,
        input [2:0] lane,
        input [7:0] alpha,
        input [7:0] alpha_max
    );
        reg [15:0] destination565;
        reg [7:0] out_r, out_g, out_b;
        reg [31:0] destination_argb;
        begin
            destination565 = {beat_byte(destination_beat, lane),
                              beat_byte(destination_beat, lane + 3'd1)};
            destination_argb = decode_destination(format,
                                                   destination_beat, lane);
            if (format == `ASTRA_RENDER_FORMAT_RGB565) begin
                if (alpha_max == 8'd15) begin
                    out_r = blend_channel_15({3'd0, foreground_pixel[15:11]},
                        {3'd0, destination565[15:11]}, alpha[3:0]);
                    out_g = blend_channel_15({2'd0, foreground_pixel[10:5]},
                        {2'd0, destination565[10:5]}, alpha[3:0]);
                    out_b = blend_channel_15({3'd0, foreground_pixel[4:0]},
                        {3'd0, destination565[4:0]}, alpha[3:0]);
                end else begin
                    out_r = blend_channel_255({3'd0, foreground_pixel[15:11]},
                        {3'd0, destination565[15:11]}, alpha);
                    out_g = blend_channel_255({2'd0, foreground_pixel[10:5]},
                        {2'd0, destination565[10:5]}, alpha);
                    out_b = blend_channel_255({3'd0, foreground_pixel[4:0]},
                        {3'd0, destination565[4:0]}, alpha);
                end
                blend_coverage_pixel = {16'd0, out_r[4:0], out_g[5:0],
                                        out_b[4:0]};
            end else begin
                if (alpha_max == 8'd15) begin
                    out_r = blend_channel_15(foreground_pixel[23:16],
                        destination_argb[23:16], alpha[3:0]);
                    out_g = blend_channel_15(foreground_pixel[15:8],
                        destination_argb[15:8], alpha[3:0]);
                    out_b = blend_channel_15(foreground_pixel[7:0],
                        destination_argb[7:0], alpha[3:0]);
                end else begin
                    out_r = blend_channel_255(foreground_pixel[23:16],
                        destination_argb[23:16], alpha);
                    out_g = blend_channel_255(foreground_pixel[15:8],
                        destination_argb[15:8], alpha);
                    out_b = blend_channel_255(foreground_pixel[7:0],
                        destination_argb[7:0], alpha);
                end
                blend_coverage_pixel = {8'hff, out_r, out_g, out_b};
            end
        end
    endfunction

    wire [31:0] descriptor_address = arena_base + command_descriptor_offset_q +
        ({19'd0, descriptor_index_q} << 4);
    wire [31:0] desc_source_offset = beat_word_be(descriptor_beat0_q, 1'b0);
    wire [31:0] desc_source_xy = beat_word_be(descriptor_beat0_q, 1'b1);
    wire [31:0] desc_destination_xy = beat_word_be(descriptor_beat1_q, 1'b0);
    wire [31:0] desc_width_height = beat_word_be(descriptor_beat1_q, 1'b1);
    wire [16:0] desc_source_end_x = {1'b0, desc_source_xy[31:16]} +
        {1'b0, desc_width_height[31:16]};
    wire [16:0] desc_source_end_y = {1'b0, desc_source_xy[15:0]} +
        {1'b0, desc_width_height[15:0]};
    wire signed [16:0] destination_pixel_x =
        $signed({destination_x_q[15], destination_x_q}) +
        $signed({1'b0, pixel_x_q});
    wire signed [16:0] destination_pixel_y =
        $signed({destination_y_q[15], destination_y_q}) +
        $signed({1'b0, pixel_y_q});
    wire signed [16:0] effective_clip_left = clip_left < 0 ?
        17'sd0 : $signed({clip_left[15], clip_left});
    wire signed [16:0] effective_clip_top = clip_top < 0 ?
        17'sd0 : $signed({clip_top[15], clip_top});
    wire signed [16:0] effective_clip_right =
        $signed({clip_right[15], clip_right}) <
        $signed({1'b0, destination_width}) ?
            $signed({clip_right[15], clip_right}) :
            $signed({1'b0, destination_width});
    wire signed [16:0] effective_clip_bottom =
        $signed({clip_bottom[15], clip_bottom}) <
        $signed({1'b0, destination_height}) ?
            $signed({clip_bottom[15], clip_bottom}) :
            $signed({1'b0, destination_height});
    wire destination_inside =
        destination_pixel_x_q >= effective_clip_left_q &&
        destination_pixel_x_q < effective_clip_right_q &&
        destination_pixel_y_q >= effective_clip_top_q &&
        destination_pixel_y_q < effective_clip_bottom_q;
    wire [16:0] current_source_x = {1'b0, source_x_q} + {1'b0, pixel_x_q};
    wire [16:0] current_source_y = {1'b0, source_y_q} + {1'b0, pixel_y_q};
    wire [31:0] source_column_byte =
        command_source_format_q == `ASTRA_RENDER_FORMAT_MASK1 ?
            {15'd0, current_source_x} >> 3 :
        command_source_format_q == `ASTRA_RENDER_FORMAT_A4 ||
        command_source_format_q == `ASTRA_RENDER_FORMAT_INDEX4 ?
            {15'd0, current_source_x} >> 1 :
            {15'd0, current_source_x};
    wire [31:0] destination_column_byte =
        destination_pixel_x_q[15:0] * destination_bytes_per_pixel;
    wire [2:0] source_lane = source_address_q[2:0];
    wire [7:0] source_byte = beat_byte(cache_q, source_lane);
    wire source_mask_bit = source_byte[7 - current_source_x[2:0]];
    wire [3:0] source_nibble = current_source_x[0] ?
        source_byte[3:0] : source_byte[7:4];
    wire [7:0] decoded_sample =
        command_source_format_q == `ASTRA_RENDER_FORMAT_MASK1 ?
            {7'd0, source_mask_bit} :
        command_source_format_q == `ASTRA_RENDER_FORMAT_A4 ||
        command_source_format_q == `ASTRA_RENDER_FORMAT_INDEX4 ?
            {4'd0, source_nibble} : source_byte;
    wire [2:0] destination_lane = destination_address_q[2:0];
    wire [31:0] palette_address = arena_base + source_palette_offset +
        ({24'd0, source_sample_q} << 2);
    wire [2:0] palette_lane = palette_address[2:0];
    wire [31:0] decoded_palette = {
        beat_byte(cache_q, palette_lane),
        beat_byte(cache_q, palette_lane + 3'd1),
        beat_byte(cache_q, palette_lane + 3'd2),
        beat_byte(cache_q, palette_lane + 3'd3)
    };

    task automatic fail(input [15:0] failure_status,
                        input [31:0] detail);
        begin
            status <= failure_status;
            fault_detail <= detail;
            m_axi_arvalid <= 1'b0;
            m_axi_rready <= 1'b0;
            state <= ST_FAIL;
        end
    endtask

    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            busy <= 1'b0;
            done <= 1'b0;
            status <= `ASTRA_RENDER_STATUS_OK;
            fault_detail <= 32'd0;
            completed_pixels <= 32'd0;
            writer_start <= 1'b0;
            writer_abort <= 1'b0;
            writer_flush <= 1'b0;
            pixel_valid <= 1'b0;
            pixel_address <= 32'd0;
            pixel_format <= 8'd0;
            pixel_value <= 32'd0;
            m_axi_araddr <= 32'd0;
            m_axi_arvalid <= 1'b0;
            m_axi_rready <= 1'b0;
            prepass_q <= 1'b0;
            descriptor_index_q <= 13'd0;
            descriptor_last_index_q <= 13'd0;
            descriptor_is_last_q <= 1'b0;
            read_kind_q <= READ_DESCRIPTOR;
            descriptor_beat0_q <= 64'd0;
            descriptor_beat1_q <= 64'd0;
            descriptor_first_error_q <= 1'b0;
            cache_q <= 64'd0;
            source_offset_q <= 32'd0;
            source_x_q <= 16'd0;
            source_y_q <= 16'd0;
            destination_x_q <= 16'sd0;
            destination_y_q <= 16'sd0;
            destination_pixel_x_q <= 17'sd0;
            destination_pixel_y_q <= 17'sd0;
            effective_clip_left_q <= 17'sd0;
            effective_clip_top_q <= 17'sd0;
            effective_clip_right_q <= 17'sd0;
            effective_clip_bottom_q <= 17'sd0;
            glyph_width_q <= 16'd0;
            glyph_height_q <= 16'd0;
            glyph_last_x_q <= 16'd0;
            glyph_last_y_q <= 16'd0;
            pixel_x_q <= 16'd0;
            pixel_y_q <= 16'd0;
            source_address_q <= 32'd0;
            destination_address_q <= 32'd0;
            palette_address_q <= 32'd0;
            source_sample_q <= 8'd0;
            source_apply_kind_q <= SOURCE_APPLY_MASK;
            source_color_q <= 32'd0;
            destination_color_q <= 32'd0;
            output_pixel_q <= 32'd0;
            source_visible_q <= 1'b0;
            palette_transparent_q <= 1'b0;
            palette_opaque_q <= 1'b0;
            needs_destination_q <= 1'b0;
            coverage_q <= 8'd0;
            coverage_max_q <= 8'd0;
            blend_source_r_q <= 8'd0;
            blend_source_g_q <= 8'd0;
            blend_source_b_q <= 8'd0;
            blend_destination_r_q <= 8'd0;
            blend_destination_g_q <= 8'd0;
            blend_destination_b_q <= 8'd0;
            blend_alpha_q <= 8'd0;
            blend_inverse_alpha_q <= 8'd0;
            blend_multiply_source_r_q <= 8'd0;
            blend_multiply_source_g_q <= 8'd0;
            blend_multiply_source_b_q <= 8'd0;
            blend_multiply_destination_r_q <= 8'd0;
            blend_multiply_destination_g_q <= 8'd0;
            blend_multiply_destination_b_q <= 8'd0;
            blend_multiply_alpha_q <= 8'd0;
            blend_multiply_inverse_alpha_q <= 8'd0;
            blend_source_stage_r_q <= 16'd0;
            blend_source_stage_g_q <= 16'd0;
            blend_source_stage_b_q <= 16'd0;
            blend_destination_stage_r_q <= 16'd0;
            blend_destination_stage_g_q <= 16'd0;
            blend_destination_stage_b_q <= 16'd0;
            blend_numerator_r_q <= 17'd0;
            blend_numerator_g_q <= 17'd0;
            blend_numerator_b_q <= 17'd0;
            blend_div_operand_r_q <= 17'd0;
            blend_div_operand_g_q <= 17'd0;
            blend_div_operand_b_q <= 17'd0;
            blend_div_multiply_operand_r_q <= 17'd0;
            blend_div_multiply_operand_g_q <= 17'd0;
            blend_div_multiply_operand_b_q <= 17'd0;
            blend_source_product_r_q <= 16'd0;
            blend_source_product_g_q <= 16'd0;
            blend_source_product_b_q <= 16'd0;
            blend_destination_product_r_q <= 16'd0;
            blend_destination_product_g_q <= 16'd0;
            blend_destination_product_b_q <= 16'd0;
            blend_div_product_r_q <= 26'd0;
            blend_div_product_g_q <= 26'd0;
            blend_div_product_b_q <= 26'd0;
            blend_result_r_q <= 8'd0;
            blend_result_g_q <= 8'd0;
            blend_result_b_q <= 8'd0;
            blend_component_rgb565_q <= 1'b0;
            descriptor_source_end_x_q <= 17'd0;
            descriptor_source_end_y_q <= 17'd0;
            descriptor_last_row_q <= 48'd0;
            descriptor_last_bit_q <= 33'd0;
            descriptor_source_end_byte_q <= 49'd0;
            descriptor_range_invalid_q <= 1'b0;
            descriptor_bounds_invalid_q <= 1'b0;
            destination_inside_q <= 1'b0;
            pixel_last_x_q <= 1'b0;
            pixel_last_y_q <= 1'b0;
            source_base_q <= 32'd0;
            destination_base_q <= 32'd0;
            command_descriptor_offset_q <= 32'd0;
            command_foreground_q <= 32'd0;
            command_background_q <= 32'd0;
            command_transparent_index_q <= 8'd0;
            command_source_format_q <= 8'd0;
            command_destination_format_q <= 8'd0;
            command_destination_index8_q <= 1'b0;
            command_destination_rgb565_q <= 1'b0;
            source_row_product_q <= 48'd0;
            destination_row_product_q <= 48'd0;
            source_row_column_q <= 32'd0;
            destination_row_column_q <= 32'd0;
            source_row_product_low_q <= 32'd0;
            source_row_product_high_q <= 32'd0;
            destination_row_product_low_q <= 32'd0;
            destination_row_product_high_q <= 32'd0;
            source_row_multiply_operand_q <= 16'd0;
            destination_row_multiply_operand_q <= 16'd0;
            source_pitch_low_operand_q <= 16'd0;
            source_pitch_high_operand_q <= 16'd0;
            destination_pitch_low_operand_q <= 16'd0;
            destination_pitch_high_operand_q <= 16'd0;
            descriptor_range_accumulator_q <= 48'd0;
            descriptor_range_multiplicand_q <= 48'd0;
            descriptor_range_multiplier_q <= 16'd0;
            descriptor_range_step_q <= 4'd0;
            source_row_operand_q <= 16'd0;
            destination_row_operand_q <= 16'd0;
            source_column_byte_q <= 32'd0;
            destination_column_byte_q <= 32'd0;
            abort_pending_q <= 1'b0;
            writer_started_q <= 1'b0;
        end else begin
            done <= 1'b0;
            writer_start <= 1'b0;
            writer_abort <= 1'b0;
            writer_flush <= 1'b0;
            source_row_multiply_operand_q <= source_row_operand_q;
            destination_row_multiply_operand_q <= destination_row_operand_q;
            source_pitch_low_operand_q <= source_pitch[15:0];
            source_pitch_high_operand_q <= source_pitch[31:16];
            destination_pitch_low_operand_q <= destination_pitch[15:0];
            destination_pitch_high_operand_q <= destination_pitch[31:16];
            source_row_product_low_q <=
                source_row_multiply_operand_q * source_pitch_low_operand_q;
            source_row_product_high_q <=
                source_row_multiply_operand_q * source_pitch_high_operand_q;
            destination_row_product_low_q <=
                destination_row_multiply_operand_q *
                destination_pitch_low_operand_q;
            destination_row_product_high_q <=
                destination_row_multiply_operand_q *
                destination_pitch_high_operand_q;
            source_row_product_q <=
                {source_row_product_high_q, 16'd0} +
                {16'd0, source_row_product_low_q};
            destination_row_product_q <=
                {destination_row_product_high_q, 16'd0} +
                {16'd0, destination_row_product_low_q};
            source_row_column_q <= source_row_product_q[31:0] +
                source_column_byte_q;
            destination_row_column_q <= destination_row_product_q[31:0] +
                destination_column_byte_q;
            source_address_q <= source_base_q + source_row_column_q;
            destination_address_q <= destination_base_q +
                destination_row_column_q;
            blend_multiply_source_r_q <= blend_source_r_q;
            blend_multiply_source_g_q <= blend_source_g_q;
            blend_multiply_source_b_q <= blend_source_b_q;
            blend_multiply_destination_r_q <= blend_destination_r_q;
            blend_multiply_destination_g_q <= blend_destination_g_q;
            blend_multiply_destination_b_q <= blend_destination_b_q;
            blend_multiply_alpha_q <= blend_alpha_q;
            blend_multiply_inverse_alpha_q <= blend_inverse_alpha_q;
            blend_source_product_r_q <=
                blend_multiply_source_r_q * blend_multiply_alpha_q;
            blend_source_product_g_q <=
                blend_multiply_source_g_q * blend_multiply_alpha_q;
            blend_source_product_b_q <=
                blend_multiply_source_b_q * blend_multiply_alpha_q;
            blend_destination_product_r_q <=
                blend_multiply_destination_r_q *
                blend_multiply_inverse_alpha_q;
            blend_destination_product_g_q <=
                blend_multiply_destination_g_q *
                blend_multiply_inverse_alpha_q;
            blend_destination_product_b_q <=
                blend_multiply_destination_b_q *
                blend_multiply_inverse_alpha_q;
            blend_div_multiply_operand_r_q <= blend_div_operand_r_q;
            blend_div_multiply_operand_g_q <= blend_div_operand_g_q;
            blend_div_multiply_operand_b_q <= blend_div_operand_b_q;
            blend_div_operand_r_q <= blend_numerator_r_q + 17'd8;
            blend_div_operand_g_q <= blend_numerator_g_q + 17'd8;
            blend_div_operand_b_q <= blend_numerator_b_q + 17'd8;
            blend_div_product_r_q <= blend_div_product_r_next;
            blend_div_product_g_q <= blend_div_product_g_next;
            blend_div_product_b_q <= blend_div_product_b_next;
            cache_q <= m_axi_rdata;
            if (abort && busy)
                abort_pending_q <= 1'b1;

            if (abort_pending_q && state != ST_ABORT &&
                state != ST_FAIL && state != ST_IDLE) begin
                m_axi_arvalid <= 1'b0;
                m_axi_rready <= 1'b0;
                pixel_valid <= 1'b0;
                writer_abort <= 1'b1;
                state <= ST_ABORT;
            end else case (state)
                ST_IDLE: if (start) begin
                    busy <= 1'b1;
                    status <= `ASTRA_RENDER_STATUS_OK;
                    fault_detail <= 32'd0;
                    completed_pixels <= 32'd0;
                    descriptor_index_q <= 13'd0;
                    descriptor_last_index_q <= descriptor_count - 13'd1;
                    prepass_q <= 1'b1;
                    abort_pending_q <= 1'b0;
                    writer_started_q <= 1'b0;
                    command_descriptor_offset_q <= descriptor_offset;
                    command_foreground_q <= foreground;
                    command_background_q <= background;
                    command_transparent_index_q <= transparent_index;
                    command_source_format_q <= source_format;
                    case (source_format)
                        `ASTRA_RENDER_FORMAT_MASK1:
                            source_apply_kind_q <= SOURCE_APPLY_MASK;
                        `ASTRA_RENDER_FORMAT_A4:
                            source_apply_kind_q <= SOURCE_APPLY_A4;
                        `ASTRA_RENDER_FORMAT_A8:
                            source_apply_kind_q <= SOURCE_APPLY_A8;
                        default:
                            source_apply_kind_q <= SOURCE_APPLY_INDEX;
                    endcase
                    command_destination_format_q <= destination_format;
                    command_destination_index8_q <=
                        destination_format == `ASTRA_RENDER_FORMAT_INDEX8;
                    command_destination_rgb565_q <=
                        destination_format == `ASTRA_RENDER_FORMAT_RGB565;
                    effective_clip_left_q <= effective_clip_left;
                    effective_clip_top_q <= effective_clip_top;
                    effective_clip_right_q <= effective_clip_right;
                    effective_clip_bottom_q <= effective_clip_bottom;
                    state <= ST_DESC_PREPARE;
                end

                ST_DESC_PREPARE: begin
                    m_axi_araddr <= descriptor_address;
                    descriptor_is_last_q <=
                        descriptor_index_q == descriptor_last_index_q;
                    state <= ST_DESC_AR;
                end

                ST_DESC_AR: begin
                    read_kind_q <= READ_DESCRIPTOR;
                    descriptor_first_error_q <= 1'b0;
                    m_axi_arvalid <= 1'b1;
                    if (m_axi_arvalid && m_axi_arready) begin
                        m_axi_arvalid <= 1'b0;
                        m_axi_rready <= 1'b1;
                        state <= ST_DESC_R0;
                    end
                end

                ST_DESC_R0: if (m_axi_rvalid && m_axi_rready) begin
                    descriptor_beat0_q <= m_axi_rdata;
                    if (m_axi_rlast) begin
                        m_axi_rready <= 1'b0;
                        fail(`ASTRA_RENDER_STATUS_AXI_READ, 32'h00050001);
                    end else begin
                        descriptor_first_error_q <=
                            m_axi_rid != AXI_ID || m_axi_rresp != 2'b00;
                        state <= ST_DESC_R1;
                    end
                end

                ST_DESC_R1: if (m_axi_rvalid && m_axi_rready) begin
                    descriptor_beat1_q <= m_axi_rdata;
                    m_axi_rready <= 1'b0;
                    if (descriptor_first_error_q ||
                        m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        !m_axi_rlast) begin
                        fail(`ASTRA_RENDER_STATUS_AXI_READ, 32'h00050002);
                    end else begin
                        state <= ST_DESC_CHECK;
                    end
                end

                ST_DESC_CHECK: begin
                    source_offset_q <= desc_source_offset;
                    source_x_q <= desc_source_xy[31:16];
                    source_y_q <= desc_source_xy[15:0];
                    destination_x_q <= desc_destination_xy[31:16];
                    destination_y_q <= desc_destination_xy[15:0];
                    glyph_width_q <= desc_width_height[31:16];
                    glyph_height_q <= desc_width_height[15:0];
                    glyph_last_x_q <= desc_width_height[31:16] - 16'd1;
                    glyph_last_y_q <= desc_width_height[15:0] - 16'd1;
                    descriptor_source_end_x_q <= desc_source_end_x;
                    descriptor_source_end_y_q <= desc_source_end_y;
                    source_base_q <= arena_base + source_data_offset +
                        desc_source_offset;
                    destination_base_q <= arena_base +
                        destination_data_offset;
                    state <= ST_DESC_VALIDATE;
                end

                ST_DESC_VALIDATE: begin
                    if (!prepass_q) begin
                        state <= ST_EXEC_SETUP;
                    end else begin
                        descriptor_bounds_invalid_q <=
                            glyph_width_q == 16'd0 ||
                            glyph_height_q == 16'd0 ||
                            descriptor_source_end_x_q >
                                {1'b0, source_width} ||
                            descriptor_source_end_y_q >
                                {1'b0, source_height};
                        state <= ST_DESC_VALIDATE_RESULT;
                    end
                end

                ST_DESC_VALIDATE_RESULT: begin
                    if (descriptor_bounds_invalid_q)
                        fail(`ASTRA_RENDER_STATUS_BAD_RANGE,
                             {16'h0005, descriptor_index_q, 3'd0});
                    else
                        state <= ST_DESC_RANGE_ROW;
                end

                ST_DESC_RANGE_ROW: begin
                    descriptor_range_accumulator_q <= 48'd0;
                    descriptor_range_multiplicand_q <=
                        {16'd0, source_pitch};
                    descriptor_range_multiplier_q <=
                        descriptor_source_end_y_q[15:0] - 16'd1;
                    descriptor_range_step_q <= 4'd0;
                    if (command_source_format_q == `ASTRA_RENDER_FORMAT_MASK1)
                        descriptor_last_bit_q <=
                            {16'd0, descriptor_source_end_x_q} - 33'd1;
                    else if (command_source_format_q == `ASTRA_RENDER_FORMAT_A4 ||
                             command_source_format_q == `ASTRA_RENDER_FORMAT_INDEX4)
                        descriptor_last_bit_q <=
                            ({16'd0, descriptor_source_end_x_q} - 33'd1) << 2;
                    else
                        descriptor_last_bit_q <=
                            ({16'd0, descriptor_source_end_x_q} - 33'd1) << 3;
                    state <= ST_DESC_RANGE_MULTIPLY;
                end

                ST_DESC_RANGE_MULTIPLY: begin
                    descriptor_last_row_q <=
                        descriptor_range_accumulator_q +
                        (descriptor_range_multiplier_q[0] ?
                         descriptor_range_multiplicand_q : 48'd0);
                    if (descriptor_range_multiplier_q[0])
                        descriptor_range_accumulator_q <=
                            descriptor_range_accumulator_q +
                            descriptor_range_multiplicand_q;
                    descriptor_range_multiplicand_q <=
                        descriptor_range_multiplicand_q << 1;
                    descriptor_range_multiplier_q <=
                        descriptor_range_multiplier_q >> 1;
                    if (descriptor_range_step_q == 4'd15) begin
                        state <= ST_DESC_RANGE_SUM;
                    end else begin
                        descriptor_range_step_q <=
                            descriptor_range_step_q + 4'd1;
                    end
                end

                ST_DESC_RANGE_SUM: begin
                    descriptor_source_end_byte_q <=
                        {17'd0, source_offset_q} +
                        {1'b0, descriptor_last_row_q} +
                        {16'd0, descriptor_last_bit_q[32:3]} + 49'd1;
                    state <= ST_DESC_RANGE_FINAL;
                end

                ST_DESC_RANGE_FINAL: begin
                    descriptor_range_invalid_q <=
                        descriptor_source_end_byte_q >
                        {17'd0, source_data_bytes};
                    state <= ST_DESC_RANGE_RESULT;
                end

                ST_DESC_RANGE_RESULT: begin
                    if (descriptor_range_invalid_q)
                        fail(`ASTRA_RENDER_STATUS_BAD_RANGE,
                             {16'h0005, descriptor_index_q, 3'd0});
                    else
                        state <= ST_PREPASS_NEXT;
                end

                ST_PREPASS_NEXT: begin
                    if (descriptor_is_last_q) begin
                        descriptor_index_q <= 13'd0;
                        prepass_q <= 1'b0;
                        state <= ST_WRITER_START;
                    end else begin
                        descriptor_index_q <= descriptor_index_q + 13'd1;
                        state <= ST_DESC_PREPARE;
                    end
                end

                ST_WRITER_START: begin
                    writer_start <= 1'b1;
                    writer_started_q <= 1'b1;
                    state <= ST_DESC_PREPARE;
                end

                ST_EXEC_SETUP: begin
                    pixel_x_q <= 16'd0;
                    pixel_y_q <= 16'd0;
                    state <= ST_PIXEL_CLASSIFY;
                end

                ST_PIXEL_CLASSIFY: begin
                    destination_pixel_x_q <= destination_pixel_x;
                    destination_pixel_y_q <= destination_pixel_y;
                    state <= ST_PIXEL_BOUNDS;
                end

                ST_PIXEL_BOUNDS: begin
                    source_row_operand_q <= current_source_y[15:0];
                    destination_row_operand_q <=
                        destination_pixel_y_q[15:0];
                    source_column_byte_q <= source_column_byte;
                    destination_column_byte_q <= destination_column_byte;
                    destination_inside_q <= destination_inside;
                    state <= ST_PIXEL_BOUNDS_RESULT;
                end

                ST_PIXEL_BOUNDS_RESULT: begin
                    if (!destination_inside_q) begin
                        state <= ST_PIXEL_NEXT;
                    end else begin
                        state <= ST_PIXEL_MULTIPLY;
                    end
                end

                ST_PIXEL_MULTIPLY: begin
                    state <= ST_PIXEL_ROW_COMBINE;
                end

                ST_PIXEL_ROW_COMBINE: begin
                    state <= ST_PIXEL_ADDRESS_SUM;
                end

                ST_PIXEL_ADDRESS_SUM: begin
                    state <= ST_PIXEL_ADDRESS_BASE;
                end

                ST_PIXEL_ADDRESS_BASE: begin
                    state <= ST_SOURCE_REQUEST;
                end

                ST_SOURCE_REQUEST: begin
                    read_kind_q <= READ_SOURCE;
                    m_axi_araddr <= {source_address_q[31:3], 3'b000};
                    m_axi_arvalid <= 1'b1;
                    state <= ST_SOURCE_AR;
                end

                ST_SOURCE_AR: if (m_axi_arvalid && m_axi_arready) begin
                    m_axi_arvalid <= 1'b0;
                    m_axi_rready <= 1'b1;
                    state <= ST_SOURCE_R;
                end

                ST_SOURCE_R: if (m_axi_rvalid && m_axi_rready) begin
                    m_axi_rready <= 1'b0;
                    if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        !m_axi_rlast) begin
                        fail(`ASTRA_RENDER_STATUS_AXI_READ, 32'h00050003);
                    end else begin
                        state <= ST_SOURCE_DECODE;
                    end
                end

                ST_SOURCE_DECODE: begin
                    source_sample_q <= decoded_sample;
                    source_sample_zero_q <= decoded_sample == 8'd0;
                    source_sample_full_q <=
                        (source_apply_kind_q == SOURCE_APPLY_A4 &&
                         decoded_sample[3:0] == 4'hf) ||
                        (source_apply_kind_q == SOURCE_APPLY_A8 &&
                         decoded_sample == 8'hff);
                    source_sample_transparent_q <=
                        decoded_sample == command_transparent_index_q;
                    state <= ST_SOURCE_APPLY;
                end

                ST_SOURCE_APPLY: begin
                    source_visible_q <= 1'b1;
                    needs_destination_q <= 1'b0;
                    coverage_q <= 8'd0;
                    coverage_max_q <= 8'd0;
                    case (source_apply_kind_q)
                        SOURCE_APPLY_MASK: begin
                            if (!source_sample_zero_q) begin
                            source_color_q <= command_foreground_q;
                            output_pixel_q <= command_foreground_q;
                        end else if (command_flags[0]) begin
                            source_color_q <= command_background_q;
                            output_pixel_q <= command_background_q;
                            end else begin
                                source_visible_q <= 1'b0;
                            end
                            state <= ST_EMIT;
                        end
                        SOURCE_APPLY_A4: begin
                        source_color_q <= command_foreground_q;
                        output_pixel_q <= command_foreground_q;
                            coverage_q <= {4'd0, source_sample_q[3:0]};
                            coverage_max_q <= 8'd15;
                            needs_destination_q <= !source_sample_full_q;
                            source_visible_q <= !source_sample_zero_q;
                            state <= source_sample_zero_q ?
                                ST_PIXEL_NEXT : source_sample_full_q ?
                                ST_EMIT : ST_DEST_AR;
                        end
                        SOURCE_APPLY_A8: begin
                        source_color_q <= command_foreground_q;
                        output_pixel_q <= command_foreground_q;
                            coverage_q <= source_sample_q;
                            coverage_max_q <= 8'd255;
                            needs_destination_q <= !source_sample_full_q;
                            source_visible_q <= !source_sample_zero_q;
                            state <= source_sample_zero_q ? ST_PIXEL_NEXT :
                                source_sample_full_q ? ST_EMIT : ST_DEST_AR;
                        end
                        default: begin
                        if (source_sample_transparent_q) begin
                                source_visible_q <= 1'b0;
                                state <= ST_PIXEL_NEXT;
                            end else if (command_destination_index8_q) begin
                                source_color_q <= {24'd0, source_sample_q};
                                output_pixel_q <= {24'd0, source_sample_q};
                                state <= ST_EMIT;
                            end else begin
                                palette_address_q <= palette_address;
                                state <= ST_PALETTE_REQUEST;
                            end
                        end
                    endcase
                end

                ST_PALETTE_REQUEST: begin
                    read_kind_q <= READ_PALETTE;
                    m_axi_araddr <= {palette_address_q[31:3], 3'b000};
                    m_axi_arvalid <= 1'b1;
                    state <= ST_PALETTE_AR;
                end

                ST_PALETTE_AR: if (m_axi_arvalid && m_axi_arready) begin
                    m_axi_arvalid <= 1'b0;
                    m_axi_rready <= 1'b1;
                    state <= ST_PALETTE_R;
                end

                ST_PALETTE_R: if (m_axi_rvalid && m_axi_rready) begin
                    m_axi_rready <= 1'b0;
                    if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        !m_axi_rlast) begin
                        fail(`ASTRA_RENDER_STATUS_AXI_READ, 32'h00050004);
                    end else begin
                        state <= ST_PALETTE_DECODE;
                    end
                end

                ST_PALETTE_DECODE: begin
                    source_color_q <= decoded_palette;
                    output_pixel_q <= pack_destination(
                        command_destination_format_q,
                                                       decoded_palette);
                    coverage_q <= decoded_palette[31:24];
                    coverage_max_q <= 8'd255;
                    source_visible_q <= decoded_palette[31:24] != 8'd0;
                    needs_destination_q <= decoded_palette[31:24] != 8'hff;
                    palette_transparent_q <=
                        decoded_palette[31:24] == 8'd0;
                    palette_opaque_q <= decoded_palette[31:24] == 8'hff;
                    state <= ST_PALETTE_DECIDE;
                end

                ST_PALETTE_DECIDE: begin
                    state <= palette_transparent_q ? ST_PIXEL_NEXT :
                        palette_opaque_q ? ST_EMIT : ST_DEST_AR;
                end

                ST_DEST_AR: begin
                    read_kind_q <= READ_DESTINATION;
                    m_axi_araddr <= {destination_address_q[31:3], 3'b000};
                    m_axi_arvalid <= 1'b1;
                    if (m_axi_arvalid && m_axi_arready) begin
                        m_axi_arvalid <= 1'b0;
                        m_axi_rready <= 1'b1;
                        state <= ST_DEST_R;
                    end
                end

                ST_DEST_R: if (m_axi_rvalid && m_axi_rready) begin
                    m_axi_rready <= 1'b0;
                    if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                        !m_axi_rlast) begin
                        fail(`ASTRA_RENDER_STATUS_AXI_READ, 32'h00050005);
                    end else begin
                        destination_color_q <= decode_destination(
                            command_destination_format_q, m_axi_rdata,
                            destination_lane);
                        state <= ST_BLEND;
                    end
                end

                ST_BLEND: begin
                    blend_component_rgb565_q <=
                        command_destination_rgb565_q &&
                        (source_apply_kind_q == SOURCE_APPLY_A4 ||
                         source_apply_kind_q == SOURCE_APPLY_A8);
                    if (command_destination_rgb565_q &&
                        (source_apply_kind_q == SOURCE_APPLY_A4 ||
                         source_apply_kind_q == SOURCE_APPLY_A8)) begin
                        blend_source_r_q <= {3'd0, command_foreground_q[15:11]};
                        blend_source_g_q <= {2'd0, command_foreground_q[10:5]};
                        blend_source_b_q <= {3'd0, command_foreground_q[4:0]};
                        blend_destination_r_q <= {3'd0,
                            destination_color_q[23:19]};
                        blend_destination_g_q <= {2'd0,
                            destination_color_q[15:10]};
                        blend_destination_b_q <= {3'd0,
                            destination_color_q[7:3]};
                    end else begin
                        blend_source_r_q <= source_color_q[23:16];
                        blend_source_g_q <= source_color_q[15:8];
                        blend_source_b_q <= source_color_q[7:0];
                        blend_destination_r_q <= destination_color_q[23:16];
                        blend_destination_g_q <= destination_color_q[15:8];
                        blend_destination_b_q <= destination_color_q[7:0];
                    end
                    if (coverage_max_q == 8'd15) begin
                        blend_alpha_q <= {4'd0, coverage_q[3:0]};
                        blend_inverse_alpha_q <=
                            {4'd0, 4'd15 - coverage_q[3:0]};
                    end else begin
                        blend_alpha_q <= coverage_q;
                        blend_inverse_alpha_q <= 8'd255 - coverage_q;
                    end
                    state <= ST_BLEND_MULTIPLY;
                end

                ST_BLEND_MULTIPLY: begin
                    state <= ST_BLEND_PRODUCT_PIPE;
                end

                ST_BLEND_PRODUCT_PIPE: begin
                    state <= ST_BLEND_ACCUMULATE;
                end

                ST_BLEND_ACCUMULATE: begin
                    blend_source_stage_r_q <= blend_source_product_r_q;
                    blend_source_stage_g_q <= blend_source_product_g_q;
                    blend_source_stage_b_q <= blend_source_product_b_q;
                    blend_destination_stage_r_q <=
                        blend_destination_product_r_q;
                    blend_destination_stage_g_q <=
                        blend_destination_product_g_q;
                    blend_destination_stage_b_q <=
                        blend_destination_product_b_q;
                    state <= ST_BLEND_ACCUMULATE_FINISH;
                end

                ST_BLEND_ACCUMULATE_FINISH: begin
                    blend_numerator_r_q <= {1'b0, blend_source_stage_r_q} +
                        {1'b0, blend_destination_stage_r_q};
                    blend_numerator_g_q <= {1'b0, blend_source_stage_g_q} +
                        {1'b0, blend_destination_stage_g_q};
                    blend_numerator_b_q <= {1'b0, blend_source_stage_b_q} +
                        {1'b0, blend_destination_stage_b_q};
                    state <= ST_BLEND_NORMALIZE;
                end

                ST_BLEND_NORMALIZE: begin
                    if (coverage_max_q == 8'd15) begin
                        // Exact floor(n / 15) for the complete 0..3832 range.
                        state <= ST_BLEND_DIV_MULTIPLY;
                    end else begin
                        blend_result_r_q <= divide_255_round(blend_numerator_r_q);
                        blend_result_g_q <= divide_255_round(blend_numerator_g_q);
                        blend_result_b_q <= divide_255_round(blend_numerator_b_q);
                        state <= ST_BLEND_PACK;
                    end
                end

                ST_BLEND_DIV_MULTIPLY: begin
                    state <= ST_BLEND_DIV_PIPE;
                end

                ST_BLEND_DIV_PIPE: begin
                    state <= ST_BLEND_DIVIDE;
                end

                ST_BLEND_DIVIDE: begin
                    blend_result_r_q <= blend_div_product_r_q >> 12;
                    blend_result_g_q <= blend_div_product_g_q >> 12;
                    blend_result_b_q <= blend_div_product_b_q >> 12;
                    state <= ST_BLEND_PACK;
                end

                ST_BLEND_PACK: begin
                    if (blend_component_rgb565_q)
                        output_pixel_q <= {16'd0, blend_result_r_q[4:0],
                            blend_result_g_q[5:0], blend_result_b_q[4:0]};
                    else
                        output_pixel_q <= pack_destination(
                            command_destination_format_q,
                            {8'hff, blend_result_r_q, blend_result_g_q,
                             blend_result_b_q});
                    state <= ST_EMIT;
                end

                ST_EMIT: begin
                    if (!source_visible_q) begin
                        state <= ST_PIXEL_NEXT;
                    end else if (!pixel_valid) begin
                        pixel_address <= destination_address_q;
                        pixel_format <= command_destination_format_q;
                        pixel_value <= output_pixel_q;
                        pixel_valid <= 1'b1;
                    end else if (pixel_ready) begin
                        pixel_valid <= 1'b0;
                        completed_pixels <= completed_pixels + 32'd1;
                        state <= ST_PIXEL_NEXT;
                    end
                end

                ST_PIXEL_NEXT: begin
                    pixel_last_x_q <= pixel_x_q == glyph_last_x_q;
                    pixel_last_y_q <= pixel_y_q == glyph_last_y_q;
                    state <= ST_PIXEL_NEXT_DECIDE;
                end

                ST_PIXEL_NEXT_DECIDE: begin
                    if (pixel_last_x_q) begin
                        pixel_x_q <= 16'd0;
                        if (pixel_last_y_q) begin
                            pixel_y_q <= 16'd0;
                            state <= ST_DESCRIPTOR_NEXT;
                        end else begin
                            pixel_y_q <= pixel_y_q + 16'd1;
                            state <= ST_PIXEL_CLASSIFY;
                        end
                    end else begin
                        pixel_x_q <= pixel_x_q + 16'd1;
                        state <= ST_PIXEL_CLASSIFY;
                    end
                end

                ST_DESCRIPTOR_NEXT: begin
                    if (descriptor_is_last_q) begin
                        state <= ST_FLUSH;
                    end else begin
                        descriptor_index_q <= descriptor_index_q + 13'd1;
                        state <= ST_DESC_PREPARE;
                    end
                end

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
                    busy <= 1'b0;
                    done <= 1'b1;
                    writer_started_q <= 1'b0;
                    state <= ST_IDLE;
                end

                ST_ABORT: if (!writer_started_q || writer_aborted) begin
                    status <= `ASTRA_RENDER_STATUS_RESET;
                    busy <= 1'b0;
                    done <= 1'b1;
                    abort_pending_q <= 1'b0;
                    writer_started_q <= 1'b0;
                    state <= ST_IDLE;
                end

                ST_FAIL: begin
                    if (writer_started_q) begin
                        writer_abort <= 1'b1;
                        state <= ST_FAIL_WAIT;
                    end else begin
                        busy <= 1'b0;
                        done <= 1'b1;
                        state <= ST_IDLE;
                    end
                end

                ST_FAIL_WAIT: if (writer_aborted) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    writer_started_q <= 1'b0;
                    state <= ST_IDLE;
                end

                default: begin
                    status <= `ASTRA_RENDER_STATUS_UNSUPPORTED;
                    fault_detail <= 32'h0005ffff;
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
