// Copyright (c) 2026 Astra68 contributors
//
// Basic Astraea fill and same-format one-to-one blit executor. All descriptor
// and ring validation happens before start. This block completes signed
// clipping and address planning before it starts the shared pixel writer.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_blitter #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = {AXI_ID_WIDTH{1'b0}}
) (
    input  wire                         clk,
    input  wire                         reset,
    input  wire                         start,
    input  wire                         abort,
    input  wire                         is_fill,
    input  wire                         is_blit,
    input  wire [31:0]                  arena_base,
    input  wire signed [15:0]           clip_left,
    input  wire signed [15:0]           clip_top,
    input  wire signed [15:0]           clip_right,
    input  wire signed [15:0]           clip_bottom,
    input  wire signed [15:0]           source_x,
    input  wire signed [15:0]           source_y,
    input  wire signed [15:0]           destination_x,
    input  wire signed [15:0]           destination_y,
    input  wire [15:0]                  source_width,
    input  wire [15:0]                  source_height,
    input  wire [15:0]                  destination_width,
    input  wire [15:0]                  destination_height,
    input  wire [15:0]                  command_flags,
    input  wire [31:0]                  options,
    input  wire                         same_surface,

    input  wire [31:0]                  destination_data_offset,
    input  wire [31:0]                  destination_pitch,
    input  wire [15:0]                  destination_surface_width,
    input  wire [15:0]                  destination_surface_height,
    input  wire [7:0]                   destination_format,
    input  wire [2:0]                   destination_bytes_per_pixel,

    input  wire [31:0]                  source_data_offset,
    input  wire [31:0]                  source_pitch,
    input  wire [15:0]                  source_surface_width,
    input  wire [15:0]                  source_surface_height,
    input  wire [7:0]                   source_format,
    input  wire [2:0]                   source_bytes_per_pixel,
    input  wire [31:0]                  source_palette_offset,
    input  wire [31:0]                  auxiliary_data_offset,
    input  wire [31:0]                  auxiliary_pitch,
    input  wire [15:0]                  auxiliary_surface_width,
    input  wire [15:0]                  auxiliary_surface_height,

    output reg                          busy,
    output reg                          done,
    output reg  [15:0]                  status,
    output reg  [31:0]                  fault_detail,
    output reg  [31:0]                  completed_pixels,

    output reg                          writer_start,
    output reg                          writer_abort,
    output reg                          writer_flush,
    input  wire                         writer_flush_ready,
    input  wire                         writer_busy,
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
    localparam [5:0] ST_PLAN = 6'd1;
    localparam [5:0] ST_PLAN_CANDIDATES = 6'd2;
    localparam [5:0] ST_PLAN_DEST_LIMIT = 6'd3;
    localparam [5:0] ST_PLAN_CLIP_LIMIT = 6'd4;
    localparam [5:0] ST_PLAN_SOURCE_LIMIT = 6'd5;
    localparam [5:0] ST_PLAN_VALIDATE = 6'd6;
    localparam [5:0] ST_PLAN_COORDINATES = 6'd7;
    localparam [5:0] ST_PLAN_COUNTS = 6'd8;
    localparam [5:0] ST_ROW_MULTIPLY = 6'd9;
    localparam [5:0] ST_ROW_COMBINE = 6'd10;
    localparam [5:0] ST_FIRST_ADDRESS = 6'd11;
    localparam [5:0] ST_LAST_MULTIPLY = 6'd12;
    localparam [5:0] ST_LAST_COMBINE = 6'd13;
    localparam [5:0] ST_EXECUTION_ADDRESS = 6'd14;
    localparam [5:0] ST_WRITER_START = 6'd15;
    localparam [5:0] ST_SOURCE = 6'd16;
    localparam [5:0] ST_SOURCE_REQUEST = 6'd17;
    localparam [5:0] ST_SOURCE_RESPONSE = 6'd18;
    localparam [5:0] ST_PIXEL = 6'd19;
    localparam [5:0] ST_NEXT_PIXEL = 6'd20;
    localparam [5:0] ST_NEXT_ROW_ADDRESS = 6'd21;
    localparam [5:0] ST_NEXT_ROW_PIXEL = 6'd22;
    localparam [5:0] ST_FLUSH = 6'd23;
    localparam [5:0] ST_WRITER_DONE = 6'd24;
    localparam [5:0] ST_ABORT_WRITER = 6'd25;
    localparam [5:0] ST_FINISH = 6'd26;
    localparam [5:0] ST_PLAN_BLIT_DIMENSIONS = 6'd27;
    localparam [5:0] ST_PLAN_BLIT_FORMAT = 6'd28;
    localparam [5:0] ST_FIRST_ADDRESS_ROW = 6'd29;
    localparam [5:0] ST_FIRST_ADDRESS_X = 6'd30;
    localparam [5:0] ST_EXECUTION_PIXEL = 6'd31;
    localparam [5:0] ST_PLAN_BLIT_FORMAT_DECIDE = 6'd32;
    localparam [5:0] ST_SCALE_X_LOAD = 6'd33;
    localparam [5:0] ST_SCALE_DIV_SHIFT = 6'd34;
    localparam [5:0] ST_SCALE_DIVIDE = 6'd35;
    localparam [5:0] ST_SCALE_DIV_ROUND = 6'd36;
    localparam [5:0] ST_SCALE_STORE = 6'd37;
    localparam [5:0] ST_PHASE_LOAD = 6'd38;
    localparam [5:0] ST_PHASE_MULTIPLY = 6'd39;
    localparam [5:0] ST_PHASE_STORE = 6'd40;
    localparam [5:0] ST_SOURCE_COORDINATES = 6'd41;
    localparam [5:0] ST_NEXT_ROW_SOURCE_MULTIPLY = 6'd42;
    localparam [5:0] ST_NEXT_ROW_SOURCE_COMBINE = 6'd43;
    localparam [5:0] ST_NEXT_ROW_SOURCE_ADDRESS = 6'd44;
    localparam [5:0] ST_DESTINATION = 6'd45;
    localparam [5:0] ST_DESTINATION_REQUEST = 6'd46;
    localparam [5:0] ST_DESTINATION_RESPONSE = 6'd47;
    localparam [5:0] ST_COMPOSITE = 6'd48;
    localparam [5:0] ST_BLEND_SOURCE_LOAD = 6'd49;
    localparam [5:0] ST_BLEND_SOURCE_STORE = 6'd50;
    localparam [5:0] ST_BLEND_MULTIPLY = 6'd51;
    localparam [5:0] ST_BLEND_DESTINATION_LOAD = 6'd52;
    localparam [5:0] ST_BLEND_DESTINATION_STORE = 6'd53;
    localparam [5:0] ST_BLEND_PACK = 6'd54;
    localparam [5:0] ST_SKIP_PIXEL = 6'd55;
    localparam [5:0] ST_PALETTE = 6'd56;
    localparam [5:0] ST_PALETTE_REQUEST = 6'd57;
    localparam [5:0] ST_PALETTE_RESPONSE = 6'd58;
    localparam [5:0] ST_SOURCE_DECODE = 6'd59;
    localparam [5:0] ST_MASK = 6'd60;
    localparam [5:0] ST_MASK_REQUEST = 6'd61;
    localparam [5:0] ST_MASK_RESPONSE = 6'd62;
    localparam [5:0] ST_NEXT_SOURCE_ADDRESS = 6'd63;
    localparam [6:0] ST_NEXT_ROW_SOURCE_MAP = 7'd64;
    localparam [6:0] ST_BLEND_DESTINATION_DIVIDE = 7'd65;
    localparam [6:0] ST_PALETTE_DECODE = 7'd66;
    localparam [6:0] ST_PLAN_BLIT_SOURCE_VALIDATE = 7'd67;
    localparam [6:0] ST_PLAN_BLIT_MASK_DIMENSIONS = 7'd68;
    localparam [6:0] ST_PLAN_BLIT_MASK_PITCH = 7'd69;
    localparam [6:0] ST_NEXT_SOURCE_MAP = 7'd70;
    localparam [6:0] ST_SOURCE_DISPATCH = 7'd71;
    localparam [6:0] ST_MASK_DECODE = 7'd72;
    localparam [6:0] ST_PALETTE_ISSUE = 7'd73;
    localparam [6:0] ST_NEXT_ROW_SOURCE_X = 7'd74;
    localparam [6:0] ST_NEXT_ROW_SOURCE_OPERANDS = 7'd75;
    localparam [6:0] ST_MASK_DISPATCH = 7'd76;
    localparam [6:0] ST_PLAN_COMMAND_VALIDATE = 7'd77;
    localparam [6:0] ST_BLEND_OUTPUT = 7'd78;
    localparam [6:0] ST_PALETTE_OUTPUT = 7'd79;
    localparam [6:0] ST_NEXT_SOURCE_OFFSET = 7'd80;
    localparam [6:0] ST_SOURCE_ADDRESS_COMMIT = 7'd81;
    localparam [6:0] ST_BLEND_DIVIDE_FINISH = 7'd82;

    function automatic signed [17:0] signed_max18(
        input signed [17:0] left,
        input signed [17:0] right
    );
        begin
            signed_max18 = left > right ? left : right;
        end
    endfunction

    function automatic signed [17:0] signed_min18(
        input signed [17:0] left,
        input signed [17:0] right
    );
        begin
            signed_min18 = left < right ? left : right;
        end
    endfunction

    function automatic [7:0] beat_byte(
        input [63:0] beat,
        input [2:0] lane
    );
        begin
            beat_byte = beat[lane * 8 +: 8];
        end
    endfunction

    function automatic [31:0] expand_source_argb(
        input [7:0] source_pixel_format,
        input [31:0] source_pixel
    );
        reg [4:0] red5;
        reg [5:0] green6;
        reg [4:0] blue5;
        begin
            red5 = source_pixel[15:11];
            green6 = source_pixel[10:5];
            blue5 = source_pixel[4:0];
            case (source_pixel_format)
                `ASTRA_RENDER_FORMAT_RGB565:
                    expand_source_argb = {
                        8'hff,
                        red5, red5[4:2],
                        green6, green6[5:4],
                        blue5, blue5[4:2]
                    };
                `ASTRA_RENDER_FORMAT_XRGB8888:
                    expand_source_argb = {8'hff, source_pixel[23:0]};
                default: expand_source_argb = source_pixel;
            endcase
        end
    endfunction

    function automatic [31:0] pack_destination_pixel(
        input [7:0] destination_pixel_format,
        input [31:0] source_argb
    );
        begin
            case (destination_pixel_format)
                `ASTRA_RENDER_FORMAT_INDEX8:
                    pack_destination_pixel = {24'd0, source_argb[7:0]};
                `ASTRA_RENDER_FORMAT_RGB565:
                    pack_destination_pixel = {
                        16'd0,
                        source_argb[23:19],
                        source_argb[15:10],
                        source_argb[7:3]
                    };
                `ASTRA_RENDER_FORMAT_XRGB8888:
                    pack_destination_pixel = {8'hff, source_argb[23:0]};
                default: pack_destination_pixel = source_argb;
            endcase
        end
    endfunction

    function automatic [7:0] argb_channel(
        input [31:0] argb,
        input [1:0] channel
    );
        begin
            case (channel)
                2'd0: argb_channel = argb[31:24];
                2'd1: argb_channel = argb[23:16];
                2'd2: argb_channel = argb[15:8];
                default: argb_channel = argb[7:0];
            endcase
        end
    endfunction

    function automatic [7:0] saturate_channel(input [8:0] value);
        begin
            saturate_channel = value[8] ? 8'hff : value[7:0];
        end
    endfunction

    function automatic [31:0] apply_rop(
        input [3:0] rop,
        input [31:0] source_pixel,
        input [31:0] destination_pixel
    );
        begin
            apply_rop =
                ({32{rop[0]}} & ~source_pixel & ~destination_pixel) |
                ({32{rop[1]}} & ~source_pixel &  destination_pixel) |
                ({32{rop[2]}} &  source_pixel & ~destination_pixel) |
                ({32{rop[3]}} &  source_pixel &  destination_pixel);
        end
    endfunction

    function automatic [31:0] normalize_destination_pixel(
        input [7:0] destination_pixel_format,
        input [31:0] pixel
    );
        begin
            case (destination_pixel_format)
                `ASTRA_RENDER_FORMAT_INDEX8:
                    normalize_destination_pixel = {24'd0, pixel[7:0]};
                `ASTRA_RENDER_FORMAT_RGB565:
                    normalize_destination_pixel = {16'd0, pixel[15:0]};
                `ASTRA_RENDER_FORMAT_XRGB8888:
                    normalize_destination_pixel = {8'hff, pixel[23:0]};
                default: normalize_destination_pixel = pixel;
            endcase
        end
    endfunction

    (* fsm_encoding = "one_hot" *) reg [6:0] state;
    reg plan_command_valid_q;
    reg abort_pending;
    reg is_fill_q;
    reg is_blit_q;
    reg direct_copy_q;
    reg same_surface_q;
    reg [31:0] arena_base_q;
    reg signed [15:0] clip_left_q;
    reg signed [15:0] clip_top_q;
    reg signed [15:0] clip_right_q;
    reg signed [15:0] clip_bottom_q;
    reg signed [15:0] source_x_q;
    reg signed [15:0] source_y_q;
    reg signed [15:0] destination_x_q;
    reg signed [15:0] destination_y_q;
    reg [15:0] source_command_width_q;
    reg [15:0] source_command_height_q;
    reg [15:0] command_width_q;
    reg [15:0] command_height_q;
    reg [15:0] command_flags_q;
    reg [31:0] options_q;
    reg blit_no_flags_q;
    reg blit_same_format_q;
    reg blit_same_dimensions_q;
    reg blit_dimensions_valid_q;
    reg [16:0] source_end_x_q;
    reg [16:0] source_end_y_q;
    reg [31:0] auxiliary_min_pitch_q;
    (* keep = "true" *) reg blit_flag_contract_q;
    (* keep = "true" *) reg blit_format_contract_q;
    (* keep = "true" *) reg blit_palette_contract_q;
    (* keep = "true" *) reg blit_auxiliary_contract_q;
    (* keep = "true" *) reg blit_overlap_contract_q;
    reg [31:0] destination_data_offset_q;
    reg [31:0] destination_pitch_q;
    reg [15:0] destination_surface_width_q;
    reg [15:0] destination_surface_height_q;
    reg [7:0] destination_format_q;
    reg [2:0] destination_bpp_q;
    reg [1:0] destination_shift_q;
    reg [31:0] source_data_offset_q;
    reg [31:0] source_pitch_q;
    reg [15:0] source_surface_width_q;
    reg [15:0] source_surface_height_q;
    reg [7:0] source_format_q;
    reg [2:0] source_bpp_q;
    reg [1:0] source_shift_q;
    reg [31:0] source_palette_offset_q;
    reg [31:0] auxiliary_data_offset_q;
    reg [31:0] auxiliary_pitch_q;
    reg [15:0] auxiliary_surface_width_q;
    reg [15:0] auxiliary_surface_height_q;

    // The sprite scene store uses the same Q24 scale contract. This copy is
    // embedded because the two clients have unrelated lifetime/state-machine
    // interfaces; sharing a live divider would couple active scanout metadata
    // validation to asynchronous rendering.
    reg scale_axis_q;
    reg [39:0] scale_dividend_q;
    reg [15:0] scale_denominator_q;
    reg [16:0] scale_remainder_q;
    reg [39:0] scale_quotient_q;
    reg [39:0] scale_result_q;
    reg [5:0] scale_bit_q;
    reg [16:0] scale_shifted_q;
    reg [39:0] scale_step_x_q;
    reg [39:0] scale_step_y_q;
    reg phase_axis_q;
    reg [15:0] phase_multiplier_q;
    reg [55:0] phase_multiplicand_q;
    reg [55:0] phase_product_q;
    reg [4:0] phase_bit_q;
    reg [39:0] source_phase_x_start_q;
    (* extract_enable = "no" *)
    reg [39:0] source_phase_x_q;
    reg [39:0] source_phase_y_q;

    wire signed [17:0] destination_x_w =
        {{2{destination_x_q[15]}}, destination_x_q};
    wire signed [17:0] destination_y_w =
        {{2{destination_y_q[15]}}, destination_y_q};
    wire signed [17:0] source_x_w =
        {{2{source_x_q[15]}}, source_x_q};
    wire signed [17:0] source_y_w =
        {{2{source_y_q[15]}}, source_y_q};
    wire signed [17:0] clip_left_w =
        {{2{clip_left_q[15]}}, clip_left_q};
    wire signed [17:0] clip_top_w =
        {{2{clip_top_q[15]}}, clip_top_q};
    wire signed [17:0] clip_right_w =
        {{2{clip_right_q[15]}}, clip_right_q};
    wire signed [17:0] clip_bottom_w =
        {{2{clip_bottom_q[15]}}, clip_bottom_q};
    wire signed [17:0] command_width_w = {2'd0, command_width_q};
    wire signed [17:0] command_height_w = {2'd0, command_height_q};
    wire signed [17:0] destination_surface_width_w =
        {2'd0, destination_surface_width_q};
    wire signed [17:0] destination_surface_height_w =
        {2'd0, destination_surface_height_q};
    wire signed [17:0] source_surface_width_w =
        {2'd0, source_surface_width_q};
    wire signed [17:0] source_surface_height_w =
        {2'd0, source_surface_height_q};

    reg signed [17:0] destination_left_edge_q;
    reg signed [17:0] destination_top_edge_q;
    reg signed [17:0] destination_right_edge_q;
    reg signed [17:0] destination_bottom_edge_q;
    reg signed [17:0] clip_left_edge_q;
    reg signed [17:0] clip_top_edge_q;
    reg signed [17:0] clip_right_edge_q;
    reg signed [17:0] clip_bottom_edge_q;
    reg signed [17:0] source_left_edge_q;
    reg signed [17:0] source_top_edge_q;
    reg signed [17:0] source_right_edge_q;
    reg signed [17:0] source_bottom_edge_q;
    reg signed [17:0] destination_left_limit_q;
    reg signed [17:0] destination_top_limit_q;
    reg signed [17:0] destination_right_limit_q;
    reg signed [17:0] destination_bottom_limit_q;
    reg signed [17:0] clipped_left_limit_q;
    reg signed [17:0] clipped_top_limit_q;
    reg signed [17:0] clipped_right_limit_q;
    reg signed [17:0] clipped_bottom_limit_q;
    reg signed [17:0] planned_left_offset_q;
    reg signed [17:0] planned_top_offset_q;
    reg signed [17:0] planned_right_offset_q;
    reg signed [17:0] planned_bottom_offset_q;

    reg [15:0] effective_width_q;
    reg [15:0] effective_height_q;
    reg [15:0] effective_destination_x_q;
    reg [15:0] effective_destination_y_q;
    reg [15:0] effective_source_x_q;
    reg [15:0] mapped_source_y_q;
    // Keep one registered Y operand beside each row-product DSP. A shared
    // register creates a die-spanning four-DSP fanout at 200 MHz.
    (* keep = "true" *) reg [15:0] source_row_low_y_q;
    (* keep = "true" *) reg [15:0] source_row_high_y_q;
    (* keep = "true" *) reg [15:0] auxiliary_row_low_y_q;
    (* keep = "true" *) reg [15:0] auxiliary_row_high_y_q;
    reg [15:0] rows_before_last_q;
    reg [15:0] columns_before_last_q;

    reg [31:0] destination_row_product_low_q;
    reg [31:0] destination_row_product_high_q;
    reg [31:0] source_row_product_low_q;
    reg [31:0] source_row_product_high_q;
    reg [31:0] auxiliary_row_product_low_q;
    reg [31:0] auxiliary_row_product_high_q;
    reg [47:0] destination_row_product_q;
    reg [47:0] source_row_product_q;
    reg [47:0] auxiliary_row_product_q;
    reg [47:0] destination_surface_base_q;
    reg [47:0] source_surface_base_q;
    reg [47:0] auxiliary_surface_base_q;
    reg [47:0] destination_row_base_q;
    reg [47:0] source_row_base_q;
    reg [47:0] auxiliary_row_base_q;
    reg [47:0] destination_first_row_address_q;
    reg [47:0] source_first_row_address_q;
    reg destination_prefix_greater_q;
    reg destination_prefix_equal_q;
    reg [31:0] destination_last_product_low_q;
    reg [31:0] destination_last_product_high_q;
    reg [31:0] source_last_product_low_q;
    reg [31:0] source_last_product_high_q;
    reg [47:0] destination_last_product_q;
    reg [47:0] source_last_product_q;
    reg reverse_q;
    reg [47:0] destination_row_address_q;
    reg [47:0] source_row_address_q;
    reg [31:0] destination_pixel_address_q;
    (* extract_enable = "no" *) reg [31:0] source_pixel_address_q;
    reg [15:0] columns_remaining_q;
    reg [15:0] rows_remaining_q;
    reg [17:0] endpoint_byte_offset_q;
    reg [17:0] source_x_byte_offset_q;

    reg source_cache_valid;
    reg [63:0] source_cache_data;
    reg [31:0] source_cache_address_q;
    reg destination_cache_valid;
    reg [63:0] destination_cache_data;
    reg [31:0] destination_cache_address_q;
    reg palette_cache_valid;
    reg [63:0] palette_cache_data;
    reg [31:0] palette_cache_address_q;
    reg [31:0] palette_pixel_address_q;
    reg mask_cache_valid;
    reg [63:0] mask_cache_data;
    reg [31:0] mask_cache_address_q;
    reg [31:0] mask_pixel_address_q;
    reg mask_pixel_enabled_q;
    reg source_arvalid;
    reg [31:0] source_araddr;
    reg [31:0] source_pixel_q;
    reg [31:0] source_pixel_address_load_q;
    reg source_address_starts_writer_q;
    reg [31:0] source_argb_q;
    reg source_key_matches_q;
    reg [31:0] destination_pixel_q;
    reg [31:0] destination_argb_q;
    reg [31:0] blend_source_argb_q;
    reg [31:0] blend_destination_argb_q;
    reg [31:0] blend_result_argb_q;
    reg [1:0] blend_channel_q;
    reg [7:0] blend_inverse_alpha_q;
    (* keep = "true", max_fanout = 1 *) reg [7:0] blend_multiplicand_q;
    (* keep = "true", max_fanout = 1 *) reg [7:0] blend_multiplier_q;
    reg blend_destination_phase_q;
    (* use_dsp = "yes" *) reg [15:0] blend_product_q;
    reg [16:0] blend_divide_adjusted_q;
    (* keep = "true", max_fanout = 1 *) reg [7:0] blend_divided_q;
    (* use_dsp = "no" *) wire [16:0] blend_divide_folded =
        blend_divide_adjusted_q + {9'd0, blend_divide_adjusted_q[16:8]};
    wire [7:0] blend_product_divided = blend_divide_folded[15:8];
    wire [31:0] required_source_beat =
        {source_pixel_address_q[31:3], 3'b000};
    wire [2:0] source_lane = source_pixel_address_q[2:0];
    wire [31:0] decoded_source_pixel =
        source_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
            {24'd0, beat_byte(source_cache_data, source_lane)} :
        source_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            {16'd0, beat_byte(source_cache_data, source_lane),
             beat_byte(source_cache_data, source_lane + 3'd1)} :
            {beat_byte(source_cache_data, source_lane),
             beat_byte(source_cache_data, source_lane + 3'd1),
             beat_byte(source_cache_data, source_lane + 3'd2),
             beat_byte(source_cache_data, source_lane + 3'd3)};
    wire [31:0] expanded_registered_source_argb =
        expand_source_argb(source_format_q, source_pixel_q);
    wire [31:0] converted_registered_source_pixel =
        source_format_q == `ASTRA_RENDER_FORMAT_INDEX8 &&
        destination_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
            source_pixel_q :
            pack_destination_pixel(destination_format_q,
                                   expanded_registered_source_argb);
    wire source_key_matches =
        source_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
            source_pixel_q[7:0] == options_q[7:0] :
        source_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            source_pixel_q[15:0] == options_q[15:0] :
            source_pixel_q[23:0] == options_q[23:0];
    wire [31:0] required_destination_beat =
        {destination_pixel_address_q[31:3], 3'b000};
    wire [2:0] destination_lane = destination_pixel_address_q[2:0];
    wire [31:0] decoded_destination_pixel =
        destination_format_q == `ASTRA_RENDER_FORMAT_INDEX8 ?
            {24'd0, beat_byte(destination_cache_data, destination_lane)} :
        destination_format_q == `ASTRA_RENDER_FORMAT_RGB565 ?
            {16'd0, beat_byte(destination_cache_data, destination_lane),
             beat_byte(destination_cache_data, destination_lane + 3'd1)} :
            {beat_byte(destination_cache_data, destination_lane),
             beat_byte(destination_cache_data, destination_lane + 3'd1),
             beat_byte(destination_cache_data, destination_lane + 3'd2),
             beat_byte(destination_cache_data, destination_lane + 3'd3)};
    wire [31:0] expanded_destination_argb =
        expand_source_argb(destination_format_q,
                           decoded_destination_pixel);
    wire [31:0] rop_pixel = normalize_destination_pixel(
        destination_format_q,
        apply_rop(command_flags_q[11:8], source_pixel_q,
                  destination_pixel_q));
    wire [31:0] required_palette_beat =
        {palette_pixel_address_q[31:3], 3'b000};
    wire [2:0] palette_lane = palette_pixel_address_q[2:0];
    wire [31:0] decoded_palette_argb = {
        beat_byte(palette_cache_data, palette_lane),
        beat_byte(palette_cache_data, palette_lane + 3'd1),
        beat_byte(palette_cache_data, palette_lane + 3'd2),
        beat_byte(palette_cache_data, palette_lane + 3'd3)
    };
    wire [31:0] converted_palette_pixel =
        pack_destination_pixel(destination_format_q,
                               decoded_palette_argb);

    wire scale_subtract = scale_shifted_q >=
                          {1'b0, scale_denominator_q};
    wire [16:0] scale_remainder_next = scale_subtract ?
        scale_shifted_q - {1'b0, scale_denominator_q} : scale_shifted_q;
    wire [39:0] scale_quotient_next = scale_quotient_q |
        (scale_subtract ? (40'd1 << scale_bit_q) : 40'd0);
    wire [55:0] phase_product_next = phase_product_q +
        (phase_multiplier_q[0] ? phase_multiplicand_q : 56'd0);
    wire [39:0] next_source_phase_x =
        source_phase_x_q + scale_step_x_q;
    wire [39:0] next_source_phase_y =
        source_phase_y_q + scale_step_y_q;
    wire [15:0] source_index_x = source_phase_x_start_q[39:24];
    wire [15:0] source_index_y = source_phase_y_q[39:24];
    wire [15:0] current_source_index_x = source_phase_x_q[39:24];
    wire [15:0] next_source_index_y = next_source_phase_y[39:24];
    wire [16:0] mapped_source_x_start = command_flags_q[0] ?
        {1'b0, source_x_q} + {1'b0, source_command_width_q} - 17'd1 -
            {1'b0, source_index_x} :
        {1'b0, source_x_q} + {1'b0, source_index_x};
    wire [16:0] mapped_source_y_start = command_flags_q[1] ?
        {1'b0, source_y_q} + {1'b0, source_command_height_q} - 17'd1 -
            {1'b0, source_index_y} :
        {1'b0, source_y_q} + {1'b0, source_index_y};
    wire [16:0] mapped_source_x_current = command_flags_q[0] ?
        {1'b0, source_x_q} + {1'b0, source_command_width_q} - 17'd1 -
            {1'b0, current_source_index_x} :
        {1'b0, source_x_q} + {1'b0, current_source_index_x};
    wire [16:0] mapped_source_y_next = command_flags_q[1] ?
        {1'b0, source_y_q} + {1'b0, source_command_height_q} - 17'd1 -
            {1'b0, next_source_index_y} :
        {1'b0, source_y_q} + {1'b0, next_source_index_y};
    wire [31:0] required_mask_beat =
        {mask_pixel_address_q[31:3], 3'b000};
    wire [2:0] mask_lane = mask_pixel_address_q[2:0];
    wire [7:0] decoded_mask_byte =
        beat_byte(mask_cache_data, mask_lane);
    wire mask_pixel_enabled =
        decoded_mask_byte[3'd7 - effective_source_x_q[2:0]];

    assign m_axi_arid = AXI_ID;
    assign m_axi_araddr = source_araddr;
    assign m_axi_arlen = 8'd0;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b0000;
    assign m_axi_arvalid = source_arvalid;
    assign m_axi_rready = state == ST_SOURCE_RESPONSE ||
                          state == ST_DESTINATION_RESPONSE ||
                          state == ST_PALETTE_RESPONSE ||
                          state == ST_MASK_RESPONSE;

    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            abort_pending <= 1'b0;
            is_fill_q <= 1'b0;
            is_blit_q <= 1'b0;
            direct_copy_q <= 1'b0;
            same_surface_q <= 1'b0;
            arena_base_q <= 32'd0;
            clip_left_q <= 16'sd0;
            clip_top_q <= 16'sd0;
            clip_right_q <= 16'sd0;
            clip_bottom_q <= 16'sd0;
            source_x_q <= 16'sd0;
            source_y_q <= 16'sd0;
            destination_x_q <= 16'sd0;
            destination_y_q <= 16'sd0;
            source_command_width_q <= 16'd0;
            source_command_height_q <= 16'd0;
            command_width_q <= 16'd0;
            command_height_q <= 16'd0;
            command_flags_q <= 16'd0;
            options_q <= 32'd0;
            blit_no_flags_q <= 1'b0;
            blit_same_format_q <= 1'b0;
            blit_same_dimensions_q <= 1'b0;
            plan_command_valid_q <= 1'b0;
            blit_dimensions_valid_q <= 1'b0;
            source_end_x_q <= 17'd0;
            source_end_y_q <= 17'd0;
            auxiliary_min_pitch_q <= 32'd0;
            blit_flag_contract_q <= 1'b0;
            blit_format_contract_q <= 1'b0;
            blit_palette_contract_q <= 1'b0;
            blit_auxiliary_contract_q <= 1'b0;
            blit_overlap_contract_q <= 1'b0;
            destination_data_offset_q <= 32'd0;
            destination_pitch_q <= 32'd0;
            destination_surface_width_q <= 16'd0;
            destination_surface_height_q <= 16'd0;
            destination_format_q <= 8'd0;
            destination_bpp_q <= 3'd0;
            destination_shift_q <= 2'd0;
            source_data_offset_q <= 32'd0;
            source_pitch_q <= 32'd0;
            source_surface_width_q <= 16'd0;
            source_surface_height_q <= 16'd0;
            source_format_q <= 8'd0;
            source_bpp_q <= 3'd0;
            source_shift_q <= 2'd0;
            source_palette_offset_q <= 32'd0;
            auxiliary_data_offset_q <= 32'd0;
            auxiliary_pitch_q <= 32'd0;
            auxiliary_surface_width_q <= 16'd0;
            auxiliary_surface_height_q <= 16'd0;
            scale_axis_q <= 1'b0;
            scale_dividend_q <= 40'd0;
            scale_denominator_q <= 16'd1;
            scale_remainder_q <= 17'd0;
            scale_quotient_q <= 40'd0;
            scale_result_q <= 40'd0;
            scale_bit_q <= 6'd0;
            scale_shifted_q <= 17'd0;
            scale_step_x_q <= 40'd0;
            scale_step_y_q <= 40'd0;
            phase_axis_q <= 1'b0;
            phase_multiplier_q <= 16'd0;
            phase_multiplicand_q <= 56'd0;
            phase_product_q <= 56'd0;
            phase_bit_q <= 5'd0;
            source_phase_x_start_q <= 40'd0;
            source_phase_x_q <= 40'd0;
            source_phase_y_q <= 40'd0;
            destination_left_edge_q <= 18'sd0;
            destination_top_edge_q <= 18'sd0;
            destination_right_edge_q <= 18'sd0;
            destination_bottom_edge_q <= 18'sd0;
            clip_left_edge_q <= 18'sd0;
            clip_top_edge_q <= 18'sd0;
            clip_right_edge_q <= 18'sd0;
            clip_bottom_edge_q <= 18'sd0;
            source_left_edge_q <= 18'sd0;
            source_top_edge_q <= 18'sd0;
            source_right_edge_q <= 18'sd0;
            source_bottom_edge_q <= 18'sd0;
            destination_left_limit_q <= 18'sd0;
            destination_top_limit_q <= 18'sd0;
            destination_right_limit_q <= 18'sd0;
            destination_bottom_limit_q <= 18'sd0;
            clipped_left_limit_q <= 18'sd0;
            clipped_top_limit_q <= 18'sd0;
            clipped_right_limit_q <= 18'sd0;
            clipped_bottom_limit_q <= 18'sd0;
            planned_left_offset_q <= 18'sd0;
            planned_top_offset_q <= 18'sd0;
            planned_right_offset_q <= 18'sd0;
            planned_bottom_offset_q <= 18'sd0;
            effective_width_q <= 16'd0;
            effective_height_q <= 16'd0;
            effective_destination_x_q <= 16'd0;
            effective_destination_y_q <= 16'd0;
            effective_source_x_q <= 16'd0;
            mapped_source_y_q <= 16'd0;
            source_row_low_y_q <= 16'd0;
            source_row_high_y_q <= 16'd0;
            auxiliary_row_low_y_q <= 16'd0;
            auxiliary_row_high_y_q <= 16'd0;
            rows_before_last_q <= 16'd0;
            columns_before_last_q <= 16'd0;
            destination_row_product_low_q <= 32'd0;
            destination_row_product_high_q <= 32'd0;
            source_row_product_low_q <= 32'd0;
            source_row_product_high_q <= 32'd0;
            auxiliary_row_product_low_q <= 32'd0;
            auxiliary_row_product_high_q <= 32'd0;
            destination_row_product_q <= 48'd0;
            source_row_product_q <= 48'd0;
            auxiliary_row_product_q <= 48'd0;
            destination_surface_base_q <= 48'd0;
            source_surface_base_q <= 48'd0;
            auxiliary_surface_base_q <= 48'd0;
            destination_row_base_q <= 48'd0;
            source_row_base_q <= 48'd0;
            auxiliary_row_base_q <= 48'd0;
            destination_first_row_address_q <= 48'd0;
            source_first_row_address_q <= 48'd0;
            destination_prefix_greater_q <= 1'b0;
            destination_prefix_equal_q <= 1'b0;
            destination_last_product_low_q <= 32'd0;
            destination_last_product_high_q <= 32'd0;
            source_last_product_low_q <= 32'd0;
            source_last_product_high_q <= 32'd0;
            destination_last_product_q <= 48'd0;
            source_last_product_q <= 48'd0;
            reverse_q <= 1'b0;
            destination_row_address_q <= 48'd0;
            source_row_address_q <= 48'd0;
            destination_pixel_address_q <= 32'd0;
            columns_remaining_q <= 16'd0;
            rows_remaining_q <= 16'd0;
            endpoint_byte_offset_q <= 18'd0;
            source_x_byte_offset_q <= 18'd0;
            source_cache_valid <= 1'b0;
            source_cache_data <= 64'd0;
            source_cache_address_q <= 32'd0;
            destination_cache_valid <= 1'b0;
            destination_cache_data <= 64'd0;
            destination_cache_address_q <= 32'd0;
            palette_cache_valid <= 1'b0;
            palette_cache_data <= 64'd0;
            palette_cache_address_q <= 32'd0;
            palette_pixel_address_q <= 32'd0;
            mask_cache_valid <= 1'b0;
            mask_cache_data <= 64'd0;
            mask_cache_address_q <= 32'd0;
            mask_pixel_address_q <= 32'd0;
            mask_pixel_enabled_q <= 1'b0;
            source_arvalid <= 1'b0;
            source_araddr <= 32'd0;
            source_pixel_q <= 32'd0;
            source_pixel_address_load_q <= 32'd0;
            source_address_starts_writer_q <= 1'b0;
            source_argb_q <= 32'd0;
            source_key_matches_q <= 1'b0;
            destination_pixel_q <= 32'd0;
            destination_argb_q <= 32'd0;
            blend_source_argb_q <= 32'd0;
            blend_destination_argb_q <= 32'd0;
            blend_result_argb_q <= 32'd0;
            blend_channel_q <= 2'd0;
            blend_inverse_alpha_q <= 8'd0;
            blend_multiplicand_q <= 8'd0;
            blend_multiplier_q <= 8'd0;
            blend_destination_phase_q <= 1'b0;
            blend_product_q <= 16'd0;
            blend_divide_adjusted_q <= 17'd0;
            blend_divided_q <= 8'd0;
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
        end else begin
            done <= 1'b0;
            writer_start <= 1'b0;
            writer_abort <= 1'b0;

            if (abort && busy)
                abort_pending <= 1'b1;

            case (state)
                ST_IDLE: begin
                    pixel_valid <= 1'b0;
                    writer_flush <= 1'b0;
                    source_arvalid <= 1'b0;
                    if (start) begin
                        is_fill_q <= is_fill;
                        is_blit_q <= is_blit;
                        same_surface_q <= same_surface;
                        arena_base_q <= arena_base;
                        clip_left_q <= clip_left;
                        clip_top_q <= clip_top;
                        clip_right_q <= clip_right;
                        clip_bottom_q <= clip_bottom;
                        source_x_q <= source_x;
                        source_y_q <= source_y;
                        destination_x_q <= destination_x;
                        destination_y_q <= destination_y;
                        source_command_width_q <= source_width;
                        source_command_height_q <= source_height;
                        command_width_q <= destination_width;
                        command_height_q <= destination_height;
                        command_flags_q <= command_flags;
                        options_q <= options;
                        blit_no_flags_q <= command_flags == 16'd0;
                        blit_same_format_q <=
                            source_format == destination_format;
                        blit_same_dimensions_q <=
                            source_width == destination_width &&
                            source_height == destination_height;
                        destination_data_offset_q <=
                            destination_data_offset;
                        destination_pitch_q <= destination_pitch;
                        destination_surface_width_q <=
                            destination_surface_width;
                        destination_surface_height_q <=
                            destination_surface_height;
                        destination_format_q <= destination_format;
                        destination_bpp_q <= destination_bytes_per_pixel;
                        destination_shift_q <=
                            destination_bytes_per_pixel == 3'd1 ? 2'd0 :
                            destination_bytes_per_pixel == 3'd2 ? 2'd1 : 2'd2;
                        source_data_offset_q <= source_data_offset;
                        source_pitch_q <= source_pitch;
                        source_surface_width_q <= source_surface_width;
                        source_surface_height_q <= source_surface_height;
                        source_format_q <= source_format;
                        source_bpp_q <= source_bytes_per_pixel;
                        source_shift_q <=
                            source_bytes_per_pixel == 3'd1 ? 2'd0 :
                            source_bytes_per_pixel == 3'd2 ? 2'd1 : 2'd2;
                        source_palette_offset_q <= source_palette_offset;
                        auxiliary_data_offset_q <= auxiliary_data_offset;
                        auxiliary_pitch_q <= auxiliary_pitch;
                        auxiliary_surface_width_q <=
                            auxiliary_surface_width;
                        auxiliary_surface_height_q <=
                            auxiliary_surface_height;
                        source_cache_valid <= 1'b0;
                        destination_cache_valid <= 1'b0;
                        palette_cache_valid <= 1'b0;
                        mask_cache_valid <= 1'b0;
                        abort_pending <= 1'b0;
                        status <= `ASTRA_RENDER_STATUS_OK;
                        fault_detail <= 32'd0;
                        completed_pixels <= 32'd0;
                        busy <= 1'b1;
                        state <= ST_PLAN;
                    end
                end

                ST_PLAN: begin
                    plan_command_valid_q <= (is_fill_q || is_blit_q) &&
                        command_width_q != 16'd0 &&
                        command_height_q != 16'd0;
                    direct_copy_q <= is_blit_q && blit_no_flags_q &&
                        blit_same_format_q && blit_same_dimensions_q;
                    state <= ST_PLAN_COMMAND_VALIDATE;
                end

                ST_PLAN_COMMAND_VALIDATE: begin
                    if (!plan_command_valid_q) begin
                        status <= `ASTRA_RENDER_STATUS_UNSUPPORTED;
                        fault_detail <= 32'h00010000;
                        state <= ST_FINISH;
                    end else if (is_blit_q) begin
                        source_end_x_q <= {1'b0, source_x_q} +
                            {1'b0, source_command_width_q};
                        source_end_y_q <= {1'b0, source_y_q} +
                            {1'b0, source_command_height_q};
                        auxiliary_min_pitch_q <=
                            ({16'd0, auxiliary_surface_width_q} + 32'd7) >> 3;
                        blit_dimensions_valid_q <= !(
                            source_command_width_q == 16'd0 ||
                            source_command_height_q == 16'd0 ||
                            source_x_q[15] || source_y_q[15]);
                        state <= ST_PLAN_BLIT_DIMENSIONS;
                    end else begin
                        state <= ST_PLAN_CANDIDATES;
                    end
                end

                ST_PLAN_BLIT_DIMENSIONS: begin
                    blit_dimensions_valid_q <= blit_dimensions_valid_q &&
                        source_end_x_q <= {1'b0, source_surface_width_q} &&
                        source_end_y_q <= {1'b0, source_surface_height_q};
                    state <= ST_PLAN_BLIT_SOURCE_VALIDATE;
                end

                ST_PLAN_BLIT_SOURCE_VALIDATE: begin
                    if (!blit_dimensions_valid_q) begin
                        status <= `ASTRA_RENDER_STATUS_BAD_RANGE;
                        fault_detail <= 32'h00010001;
                        state <= ST_FINISH;
                    end else if (command_flags_q[3]) begin
                        state <= ST_PLAN_BLIT_MASK_DIMENSIONS;
                    end else begin
                        state <= ST_PLAN_BLIT_FORMAT;
                    end
                end

                ST_PLAN_BLIT_MASK_DIMENSIONS: begin
                    blit_dimensions_valid_q <=
                        source_end_x_q <=
                            {1'b0, auxiliary_surface_width_q} &&
                        source_end_y_q <=
                            {1'b0, auxiliary_surface_height_q} &&
                        auxiliary_pitch_q >= auxiliary_min_pitch_q;
                    state <= ST_PLAN_BLIT_MASK_PITCH;
                end

                ST_PLAN_BLIT_MASK_PITCH: begin
                    if (!blit_dimensions_valid_q) begin
                        status <= `ASTRA_RENDER_STATUS_BAD_RANGE;
                        fault_detail <= 32'h00010001;
                        state <= ST_FINISH;
                    end else begin
                        state <= ST_PLAN_BLIT_FORMAT;
                    end
                end

                ST_PLAN_BLIT_FORMAT: begin
                    blit_flag_contract_q <=
                        !(|(command_flags_q &
                            ~(`ASTRA_RENDER_FLAG_BLIT_REFLECT_X |
                              `ASTRA_RENDER_FLAG_BLIT_REFLECT_Y |
                              `ASTRA_RENDER_FLAG_BLIT_SOURCE_KEY |
                              `ASTRA_RENDER_FLAG_BLIT_MASK1 |
                              `ASTRA_RENDER_FLAG_BLIT_ALPHA |
                              `ASTRA_RENDER_FLAG_BLIT_PALETTE |
                              `ASTRA_RENDER_FLAG_BLIT_ROP_ENABLE |
                              `ASTRA_RENDER_FLAG_BLIT_ROP_MASK))) &&
                        (command_flags_q[6] ||
                         command_flags_q[11:8] == 4'd0) &&
                        !(command_flags_q[6] && command_flags_q[4]) &&
                        (command_flags_q[2] ||
                         options_q[23:0] == 24'd0) &&
                        (command_flags_q[4] ||
                         options_q[31:24] == 8'd0);
                    blit_format_contract_q <=
                        source_format_q <=
                            `ASTRA_RENDER_FORMAT_ARGB8888 &&
                        destination_format_q <=
                            `ASTRA_RENDER_FORMAT_ARGB8888 &&
                        ((!command_flags_q[5] &&
                          source_format_q == `ASTRA_RENDER_FORMAT_INDEX8 &&
                          destination_format_q ==
                            `ASTRA_RENDER_FORMAT_INDEX8) ||
                         (command_flags_q[5] &&
                          source_format_q == `ASTRA_RENDER_FORMAT_INDEX8 &&
                          destination_format_q !=
                            `ASTRA_RENDER_FORMAT_INDEX8) ||
                         (!command_flags_q[5] &&
                          source_format_q != `ASTRA_RENDER_FORMAT_INDEX8 &&
                          destination_format_q !=
                            `ASTRA_RENDER_FORMAT_INDEX8)) &&
                        (!command_flags_q[4] ||
                         destination_format_q !=
                            `ASTRA_RENDER_FORMAT_INDEX8);
                    blit_palette_contract_q <=
                        command_flags_q[5] ?
                            (source_palette_offset_q != 32'd0 &&
                             source_palette_offset_q[5:0] == 6'd0) :
                            source_palette_offset_q == 32'd0;
                    blit_auxiliary_contract_q <=
                        command_flags_q[3] ||
                         (auxiliary_data_offset_q == 32'd0 &&
                          auxiliary_pitch_q == 32'd0 &&
                          auxiliary_surface_width_q == 16'd0 &&
                          auxiliary_surface_height_q == 16'd0);
                    blit_overlap_contract_q <=
                        !same_surface_q ||
                         (blit_same_format_q &&
                          source_bpp_q == destination_bpp_q &&
                          blit_same_dimensions_q &&
                          blit_no_flags_q);
                    state <= ST_PLAN_BLIT_FORMAT_DECIDE;
                end

                ST_PLAN_BLIT_FORMAT_DECIDE: begin
                    if (!(blit_flag_contract_q &&
                          blit_format_contract_q &&
                          blit_palette_contract_q &&
                          blit_auxiliary_contract_q &&
                          blit_overlap_contract_q)) begin
                        status <= `ASTRA_RENDER_STATUS_UNSUPPORTED;
                        fault_detail <= 32'h00010000;
                        state <= ST_FINISH;
                    end else begin
                        state <= ST_PLAN_CANDIDATES;
                    end
                end

                ST_PLAN_CANDIDATES: begin
                    destination_left_edge_q <= -destination_x_w;
                    destination_top_edge_q <= -destination_y_w;
                    destination_right_edge_q <=
                        destination_surface_width_w - destination_x_w;
                    destination_bottom_edge_q <=
                        destination_surface_height_w - destination_y_w;
                    clip_left_edge_q <= clip_left_w - destination_x_w;
                    clip_top_edge_q <= clip_top_w - destination_y_w;
                    clip_right_edge_q <= clip_right_w - destination_x_w;
                    clip_bottom_edge_q <= clip_bottom_w - destination_y_w;
                    source_left_edge_q <= -source_x_w;
                    source_top_edge_q <= -source_y_w;
                    source_right_edge_q <= source_surface_width_w - source_x_w;
                    source_bottom_edge_q <=
                        source_surface_height_w - source_y_w;
                    state <= ST_PLAN_DEST_LIMIT;
                end

                ST_PLAN_DEST_LIMIT: begin
                    destination_left_limit_q <= signed_max18(
                        18'sd0, destination_left_edge_q);
                    destination_top_limit_q <= signed_max18(
                        18'sd0, destination_top_edge_q);
                    destination_right_limit_q <= signed_min18(
                        command_width_w, destination_right_edge_q);
                    destination_bottom_limit_q <= signed_min18(
                        command_height_w, destination_bottom_edge_q);
                    state <= ST_PLAN_CLIP_LIMIT;
                end

                ST_PLAN_CLIP_LIMIT: begin
                    clipped_left_limit_q <= signed_max18(
                        destination_left_limit_q, clip_left_edge_q);
                    clipped_top_limit_q <= signed_max18(
                        destination_top_limit_q, clip_top_edge_q);
                    clipped_right_limit_q <= signed_min18(
                        destination_right_limit_q, clip_right_edge_q);
                    clipped_bottom_limit_q <= signed_min18(
                        destination_bottom_limit_q, clip_bottom_edge_q);
                    state <= ST_PLAN_SOURCE_LIMIT;
                end

                ST_PLAN_SOURCE_LIMIT: begin
                    planned_left_offset_q <= clipped_left_limit_q;
                    planned_top_offset_q <= clipped_top_limit_q;
                    planned_right_offset_q <= clipped_right_limit_q;
                    planned_bottom_offset_q <= clipped_bottom_limit_q;
                    state <= ST_PLAN_VALIDATE;
                end

                ST_PLAN_VALIDATE: begin
                    if (planned_left_offset_q >= planned_right_offset_q ||
                        planned_top_offset_q >= planned_bottom_offset_q) begin
                        state <= ST_FINISH;
                    end else begin
                        state <= ST_PLAN_COORDINATES;
                    end
                end

                ST_PLAN_COORDINATES: begin
                    effective_width_q <= planned_right_offset_q -
                                         planned_left_offset_q;
                    effective_height_q <= planned_bottom_offset_q -
                                          planned_top_offset_q;
                    effective_destination_x_q <= destination_x_w +
                                                 planned_left_offset_q;
                    effective_destination_y_q <= destination_y_w +
                                                 planned_top_offset_q;
                    if (is_blit_q)
                        state <= ST_SCALE_X_LOAD;
                    else
                        state <= ST_PLAN_COUNTS;
                end

                ST_SCALE_X_LOAD: begin
                    scale_axis_q <= 1'b0;
                    scale_dividend_q <= {source_command_width_q, 24'd0};
                    scale_denominator_q <= command_width_q;
                    scale_remainder_q <= 17'd0;
                    scale_quotient_q <= 40'd0;
                    scale_bit_q <= 6'd39;
                    state <= ST_SCALE_DIV_SHIFT;
                end

                ST_SCALE_DIV_SHIFT: begin
                    scale_shifted_q <= {
                        scale_remainder_q[15:0],
                        scale_dividend_q[scale_bit_q]
                    };
                    state <= ST_SCALE_DIVIDE;
                end

                ST_SCALE_DIVIDE: begin
                    scale_remainder_q <= scale_remainder_next;
                    scale_quotient_q <= scale_quotient_next;
                    if (scale_bit_q == 6'd0)
                        state <= ST_SCALE_DIV_ROUND;
                    else begin
                        scale_bit_q <= scale_bit_q - 6'd1;
                        state <= ST_SCALE_DIV_SHIFT;
                    end
                end

                ST_SCALE_DIV_ROUND: begin
                    scale_result_q <= scale_quotient_q +
                        (scale_remainder_q != 17'd0);
                    state <= ST_SCALE_STORE;
                end

                ST_SCALE_STORE: begin
                    if (!scale_axis_q) begin
                        scale_step_x_q <= scale_result_q;
                        scale_axis_q <= 1'b1;
                        scale_dividend_q <=
                            {source_command_height_q, 24'd0};
                        scale_denominator_q <= command_height_q;
                        scale_remainder_q <= 17'd0;
                        scale_quotient_q <= 40'd0;
                        scale_bit_q <= 6'd39;
                        state <= ST_SCALE_DIV_SHIFT;
                    end else begin
                        scale_step_y_q <= scale_result_q;
                        state <= ST_PHASE_LOAD;
                    end
                end

                ST_PHASE_LOAD: begin
                    phase_axis_q <= 1'b0;
                    phase_multiplier_q <= planned_left_offset_q[15:0];
                    phase_multiplicand_q <= {16'd0, scale_step_x_q};
                    phase_product_q <= 56'd0;
                    phase_bit_q <= 5'd0;
                    state <= ST_PHASE_MULTIPLY;
                end

                ST_PHASE_MULTIPLY: begin
                    phase_product_q <= phase_product_next;
                    phase_multiplier_q <= {1'b0, phase_multiplier_q[15:1]};
                    phase_multiplicand_q <=
                        {phase_multiplicand_q[54:0], 1'b0};
                    if (phase_bit_q == 5'd15)
                        state <= ST_PHASE_STORE;
                    else
                        phase_bit_q <= phase_bit_q + 5'd1;
                end

                ST_PHASE_STORE: begin
                    if (!phase_axis_q) begin
                        source_phase_x_start_q <= phase_product_q[39:0];
                        source_phase_x_q <= phase_product_q[39:0];
                        phase_axis_q <= 1'b1;
                        phase_multiplier_q <= planned_top_offset_q[15:0];
                        phase_multiplicand_q <= {16'd0, scale_step_y_q};
                        phase_product_q <= 56'd0;
                        phase_bit_q <= 5'd0;
                        state <= ST_PHASE_MULTIPLY;
                    end else begin
                        source_phase_y_q <= phase_product_q[39:0];
                        state <= ST_SOURCE_COORDINATES;
                    end
                end

                ST_SOURCE_COORDINATES: begin
                    effective_source_x_q <= mapped_source_x_start[15:0];
                    mapped_source_y_q <= mapped_source_y_start[15:0];
                    state <= ST_PLAN_COUNTS;
                end

                ST_PLAN_COUNTS: begin
                    rows_before_last_q <= effective_height_q - 16'd1;
                    columns_before_last_q <= effective_width_q - 16'd1;
                    source_row_low_y_q <= mapped_source_y_q;
                    source_row_high_y_q <= mapped_source_y_q;
                    auxiliary_row_low_y_q <= mapped_source_y_q;
                    auxiliary_row_high_y_q <= mapped_source_y_q;
                    state <= ST_ROW_MULTIPLY;
                end

                ST_ROW_MULTIPLY: begin
                    destination_row_product_low_q <=
                        destination_pitch_q[15:0] *
                        effective_destination_y_q;
                    destination_row_product_high_q <=
                        destination_pitch_q[31:16] *
                        effective_destination_y_q;
                    source_row_product_low_q <=
                        source_pitch_q[15:0] * source_row_low_y_q;
                    source_row_product_high_q <=
                        source_pitch_q[31:16] * source_row_high_y_q;
                    auxiliary_row_product_low_q <=
                        auxiliary_pitch_q[15:0] * auxiliary_row_low_y_q;
                    auxiliary_row_product_high_q <=
                        auxiliary_pitch_q[31:16] * auxiliary_row_high_y_q;
                    state <= ST_ROW_COMBINE;
                end

                ST_ROW_COMBINE: begin
                    destination_row_product_q <=
                        {destination_row_product_high_q, 16'd0} +
                        {16'd0, destination_row_product_low_q};
                    source_row_product_q <=
                        {source_row_product_high_q, 16'd0} +
                        {16'd0, source_row_product_low_q};
                    auxiliary_row_product_q <=
                        {auxiliary_row_product_high_q, 16'd0} +
                        {16'd0, auxiliary_row_product_low_q};
                    state <= ST_FIRST_ADDRESS;
                end

                ST_FIRST_ADDRESS: begin
                    destination_surface_base_q <=
                        {16'd0, arena_base_q} +
                        {16'd0, destination_data_offset_q};
                    source_surface_base_q <=
                        {16'd0, arena_base_q} +
                        {16'd0, source_data_offset_q};
                    auxiliary_surface_base_q <=
                        {16'd0, arena_base_q} +
                        {16'd0, auxiliary_data_offset_q};
                    state <= ST_FIRST_ADDRESS_ROW;
                end

                ST_FIRST_ADDRESS_ROW: begin
                    destination_row_base_q <=
                        destination_surface_base_q +
                        destination_row_product_q;
                    source_row_base_q <= source_surface_base_q +
                        source_row_product_q;
                    auxiliary_row_base_q <= auxiliary_surface_base_q +
                        auxiliary_row_product_q;
                    state <= ST_FIRST_ADDRESS_X;
                end

                ST_FIRST_ADDRESS_X: begin
                    destination_first_row_address_q <=
                        destination_row_base_q +
                        ({32'd0, effective_destination_x_q} <<
                         destination_shift_q);
                    source_first_row_address_q <= source_row_base_q +
                        ({32'd0, effective_source_x_q} <<
                         source_shift_q);
                    state <= ST_LAST_MULTIPLY;
                end

                ST_LAST_MULTIPLY: begin
                    destination_prefix_greater_q <=
                        destination_first_row_address_q[47:12] >
                        source_first_row_address_q[47:12];
                    destination_prefix_equal_q <=
                        destination_first_row_address_q[47:12] ==
                        source_first_row_address_q[47:12];
                    destination_last_product_low_q <=
                        destination_pitch_q[15:0] * rows_before_last_q;
                    destination_last_product_high_q <=
                        destination_pitch_q[31:16] * rows_before_last_q;
                    source_last_product_low_q <=
                        source_pitch_q[15:0] * rows_before_last_q;
                    source_last_product_high_q <=
                        source_pitch_q[31:16] * rows_before_last_q;
                    state <= ST_LAST_COMBINE;
                end

                ST_LAST_COMBINE: begin
                    destination_last_product_q <=
                        {destination_last_product_high_q, 16'd0} +
                        {16'd0, destination_last_product_low_q};
                    source_last_product_q <=
                        {source_last_product_high_q, 16'd0} +
                        {16'd0, source_last_product_low_q};
                    reverse_q <= is_blit_q && same_surface_q &&
                        (destination_prefix_greater_q ||
                         (destination_prefix_equal_q &&
                          destination_first_row_address_q[11:0] >
                          source_first_row_address_q[11:0]));
                    endpoint_byte_offset_q <=
                        {2'd0, columns_before_last_q} <<
                        destination_shift_q;
                    state <= ST_EXECUTION_ADDRESS;
                end

                ST_EXECUTION_ADDRESS: begin
                    destination_row_address_q <=
                        destination_first_row_address_q +
                        (reverse_q ? destination_last_product_q : 48'd0);
                    source_row_address_q <= source_first_row_address_q +
                        (reverse_q ? source_last_product_q : 48'd0);
                    state <= ST_EXECUTION_PIXEL;
                end

                ST_EXECUTION_PIXEL: begin
                    destination_pixel_address_q <=
                        destination_row_address_q[31:0] +
                        (reverse_q ? {14'd0, endpoint_byte_offset_q} :
                         32'd0);
                    source_pixel_address_load_q <=
                        source_row_address_q[31:0] +
                        (reverse_q ? {14'd0, endpoint_byte_offset_q} :
                         32'd0);
                    source_address_starts_writer_q <= 1'b1;
                    columns_remaining_q <= effective_width_q;
                    rows_remaining_q <= effective_height_q;
                    state <= ST_SOURCE_ADDRESS_COMMIT;
                end

                ST_WRITER_START: begin
                    writer_start <= 1'b1;
                    if (abort_pending)
                        state <= ST_ABORT_WRITER;
                    else if (is_fill_q)
                        state <= ST_PIXEL;
                    else
                        state <= ST_SOURCE;
                end

                ST_SOURCE: begin
                    source_araddr <= required_source_beat;
                    source_pixel_q <= decoded_source_pixel;
                    if (abort_pending) begin
                        state <= ST_ABORT_WRITER;
                    end else if (source_cache_valid &&
                                 source_cache_address_q ==
                                 required_source_beat) begin
                        if (direct_copy_q) begin
                            state <= ST_PIXEL;
                        end else begin
                            state <= ST_SOURCE_DECODE;
                        end
                    end else begin
                        state <= ST_SOURCE_REQUEST;
                    end
                end

                ST_SOURCE_DECODE: begin
                    source_argb_q <= expanded_registered_source_argb;
                    source_pixel_q <= converted_registered_source_pixel;
                    if (command_flags_q[5])
                        palette_pixel_address_q <= arena_base_q +
                            source_palette_offset_q +
                            ({24'd0, source_pixel_q[7:0]} << 2);
                    if (command_flags_q[3])
                        mask_pixel_address_q <=
                            auxiliary_row_base_q[31:0] +
                            {19'd0, effective_source_x_q[15:3]};
                    source_key_matches_q <= source_key_matches;
                    state <= ST_SOURCE_DISPATCH;
                end

                ST_SOURCE_DISPATCH: begin
                    if (command_flags_q[2] && source_key_matches_q) begin
                        state <= ST_SKIP_PIXEL;
                    end else if (command_flags_q[3]) begin
                        state <= ST_MASK;
                    end else if (command_flags_q[5]) begin
                        state <= ST_PALETTE;
                    end else if (command_flags_q[6] ||
                                 command_flags_q[4]) begin
                        state <= ST_DESTINATION;
                    end else begin
                        state <= ST_PIXEL;
                    end
                end

                ST_SOURCE_REQUEST: begin
                    if (!source_arvalid) begin
                        source_arvalid <= 1'b1;
                    end else if (m_axi_arready) begin
                        source_arvalid <= 1'b0;
                        state <= ST_SOURCE_RESPONSE;
                    end
                end

                ST_SOURCE_RESPONSE: begin
                    if (m_axi_rvalid) begin
                        if (m_axi_rid != AXI_ID ||
                            m_axi_rresp != 2'b00 || !m_axi_rlast) begin
                            status <= `ASTRA_RENDER_STATUS_AXI_READ;
                            fault_detail <= {16'h0002,
                                {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_rid},
                                5'd0, m_axi_rlast, m_axi_rresp};
                            state <= ST_ABORT_WRITER;
                        end else begin
                            source_cache_valid <= 1'b1;
                            source_cache_data <= m_axi_rdata;
                            source_cache_address_q <= source_araddr;
                            state <= ST_SOURCE;
                        end
                    end
                end

                ST_MASK: begin
                    source_araddr <= required_mask_beat;
                    if (abort_pending) begin
                        state <= ST_ABORT_WRITER;
                    end else if (mask_cache_valid &&
                                 mask_cache_address_q ==
                                 required_mask_beat) begin
                        state <= ST_MASK_DECODE;
                    end else begin
                        source_arvalid <= 1'b1;
                        state <= ST_MASK_REQUEST;
                    end
                end

                ST_MASK_DECODE: begin
                    mask_pixel_enabled_q <= mask_pixel_enabled;
                    state <= ST_MASK_DISPATCH;
                end

                ST_MASK_DISPATCH: begin
                    if (!mask_pixel_enabled_q) begin
                        state <= ST_SKIP_PIXEL;
                    end else if (command_flags_q[5]) begin
                        state <= ST_PALETTE;
                    end else if (command_flags_q[6] ||
                                 command_flags_q[4]) begin
                        state <= ST_DESTINATION;
                    end else begin
                        state <= ST_PIXEL;
                    end
                end

                ST_MASK_REQUEST: begin
                    if (source_arvalid && m_axi_arready) begin
                        source_arvalid <= 1'b0;
                        state <= ST_MASK_RESPONSE;
                    end
                end

                ST_MASK_RESPONSE: begin
                    if (m_axi_rvalid) begin
                        if (m_axi_rid != AXI_ID ||
                            m_axi_rresp != 2'b00 || !m_axi_rlast) begin
                            status <= `ASTRA_RENDER_STATUS_AXI_READ;
                            fault_detail <= {16'h0005,
                                {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_rid},
                                5'd0, m_axi_rlast, m_axi_rresp};
                            state <= ST_ABORT_WRITER;
                        end else begin
                            mask_cache_valid <= 1'b1;
                            mask_cache_data <= m_axi_rdata;
                            mask_cache_address_q <= source_araddr;
                            state <= ST_MASK;
                        end
                    end
                end

                ST_PALETTE: begin
                    source_araddr <= required_palette_beat;
                    if (abort_pending) begin
                        state <= ST_ABORT_WRITER;
                    end else if (palette_cache_valid &&
                                 palette_cache_address_q ==
                                 required_palette_beat) begin
                        state <= ST_PALETTE_DECODE;
                    end else begin
                        state <= ST_PALETTE_ISSUE;
                    end
                end

                ST_PALETTE_ISSUE: begin
                    if (abort_pending) begin
                        state <= ST_ABORT_WRITER;
                    end else begin
                        source_arvalid <= 1'b1;
                        state <= ST_PALETTE_REQUEST;
                    end
                end

                ST_PALETTE_DECODE: begin
                    source_pixel_q <= converted_palette_pixel;
                    source_argb_q <= decoded_palette_argb;
                    if (command_flags_q[6] || command_flags_q[4])
                        state <= ST_DESTINATION;
                    else
                        state <= ST_PALETTE_OUTPUT;
                end

                ST_PALETTE_OUTPUT: begin
                    state <= ST_PIXEL;
                end

                ST_PALETTE_REQUEST: begin
                    if (source_arvalid && m_axi_arready) begin
                        source_arvalid <= 1'b0;
                        state <= ST_PALETTE_RESPONSE;
                    end
                end

                ST_PALETTE_RESPONSE: begin
                    if (m_axi_rvalid) begin
                        if (m_axi_rid != AXI_ID ||
                            m_axi_rresp != 2'b00 || !m_axi_rlast) begin
                            status <= `ASTRA_RENDER_STATUS_AXI_READ;
                            fault_detail <= {16'h0004,
                                {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_rid},
                                5'd0, m_axi_rlast, m_axi_rresp};
                            state <= ST_ABORT_WRITER;
                        end else begin
                            palette_cache_valid <= 1'b1;
                            palette_cache_data <= m_axi_rdata;
                            palette_cache_address_q <= source_araddr;
                            state <= ST_PALETTE;
                        end
                    end
                end

                ST_DESTINATION: begin
                    source_araddr <= required_destination_beat;
                    destination_pixel_q <= decoded_destination_pixel;
                    destination_argb_q <= expanded_destination_argb;
                    if (abort_pending) begin
                        state <= ST_ABORT_WRITER;
                    end else if (destination_cache_valid &&
                                 destination_cache_address_q ==
                                 required_destination_beat) begin
                        state <= ST_COMPOSITE;
                    end else begin
                        source_arvalid <= 1'b1;
                        state <= ST_DESTINATION_REQUEST;
                    end
                end

                ST_DESTINATION_REQUEST: begin
                    if (source_arvalid && m_axi_arready) begin
                        source_arvalid <= 1'b0;
                        state <= ST_DESTINATION_RESPONSE;
                    end
                end

                ST_DESTINATION_RESPONSE: begin
                    if (m_axi_rvalid) begin
                        if (m_axi_rid != AXI_ID ||
                            m_axi_rresp != 2'b00 || !m_axi_rlast) begin
                            status <= `ASTRA_RENDER_STATUS_AXI_READ;
                            fault_detail <= {16'h0003,
                                {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_rid},
                                5'd0, m_axi_rlast, m_axi_rresp};
                            state <= ST_ABORT_WRITER;
                        end else begin
                            destination_cache_valid <= 1'b1;
                            destination_cache_data <= m_axi_rdata;
                            destination_cache_address_q <= source_araddr;
                            state <= ST_DESTINATION;
                        end
                    end
                end

                ST_COMPOSITE: begin
                    if (command_flags_q[6]) begin
                        source_pixel_q <= rop_pixel;
                        state <= ST_PIXEL;
                    end else begin
                        blend_source_argb_q <= source_argb_q;
                        blend_destination_argb_q <= destination_argb_q;
                        blend_result_argb_q <= 32'd0;
                        blend_channel_q <= 2'd0;
                        blend_destination_phase_q <= 1'b0;
                        state <= ST_BLEND_SOURCE_LOAD;
                    end
                end

                ST_BLEND_SOURCE_LOAD: begin
                    blend_multiplicand_q <=
                        argb_channel(blend_source_argb_q,
                                     blend_channel_q);
                    blend_multiplier_q <= options_q[31:24];
                    state <= ST_BLEND_MULTIPLY;
                end

                ST_BLEND_MULTIPLY: begin
                    blend_product_q <=
                        blend_multiplicand_q * blend_multiplier_q;
                    state <= ST_BLEND_DESTINATION_DIVIDE;
                end

                ST_BLEND_SOURCE_STORE: begin
                    case (blend_channel_q)
                        2'd0: blend_result_argb_q[31:24] <=
                            blend_divided_q;
                        2'd1: blend_result_argb_q[23:16] <=
                            blend_divided_q;
                        2'd2: blend_result_argb_q[15:8] <=
                            blend_divided_q;
                        default: blend_result_argb_q[7:0] <=
                            blend_divided_q;
                    endcase
                    if (blend_channel_q == 2'd3) begin
                        blend_inverse_alpha_q <= 8'd255 -
                            blend_result_argb_q[31:24];
                        blend_channel_q <= 2'd0;
                        blend_destination_phase_q <= 1'b1;
                        state <= ST_BLEND_DESTINATION_LOAD;
                    end else begin
                        blend_channel_q <= blend_channel_q + 2'd1;
                        state <= ST_BLEND_SOURCE_LOAD;
                    end
                end

                ST_BLEND_DESTINATION_LOAD: begin
                    blend_multiplicand_q <=
                        argb_channel(blend_destination_argb_q,
                                     blend_channel_q);
                    blend_multiplier_q <= blend_inverse_alpha_q;
                    state <= ST_BLEND_MULTIPLY;
                end

                ST_BLEND_DESTINATION_DIVIDE: begin
                    blend_divide_adjusted_q <=
                        {1'b0, blend_product_q} + 17'd128;
                    state <= ST_BLEND_DIVIDE_FINISH;
                end

                ST_BLEND_DIVIDE_FINISH: begin
                    blend_divided_q <= blend_product_divided;
                    state <= blend_destination_phase_q ?
                        ST_BLEND_DESTINATION_STORE :
                        ST_BLEND_SOURCE_STORE;
                end

                ST_BLEND_DESTINATION_STORE: begin
                    case (blend_channel_q)
                        2'd0: blend_result_argb_q[31:24] <=
                            saturate_channel(
                                {1'b0, blend_result_argb_q[31:24]} +
                                {1'b0, blend_divided_q});
                        2'd1: blend_result_argb_q[23:16] <=
                            saturate_channel(
                                {1'b0, blend_result_argb_q[23:16]} +
                                {1'b0, blend_divided_q});
                        2'd2: blend_result_argb_q[15:8] <=
                            saturate_channel(
                                {1'b0, blend_result_argb_q[15:8]} +
                                {1'b0, blend_divided_q});
                        default: blend_result_argb_q[7:0] <=
                            saturate_channel(
                                {1'b0, blend_result_argb_q[7:0]} +
                                {1'b0, blend_divided_q});
                    endcase
                    if (blend_channel_q == 2'd3) begin
                        state <= ST_BLEND_PACK;
                    end else begin
                        blend_channel_q <= blend_channel_q + 2'd1;
                        state <= ST_BLEND_DESTINATION_LOAD;
                    end
                end

                ST_BLEND_PACK: begin
                    source_pixel_q <= pack_destination_pixel(
                        destination_format_q, blend_result_argb_q);
                    state <= ST_BLEND_OUTPUT;
                end

                ST_BLEND_OUTPUT: begin
                    state <= ST_PIXEL;
                end

                ST_SKIP_PIXEL: begin
                    state <= ST_NEXT_PIXEL;
                end

                ST_PIXEL: begin
                    if (abort_pending) begin
                        pixel_valid <= 1'b0;
                        state <= ST_ABORT_WRITER;
                    end else begin
                        pixel_valid <= 1'b1;
                        pixel_address <= destination_pixel_address_q;
                        pixel_format <= destination_format_q;
                        if (is_fill_q)
                            pixel_value <= options_q;
                        else
                            pixel_value <= source_pixel_q;
                        if (pixel_valid && pixel_ready) begin
                            pixel_valid <= 1'b0;
                            completed_pixels <= completed_pixels + 32'd1;
                            state <= ST_NEXT_PIXEL;
                        end
                    end
                end

                ST_NEXT_PIXEL: begin
                    if (columns_remaining_q > 16'd1) begin
                        columns_remaining_q <= columns_remaining_q - 16'd1;
                        if (reverse_q) begin
                            destination_pixel_address_q <=
                                destination_pixel_address_q -
                                destination_bpp_q;
                            source_pixel_address_q <= source_pixel_address_q -
                                                      source_bpp_q;
                            state <= ST_SOURCE;
                        end else begin
                            destination_pixel_address_q <=
                                destination_pixel_address_q +
                                destination_bpp_q;
                            if (direct_copy_q) begin
                                source_pixel_address_q <=
                                    source_pixel_address_q + source_bpp_q;
                                state <= ST_SOURCE;
                            end else if (is_blit_q) begin
                                source_phase_x_q <= next_source_phase_x;
                                state <= ST_NEXT_SOURCE_MAP;
                            end else begin
                                state <= ST_PIXEL;
                            end
                        end
                    end else if (rows_remaining_q > 16'd1) begin
                        state <= ST_NEXT_ROW_ADDRESS;
                    end else begin
                        state <= ST_FLUSH;
                    end
                end

                ST_NEXT_SOURCE_MAP: begin
                    effective_source_x_q <=
                        mapped_source_x_current[15:0];
                    state <= ST_NEXT_SOURCE_OFFSET;
                end

                ST_NEXT_SOURCE_OFFSET: begin
                    source_x_byte_offset_q <=
                        {2'd0, effective_source_x_q} << source_shift_q;
                    state <= ST_NEXT_SOURCE_ADDRESS;
                end

                ST_NEXT_SOURCE_ADDRESS: begin
                    source_pixel_address_load_q <= source_row_base_q[31:0] +
                        {14'd0, source_x_byte_offset_q};
                    source_address_starts_writer_q <= 1'b0;
                    state <= ST_SOURCE_ADDRESS_COMMIT;
                end

                ST_NEXT_ROW_ADDRESS: begin
                        rows_remaining_q <= rows_remaining_q - 16'd1;
                        columns_remaining_q <= effective_width_q;
                        if (reverse_q) begin
                            destination_row_address_q <=
                                destination_row_address_q -
                                destination_pitch_q;
                            source_row_address_q <= source_row_address_q -
                                                    source_pitch_q;
                            state <= ST_NEXT_ROW_PIXEL;
                        end else begin
                            destination_row_address_q <=
                                destination_row_address_q +
                                destination_pitch_q;
                            if (is_blit_q) begin
                                source_phase_x_q <= source_phase_x_start_q;
                                source_phase_y_q <= next_source_phase_y;
                                effective_source_x_q <=
                                    mapped_source_x_start[15:0];
                                state <= ST_NEXT_ROW_SOURCE_MAP;
                            end else begin
                                state <= ST_NEXT_ROW_PIXEL;
                            end
                        end
                end

                ST_NEXT_ROW_SOURCE_MAP: begin
                    mapped_source_y_q <= mapped_source_y_start[15:0];
                    state <= ST_NEXT_ROW_SOURCE_OPERANDS;
                end

                ST_NEXT_ROW_SOURCE_OPERANDS: begin
                    source_row_low_y_q <= mapped_source_y_q;
                    source_row_high_y_q <= mapped_source_y_q;
                    auxiliary_row_low_y_q <= mapped_source_y_q;
                    auxiliary_row_high_y_q <= mapped_source_y_q;
                    state <= ST_NEXT_ROW_SOURCE_MULTIPLY;
                end

                ST_NEXT_ROW_SOURCE_MULTIPLY: begin
                    source_row_product_low_q <=
                        source_pitch_q[15:0] * source_row_low_y_q;
                    source_row_product_high_q <=
                        source_pitch_q[31:16] * source_row_high_y_q;
                    auxiliary_row_product_low_q <=
                        auxiliary_pitch_q[15:0] * auxiliary_row_low_y_q;
                    auxiliary_row_product_high_q <=
                        auxiliary_pitch_q[31:16] * auxiliary_row_high_y_q;
                    state <= ST_NEXT_ROW_SOURCE_COMBINE;
                end

                ST_NEXT_ROW_SOURCE_COMBINE: begin
                    source_row_product_q <=
                        {source_row_product_high_q, 16'd0} +
                        {16'd0, source_row_product_low_q};
                    auxiliary_row_product_q <=
                        {auxiliary_row_product_high_q, 16'd0} +
                        {16'd0, auxiliary_row_product_low_q};
                    state <= ST_NEXT_ROW_SOURCE_ADDRESS;
                end

                ST_NEXT_ROW_SOURCE_ADDRESS: begin
                    source_row_base_q <= source_surface_base_q +
                        source_row_product_q;
                    auxiliary_row_base_q <= auxiliary_surface_base_q +
                        auxiliary_row_product_q;
                    source_x_byte_offset_q <=
                        {2'd0, effective_source_x_q} << source_shift_q;
                    state <= ST_NEXT_ROW_SOURCE_X;
                end

                ST_NEXT_ROW_SOURCE_X: begin
                    source_row_address_q <= source_row_base_q +
                        {30'd0, source_x_byte_offset_q};
                    state <= ST_NEXT_ROW_PIXEL;
                end

                ST_NEXT_ROW_PIXEL: begin
                    destination_pixel_address_q <=
                        destination_row_address_q[31:0] +
                        (reverse_q ? {14'd0, endpoint_byte_offset_q} : 32'd0);
                    if (is_fill_q) begin
                        state <= ST_PIXEL;
                    end else begin
                        source_pixel_address_load_q <=
                            source_row_address_q[31:0] +
                            (reverse_q ? {14'd0, endpoint_byte_offset_q} :
                             32'd0);
                        source_address_starts_writer_q <= 1'b0;
                        state <= ST_SOURCE_ADDRESS_COMMIT;
                    end
                end

                ST_SOURCE_ADDRESS_COMMIT: begin
                    source_pixel_address_q <= source_pixel_address_load_q;
                    state <= source_address_starts_writer_q ?
                        ST_WRITER_START : ST_SOURCE;
                end

                ST_FLUSH: begin
                    writer_flush <= 1'b1;
                    if (writer_flush && writer_flush_ready) begin
                        writer_flush <= 1'b0;
                        state <= ST_WRITER_DONE;
                    end
                end

                ST_WRITER_DONE: begin
                    if (writer_done) begin
                        if (writer_error) begin
                            status <= `ASTRA_RENDER_STATUS_AXI_WRITE;
                            fault_detail <= writer_fault_detail;
                        end else if (writer_aborted) begin
                            status <= `ASTRA_RENDER_STATUS_RESET;
                        end
                        state <= ST_FINISH;
                    end
                end

                ST_ABORT_WRITER: begin
                    pixel_valid <= 1'b0;
                    writer_flush <= 1'b0;
                    writer_abort <= 1'b1;
                    if (writer_done) begin
                        if (status == `ASTRA_RENDER_STATUS_OK)
                            status <= `ASTRA_RENDER_STATUS_RESET;
                        state <= ST_FINISH;
                    end
                end

                ST_FINISH: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    abort_pending <= 1'b0;
                    state <= ST_IDLE;
                end

                default: begin
                    status <= `ASTRA_RENDER_STATUS_RESET;
                    fault_detail <= 32'hffff0000;
                    state <= ST_ABORT_WRITER;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
