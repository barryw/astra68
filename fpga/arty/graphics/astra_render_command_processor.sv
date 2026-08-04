// Copyright (c) 2026 Astra68 contributors
//
// Astraea v1 command transport and rendering-engine integration. Submission and
// completion records remain in the reserved DDR arena. This block validates
// the complete command and its surface descriptors before dispatching any
// pixel DMA and never overwrites an unread completion.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_command_processor #(
    parameter [31:0] ARENA_BASE = 32'h18000000,
    parameter [31:0] ARENA_LIMIT = 32'h20000000,
    parameter integer AXI_ID_WIDTH = 6,
    parameter integer CYCLES_PER_US = 200,
    parameter integer RESET_HOLD_CYCLES = 16
) (
    input  wire                         clk,
    input  wire                         reset,
    input  wire                         enable,
    input  wire                         queue_rebase,
    input  wire                         soft_reset,
    input  wire [31:0]                  submission_ring_offset,
    input  wire [10:0]                  submission_producer,
    output reg  [10:0]                  submission_consumer,
    input  wire [31:0]                  completion_ring_offset,
    output reg  [10:0]                  completion_producer,
    input  wire [10:0]                  completion_consumer,
    input  wire [31:0]                  resource_generation,
    input  wire                         protected0_valid,
    input  wire [31:0]                  protected0_offset,
    input  wire [31:0]                  protected0_bytes,
    input  wire                         protected1_valid,
    input  wire [31:0]                  protected1_offset,
    input  wire [31:0]                  protected1_bytes,

    output reg                          busy,
    output reg                          completion_irq,
    output reg                          engine_reset_active,
    output reg                          configuration_fault,
    output reg  [31:0]                  retired_fence,
    output reg  [31:0]                  commands_submitted,
    output reg  [31:0]                  commands_completed,
    output reg  [31:0]                  commands_failed,
    output reg  [31:0]                  backpressure_cycles,
    output reg  [31:0]                  timeout_count,
    output reg  [31:0]                  reset_count,
    output reg  [31:0]                  last_fault_detail,

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

    output wire [AXI_ID_WIDTH-1:0]      m_axi_awid,
    output wire [31:0]                  m_axi_awaddr,
    output wire [7:0]                   m_axi_awlen,
    output wire [2:0]                   m_axi_awsize,
    output wire [1:0]                   m_axi_awburst,
    output wire [3:0]                   m_axi_awcache,
    output wire [2:0]                   m_axi_awprot,
    output wire [3:0]                   m_axi_awqos,
    output wire                         m_axi_awvalid,
    input  wire                         m_axi_awready,
    output wire [63:0]                  m_axi_wdata,
    output wire [7:0]                   m_axi_wstrb,
    output wire                         m_axi_wlast,
    output wire                         m_axi_wvalid,
    input  wire                         m_axi_wready,
    input  wire [AXI_ID_WIDTH-1:0]      m_axi_bid,
    input  wire [1:0]                   m_axi_bresp,
    input  wire                         m_axi_bvalid,
    output wire                         m_axi_bready
);
    localparam [AXI_ID_WIDTH-1:0] MANAGER_READ_ID =
        {{(AXI_ID_WIDTH-1){1'b0}}, 1'b0};
    localparam [AXI_ID_WIDTH-1:0] BLITTER_READ_ID =
        {{(AXI_ID_WIDTH-1){1'b0}}, 1'b1};
    localparam [AXI_ID_WIDTH-1:0] FLOOD_READ_ID =
        {{(AXI_ID_WIDTH-3){1'b0}}, 3'b100};
    localparam [AXI_ID_WIDTH-1:0] GLYPH_READ_ID =
        {{(AXI_ID_WIDTH-3){1'b0}}, 3'b101};
    localparam [AXI_ID_WIDTH-1:0] PIXEL_WRITE_ID =
        {{(AXI_ID_WIDTH-2){1'b0}}, 2'b10};
    localparam [AXI_ID_WIDTH-1:0] COMPLETION_WRITE_ID =
        {{(AXI_ID_WIDTH-2){1'b0}}, 2'b11};
    localparam [31:0] ARENA_BYTES = ARENA_LIMIT - ARENA_BASE;
    localparam [31:0] SUBMISSION_RING_BYTES =
        `ASTRA_RENDER_COMMAND_BYTES * `ASTRA_RENDER_RING_ENTRIES;
    localparam [31:0] COMPLETION_RING_BYTES =
        `ASTRA_RENDER_COMPLETION_BYTES * `ASTRA_RENDER_RING_ENTRIES;

    localparam [5:0] ST_IDLE = 6'd0;
    localparam [5:0] ST_COMMAND_AR = 6'd1;
    localparam [5:0] ST_COMMAND_R = 6'd2;
    localparam [5:0] ST_COMMON_VALIDATE = 6'd3;
    localparam [5:0] ST_DESTINATION_AR = 6'd4;
    localparam [5:0] ST_DESTINATION_R = 6'd5;
    localparam [5:0] ST_DESTINATION_VALIDATE_START = 6'd6;
    localparam [5:0] ST_DESTINATION_VALIDATE_WAIT = 6'd7;
    localparam [5:0] ST_SOURCE_AR = 6'd8;
    localparam [5:0] ST_SOURCE_R = 6'd9;
    localparam [5:0] ST_SOURCE_VALIDATE_START = 6'd10;
    localparam [5:0] ST_SOURCE_VALIDATE_WAIT = 6'd11;
    localparam [5:0] ST_RANGE_VALIDATE = 6'd12;
    localparam [5:0] ST_DISPATCH = 6'd13;
    localparam [5:0] ST_EXECUTE = 6'd14;
    localparam [5:0] ST_PREPARE_COMPLETION = 6'd15;
    localparam [5:0] ST_COMPLETION_AW = 6'd16;
    localparam [5:0] ST_COMPLETION_W = 6'd17;
    localparam [5:0] ST_COMPLETION_B = 6'd18;
    localparam [5:0] ST_RETIRE = 6'd19;
    localparam [5:0] ST_ABORT_WAIT = 6'd20;
    localparam [5:0] ST_ENGINE_RESET_HOLD = 6'd21;
    localparam [5:0] ST_FATAL = 6'd22;
    localparam [5:0] ST_RANGE_LOAD = 6'd23;
    localparam [5:0] ST_RANGE_END = 6'd24;
    localparam [5:0] ST_RANGE_COMPARE = 6'd25;
    localparam [5:0] ST_RANGE_DECIDE = 6'd26;
    localparam [5:0] ST_VALIDATE_HEADER = 6'd27;
    localparam [5:0] ST_VALIDATE_SEQUENCE = 6'd28;
    localparam [5:0] ST_VALIDATE_LAYOUT = 6'd29;
    localparam [5:0] ST_DESCRIPTOR_END = 6'd30;
    localparam [5:0] ST_DESCRIPTOR_COMPARE = 6'd31;
    localparam [5:0] ST_DESCRIPTOR_DECIDE = 6'd32;
    localparam [5:0] ST_ADMISSION_ARITH = 6'd33;
    localparam [5:0] ST_ADMISSION_VALIDATE = 6'd34;
    localparam [5:0] ST_ADMISSION_DECIDE = 6'd35;
    localparam [5:0] ST_VALIDATE_LAYOUT_DECIDE = 6'd36;
    localparam [5:0] ST_VALIDATE_SEQUENCE_DECIDE = 6'd37;
    localparam [5:0] ST_VALIDATE_SEQUENCE_CHECK = 6'd38;
    localparam [5:0] ST_VALIDATE_HEADER_DECIDE = 6'd39;
    localparam [5:0] ST_VALIDATE_SEQUENCE_RESULT = 6'd40;
    localparam [5:0] ST_COMMAND_R_DECIDE = 6'd41;
    localparam [5:0] ST_DESTINATION_R_DECIDE = 6'd42;
    localparam [5:0] ST_SOURCE_R_DECIDE = 6'd43;
    localparam [5:0] ST_VALIDATE_LAYOUT_RESULT = 6'd44;
    localparam [5:0] ST_AUXILIARY_AR = 6'd45;
    localparam [5:0] ST_AUXILIARY_R = 6'd46;
    localparam [5:0] ST_AUXILIARY_R_DECIDE = 6'd47;
    localparam [5:0] ST_AUXILIARY_VALIDATE_START = 6'd48;
    localparam [5:0] ST_AUXILIARY_VALIDATE_WAIT = 6'd49;
    localparam [5:0] ST_DESTINATION_VALIDATE_DECIDE = 6'd50;
    localparam [5:0] ST_SOURCE_VALIDATE_DECIDE = 6'd51;
    localparam [5:0] ST_AUXILIARY_VALIDATE_DECIDE = 6'd52;
    localparam [5:0] ST_VALIDATE_GLYPH_RANGE = 6'd53;

    localparam [2:0] HEADER_OK = 3'd0;
    localparam [2:0] HEADER_BAD_VERSION = 3'd1;
    localparam [2:0] HEADER_BAD_SIZE = 3'd2;
    localparam [2:0] HEADER_BAD_OPCODE = 3'd3;
    localparam [2:0] HEADER_BAD_FLAGS = 3'd4;

    localparam [1:0] SEQUENCE_OK = 2'd0;
    localparam [1:0] SEQUENCE_BAD_ORDER = 2'd1;
    localparam [1:0] SEQUENCE_BAD_GENERATION = 2'd2;
    localparam [1:0] SEQUENCE_BAD_DEADLINE = 2'd3;

    function automatic [31:0] swap32(input [31:0] value);
        begin
            swap32 = {value[7:0], value[15:8],
                      value[23:16], value[31:24]};
        end
    endfunction

    (* fsm_encoding = "one_hot" *) reg [5:0] state;
    reg [31:0] cycle_counter;
    reg command_active;
    reg command_dispatched_q;
    reg [31:0] command_start_cycle;
    reg [31:0] deadline_budget_q;
    (* extract_enable = "no" *)
    reg [15:0] deadline_remaining_low_q;
    (* extract_enable = "no" *)
    reg [15:0] deadline_remaining_high_q;
    reg deadline_active;
    reg deadline_expired_q;
    reg [7:0] reset_hold_count;
    reg reset_completion_pending;
    reg [15:0] reset_completion_status;
    reg local_engine_reset;
    reg cancel_before_dispatch;
    reg retire_commit_q;

    reg [31:0] command_words [0:15];
    reg [31:0] descriptor_words [0:7];
    reg [4:0] read_beat_index;
    reg read_error_seen;
    reg read_beat_last_q;
    reg read_beat_expected_last_q;
    reg manager_arvalid;
    reg [31:0] manager_araddr;
    reg [7:0] manager_arlen;
    reg manager_response_valid_q;
    reg [AXI_ID_WIDTH-1:0] manager_response_id_q;
    reg [63:0] manager_response_data_q;
    reg [1:0] manager_response_resp_q;
    reg manager_response_last_q;
    reg descriptor_capture_enabled_q;

    reg [15:0] command_opcode_q;
    // Preserve the command classification at intake. Re-decoding the full
    // opcode throughout the FSM creates a high-route-delay control cone at
    // 200 MHz and duplicates the same comparison inside the blitter.
(* max_fanout = 16 *) reg command_is_fill_q;
(* max_fanout = 16 *) reg command_is_blit_q;
(* max_fanout = 16 *) reg command_is_geometry_q;
(* max_fanout = 16 *) reg command_is_flood_q;
(* max_fanout = 16 *) reg command_is_glyph_q;
    reg [15:0] command_flags_q;
    reg [31:0] command_sequence_q;
    reg [31:0] command_generation_q;
    reg [31:0] command_deadline_us_q;
    reg signed [15:0] command_clip_left_q;
    reg signed [15:0] command_clip_top_q;
    reg signed [15:0] command_clip_right_q;
    reg signed [15:0] command_clip_bottom_q;
    reg [31:0] destination_descriptor_offset_q;
    reg [31:0] source_descriptor_offset_q;
    reg [31:0] auxiliary_descriptor_offset_q;
    reg same_surface_q;
    reg [31:0] active_submission_ring_offset_q;
    reg [31:0] active_completion_ring_offset_q;
    reg [32:0] active_submission_ring_end_q;
    reg [32:0] active_completion_ring_end_q;
    reg [31:0] active_resource_generation_q;
    reg active_protected0_valid_q;
    reg [31:0] active_protected0_offset_q;
    reg [31:0] active_protected0_bytes_q;
    reg active_protected1_valid_q;
    reg [31:0] active_protected1_offset_q;
    reg [31:0] active_protected1_bytes_q;

    reg [31:0] destination_data_offset_q;
    reg [31:0] destination_data_bytes_q;
    reg [31:0] destination_pitch_q;
    reg [15:0] destination_width_q;
    reg [15:0] destination_height_q;
    reg [7:0] destination_format_q;
    reg [2:0] destination_bpp_q;
    reg [31:0] source_data_offset_q;
    reg [31:0] source_data_bytes_q;
    reg [31:0] source_pitch_q;
    reg [15:0] source_width_q;
    reg [15:0] source_height_q;
    reg [7:0] source_format_q;
    reg [2:0] source_bpp_q;
    reg [31:0] source_palette_offset_q;
    reg [31:0] auxiliary_data_offset_q;
    reg [31:0] auxiliary_data_bytes_q;
    reg [31:0] auxiliary_pitch_q;
    reg [15:0] auxiliary_width_q;
    reg [15:0] auxiliary_height_q;

    reg [4:0] range_check_index_q;
    reg range_check_enabled_q;
    reg range_check_protected_q;
    reg [31:0] range_first_offset_q;
    reg [31:0] range_first_bytes_q;
    reg [31:0] range_second_offset_q;
    reg [31:0] range_second_bytes_q;
    reg [32:0] range_first_end_q;
    reg [32:0] range_second_end_q;
    reg range_overlap_q;

    reg [31:0] descriptor_check_offset_q;
    reg [32:0] descriptor_check_end_q;
    reg descriptor_check_valid_q;
    reg [1:0] descriptor_check_kind_q;

    reg [10:0] admission_submission_producer_q;
    reg [10:0] admission_completion_consumer_q;
    (* keep = "true" *) reg [10:0] admission_submission_used_q;
    (* keep = "true" *) reg [10:0] admission_completion_used_q;
    (* keep = "true" *) reg admission_configuration_valid_q;
    (* keep = "true" *) reg admission_completion_available_q;

    reg validation_error_q;
    reg [15:0] validation_status_q;
    reg [31:0] validation_fault_q;
    reg layout_bad_clip_q;
    reg layout_bad_flags_q;
    reg layout_bad_fill_q;
    reg layout_bad_geometry_q;
    reg layout_bad_flood_q;
    reg layout_bad_glyph_q;
    reg [32:0] glyph_descriptor_end_q;
    reg [2:0] header_result_q;
    reg [1:0] sequence_result_q;

    reg [15:0] completion_status_q;
    reg [31:0] completion_count_q;
    reg [31:0] completion_fault_q;
reg [31:0] completion_words [0:7];
reg [31:0] completion_write_address_q;
reg completion_awvalid;
reg completion_wvalid;
reg [1:0] completion_beat_index;
    reg retirement_open;
    reg last_sequence_valid;
    reg [31:0] last_sequence;
    reg [31:0] sequence_delta_q;

    wire [31:0] command_word0 = command_words[0];
    wire [31:0] command_word1 = command_words[1];
    wire incoming_is_geometry =
        command_word1[31:16] == `ASTRA_RENDER_OP_LINE ||
        command_word1[31:16] == `ASTRA_RENDER_OP_RECT ||
        command_word1[31:16] == `ASTRA_RENDER_OP_CIRCLE ||
        command_word1[31:16] == `ASTRA_RENDER_OP_ELLIPSE ||
        command_word1[31:16] == `ASTRA_RENDER_OP_PATTERN_FILL;
    wire incoming_is_flood = command_word1[31:16] ==
        `ASTRA_RENDER_OP_FLOOD_FILL;
    wire incoming_is_glyph = command_word1[31:16] ==
        `ASTRA_RENDER_OP_GLYPH_RUN;
    wire [31:0] command_sequence = command_words[2];
    wire [31:0] command_generation = command_words[3];
    wire [31:0] command_deadline_us = command_words[4];
    wire sequence_valid_q = command_sequence_q != 32'd0 &&
        (!last_sequence_valid ||
         (sequence_delta_q != 32'd0 && !sequence_delta_q[31]));
    wire command_uses_auxiliary_q = command_is_flood_q ||
        (command_is_blit_q && command_flags_q[3]);
    wire command_uses_palette_q =
        (command_is_blit_q && command_flags_q[5]) ||
        (command_is_glyph_q &&
         (source_format_q == `ASTRA_RENDER_FORMAT_INDEX4 ||
          source_format_q == `ASTRA_RENDER_FORMAT_INDEX8));
    wire [31:0] source_palette_bytes_q = command_is_glyph_q &&
        source_format_q == `ASTRA_RENDER_FORMAT_INDEX4 ? 32'd64 : 32'd1024;
    wire [31:0] glyph_descriptor_bytes_q =
        {19'd0, command_words[11][12:0]} << 4;

    reg validator_start;
    // This is a deliberate control-pipeline boundary into the validator.
    (* dont_touch = "yes" *) reg [1:0] validator_required_access;
    reg validator_palette_required;
    wire validator_busy;
    wire validator_done;
    wire validator_valid;
    wire [7:0] validator_format;
    wire [2:0] validator_bpp;
    wire [15:0] validator_width;
    wire [15:0] validator_height;
    wire [31:0] validator_data_offset;
    wire [31:0] validator_data_bytes;
    wire [31:0] validator_pitch;
    wire [31:0] validator_palette_offset;

    astra_render_surface_validator surface_validator_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(validator_start),
        .expected_generation(command_generation_q),
        .required_access(validator_required_access),
        .palette_required(validator_palette_required),
        .arena_bytes(ARENA_BYTES),
        .version_size(descriptor_words[0]),
        .generation(descriptor_words[1]),
        .data_offset(descriptor_words[2]),
        .data_bytes(descriptor_words[3]),
        .pitch(descriptor_words[4]),
        .width_height(descriptor_words[5]),
        .format_flags(descriptor_words[6]),
        .palette_offset(descriptor_words[7]),
        .busy(validator_busy),
        .done(validator_done),
        .descriptor_valid(validator_valid),
        .format(validator_format),
        .bytes_per_pixel(validator_bpp),
        .width(validator_width),
        .height(validator_height),
        .validated_data_offset(validator_data_offset),
        .validated_data_bytes(validator_data_bytes),
        .validated_pitch(validator_pitch),
        .validated_palette_offset(validator_palette_offset)
    );

    reg blitter_start;
    reg blitter_abort;
    wire blitter_busy;
    wire blitter_done;
    wire [15:0] blitter_status;
    wire [31:0] blitter_fault_detail;
    wire [31:0] blitter_completed_pixels;
    wire blitter_writer_start;
    wire blitter_writer_abort;
    wire blitter_writer_flush;
    wire writer_flush_ready;
    wire engine_writer_flush_ready;
    reg engine_writer_flush_ready_q;
    wire writer_busy;
    wire writer_done;
    wire writer_aborted;
    wire writer_error;
    wire [31:0] writer_fault_detail;
    wire blitter_pixel_valid;
    wire blitter_pixel_ready;
    wire [31:0] blitter_pixel_address;
    wire [7:0] blitter_pixel_format;
    wire [31:0] blitter_pixel_value;
    wire [AXI_ID_WIDTH-1:0] blitter_arid;
    wire [31:0] blitter_araddr;
    wire [7:0] blitter_arlen;
    wire [2:0] blitter_arsize;
    wire [1:0] blitter_arburst;
    wire [3:0] blitter_arcache;
    wire [2:0] blitter_arprot;
    wire [3:0] blitter_arqos;
    wire blitter_arvalid;
    wire blitter_arready;
    wire [AXI_ID_WIDTH-1:0] blitter_rid;
    wire [63:0] blitter_rdata;
    wire [1:0] blitter_rresp;
    wire blitter_rlast;
    wire blitter_rvalid;
    wire blitter_rready;

    reg geometry_start;
    reg geometry_abort;
    wire geometry_busy;
    wire geometry_done;
    wire [15:0] geometry_status;
    wire [31:0] geometry_fault_detail;
    wire [31:0] geometry_completed_pixels;
    wire geometry_writer_start;
    wire geometry_writer_abort;
    wire geometry_writer_flush;
    wire geometry_pixel_valid;
    wire geometry_pixel_ready;
    wire [31:0] geometry_pixel_address;
    wire [7:0] geometry_pixel_format;
    wire [31:0] geometry_pixel_value;

    reg flood_start;
    reg flood_abort;
    wire flood_busy;
    wire flood_done;
    wire [15:0] flood_status;
    wire [31:0] flood_fault_detail;
    wire [31:0] flood_completed_pixels;
    wire flood_writer_start;
    wire flood_writer_abort;
    wire flood_writer_flush;
    wire flood_writer_barrier;
    wire writer_barrier_ready;
    wire writer_barrier_done;
    wire flood_pixel_valid;
    wire flood_pixel_ready;
    wire [31:0] flood_pixel_address;
    wire [7:0] flood_pixel_format;
    wire [31:0] flood_pixel_value;
    wire [AXI_ID_WIDTH-1:0] flood_arid;
    wire [31:0] flood_araddr;
    wire [7:0] flood_arlen;
    wire [2:0] flood_arsize;
    wire [1:0] flood_arburst;
    wire [3:0] flood_arcache;
    wire [2:0] flood_arprot;
    wire [3:0] flood_arqos;
    wire flood_arvalid;
    wire flood_arready;
    wire flood_rready;

    reg glyph_start;
    reg glyph_abort;
    wire glyph_busy;
    wire glyph_done;
    wire [15:0] glyph_status;
    wire [31:0] glyph_fault_detail;
    wire [31:0] glyph_completed_pixels;
    wire glyph_writer_start;
    wire glyph_writer_abort;
    wire glyph_writer_flush;
    wire glyph_pixel_valid;
    wire glyph_pixel_ready;
    wire [31:0] glyph_pixel_address;
    wire [7:0] glyph_pixel_format;
    wire [31:0] glyph_pixel_value;
    wire [AXI_ID_WIDTH-1:0] glyph_arid;
    wire [31:0] glyph_araddr;
    wire [7:0] glyph_arlen;
    wire [2:0] glyph_arsize;
    wire [1:0] glyph_arburst;
    wire [3:0] glyph_arcache;
    wire [2:0] glyph_arprot;
    wire [3:0] glyph_arqos;
    wire glyph_arvalid;
    wire glyph_arready;
    wire glyph_rready;

    astra_render_blitter #(
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID(BLITTER_READ_ID)
    ) blitter_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(blitter_start),
        .abort(blitter_abort),
        .is_fill(command_is_fill_q),
        .is_blit(command_is_blit_q),
        .arena_base(ARENA_BASE),
        .clip_left(command_clip_left_q),
        .clip_top(command_clip_top_q),
        .clip_right(command_clip_right_q),
        .clip_bottom(command_clip_bottom_q),
        .source_x(command_words[11][31:16]),
        .source_y(command_words[11][15:0]),
        .destination_x(command_words[12][31:16]),
        .destination_y(command_words[12][15:0]),
        .source_width(command_words[13][31:16]),
        .source_height(command_words[13][15:0]),
        .destination_width(command_words[14][31:16]),
        .destination_height(command_words[14][15:0]),
        .command_flags(command_flags_q),
        .options(command_words[15]),
        .same_surface(same_surface_q),
        .destination_data_offset(destination_data_offset_q),
        .destination_pitch(destination_pitch_q),
        .destination_surface_width(destination_width_q),
        .destination_surface_height(destination_height_q),
        .destination_format(destination_format_q),
        .destination_bytes_per_pixel(destination_bpp_q),
        .source_data_offset(source_data_offset_q),
        .source_pitch(source_pitch_q),
        .source_surface_width(source_width_q),
        .source_surface_height(source_height_q),
        .source_format(source_format_q),
        .source_bytes_per_pixel(source_bpp_q),
        .source_palette_offset(source_palette_offset_q),
        .auxiliary_data_offset(auxiliary_data_offset_q),
        .auxiliary_pitch(auxiliary_pitch_q),
        .auxiliary_surface_width(auxiliary_width_q),
        .auxiliary_surface_height(auxiliary_height_q),
        .busy(blitter_busy),
        .done(blitter_done),
        .status(blitter_status),
        .fault_detail(blitter_fault_detail),
        .completed_pixels(blitter_completed_pixels),
        .writer_start(blitter_writer_start),
        .writer_abort(blitter_writer_abort),
        .writer_flush(blitter_writer_flush),
        .writer_flush_ready(engine_writer_flush_ready),
        .writer_busy(writer_busy),
        .writer_done(writer_done),
        .writer_aborted(writer_aborted),
        .writer_error(writer_error),
        .writer_fault_detail(writer_fault_detail),
        .pixel_valid(blitter_pixel_valid),
        .pixel_ready(blitter_pixel_ready),
        .pixel_address(blitter_pixel_address),
        .pixel_format(blitter_pixel_format),
        .pixel_value(blitter_pixel_value),
        .m_axi_arid(blitter_arid),
        .m_axi_araddr(blitter_araddr),
        .m_axi_arlen(blitter_arlen),
        .m_axi_arsize(blitter_arsize),
        .m_axi_arburst(blitter_arburst),
        .m_axi_arcache(blitter_arcache),
        .m_axi_arprot(blitter_arprot),
        .m_axi_arqos(blitter_arqos),
        .m_axi_arvalid(blitter_arvalid),
        .m_axi_arready(blitter_arready),
        .m_axi_rid(blitter_rid),
        .m_axi_rdata(blitter_rdata),
        .m_axi_rresp(blitter_rresp),
        .m_axi_rlast(blitter_rlast),
        .m_axi_rvalid(blitter_rvalid),
        .m_axi_rready(blitter_rready)
    );

    astra_render_geometry geometry_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(geometry_start),
        .abort(geometry_abort),
        .opcode(command_opcode_q),
        .command_flags(command_flags_q),
        .clip_left(command_clip_left_q),
        .clip_top(command_clip_top_q),
        .clip_right(command_clip_right_q),
        .clip_bottom(command_clip_bottom_q),
        .p0_x(command_words[11][31:16]),
        .p0_y(command_words[11][15:0]),
        .p1_x(command_words[12][31:16]),
        .p1_y(command_words[12][15:0]),
        .radius_x(command_words[13][31:16]),
        .radius_y(command_words[13][15:0]),
        .pattern_origin_x(command_words[13][31:16]),
        .pattern_origin_y(command_words[13][15:0]),
        .pattern({command_words[9], command_words[10]}),
        .foreground(command_words[15]),
        .background(command_words[14]),
        .arena_base(ARENA_BASE),
        .destination_data_offset(destination_data_offset_q),
        .destination_pitch(destination_pitch_q),
        .destination_format(destination_format_q),
        .destination_bytes_per_pixel(destination_bpp_q),
        .busy(geometry_busy),
        .done(geometry_done),
        .status(geometry_status),
        .fault_detail(geometry_fault_detail),
        .completed_pixels(geometry_completed_pixels),
        .writer_start(geometry_writer_start),
        .writer_abort(geometry_writer_abort),
        .writer_flush(geometry_writer_flush),
        .writer_flush_ready(engine_writer_flush_ready),
        .writer_done(writer_done),
        .writer_aborted(writer_aborted),
        .writer_error(writer_error),
        .writer_fault_detail(writer_fault_detail),
        .pixel_valid(geometry_pixel_valid),
        .pixel_ready(geometry_pixel_ready),
        .pixel_address(geometry_pixel_address),
        .pixel_format(geometry_pixel_format),
        .pixel_value(geometry_pixel_value)
    );

    astra_render_flood #(
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID(FLOOD_READ_ID)
    ) flood_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(flood_start),
        .abort(flood_abort),
        .arena_base(ARENA_BASE),
        .clip_left(command_clip_left_q),
        .clip_top(command_clip_top_q),
        .clip_right(command_clip_right_q),
        .clip_bottom(command_clip_bottom_q),
        .seed_x(command_words[11][31:16]),
        .seed_y(command_words[11][15:0]),
        .replacement(command_words[15]),
        .destination_data_offset(destination_data_offset_q),
        .destination_pitch(destination_pitch_q),
        .destination_width(destination_width_q),
        .destination_height(destination_height_q),
        .destination_format(destination_format_q),
        .destination_bytes_per_pixel(destination_bpp_q),
        .workspace_data_offset(auxiliary_data_offset_q),
        .workspace_data_bytes(auxiliary_data_bytes_q),
        .busy(flood_busy),
        .done(flood_done),
        .status(flood_status),
        .fault_detail(flood_fault_detail),
        .completed_pixels(flood_completed_pixels),
        .writer_start(flood_writer_start),
        .writer_abort(flood_writer_abort),
        .writer_flush(flood_writer_flush),
        .writer_flush_ready(engine_writer_flush_ready),
        .writer_barrier(flood_writer_barrier),
        .writer_barrier_ready(writer_barrier_ready),
        .writer_barrier_done(writer_barrier_done),
        .writer_done(writer_done),
        .writer_aborted(writer_aborted),
        .writer_error(writer_error),
        .writer_fault_detail(writer_fault_detail),
        .pixel_valid(flood_pixel_valid),
        .pixel_ready(flood_pixel_ready),
        .pixel_address(flood_pixel_address),
        .pixel_format(flood_pixel_format),
        .pixel_value(flood_pixel_value),
        .m_axi_arid(flood_arid),
        .m_axi_araddr(flood_araddr),
        .m_axi_arlen(flood_arlen),
        .m_axi_arsize(flood_arsize),
        .m_axi_arburst(flood_arburst),
        .m_axi_arcache(flood_arcache),
        .m_axi_arprot(flood_arprot),
        .m_axi_arqos(flood_arqos),
        .m_axi_arvalid(flood_arvalid),
        .m_axi_arready(flood_arready),
        .m_axi_rid(m_axi_rid),
        .m_axi_rdata(m_axi_rdata),
        .m_axi_rresp(m_axi_rresp),
        .m_axi_rlast(m_axi_rlast),
        .m_axi_rvalid(flood_busy && m_axi_rvalid),
        .m_axi_rready(flood_rready)
    );

    astra_render_glyph #(
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID(GLYPH_READ_ID)
    ) glyph_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(glyph_start),
        .abort(glyph_abort),
        .arena_base(ARENA_BASE),
        .clip_left(command_clip_left_q),
        .clip_top(command_clip_top_q),
        .clip_right(command_clip_right_q),
        .clip_bottom(command_clip_bottom_q),
        .command_flags(command_flags_q),
        .foreground(command_words[12]),
        .background(command_words[13]),
        .transparent_index(command_words[14][7:0]),
        .descriptor_offset(command_words[10]),
        .descriptor_count(command_words[11][12:0]),
        .destination_data_offset(destination_data_offset_q),
        .destination_pitch(destination_pitch_q),
        .destination_width(destination_width_q),
        .destination_height(destination_height_q),
        .destination_format(destination_format_q),
        .destination_bytes_per_pixel(destination_bpp_q),
        .source_data_offset(source_data_offset_q),
        .source_data_bytes(source_data_bytes_q),
        .source_pitch(source_pitch_q),
        .source_width(source_width_q),
        .source_height(source_height_q),
        .source_format(source_format_q),
        .source_palette_offset(source_palette_offset_q),
        .busy(glyph_busy),
        .done(glyph_done),
        .status(glyph_status),
        .fault_detail(glyph_fault_detail),
        .completed_pixels(glyph_completed_pixels),
        .writer_start(glyph_writer_start),
        .writer_abort(glyph_writer_abort),
        .writer_flush(glyph_writer_flush),
        .writer_flush_ready(engine_writer_flush_ready_q),
        .writer_done(writer_done),
        .writer_aborted(writer_aborted),
        .writer_error(writer_error),
        .writer_fault_detail(writer_fault_detail),
        .pixel_valid(glyph_pixel_valid),
        .pixel_ready(glyph_pixel_ready),
        .pixel_address(glyph_pixel_address),
        .pixel_format(glyph_pixel_format),
        .pixel_value(glyph_pixel_value),
        .m_axi_arid(glyph_arid),
        .m_axi_araddr(glyph_araddr),
        .m_axi_arlen(glyph_arlen),
        .m_axi_arsize(glyph_arsize),
        .m_axi_arburst(glyph_arburst),
        .m_axi_arcache(glyph_arcache),
        .m_axi_arprot(glyph_arprot),
        .m_axi_arqos(glyph_arqos),
        .m_axi_arvalid(glyph_arvalid),
        .m_axi_arready(glyph_arready),
        .m_axi_rid(m_axi_rid),
        .m_axi_rdata(m_axi_rdata),
        .m_axi_rresp(m_axi_rresp),
        .m_axi_rlast(m_axi_rlast),
        .m_axi_rvalid(glyph_busy && m_axi_rvalid),
        .m_axi_rready(glyph_rready)
    );

    wire selected_writer_start = command_is_glyph_q ? glyph_writer_start :
        command_is_flood_q ? flood_writer_start :
        command_is_geometry_q ? geometry_writer_start : blitter_writer_start;
    wire selected_writer_abort = command_is_glyph_q ? glyph_writer_abort :
        command_is_flood_q ? flood_writer_abort :
        command_is_geometry_q ? geometry_writer_abort : blitter_writer_abort;
    wire selected_writer_flush = command_is_glyph_q ? glyph_writer_flush :
        command_is_flood_q ? flood_writer_flush :
        command_is_geometry_q ? geometry_writer_flush : blitter_writer_flush;
    wire selected_pixel_valid = command_is_glyph_q ? glyph_pixel_valid :
        command_is_flood_q ? flood_pixel_valid :
        command_is_geometry_q ? geometry_pixel_valid : blitter_pixel_valid;
    wire [31:0] selected_pixel_address = command_is_glyph_q ?
        glyph_pixel_address : command_is_flood_q ?
        flood_pixel_address : command_is_geometry_q ?
        geometry_pixel_address : blitter_pixel_address;
    wire [7:0] selected_pixel_format = command_is_glyph_q ?
        glyph_pixel_format : command_is_flood_q ?
        flood_pixel_format : command_is_geometry_q ?
        geometry_pixel_format : blitter_pixel_format;
    wire [31:0] selected_pixel_value = command_is_glyph_q ?
        glyph_pixel_value : command_is_flood_q ?
        flood_pixel_value : command_is_geometry_q ?
        geometry_pixel_value : blitter_pixel_value;
    reg [31:0] dispatch_address_q [0:1];
    reg [7:0] dispatch_format_q [0:1];
    reg [31:0] dispatch_value_q [0:1];
    reg dispatch_read_pointer_q, dispatch_write_pointer_q;
    reg [1:0] dispatch_count_q;
    wire writer_pixel_ready;
    wire dispatch_valid = dispatch_count_q != 2'd0;
    wire dispatch_ready = dispatch_count_q != 2'd2;
    wire dispatch_enqueue = selected_pixel_valid && dispatch_ready;
    wire dispatch_dequeue = dispatch_valid && writer_pixel_ready;

    always @(posedge clk) begin
        if (reset || local_engine_reset || selected_writer_abort) begin
            dispatch_read_pointer_q <= 1'b0;
            dispatch_write_pointer_q <= 1'b0;
            dispatch_count_q <= 2'd0;
            dispatch_address_q[0] <= 32'd0;
            dispatch_address_q[1] <= 32'd0;
            dispatch_format_q[0] <= 8'd0;
            dispatch_format_q[1] <= 8'd0;
            dispatch_value_q[0] <= 32'd0;
            dispatch_value_q[1] <= 32'd0;
        end else begin
            if (dispatch_enqueue) begin
                dispatch_address_q[dispatch_write_pointer_q] <=
                    selected_pixel_address;
                dispatch_format_q[dispatch_write_pointer_q] <=
                    selected_pixel_format;
                dispatch_value_q[dispatch_write_pointer_q] <=
                    selected_pixel_value;
                dispatch_write_pointer_q <= dispatch_write_pointer_q + 1'b1;
            end
            if (dispatch_dequeue)
                dispatch_read_pointer_q <= dispatch_read_pointer_q + 1'b1;
            case ({dispatch_enqueue, dispatch_dequeue})
                2'b10: dispatch_count_q <= dispatch_count_q + 2'd1;
                2'b01: dispatch_count_q <= dispatch_count_q - 2'd1;
                default: dispatch_count_q <= dispatch_count_q;
            endcase
        end
    end

    assign engine_writer_flush_ready = writer_flush_ready &&
        !dispatch_valid;
    always @(posedge clk) begin
        if (reset || local_engine_reset)
            engine_writer_flush_ready_q <= 1'b0;
        else
            engine_writer_flush_ready_q <= engine_writer_flush_ready;
    end
    assign blitter_pixel_ready = !command_is_glyph_q &&
        !command_is_flood_q && !command_is_geometry_q && dispatch_ready;
    assign geometry_pixel_ready = command_is_geometry_q && dispatch_ready;
    assign flood_pixel_ready = command_is_flood_q && dispatch_ready;
    assign glyph_pixel_ready = command_is_glyph_q && dispatch_ready;

    wire [AXI_ID_WIDTH-1:0] pixel_awid;
    wire [31:0] pixel_awaddr;
    wire [7:0] pixel_awlen;
    wire [2:0] pixel_awsize;
    wire [1:0] pixel_awburst;
    wire [3:0] pixel_awcache;
    wire [2:0] pixel_awprot;
    wire [3:0] pixel_awqos;
    wire pixel_awvalid;
    wire pixel_awready;
    wire [63:0] pixel_wdata;
    wire [7:0] pixel_wstrb;
    wire pixel_wlast;
    wire pixel_wvalid;
    wire pixel_wready;
    wire [AXI_ID_WIDTH-1:0] pixel_bid;
    wire [1:0] pixel_bresp;
    wire pixel_bvalid;
    wire pixel_bready;

    astra_render_pixel_writer #(
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID(PIXEL_WRITE_ID)
    ) pixel_writer_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(selected_writer_start),
        .abort(selected_writer_abort),
        .flush(selected_writer_flush),
        .flush_ready(writer_flush_ready),
        .barrier(command_is_flood_q && flood_writer_barrier),
        .barrier_ready(writer_barrier_ready),
        .barrier_done(writer_barrier_done),
        .pixel_valid(dispatch_valid),
        .pixel_ready(writer_pixel_ready),
        .pixel_address(dispatch_address_q[dispatch_read_pointer_q]),
        .pixel_format(dispatch_format_q[dispatch_read_pointer_q]),
        .pixel_value(dispatch_value_q[dispatch_read_pointer_q]),
        .busy(writer_busy),
        .done(writer_done),
        .aborted(writer_aborted),
        .write_error(writer_error),
        .fault_detail(writer_fault_detail),
        .pixels_accepted(),
        .bytes_written(),
        .m_axi_awid(pixel_awid),
        .m_axi_awaddr(pixel_awaddr),
        .m_axi_awlen(pixel_awlen),
        .m_axi_awsize(pixel_awsize),
        .m_axi_awburst(pixel_awburst),
        .m_axi_awcache(pixel_awcache),
        .m_axi_awprot(pixel_awprot),
        .m_axi_awqos(pixel_awqos),
        .m_axi_awvalid(pixel_awvalid),
        .m_axi_awready(pixel_awready),
        .m_axi_wdata(pixel_wdata),
        .m_axi_wstrb(pixel_wstrb),
        .m_axi_wlast(pixel_wlast),
        .m_axi_wvalid(pixel_wvalid),
        .m_axi_wready(pixel_wready),
        .m_axi_bid(pixel_bid),
        .m_axi_bresp(pixel_bresp),
        .m_axi_bvalid(pixel_bvalid),
        .m_axi_bready(pixel_bready)
    );

    wire read_owner_glyph = glyph_busy;
    wire read_owner_flood = flood_busy;
    wire read_owner_blitter = blitter_busy;
    assign m_axi_arid = read_owner_glyph ? glyph_arid :
        read_owner_flood ? flood_arid :
        read_owner_blitter ? blitter_arid : MANAGER_READ_ID;
    assign m_axi_araddr = read_owner_glyph ? glyph_araddr :
        read_owner_flood ? flood_araddr :
        read_owner_blitter ? blitter_araddr : manager_araddr;
    assign m_axi_arlen = read_owner_glyph ? glyph_arlen :
        read_owner_flood ? flood_arlen :
        read_owner_blitter ? blitter_arlen : manager_arlen;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = read_owner_glyph ? glyph_arqos :
        read_owner_flood ? flood_arqos :
        read_owner_blitter ? blitter_arqos : 4'b0000;
    assign m_axi_arvalid = read_owner_glyph ? glyph_arvalid :
        read_owner_flood ? flood_arvalid :
        read_owner_blitter ? blitter_arvalid : manager_arvalid;
    assign glyph_arready = read_owner_glyph && m_axi_arready;
    assign flood_arready = read_owner_flood && m_axi_arready;
    assign blitter_arready = read_owner_blitter && m_axi_arready;
    assign blitter_rid = m_axi_rid;
    assign blitter_rdata = m_axi_rdata;
    assign blitter_rresp = m_axi_rresp;
    assign blitter_rlast = m_axi_rlast;
    assign blitter_rvalid = read_owner_blitter && m_axi_rvalid;
    wire manager_rready = !manager_response_valid_q &&
        (state == ST_COMMAND_R || state == ST_DESTINATION_R ||
         state == ST_SOURCE_R || state == ST_AUXILIARY_R);
    wire manager_response_accept = !read_owner_glyph && !read_owner_flood &&
        !read_owner_blitter &&
        m_axi_rvalid && manager_rready;
    assign m_axi_rready = read_owner_glyph ? glyph_rready :
        read_owner_flood ? flood_rready :
        read_owner_blitter ? blitter_rready : manager_rready;

    wire write_owner_pixels = writer_busy;
    wire [63:0] completion_beat_data =
        completion_beat_index == 2'd0 ?
            {swap32(completion_words[1]), swap32(completion_words[0])} :
        completion_beat_index == 2'd1 ?
            {swap32(completion_words[3]), swap32(completion_words[2])} :
        completion_beat_index == 2'd2 ?
            {swap32(completion_words[5]), swap32(completion_words[4])} :
            {swap32(completion_words[7]), swap32(completion_words[6])};
assign m_axi_awid = write_owner_pixels ? pixel_awid :
                         COMPLETION_WRITE_ID;
assign m_axi_awaddr = write_owner_pixels ? pixel_awaddr :
                           completion_write_address_q;
    assign m_axi_awlen = write_owner_pixels ? pixel_awlen : 8'd3;
    assign m_axi_awsize = 3'b011;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot = 3'b000;
    assign m_axi_awqos = 4'b0000;
    assign m_axi_awvalid = write_owner_pixels ? pixel_awvalid :
                            completion_awvalid;
    assign pixel_awready = write_owner_pixels && m_axi_awready;
    assign m_axi_wdata = write_owner_pixels ? pixel_wdata :
                          completion_beat_data;
    assign m_axi_wstrb = write_owner_pixels ? pixel_wstrb : 8'hff;
    assign m_axi_wlast = write_owner_pixels ? pixel_wlast :
                          completion_beat_index == 2'd3;
    assign m_axi_wvalid = write_owner_pixels ? pixel_wvalid :
                           completion_wvalid;
    assign pixel_wready = write_owner_pixels && m_axi_wready;
    assign pixel_bid = m_axi_bid;
    assign pixel_bresp = m_axi_bresp;
    assign pixel_bvalid = write_owner_pixels && m_axi_bvalid;
    assign m_axi_bready = write_owner_pixels ? pixel_bready :
                          state == ST_COMPLETION_B;

    integer word_index;
    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            cycle_counter <= 32'd0;
            command_active <= 1'b0;
            command_dispatched_q <= 1'b0;
            command_start_cycle <= 32'd0;
            deadline_budget_q <= 32'd0;
            deadline_remaining_low_q <= 16'd0;
            deadline_remaining_high_q <= 16'd0;
            deadline_active <= 1'b0;
            deadline_expired_q <= 1'b0;
            reset_hold_count <= 8'd0;
            reset_completion_pending <= 1'b0;
            reset_completion_status <= `ASTRA_RENDER_STATUS_RESET;
            local_engine_reset <= 1'b0;
            cancel_before_dispatch <= 1'b0;
            retire_commit_q <= 1'b0;
            read_beat_index <= 5'd0;
            read_error_seen <= 1'b0;
            read_beat_last_q <= 1'b0;
            read_beat_expected_last_q <= 1'b0;
            manager_arvalid <= 1'b0;
            manager_araddr <= 32'd0;
            manager_arlen <= 8'd0;
            manager_response_valid_q <= 1'b0;
            manager_response_id_q <= {AXI_ID_WIDTH{1'b0}};
            manager_response_data_q <= 64'd0;
            manager_response_resp_q <= 2'd0;
            manager_response_last_q <= 1'b0;
            descriptor_capture_enabled_q <= 1'b0;
            command_opcode_q <= 16'd0;
            command_is_fill_q <= 1'b0;
            command_is_blit_q <= 1'b0;
            command_is_geometry_q <= 1'b0;
            command_is_flood_q <= 1'b0;
            command_is_glyph_q <= 1'b0;
            command_flags_q <= 16'd0;
            command_sequence_q <= 32'd0;
            command_generation_q <= 32'd0;
            command_deadline_us_q <= 32'd0;
            command_clip_left_q <= 16'sd0;
            command_clip_top_q <= 16'sd0;
            command_clip_right_q <= 16'sd0;
            command_clip_bottom_q <= 16'sd0;
            destination_descriptor_offset_q <= 32'd0;
            source_descriptor_offset_q <= 32'd0;
            auxiliary_descriptor_offset_q <= 32'd0;
            same_surface_q <= 1'b0;
            active_submission_ring_offset_q <= 32'd0;
            active_completion_ring_offset_q <= 32'd0;
            active_submission_ring_end_q <= 33'd0;
            active_completion_ring_end_q <= 33'd0;
            active_resource_generation_q <= 32'd0;
            active_protected0_valid_q <= 1'b0;
            active_protected0_offset_q <= 32'd0;
            active_protected0_bytes_q <= 32'd0;
            active_protected1_valid_q <= 1'b0;
            active_protected1_offset_q <= 32'd0;
            active_protected1_bytes_q <= 32'd0;
            destination_data_offset_q <= 32'd0;
            destination_data_bytes_q <= 32'd0;
            destination_pitch_q <= 32'd0;
            destination_width_q <= 16'd0;
            destination_height_q <= 16'd0;
            destination_format_q <= 8'd0;
            destination_bpp_q <= 3'd0;
            source_data_offset_q <= 32'd0;
            source_data_bytes_q <= 32'd0;
            source_pitch_q <= 32'd0;
            source_width_q <= 16'd0;
            source_height_q <= 16'd0;
            source_format_q <= 8'd0;
            source_bpp_q <= 3'd0;
            source_palette_offset_q <= 32'd0;
            auxiliary_data_offset_q <= 32'd0;
            auxiliary_data_bytes_q <= 32'd0;
            auxiliary_pitch_q <= 32'd0;
            auxiliary_width_q <= 16'd0;
            auxiliary_height_q <= 16'd0;
            range_check_index_q <= 5'd0;
            range_check_enabled_q <= 1'b0;
            range_check_protected_q <= 1'b0;
            range_first_offset_q <= 32'd0;
            range_first_bytes_q <= 32'd0;
            range_second_offset_q <= 32'd0;
            range_second_bytes_q <= 32'd0;
            range_first_end_q <= 33'd0;
            range_second_end_q <= 33'd0;
            range_overlap_q <= 1'b0;
            descriptor_check_offset_q <= 32'd0;
            descriptor_check_end_q <= 33'd0;
            descriptor_check_valid_q <= 1'b0;
            descriptor_check_kind_q <= 2'd0;
            admission_submission_producer_q <= 11'd0;
            admission_completion_consumer_q <= 11'd0;
            admission_submission_used_q <= 11'd0;
            admission_completion_used_q <= 11'd0;
            admission_configuration_valid_q <= 1'b0;
            admission_completion_available_q <= 1'b0;
            validation_error_q <= 1'b0;
            validation_status_q <= `ASTRA_RENDER_STATUS_OK;
            validation_fault_q <= 32'd0;
            layout_bad_clip_q <= 1'b0;
            layout_bad_flags_q <= 1'b0;
            layout_bad_fill_q <= 1'b0;
            layout_bad_geometry_q <= 1'b0;
            layout_bad_flood_q <= 1'b0;
            layout_bad_glyph_q <= 1'b0;
            glyph_descriptor_end_q <= 33'd0;
            header_result_q <= HEADER_OK;
            sequence_result_q <= SEQUENCE_OK;
            completion_status_q <= `ASTRA_RENDER_STATUS_OK;
            completion_count_q <= 32'd0;
            completion_fault_q <= 32'd0;
            completion_write_address_q <= 32'd0;
            completion_awvalid <= 1'b0;
            completion_wvalid <= 1'b0;
            completion_beat_index <= 2'd0;
            retirement_open <= 1'b1;
            last_sequence_valid <= 1'b0;
            last_sequence <= 32'd0;
            sequence_delta_q <= 32'd0;
            validator_start <= 1'b0;
            validator_required_access <= 2'd0;
            validator_palette_required <= 1'b0;
            blitter_start <= 1'b0;
            blitter_abort <= 1'b0;
            geometry_start <= 1'b0;
            geometry_abort <= 1'b0;
            flood_start <= 1'b0;
            flood_abort <= 1'b0;
            glyph_start <= 1'b0;
            glyph_abort <= 1'b0;
            submission_consumer <= 11'd0;
            completion_producer <= 11'd0;
            busy <= 1'b0;
            completion_irq <= 1'b0;
            engine_reset_active <= 1'b0;
            configuration_fault <= 1'b0;
            retired_fence <= 32'd0;
            commands_submitted <= 32'd0;
            commands_completed <= 32'd0;
            commands_failed <= 32'd0;
            backpressure_cycles <= 32'd0;
            timeout_count <= 32'd0;
            reset_count <= 32'd0;
            last_fault_detail <= 32'd0;
            for (word_index = 0; word_index < 16;
                 word_index = word_index + 1)
                command_words[word_index] <= 32'd0;
            for (word_index = 0; word_index < 8;
                 word_index = word_index + 1) begin
                descriptor_words[word_index] <= 32'd0;
                completion_words[word_index] <= 32'd0;
            end
        end else begin
            cycle_counter <= cycle_counter + 32'd1;
            completion_irq <= 1'b0;
            validator_start <= 1'b0;
            blitter_start <= 1'b0;
            blitter_abort <= 1'b0;
            geometry_start <= 1'b0;
            geometry_abort <= 1'b0;
            flood_start <= 1'b0;
            flood_abort <= 1'b0;
            glyph_start <= 1'b0;
            glyph_abort <= 1'b0;
            retire_commit_q <= 1'b0;

            if (manager_response_accept) begin
                manager_response_valid_q <= 1'b1;
                manager_response_id_q <= m_axi_rid;
                manager_response_data_q <= m_axi_rdata;
                manager_response_resp_q <= m_axi_rresp;
                manager_response_last_q <= m_axi_rlast;
            end

            if (manager_response_valid_q &&
                descriptor_capture_enabled_q) begin
                descriptor_words[read_beat_index * 2] <=
                    swap32(manager_response_data_q[31:0]);
                descriptor_words[read_beat_index * 2 + 1] <=
                    swap32(manager_response_data_q[63:32]);
            end

            if (queue_rebase && !command_active) begin
                submission_consumer <= submission_producer;
                completion_producer <= completion_consumer;
                retired_fence <= 32'd0;
                retirement_open <= 1'b1;
                last_sequence_valid <= 1'b0;
                configuration_fault <= 1'b0;
                last_fault_detail <= 32'd0;
                descriptor_capture_enabled_q <= 1'b0;
                busy <= 1'b0;
                state <= ST_IDLE;
            end

            if (retire_commit_q) begin
                completion_producer <= completion_producer + 11'd1;
                submission_consumer <= submission_consumer + 11'd1;
                commands_completed <= commands_completed + 32'd1;
                completion_irq <= 1'b1;
                command_active <= 1'b0;
                command_dispatched_q <= 1'b0;
                descriptor_capture_enabled_q <= 1'b0;
                if (completion_status_q == `ASTRA_RENDER_STATUS_OK) begin
                    if (retirement_open)
                        retired_fence <= command_sequence_q;
                end else begin
                    commands_failed <= commands_failed + 32'd1;
                    retirement_open <= 1'b0;
                    last_fault_detail <= completion_fault_q;
                end
            end

            if (deadline_active &&
                (deadline_remaining_high_q != 16'd0 ||
                 deadline_remaining_low_q != 16'd0)) begin
                if (deadline_remaining_low_q == 16'd0) begin
                    deadline_remaining_low_q <= 16'hffff;
                    deadline_remaining_high_q <=
                        deadline_remaining_high_q - 16'd1;
                end else begin
                    deadline_remaining_low_q <=
                        deadline_remaining_low_q - 16'd1;
                end
                if (deadline_remaining_high_q == 16'd0 &&
                    deadline_remaining_low_q == 16'd1)
                    deadline_expired_q <= 1'b1;
            end else if (!deadline_active) begin
                deadline_expired_q <= 1'b0;
            end

            if (soft_reset && command_active && !command_dispatched_q)
                cancel_before_dispatch <= 1'b1;

            case (state)
                    ST_IDLE: begin
                        busy <= 1'b0;
                        command_active <= 1'b0;
                        command_dispatched_q <= 1'b0;
                        deadline_active <= 1'b0;
                        local_engine_reset <= 1'b0;
                        engine_reset_active <= 1'b0;
                        active_submission_ring_offset_q <=
                            submission_ring_offset;
                        active_completion_ring_offset_q <=
                            completion_ring_offset;
                        active_resource_generation_q <=
                            resource_generation;
                        active_protected0_valid_q <= protected0_valid;
                        active_protected0_offset_q <= protected0_offset;
                        active_protected0_bytes_q <= protected0_bytes;
                        active_protected1_valid_q <= protected1_valid;
                        active_protected1_offset_q <= protected1_offset;
                        active_protected1_bytes_q <= protected1_bytes;
                        admission_submission_producer_q <=
                            submission_producer;
                        admission_completion_consumer_q <=
                            completion_consumer;
                        if (!queue_rebase && enable) begin
                            state <= ST_ADMISSION_ARITH;
                        end
                    end

                    ST_ADMISSION_ARITH: begin
                        if (!queue_rebase) begin
                            admission_submission_used_q <=
                                admission_submission_producer_q -
                                submission_consumer;
                            admission_completion_used_q <=
                                completion_producer -
                                admission_completion_consumer_q;
                            active_submission_ring_end_q <=
                                {1'b0, active_submission_ring_offset_q} +
                                SUBMISSION_RING_BYTES;
                            active_completion_ring_end_q <=
                                {1'b0, active_completion_ring_offset_q} +
                                COMPLETION_RING_BYTES;
                            state <= ST_ADMISSION_VALIDATE;
                        end
                    end

                    ST_ADMISSION_VALIDATE: begin
                        if (!queue_rebase) begin
                            admission_configuration_valid_q <=
                                ARENA_LIMIT > ARENA_BASE &&
                                active_submission_ring_offset_q[5:0] ==
                                    6'd0 &&
                                active_completion_ring_offset_q[4:0] ==
                                    5'd0 &&
                                active_submission_ring_end_q <=
                                    {1'b0, ARENA_BYTES} &&
                                active_completion_ring_end_q <=
                                    {1'b0, ARENA_BYTES} &&
                                !({1'b0, active_submission_ring_offset_q} <
                                        active_completion_ring_end_q &&
                                  {1'b0, active_completion_ring_offset_q} <
                                        active_submission_ring_end_q) &&
                                active_resource_generation_q != 32'd0 &&
                                admission_submission_used_q <=
                                    `ASTRA_RENDER_RING_ENTRIES &&
                                admission_completion_used_q <=
                                    `ASTRA_RENDER_RING_ENTRIES;
                            admission_completion_available_q <=
                                admission_completion_used_q <
                                    `ASTRA_RENDER_RING_ENTRIES;
                            state <= ST_ADMISSION_DECIDE;
                        end
                    end

                    ST_ADMISSION_DECIDE: begin
                        if (queue_rebase) begin
                        end else if (admission_submission_used_q == 11'd0) begin
                            busy <= 1'b0;
                            state <= ST_IDLE;
                        end else if (!admission_configuration_valid_q) begin
                            configuration_fault <= 1'b1;
                            last_fault_detail <= 32'h00010000;
                            busy <= 1'b0;
                            state <= ST_IDLE;
                        end else if (!admission_completion_available_q) begin
                            backpressure_cycles <=
                                backpressure_cycles + 32'd1;
                            busy <= 1'b0;
                            state <= ST_IDLE;
                        end else begin
                            busy <= 1'b1;
                            manager_araddr <= ARENA_BASE +
                                active_submission_ring_offset_q +
                                ({21'd0, submission_consumer[9:0]} << 6);
                            manager_arlen <= 8'd7;
                            manager_arvalid <= 1'b1;
                            read_beat_index <= 5'd0;
                            read_error_seen <= 1'b0;
                            command_start_cycle <= cycle_counter;
                            command_active <= 1'b1;
                            command_dispatched_q <= 1'b0;
                            descriptor_capture_enabled_q <= 1'b0;
                            cancel_before_dispatch <= 1'b0;
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_OK;
                            completion_count_q <= 32'd0;
                            completion_fault_q <= 32'd0;
                            commands_submitted <=
                                commands_submitted + 32'd1;
                            state <= ST_COMMAND_AR;
                        end
                    end

                    ST_COMMAND_AR: begin
                        if (manager_arvalid && m_axi_arready) begin
                            manager_arvalid <= 1'b0;
                            state <= ST_COMMAND_R;
                        end
                    end

                    ST_COMMAND_R: begin
                        if (manager_response_valid_q) begin
                            manager_response_valid_q <= 1'b0;
                            command_words[read_beat_index * 2] <=
                                swap32(manager_response_data_q[31:0]);
                            command_words[read_beat_index * 2 + 1] <=
                                swap32(manager_response_data_q[63:32]);
                            if (manager_response_id_q != MANAGER_READ_ID ||
                                manager_response_resp_q != 2'b00)
                                read_error_seen <= 1'b1;
                            read_beat_last_q <= manager_response_last_q;
                            read_beat_expected_last_q <=
                                read_beat_index == 5'd7;
                            state <= ST_COMMAND_R_DECIDE;
                        end
                    end

                    ST_COMMAND_R_DECIDE: begin
                        if (read_beat_last_q !=
                            read_beat_expected_last_q) begin
                            configuration_fault <= 1'b1;
                            last_fault_detail <= 32'h00020001;
                            manager_arvalid <= 1'b0;
                            state <= ST_FATAL;
                        end else if (read_beat_expected_last_q) begin
                            if (read_error_seen) begin
                                command_opcode_q <= 16'd0;
                                command_is_fill_q <= 1'b0;
                                command_is_blit_q <= 1'b0;
                                command_is_geometry_q <= 1'b0;
                                command_is_flood_q <= 1'b0;
                                command_is_glyph_q <= 1'b0;
                                command_sequence_q <= 32'd0;
                                command_generation_q <=
                                    active_resource_generation_q;
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_AXI_READ;
                                completion_fault_q <= 32'h00020002;
                                state <= ST_PREPARE_COMPLETION;
                            end else begin
                                state <= ST_COMMON_VALIDATE;
                            end
                        end else begin
                            read_beat_index <= read_beat_index + 5'd1;
                            state <= ST_COMMAND_R;
                        end
                    end

                    ST_COMMON_VALIDATE: begin
                        command_opcode_q <= command_word1[31:16];
                        command_is_fill_q <= command_word1[31:16] ==
                            `ASTRA_RENDER_OP_FILL;
                        command_is_blit_q <= command_word1[31:16] ==
                            `ASTRA_RENDER_OP_BLIT;
                        command_is_geometry_q <= incoming_is_geometry;
                        command_is_flood_q <= incoming_is_flood;
                        command_is_glyph_q <= incoming_is_glyph;
                        command_flags_q <= command_word1[15:0];
                        command_sequence_q <= command_sequence;
                        command_generation_q <= command_generation;
                        command_deadline_us_q <= command_deadline_us;
                        command_clip_left_q <= command_words[6][31:16];
                        command_clip_top_q <= command_words[6][15:0];
                        command_clip_right_q <= command_words[7][31:16];
                        command_clip_bottom_q <= command_words[7][15:0];
                        destination_descriptor_offset_q <= command_words[8];
                        source_descriptor_offset_q <= command_words[9];
                        auxiliary_descriptor_offset_q <= command_words[10];
                        auxiliary_data_offset_q <= 32'd0;
                        auxiliary_data_bytes_q <= 32'd0;
                        auxiliary_pitch_q <= 32'd0;
                        auxiliary_width_q <= 16'd0;
                        auxiliary_height_q <= 16'd0;
                        same_surface_q <=
                            command_words[8] == command_words[9];
                        state <= ST_VALIDATE_HEADER;
                    end

                    ST_VALIDATE_HEADER: begin
                        if (command_word0[31:16] !=
                            `ASTRA_RENDER_ABI_VERSION) begin
                            header_result_q <= HEADER_BAD_VERSION;
                        end else if (command_word0[15:0] !=
                                     `ASTRA_RENDER_COMMAND_BYTES) begin
                            header_result_q <= HEADER_BAD_SIZE;
                        end else if (!command_is_fill_q &&
                                     !command_is_blit_q &&
                                     !command_is_geometry_q &&
                                     !command_is_flood_q &&
                                     !command_is_glyph_q) begin
                            header_result_q <= HEADER_BAD_OPCODE;
                        end else if (
                            (command_is_fill_q &&
                             command_flags_q != 16'd0) ||
                            (command_is_flood_q &&
                             command_flags_q != 16'd0) ||
                            (command_is_glyph_q &&
                             (command_flags_q &
                              ~`ASTRA_RENDER_GLYPH_FLAG_ALLOWED_MASK) !=
                                 16'd0) ||
                            (command_is_blit_q &&
                             ((command_flags_q &
                               ~`ASTRA_RENDER_FLAG_BLIT_ALLOWED_MASK) !=
                                  16'd0 ||
                              (!command_flags_q[6] &&
                               command_flags_q[11:8] != 4'd0) ||
                              (command_flags_q[6] &&
                               command_flags_q[4]) ||
                              (!command_flags_q[2] &&
                               command_words[15][23:0] != 24'd0) ||
                              (!command_flags_q[4] &&
                               command_words[15][31:24] != 8'd0))) ||
                            (command_is_geometry_q &&
                             ((command_opcode_q == `ASTRA_RENDER_OP_LINE &&
                               command_flags_q != 16'd0) ||
                              ((command_opcode_q == `ASTRA_RENDER_OP_RECT ||
                                command_opcode_q == `ASTRA_RENDER_OP_CIRCLE ||
                                command_opcode_q ==
                                    `ASTRA_RENDER_OP_ELLIPSE) &&
                               (command_flags_q &
                                ~`ASTRA_RENDER_GEOMETRY_FLAG_FILLED) !=
                                   16'd0) ||
                              (command_opcode_q ==
                                   `ASTRA_RENDER_OP_PATTERN_FILL &&
                               (command_flags_q &
                                ~`ASTRA_RENDER_GEOMETRY_FLAG_PATTERN_OPAQUE) !=
                                   16'd0)))) begin
                            header_result_q <= HEADER_BAD_FLAGS;
                        end else begin
                            header_result_q <= HEADER_OK;
                        end
                        state <= ST_VALIDATE_HEADER_DECIDE;
                    end

                    ST_VALIDATE_HEADER_DECIDE: begin
                        case (header_result_q)
                            HEADER_BAD_VERSION: begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_VERSION;
                                completion_fault_q <= command_word0;
                                state <= ST_PREPARE_COMPLETION;
                            end
                            HEADER_BAD_SIZE: begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_SIZE;
                                completion_fault_q <= command_word0;
                                state <= ST_PREPARE_COMPLETION;
                            end
                            HEADER_BAD_OPCODE: begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_OPCODE;
                                completion_fault_q <= command_word1;
                                state <= ST_PREPARE_COMPLETION;
                            end
                            HEADER_BAD_FLAGS: begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_FLAGS;
                                completion_fault_q <= command_word1;
                                state <= ST_PREPARE_COMPLETION;
                            end
                            default: begin
                                state <= ST_VALIDATE_SEQUENCE;
                            end
                        endcase
                    end

                    ST_VALIDATE_SEQUENCE: begin
                        sequence_delta_q <=
                            command_sequence_q - last_sequence;
                        state <= ST_VALIDATE_SEQUENCE_CHECK;
                    end

                    ST_VALIDATE_SEQUENCE_CHECK: begin
                        deadline_budget_q <=
                            command_deadline_us_q * CYCLES_PER_US;
                        if (!sequence_valid_q) begin
                            sequence_result_q <= SEQUENCE_BAD_ORDER;
                        end else if (command_generation_q !=
                                     active_resource_generation_q ||
                                     command_generation_q == 32'd0) begin
                            sequence_result_q <= SEQUENCE_BAD_GENERATION;
                        end else if (command_deadline_us_q == 32'd0 ||
                                     command_deadline_us_q >
                                     `ASTRA_RENDER_MAX_DEADLINE_US) begin
                            sequence_result_q <= SEQUENCE_BAD_DEADLINE;
                        end else begin
                            sequence_result_q <= SEQUENCE_OK;
                        end
                        state <= ST_VALIDATE_SEQUENCE_RESULT;
                    end

                    ST_VALIDATE_SEQUENCE_RESULT: begin
                        case (sequence_result_q)
                            SEQUENCE_BAD_ORDER: begin
                                validation_error_q <= 1'b1;
                                validation_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_SEQUENCE;
                                validation_fault_q <= sequence_delta_q;
                            end
                            SEQUENCE_BAD_GENERATION: begin
                                validation_error_q <= 1'b1;
                                validation_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_GENERATION;
                                validation_fault_q <= command_generation_q;
                            end
                            SEQUENCE_BAD_DEADLINE: begin
                                validation_error_q <= 1'b1;
                                validation_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_RANGE;
                                validation_fault_q <= command_deadline_us_q;
                            end
                            default: begin
                                validation_error_q <= 1'b0;
                                validation_status_q <=
                                    `ASTRA_RENDER_STATUS_OK;
                                validation_fault_q <= 32'd0;
                            end
                        endcase
                        state <= ST_VALIDATE_SEQUENCE_DECIDE;
                    end

                    ST_VALIDATE_SEQUENCE_DECIDE: begin
                        if (validation_error_q) begin
                            completion_status_q <= validation_status_q;
                            completion_fault_q <= validation_fault_q;
                            state <= ST_PREPARE_COMPLETION;
                        end else begin
                            state <= ST_VALIDATE_LAYOUT;
                        end
                    end

                    ST_VALIDATE_LAYOUT: begin
                        layout_bad_clip_q <=
                            $signed(command_clip_left_q) >
                                $signed(command_clip_right_q) ||
                            $signed(command_clip_top_q) >
                                $signed(command_clip_bottom_q);
                        layout_bad_flags_q <= command_words[5] != 32'd0 ||
                            (command_is_blit_q &&
                             (command_flags_q[3] ?
                                auxiliary_descriptor_offset_q == 32'd0 :
                                auxiliary_descriptor_offset_q != 32'd0)) ||
                            (command_is_fill_q &&
                             auxiliary_descriptor_offset_q != 32'd0) ||
                            (command_is_flood_q &&
                             auxiliary_descriptor_offset_q == 32'd0);
                        layout_bad_fill_q <=
                            command_is_fill_q &&
                            (source_descriptor_offset_q != 32'd0 ||
                             command_words[11] != 32'd0 ||
                             command_words[13] != 32'd0);
                        layout_bad_geometry_q <= command_is_geometry_q &&
                            (((command_opcode_q == `ASTRA_RENDER_OP_LINE ||
                               command_opcode_q == `ASTRA_RENDER_OP_RECT) &&
                              (command_words[9] != 32'd0 ||
                               command_words[10] != 32'd0 ||
                               command_words[13] != 32'd0 ||
                               command_words[14] != 32'd0)) ||
                             (command_opcode_q == `ASTRA_RENDER_OP_CIRCLE &&
                              (command_words[9] != 32'd0 ||
                               command_words[10] != 32'd0 ||
                               command_words[12] != 32'd0 ||
                               command_words[13][15:0] != 16'd0 ||
                               command_words[14] != 32'd0)) ||
                             (command_opcode_q == `ASTRA_RENDER_OP_ELLIPSE &&
                              (command_words[9] != 32'd0 ||
                               command_words[10] != 32'd0 ||
                               command_words[12] != 32'd0 ||
                               command_words[14] != 32'd0)) ||
                             (command_opcode_q ==
                                  `ASTRA_RENDER_OP_PATTERN_FILL &&
                              !command_flags_q[1] &&
                              command_words[14] != 32'd0));
                        layout_bad_flood_q <= command_is_flood_q &&
                            (source_descriptor_offset_q != 32'd0 ||
                             command_words[12] != 32'd0 ||
                             command_words[13] != 32'd0 ||
                             command_words[14] != 32'd0);
                        layout_bad_glyph_q <= command_is_glyph_q &&
                            (command_words[10][3:0] != 4'd0 ||
                             command_words[11] == 32'd0 ||
                             command_words[11] >
                                `ASTRA_RENDER_MAX_GLYPH_DESCRIPTORS ||
                             command_words[14][31:8] != 24'd0 ||
                             command_words[15] != 32'd0);
                        glyph_descriptor_end_q <=
                            {1'b0, command_words[10]} +
                            ({20'd0, command_words[11][12:0]} << 4);
                        state <= ST_VALIDATE_GLYPH_RANGE;
                    end

                    ST_VALIDATE_GLYPH_RANGE: begin
                        layout_bad_glyph_q <= layout_bad_glyph_q ||
                            (command_is_glyph_q &&
                             glyph_descriptor_end_q >
                                {1'b0, ARENA_BYTES});
                        state <= ST_VALIDATE_LAYOUT_RESULT;
                    end

                    ST_VALIDATE_LAYOUT_RESULT: begin
                        if (layout_bad_clip_q) begin
                            validation_error_q <= 1'b1;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_CLIP;
                            validation_fault_q <= command_words[6];
                        end else if (layout_bad_flags_q) begin
                            validation_error_q <= 1'b1;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_FLAGS;
                            validation_fault_q <= command_words[5] |
                                                  auxiliary_descriptor_offset_q;
                        end else if (layout_bad_fill_q) begin
                            validation_error_q <= 1'b1;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_FLAGS;
                            validation_fault_q <= 32'h00030001;
                        end else if (layout_bad_geometry_q) begin
                            validation_error_q <= 1'b1;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_FLAGS;
                            validation_fault_q <= 32'h00030002;
                        end else if (layout_bad_flood_q) begin
                            validation_error_q <= 1'b1;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_FLAGS;
                            validation_fault_q <= 32'h00030003;
                        end else if (layout_bad_glyph_q) begin
                            validation_error_q <= 1'b1;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_RANGE;
                            validation_fault_q <= 32'h00030004;
                        end else begin
                            validation_error_q <= 1'b0;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_OK;
                            validation_fault_q <= 32'd0;
                        end
                        state <= ST_VALIDATE_LAYOUT_DECIDE;
                    end

                    ST_VALIDATE_LAYOUT_DECIDE: begin
                        if (validation_error_q) begin
                            completion_status_q <= validation_status_q;
                            completion_fault_q <= validation_fault_q;
                            state <= ST_PREPARE_COMPLETION;
                        end else begin
                            descriptor_check_offset_q <=
                                destination_descriptor_offset_q;
                            descriptor_check_kind_q <= 2'd0;
                            state <= ST_DESCRIPTOR_END;
                        end
                    end

                    ST_DESCRIPTOR_END: begin
                        descriptor_check_end_q <=
                            {1'b0, descriptor_check_offset_q} +
                            `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                        state <= ST_DESCRIPTOR_COMPARE;
                    end

                    ST_DESCRIPTOR_COMPARE: begin
                        descriptor_check_valid_q <=
                            descriptor_check_offset_q[4:0] == 5'd0 &&
                            descriptor_check_end_q <=
                                {1'b0, ARENA_BYTES} &&
                            !({1'b0, descriptor_check_offset_q} <
                                    active_submission_ring_end_q &&
                              {1'b0, active_submission_ring_offset_q} <
                                    descriptor_check_end_q) &&
                            !({1'b0, descriptor_check_offset_q} <
                                    active_completion_ring_end_q &&
                              {1'b0, active_completion_ring_offset_q} <
                                    descriptor_check_end_q);
                        state <= ST_DESCRIPTOR_DECIDE;
                    end

                    ST_DESCRIPTOR_DECIDE: begin
                        if (!descriptor_check_valid_q) begin
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR;
                            completion_fault_q <= descriptor_check_offset_q;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (descriptor_check_kind_q == 2'd0 &&
                                     (command_is_blit_q ||
                                      command_is_glyph_q)) begin
                            descriptor_check_offset_q <=
                                source_descriptor_offset_q;
                            descriptor_check_kind_q <= 2'd1;
                            state <= ST_DESCRIPTOR_END;
                        end else if (descriptor_check_kind_q == 2'd0 &&
                                     command_is_flood_q) begin
                            descriptor_check_offset_q <=
                                auxiliary_descriptor_offset_q;
                            descriptor_check_kind_q <= 2'd2;
                            state <= ST_DESCRIPTOR_END;
                        end else if (descriptor_check_kind_q == 2'd1 &&
                                     command_flags_q[3]) begin
                            descriptor_check_offset_q <=
                                auxiliary_descriptor_offset_q;
                            descriptor_check_kind_q <= 2'd2;
                            state <= ST_DESCRIPTOR_END;
                        end else begin
                            last_sequence <= command_sequence_q;
                            last_sequence_valid <= 1'b1;
                            manager_araddr <= ARENA_BASE +
                                destination_descriptor_offset_q;
                            manager_arlen <= 8'd3;
                            manager_arvalid <= 1'b1;
                            descriptor_capture_enabled_q <= 1'b1;
                            read_beat_index <= 5'd0;
                            read_error_seen <= 1'b0;
                            state <= ST_DESTINATION_AR;
                        end
                    end

                    ST_DESTINATION_AR: begin
                        if (manager_arvalid && m_axi_arready) begin
                            manager_arvalid <= 1'b0;
                            state <= ST_DESTINATION_R;
                        end
                    end

                    ST_DESTINATION_R: begin
                        if (manager_response_valid_q) begin
                            manager_response_valid_q <= 1'b0;
                            if (manager_response_id_q != MANAGER_READ_ID ||
                                manager_response_resp_q != 2'b00)
                                read_error_seen <= 1'b1;
                            read_beat_last_q <= manager_response_last_q;
                            read_beat_expected_last_q <=
                                read_beat_index == 5'd3;
                            state <= ST_DESTINATION_R_DECIDE;
                        end
                    end

                    ST_DESTINATION_R_DECIDE: begin
                        if (read_beat_last_q !=
                            read_beat_expected_last_q) begin
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_AXI_READ;
                            completion_fault_q <= 32'h00040001;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (read_beat_expected_last_q) begin
                            if (read_error_seen) begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_AXI_READ;
                                completion_fault_q <= 32'h00040002;
                                state <= ST_PREPARE_COMPLETION;
                            end else begin
                                state <= ST_DESTINATION_VALIDATE_START;
                            end
                        end else begin
                            read_beat_index <= read_beat_index + 5'd1;
                            state <= ST_DESTINATION_R;
                        end
                    end

                    ST_DESTINATION_VALIDATE_START: begin
                        validator_required_access <=
                            command_is_glyph_q || command_is_flood_q ||
                            (command_is_blit_q && same_surface_q) ?
                                2'b11 : 2'b10;
                        validator_palette_required <= 1'b0;
                        validator_start <= 1'b1;
                        state <= ST_DESTINATION_VALIDATE_WAIT;
                    end

                    ST_DESTINATION_VALIDATE_WAIT: begin
                        if (validator_done) begin
                            validation_error_q <= !validator_valid ||
                                ((command_is_geometry_q || command_is_flood_q ||
                                  command_is_glyph_q) &&
                                 validator_format >
                                     `ASTRA_RENDER_FORMAT_XRGB8888);
                            destination_data_offset_q <= validator_data_offset;
                            destination_data_bytes_q <= validator_data_bytes;
                            destination_pitch_q <= validator_pitch;
                            destination_width_q <= validator_width;
                            destination_height_q <= validator_height;
                            destination_format_q <= validator_format;
                            destination_bpp_q <= validator_bpp;
                            state <= ST_DESTINATION_VALIDATE_DECIDE;
                        end
                    end

                    ST_DESTINATION_VALIDATE_DECIDE: begin
                        if (validation_error_q) begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR;
                                completion_fault_q <=
                                    destination_descriptor_offset_q;
                                state <= ST_PREPARE_COMPLETION;
                        end else begin
                                if ((command_is_blit_q && !same_surface_q) ||
                                    command_is_glyph_q) begin
                                    manager_araddr <= ARENA_BASE +
                                        source_descriptor_offset_q;
                                    manager_arlen <= 8'd3;
                                    manager_arvalid <= 1'b1;
                                    read_beat_index <= 5'd0;
                                    read_error_seen <= 1'b0;
                                    state <= ST_SOURCE_AR;
                                end else if (command_is_flood_q) begin
                                    manager_araddr <= ARENA_BASE +
                                        auxiliary_descriptor_offset_q;
                                    manager_arlen <= 8'd3;
                                    manager_arvalid <= 1'b1;
                                    read_beat_index <= 5'd0;
                                    read_error_seen <= 1'b0;
                                    state <= ST_AUXILIARY_AR;
                                end else begin
                                    source_data_offset_q <=
                                        destination_data_offset_q;
                                    source_data_bytes_q <=
                                        destination_data_bytes_q;
                                    source_pitch_q <= destination_pitch_q;
                                    source_width_q <= destination_width_q;
                                    source_height_q <= destination_height_q;
                                    source_format_q <= destination_format_q;
                                    source_bpp_q <= destination_bpp_q;
                                    source_palette_offset_q <= 32'd0;
                                    state <= ST_RANGE_VALIDATE;
                                end
                        end
                    end

                    ST_SOURCE_AR: begin
                        if (manager_arvalid && m_axi_arready) begin
                            manager_arvalid <= 1'b0;
                            state <= ST_SOURCE_R;
                        end
                    end

                    ST_SOURCE_R: begin
                        if (manager_response_valid_q) begin
                            manager_response_valid_q <= 1'b0;
                            if (manager_response_id_q != MANAGER_READ_ID ||
                                manager_response_resp_q != 2'b00)
                                read_error_seen <= 1'b1;
                            read_beat_last_q <= manager_response_last_q;
                            read_beat_expected_last_q <=
                                read_beat_index == 5'd3;
                            state <= ST_SOURCE_R_DECIDE;
                        end
                    end

                    ST_SOURCE_R_DECIDE: begin
                        if (read_beat_last_q !=
                            read_beat_expected_last_q) begin
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_AXI_READ;
                            completion_fault_q <= 32'h00050001;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (read_beat_expected_last_q) begin
                            if (read_error_seen) begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_AXI_READ;
                                completion_fault_q <= 32'h00050002;
                                state <= ST_PREPARE_COMPLETION;
                            end else begin
                                state <= ST_SOURCE_VALIDATE_START;
                            end
                        end else begin
                            read_beat_index <= read_beat_index + 5'd1;
                            state <= ST_SOURCE_R;
                        end
                    end

                    ST_SOURCE_VALIDATE_START: begin
                        validator_required_access <= 2'b01;
                        validator_palette_required <= command_is_glyph_q ||
                            command_flags_q[5];
                        validator_start <= 1'b1;
                        state <= ST_SOURCE_VALIDATE_WAIT;
                    end

                    ST_SOURCE_VALIDATE_WAIT: begin
                        if (validator_done) begin
                            validation_error_q <= !validator_valid ||
                                (command_is_glyph_q &&
                                 (!(validator_format ==
                                        `ASTRA_RENDER_FORMAT_INDEX8 ||
                                    (validator_format >=
                                        `ASTRA_RENDER_FORMAT_MASK1 &&
                                     validator_format <=
                                        `ASTRA_RENDER_FORMAT_INDEX4)) ||
                                  ((validator_format ==
                                        `ASTRA_RENDER_FORMAT_A4 ||
                                    validator_format ==
                                        `ASTRA_RENDER_FORMAT_A8) &&
                                   destination_format_q ==
                                        `ASTRA_RENDER_FORMAT_INDEX8)));
                            source_data_offset_q <= validator_data_offset;
                            source_data_bytes_q <= validator_data_bytes;
                            source_pitch_q <= validator_pitch;
                            source_width_q <= validator_width;
                            source_height_q <= validator_height;
                            source_format_q <= validator_format;
                            source_bpp_q <= validator_bpp;
                            source_palette_offset_q <= validator_palette_offset;
                            state <= ST_SOURCE_VALIDATE_DECIDE;
                        end
                    end

                    ST_SOURCE_VALIDATE_DECIDE: begin
                        if (validation_error_q) begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR;
                                completion_fault_q <=
                                    source_descriptor_offset_q;
                                state <= ST_PREPARE_COMPLETION;
                        end else begin
                                if (!command_is_glyph_q &&
                                    command_flags_q[3]) begin
                                    manager_araddr <= ARENA_BASE +
                                        auxiliary_descriptor_offset_q;
                                    manager_arlen <= 8'd3;
                                    manager_arvalid <= 1'b1;
                                    read_beat_index <= 5'd0;
                                    read_error_seen <= 1'b0;
                                    state <= ST_AUXILIARY_AR;
                                end else begin
                                    state <= ST_RANGE_VALIDATE;
                                end
                        end
                    end

                    ST_AUXILIARY_AR: begin
                        if (manager_arvalid && m_axi_arready) begin
                            manager_arvalid <= 1'b0;
                            state <= ST_AUXILIARY_R;
                        end
                    end

                    ST_AUXILIARY_R: begin
                        if (manager_response_valid_q) begin
                            manager_response_valid_q <= 1'b0;
                            if (manager_response_id_q != MANAGER_READ_ID ||
                                manager_response_resp_q != 2'b00)
                                read_error_seen <= 1'b1;
                            read_beat_last_q <= manager_response_last_q;
                            read_beat_expected_last_q <=
                                read_beat_index == 5'd3;
                            state <= ST_AUXILIARY_R_DECIDE;
                        end
                    end

                    ST_AUXILIARY_R_DECIDE: begin
                        if (read_beat_last_q !=
                            read_beat_expected_last_q) begin
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_AXI_READ;
                            completion_fault_q <= 32'h00080001;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (read_beat_expected_last_q) begin
                            if (read_error_seen) begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_AXI_READ;
                                completion_fault_q <= 32'h00080002;
                                state <= ST_PREPARE_COMPLETION;
                            end else begin
                                state <= ST_AUXILIARY_VALIDATE_START;
                            end
                        end else begin
                            read_beat_index <= read_beat_index + 5'd1;
                            state <= ST_AUXILIARY_R;
                        end
                    end

                    ST_AUXILIARY_VALIDATE_START: begin
                        validator_required_access <= command_is_flood_q ?
                            2'b11 : 2'b01;
                        validator_palette_required <= 1'b0;
                        validator_start <= 1'b1;
                        state <= ST_AUXILIARY_VALIDATE_WAIT;
                    end

                    ST_AUXILIARY_VALIDATE_WAIT: begin
                        if (validator_done) begin
                            validation_error_q <= !validator_valid ||
                                (command_is_flood_q &&
                                 (validator_format !=
                                      `ASTRA_RENDER_FORMAT_XRGB8888 ||
                                  validator_data_bytes < 32'd4 ||
                                  validator_data_bytes[1:0] != 2'd0)) ||
                                (!command_is_flood_q &&
                                 validator_format !=
                                     `ASTRA_RENDER_FORMAT_MASK1);
                            auxiliary_data_offset_q <= validator_data_offset;
                            auxiliary_data_bytes_q <= validator_data_bytes;
                            auxiliary_pitch_q <= validator_pitch;
                            auxiliary_width_q <= validator_width;
                            auxiliary_height_q <= validator_height;
                            state <= ST_AUXILIARY_VALIDATE_DECIDE;
                        end
                    end

                    ST_AUXILIARY_VALIDATE_DECIDE: begin
                        if (validation_error_q) begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR;
                                completion_fault_q <=
                                    auxiliary_descriptor_offset_q;
                                state <= ST_PREPARE_COMPLETION;
                        end else begin
                            state <= ST_RANGE_VALIDATE;
                        end
                    end

                    ST_RANGE_VALIDATE: begin
                        range_check_index_q <= 5'd0;
                        state <= ST_RANGE_LOAD;
                    end

                    // Reuse one registered overlap checker for every policy
                    // range. Command setup is bounded and infrequent; keeping
                    // eleven 33-bit comparisons out of one completion cone is
                    // both smaller and substantially easier to time at 200 MHz.
                    ST_RANGE_LOAD: begin
                        range_check_enabled_q <= 1'b1;
                        range_check_protected_q <= 1'b0;
                        range_first_offset_q <= destination_data_offset_q;
                        range_first_bytes_q <= destination_data_bytes_q;
                        range_second_offset_q <=
                            active_submission_ring_offset_q;
                        range_second_bytes_q <= SUBMISSION_RING_BYTES;
                        case (range_check_index_q)
                            4'd1: begin
                                range_second_offset_q <=
                                    active_completion_ring_offset_q;
                                range_second_bytes_q <=
                                    COMPLETION_RING_BYTES;
                            end
                            4'd2: begin
                                range_second_offset_q <=
                                    destination_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            4'd3: begin
                                range_check_enabled_q <= command_is_blit_q ||
                                    command_is_glyph_q;
                                range_second_offset_q <=
                                    source_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            4'd4: begin
                                range_check_enabled_q <=
                                    active_protected0_valid_q;
                                range_check_protected_q <= 1'b1;
                                range_second_offset_q <=
                                    active_protected0_offset_q;
                                range_second_bytes_q <=
                                    active_protected0_bytes_q;
                            end
                            4'd5: begin
                                range_check_enabled_q <=
                                    active_protected1_valid_q;
                                range_check_protected_q <= 1'b1;
                                range_second_offset_q <=
                                    active_protected1_offset_q;
                                range_second_bytes_q <=
                                    active_protected1_bytes_q;
                            end
                            4'd6: begin
                                range_check_enabled_q <= command_is_blit_q ||
                                    command_is_glyph_q;
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                            end
                            4'd7: begin
                                range_check_enabled_q <= command_is_blit_q ||
                                    command_is_glyph_q;
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                                range_second_offset_q <=
                                    active_completion_ring_offset_q;
                                range_second_bytes_q <=
                                    COMPLETION_RING_BYTES;
                            end
                            4'd8: begin
                                range_check_enabled_q <= command_is_blit_q ||
                                    command_is_glyph_q;
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                                range_second_offset_q <=
                                    destination_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            4'd9: begin
                                range_check_enabled_q <= command_is_blit_q ||
                                    command_is_glyph_q;
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                                range_second_offset_q <=
                                    source_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            4'd10: begin
                                range_check_enabled_q <=
                                    (command_is_blit_q && !same_surface_q) ||
                                    command_is_glyph_q;
                                range_second_offset_q <= source_data_offset_q;
                                range_second_bytes_q <= source_data_bytes_q;
                            end
                            5'd11: begin
                                range_check_enabled_q <=
                                    command_uses_palette_q;
                                range_first_offset_q <=
                                    source_palette_offset_q;
                                range_first_bytes_q <= source_palette_bytes_q;
                            end
                            5'd12: begin
                                range_check_enabled_q <=
                                    command_uses_palette_q;
                                range_first_offset_q <=
                                    source_palette_offset_q;
                                range_first_bytes_q <= source_palette_bytes_q;
                                range_second_offset_q <=
                                    active_completion_ring_offset_q;
                                range_second_bytes_q <=
                                    COMPLETION_RING_BYTES;
                            end
                            5'd13: begin
                                range_check_enabled_q <=
                                    command_uses_palette_q;
                                range_first_offset_q <=
                                    source_palette_offset_q;
                                range_first_bytes_q <= source_palette_bytes_q;
                                range_second_offset_q <=
                                    destination_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd14: begin
                                range_check_enabled_q <=
                                    command_uses_palette_q;
                                range_first_offset_q <=
                                    source_palette_offset_q;
                                range_first_bytes_q <= source_palette_bytes_q;
                                range_second_offset_q <=
                                    source_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd15: begin
                                range_check_enabled_q <=
                                    command_uses_palette_q;
                                range_first_offset_q <=
                                    source_palette_offset_q;
                                range_first_bytes_q <= source_palette_bytes_q;
                                range_second_offset_q <=
                                    destination_data_offset_q;
                                range_second_bytes_q <=
                                    destination_data_bytes_q;
                            end
                            5'd16: begin
                                range_check_enabled_q <=
                                    command_uses_palette_q;
                                range_first_offset_q <=
                                    source_palette_offset_q;
                                range_first_bytes_q <= source_palette_bytes_q;
                                range_second_offset_q <= source_data_offset_q;
                                range_second_bytes_q <= source_data_bytes_q;
                            end
                            5'd17: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q || command_is_glyph_q;
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                            end
                            5'd18: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q || command_is_glyph_q;
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    active_completion_ring_offset_q;
                                range_second_bytes_q <=
                                    COMPLETION_RING_BYTES;
                            end
                            5'd19: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q || command_is_glyph_q;
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    destination_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd20: begin
                                range_check_enabled_q <=
                                    (command_is_blit_q && command_flags_q[3]) ||
                                    command_is_glyph_q;
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    source_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd21: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q || command_is_glyph_q;
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    command_is_glyph_q ?
                                    destination_data_offset_q :
                                    auxiliary_descriptor_offset_q;
                                range_second_bytes_q <= command_is_glyph_q ?
                                    destination_data_bytes_q :
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd22: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q || command_is_glyph_q;
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    command_is_glyph_q ? source_data_offset_q :
                                    destination_data_offset_q;
                                range_second_bytes_q <=
                                    command_is_glyph_q ? source_data_bytes_q :
                                    destination_data_bytes_q;
                            end
                            5'd23: begin
                                range_check_enabled_q <=
                                    (command_is_blit_q && command_flags_q[3]) ||
                                    (command_is_glyph_q && command_uses_palette_q);
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <= command_is_glyph_q ?
                                    source_palette_offset_q : source_data_offset_q;
                                range_second_bytes_q <= command_is_glyph_q ?
                                    source_palette_bytes_q : source_data_bytes_q;
                            end
                            5'd24: begin
                                range_check_enabled_q <=
                                    (command_is_blit_q && command_flags_q[3] &&
                                     command_flags_q[5]) ||
                                    (command_is_glyph_q &&
                                     active_protected0_valid_q);
                                range_check_protected_q <= command_is_glyph_q;
                                range_first_offset_q <=
                                    command_is_glyph_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_is_glyph_q ? glyph_descriptor_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    command_is_glyph_q ?
                                    active_protected0_offset_q :
                                    source_palette_offset_q;
                                range_second_bytes_q <= command_is_glyph_q ?
                                    active_protected0_bytes_q : 32'd1024;
                            end
                            5'd25: begin
                                range_check_enabled_q <=
                                    command_is_glyph_q ?
                                    active_protected1_valid_q :
                                    command_uses_auxiliary_q;
                                range_check_protected_q <= command_is_glyph_q;
                                range_first_offset_q <= command_is_glyph_q ?
                                    command_words[10] : destination_data_offset_q;
                                range_first_bytes_q <= command_is_glyph_q ?
                                    glyph_descriptor_bytes_q :
                                    destination_data_bytes_q;
                                range_second_offset_q <=
                                    command_is_glyph_q ?
                                    active_protected1_offset_q :
                                    auxiliary_descriptor_offset_q;
                                range_second_bytes_q <= command_is_glyph_q ?
                                    active_protected1_bytes_q :
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd26: begin
                                range_check_enabled_q <=
                                    command_is_blit_q && command_flags_q[3];
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                                range_second_offset_q <=
                                    auxiliary_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd27: begin
                                range_check_enabled_q <=
                                    command_is_blit_q && command_flags_q[3] &&
                                    command_flags_q[5];
                                range_first_offset_q <=
                                    source_palette_offset_q;
                                range_first_bytes_q <= 32'd1024;
                                range_second_offset_q <=
                                    auxiliary_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd28: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q &&
                                    active_protected0_valid_q;
                                range_check_protected_q <= 1'b1;
                                range_first_offset_q <=
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    active_protected0_offset_q;
                                range_second_bytes_q <=
                                    active_protected0_bytes_q;
                            end
                            5'd29: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q &&
                                    active_protected1_valid_q;
                                range_check_protected_q <= 1'b1;
                                range_first_offset_q <=
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    active_protected1_offset_q;
                                range_second_bytes_q <=
                                    active_protected1_bytes_q;
                            end
                            default: begin
                            end
                        endcase
                        state <= ST_RANGE_END;
                    end

                    ST_RANGE_END: begin
                        range_first_end_q <=
                            {1'b0, range_first_offset_q} +
                            {1'b0, range_first_bytes_q};
                        range_second_end_q <=
                            {1'b0, range_second_offset_q} +
                            {1'b0, range_second_bytes_q};
                        state <= ST_RANGE_COMPARE;
                    end

                    ST_RANGE_COMPARE: begin
                        range_overlap_q <= range_check_enabled_q &&
                            range_first_bytes_q != 32'd0 &&
                            range_second_bytes_q != 32'd0 &&
                            {1'b0, range_first_offset_q} <
                                range_second_end_q &&
                            {1'b0, range_second_offset_q} <
                                range_first_end_q;
                        state <= ST_RANGE_DECIDE;
                    end

                    ST_RANGE_DECIDE: begin
                        if (range_overlap_q) begin
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_RANGE;
                            completion_fault_q <= range_check_protected_q ?
                                32'h00060002 : 32'h00060001;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (range_check_index_q == 5'd29) begin
                            state <= ST_DISPATCH;
                        end else begin
                            range_check_index_q <=
                                range_check_index_q + 5'd1;
                            state <= ST_RANGE_LOAD;
                        end
                    end

                    ST_DISPATCH: begin
                        if (cancel_before_dispatch || soft_reset) begin
                            cancel_before_dispatch <= 1'b0;
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_RESET;
                            completion_count_q <= 32'd0;
                            completion_fault_q <= 32'h00000001;
                            reset_completion_pending <= 1'b1;
                            reset_completion_status <=
                                `ASTRA_RENDER_STATUS_RESET;
                            reset_count <= reset_count + 32'd1;
                            local_engine_reset <= 1'b1;
                            engine_reset_active <= 1'b1;
                            reset_hold_count <= RESET_HOLD_CYCLES - 1;
                            state <= ST_ENGINE_RESET_HOLD;
                        end else begin
                            deadline_remaining_low_q <=
                                deadline_budget_q[15:0];
                            deadline_remaining_high_q <=
                                deadline_budget_q[31:16];
                            deadline_active <= 1'b1;
                            if (command_is_geometry_q)
                                geometry_start <= 1'b1;
                            else if (command_is_flood_q)
                                flood_start <= 1'b1;
                            else if (command_is_glyph_q)
                                glyph_start <= 1'b1;
                            else
                                blitter_start <= 1'b1;
                            command_dispatched_q <= 1'b1;
                            state <= ST_EXECUTE;
                        end
                    end

                    ST_EXECUTE: begin
                        if ((soft_reset || deadline_expired_q) &&
                            command_active) begin
                            deadline_active <= 1'b0;
                            if (command_is_geometry_q)
                                geometry_abort <= 1'b1;
                            else if (command_is_flood_q)
                                flood_abort <= 1'b1;
                            else if (command_is_glyph_q)
                                glyph_abort <= 1'b1;
                            else
                                blitter_abort <= 1'b1;
                            engine_reset_active <= 1'b1;
                            reset_completion_pending <= 1'b1;
                            reset_completion_status <= soft_reset ?
                                `ASTRA_RENDER_STATUS_RESET :
                                `ASTRA_RENDER_STATUS_TIMEOUT;
                            completion_fault_q <= soft_reset ?
                                32'h00000001 : 32'h00000002;
                            manager_arvalid <= 1'b0;
                            completion_awvalid <= 1'b0;
                            completion_wvalid <= 1'b0;
                            reset_count <= reset_count + 32'd1;
                            if (!soft_reset)
                                timeout_count <= timeout_count + 32'd1;
                            state <= ST_ABORT_WAIT;
                        end else if ((command_is_geometry_q && geometry_done) ||
                                     (command_is_flood_q && flood_done) ||
                                     (command_is_glyph_q && glyph_done) ||
                                     (!command_is_geometry_q &&
                                      !command_is_flood_q &&
                                      !command_is_glyph_q && blitter_done)) begin
                            completion_status_q <= command_is_glyph_q ?
                                glyph_status : command_is_flood_q ?
                                flood_status : command_is_geometry_q ?
                                geometry_status : blitter_status;
                            completion_count_q <= command_is_glyph_q ?
                                glyph_completed_pixels : command_is_flood_q ?
                                flood_completed_pixels : command_is_geometry_q ?
                                geometry_completed_pixels :
                                blitter_completed_pixels;
                            completion_fault_q <= command_is_glyph_q ?
                                glyph_fault_detail : command_is_flood_q ?
                                flood_fault_detail : command_is_geometry_q ?
                                geometry_fault_detail : blitter_fault_detail;
                            deadline_active <= 1'b0;
                            state <= ST_PREPARE_COMPLETION;
                        end
                    end

                    ST_PREPARE_COMPLETION: begin
                        deadline_active <= 1'b0;
                        cancel_before_dispatch <= 1'b0;
                        completion_words[0] <=
                            {16'(`ASTRA_RENDER_ABI_VERSION),
                             16'(`ASTRA_RENDER_COMPLETION_BYTES)};
                        completion_words[1] <=
                            {command_opcode_q, completion_status_q};
                        completion_words[2] <= command_sequence_q;
                        completion_words[3] <= completion_count_q;
                        completion_words[4] <= command_start_cycle;
                        completion_words[5] <= cycle_counter;
                        completion_words[6] <= completion_fault_q;
                        completion_words[7] <= command_generation_q;
                        completion_write_address_q <= ARENA_BASE +
                            active_completion_ring_offset_q +
                            ({21'd0, completion_producer[9:0]} << 5);
                        completion_awvalid <= 1'b1;
                        completion_beat_index <= 2'd0;
                        state <= ST_COMPLETION_AW;
                    end

                    ST_COMPLETION_AW: begin
                        if (completion_awvalid && m_axi_awready) begin
                            completion_awvalid <= 1'b0;
                            completion_wvalid <= 1'b1;
                            state <= ST_COMPLETION_W;
                        end
                    end

                    ST_COMPLETION_W: begin
                        if (completion_wvalid && m_axi_wready) begin
                            if (completion_beat_index == 2'd3) begin
                                completion_wvalid <= 1'b0;
                                state <= ST_COMPLETION_B;
                            end else begin
                                completion_beat_index <=
                                    completion_beat_index + 2'd1;
                            end
                        end
                    end

                    ST_COMPLETION_B: begin
                        if (m_axi_bvalid) begin
                            if (m_axi_bid != COMPLETION_WRITE_ID ||
                                m_axi_bresp != 2'b00) begin
                                configuration_fault <= 1'b1;
                                last_fault_detail <= {16'h0007,
                                    {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_bid},
                                    6'd0, m_axi_bresp};
                                state <= ST_FATAL;
                            end else begin
                                retire_commit_q <= 1'b1;
                                state <= ST_RETIRE;
                            end
                        end
                    end

                    ST_RETIRE: begin
                        state <= ST_IDLE;
                    end

                    ST_ABORT_WAIT: begin
                        engine_reset_active <= 1'b1;
                        if (command_is_geometry_q)
                            geometry_abort <= 1'b1;
                        else if (command_is_flood_q)
                            flood_abort <= 1'b1;
                        else if (command_is_glyph_q)
                            glyph_abort <= 1'b1;
                        else
                            blitter_abort <= 1'b1;
                        if ((command_is_geometry_q && geometry_done) ||
                            (command_is_flood_q && flood_done) ||
                            (command_is_glyph_q && glyph_done) ||
                            (!command_is_geometry_q && !command_is_flood_q &&
                             !command_is_glyph_q &&
                             blitter_done)) begin
                            local_engine_reset <= 1'b1;
                            reset_hold_count <= RESET_HOLD_CYCLES - 1;
                            state <= ST_ENGINE_RESET_HOLD;
                        end
                    end

                    ST_ENGINE_RESET_HOLD: begin
                        engine_reset_active <= 1'b1;
                        local_engine_reset <= 1'b1;
                        if (reset_hold_count == 8'd0) begin
                            engine_reset_active <= 1'b0;
                            local_engine_reset <= 1'b0;
                            completion_status_q <= reset_completion_status;
                            completion_count_q <= 32'd0;
                            state <= reset_completion_pending ?
                                ST_PREPARE_COMPLETION : ST_IDLE;
                            reset_completion_pending <= 1'b0;
                        end else begin
                            reset_hold_count <= reset_hold_count - 8'd1;
                        end
                    end

                    ST_FATAL: begin
                        busy <= 1'b0;
                        command_active <= 1'b0;
                        command_dispatched_q <= 1'b0;
                        deadline_active <= 1'b0;
                    end

                    default: begin
                        configuration_fault <= 1'b1;
                        last_fault_detail <= 32'hffff0000;
                        state <= ST_FATAL;
                    end
            endcase
        end
    end
endmodule

`default_nettype wire
