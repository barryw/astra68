// Copyright (c) 2026 Astra68 contributors
//
// Four-pixel-per-clock INDEX8 sprite scanline builder. Admission is performed
// topmost first against the sprite-count and pixel budgets; admitted sprites are
// then rendered bottom-to-top into premultiplied front/behind line planes.
`timescale 1ns/1ps
`default_nettype none

module astra_sprite_line_builder #(
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720,
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = {AXI_ID_WIDTH{1'b0}},
    parameter integer PIXEL_BUDGET = 2048,
    parameter integer MAX_SPRITES_PER_LINE = 16,
    parameter integer MAX_BUILD_CYCLES = 4300
) (
    input  wire                         build_clk,
    input  wire                         build_reset,
    input  wire                         start,
    input  wire [1:0]                   build_slot,
    input  wire [9:0]                   line_y,

    output wire                         order_read_enable,
    output wire [5:0]                   order_read_position,
    input  wire [5:0]                   order_read_index,
    output wire                         descriptor_read_enable,
    output wire [5:0]                   descriptor_read_index,
    input  wire [31:0]                  descriptor_word0,
    input  wire [31:0]                  descriptor_word1,
    input  wire [31:0]                  descriptor_word2,
    input  wire [31:0]                  descriptor_word3,
    input  wire [31:0]                  descriptor_word4,
    input  wire [31:0]                  descriptor_word5,
    input  wire [31:0]                  descriptor_word6,
    input  wire [31:0]                  descriptor_scale_step_x,
    input  wire [63:0]                  descriptor_collision_compatible,

    output wire [3:0]                   palette0_read_bank,
    output wire [7:0]                   palette0_read_index,
    input  wire [31:0]                  palette0_read_argb,
    output wire [3:0]                   palette1_read_bank,
    output wire [7:0]                   palette1_read_index,
    input  wire [31:0]                  palette1_read_argb,
    output wire [3:0]                   palette2_read_bank,
    output wire [7:0]                   palette2_read_index,
    input  wire [31:0]                  palette2_read_argb,
    output wire [3:0]                   palette3_read_bank,
    output wire [7:0]                   palette3_read_index,
    input  wire [31:0]                  palette3_read_argb,

    output reg                          busy,
    output reg                          done,
    output reg                          line_complete,
    output reg  [1:0]                   completed_slot,
    output reg  [3:0]                   slot_valid,
    output reg                          fetch_error,
    output reg                          deadline_error,
    output wire [31:0]                  build_cycles,
    output wire [31:0]                  max_build_cycles,
    output reg  [31:0]                  axi_error_count,
    output reg  [31:0]                  deadline_error_count,
    output reg  [31:0]                  read_bytes,
    output reg  [63:0]                  overflow_bitmap,
    output reg  [9:0]                   overflow_line,
    output reg  [31:0]                  overflow_count,
    output reg  [31:0]                  pixels_admitted,
    output reg  [31:0]                  pixels_dropped,
    input  wire [5:0]                   collision_read_row,
    output reg  [63:0]                  collision_read_data,
    output reg  [31:0]                  collision_frame,
    output reg                          collision_event,

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
    output wire [31:0]                  pixel_front_argb,
    output wire [31:0]                  pixel_behind_argb
);
    localparam integer QUADS = (OUTPUT_WIDTH + 3) / 4;
    localparam integer BUILD_COUNTER_WIDTH =
        $clog2(MAX_BUILD_CYCLES + 1);
    localparam [BUILD_COUNTER_WIDTH-1:0] MAX_BUILD_COUNT =
        MAX_BUILD_CYCLES;
    localparam [BUILD_COUNTER_WIDTH-1:0] BUILD_COUNTER_ONE =
        {{(BUILD_COUNTER_WIDTH - 1){1'b0}}, 1'b1};

    localparam [3:0] S_IDLE = 4'd0;
    localparam [3:0] S_CLEAR = 4'd1;
    localparam [3:0] S_ORDER = 4'd2;
    localparam [3:0] S_DESCRIPTOR = 4'd3;
    localparam [3:0] S_EVALUATE = 4'd4;
    localparam [3:0] S_RUN = 4'd5;
    localparam [3:0] S_COPY = 4'd6;
    localparam [3:0] S_COPY_DRAIN = 4'd7;
    localparam [3:0] S_ADMIT = 4'd8;
    localparam [3:0] S_GEOMETRY = 4'd9;
    localparam [3:0] S_SPAN = 4'd10;
    localparam [3:0] S_COPY_DRAIN_2 = 4'd11;
    localparam [3:0] S_WAIT_CLEAR = 4'd12;
    localparam [3:0] S_CLIP = 4'd13;

    localparam [3:0] P_IDLE = 4'd0;
    localparam [3:0] P_DIVIDE = 4'd1;
    localparam [3:0] P_ADDRESS = 4'd2;
    localparam [3:0] P_SETUP = 4'd3;
    localparam [3:0] P_BURST = 4'd4;
    localparam [3:0] P_RECORD = 4'd5;
    localparam [3:0] P_METADATA = 4'd6;
    localparam [3:0] P_ROW_ADDRESS = 4'd7;
    localparam [3:0] P_SOURCE_Y = 4'd8;
    localparam [3:0] P_PUBLISH = 4'd9;
    localparam [3:0] P_LOAD = 4'd10;
    localparam [3:0] P_DIVIDE_LOAD = 4'd11;
    localparam [3:0] P_METADATA_WAIT = 4'd12;
    localparam [3:0] P_METADATA_DSP_WAIT = 4'd13;

    localparam [2:0] R_IDLE = 3'd0;
    localparam [2:0] R_ISSUE = 3'd1;
    localparam [2:0] R_WAIT = 3'd2;
    localparam [2:0] R_COLLISION = 3'd3;
    localparam [2:0] R_COLLISION_DRAIN = 3'd4;
    localparam [2:0] R_COMPLETE = 3'd5;

    function automatic [7:0] beat_byte(
        input [63:0] beat,
        input [2:0] byte_index
    );
        begin
            beat_byte = beat[byte_index * 8 +: 8];
        end
    endfunction

    function automatic [63:0] collision_bank_mask(
        input [2:0] bank,
        input [2:0] row,
        input [5:0] sprite,
        input [63:0] hits
    );
        reg [5:0] target;
        begin
            target = {row, bank};
            collision_bank_mask =
                (target == sprite ? hits : 64'd0) |
                (hits[target] ? (64'd1 << sprite) : 64'd0);
        end
    endfunction

    reg [3:0] state;
    reg [BUILD_COUNTER_WIDTH-1:0] build_cycles_q;
    reg [BUILD_COUNTER_WIDTH-1:0] max_build_cycles_q;
    reg [1:0] build_slot_q;
    reg [9:0] line_y_q;
    reg [8:0] clear_quad_q;
    reg clear_active_q;
    reg [5:0] admission_position_q;
    reg [5:0] order_position_q;
    reg [13:0] budget_remaining_q;
    reg [6:0] admitted_count_q;
    reg [63:0] line_overflow_q;
    reg [5:0] admission_sprite_index_q;
    reg admission_candidate_q;
    reg admission_accept_q;
    reg admission_write_q;
    reg signed [16:0] admission_x_q;
    reg signed [17:0] admission_right_signed_q;
    reg [10:0] admission_left_q;
    reg [10:0] admission_right_q;
    reg [10:0] admission_span_q;
    reg [10:0] admission_destination_offset_q;
    reg [10:0] admission_line_delta_q;
    reg signed [17:0] admission_line_delta_full_q;
    reg [10:0] admission_destination_height_q;
    reg admission_enabled_q;

    wire [BUILD_COUNTER_WIDTH-1:0] completed_build_cycles =
        build_cycles_q + BUILD_COUNTER_ONE;

    assign build_cycles =
        {{(32 - BUILD_COUNTER_WIDTH){1'b0}}, build_cycles_q};
    assign max_build_cycles =
        {{(32 - BUILD_COUNTER_WIDTH){1'b0}}, max_build_cycles_q};

    // After the first sprite, fetch the next order entry and descriptor while
    // the current admission decision retires. The scene-store ports are
    // independent, so this removes two dead cycles per remaining sprite.
    assign order_read_enable = state == S_ORDER ||
        (state == S_EVALUATE && admission_position_q != 6'd63);
    assign order_read_position = order_position_q;
    wire signed [16:0] admission_x =
        $signed({descriptor_word1[15], descriptor_word1[15:0]});
    wire signed [16:0] admission_y =
        $signed({descriptor_word1[31], descriptor_word1[31:16]});
    wire [10:0] admission_destination_width = descriptor_word3[10:0];
    wire [10:0] admission_destination_height = descriptor_word3[26:16];
    wire signed [17:0] admission_right_signed = admission_x +
        $signed({7'd0, admission_destination_width});
    wire signed [17:0] admission_line_delta =
        $signed({8'd0, line_y_q}) - admission_y;
    wire admission_line_visible = !admission_line_delta_full_q[17] &&
        admission_line_delta_full_q <
            $signed({7'd0, admission_destination_height_q});
    wire [10:0] admission_left = admission_x[16] ? 11'd0 :
        admission_x >= OUTPUT_WIDTH ? OUTPUT_WIDTH[10:0] : admission_x[10:0];
    wire [10:0] admission_right = admission_right_signed_q[17] ? 11'd0 :
        admission_right_signed_q >= OUTPUT_WIDTH ? OUTPUT_WIDTH[10:0] :
        admission_right_signed_q[10:0];
    wire [10:0] admission_span = admission_right_q > admission_left_q ?
        admission_right_q - admission_left_q : 11'd0;
    wire [10:0] admission_destination_offset =
        $signed({1'b0, admission_left_q}) - admission_x_q;
    wire admission_candidate = admission_enabled_q &&
        admission_line_visible && admission_span_q != 11'd0;
    wire admission_fits =
        admitted_count_q < MAX_SPRITES_PER_LINE &&
        budget_remaining_q >= admission_span_q;

    // The authoritative descriptor already lives in the scene store. Keep
    // only admission results here and reread the descriptor during preparation
    // instead of duplicating 214 descriptor bits for every admitted sprite.
    reg [27:0] list_record_q;
    wire [27:0] list_write_data = {
        admission_sprite_index_q,
        admission_left_q,
        admission_span_q
    };
    wire [5:0] list_record_index = list_record_q[27:22];
    wire [10:0] list_record_screen_x = list_record_q[21:11];
    wire [10:0] list_record_span = list_record_q[10:0];

    // Four source-row slots let address generation run ahead of DDR response
    // latency. Each 64-bit row memory is replicated four times for four
    // arbitrary source-byte lookups per build clock. The slot is the upper
    // two bits of the 64-word memory address.
    (* ram_style = "distributed" *) reg [63:0] row_rep0 [0:63];
    (* ram_style = "distributed" *) reg [63:0] row_rep1 [0:63];
    (* ram_style = "distributed" *) reg [63:0] row_rep2 [0:63];
    (* ram_style = "distributed" *) reg [63:0] row_rep3 [0:63];

    reg [3:0] buffer_ready_q;
    reg [3:0] buffer_fetch_busy_q;
    reg [3:0] buffer_render_busy_q;
    reg [5:0] buffer_sprite_index [0:3];
    reg [10:0] buffer_screen_x [0:3];
    reg [10:0] buffer_span [0:3];
    reg [7:0] buffer_source_width [0:3];
    reg [31:0] buffer_scale_step_x [0:3];
    reg [31:0] buffer_phase_x [0:3];
    reg [3:0] buffer_palette_bank [0:3];
    reg [7:0] buffer_transparent_index [0:3];
    reg [7:0] buffer_opacity [0:3];
    reg [3:0] buffer_flags [0:3];
    reg [63:0] buffer_compatible [0:3];

    reg [3:0] prep_state;
    reg [5:0] prep_sprite_index_q;
    assign descriptor_read_enable = state == S_DESCRIPTOR ||
        (state == S_ADMIT && admission_position_q != 6'd63) ||
        prep_state == P_METADATA;
    assign descriptor_read_index = prep_state == P_METADATA ?
        prep_sprite_index_q : order_read_index;
    reg prep_launch_q;
    reg [6:0] prep_remaining_q;
    reg [5:0] prep_list_position_q;
    reg [1:0] prep_slot_q;
    reg [10:0] prep_screen_x_q;
    reg [10:0] prep_span_q;
    reg [31:0] prep_base_q;
    reg [12:0] prep_pitch_q;
    reg [7:0] prep_source_width_q;
    reg [7:0] prep_source_height_q;
    reg [31:0] prep_scale_step_x_q;
    reg [3:0] prep_palette_bank_q;
    reg [7:0] prep_transparent_index_q;
    reg [7:0] prep_opacity_q;
    reg [3:0] prep_flags_q;
    reg [63:0] prep_compatible_q;
    reg [10:0] prep_destination_y_q;

    reg [17:0] prep_y_numerator_q;
    reg [17:0] prep_y_dividend_q;
    reg [10:0] prep_y_denominator_q;
    reg [11:0] prep_y_remainder_q;
    reg [17:0] prep_y_quotient_q;
    reg [4:0] prep_divider_bit_q;
    reg [10:0] prep_phase_multiplier_q;
    reg [42:0] prep_phase_multiplicand_q;
    reg [42:0] prep_phase_x_q;
    reg [7:0] prep_source_y_q;
    wire signed [16:0] prep_destination_offset =
        $signed({1'b0, prep_screen_x_q}) -
        $signed({descriptor_word1[15], descriptor_word1[15:0]});
    wire signed [17:0] prep_line_delta =
        $signed({8'd0, line_y_q}) -
        $signed({descriptor_word1[31], descriptor_word1[31],
                 descriptor_word1[31:16]});
    reg [20:0] prep_row_offset_q;
    reg [31:0] prep_row_address_q;
    reg [5:0] prep_beats_remaining_q;
    reg [31:0] prep_issue_address_q;
    reg [4:0] prep_burst_beats_q;
    reg [4:0] prep_row_word_q;
    reg prep_arvalid_q;

    // Every legal 128-byte source row needs at most two bursts because source
    // bases and pitches are 64-byte aligned. Four row slots therefore need at
    // most eight queued bursts. One fixed AXI ID preserves response order.
    (* ram_style = "distributed" *) reg [1:0] request_slot [0:7];
    (* ram_style = "distributed" *) reg [4:0] request_start_word [0:7];
    (* ram_style = "distributed" *) reg [4:0] request_beats [0:7];
    (* ram_style = "distributed" *) reg request_last_for_sprite [0:7];
    reg [2:0] request_write_ptr_q;
    reg [2:0] request_read_ptr_q;
    reg [3:0] request_count_q;
    reg response_active_q;
    reg [1:0] response_slot_q;
    reg [4:0] response_start_word_q;
    reg [4:0] response_burst_beats_q;
    reg response_last_for_sprite_q;
    reg [4:0] response_beat_q;
    reg row_write_valid_q;
    reg [5:0] row_write_address_q;
    reg [63:0] row_write_data_q;
    reg response_publish_valid_q;
    reg [1:0] response_publish_slot_q;
    reg abort_drain_q;

    wire [11:0] prep_y_shifted = {
        prep_y_remainder_q[10:0], prep_y_dividend_q[17]
    };
    wire prep_y_subtract = prep_y_shifted >= {1'b0, prep_y_denominator_q};
    wire [11:0] prep_y_remainder_next = prep_y_subtract ?
        prep_y_shifted - {1'b0, prep_y_denominator_q} : prep_y_shifted;
    wire [9:0] prep_beats_to_4k =
        (13'd4096 - {1'b0, prep_issue_address_q[11:0]}) >> 3;
    wire [4:0] prep_selected_burst =
        prep_beats_remaining_q < 6'd16 ? prep_beats_remaining_q[4:0] :
        prep_beats_to_4k < 10'd16 ? prep_beats_to_4k[4:0] : 5'd16;

    assign m_axi_arid = AXI_ID;
    assign m_axi_araddr = prep_issue_address_q;
    assign m_axi_arlen = {3'd0, prep_burst_beats_q} - 8'd1;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b1110;
    assign m_axi_arvalid = prep_arvalid_q;
    assign m_axi_rready = response_active_q;
    wire prep_ar_accept = m_axi_arvalid && m_axi_arready;
    wire prep_response_accept = m_axi_rvalid && m_axi_rready;
    wire response_dispatch = !response_active_q &&
        request_count_q != 4'd0;
    wire [4:0] response_row_word =
        response_start_word_q + response_beat_q;
    wire response_expected_last =
        response_beat_q + 5'd1 == response_burst_beats_q;
    wire response_burst_complete = prep_response_accept && m_axi_rlast;

    reg [2:0] render_state;
    reg [6:0] render_remaining_sprites_q;
    reg [1:0] render_slot_q;
    reg [5:0] render_sprite_index_q;
    reg [10:0] render_screen_x_q;
    reg [10:0] render_pixels_remaining_q;
    reg [7:0] render_source_width_q;
    reg [31:0] render_phase_x_q;
    reg [31:0] render_scale_step1_x_q;
    reg [31:0] render_scale_step2_x_q;
    reg [31:0] render_scale_step3_x_q;
    reg [31:0] render_scale_step4_x_q;
    reg [3:0] render_palette_bank_q;
    reg [7:0] render_transparent_index_q;
    reg [7:0] render_opacity_q;
    reg [3:0] render_flags_q;
    reg [63:0] render_compatible_q;
    reg [63:0] render_overlap_q;
    reg [63:0] collision_hits_q;
    reg [7:0] collision_hit_groups_q;
    reg [3:0] collision_update_step_q;
    reg [5:0] collision_update_sprite_q;
    reg collision_update_busy_q;
    reg collision_bank_update_valid_q;
    reg [2:0] collision_bank_update_row_q;
    reg [63:0] collision_bank_update_mask_q [0:7];
    wire [63:0] render_collision_hits =
        render_overlap_q & render_compatible_q;

    wire [2:0] render_lane_capacity = 3'd4 -
        {1'b0, render_screen_x_q[1:0]};
    wire [2:0] render_group_count =
        render_pixels_remaining_q < render_lane_capacity ?
        render_pixels_remaining_q[2:0] : render_lane_capacity;
    wire render_issue = render_state == R_ISSUE &&
                        render_pixels_remaining_q != 11'd0;
    wire render_group_last = render_pixels_remaining_q <=
        {8'd0, render_lane_capacity};
    wire [8:0] render_quad = render_screen_x_q[10:2];

    wire [31:0] render_phase1_x =
        render_phase_x_q + render_scale_step1_x_q;
    wire [31:0] render_phase2_x =
        render_phase_x_q + render_scale_step2_x_q;
    wire [31:0] render_phase3_x =
        render_phase_x_q + render_scale_step3_x_q;
    wire [31:0] render_phase4_x =
        render_phase_x_q + render_scale_step4_x_q;
    wire [31:0] render_phase_boundary_x =
        render_screen_x_q[1:0] == 2'd0 ? render_phase4_x :
        render_screen_x_q[1:0] == 2'd1 ? render_phase3_x :
        render_screen_x_q[1:0] == 2'd2 ? render_phase2_x : render_phase1_x;

    reg source_stage_valid_q;
    reg [1:0] source_stage_slot_q;
    reg [8:0] source_stage_quad_q;
    reg [1:0] source_stage_lane_offset_q;
    reg [3:0] source_stage_lane_mask_q;
    reg source_stage_front_q;
    reg source_stage_collision_q;
    reg source_stage_last_q;
    reg [5:0] source_stage_sprite_index_q;
    reg [3:0] source_stage_palette_bank_q;
    reg [7:0] source_stage_transparent_index_q;
    reg [7:0] source_stage_opacity_q;
    reg source_stage_reflect_x_q;
    reg [7:0] source_stage_width_q;
    reg [7:0] source_stage_x0_q;
    reg [7:0] source_stage_x1_q;
    reg [7:0] source_stage_x2_q;
    reg [7:0] source_stage_x3_q;

    reg row_stage_valid_q;
    reg [1:0] row_stage_slot_q;
    reg [8:0] row_stage_quad_q;
    reg [3:0] row_stage_lane_mask_q;
    reg row_stage_front_q;
    reg row_stage_collision_q;
    reg row_stage_last_q;
    reg [5:0] row_stage_sprite_index_q;
    reg [3:0] row_stage_palette_bank_q;
    reg [7:0] row_stage_transparent_index_q;
    reg [7:0] row_stage_opacity_q;
    reg [7:0] row_stage_source_index0_q;
    reg [7:0] row_stage_source_index1_q;
    reg [7:0] row_stage_source_index2_q;
    reg [7:0] row_stage_source_index3_q;

    wire [7:0] source_stage_index0 = source_stage_reflect_x_q ?
        source_stage_width_q - 8'd1 - source_stage_x0_q :
        source_stage_x0_q;
    wire [7:0] source_stage_index1 = source_stage_reflect_x_q ?
        source_stage_width_q - 8'd1 - source_stage_x1_q :
        source_stage_x1_q;
    wire [7:0] source_stage_index2 = source_stage_reflect_x_q ?
        source_stage_width_q - 8'd1 - source_stage_x2_q :
        source_stage_x2_q;
    wire [7:0] source_stage_index3 = source_stage_reflect_x_q ?
        source_stage_width_q - 8'd1 - source_stage_x3_q :
        source_stage_x3_q;

`ifdef SYNTHESIS
    xpm_memory_sdpram #(
        .ADDR_WIDTH_A(6),
        .ADDR_WIDTH_B(6),
        .BYTE_WRITE_WIDTH_A(28),
        .CLOCKING_MODE("common_clock"),
        .MEMORY_INIT_FILE("none"),
        .MEMORY_INIT_PARAM("0"),
        .MEMORY_OPTIMIZATION("true"),
        .MEMORY_PRIMITIVE("block"),
        .MEMORY_SIZE(1792),
        .RAM_DECOMP("area"),
        .READ_DATA_WIDTH_B(28),
        .READ_LATENCY_B(1),
        .READ_RESET_VALUE_B("0"),
        .USE_MEM_INIT(0),
        .WRITE_DATA_WIDTH_A(28),
        .WRITE_MODE_B("no_change")
    ) list_memory_i (
        .dbiterrb(),
        .doutb(list_record_q),
        .sbiterrb(),
        .addra(admitted_count_q[5:0]),
        .addrb(prep_list_position_q),
        .clka(build_clk),
        .clkb(build_clk),
        .dina(list_write_data),
        .ena(admission_write_q),
        .enb(1'b1),
        .injectdbiterra(1'b0),
        .injectsbiterra(1'b0),
        .regceb(1'b1),
        .rstb(1'b0),
        .sleep(1'b0),
        .wea(admission_write_q)
    );
`else
    reg [27:0] list_memory [0:63];

    always @(posedge build_clk) begin
        if (admission_write_q)
            list_memory[admitted_count_q[5:0]] <= list_write_data;
    end

    always @(posedge build_clk) begin
        list_record_q <= list_memory[prep_list_position_q];
    end
`endif

    wire [63:0] row_stage_row_word0 =
        row_rep0[{row_stage_slot_q, row_stage_source_index0_q[6:3]}];
    wire [63:0] row_stage_row_word1 =
        row_rep1[{row_stage_slot_q, row_stage_source_index1_q[6:3]}];
    wire [63:0] row_stage_row_word2 =
        row_rep2[{row_stage_slot_q, row_stage_source_index2_q[6:3]}];
    wire [63:0] row_stage_row_word3 =
        row_rep3[{row_stage_slot_q, row_stage_source_index3_q[6:3]}];
    wire [7:0] row_stage_index0 = beat_byte(
        row_stage_row_word0, row_stage_source_index0_q[2:0]);
    wire [7:0] row_stage_index1 = beat_byte(
        row_stage_row_word1, row_stage_source_index1_q[2:0]);
    wire [7:0] row_stage_index2 = beat_byte(
        row_stage_row_word2, row_stage_source_index2_q[2:0]);
    wire [7:0] row_stage_index3 = beat_byte(
        row_stage_row_word3, row_stage_source_index3_q[2:0]);

    reg [3:0] render_lane_mask;
    integer render_lane;
    always @* begin
        render_lane_mask = 4'd0;
        for (render_lane = 0; render_lane < 4; render_lane = render_lane + 1) begin
            if (render_lane >= render_screen_x_q[1:0] &&
                render_lane < render_screen_x_q[1:0] + render_group_count)
                render_lane_mask[render_lane] = 1'b1;
        end
    end

    wire [31:0] working_front_read0;
    wire [31:0] working_front_read1;
    wire [31:0] working_front_read2;
    wire [31:0] working_front_read3;
    wire [31:0] working_behind_read0;
    wire [31:0] working_behind_read1;
    wire [31:0] working_behind_read2;
    wire [31:0] working_behind_read3;
    wire [63:0] occupancy_read0;
    wire [63:0] occupancy_read1;
    wire [63:0] occupancy_read2;
    wire [63:0] occupancy_read3;

    reg palette_stage_valid_q;
    reg [8:0] palette_stage_quad_q;
    reg [3:0] palette_stage_lane_mask_q;
    reg palette_stage_front_q;
    reg palette_stage_collision_q;
    reg palette_stage_last_q;
    reg [5:0] palette_stage_sprite_index_q;
    reg [3:0] palette_stage_palette_bank_q;
    reg [7:0] palette_stage_transparent_index_q;
    reg [7:0] palette_stage_opacity_q;
    reg [7:0] palette_stage_index0_q;
    reg [7:0] palette_stage_index1_q;
    reg [7:0] palette_stage_index2_q;
    reg [7:0] palette_stage_index3_q;

    reg palette_lookup_valid_q;
    reg [8:0] palette_lookup_quad_q;
    reg [3:0] palette_lookup_lane_mask_q;
    reg palette_lookup_front_q;
    reg palette_lookup_collision_q;
    reg palette_lookup_last_q;
    reg [5:0] palette_lookup_sprite_index_q;
    reg [7:0] palette_lookup_transparent_index_q;
    reg [7:0] palette_lookup_opacity_q;
    reg [7:0] palette_lookup_index0_q;
    reg [7:0] palette_lookup_index1_q;
    reg [7:0] palette_lookup_index2_q;
    reg [7:0] palette_lookup_index3_q;

    reg palette_capture_valid_q;
    reg [8:0] palette_capture_quad_q;
    reg [3:0] palette_capture_lane_mask_q;
    reg palette_capture_front_q;
    reg palette_capture_collision_q;
    reg palette_capture_last_q;
    reg [5:0] palette_capture_sprite_index_q;
    reg [7:0] palette_capture_transparent_index_q;
    reg [7:0] palette_capture_opacity_q;
    reg [7:0] palette_capture_index0_q;
    reg [7:0] palette_capture_index1_q;
    reg [7:0] palette_capture_index2_q;
    reg [7:0] palette_capture_index3_q;
    (* keep = "yes" *) reg [31:0] palette_capture_argb0_q;
    (* keep = "yes" *) reg [31:0] palette_capture_argb1_q;
    (* keep = "yes" *) reg [31:0] palette_capture_argb2_q;
    (* keep = "yes" *) reg [31:0] palette_capture_argb3_q;
    reg [31:0] palette_capture_destination0_q;
    reg [31:0] palette_capture_destination1_q;
    reg [31:0] palette_capture_destination2_q;
    reg [31:0] palette_capture_destination3_q;
    reg [63:0] palette_capture_occupancy0_q;
    reg [63:0] palette_capture_occupancy1_q;
    reg [63:0] palette_capture_occupancy2_q;
    reg [63:0] palette_capture_occupancy3_q;

    reg palette_color_valid_q;
    reg [8:0] palette_color_quad_q;
    reg [3:0] palette_color_lane_mask_q;
    reg palette_color_front_q;
    reg palette_color_collision_q;
    reg palette_color_last_q;
    reg [5:0] palette_color_sprite_index_q;
    reg [7:0] palette_color_transparent_index_q;
    reg [7:0] palette_color_opacity_q;
    reg [7:0] palette_color_index0_q;
    reg [7:0] palette_color_index1_q;
    reg [7:0] palette_color_index2_q;
    reg [7:0] palette_color_index3_q;
    // The preceding capture stage owns BRAM placement. Allow Vivado to
    // replicate this math stage beside the independent blend and collision
    // DSP consumers without pulling those replicas back to the palette BRAM.
    (* keep = "yes", max_fanout = 1 *) reg [31:0] palette_color_argb0_q;
    (* keep = "yes", max_fanout = 1 *) reg [31:0] palette_color_argb1_q;
    (* keep = "yes", max_fanout = 1 *) reg [31:0] palette_color_argb2_q;
    (* keep = "yes", max_fanout = 1 *) reg [31:0] palette_color_argb3_q;
    reg [31:0] palette_color_destination0_q;
    reg [31:0] palette_color_destination1_q;
    reg [31:0] palette_color_destination2_q;
    reg [31:0] palette_color_destination3_q;
    reg [63:0] palette_color_occupancy0_q;
    reg [63:0] palette_color_occupancy1_q;
    reg [63:0] palette_color_occupancy2_q;
    reg [63:0] palette_color_occupancy3_q;

    assign palette0_read_bank = palette_stage_palette_bank_q;
    assign palette1_read_bank = palette_stage_palette_bank_q;
    assign palette2_read_bank = palette_stage_palette_bank_q;
    assign palette3_read_bank = palette_stage_palette_bank_q;
    assign palette0_read_index = palette_stage_index0_q;
    assign palette1_read_index = palette_stage_index1_q;
    assign palette2_read_index = palette_stage_index2_q;
    assign palette3_read_index = palette_stage_index3_q;

    wire palette_apply0 = palette_color_lane_mask_q[0] &&
        palette_color_index0_q != palette_color_transparent_index_q;
    wire palette_apply1 = palette_color_lane_mask_q[1] &&
        palette_color_index1_q != palette_color_transparent_index_q;
    wire palette_apply2 = palette_color_lane_mask_q[2] &&
        palette_color_index2_q != palette_color_transparent_index_q;
    wire palette_apply3 = palette_color_lane_mask_q[3] &&
        palette_color_index3_q != palette_color_transparent_index_q;
    wire [15:0] collision_alpha0 =
        palette_color_argb0_q[31:24] * palette_color_opacity_q;
    wire [15:0] collision_alpha1 =
        palette_color_argb1_q[31:24] * palette_color_opacity_q;
    wire [15:0] collision_alpha2 =
        palette_color_argb2_q[31:24] * palette_color_opacity_q;
    wire [15:0] collision_alpha3 =
        palette_color_argb3_q[31:24] * palette_color_opacity_q;
    // Split alpha multiplication from threshold qualification, then isolate
    // both from the occupancy read/modify/write path. These stages run in
    // parallel with the longer blend pipeline and add no line latency.
    reg collision_qualify_valid_q;
    reg [3:0] collision_qualify_apply_q;
    reg [8:0] collision_qualify_quad_q;
    reg [5:0] collision_qualify_sprite_index_q;
    (* use_dsp = "yes" *) reg [15:0] collision_qualify_alpha0_q;
    (* use_dsp = "yes" *) reg [15:0] collision_qualify_alpha1_q;
    (* use_dsp = "yes" *) reg [15:0] collision_qualify_alpha2_q;
    (* use_dsp = "yes" *) reg [15:0] collision_qualify_alpha3_q;
    reg [63:0] collision_qualify_occupancy0_q;
    reg [63:0] collision_qualify_occupancy1_q;
    reg [63:0] collision_qualify_occupancy2_q;
    reg [63:0] collision_qualify_occupancy3_q;
    reg collision_stage_valid_q;
    reg [3:0] collision_stage_apply_q;
    reg [8:0] collision_stage_quad_q;
    reg [5:0] collision_stage_sprite_index_q;
    reg [63:0] collision_stage_occupancy0_q;
    reg [63:0] collision_stage_occupancy1_q;
    reg [63:0] collision_stage_occupancy2_q;
    reg [63:0] collision_stage_occupancy3_q;
    wire [63:0] collision_stage_sprite_mask =
        64'd1 << collision_stage_sprite_index_q;

    wire [31:0] blend_destination0 = palette_color_destination0_q;
    wire [31:0] blend_destination1 = palette_color_destination1_q;
    wire [31:0] blend_destination2 = palette_color_destination2_q;
    wire [31:0] blend_destination3 = palette_color_destination3_q;

    wire blend_valid0;
    wire blend_valid1;
    wire blend_valid2;
    wire blend_valid3;
    wire [31:0] blend_output0;
    wire [31:0] blend_output1;
    wire [31:0] blend_output2;
    wire [31:0] blend_output3;
    astra_blend_premult_pipeline blend0_i (
        .clk(build_clk), .reset(build_reset),
        .input_valid(palette_color_valid_q),
        .destination_premult_argb(blend_destination0),
        .source_straight_argb(palette_color_argb0_q),
        .opacity(palette_color_opacity_q), .apply_source(palette_apply0),
        .output_valid(blend_valid0), .output_premult_argb(blend_output0)
    );
    astra_blend_premult_pipeline blend1_i (
        .clk(build_clk), .reset(build_reset),
        .input_valid(palette_color_valid_q),
        .destination_premult_argb(blend_destination1),
        .source_straight_argb(palette_color_argb1_q),
        .opacity(palette_color_opacity_q), .apply_source(palette_apply1),
        .output_valid(blend_valid1), .output_premult_argb(blend_output1)
    );
    astra_blend_premult_pipeline blend2_i (
        .clk(build_clk), .reset(build_reset),
        .input_valid(palette_color_valid_q),
        .destination_premult_argb(blend_destination2),
        .source_straight_argb(palette_color_argb2_q),
        .opacity(palette_color_opacity_q), .apply_source(palette_apply2),
        .output_valid(blend_valid2), .output_premult_argb(blend_output2)
    );
    astra_blend_premult_pipeline blend3_i (
        .clk(build_clk), .reset(build_reset),
        .input_valid(palette_color_valid_q),
        .destination_premult_argb(blend_destination3),
        .source_straight_argb(palette_color_argb3_q),
        .opacity(palette_color_opacity_q), .apply_source(palette_apply3),
        .output_valid(blend_valid3), .output_premult_argb(blend_output3)
    );

    reg [8:0] blend_quad_pipe [0:7];
    reg [3:0] blend_mask_pipe [0:7];
    reg blend_front_pipe [0:7];
    reg blend_last_pipe [0:7];
    wire blend_output_valid = blend_valid0;

    wire clear_working_line = clear_active_q;
    reg copy_active_q;
    reg [8:0] copy_read_quad_q;
    wire [8:0] working_read_address = copy_active_q ? copy_read_quad_q :
                                                    palette_stage_quad_q;

    wire front0_blend_write = blend_output_valid &&
                              blend_front_pipe[7] &&
                              blend_mask_pipe[7][0];
    wire front1_blend_write = blend_output_valid &&
                              blend_front_pipe[7] &&
                              blend_mask_pipe[7][1];
    wire front2_blend_write = blend_output_valid &&
                              blend_front_pipe[7] &&
                              blend_mask_pipe[7][2];
    wire front3_blend_write = blend_output_valid &&
                              blend_front_pipe[7] &&
                              blend_mask_pipe[7][3];
    wire behind0_blend_write = blend_output_valid &&
                               !blend_front_pipe[7] &&
                               blend_mask_pipe[7][0];
    wire behind1_blend_write = blend_output_valid &&
                               !blend_front_pipe[7] &&
                               blend_mask_pipe[7][1];
    wire behind2_blend_write = blend_output_valid &&
                               !blend_front_pipe[7] &&
                               blend_mask_pipe[7][2];
    wire behind3_blend_write = blend_output_valid &&
                               !blend_front_pipe[7] &&
                               blend_mask_pipe[7][3];

    wire occupancy0_collision_write = collision_stage_valid_q &&
                                      collision_stage_apply_q[0];
    wire occupancy1_collision_write = collision_stage_valid_q &&
                                      collision_stage_apply_q[1];
    wire occupancy2_collision_write = collision_stage_valid_q &&
                                      collision_stage_apply_q[2];
    wire occupancy3_collision_write = collision_stage_valid_q &&
                                      collision_stage_apply_q[3];

    // Register every working-RAM write command before the BRAM boundary.
    // Besides matching the RAM's synchronous interface, this keeps the
    // high-fanout clear-state decode out of the BRAM data-input timing cone.
    reg [7:0] color_write_enable_q;
    reg [8:0] color_write_address_q;
    reg [31:0] color_write_data0_q;
    reg [31:0] color_write_data1_q;
    reg [31:0] color_write_data2_q;
    reg [31:0] color_write_data3_q;
    reg [3:0] occupancy_write_enable_q;
    reg [8:0] occupancy_write_address_q;
    reg [63:0] occupancy_write_data0_q;
    reg [63:0] occupancy_write_data1_q;
    reg [63:0] occupancy_write_data2_q;
    reg [63:0] occupancy_write_data3_q;

    always @(posedge build_clk) begin
        if (build_reset) begin
            color_write_enable_q <= 8'd0;
            occupancy_write_enable_q <= 4'd0;
        end else begin
            color_write_enable_q <= {
                clear_working_line || behind3_blend_write,
                clear_working_line || behind2_blend_write,
                clear_working_line || behind1_blend_write,
                clear_working_line || behind0_blend_write,
                clear_working_line || front3_blend_write,
                clear_working_line || front2_blend_write,
                clear_working_line || front1_blend_write,
                clear_working_line || front0_blend_write
            };
            occupancy_write_enable_q <= {
                clear_working_line || occupancy3_collision_write,
                clear_working_line || occupancy2_collision_write,
                clear_working_line || occupancy1_collision_write,
                clear_working_line || occupancy0_collision_write
            };
        end

        if (clear_working_line || blend_output_valid) begin
            color_write_address_q <= clear_working_line ? clear_quad_q :
                                                              blend_quad_pipe[7];
            color_write_data0_q <= clear_working_line ? 32'd0 :
                                                        blend_output0;
            color_write_data1_q <= clear_working_line ? 32'd0 :
                                                        blend_output1;
            color_write_data2_q <= clear_working_line ? 32'd0 :
                                                        blend_output2;
            color_write_data3_q <= clear_working_line ? 32'd0 :
                                                        blend_output3;
        end

        if (clear_working_line || collision_stage_valid_q) begin
            occupancy_write_address_q <= clear_working_line ? clear_quad_q :
                                                       collision_stage_quad_q;
            occupancy_write_data0_q <= clear_working_line ? 64'd0 :
                collision_stage_occupancy0_q |
                collision_stage_sprite_mask;
            occupancy_write_data1_q <= clear_working_line ? 64'd0 :
                collision_stage_occupancy1_q |
                collision_stage_sprite_mask;
            occupancy_write_data2_q <= clear_working_line ? 64'd0 :
                collision_stage_occupancy2_q |
                collision_stage_sprite_mask;
            occupancy_write_data3_q <= clear_working_line ? 64'd0 :
                collision_stage_occupancy3_q |
                collision_stage_sprite_mask;
        end
    end

    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_front0_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[0]),
            .write_address(color_write_address_q),
            .write_data(color_write_data0_q),
            .read_address(working_read_address),
            .read_data(working_front_read0)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_front1_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[1]),
            .write_address(color_write_address_q),
            .write_data(color_write_data1_q),
            .read_address(working_read_address),
            .read_data(working_front_read1)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_front2_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[2]),
            .write_address(color_write_address_q),
            .write_data(color_write_data2_q),
            .read_address(working_read_address),
            .read_data(working_front_read2)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_front3_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[3]),
            .write_address(color_write_address_q),
            .write_data(color_write_data3_q),
            .read_address(working_read_address),
            .read_data(working_front_read3)
        );

    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_behind0_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[4]),
            .write_address(color_write_address_q),
            .write_data(color_write_data0_q),
            .read_address(working_read_address),
            .read_data(working_behind_read0)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_behind1_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[5]),
            .write_address(color_write_address_q),
            .write_data(color_write_data1_q),
            .read_address(working_read_address),
            .read_data(working_behind_read1)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_behind2_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[6]),
            .write_address(color_write_address_q),
            .write_data(color_write_data2_q),
            .read_address(working_read_address),
            .read_data(working_behind_read2)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(32), .DEPTH(QUADS))
        working_behind3_i (
            .clk(build_clk),
            .write_enable(color_write_enable_q[7]),
            .write_address(color_write_address_q),
            .write_data(color_write_data3_q),
            .read_address(working_read_address),
            .read_data(working_behind_read3)
        );

    astra_sprite_work_ram #(.DATA_WIDTH(64), .DEPTH(QUADS))
        occupancy0_i (
            .clk(build_clk),
            .write_enable(occupancy_write_enable_q[0]),
            .write_address(occupancy_write_address_q),
            .write_data(occupancy_write_data0_q),
            .read_address(working_read_address),
            .read_data(occupancy_read0)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(64), .DEPTH(QUADS))
        occupancy1_i (
            .clk(build_clk),
            .write_enable(occupancy_write_enable_q[1]),
            .write_address(occupancy_write_address_q),
            .write_data(occupancy_write_data1_q),
            .read_address(working_read_address),
            .read_data(occupancy_read1)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(64), .DEPTH(QUADS))
        occupancy2_i (
            .clk(build_clk),
            .write_enable(occupancy_write_enable_q[2]),
            .write_address(occupancy_write_address_q),
            .write_data(occupancy_write_data2_q),
            .read_address(working_read_address),
            .read_data(occupancy_read2)
        );
    astra_sprite_work_ram #(.DATA_WIDTH(64), .DEPTH(QUADS))
        occupancy3_i (
            .clk(build_clk),
            .write_enable(occupancy_write_enable_q[3]),
            .write_address(occupancy_write_address_q),
            .write_data(occupancy_write_data3_q),
            .read_address(working_read_address),
            .read_data(occupancy_read3)
        );

    reg collision_any_current_q;
    reg [31:0] collision_current_frame_q;
    reg collision_rotate_frame_q;
    wire collision_rotate_enable = clear_active_q &&
        collision_rotate_frame_q && clear_quad_q < 9'd8;
    wire [63:0] collision_published_bank_read [0:7];
    wire [63:0] collision_published_read =
        collision_published_bank_read[collision_read_row[2:0]];
    genvar collision_bank;
    generate
        for (collision_bank = 0; collision_bank < 8;
             collision_bank = collision_bank + 1) begin : collision_banks
            astra_sprite_collision_bank collision_bank_i (
                .clk(build_clk),
                .reset(build_reset),
                .update_enable(collision_bank_update_valid_q),
                .update_row(collision_bank_update_row_q),
                .update_mask(collision_bank_update_mask_q[collision_bank]),
                .rotate_enable(collision_rotate_enable),
                .rotate_row(clear_quad_q[2:0]),
                .published_read_row(collision_read_row[5:3]),
                .published_read_data(
                    collision_published_bank_read[collision_bank])
            );
        end
    endgenerate

    reg copy_valid_q;
    reg [8:0] copy_quad_q;
    reg copy_data_valid_q;
    reg [1:0] copy_data_slot_q;
    reg [8:0] copy_data_quad_q;
    reg [127:0] copy_front_q;
    reg [127:0] copy_behind_q;
    wire [127:0] copy_front = {
        working_front_read3,
        working_front_read2,
        working_front_read1,
        working_front_read0
    };
    wire [127:0] copy_behind = {
        working_behind_read3,
        working_behind_read2,
        working_behind_read1,
        working_behind_read0
    };
    astra_sprite_line_store #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH)
    ) line_store_i (
        .build_clk(build_clk),
        .write_enable(copy_data_valid_q),
        .write_slot(copy_data_slot_q),
        .write_quad(copy_data_quad_q),
        .write_front(copy_front_q),
        .write_behind(copy_behind_q),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .read_slot(pixel_read_slot),
        .read_x(pixel_read_x),
        .read_front(pixel_front_argb),
        .read_behind(pixel_behind_argb)
    );

    always @(posedge build_clk) begin
        if (build_reset) begin
            copy_data_valid_q <= 1'b0;
        end else begin
            copy_data_valid_q <= copy_valid_q;
        end
        if (copy_valid_q) begin
            copy_data_slot_q <= build_slot_q;
            copy_data_quad_q <= copy_quad_q;
            copy_front_q <= copy_front;
            copy_behind_q <= copy_behind;
        end
    end

    integer blend_pipe_index;
    integer collision_mask_index;
    always @(posedge build_clk) begin
        if (build_reset) begin
            source_stage_valid_q <= 1'b0;
            row_stage_valid_q <= 1'b0;
            palette_stage_valid_q <= 1'b0;
            palette_lookup_valid_q <= 1'b0;
            palette_capture_valid_q <= 1'b0;
            palette_color_valid_q <= 1'b0;
            collision_qualify_valid_q <= 1'b0;
            collision_stage_valid_q <= 1'b0;
        end else begin
            source_stage_valid_q <= render_issue;
            row_stage_valid_q <= source_stage_valid_q;
            palette_stage_valid_q <= row_stage_valid_q;
            palette_lookup_valid_q <= palette_stage_valid_q;
            palette_capture_valid_q <= palette_lookup_valid_q;
            palette_color_valid_q <= palette_capture_valid_q;
            collision_qualify_valid_q <= palette_color_valid_q;
            collision_stage_valid_q <= collision_qualify_valid_q;
        end
    end

    // Stage payload is guarded by the valid pipeline above. It deliberately
    // has no global reset, reducing reset fanout and allowing DSP/BRAM-local
    // placement without changing externally visible reset behavior.
    always @(posedge build_clk) begin
        if (render_issue) begin
            source_stage_slot_q <= render_slot_q;
            source_stage_quad_q <= render_quad;
            source_stage_lane_offset_q <= render_screen_x_q[1:0];
            source_stage_lane_mask_q <= render_lane_mask;
            source_stage_front_q <= render_flags_q[2];
            source_stage_collision_q <= render_flags_q[3];
            source_stage_last_q <= render_group_last;
            source_stage_sprite_index_q <= render_sprite_index_q;
            source_stage_palette_bank_q <= render_palette_bank_q;
            source_stage_transparent_index_q <= render_transparent_index_q;
            source_stage_opacity_q <= render_opacity_q;
            source_stage_reflect_x_q <= render_flags_q[0];
            source_stage_width_q <= render_source_width_q;
            source_stage_x0_q <= render_phase_x_q[31:24];
            source_stage_x1_q <= render_phase1_x[31:24];
            source_stage_x2_q <= render_phase2_x[31:24];
            source_stage_x3_q <= render_phase3_x[31:24];
        end

        if (source_stage_valid_q) begin
            row_stage_slot_q <= source_stage_slot_q;
            row_stage_quad_q <= source_stage_quad_q;
            row_stage_lane_mask_q <= source_stage_lane_mask_q;
            row_stage_front_q <= source_stage_front_q;
            row_stage_collision_q <= source_stage_collision_q;
            row_stage_last_q <= source_stage_last_q;
            row_stage_sprite_index_q <= source_stage_sprite_index_q;
            row_stage_palette_bank_q <= source_stage_palette_bank_q;
            row_stage_transparent_index_q <=
                source_stage_transparent_index_q;
            row_stage_opacity_q <= source_stage_opacity_q;
            case (source_stage_lane_offset_q)
                2'd0: begin
                    row_stage_source_index0_q <= source_stage_index0;
                    row_stage_source_index1_q <= source_stage_index1;
                    row_stage_source_index2_q <= source_stage_index2;
                    row_stage_source_index3_q <= source_stage_index3;
                end
                2'd1: begin
                    row_stage_source_index0_q <= 8'd0;
                    row_stage_source_index1_q <= source_stage_index0;
                    row_stage_source_index2_q <= source_stage_index1;
                    row_stage_source_index3_q <= source_stage_index2;
                end
                2'd2: begin
                    row_stage_source_index0_q <= 8'd0;
                    row_stage_source_index1_q <= 8'd0;
                    row_stage_source_index2_q <= source_stage_index0;
                    row_stage_source_index3_q <= source_stage_index1;
                end
                default: begin
                    row_stage_source_index0_q <= 8'd0;
                    row_stage_source_index1_q <= 8'd0;
                    row_stage_source_index2_q <= 8'd0;
                    row_stage_source_index3_q <= source_stage_index0;
                end
            endcase
        end

        if (row_stage_valid_q) begin
            palette_stage_quad_q <= row_stage_quad_q;
            palette_stage_lane_mask_q <= row_stage_lane_mask_q;
            palette_stage_front_q <= row_stage_front_q;
            palette_stage_collision_q <= row_stage_collision_q;
            palette_stage_last_q <= row_stage_last_q;
            palette_stage_sprite_index_q <= row_stage_sprite_index_q;
            palette_stage_palette_bank_q <= row_stage_palette_bank_q;
            palette_stage_transparent_index_q <=
                row_stage_transparent_index_q;
            palette_stage_opacity_q <= row_stage_opacity_q;
            palette_stage_index0_q <= row_stage_index0;
            palette_stage_index1_q <= row_stage_index1;
            palette_stage_index2_q <= row_stage_index2;
            palette_stage_index3_q <= row_stage_index3;
        end

        if (palette_stage_valid_q) begin
            palette_lookup_quad_q <= palette_stage_quad_q;
            palette_lookup_lane_mask_q <= palette_stage_lane_mask_q;
            palette_lookup_front_q <= palette_stage_front_q;
            palette_lookup_collision_q <= palette_stage_collision_q;
            palette_lookup_last_q <= palette_stage_last_q;
            palette_lookup_sprite_index_q <= palette_stage_sprite_index_q;
            palette_lookup_transparent_index_q <=
                palette_stage_transparent_index_q;
            palette_lookup_opacity_q <= palette_stage_opacity_q;
            palette_lookup_index0_q <= palette_stage_index0_q;
            palette_lookup_index1_q <= palette_stage_index1_q;
            palette_lookup_index2_q <= palette_stage_index2_q;
            palette_lookup_index3_q <= palette_stage_index3_q;
        end

        if (palette_lookup_valid_q) begin
            palette_capture_quad_q <= palette_lookup_quad_q;
            palette_capture_lane_mask_q <= palette_lookup_lane_mask_q;
            palette_capture_front_q <= palette_lookup_front_q;
            palette_capture_collision_q <= palette_lookup_collision_q;
            palette_capture_last_q <= palette_lookup_last_q;
            palette_capture_sprite_index_q <=
                palette_lookup_sprite_index_q;
            palette_capture_transparent_index_q <=
                palette_lookup_transparent_index_q;
            palette_capture_opacity_q <= palette_lookup_opacity_q;
            palette_capture_index0_q <= palette_lookup_index0_q;
            palette_capture_index1_q <= palette_lookup_index1_q;
            palette_capture_index2_q <= palette_lookup_index2_q;
            palette_capture_index3_q <= palette_lookup_index3_q;
            palette_capture_argb0_q <= palette0_read_argb;
            palette_capture_argb1_q <= palette1_read_argb;
            palette_capture_argb2_q <= palette2_read_argb;
            palette_capture_argb3_q <= palette3_read_argb;
            palette_capture_destination0_q <= palette_lookup_front_q ?
                working_front_read0 : working_behind_read0;
            palette_capture_destination1_q <= palette_lookup_front_q ?
                working_front_read1 : working_behind_read1;
            palette_capture_destination2_q <= palette_lookup_front_q ?
                working_front_read2 : working_behind_read2;
            palette_capture_destination3_q <= palette_lookup_front_q ?
                working_front_read3 : working_behind_read3;
            palette_capture_occupancy0_q <= occupancy_read0;
            palette_capture_occupancy1_q <= occupancy_read1;
            palette_capture_occupancy2_q <= occupancy_read2;
            palette_capture_occupancy3_q <= occupancy_read3;
        end

        if (palette_capture_valid_q) begin
            palette_color_quad_q <= palette_capture_quad_q;
            palette_color_lane_mask_q <= palette_capture_lane_mask_q;
            palette_color_front_q <= palette_capture_front_q;
            palette_color_collision_q <= palette_capture_collision_q;
            palette_color_last_q <= palette_capture_last_q;
            palette_color_sprite_index_q <=
                palette_capture_sprite_index_q;
            palette_color_transparent_index_q <=
                palette_capture_transparent_index_q;
            palette_color_opacity_q <= palette_capture_opacity_q;
            palette_color_index0_q <= palette_capture_index0_q;
            palette_color_index1_q <= palette_capture_index1_q;
            palette_color_index2_q <= palette_capture_index2_q;
            palette_color_index3_q <= palette_capture_index3_q;
            palette_color_argb0_q <= palette_capture_argb0_q;
            palette_color_argb1_q <= palette_capture_argb1_q;
            palette_color_argb2_q <= palette_capture_argb2_q;
            palette_color_argb3_q <= palette_capture_argb3_q;
            palette_color_destination0_q <=
                palette_capture_destination0_q;
            palette_color_destination1_q <=
                palette_capture_destination1_q;
            palette_color_destination2_q <=
                palette_capture_destination2_q;
            palette_color_destination3_q <=
                palette_capture_destination3_q;
            palette_color_occupancy0_q <= palette_capture_occupancy0_q;
            palette_color_occupancy1_q <= palette_capture_occupancy1_q;
            palette_color_occupancy2_q <= palette_capture_occupancy2_q;
            palette_color_occupancy3_q <= palette_capture_occupancy3_q;
        end

        if (palette_color_valid_q) begin
            collision_qualify_apply_q <= {
                palette_color_collision_q && palette_apply3,
                palette_color_collision_q && palette_apply2,
                palette_color_collision_q && palette_apply1,
                palette_color_collision_q && palette_apply0
            };
            collision_qualify_quad_q <= palette_color_quad_q;
            collision_qualify_sprite_index_q <=
                palette_color_sprite_index_q;
            collision_qualify_alpha0_q <= collision_alpha0;
            collision_qualify_alpha1_q <= collision_alpha1;
            collision_qualify_alpha2_q <= collision_alpha2;
            collision_qualify_alpha3_q <= collision_alpha3;
            collision_qualify_occupancy0_q <= palette_color_occupancy0_q;
            collision_qualify_occupancy1_q <= palette_color_occupancy1_q;
            collision_qualify_occupancy2_q <= palette_color_occupancy2_q;
            collision_qualify_occupancy3_q <= palette_color_occupancy3_q;
        end

        if (collision_qualify_valid_q) begin
            collision_stage_apply_q <= {
                collision_qualify_apply_q[3] &&
                    collision_qualify_alpha3_q >= 16'd128,
                collision_qualify_apply_q[2] &&
                    collision_qualify_alpha2_q >= 16'd128,
                collision_qualify_apply_q[1] &&
                    collision_qualify_alpha1_q >= 16'd128,
                collision_qualify_apply_q[0] &&
                    collision_qualify_alpha0_q >= 16'd128
            };
            collision_stage_quad_q <= collision_qualify_quad_q;
            collision_stage_sprite_index_q <=
                collision_qualify_sprite_index_q;
            collision_stage_occupancy0_q <=
                collision_qualify_occupancy0_q;
            collision_stage_occupancy1_q <=
                collision_qualify_occupancy1_q;
            collision_stage_occupancy2_q <=
                collision_qualify_occupancy2_q;
            collision_stage_occupancy3_q <=
                collision_qualify_occupancy3_q;
        end

        blend_quad_pipe[0] <= palette_color_quad_q;
        blend_mask_pipe[0] <= palette_color_lane_mask_q;
        blend_front_pipe[0] <= palette_color_front_q;
        blend_last_pipe[0] <= palette_color_last_q;
        for (blend_pipe_index = 1; blend_pipe_index < 8;
             blend_pipe_index = blend_pipe_index + 1) begin
            blend_quad_pipe[blend_pipe_index] <=
                blend_quad_pipe[blend_pipe_index - 1];
            blend_mask_pipe[blend_pipe_index] <=
                blend_mask_pipe[blend_pipe_index - 1];
            blend_front_pipe[blend_pipe_index] <=
                blend_front_pipe[blend_pipe_index - 1];
            blend_last_pipe[blend_pipe_index] <=
                blend_last_pipe[blend_pipe_index - 1];
        end
    end

    always @(posedge build_clk) begin
        collision_read_data <= collision_published_read;
    end

    always @(posedge build_clk) begin
        if (build_reset) begin
            state <= S_IDLE;
            prep_state <= P_IDLE;
            render_state <= R_IDLE;
            busy <= 1'b0;
            done <= 1'b0;
            line_complete <= 1'b0;
            completed_slot <= 2'd0;
            slot_valid <= 4'd0;
            fetch_error <= 1'b0;
            deadline_error <= 1'b0;
            build_cycles_q <= {BUILD_COUNTER_WIDTH{1'b0}};
            max_build_cycles_q <= {BUILD_COUNTER_WIDTH{1'b0}};
            axi_error_count <= 32'd0;
            deadline_error_count <= 32'd0;
            read_bytes <= 32'd0;
            overflow_bitmap <= 64'd0;
            overflow_line <= 10'd0;
            overflow_count <= 32'd0;
            pixels_admitted <= 32'd0;
            pixels_dropped <= 32'd0;
            collision_frame <= 32'd0;
            collision_event <= 1'b0;
            collision_any_current_q <= 1'b0;
            collision_current_frame_q <= 32'd0;
            collision_rotate_frame_q <= 1'b0;
            build_slot_q <= 2'd0;
            line_y_q <= 10'd0;
            clear_quad_q <= 9'd0;
            clear_active_q <= 1'b0;
            admission_position_q <= 6'd0;
            order_position_q <= 6'd63;
            budget_remaining_q <= PIXEL_BUDGET;
            admitted_count_q <= 7'd0;
            line_overflow_q <= 64'd0;
            admission_sprite_index_q <= 6'd0;
            admission_candidate_q <= 1'b0;
            admission_accept_q <= 1'b0;
            admission_write_q <= 1'b0;
            admission_x_q <= 17'sd0;
            admission_right_signed_q <= 18'sd0;
            admission_left_q <= 11'd0;
            admission_right_q <= 11'd0;
            admission_span_q <= 11'd0;
            admission_destination_offset_q <= 11'd0;
            admission_line_delta_q <= 11'd0;
            admission_line_delta_full_q <= 18'sd0;
            admission_destination_height_q <= 11'd0;
            admission_enabled_q <= 1'b0;
            buffer_ready_q <= 4'd0;
            buffer_fetch_busy_q <= 4'd0;
            buffer_render_busy_q <= 4'd0;
            prep_remaining_q <= 7'd0;
            prep_list_position_q <= 6'd0;
            prep_launch_q <= 1'b0;
            prep_slot_q <= 2'd0;
            prep_divider_bit_q <= 5'd0;
            prep_destination_y_q <= 11'd0;
            prep_y_dividend_q <= 18'd0;
            prep_y_remainder_q <= 12'd0;
            prep_y_quotient_q <= 18'd0;
            prep_phase_multiplier_q <= 11'd0;
            prep_phase_multiplicand_q <= 43'd0;
            prep_phase_x_q <= 43'd0;
            prep_y_numerator_q <= 18'd0;
            prep_source_y_q <= 8'd0;
            prep_row_offset_q <= 21'd0;
            prep_beats_remaining_q <= 6'd0;
            prep_issue_address_q <= 32'd0;
            prep_burst_beats_q <= 5'd0;
            prep_row_word_q <= 5'd0;
            prep_arvalid_q <= 1'b0;
            request_write_ptr_q <= 3'd0;
            request_read_ptr_q <= 3'd0;
            request_count_q <= 4'd0;
            response_active_q <= 1'b0;
            response_slot_q <= 2'd0;
            response_start_word_q <= 5'd0;
            response_burst_beats_q <= 5'd0;
            response_last_for_sprite_q <= 1'b0;
            response_beat_q <= 5'd0;
            row_write_valid_q <= 1'b0;
            row_write_address_q <= 6'd0;
            row_write_data_q <= 64'd0;
            response_publish_valid_q <= 1'b0;
            response_publish_slot_q <= 2'd0;
            abort_drain_q <= 1'b0;
            render_remaining_sprites_q <= 7'd0;
            render_slot_q <= 2'd0;
            render_screen_x_q <= 11'd0;
            render_pixels_remaining_q <= 11'd0;
            render_phase_x_q <= 32'd0;
            render_scale_step1_x_q <= 32'd0;
            render_scale_step2_x_q <= 32'd0;
            render_scale_step3_x_q <= 32'd0;
            render_scale_step4_x_q <= 32'd0;
            render_overlap_q <= 64'd0;
            collision_hits_q <= 64'd0;
            collision_hit_groups_q <= 8'd0;
            collision_update_step_q <= 4'd0;
            collision_update_sprite_q <= 6'd0;
            collision_update_busy_q <= 1'b0;
            collision_bank_update_valid_q <= 1'b0;
            collision_bank_update_row_q <= 3'd0;
            for (collision_mask_index = 0; collision_mask_index < 8;
                 collision_mask_index = collision_mask_index + 1)
                collision_bank_update_mask_q[collision_mask_index] <= 64'd0;
            copy_valid_q <= 1'b0;
            copy_quad_q <= 9'd0;
            copy_active_q <= 1'b0;
            copy_read_quad_q <= 9'd0;
        end else begin
            done <= 1'b0;
            line_complete <= 1'b0;
            collision_event <= 1'b0;
            copy_valid_q <= 1'b0;
            collision_bank_update_valid_q <= 1'b0;
            admission_write_q <= 1'b0;
            row_write_valid_q <= 1'b0;
            response_publish_valid_q <= 1'b0;
            prep_launch_q <= !abort_drain_q && prep_state == P_IDLE &&
                state == S_RUN && prep_remaining_q != 7'd0 &&
                !buffer_ready_q[prep_slot_q] &&
                !buffer_fetch_busy_q[prep_slot_q] &&
                !buffer_render_busy_q[prep_slot_q];

            if (collision_update_busy_q) begin
                if (collision_update_step_q < 4'd8) begin
                    collision_bank_update_valid_q <= 1'b1;
                    collision_bank_update_row_q <=
                        collision_update_step_q[2:0];
                    for (collision_mask_index = 0;
                         collision_mask_index < 8;
                         collision_mask_index = collision_mask_index + 1)
                        collision_bank_update_mask_q[
                            collision_mask_index] <= collision_bank_mask(
                                collision_mask_index[2:0],
                                collision_update_step_q[2:0],
                                collision_update_sprite_q,
                                collision_hits_q);
                end
                if (collision_update_step_q == 4'd9)
                    collision_update_busy_q <= 1'b0;
                else
                    collision_update_step_q <=
                        collision_update_step_q + 4'd1;
            end

            if (busy)
                build_cycles_q <= build_cycles_q + BUILD_COUNTER_ONE;

            // Working-line RAM and descriptor-store accesses are independent.
            // Clear beside the fixed admission walk instead of serializing a
            // full 1280-pixel clear ahead of every line.
            if (clear_active_q) begin
                if (clear_quad_q == QUADS - 1)
                    clear_active_q <= 1'b0;
                else
                    clear_quad_q <= clear_quad_q + 9'd1;
            end

            // These DSP outputs run continuously so their clock-enable paths
            // cannot become timing-critical state-decode cones.
            prep_y_numerator_q <=
                prep_destination_y_q * prep_source_height_q;
            prep_row_offset_q <= prep_source_y_q * prep_pitch_q;

            if (prep_state == P_RECORD) begin
                prep_sprite_index_q <= list_record_index;
                prep_screen_x_q <= list_record_screen_x;
                prep_span_q <= list_record_span;
            end

            if (prep_state == P_METADATA_WAIT) begin
                prep_base_q <= descriptor_word4;
                prep_pitch_q <= descriptor_word5[12:0];
                prep_source_width_q <= descriptor_word2[7:0];
                prep_source_height_q <= descriptor_word2[15:8];
                prep_scale_step_x_q <= descriptor_scale_step_x;
                prep_palette_bank_q <= descriptor_word0[19:16];
                prep_transparent_index_q <= descriptor_word0[27:20];
                prep_opacity_q <= descriptor_word2[23:16];
                prep_flags_q <= descriptor_word0[5:2];
                prep_compatible_q <= descriptor_collision_compatible;
                prep_destination_y_q <= prep_line_delta[10:0];
                prep_phase_multiplier_q <= prep_destination_offset[10:0];
                prep_phase_multiplicand_q <= {
                    11'd0, descriptor_scale_step_x
                };
                prep_phase_x_q <= 43'd0;
                prep_y_denominator_q <= descriptor_word3[26:16];
                prep_y_remainder_q <= 12'd0;
                prep_y_quotient_q <= 18'd0;
                prep_divider_bit_q <= 5'd17;
            end

            // AXI response metadata is staged before accepting data. The
            // following beat pipeline keeps the request FIFO read pointer and
            // AXI decode out of the replicated row-memory write-enable cone.
            if (response_dispatch) begin
                response_active_q <= 1'b1;
                response_slot_q <= request_slot[request_read_ptr_q];
                response_start_word_q <=
                    request_start_word[request_read_ptr_q];
                response_burst_beats_q <=
                    request_beats[request_read_ptr_q];
                response_last_for_sprite_q <=
                    request_last_for_sprite[request_read_ptr_q];
                response_beat_q <= 5'd0;
            end

            if (row_write_valid_q) begin
                row_rep0[row_write_address_q] <= row_write_data_q;
                row_rep1[row_write_address_q] <= row_write_data_q;
                row_rep2[row_write_address_q] <= row_write_data_q;
                row_rep3[row_write_address_q] <= row_write_data_q;
            end

            if (response_publish_valid_q && !abort_drain_q) begin
                buffer_fetch_busy_q[response_publish_slot_q] <= 1'b0;
                buffer_ready_q[response_publish_slot_q] <= 1'b1;
            end

            if (prep_ar_accept) begin
                request_slot[request_write_ptr_q] <= prep_slot_q;
                request_start_word[request_write_ptr_q] <= prep_row_word_q;
                request_beats[request_write_ptr_q] <= prep_burst_beats_q;
                request_last_for_sprite[request_write_ptr_q] <=
                    prep_beats_remaining_q ==
                        {1'b0, prep_burst_beats_q};
                request_write_ptr_q <= request_write_ptr_q + 3'd1;
                read_bytes <= read_bytes +
                    ({27'd0, prep_burst_beats_q} << 3);
            end

            if (response_burst_complete)
                request_read_ptr_q <= request_read_ptr_q + 3'd1;

            case ({prep_ar_accept, response_burst_complete})
                2'b10: request_count_q <= request_count_q + 4'd1;
                2'b01: request_count_q <= request_count_q - 4'd1;
                default: begin end
            endcase

            if (abort_drain_q && prep_ar_accept)
                prep_arvalid_q <= 1'b0;

            if (prep_response_accept) begin
                if (response_beat_q < response_burst_beats_q) begin
                    row_write_valid_q <= 1'b1;
                    row_write_address_q <= {
                        response_slot_q, response_row_word[3:0]
                    };
                    row_write_data_q <= m_axi_rdata;
                end
                if (m_axi_rid != AXI_ID || m_axi_rresp != 2'b00 ||
                    m_axi_rlast != response_expected_last) begin
                    fetch_error <= 1'b1;
                    axi_error_count <= axi_error_count + 32'd1;
                end
                if (m_axi_rlast) begin
                    response_active_q <= 1'b0;
                    response_beat_q <= 5'd0;
                    if (response_last_for_sprite_q && !abort_drain_q) begin
                        response_publish_valid_q <= 1'b1;
                        response_publish_slot_q <= response_slot_q;
                    end
                end else if (response_beat_q < response_burst_beats_q) begin
                    response_beat_q <= response_beat_q + 5'd1;
                end
            end

            if (collision_stage_valid_q) begin
                render_overlap_q <= render_overlap_q |
                    (collision_stage_apply_q[0] ?
                        collision_stage_occupancy0_q : 64'd0) |
                    (collision_stage_apply_q[1] ?
                        collision_stage_occupancy1_q : 64'd0) |
                    (collision_stage_apply_q[2] ?
                        collision_stage_occupancy2_q : 64'd0) |
                    (collision_stage_apply_q[3] ?
                        collision_stage_occupancy3_q : 64'd0);
            end

            case (prep_state)
                P_IDLE: begin
                    if (!abort_drain_q)
                        prep_arvalid_q <= 1'b0;
                    if (prep_launch_q) begin
                        prep_list_position_q <= prep_remaining_q - 7'd1;
                        prep_state <= P_LOAD;
                    end
                end
                P_LOAD: begin
                    prep_state <= P_RECORD;
                end
                P_RECORD: begin
                    prep_state <= P_METADATA;
                end
                P_METADATA: begin
                    prep_state <= P_METADATA_WAIT;
                end
                P_METADATA_WAIT: begin
                    prep_state <= P_METADATA_DSP_WAIT;
                end
                P_METADATA_DSP_WAIT: begin
                    prep_state <= P_DIVIDE_LOAD;
                end
                P_DIVIDE_LOAD: begin
                    prep_y_dividend_q <= prep_y_numerator_q;
                    prep_state <= P_DIVIDE;
                end
                P_DIVIDE: begin
                    if (prep_phase_multiplier_q[0])
                        prep_phase_x_q <= prep_phase_x_q +
                                          prep_phase_multiplicand_q;
                    prep_phase_multiplier_q <= {
                        1'b0, prep_phase_multiplier_q[10:1]
                    };
                    prep_phase_multiplicand_q <= {
                        prep_phase_multiplicand_q[41:0], 1'b0
                    };
                    prep_y_dividend_q <= {
                        prep_y_dividend_q[16:0], 1'b0
                    };
                    prep_y_remainder_q <= prep_y_remainder_next;
                    prep_y_quotient_q <= {
                        prep_y_quotient_q[16:0], prep_y_subtract
                    };
                    if (prep_divider_bit_q == 5'd0) begin
                        prep_state <= P_SOURCE_Y;
                    end else begin
                        prep_divider_bit_q <= prep_divider_bit_q - 5'd1;
                    end
                end
                P_SOURCE_Y: begin
                    prep_source_y_q <= prep_flags_q[1] ?
                        prep_source_height_q - 8'd1 -
                            prep_y_quotient_q[7:0] :
                        prep_y_quotient_q[7:0];
                    prep_state <= P_ADDRESS;
                end
                P_ADDRESS: begin
                    prep_beats_remaining_q <=
                        ({1'b0, prep_source_width_q} + 9'd7) >> 3;
                    prep_row_word_q <= 5'd0;
                    prep_state <= P_ROW_ADDRESS;
                end
                P_ROW_ADDRESS: begin
                    prep_row_address_q <= prep_base_q +
                        {11'd0, prep_row_offset_q};
                    prep_issue_address_q <= prep_base_q +
                        {11'd0, prep_row_offset_q};
                    buffer_sprite_index[prep_slot_q] <= prep_sprite_index_q;
                    buffer_screen_x[prep_slot_q] <= prep_screen_x_q;
                    buffer_span[prep_slot_q] <= prep_span_q;
                    buffer_source_width[prep_slot_q] <= prep_source_width_q;
                    buffer_scale_step_x[prep_slot_q] <= prep_scale_step_x_q;
                    buffer_phase_x[prep_slot_q] <= prep_phase_x_q[31:0];
                    buffer_palette_bank[prep_slot_q] <= prep_palette_bank_q;
                    buffer_transparent_index[prep_slot_q] <=
                        prep_transparent_index_q;
                    buffer_opacity[prep_slot_q] <= prep_opacity_q;
                    buffer_flags[prep_slot_q] <= prep_flags_q;
                    buffer_compatible[prep_slot_q] <= prep_compatible_q;
                    buffer_fetch_busy_q[prep_slot_q] <= 1'b1;
                    prep_state <= P_SETUP;
                end
                P_SETUP: begin
                    if (request_count_q != 4'd8) begin
                        prep_burst_beats_q <= prep_selected_burst;
                        prep_arvalid_q <= 1'b1;
                        prep_state <= P_BURST;
                    end
                end
                P_BURST: begin
                    if (prep_ar_accept) begin
                        prep_arvalid_q <= 1'b0;
                        if (abort_drain_q) begin
                            prep_state <= P_IDLE;
                        end else if (prep_beats_remaining_q ==
                            {1'b0, prep_burst_beats_q}) begin
                            prep_remaining_q <= prep_remaining_q - 7'd1;
                            prep_slot_q <= prep_slot_q + 2'd1;
                            prep_state <= P_IDLE;
                        end else begin
                            prep_beats_remaining_q <=
                                prep_beats_remaining_q - prep_burst_beats_q;
                            prep_issue_address_q <= prep_issue_address_q +
                                ({27'd0, prep_burst_beats_q} << 3);
                            prep_row_word_q <= prep_row_word_q +
                                prep_burst_beats_q;
                            prep_state <= P_SETUP;
                        end
                    end
                end
                default: prep_state <= P_IDLE;
            endcase

            case (render_state)
                R_IDLE: begin
                    // Slot payload may be sampled before its ready bit is
                    // published; it is not consumed until R_ISSUE. Keeping
                    // this preload unconditional prevents indexed readiness
                    // from becoming the payload-register clock enable.
                    render_sprite_index_q <=
                        buffer_sprite_index[render_slot_q];
                    render_screen_x_q <= buffer_screen_x[render_slot_q];
                    render_pixels_remaining_q <= buffer_span[render_slot_q];
                    render_source_width_q <=
                        buffer_source_width[render_slot_q];
                    render_phase_x_q <= buffer_phase_x[render_slot_q];
                    render_scale_step1_x_q <=
                        buffer_scale_step_x[render_slot_q];
                    render_scale_step2_x_q <=
                        buffer_scale_step_x[render_slot_q] << 1;
                    render_scale_step3_x_q <=
                        buffer_scale_step_x[render_slot_q] +
                        (buffer_scale_step_x[render_slot_q] << 1);
                    render_scale_step4_x_q <=
                        buffer_scale_step_x[render_slot_q] << 2;
                    render_palette_bank_q <=
                        buffer_palette_bank[render_slot_q];
                    render_transparent_index_q <=
                        buffer_transparent_index[render_slot_q];
                    render_opacity_q <= buffer_opacity[render_slot_q];
                    render_flags_q <= buffer_flags[render_slot_q];
                    render_compatible_q <=
                        buffer_compatible[render_slot_q];
                    render_overlap_q <= 64'd0;
                    if (state == S_RUN && render_remaining_sprites_q != 7'd0 &&
                        buffer_ready_q[render_slot_q]) begin
                        buffer_ready_q[render_slot_q] <= 1'b0;
                        buffer_render_busy_q[render_slot_q] <= 1'b1;
                        render_state <= R_ISSUE;
                    end
                end
                R_ISSUE: begin
                    if (render_issue) begin
                        render_screen_x_q <= {
                            render_screen_x_q[10:2] + 9'd1, 2'b00
                        };
                        if (render_group_last)
                            render_pixels_remaining_q <= 11'd0;
                        else
                            render_pixels_remaining_q <=
                                render_pixels_remaining_q -
                                {8'd0, render_lane_capacity};
                        render_phase_x_q <= render_phase_boundary_x;
                        if (render_group_last)
                            render_state <= R_WAIT;
                    end
                end
                R_WAIT: begin
                    if (blend_output_valid && blend_last_pipe[7]) begin
                        if (render_flags_q[3]) begin
                            collision_hits_q <= render_collision_hits;
                            collision_hit_groups_q <= {
                                |render_collision_hits[63:56],
                                |render_collision_hits[55:48],
                                |render_collision_hits[47:40],
                                |render_collision_hits[39:32],
                                |render_collision_hits[31:24],
                                |render_collision_hits[23:16],
                                |render_collision_hits[15:8],
                                |render_collision_hits[7:0]
                            };
                            render_state <= R_COLLISION;
                        end else begin
                            render_state <= R_COMPLETE;
                        end
                    end
                end
                R_COLLISION: begin
                    if (collision_hit_groups_q == 8'd0) begin
                        render_state <= R_COMPLETE;
                    end else if (!collision_update_busy_q) begin
                        collision_any_current_q <= 1'b1;
                        collision_update_sprite_q <= render_sprite_index_q;
                        collision_update_step_q <= 4'd0;
                        collision_update_busy_q <= 1'b1;
                        render_state <= R_COMPLETE;
                    end
                end
                R_COLLISION_DRAIN: begin
                    render_state <= R_COMPLETE;
                end
                R_COMPLETE: begin
                    buffer_render_busy_q[render_slot_q] <= 1'b0;
                    render_remaining_sprites_q <=
                        render_remaining_sprites_q - 7'd1;
                    render_slot_q <= render_slot_q + 2'd1;
                    render_state <= R_IDLE;
                end
                default: render_state <= R_IDLE;
            endcase

            case (state)
                S_IDLE: begin
                    if (start) begin
                        if (abort_drain_q || request_count_q != 4'd0 ||
                            response_active_q || row_write_valid_q ||
                            response_publish_valid_q || prep_arvalid_q) begin
                            done <= 1'b1;
                            line_complete <= 1'b0;
                            fetch_error <= 1'b1;
                            deadline_error <= 1'b1;
                            completed_slot <= build_slot;
                            slot_valid[build_slot] <= 1'b0;
                        end else begin
                            busy <= 1'b1;
                            fetch_error <= 1'b0;
                            deadline_error <= 1'b0;
                            build_cycles_q <= {BUILD_COUNTER_WIDTH{1'b0}};
                            read_bytes <= 32'd0;
                            build_slot_q <= build_slot;
                            completed_slot <= build_slot;
                            slot_valid[build_slot] <= 1'b0;
                            line_y_q <= line_y;
                            collision_rotate_frame_q <= line_y == 10'd0;
                            clear_quad_q <= 9'd0;
                            clear_active_q <= 1'b1;
                            admission_position_q <= 6'd0;
                            order_position_q <= 6'd63;
                            budget_remaining_q <= PIXEL_BUDGET;
                            admitted_count_q <= 7'd0;
                            line_overflow_q <= 64'd0;
                            buffer_ready_q <= 4'd0;
                            buffer_fetch_busy_q <= 4'd0;
                            buffer_render_busy_q <= 4'd0;
                            copy_active_q <= 1'b0;
                            prep_state <= P_IDLE;
                            render_state <= R_IDLE;
                            state <= S_ORDER;
                            if (line_y == 10'd0) begin
                                collision_frame <= collision_current_frame_q;
                                collision_current_frame_q <=
                                    collision_current_frame_q + 32'd1;
                                collision_event <= collision_any_current_q;
                                collision_any_current_q <= 1'b0;
                            end
                        end
                    end
                end
                S_CLEAR: begin
                    order_position_q <= 6'd63;
                    state <= S_ORDER;
                end
                S_ORDER: begin
                    order_position_q <= order_position_q - 6'd1;
                    state <= S_DESCRIPTOR;
                end
                S_DESCRIPTOR: state <= S_GEOMETRY;
                S_GEOMETRY: begin
                    admission_sprite_index_q <= order_read_index;
                    admission_x_q <= admission_x;
                    admission_left_q <= admission_left;
                    admission_right_signed_q <= admission_right_signed;
                    admission_line_delta_q <= admission_line_delta[10:0];
                    admission_line_delta_full_q <= admission_line_delta;
                    admission_destination_height_q <=
                        admission_destination_height;
                    admission_enabled_q <=
                        descriptor_word0[0] && descriptor_word0[1];
                    state <= S_CLIP;
                end
                S_CLIP: begin
                    admission_right_q <= admission_right;
                    state <= S_SPAN;
                end
                S_SPAN: begin
                    admission_span_q <= admission_span;
                    admission_destination_offset_q <=
                        admission_destination_offset;
                    state <= S_EVALUATE;
                end
                S_EVALUATE: begin
                    admission_candidate_q <= admission_candidate;
                    admission_accept_q <= admission_candidate && admission_fits;
                    admission_write_q <= admission_candidate && admission_fits;
                    if (admission_position_q != 6'd63 &&
                        order_position_q != 6'd0)
                        order_position_q <= order_position_q - 6'd1;
                    state <= S_ADMIT;
                end
                S_ADMIT: begin
                    if (admission_candidate_q) begin
                        if (admission_accept_q) begin
                            admitted_count_q <= admitted_count_q + 7'd1;
                            budget_remaining_q <= budget_remaining_q -
                                admission_span_q;
                            pixels_admitted <= pixels_admitted +
                                admission_span_q;
                        end else begin
                            line_overflow_q[admission_sprite_index_q] <= 1'b1;
                            pixels_dropped <= pixels_dropped + admission_span_q;
                        end
                    end
                    if (admission_position_q == 6'd63) begin
                        prep_remaining_q <= admitted_count_q +
                            (admission_accept_q ? 7'd1 : 7'd0);
                        render_remaining_sprites_q <= admitted_count_q +
                            (admission_accept_q ? 7'd1 : 7'd0);
                        prep_slot_q <= 2'd0;
                        render_slot_q <= 2'd0;
                        prep_state <= P_IDLE;
                        render_state <= R_IDLE;
                        state <= clear_active_q ? S_WAIT_CLEAR : S_RUN;
                    end else begin
                        admission_position_q <= admission_position_q + 6'd1;
                        state <= S_GEOMETRY;
                    end
                end
                S_WAIT_CLEAR: begin
                    if (!clear_active_q)
                        state <= S_RUN;
                end
                S_RUN: begin
                    if (prep_remaining_q == 7'd0 &&
                        render_remaining_sprites_q == 7'd0 &&
                        prep_state == P_IDLE && render_state == R_IDLE &&
                        request_count_q == 4'd0 &&
                        !response_active_q && !row_write_valid_q &&
                        !response_publish_valid_q &&
                        buffer_ready_q == 4'd0 &&
                        buffer_fetch_busy_q == 4'd0 &&
                        buffer_render_busy_q == 4'd0 &&
                        !collision_update_busy_q) begin
                        copy_valid_q <= 1'b0;
                        state <= S_COPY;
                    end
                end
                S_COPY: begin
                    if (!copy_active_q) begin
                        copy_active_q <= 1'b1;
                        copy_read_quad_q <= 9'd0;
                    end else begin
                        copy_valid_q <= 1'b1;
                        copy_quad_q <= copy_read_quad_q;
                        if (copy_read_quad_q == QUADS - 1)
                            state <= S_COPY_DRAIN;
                        else
                            copy_read_quad_q <= copy_read_quad_q + 9'd1;
                    end
                end
                S_COPY_DRAIN: begin
                    copy_valid_q <= 1'b0;
                    copy_active_q <= 1'b0;
                    state <= S_COPY_DRAIN_2;
                end
                S_COPY_DRAIN_2: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    completed_slot <= build_slot_q;
                    overflow_bitmap <= line_overflow_q;
                    overflow_line <= line_y_q;
                    if (line_overflow_q != 64'd0)
                        overflow_count <= overflow_count + 32'd1;
                    if (!fetch_error && !deadline_error) begin
                        slot_valid[build_slot_q] <= 1'b1;
                        line_complete <= 1'b1;
                    end
                    if (build_cycles_q >= max_build_cycles_q)
                        max_build_cycles_q <= completed_build_cycles;
                    state <= S_IDLE;
                end
                default: state <= S_IDLE;
            endcase

            if (busy && build_cycles_q == MAX_BUILD_COUNT - BUILD_COUNTER_ONE) begin
                busy <= 1'b0;
                done <= 1'b1;
                fetch_error <= 1'b1;
                deadline_error <= 1'b1;
                deadline_error_count <= deadline_error_count + 32'd1;
                if (MAX_BUILD_COUNT > max_build_cycles_q)
                    max_build_cycles_q <= MAX_BUILD_COUNT;
                slot_valid[build_slot_q] <= 1'b0;
                clear_active_q <= 1'b0;
                buffer_ready_q <= 4'd0;
                buffer_fetch_busy_q <= 4'd0;
                buffer_render_busy_q <= 4'd0;
                collision_update_busy_q <= 1'b0;
                abort_drain_q <= 1'b1;
                if (!prep_arvalid_q)
                    prep_state <= P_IDLE;
                render_state <= R_IDLE;
                state <= S_IDLE;
            end

            if (abort_drain_q && request_count_q == 4'd0 &&
                !response_active_q && !row_write_valid_q &&
                !response_publish_valid_q && !prep_arvalid_q)
                abort_drain_q <= 1'b0;
        end
    end

    wire unused_descriptor_word6 = ^descriptor_word6;
    wire unused_prep_row_address = ^prep_row_address_q;
    wire unused_blend_valids = &{1'b1, blend_valid1, blend_valid2,
                                blend_valid3};
endmodule

module astra_sprite_work_ram #(
    parameter integer DATA_WIDTH = 32,
    parameter integer DEPTH = 320,
    parameter integer ADDRESS_WIDTH = 9
) (
    input  wire                     clk,
    input  wire                     write_enable,
    input  wire [ADDRESS_WIDTH-1:0] write_address,
    input  wire [DATA_WIDTH-1:0]    write_data,
    input  wire [ADDRESS_WIDTH-1:0] read_address,
    output reg  [DATA_WIDTH-1:0]    read_data
);
    (* ram_style = "block" *) reg [DATA_WIDTH-1:0] working_memory [0:DEPTH-1];

    always @(posedge clk) begin
        if (write_enable)
            working_memory[write_address] <= write_data;
        read_data <= working_memory[read_address];
    end
endmodule

// One of eight interleaved 8-row collision banks. Row N is stored in bank
// N[2:0] at address N[5:3], allowing all eight symmetric row updates to occur
// in parallel without a 4096-bit dynamically addressed register matrix.
module astra_sprite_collision_bank (
    input  wire        clk,
    input  wire        reset,
    input  wire        update_enable,
    input  wire [2:0]  update_row,
    input  wire [63:0] update_mask,
    input  wire        rotate_enable,
    input  wire [2:0]  rotate_row,
    input  wire [2:0]  published_read_row,
    output reg  [63:0] published_read_data
);
    (* ram_style = "block" *) reg [63:0] current [0:7];
    (* ram_style = "block" *) reg [63:0] published [0:7];
    reg command_valid_q;
    reg command_rotate_q;
    (* keep = "true" *) reg [2:0] command_row_q;
    reg [63:0] command_mask_q;
    integer initialize_row;

    initial begin
        for (initialize_row = 0; initialize_row < 8;
             initialize_row = initialize_row + 1) begin
            current[initialize_row] = 64'd0;
            published[initialize_row] = 64'd0;
        end
    end

    always @(posedge clk) begin
        published_read_data <= published[published_read_row];
    end

    always @(posedge clk) begin
        if (reset) begin
            command_valid_q <= 1'b0;
        end else begin
            if (command_valid_q) begin
                if (command_rotate_q) begin
                    published[command_row_q] <= current[command_row_q];
                    current[command_row_q] <= 64'd0;
                end else begin
                    current[command_row_q] <=
                        current[command_row_q] | command_mask_q;
                end
            end

            command_valid_q <= rotate_enable || update_enable;
        end
    end

    // command_valid_q flushes stale payload after reset.
    always @(posedge clk) begin
        command_rotate_q <= rotate_enable;
        command_row_q <= rotate_enable ? rotate_row : update_row;
        command_mask_q <= update_mask;
    end
endmodule

`default_nettype wire
