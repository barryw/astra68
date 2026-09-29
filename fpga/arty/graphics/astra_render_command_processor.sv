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
    (* extract_enable = "no" *)
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
    localparam [AXI_ID_WIDTH-1:0] BLITTER_WRITE_ID =
        {{(AXI_ID_WIDTH-3){1'b0}}, 3'b110};
    localparam [AXI_ID_WIDTH-1:0] TEXTURE_READ_ID =
        {{(AXI_ID_WIDTH-3){1'b0}}, 3'b111};
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
    localparam [5:0] ST_RETIRE = 6'd19;
    localparam [5:0] ST_ABORT_WAIT = 6'd20;
    localparam [5:0] ST_ENGINE_RESET_HOLD = 6'd21;
    // FILL_RECTS record loop.
    localparam [5:0] ST_RECTS_NEXT = 6'd24;
    localparam [5:0] ST_RECTS_AR = 6'd25;
    localparam [5:0] ST_RECTS_R = 6'd26;
    localparam [5:0] ST_RECTS_LOAD = 6'd18;
    localparam [5:0] ST_RECTS_WAIT = 6'd61;
    localparam [5:0] ST_FATAL = 6'd22;
    localparam [5:0] ST_RANGE_LOAD = 6'd23;
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
    localparam [5:0] ST_VALIDATE_LAYOUT_WORDS = 6'd55;
    localparam [5:0] ST_AUXILIARY_AR = 6'd45;
    localparam [5:0] ST_AUXILIARY_R = 6'd46;
    localparam [5:0] ST_AUXILIARY_R_DECIDE = 6'd47;
    localparam [5:0] ST_AUXILIARY_VALIDATE_START = 6'd48;
    localparam [5:0] ST_AUXILIARY_VALIDATE_WAIT = 6'd49;
    localparam [5:0] ST_DESTINATION_VALIDATE_DECIDE = 6'd50;
    localparam [5:0] ST_SOURCE_VALIDATE_DECIDE = 6'd51;
    localparam [5:0] ST_AUXILIARY_VALIDATE_DECIDE = 6'd52;
    localparam [5:0] ST_VALIDATE_GLYPH_RANGE = 6'd53;
    localparam [5:0] ST_ADMISSION_COMBINE = 6'd54;
    localparam [5:0] ST_CAPTURE_ENGINE_COMPLETION = 6'd56;
    localparam [5:0] ST_PREFETCH_R = 6'd57;
    localparam [5:0] ST_DESC_LOOKUP = 6'd58;
    localparam [5:0] ST_DESC_LOOKUP_DECIDE = 6'd59;
    localparam [5:0] ST_DESC_CACHE_LOAD = 6'd60;

    // Commands are fetched in bursts of up to PREFETCH_COMMANDS published
    // slots; descriptors referenced by the previous command are reused.
    localparam integer PREFETCH_COMMANDS = 32;

    localparam [2:0] HEADER_OK = 3'd0;
    localparam [2:0] HEADER_BAD_VERSION = 3'd1;
    localparam [2:0] HEADER_BAD_SIZE = 3'd2;
    localparam [2:0] HEADER_BAD_OPCODE = 3'd3;
    localparam [2:0] HEADER_BAD_FLAGS = 3'd4;

    localparam [1:0] SEQUENCE_OK = 2'd0;
    localparam [1:0] SEQUENCE_BAD_ORDER = 2'd1;
    localparam [1:0] SEQUENCE_BAD_GENERATION = 2'd2;
    localparam [1:0] SEQUENCE_BAD_DEADLINE = 2'd3;

    localparam integer DEADLINE_SUBCYCLE_WIDTH =
        CYCLES_PER_US <= 1 ? 1 : $clog2(CYCLES_PER_US);

    function automatic [31:0] swap32(input [31:0] value);
        begin
            swap32 = {value[7:0], value[15:8],
                      value[23:16], value[31:24]};
        end
    endfunction

    initial begin
        if (CYCLES_PER_US < 1)
            $fatal(1, "CYCLES_PER_US must be positive");
    end

    (* fsm_encoding = "one_hot" *) reg [5:0] state;
    reg [31:0] cycle_counter;
    reg command_active;
    reg command_dispatched_q;
    reg [31:0] command_start_cycle;
    reg [31:0] deadline_remaining_us_q;
    reg [DEADLINE_SUBCYCLE_WIDTH-1:0] deadline_subcycle_q;
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
    (* max_fanout = 4 *) reg engine_response_valid_q;
    reg [63:0] engine_response_data_q;
    reg engine_response_error_q;
    reg engine_response_last_q;
    reg engine_response_spill_valid_q;
    reg [63:0] engine_response_spill_data_q;
    reg engine_response_spill_error_q;
    reg engine_response_spill_last_q;
    reg [AXI_ID_WIDTH-1:0] engine_expected_id_q;
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
(* max_fanout = 16 *) reg command_is_triangles_q;
(* max_fanout = 16 *) reg command_is_fill_rects_q;
(* max_fanout = 16 *) reg command_is_lines_q;
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
    // Range checker pipeline: one policy pair enters per cycle.
    reg range_load_active_q;
    reg range_load_valid_q, range_load_last_q;
    reg range_end_valid_q, range_end_last_q, range_end_enabled_q,
        range_end_protected_q;
    reg [31:0] range_end_first_offset_q, range_end_second_offset_q;
    reg range_compare_last_q, range_compare_protected_q;

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
    reg admission_ring_alignment_valid_q;
    reg admission_submission_in_bounds_q;
    reg admission_completion_in_bounds_q;
    reg admission_rings_overlap_q;

    reg validation_error_q;
    reg [15:0] validation_status_q;
    reg [31:0] validation_fault_q;
    reg layout_bad_clip_q;
    reg layout_bad_flags_q;
    reg layout_bad_fill_q;
    reg layout_bad_geometry_q;
    reg layout_bad_geometry_line_q;
    reg layout_bad_geometry_circle_q;
    reg layout_bad_geometry_ellipse_q;
    reg layout_bad_geometry_pattern_q;
    reg layout_bad_flood_q;
    reg layout_bad_array_q;
    reg layout_bad_triangles_q;
    reg [6:0] layout_word_nonzero_q;
    reg [32:0] array_end_q;
    reg [2:0] header_result_q;
    reg [1:0] sequence_result_q;
    reg [31:0] submission_command_address_q;

    reg [15:0] completion_status_q;
    reg [31:0] completion_count_q;
    reg [31:0] completion_fault_q;
    reg completion_failed_q;
reg [31:0] completion_end_cycle_q;
    // Posted writes. Engines finish when their last write is issued; this
    // processor counts engine write responses. A command's completion
    // record waits in an in-order queue until every engine write issued
    // before it has been answered, is written between engine bursts, and
    // retires on its own response.
    // Four records in flight cover two memory round trips of small
    // commands; the fifth command waits to enqueue.
    localparam integer CQ_BITS = 2;
    localparam integer CQ_DEPTH = 1 << CQ_BITS;
    localparam [11:0] ENGINE_WRITE_LIMIT = 12'd64;
    reg completion_fatal_q;
    reg [11:0] engine_issued_q;
    reg [11:0] engine_acked_q;
    reg signed [3:0] engine_burst_open_q;
    reg completion_owns_q;
    reg completion_aw_done_q;
    reg [1:0] completion_beat_q;
    reg completion_override_q;
    reg [CQ_BITS-1:0] cq_head_q;
    reg [CQ_BITS-1:0] cq_issue_q;
    reg [CQ_BITS-1:0] cq_tail_q;
    reg [CQ_BITS:0] cq_count_q;
    reg [CQ_BITS:0] cq_inflight_q;
    reg [11:0] cq_mark_q [0:CQ_DEPTH-1];
    reg [9:0] cq_slot_q [0:CQ_DEPTH-1];
    reg [CQ_DEPTH-1:0] cq_werr_q;
    reg [7:0] cq_werr_detail_q [0:CQ_DEPTH-1];
    (* ramstyle = "MLAB", ram_style = "distributed" *)
    reg [63:0] cq_beats [0:CQ_DEPTH*4-1];
    (* ramstyle = "MLAB", ram_style = "distributed" *)
    reg [64:0] cq_info [0:CQ_DEPTH-1];
    reg [64:0] head_info_q;
    reg cq_beat_we_q;
    reg [CQ_BITS+1:0] cq_beat_waddr_q;
    reg [63:0] cq_beat_wdata_q;
    reg cq_info_we_q;
    reg [CQ_BITS-1:0] cq_info_waddr_q;
    reg [64:0] cq_info_wdata_q;
    reg [1:0] enqueue_beat_q;
    reg cur_werr_q;
    reg [7:0] cur_werr_detail_q;

    // Command prefetch and descriptor cache share one block RAM:
    // beats 0..255 hold prefetched commands, 256.. hold descriptor slots.
    (* ramstyle = "M20K", ram_style = "block" *)
    reg [64:0] local_ram [0:511];
    reg [64:0] local_ram_q;
    reg [8:0] local_ram_raddr_q;
    reg local_ram_we;
    reg [8:0] local_ram_waddr;
    reg [64:0] local_ram_wdata;
    reg [10:0] intake_pointer_q;
    reg [5:0] prefetch_count_q;
    reg [5:0] prefetch_next_q;
    reg [7:0] prefetch_beat_q;
    reg [7:0] prefetch_last_beat_q;
    reg [31:0] prefetch_ring_offset_q;
    reg [10:0] prefetch_available_q;
    reg [10:0] prefetch_to_wrap_q;
    reg [5:0] prefetch_limit_q;
    reg [5:0] prefetch_burst_q;
    reg [3:0] load_issue_q;
    reg [3:0] load_capture_q;
    reg load_valid1_q, load_valid2_q;
    // Descriptor slots: valid, tag, referenced by the current command.
    reg [2:0] slot_valid_q;
    reg [2:0] slot_used_q;
    reg [31:0] slot_tag_q [0:2];
    reg [2:0] slot_hit_q;
    reg [1:0] slot_fill_q;
    reg [1:0] desc_kind_q;
    reg [31:0] lookup_offset_q;
    reg [10:0] epoch_limit_q;
    reg [29:0] range_mask_q;

    // FILL_RECTS: records are read in page-bounded bursts of up to 64 into
    // the local RAM's upper quarter, then run one at a time as FILLs.
    reg rects_reading_q;
    reg [12:0] rects_remaining_q;
    reg [12:0] rects_unfetched_q;
    reg [31:0] rects_address_q;
    reg [6:0] rects_buffered_q;
    reg [6:0] rects_index_q;
    reg [6:0] rects_chunk_q;
    reg [7:0] rects_beat_q;
    reg rects_read_error_q;
    reg [31:0] rects_color_q;
    reg rects_record_color_q;
    reg rects_blend_q;
    // Blended records: the rectangles of the last four records, each with
    // the engine write count at which its writes were all issued. A record
    // waits only while an overlapping one still has unanswered writes, or
    // while all four are still unanswered.
    localparam integer RECT_SLOTS = 4;
    reg [RECT_SLOTS-1:0] rects_slot_valid_q;
    reg [RECT_SLOTS-1:0] rects_slot_marked_q;
    reg [11:0] rects_slot_mark_q [0:RECT_SLOTS-1];
    reg signed [17:0] rects_slot_x0_q [0:RECT_SLOTS-1];
    reg signed [17:0] rects_slot_y0_q [0:RECT_SLOTS-1];
    reg signed [17:0] rects_slot_x1_q [0:RECT_SLOTS-1];
    reg signed [17:0] rects_slot_y1_q [0:RECT_SLOTS-1];
    reg [1:0] rects_slot_q;
    reg [31:0] rects_pixels_q;
    reg rects_ran_q;

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
    wire incoming_is_triangles = command_word1[31:16] ==
        `ASTRA_RENDER_OP_TRIANGLES;
    wire [31:0] command_sequence = command_words[2];
    wire [31:0] command_generation = command_words[3];
    wire [31:0] command_deadline_us = command_words[4];
    wire sequence_valid_q = command_sequence_q != 32'd0 &&
        (!last_sequence_valid ||
         (sequence_delta_q != 32'd0 && !sequence_delta_q[31]));
    wire command_uses_auxiliary_q = command_is_flood_q ||
        (command_is_blit_q && command_flags_q[3]);
    // A textured TRIANGLES command reads a source surface; its vertex array
    // takes the glyph descriptor array's place in every range check.
    wire triangles_textured_q = command_is_triangles_q &&
        source_descriptor_offset_q != 32'd0;
    wire command_reads_source_q = command_is_blit_q || command_is_glyph_q ||
        triangles_textured_q;
    // FILL_RECTS and LINES run a record array through the blitter's FILL
    // and the geometry engine's LINE, one record at a time.
    wire command_is_records_q = command_is_fill_rects_q || command_is_lines_q;
    wire engine_geometry_q = command_is_geometry_q || command_is_lines_q;
    wire command_uses_array_q = command_is_glyph_q || command_is_triangles_q ||
        command_is_records_q;
    wire command_uses_palette_q =
        (command_is_blit_q && command_flags_q[5]) ||
        ((command_is_glyph_q || triangles_textured_q) &&
         (source_format_q == `ASTRA_RENDER_FORMAT_INDEX4 ||
          source_format_q == `ASTRA_RENDER_FORMAT_INDEX8));
    wire [31:0] source_palette_bytes_q = command_is_glyph_q &&
        source_format_q == `ASTRA_RENDER_FORMAT_INDEX4 ? 32'd64 : 32'd1024;
    wire [31:0] array_bytes_q = command_is_glyph_q || command_is_records_q ?
        {19'd0, command_words[11][12:0]} << 4 :
        ({19'd0, command_words[11][12:0]} << 6) +
        ({19'd0, command_words[11][12:0]} << 5);

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
    reg engine_writer_abort_q;
    reg engine_writer_flush_pending_q;
    wire writer_busy;
    wire writer_done;
    wire writer_aborted;
    wire writer_error;
    wire [31:0] writer_fault_detail;
    reg engine_writer_done_q;
    reg engine_writer_aborted_q;
    reg engine_writer_error_q;
    reg [31:0] engine_writer_fault_detail_q;
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
    wire blitter_copy_write_active;
    wire [AXI_ID_WIDTH-1:0] blitter_awid;
    wire [31:0] blitter_awaddr;
    wire [7:0] blitter_awlen;
    wire [2:0] blitter_awsize;
    wire [1:0] blitter_awburst;
    wire [3:0] blitter_awcache;
    wire [2:0] blitter_awprot;
    wire [3:0] blitter_awqos;
    wire blitter_awvalid;
    wire blitter_awready;
    wire [63:0] blitter_wdata;
    wire [7:0] blitter_wstrb;
    wire blitter_wlast;
    wire blitter_wvalid;
    wire blitter_wready;
    wire [AXI_ID_WIDTH-1:0] blitter_bid;
    wire [1:0] blitter_bresp;
    wire blitter_bvalid;
    wire blitter_bready;

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

    reg texture_start;
    reg texture_abort;
    wire texture_busy;
    wire texture_done;
    wire [15:0] texture_status;
    wire [31:0] texture_fault_detail;
    wire [31:0] texture_completed_pixels;
    wire texture_writer_start;
    wire texture_writer_abort;
    wire texture_writer_flush;
    wire texture_writer_barrier;
    wire texture_pixel_valid;
    wire texture_pixel_ready;
    wire [31:0] texture_pixel_address;
    wire [7:0] texture_pixel_format;
    wire [31:0] texture_pixel_value;
    wire [AXI_ID_WIDTH-1:0] texture_arid;
    wire [31:0] texture_araddr;
    wire [7:0] texture_arlen;
    wire [2:0] texture_arsize;
    wire [1:0] texture_arburst;
    wire [3:0] texture_arcache;
    wire [2:0] texture_arprot;
    wire [3:0] texture_arqos;
    wire texture_arvalid;
    wire texture_arready;
    wire texture_rready;

    astra_render_blitter #(
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID(BLITTER_READ_ID),
        .WRITE_ID(BLITTER_WRITE_ID),
        .POSTED_WRITES(1'b1)
    ) blitter_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(blitter_start),
        .abort(blitter_abort),
        .is_fill(command_is_fill_q || command_is_fill_rects_q),
        .fill_blend(rects_blend_q),
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
        .writer_done(engine_writer_done_q),
        .writer_aborted(engine_writer_aborted_q),
        .writer_error(engine_writer_error_q),
        .writer_fault_detail(engine_writer_fault_detail_q),
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
        .m_axi_rready(blitter_rready),
        .copy_write_active(blitter_copy_write_active),
        .m_axi_awid(blitter_awid),
        .m_axi_awaddr(blitter_awaddr),
        .m_axi_awlen(blitter_awlen),
        .m_axi_awsize(blitter_awsize),
        .m_axi_awburst(blitter_awburst),
        .m_axi_awcache(blitter_awcache),
        .m_axi_awprot(blitter_awprot),
        .m_axi_awqos(blitter_awqos),
        .m_axi_awvalid(blitter_awvalid),
        .m_axi_awready(blitter_awready),
        .m_axi_wdata(blitter_wdata),
        .m_axi_wstrb(blitter_wstrb),
        .m_axi_wlast(blitter_wlast),
        .m_axi_wvalid(blitter_wvalid),
        .m_axi_wready(blitter_wready),
        .m_axi_bid(blitter_bid),
        .m_axi_bresp(blitter_bresp),
        .m_axi_bvalid(blitter_bvalid),
        .m_axi_bready(blitter_bready)
    );

    astra_render_geometry geometry_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(geometry_start),
        .abort(geometry_abort),
        .opcode(command_is_lines_q ? 16'(`ASTRA_RENDER_OP_LINE) :
                                      command_opcode_q),
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
        .writer_done(engine_writer_done_q),
        .writer_aborted(engine_writer_aborted_q),
        .writer_error(engine_writer_error_q),
        .writer_fault_detail(engine_writer_fault_detail_q),
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
        .writer_done(engine_writer_done_q),
        .writer_aborted(engine_writer_aborted_q),
        .writer_error(engine_writer_error_q),
        .writer_fault_detail(engine_writer_fault_detail_q),
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
        .m_axi_rid(FLOOD_READ_ID),
        .m_axi_rdata(engine_response_data_q),
        .m_axi_rresp(engine_response_error_q ? 2'b10 : 2'b00),
        .m_axi_rlast(engine_response_last_q),
        .m_axi_rvalid(flood_busy && engine_response_valid_q),
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
        .writer_flush_ready(engine_writer_flush_ready),
        .writer_done(engine_writer_done_q),
        .writer_aborted(engine_writer_aborted_q),
        .writer_error(engine_writer_error_q),
        .writer_fault_detail(engine_writer_fault_detail_q),
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
        .m_axi_rid(GLYPH_READ_ID),
        .m_axi_rdata(engine_response_data_q),
        .m_axi_rresp(engine_response_error_q ? 2'b10 : 2'b00),
        .m_axi_rlast(engine_response_last_q),
        .m_axi_rvalid(glyph_busy && engine_response_valid_q),
        .m_axi_rready(glyph_rready)
    );

    astra_render_texture #(
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID(TEXTURE_READ_ID)
    ) texture_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(texture_start),
        .abort(texture_abort),
        .arena_base(ARENA_BASE),
        .clip_left(command_clip_left_q),
        .clip_top(command_clip_top_q),
        .clip_right(command_clip_right_q),
        .clip_bottom(command_clip_bottom_q),
        .options(command_words[12][3:0]),
        .textured(triangles_textured_q),
        .vertex_offset(command_words[10]),
        .triangle_count(command_words[11][12:0]),
        .destination_data_offset(destination_data_offset_q),
        .destination_pitch(destination_pitch_q),
        .destination_width(destination_width_q),
        .destination_height(destination_height_q),
        .destination_format(destination_format_q),
        .source_data_offset(source_data_offset_q),
        .source_pitch(source_pitch_q),
        .source_width(source_width_q),
        .source_height(source_height_q),
        .source_format(source_format_q),
        .source_palette_offset(source_palette_offset_q),
        .busy(texture_busy),
        .done(texture_done),
        .status(texture_status),
        .fault_detail(texture_fault_detail),
        .completed_pixels(texture_completed_pixels),
        .writer_start(texture_writer_start),
        .writer_abort(texture_writer_abort),
        .writer_flush(texture_writer_flush),
        .writer_flush_ready(engine_writer_flush_ready),
        .writer_barrier(texture_writer_barrier),
        .writer_barrier_ready(writer_barrier_ready),
        .writer_barrier_done(writer_barrier_done),
        .writer_done(engine_writer_done_q),
        .writer_aborted(engine_writer_aborted_q),
        .writer_error(engine_writer_error_q),
        .writer_fault_detail(engine_writer_fault_detail_q),
        .pixel_valid(texture_pixel_valid),
        .pixel_ready(texture_pixel_ready),
        .pixel_address(texture_pixel_address),
        .pixel_format(texture_pixel_format),
        .pixel_value(texture_pixel_value),
        .m_axi_arid(texture_arid),
        .m_axi_araddr(texture_araddr),
        .m_axi_arlen(texture_arlen),
        .m_axi_arsize(texture_arsize),
        .m_axi_arburst(texture_arburst),
        .m_axi_arcache(texture_arcache),
        .m_axi_arprot(texture_arprot),
        .m_axi_arqos(texture_arqos),
        .m_axi_arvalid(texture_arvalid),
        .m_axi_arready(texture_arready),
        .m_axi_rid(TEXTURE_READ_ID),
        .m_axi_rdata(engine_response_data_q),
        .m_axi_rresp(engine_response_error_q ? 2'b10 : 2'b00),
        .m_axi_rlast(engine_response_last_q),
        .m_axi_rvalid(texture_busy && engine_response_valid_q),
        .m_axi_rready(texture_rready)
    );

    wire selected_writer_start = texture_writer_start || glyph_writer_start ||
        flood_writer_start || geometry_writer_start || blitter_writer_start;
    wire selected_writer_abort = texture_writer_abort || glyph_writer_abort ||
        flood_writer_abort || geometry_writer_abort || blitter_writer_abort;
    wire selected_writer_flush = texture_writer_flush || glyph_writer_flush ||
        flood_writer_flush || geometry_writer_flush || blitter_writer_flush;
    wire selected_pixel_valid = texture_pixel_valid || glyph_pixel_valid ||
        flood_pixel_valid || geometry_pixel_valid || blitter_pixel_valid;
    wire [31:0] selected_pixel_address = texture_pixel_valid ?
        texture_pixel_address : glyph_pixel_valid ?
        glyph_pixel_address : flood_pixel_valid ?
        flood_pixel_address : geometry_pixel_valid ?
        geometry_pixel_address : blitter_pixel_address;
    wire [7:0] selected_pixel_format = texture_pixel_valid ?
        texture_pixel_format : glyph_pixel_valid ?
        glyph_pixel_format : flood_pixel_valid ?
        flood_pixel_format : geometry_pixel_valid ?
        geometry_pixel_format : blitter_pixel_format;
    wire [31:0] selected_pixel_value = texture_pixel_valid ?
        texture_pixel_value : glyph_pixel_valid ?
        glyph_pixel_value : flood_pixel_valid ?
        flood_pixel_value : geometry_pixel_valid ?
        geometry_pixel_value : blitter_pixel_value;
    wire writer_pixel_ready;

    assign engine_writer_flush_ready = !engine_writer_flush_pending_q;
    always @(posedge clk) begin
        if (reset || local_engine_reset) begin
            engine_writer_abort_q <= 1'b0;
            engine_writer_flush_pending_q <= 1'b0;
            engine_writer_done_q <= 1'b0;
            engine_writer_aborted_q <= 1'b0;
            engine_writer_error_q <= 1'b0;
            engine_writer_fault_detail_q <= 32'd0;
        end else begin
            engine_writer_abort_q <= selected_writer_abort;
            engine_writer_done_q <= writer_done;
            engine_writer_aborted_q <= writer_aborted;
            engine_writer_error_q <= writer_error;
            engine_writer_fault_detail_q <= writer_fault_detail;
            if (selected_writer_abort)
                engine_writer_flush_pending_q <= 1'b0;
            else if (engine_writer_flush_pending_q && writer_flush_ready)
                engine_writer_flush_pending_q <= 1'b0;
            else if (selected_writer_flush)
                engine_writer_flush_pending_q <= 1'b1;
        end
    end
    assign blitter_pixel_ready = writer_pixel_ready;
    assign geometry_pixel_ready = writer_pixel_ready;
    assign flood_pixel_ready = writer_pixel_ready;
    assign glyph_pixel_ready = writer_pixel_ready;
    assign texture_pixel_ready = writer_pixel_ready;

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
        // Every engine write shares one ID, so AXI keeps them in order
        // across commands and engines.
        .AXI_ID(BLITTER_WRITE_ID),
        .POSTED(1'b1)
    ) pixel_writer_i (
        .clk(clk),
        .reset(reset || local_engine_reset),
        .start(selected_writer_start),
        .abort(engine_writer_abort_q),
        .flush(engine_writer_flush_pending_q),
        .flush_ready(writer_flush_ready),
        .external_drained(engine_writes_drained),
        .barrier((command_is_flood_q && flood_writer_barrier) ||
                 (command_is_triangles_q && texture_writer_barrier)),
        .barrier_ready(writer_barrier_ready),
        .barrier_done(writer_barrier_done),
        .pixel_valid(selected_pixel_valid),
        .pixel_ready(writer_pixel_ready),
        .pixel_address(selected_pixel_address),
        .pixel_format(selected_pixel_format),
        .pixel_value(selected_pixel_value),
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

    // The command manager owns dispatch, so it also owns AXI read routing.
    // Feeding engine busy outputs back through this mux creates a needless
    // round trip on every response-valid path.
    // While FILL_RECTS reads records the manager owns the read channel.
    wire engine_reads = command_dispatched_q && !rects_reading_q;
    wire read_owner_texture = engine_reads && command_is_triangles_q;
    wire read_owner_glyph = engine_reads && command_is_glyph_q;
    wire read_owner_flood = engine_reads && command_is_flood_q;
    wire read_owner_blitter = engine_reads &&
        !engine_geometry_q && !command_is_flood_q && !command_is_glyph_q &&
        !command_is_triangles_q;
    assign m_axi_arid = read_owner_texture ? texture_arid :
        read_owner_glyph ? glyph_arid :
        read_owner_flood ? flood_arid :
        read_owner_blitter ? blitter_arid : MANAGER_READ_ID;
    assign m_axi_araddr = read_owner_texture ? texture_araddr :
        read_owner_glyph ? glyph_araddr :
        read_owner_flood ? flood_araddr :
        read_owner_blitter ? blitter_araddr : manager_araddr;
    assign m_axi_arlen = read_owner_texture ? texture_arlen :
        read_owner_glyph ? glyph_arlen :
        read_owner_flood ? flood_arlen :
        read_owner_blitter ? blitter_arlen : manager_arlen;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = read_owner_texture ? texture_arqos :
        read_owner_glyph ? glyph_arqos :
        read_owner_flood ? flood_arqos :
        read_owner_blitter ? blitter_arqos : 4'b0000;
    assign m_axi_arvalid = read_owner_texture ? texture_arvalid :
        read_owner_glyph ? glyph_arvalid :
        read_owner_flood ? flood_arvalid :
        read_owner_blitter ? blitter_arvalid : manager_arvalid;
    // Ownership gates the shared ARVALID/address mux. Ready may fan directly
    // to every idle-or-selected engine because a handshake still requires
    // that engine's own ARVALID; keeping ownership out of this return path
    // avoids feeding command classification through each engine FSM.
    assign glyph_arready = m_axi_arready;
    assign texture_arready = m_axi_arready;
    assign flood_arready = m_axi_arready;
    assign blitter_arready = m_axi_arready;
    assign blitter_rid = BLITTER_READ_ID;
    assign blitter_rdata = engine_response_data_q;
    assign blitter_rresp = engine_response_error_q ? 2'b10 : 2'b00;
    assign blitter_rlast = engine_response_last_q;
    assign blitter_rvalid = blitter_busy && engine_response_valid_q;
    wire engine_response_consume = engine_response_valid_q &&
        ((texture_busy && texture_rready) ||
         (glyph_busy && glyph_rready) ||
         (flood_busy && flood_rready) ||
         (blitter_busy && blitter_rready));
    // The spill entry keeps HP2 RREADY dependent only on registered local
    // capacity. Replacing the output on simultaneous consume/capture still
    // sustains one response beat per clock.
    wire engine_response_ready = !engine_response_spill_valid_q;
    wire engine_response_accept = engine_reads &&
        m_axi_rvalid && engine_response_ready;
    wire manager_rready = !manager_response_valid_q &&
        (state == ST_DESTINATION_R ||
         state == ST_SOURCE_R || state == ST_AUXILIARY_R);
    wire manager_response_accept = !command_dispatched_q &&
        m_axi_rvalid && manager_rready;
    // Prefetch beats stream straight into the local RAM at one per clock.
    wire prefetch_rready = state == ST_PREFETCH_R;
    wire prefetch_accept = !command_dispatched_q && m_axi_rvalid &&
        prefetch_rready;
    wire rects_accept = rects_reading_q && state == ST_RECTS_R &&
        m_axi_rvalid;
    assign m_axi_rready = engine_reads ? engine_response_ready :
        (manager_rready || prefetch_rready ||
         (rects_reading_q && state == ST_RECTS_R));

    // Local RAM write port: prefetch beats, or descriptor beats of a miss.
    always @* begin
        local_ram_we = 1'b0;
        local_ram_waddr = {1'b0, prefetch_beat_q};
        local_ram_wdata = {m_axi_rid != MANAGER_READ_ID ||
                               m_axi_rresp != 2'b00, m_axi_rdata};
        if (prefetch_accept) begin
            local_ram_we = 1'b1;
        end else if (rects_accept) begin
            local_ram_we = 1'b1;
            local_ram_waddr = {2'b11, rects_beat_q[6:0]};
        end else if (manager_response_valid_q &&
                     descriptor_capture_enabled_q) begin
            local_ram_we = 1'b1;
            local_ram_waddr = {5'b10000, slot_fill_q,
                               read_beat_index[1:0]};
            local_ram_wdata = {1'b0, manager_response_data_q};
        end
    end
    always @(posedge clk) begin
        if (local_ram_we)
            local_ram[local_ram_waddr] <= local_ram_wdata;
        local_ram_q <= local_ram[local_ram_raddr_q];
    end

    wire write_owner_blitter = blitter_copy_write_active;
    wire write_owner_pixels = writer_busy;
    wire [11:0] engine_outstanding = engine_issued_q - engine_acked_q;
    wire engine_writes_drained = engine_outstanding == 12'd0;
    wire engine_write_room = engine_outstanding < ENGINE_WRITE_LIMIT;
    wire engine_awvalid = write_owner_blitter ? blitter_awvalid :
        write_owner_pixels && pixel_awvalid;
    wire engine_wvalid = write_owner_blitter ? blitter_wvalid :
        write_owner_pixels && pixel_wvalid;
    // An engine address is shown only with room; room shrinks only by an
    // accepted address, so a shown address stays valid until accepted.
    wire engine_aw_shown = !completion_owns_q && engine_awvalid &&
        engine_write_room;
    wire engine_aw_accept = engine_aw_shown && m_axi_awready;
    wire engine_wlast_accept = !completion_owns_q && engine_wvalid &&
        m_axi_wready && m_axi_wlast;

    // Head of the unwritten completion records.
    wire [CQ_BITS-1:0] cq_issue_index = cq_issue_q;
    wire [11:0] cq_issue_wait = cq_mark_q[cq_issue_index] - engine_acked_q;
    wire completion_ready = cq_count_q != cq_inflight_q &&
        (cq_issue_wait == 12'd0 || cq_issue_wait[11]);
    wire [63:0] cq_beat_raw = cq_beats[{cq_issue_index, completion_beat_q}];
    wire [31:0] cq_beat0_word = swap32(cq_beat_raw[63:32]);
    // An engine write error the record did not report replaces an OK
    // status, as the engine itself reported it before writes were posted.
    wire cq_beat0_override = cq_werr_q[cq_issue_index] &&
        cq_beat0_word[15:0] == `ASTRA_RENDER_STATUS_OK;
    wire [7:0] cq_issue_detail = cq_werr_detail_q[cq_issue_index];
    wire [63:0] completion_beat_data =
        completion_beat_q == 2'd0 && cq_beat0_override ?
            {swap32({cq_beat0_word[31:16],
                     16'(`ASTRA_RENDER_STATUS_AXI_WRITE)}),
             cq_beat_raw[31:0]} :
        completion_beat_q == 2'd3 && completion_override_q ?
            {cq_beat_raw[63:32],
             swap32({16'h0002, 2'b00, cq_issue_detail[7:2], 6'd0,
                     cq_issue_detail[1:0]})} :
        cq_beat_raw;
    wire [31:0] completion_address = ARENA_BASE +
        active_completion_ring_offset_q +
        ({22'd0, cq_slot_q[cq_issue_index]} << 5);

    assign m_axi_awid = completion_owns_q ? COMPLETION_WRITE_ID :
        write_owner_blitter ? blitter_awid : pixel_awid;
    assign m_axi_awaddr = completion_owns_q ? completion_address :
        write_owner_blitter ? blitter_awaddr : pixel_awaddr;
    assign m_axi_awlen = completion_owns_q ? 8'd3 :
        write_owner_blitter ? blitter_awlen : pixel_awlen;
    assign m_axi_awsize = 3'b011;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot = 3'b000;
    assign m_axi_awqos = 4'b0000;
    assign m_axi_awvalid = completion_owns_q ? !completion_aw_done_q :
        engine_aw_shown;
    assign blitter_awready = !completion_owns_q && engine_write_room &&
        write_owner_blitter && m_axi_awready;
    assign pixel_awready = !completion_owns_q && engine_write_room &&
        !write_owner_blitter && write_owner_pixels && m_axi_awready;
    assign m_axi_wdata = completion_owns_q ? completion_beat_data :
        write_owner_blitter ? blitter_wdata : pixel_wdata;
    assign m_axi_wstrb = completion_owns_q ? 8'hff :
        write_owner_blitter ? blitter_wstrb : pixel_wstrb;
    assign m_axi_wlast = completion_owns_q ? completion_beat_q == 2'd3 :
        write_owner_blitter ? blitter_wlast : pixel_wlast;
    assign m_axi_wvalid = completion_owns_q ? completion_aw_done_q :
        engine_wvalid;
    assign blitter_wready = !completion_owns_q && write_owner_blitter &&
        m_axi_wready;
    assign pixel_wready = !completion_owns_q && !write_owner_blitter &&
        write_owner_pixels && m_axi_wready;

    // Responses: engines see none. A completion ID retires the oldest
    // written record; any other ID answers an engine write. With no engine
    // write outstanding, any response is taken as a record's, as the
    // unposted design did, so a wrong ID is still fatal.
    wire b_completion_id = m_axi_bid == COMPLETION_WRITE_ID;
    wire engine_b_route = !b_completion_id && !engine_writes_drained;
    wire completion_b_route = cq_inflight_q != 0 && !retire_commit_q &&
        (b_completion_id || engine_writes_drained);
    wire engine_b_accept = engine_b_route && m_axi_bvalid;
    wire completion_b_accept = completion_b_route && m_axi_bvalid;
    assign blitter_bid = m_axi_bid;
    assign blitter_bresp = m_axi_bresp;
    assign blitter_bvalid = 1'b0;
    assign pixel_bid = m_axi_bid;
    assign pixel_bresp = m_axi_bresp;
    assign pixel_bvalid = 1'b0;
    assign m_axi_bready = engine_b_route || completion_b_route;
    // Response ID widened to the 8-bit fault-detail field (DE25 uses 3).
    wire [7:0] response_id = {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_bid};
    wire engine_b_error = m_axi_bid != BLITTER_WRITE_ID ||
        m_axi_bresp != 2'b00;

    // In-order responses: a failed engine write belongs to the oldest queued
    // record still waiting for writes, else to the command being processed.
    reg werr_found;
    reg [CQ_BITS-1:0] werr_entry;
    integer werr_k;
    reg [CQ_BITS-1:0] werr_probe;
    reg [11:0] werr_wait;
    always @* begin
        werr_found = 1'b0;
        werr_entry = cq_issue_q;
        for (werr_k = 0; werr_k < CQ_DEPTH; werr_k = werr_k + 1) begin
            werr_probe = cq_issue_q + werr_k[CQ_BITS-1:0];
            werr_wait = cq_mark_q[werr_probe] - engine_acked_q;
            if (!werr_found && werr_k < cq_count_q - cq_inflight_q &&
                werr_wait != 12'd0 && !werr_wait[11]) begin
                werr_found = 1'b1;
                werr_entry = werr_probe;
            end
        end
    end
    wire werr_to_current = engine_b_accept && engine_b_error && !werr_found;

    // Queue storage (no reset: inferred as distributed RAM).
    always @(posedge clk) begin
        if (cq_beat_we_q)
            cq_beats[cq_beat_waddr_q] <= cq_beat_wdata_q;
        if (cq_info_we_q)
            cq_info[cq_info_waddr_q] <= cq_info_wdata_q;
        head_info_q <= cq_info[cq_head_q];
    end

    wire [8:0] rects_page_records = 9'd256 - {1'b0, rects_address_q[11:4]};
    wire [6:0] rects_limit = rects_unfetched_q < 13'd64 ?
        rects_unfetched_q[6:0] : 7'd64;
    wire [7:0] rects_last_beat = {rects_chunk_q, 1'b0} - 8'd1;
    // The record just loaded: [x, x + w) x [y, y + h), unclipped (a superset
    // of what it writes and reads).
    wire signed [17:0] rect_x0 = {{2{command_words[12][31]}},
                                  command_words[12][31:16]};
    wire signed [17:0] rect_y0 = {{2{command_words[12][15]}},
                                  command_words[12][15:0]};
    wire signed [17:0] rect_x1 = rect_x0 + $signed({2'b00, command_words[14][31:16]});
    wire signed [17:0] rect_y1 = rect_y0 + $signed({2'b00, command_words[14][15:0]});
    reg [RECT_SLOTS-1:0] rects_slot_pending;
    reg rect_conflict;
    reg rect_slot_free;
    reg [1:0] rect_free_slot;
    reg [11:0] rects_slot_wait;
    integer rs;
    always @* begin
        rect_conflict = 1'b0;
        rect_slot_free = 1'b0;
        rect_free_slot = 2'd0;
        for (rs = RECT_SLOTS - 1; rs >= 0; rs = rs - 1) begin
            rects_slot_wait = rects_slot_mark_q[rs] - engine_acked_q;
            rects_slot_pending[rs] = rects_slot_valid_q[rs] &&
                (!rects_slot_marked_q[rs] ||
                 (rects_slot_wait != 12'd0 && !rects_slot_wait[11]));
            if (rects_slot_pending[rs] &&
                rect_x0 < rects_slot_x1_q[rs] &&
                rects_slot_x0_q[rs] < rect_x1 &&
                rect_y0 < rects_slot_y1_q[rs] &&
                rects_slot_y0_q[rs] < rect_y1)
                rect_conflict = 1'b1;
            if (!rects_slot_pending[rs]) begin
                rect_slot_free = 1'b1;
                rect_free_slot = rs[1:0];
            end
        end
    end
    wire [6:0] page_commands_left =
        7'd64 - {1'b0, submission_command_address_q[11:6]};
    wire prefetch_buffered = prefetch_next_q != prefetch_count_q &&
        prefetch_ring_offset_q == active_submission_ring_offset_q;
    // Every range-check pair this command enables, in check order.
    wire [29:0] range_mask = {
        command_uses_auxiliary_q && active_protected1_valid_q,
        command_uses_auxiliary_q && active_protected0_valid_q,
        command_is_blit_q && command_flags_q[3] && command_flags_q[5],
        command_is_blit_q && command_flags_q[3],
        command_uses_array_q ? active_protected1_valid_q :
            command_uses_auxiliary_q,
        (command_is_blit_q && command_flags_q[3] && command_flags_q[5]) ||
            (command_uses_array_q && active_protected0_valid_q),
        (command_is_blit_q && command_flags_q[3]) ||
            (command_uses_array_q && command_uses_palette_q),
        {2{command_uses_auxiliary_q || command_uses_array_q}},
        (command_is_blit_q && command_flags_q[3]) || command_is_glyph_q ||
            triangles_textured_q,
        {3{command_uses_auxiliary_q || command_uses_array_q}},
        {6{command_uses_palette_q}},
        (command_is_blit_q && !same_surface_q) || command_is_glyph_q ||
            triangles_textured_q,
        {4{command_reads_source_q}},
        active_protected1_valid_q,
        active_protected0_valid_q,
        command_reads_source_q,
        3'b111
    };
    wire [29:0] range_mask_lowest = range_mask_q & (~range_mask_q + 30'd1);
    reg [4:0] range_mask_index;
    integer mask_bit;
    always @* begin
        range_mask_index = 5'd0;
        for (mask_bit = 0; mask_bit < 30; mask_bit = mask_bit + 1)
            if (range_mask_lowest[mask_bit])
                range_mask_index = range_mask_index | mask_bit[4:0];
    end
    // Slot lookup: the offset against each valid slot's tag.
    wire [2:0] slot_match = {
        slot_valid_q[2] && slot_tag_q[2] == lookup_offset_q,
        slot_valid_q[1] && slot_tag_q[1] == lookup_offset_q,
        slot_valid_q[0] && slot_tag_q[0] == lookup_offset_q
    };

    integer word_index;
    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            cycle_counter <= 32'd0;
            command_active <= 1'b0;
            command_dispatched_q <= 1'b0;
            command_start_cycle <= 32'd0;
            deadline_remaining_us_q <= 32'd0;
            deadline_subcycle_q <= {DEADLINE_SUBCYCLE_WIDTH{1'b0}};
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
            engine_response_valid_q <= 1'b0;
            engine_response_spill_valid_q <= 1'b0;
            engine_expected_id_q <= {AXI_ID_WIDTH{1'b0}};
            descriptor_capture_enabled_q <= 1'b0;
            command_opcode_q <= 16'd0;
            command_is_fill_q <= 1'b0;
            command_is_blit_q <= 1'b0;
            command_is_geometry_q <= 1'b0;
            command_is_flood_q <= 1'b0;
            command_is_glyph_q <= 1'b0;
            command_is_triangles_q <= 1'b0;
            command_is_fill_rects_q <= 1'b0;
            command_is_lines_q <= 1'b0;
            rects_reading_q <= 1'b0;
            rects_remaining_q <= 13'd0;
            rects_unfetched_q <= 13'd0;
            rects_address_q <= 32'd0;
            rects_buffered_q <= 7'd0;
            rects_index_q <= 7'd0;
            rects_chunk_q <= 7'd0;
            rects_beat_q <= 8'd0;
            rects_read_error_q <= 1'b0;
            rects_color_q <= 32'd0;
            rects_record_color_q <= 1'b0;
            rects_blend_q <= 1'b0;
            rects_slot_valid_q <= {RECT_SLOTS{1'b0}};
            rects_slot_marked_q <= {RECT_SLOTS{1'b0}};
            rects_slot_q <= 2'd0;
            rects_pixels_q <= 32'd0;
            rects_ran_q <= 1'b0;
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
            range_load_active_q <= 1'b0;
            range_load_valid_q <= 1'b0;
            range_load_last_q <= 1'b0;
            range_end_valid_q <= 1'b0;
            range_end_last_q <= 1'b0;
            range_end_enabled_q <= 1'b0;
            range_end_protected_q <= 1'b0;
            range_end_first_offset_q <= 32'd0;
            range_end_second_offset_q <= 32'd0;
            range_compare_last_q <= 1'b0;
            range_compare_protected_q <= 1'b0;
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
            admission_ring_alignment_valid_q <= 1'b0;
            admission_submission_in_bounds_q <= 1'b0;
            admission_completion_in_bounds_q <= 1'b0;
            admission_rings_overlap_q <= 1'b0;
            validation_error_q <= 1'b0;
            validation_status_q <= `ASTRA_RENDER_STATUS_OK;
            validation_fault_q <= 32'd0;
            layout_bad_clip_q <= 1'b0;
            layout_bad_flags_q <= 1'b0;
            layout_bad_fill_q <= 1'b0;
            layout_bad_geometry_q <= 1'b0;
            layout_bad_geometry_line_q <= 1'b0;
            layout_bad_geometry_circle_q <= 1'b0;
            layout_bad_geometry_ellipse_q <= 1'b0;
            layout_bad_geometry_pattern_q <= 1'b0;
            layout_bad_flood_q <= 1'b0;
            layout_bad_array_q <= 1'b0;
            layout_bad_triangles_q <= 1'b0;
            layout_word_nonzero_q <= 7'd0;
            array_end_q <= 33'd0;
            header_result_q <= HEADER_OK;
            sequence_result_q <= SEQUENCE_OK;
            submission_command_address_q <= 32'd0;
            completion_status_q <= `ASTRA_RENDER_STATUS_OK;
            completion_count_q <= 32'd0;
            completion_fault_q <= 32'd0;
            completion_failed_q <= 1'b0;
            completion_end_cycle_q <= 32'd0;
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
            texture_start <= 1'b0;
            texture_abort <= 1'b0;
            submission_consumer <= 11'd0;
            completion_producer <= 11'd0;
            completion_fatal_q <= 1'b0;
            engine_issued_q <= 12'd0;
            engine_acked_q <= 12'd0;
            engine_burst_open_q <= 4'sd0;
            completion_owns_q <= 1'b0;
            completion_aw_done_q <= 1'b0;
            completion_beat_q <= 2'd0;
            completion_override_q <= 1'b0;
            cq_head_q <= 0;
            cq_issue_q <= 0;
            cq_tail_q <= 0;
            cq_count_q <= 0;
            cq_inflight_q <= 0;
            cq_werr_q <= {CQ_DEPTH{1'b0}};
            cq_beat_we_q <= 1'b0;
            cq_info_we_q <= 1'b0;
            enqueue_beat_q <= 2'd0;
            cur_werr_q <= 1'b0;
            cur_werr_detail_q <= 8'd0;
            local_ram_raddr_q <= 9'd0;
            intake_pointer_q <= 11'd0;
            prefetch_count_q <= 6'd0;
            prefetch_next_q <= 6'd0;
            prefetch_beat_q <= 8'd0;
            prefetch_last_beat_q <= 8'd0;
            prefetch_ring_offset_q <= 32'd0;
            prefetch_available_q <= 11'd0;
            prefetch_to_wrap_q <= 11'd0;
            prefetch_limit_q <= 6'd0;
            prefetch_burst_q <= 6'd0;
            load_issue_q <= 4'd0;
            load_capture_q <= 4'd0;
            load_valid1_q <= 1'b0;
            load_valid2_q <= 1'b0;
            slot_valid_q <= 3'd0;
            slot_used_q <= 3'd0;
            slot_hit_q <= 3'd0;
            slot_fill_q <= 2'd0;
            desc_kind_q <= 2'd0;
            lookup_offset_q <= 32'd0;
            epoch_limit_q <= 11'd0;
            range_mask_q <= 30'd0;
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
                 word_index = word_index + 1)
                descriptor_words[word_index] <= 32'd0;
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
            texture_start <= 1'b0;
            texture_abort <= 1'b0;
            retire_commit_q <= 1'b0;

            if (manager_response_accept) begin
                manager_response_valid_q <= 1'b1;
                manager_response_id_q <= m_axi_rid;
                manager_response_data_q <= m_axi_rdata;
                manager_response_resp_q <= m_axi_rresp;
                manager_response_last_q <= m_axi_rlast;
            end

            if (engine_reads && m_axi_arvalid && m_axi_arready)
                engine_expected_id_q <= m_axi_arid;

            if (local_engine_reset) begin
                engine_response_valid_q <= 1'b0;
                engine_response_spill_valid_q <= 1'b0;
            end else begin
                case ({engine_response_accept, engine_response_consume})
                    2'b10: begin
                        if (engine_response_valid_q) begin
                            engine_response_spill_valid_q <= 1'b1;
                            engine_response_spill_data_q <= m_axi_rdata;
                            engine_response_spill_error_q <=
                                m_axi_rid != engine_expected_id_q ||
                                m_axi_rresp != 2'b00;
                            engine_response_spill_last_q <= m_axi_rlast;
                        end else begin
                            engine_response_valid_q <= 1'b1;
                            engine_response_data_q <= m_axi_rdata;
                            engine_response_error_q <=
                                m_axi_rid != engine_expected_id_q ||
                                m_axi_rresp != 2'b00;
                            engine_response_last_q <= m_axi_rlast;
                        end
                    end
                    2'b01: begin
                        if (engine_response_spill_valid_q) begin
                            engine_response_data_q <=
                                engine_response_spill_data_q;
                            engine_response_error_q <=
                                engine_response_spill_error_q;
                            engine_response_last_q <=
                                engine_response_spill_last_q;
                            engine_response_spill_valid_q <= 1'b0;
                        end else begin
                            engine_response_valid_q <= 1'b0;
                        end
                    end
                    2'b11: begin
                        engine_response_valid_q <= 1'b1;
                        engine_response_data_q <= m_axi_rdata;
                        engine_response_error_q <=
                            m_axi_rid != engine_expected_id_q ||
                            m_axi_rresp != 2'b00;
                        engine_response_last_q <= m_axi_rlast;
                    end
                    default: begin end
                endcase
            end


            if (queue_rebase && !command_active && cq_count_q == 0 &&
                engine_writes_drained) begin
                submission_consumer <= submission_producer;
                intake_pointer_q <= submission_producer;
                epoch_limit_q <= submission_producer;
                prefetch_count_q <= 6'd0;
                prefetch_next_q <= 6'd0;
                slot_valid_q <= 3'd0;
                completion_fatal_q <= 1'b0;
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

            cq_beat_we_q <= 1'b0;
            cq_info_we_q <= 1'b0;

            // ---- posted engine writes: issue and response accounting ----
            if (engine_aw_accept)
                engine_issued_q <= engine_issued_q + 12'd1;
            case ({engine_aw_accept, engine_wlast_accept})
                2'b10: engine_burst_open_q <= engine_burst_open_q + 4'sd1;
                2'b01: engine_burst_open_q <= engine_burst_open_q - 4'sd1;
                default: begin end
            endcase
            if (engine_b_accept) begin
                engine_acked_q <= engine_acked_q + 12'd1;
                // In-order responses: the owner is the oldest unwritten
                // record still waiting for writes, else the running command.
                if (engine_b_error && werr_found) begin
                    if (!cq_werr_q[werr_entry])
                        cq_werr_detail_q[werr_entry] <=
                            {response_id[5:0], m_axi_bresp};
                    cq_werr_q[werr_entry] <= 1'b1;
                end else if (engine_b_error) begin
                    if (!cur_werr_q)
                        cur_werr_detail_q <= {response_id[5:0], m_axi_bresp};
                    cur_werr_q <= 1'b1;
                end
            end

            // ---- completion records: one burst between engine bursts ----
            if (!completion_owns_q) begin
                if (completion_ready && engine_burst_open_q == 4'sd0 &&
                    !engine_awvalid && !engine_wvalid) begin
                    completion_owns_q <= 1'b1;
                    completion_aw_done_q <= 1'b0;
                    completion_beat_q <= 2'd0;
                end
            end else begin
                if (!completion_aw_done_q && m_axi_awready)
                    completion_aw_done_q <= 1'b1;
                if (completion_aw_done_q && m_axi_wready) begin
                    if (completion_beat_q == 2'd0)
                        completion_override_q <= cq_beat0_override;
                    completion_beat_q <= completion_beat_q + 2'd1;
                    if (completion_beat_q == 2'd3) begin
                        completion_owns_q <= 1'b0;
                        cq_issue_q <= cq_issue_q + 1'b1;
                    end
                end
            end

            // The record's response retires it.
            if (completion_b_accept) begin
                if (!b_completion_id || m_axi_bresp != 2'b00) begin
                    configuration_fault <= 1'b1;
                    last_fault_detail <= {16'h0007,
                        {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_bid},
                        6'd0, m_axi_bresp};
                    completion_fatal_q <= 1'b1;
                end else begin
                    retire_commit_q <= 1'b1;
                end
            end

            if (retire_commit_q) begin : retire_head
                reg retire_error;
                retire_error = cq_werr_q[cq_head_q] && !head_info_q[64];
                cq_werr_q[cq_head_q] <= 1'b0;
                cq_head_q <= cq_head_q + 1'b1;
                completion_producer <= completion_producer + 11'd1;
                submission_consumer <= submission_consumer + 11'd1;
                commands_completed <= commands_completed + 32'd1;
                completion_irq <= 1'b1;
                if (!head_info_q[64] && !retire_error) begin
                    if (retirement_open)
                        retired_fence <= head_info_q[31:0];
                end else begin
                    commands_failed <= commands_failed + 32'd1;
                    retirement_open <= 1'b0;
                    last_fault_detail <= retire_error ?
                        {16'h0002, 2'b00, cq_werr_detail_q[cq_head_q][7:2],
                         6'd0, cq_werr_detail_q[cq_head_q][1:0]} :
                        head_info_q[63:32];
                end
            end
            // Written-but-unretired and queued counts.
            case ({completion_owns_q && completion_aw_done_q &&
                       m_axi_wready && completion_beat_q == 2'd3,
                   retire_commit_q})
                2'b10: cq_inflight_q <= cq_inflight_q + 1'b1;
                2'b01: cq_inflight_q <= cq_inflight_q - 1'b1;
                default: begin end
            endcase
            case ({state == ST_COMPLETION_W, retire_commit_q})
                2'b10: cq_count_q <= cq_count_q + 1'b1;
                2'b01: cq_count_q <= cq_count_q - 1'b1;
                default: begin end
            endcase

            if (local_engine_reset)
                slot_valid_q <= 3'd0;


            if (deadline_active && deadline_remaining_us_q != 32'd0) begin
                if (CYCLES_PER_US == 1 ||
                    deadline_subcycle_q == CYCLES_PER_US - 1) begin
                    deadline_subcycle_q <=
                        {DEADLINE_SUBCYCLE_WIDTH{1'b0}};
                    deadline_remaining_us_q <=
                        deadline_remaining_us_q - 32'd1;
                    if (deadline_remaining_us_q == 32'd1)
                        deadline_expired_q <= 1'b1;
                end else begin
                    deadline_subcycle_q <= deadline_subcycle_q + 1'b1;
                end
            end else if (!deadline_active) begin
                deadline_expired_q <= 1'b0;
            end

            if (soft_reset && command_active && !command_dispatched_q)
                cancel_before_dispatch <= 1'b1;

            case (state)
                    ST_IDLE: begin
                        busy <= cq_count_q != 0;
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
                            submission_command_address_q <= ARENA_BASE +
                                active_submission_ring_offset_q +
                                ({21'd0, intake_pointer_q[9:0]} << 6);
                            // Published commands not yet taken; the posted
                            // completion's command is taken but not retired.
                            prefetch_available_q <=
                                admission_submission_producer_q -
                                intake_pointer_q;
                            prefetch_to_wrap_q <= 11'd1024 -
                                {1'b0, intake_pointer_q[9:0]};
                            state <= ST_ADMISSION_VALIDATE;
                        end
                    end

                    ST_ADMISSION_VALIDATE: begin
                        if (!queue_rebase) begin
                            admission_ring_alignment_valid_q <=
                                active_submission_ring_offset_q[5:0] ==
                                    6'd0 &&
                                active_completion_ring_offset_q[4:0] ==
                                    5'd0;
                            admission_submission_in_bounds_q <=
                                active_submission_ring_end_q <=
                                    {1'b0, ARENA_BYTES};
                            admission_completion_in_bounds_q <=
                                active_completion_ring_end_q <=
                                    {1'b0, ARENA_BYTES};
                            admission_rings_overlap_q <=
                                {1'b0, active_submission_ring_offset_q} <
                                    active_completion_ring_end_q &&
                                {1'b0, active_completion_ring_offset_q} <
                                    active_submission_ring_end_q;
                            prefetch_limit_q <=
                                prefetch_available_q >= PREFETCH_COMMANDS &&
                                prefetch_to_wrap_q >= PREFETCH_COMMANDS ?
                                    PREFETCH_COMMANDS :
                                prefetch_available_q < prefetch_to_wrap_q ?
                                    prefetch_available_q[5:0] :
                                    prefetch_to_wrap_q[5:0];
                            state <= ST_ADMISSION_COMBINE;
                        end
                    end

                    ST_ADMISSION_COMBINE: begin
                        if (!queue_rebase) begin
                            admission_configuration_valid_q <=
                                ARENA_LIMIT > ARENA_BASE &&
                                admission_ring_alignment_valid_q &&
                                admission_submission_in_bounds_q &&
                                admission_completion_in_bounds_q &&
                                !admission_rings_overlap_q &&
                                active_resource_generation_q != 32'd0 &&
                                admission_submission_used_q <=
                                    `ASTRA_RENDER_RING_ENTRIES &&
                                admission_completion_used_q <=
                                    `ASTRA_RENDER_RING_ENTRIES;
                            admission_completion_available_q <=
                                admission_completion_used_q +
                                    {{(10-CQ_BITS){1'b0}}, cq_count_q} <
                                    `ASTRA_RENDER_RING_ENTRIES;
                            manager_araddr <= submission_command_address_q;
                            // A burst never crosses a 4 KiB page.
                            prefetch_burst_q <=
                                {1'b0, prefetch_limit_q} > page_commands_left ?
                                    page_commands_left[5:0] :
                                    prefetch_limit_q;
                            state <= ST_ADMISSION_DECIDE;
                        end
                    end

                    ST_ADMISSION_DECIDE: begin
                        if (queue_rebase) begin
                        end else if (completion_fatal_q) begin
                            busy <= 1'b0;
                            state <= ST_FATAL;
                        end else if (prefetch_available_q == 11'd0 &&
                                     admission_configuration_valid_q) begin
                            busy <= cq_count_q != 0;
                            state <= ST_IDLE;
                        end else if (admission_submission_used_q == 11'd0) begin
                            busy <= cq_count_q != 0;
                            state <= ST_IDLE;
                        end else if (!admission_configuration_valid_q) begin
                            configuration_fault <= 1'b1;
                            last_fault_detail <= 32'h00010000;
                            busy <= 1'b0;
                            state <= ST_IDLE;
                        end else if (!admission_completion_available_q) begin
                            backpressure_cycles <=
                                backpressure_cycles + 32'd1;
                            busy <= cq_count_q != 0;
                            state <= ST_IDLE;
                        end else begin
                            busy <= 1'b1;
                            intake_pointer_q <= intake_pointer_q + 11'd1;
                            cur_werr_q <= 1'b0;
                            // Descriptors cached under an older doorbell
                            // serve only commands published by then.
                            if (intake_pointer_q == epoch_limit_q) begin
                                slot_valid_q <= 3'd0;
                                epoch_limit_q <=
                                    admission_submission_producer_q;
                            end
                            slot_used_q <= 3'd0;
                            read_beat_index <= 5'd0;
                            read_error_seen <= 1'b0;
                            load_issue_q <= 4'd0;
                            load_capture_q <= 4'd0;
                            load_valid1_q <= 1'b0;
                            load_valid2_q <= 1'b0;
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
                            if (prefetch_buffered) begin
                                state <= ST_COMMAND_R;
                            end else begin
                                prefetch_count_q <= prefetch_burst_q;
                                prefetch_next_q <= 6'd0;
                                prefetch_beat_q <= 8'd0;
                                prefetch_last_beat_q <=
                                    {prefetch_burst_q[4:0] - 5'd1, 3'b111};
                                prefetch_ring_offset_q <=
                                    active_submission_ring_offset_q;
                                manager_arlen <=
                                    {prefetch_burst_q[4:0] - 5'd1, 3'b111};
                                manager_arvalid <= 1'b1;
                                state <= ST_COMMAND_AR;
                            end
                        end
                    end

                    ST_COMMAND_AR: begin
                        if (manager_arvalid && m_axi_arready) begin
                            manager_arvalid <= 1'b0;
                            state <= ST_PREFETCH_R;
                        end
                    end

                    // One beat per clock into the local RAM; each beat keeps
                    // its response error for the command that owns it.
                    ST_PREFETCH_R: begin
                        if (prefetch_accept) begin
                            prefetch_beat_q <= prefetch_beat_q + 8'd1;
                            if (m_axi_rlast !=
                                (prefetch_beat_q == prefetch_last_beat_q)) begin
                                configuration_fault <= 1'b1;
                                last_fault_detail <= 32'h00020001;
                                prefetch_count_q <= 6'd0;
                                prefetch_next_q <= 6'd0;
                                state <= ST_FATAL;
                            end else if (m_axi_rlast) begin
                                state <= ST_COMMAND_R;
                            end
                        end
                    end

                    // Eight beats from the local RAM (two-cycle read).
                    ST_COMMAND_R: begin
                        if (load_issue_q != 4'd8) begin
                            local_ram_raddr_q <=
                                {1'b0, prefetch_next_q[4:0], load_issue_q[2:0]};
                            load_issue_q <= load_issue_q + 4'd1;
                        end
                        load_valid1_q <= load_issue_q != 4'd8;
                        load_valid2_q <= load_valid1_q;
                        if (load_valid2_q) begin
                            command_words[load_capture_q[2:0] * 2] <=
                                swap32(local_ram_q[31:0]);
                            command_words[load_capture_q[2:0] * 2 + 1] <=
                                swap32(local_ram_q[63:32]);
                            if (local_ram_q[64])
                                read_error_seen <= 1'b1;
                            load_capture_q <= load_capture_q + 4'd1;
                            if (load_capture_q == 4'd7) begin
                                prefetch_next_q <= prefetch_next_q + 6'd1;
                                state <= ST_COMMAND_R_DECIDE;
                            end
                        end
                    end

                    ST_COMMAND_R_DECIDE: begin
                        begin
                            if (read_error_seen) begin
                                // Re-read what follows an untrusted fetch.
                                prefetch_count_q <= 6'd0;
                                prefetch_next_q <= 6'd0;
                                command_opcode_q <= 16'd0;
                                command_is_fill_q <= 1'b0;
                                command_is_blit_q <= 1'b0;
                                command_is_geometry_q <= 1'b0;
                                command_is_flood_q <= 1'b0;
                                command_is_glyph_q <= 1'b0;
                                command_is_triangles_q <= 1'b0;
                                command_is_fill_rects_q <= 1'b0;
                                command_is_lines_q <= 1'b0;
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
                        command_is_triangles_q <= incoming_is_triangles;
                        command_is_fill_rects_q <= command_word1[31:16] ==
                            `ASTRA_RENDER_OP_FILL_RECTS;
                        command_is_lines_q <= command_word1[31:16] ==
                            `ASTRA_RENDER_OP_LINES;
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
                                     !command_is_glyph_q &&
                                     !command_is_triangles_q &&
                                     !command_is_records_q) begin
                            header_result_q <= HEADER_BAD_OPCODE;
                        end else if (
                            (command_is_fill_q &&
                             command_flags_q != 16'd0) ||
                            (command_is_flood_q &&
                             command_flags_q != 16'd0) ||
                            (command_is_triangles_q &&
                             command_flags_q != 16'd0) ||
                            (command_is_records_q &&
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
                        layout_word_nonzero_q[0] <=
                            command_words[9] != 32'd0;
                        layout_word_nonzero_q[1] <=
                            command_words[10] != 32'd0;
                        layout_word_nonzero_q[2] <=
                            command_words[11] != 32'd0;
                        layout_word_nonzero_q[3] <=
                            command_words[12] != 32'd0;
                        layout_word_nonzero_q[4] <=
                            command_words[13] != 32'd0;
                        layout_word_nonzero_q[5] <=
                            command_words[14] != 32'd0;
                        layout_word_nonzero_q[6] <=
                            command_words[15] != 32'd0;
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
                        array_end_q <= {1'b0, command_words[10]} +
                            {1'b0, array_bytes_q};
                        state <= ST_VALIDATE_LAYOUT_WORDS;
                    end

                    ST_VALIDATE_LAYOUT_WORDS: begin
                        layout_bad_fill_q <= (command_is_fill_q &&
                            (source_descriptor_offset_q != 32'd0 ||
                             layout_word_nonzero_q[2] ||
                             layout_word_nonzero_q[4])) ||
                            (command_is_fill_rects_q &&
                            (source_descriptor_offset_q != 32'd0 ||
                             layout_word_nonzero_q[4] ||
                             layout_word_nonzero_q[5] ||
                             (command_words[12] &
                              ~`ASTRA_RENDER_FILL_RECTS_OPTION_ALLOWED_MASK) !=
                                 32'd0)) ||
                            (command_is_lines_q &&
                            (source_descriptor_offset_q != 32'd0 ||
                             layout_word_nonzero_q[4] ||
                             layout_word_nonzero_q[5] ||
                             (command_words[12] &
                              ~`ASTRA_RENDER_LINES_OPTION_ALLOWED_MASK) !=
                                 32'd0));
                        layout_bad_geometry_line_q <=
                            layout_word_nonzero_q[0] ||
                            layout_word_nonzero_q[1] ||
                            layout_word_nonzero_q[4] ||
                            layout_word_nonzero_q[5];
                        layout_bad_geometry_circle_q <=
                            layout_word_nonzero_q[0] ||
                            layout_word_nonzero_q[1] ||
                            layout_word_nonzero_q[3] ||
                            command_words[13][15:0] != 16'd0 ||
                            layout_word_nonzero_q[5];
                        layout_bad_geometry_ellipse_q <=
                            layout_word_nonzero_q[0] ||
                            layout_word_nonzero_q[1] ||
                            layout_word_nonzero_q[3] ||
                            layout_word_nonzero_q[5];
                        layout_bad_geometry_pattern_q <=
                            !command_flags_q[1] &&
                            layout_word_nonzero_q[5];
                        layout_bad_flood_q <= command_is_flood_q &&
                            (source_descriptor_offset_q != 32'd0 ||
                             layout_word_nonzero_q[3] ||
                             layout_word_nonzero_q[4] ||
                             layout_word_nonzero_q[5]);
                        layout_bad_array_q <= (command_is_glyph_q &&
                            (command_words[10][3:0] != 4'd0 ||
                             command_words[11] == 32'd0 ||
                             command_words[11] >
                                `ASTRA_RENDER_MAX_GLYPH_DESCRIPTORS ||
                             command_words[14][31:8] != 24'd0 ||
                             layout_word_nonzero_q[6])) ||
                            (command_is_triangles_q &&
                            (command_words[10][4:0] != 5'd0 ||
                             command_words[11] == 32'd0 ||
                             command_words[11] >
                                `ASTRA_RENDER_MAX_TRIANGLES)) ||
                            (command_is_fill_rects_q &&
                            (command_words[10][3:0] != 4'd0 ||
                             command_words[11] == 32'd0 ||
                             command_words[11] >
                                `ASTRA_RENDER_MAX_FILL_RECTS)) ||
                            (command_is_lines_q &&
                            (command_words[10][3:0] != 4'd0 ||
                             command_words[11] == 32'd0 ||
                             command_words[11] >
                                `ASTRA_RENDER_MAX_LINE_SEGMENTS));
                        // TRIANGLES options: blend 0..4, FILTER_LINEAR only
                        // when textured, words 13..15 zero.
                        layout_bad_triangles_q <= command_is_triangles_q &&
                            ((command_words[12] &
                              ~`ASTRA_RENDER_TRIANGLE_OPTION_ALLOWED_MASK) !=
                                 32'd0 ||
                             command_words[12][2:0] >
                                `ASTRA_RENDER_TRIANGLE_OPTION_BLEND_MUL ||
                             (command_words[12][3] &&
                              !layout_word_nonzero_q[0]) ||
                             layout_word_nonzero_q[4] ||
                             layout_word_nonzero_q[5] ||
                             layout_word_nonzero_q[6]);
                        state <= ST_VALIDATE_GLYPH_RANGE;
                    end

                    ST_VALIDATE_GLYPH_RANGE: begin
                        case (command_opcode_q[2:0])
                            3'd0, 3'd1:
                                layout_bad_geometry_q <=
                                    command_is_geometry_q &&
                                    layout_bad_geometry_line_q;
                            3'd2:
                                layout_bad_geometry_q <=
                                    command_is_geometry_q &&
                                    layout_bad_geometry_circle_q;
                            3'd3:
                                layout_bad_geometry_q <=
                                    command_is_geometry_q &&
                                    layout_bad_geometry_ellipse_q;
                            default:
                                layout_bad_geometry_q <=
                                    command_is_geometry_q &&
                                    layout_bad_geometry_pattern_q;
                        endcase
                        layout_bad_array_q <= layout_bad_array_q ||
                            (command_uses_array_q &&
                             array_end_q > {1'b0, ARENA_BYTES});
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
                        end else if (layout_bad_triangles_q) begin
                            validation_error_q <= 1'b1;
                            validation_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_FLAGS;
                            validation_fault_q <= 32'h00030005;
                        end else if (layout_bad_array_q) begin
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
                                     command_reads_source_q) begin
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
                            lookup_offset_q <= destination_descriptor_offset_q;
                            desc_kind_q <= 2'd0;
                            state <= ST_DESC_LOOKUP;
                        end
                    end

                    // A descriptor the previous command also referenced is
                    // reused from the local RAM: that command's range checks
                    // proved its writes cannot have touched it.
                    ST_DESC_LOOKUP: begin
                        slot_hit_q <= slot_match;
                        // A command references at most three descriptors,
                        // so an unreferenced slot always exists.
                        slot_fill_q <=
                            !slot_used_q[0] && !slot_valid_q[0] ? 2'd0 :
                            !slot_used_q[1] && !slot_valid_q[1] ? 2'd1 :
                            !slot_used_q[2] && !slot_valid_q[2] ? 2'd2 :
                            !slot_used_q[0] ? 2'd0 :
                            !slot_used_q[1] ? 2'd1 : 2'd2;
                        state <= ST_DESC_LOOKUP_DECIDE;
                    end

                    ST_DESC_LOOKUP_DECIDE: begin
                        read_beat_index <= 5'd0;
                        read_error_seen <= 1'b0;
                        load_issue_q <= 4'd0;
                        load_capture_q <= 4'd0;
                        load_valid1_q <= 1'b0;
                        load_valid2_q <= 1'b0;
                        if (slot_hit_q != 3'd0) begin
                            slot_fill_q <= slot_hit_q[0] ? 2'd0 :
                                slot_hit_q[1] ? 2'd1 : 2'd2;
                            slot_used_q <= slot_used_q | slot_hit_q;
                            state <= ST_DESC_CACHE_LOAD;
                        end else if (!engine_writes_drained) begin
                            // A read may not pass an unanswered write.
                        end else begin
                            slot_valid_q[slot_fill_q] <= 1'b0;
                            slot_used_q[slot_fill_q] <= 1'b1;
                            slot_tag_q[slot_fill_q] <= lookup_offset_q;
                            manager_araddr <= ARENA_BASE + lookup_offset_q;
                            manager_arlen <= 8'd3;
                            manager_arvalid <= 1'b1;
                            descriptor_capture_enabled_q <= 1'b1;
                            state <= desc_kind_q == 2'd0 ? ST_DESTINATION_AR :
                                desc_kind_q == 2'd1 ? ST_SOURCE_AR :
                                ST_AUXILIARY_AR;
                        end
                    end

                    ST_DESC_CACHE_LOAD: begin
                        if (load_issue_q != 4'd4) begin
                            local_ram_raddr_q <= {5'b10000, slot_fill_q,
                                                  load_issue_q[1:0]};
                            load_issue_q <= load_issue_q + 4'd1;
                        end
                        load_valid1_q <= load_issue_q != 4'd4;
                        load_valid2_q <= load_valid1_q;
                        if (load_valid2_q) begin
                            descriptor_words[load_capture_q[1:0] * 2] <=
                                swap32(local_ram_q[31:0]);
                            descriptor_words[load_capture_q[1:0] * 2 + 1] <=
                                swap32(local_ram_q[63:32]);
                            load_capture_q <= load_capture_q + 4'd1;
                            if (load_capture_q == 4'd3)
                                state <= desc_kind_q == 2'd0 ?
                                    ST_DESTINATION_VALIDATE_START :
                                    desc_kind_q == 2'd1 ?
                                    ST_SOURCE_VALIDATE_START :
                                    ST_AUXILIARY_VALIDATE_START;
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
                                // The descriptor words come from the slot
                                // on a miss as on a hit: one load path.
                                slot_valid_q[slot_fill_q] <= 1'b1;
                                load_issue_q <= 4'd0;
                                load_capture_q <= 4'd0;
                                load_valid1_q <= 1'b0;
                                load_valid2_q <= 1'b0;
                                state <= ST_DESC_CACHE_LOAD;
                            end
                        end else begin
                            read_beat_index <= read_beat_index + 5'd1;
                            state <= ST_DESTINATION_R;
                        end
                    end

                    ST_DESTINATION_VALIDATE_START: begin
                        validator_required_access <=
                            command_is_glyph_q || command_is_flood_q ||
                            (command_is_blit_q && same_surface_q) ||
                            (command_is_triangles_q &&
                             command_words[12][2:0] != 3'd0) ?
                                2'b11 : 2'b10;
                        validator_palette_required <= 1'b0;
                        validator_start <= 1'b1;
                        state <= ST_DESTINATION_VALIDATE_WAIT;
                    end

                    ST_DESTINATION_VALIDATE_WAIT: begin
                        if (validator_done) begin
                            // A blended FILL_RECTS needs a direct-color
                            // destination whose rows the burst mover can
                            // read and write in whole beats.
                            validation_error_q <= !validator_valid ||
                                ((engine_geometry_q || command_is_flood_q ||
                                  command_is_glyph_q) &&
                                 validator_format >
                                     `ASTRA_RENDER_FORMAT_ARGB8888) ||
                                (command_is_fill_rects_q &&
                                 command_words[12][1] &&
                                 (validator_format <
                                      `ASTRA_RENDER_FORMAT_RGB565 ||
                                  validator_format >
                                      `ASTRA_RENDER_FORMAT_ARGB8888 ||
                                  validator_pitch[2:0] != 3'd0));
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
                                    command_is_glyph_q ||
                                    triangles_textured_q) begin
                                    lookup_offset_q <=
                                        source_descriptor_offset_q;
                                    desc_kind_q <= 2'd1;
                                    state <= ST_DESC_LOOKUP;
                                end else if (command_is_flood_q) begin
                                    lookup_offset_q <=
                                        auxiliary_descriptor_offset_q;
                                    desc_kind_q <= 2'd2;
                                    state <= ST_DESC_LOOKUP;
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
                                // The descriptor words come from the slot
                                // on a miss as on a hit: one load path.
                                slot_valid_q[slot_fill_q] <= 1'b1;
                                load_issue_q <= 4'd0;
                                load_capture_q <= 4'd0;
                                load_valid1_q <= 1'b0;
                                load_valid2_q <= 1'b0;
                                state <= ST_DESC_CACHE_LOAD;
                            end
                        end else begin
                            read_beat_index <= read_beat_index + 5'd1;
                            state <= ST_SOURCE_R;
                        end
                    end

                    ST_SOURCE_VALIDATE_START: begin
                        validator_required_access <= 2'b01;
                        validator_palette_required <= command_is_glyph_q ||
                            command_is_triangles_q || command_flags_q[5];
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
                                    lookup_offset_q <=
                                        auxiliary_descriptor_offset_q;
                                    desc_kind_q <= 2'd2;
                                    state <= ST_DESC_LOOKUP;
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
                                // The descriptor words come from the slot
                                // on a miss as on a hit: one load path.
                                slot_valid_q[slot_fill_q] <= 1'b1;
                                load_issue_q <= 4'd0;
                                load_capture_q <= 4'd0;
                                load_valid1_q <= 1'b0;
                                load_valid2_q <= 1'b0;
                                state <= ST_DESC_CACHE_LOAD;
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
                        range_mask_q <= range_mask & 30'h3ffffffe;
                        range_load_active_q <= 1'b1;
                        range_load_valid_q <= 1'b0;
                        range_end_valid_q <= 1'b0;
                        range_overlap_q <= 1'b0;
                        range_compare_last_q <= 1'b0;
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
                                range_check_enabled_q <=
                                    command_reads_source_q;
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
                                range_check_enabled_q <=
                                    command_reads_source_q;
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                            end
                            4'd7: begin
                                range_check_enabled_q <=
                                    command_reads_source_q;
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                                range_second_offset_q <=
                                    active_completion_ring_offset_q;
                                range_second_bytes_q <=
                                    COMPLETION_RING_BYTES;
                            end
                            4'd8: begin
                                range_check_enabled_q <=
                                    command_reads_source_q;
                                range_first_offset_q <= source_data_offset_q;
                                range_first_bytes_q <= source_data_bytes_q;
                                range_second_offset_q <=
                                    destination_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            4'd9: begin
                                range_check_enabled_q <=
                                    command_reads_source_q;
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
                                    command_is_glyph_q || triangles_textured_q;
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
                                    command_uses_auxiliary_q ||
                                    command_uses_array_q;
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                            end
                            5'd18: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q ||
                                    command_uses_array_q;
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    active_completion_ring_offset_q;
                                range_second_bytes_q <=
                                    COMPLETION_RING_BYTES;
                            end
                            5'd19: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q ||
                                    command_uses_array_q;
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    destination_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd20: begin
                                range_check_enabled_q <=
                                    (command_is_blit_q && command_flags_q[3]) ||
                                    command_is_glyph_q || triangles_textured_q;
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    source_descriptor_offset_q;
                                range_second_bytes_q <=
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd21: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q ||
                                    command_uses_array_q;
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    command_uses_array_q ?
                                    destination_data_offset_q :
                                    auxiliary_descriptor_offset_q;
                                range_second_bytes_q <= command_uses_array_q ?
                                    destination_data_bytes_q :
                                    `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
                            end
                            5'd22: begin
                                range_check_enabled_q <=
                                    command_uses_auxiliary_q ||
                                    command_uses_array_q;
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    command_uses_array_q ? source_data_offset_q :
                                    destination_data_offset_q;
                                range_second_bytes_q <=
                                    command_uses_array_q ? source_data_bytes_q :
                                    destination_data_bytes_q;
                            end
                            5'd23: begin
                                range_check_enabled_q <=
                                    (command_is_blit_q && command_flags_q[3]) ||
                                    (command_uses_array_q &&
                                     command_uses_palette_q);
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <= command_uses_array_q ?
                                    source_palette_offset_q : source_data_offset_q;
                                range_second_bytes_q <= command_uses_array_q ?
                                    source_palette_bytes_q : source_data_bytes_q;
                            end
                            5'd24: begin
                                range_check_enabled_q <=
                                    (command_is_blit_q && command_flags_q[3] &&
                                     command_flags_q[5]) ||
                                    (command_uses_array_q &&
                                     active_protected0_valid_q);
                                range_check_protected_q <= command_uses_array_q;
                                range_first_offset_q <=
                                    command_uses_array_q ? command_words[10] :
                                    auxiliary_data_offset_q;
                                range_first_bytes_q <=
                                    command_uses_array_q ? array_bytes_q :
                                    auxiliary_data_bytes_q;
                                range_second_offset_q <=
                                    command_uses_array_q ?
                                    active_protected0_offset_q :
                                    source_palette_offset_q;
                                range_second_bytes_q <= command_uses_array_q ?
                                    active_protected0_bytes_q : 32'd1024;
                            end
                            5'd25: begin
                                range_check_enabled_q <=
                                    command_uses_array_q ?
                                    active_protected1_valid_q :
                                    command_uses_auxiliary_q;
                                range_check_protected_q <= command_uses_array_q;
                                range_first_offset_q <= command_uses_array_q ?
                                    command_words[10] : destination_data_offset_q;
                                range_first_bytes_q <= command_uses_array_q ?
                                    array_bytes_q :
                                    destination_data_bytes_q;
                                range_second_offset_q <=
                                    command_uses_array_q ?
                                    active_protected1_offset_q :
                                    auxiliary_descriptor_offset_q;
                                range_second_bytes_q <= command_uses_array_q ?
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

                        // Load -> end -> compare -> decide, one pair per
                        // cycle, decided in index order so the first
                        // overlapping pair still names the fault.
                        // Pairs this command disables are skipped.
                        range_load_valid_q <= range_load_active_q;
                        range_load_last_q <= range_mask_q == 30'd0;
                        if (range_mask_q == 30'd0) begin
                            range_load_active_q <= 1'b0;
                        end else begin
                            range_check_index_q <= range_mask_index;
                            range_mask_q <= range_mask_q & ~range_mask_lowest;
                        end

                        range_first_end_q <=
                            {1'b0, range_first_offset_q} +
                            {1'b0, range_first_bytes_q};
                        range_second_end_q <=
                            {1'b0, range_second_offset_q} +
                            {1'b0, range_second_bytes_q};
                        range_end_valid_q <= range_load_valid_q;
                        range_end_last_q <= range_load_last_q;
                        range_end_enabled_q <= range_check_enabled_q &&
                            range_first_bytes_q != 32'd0 &&
                            range_second_bytes_q != 32'd0;
                        range_end_protected_q <= range_check_protected_q;
                        range_end_first_offset_q <= range_first_offset_q;
                        range_end_second_offset_q <= range_second_offset_q;

                        range_overlap_q <= range_end_valid_q &&
                            range_end_enabled_q &&
                            {1'b0, range_end_first_offset_q} <
                                range_second_end_q &&
                            {1'b0, range_end_second_offset_q} <
                                range_first_end_q;
                        range_compare_last_q <= range_end_valid_q &&
                            range_end_last_q;
                        range_compare_protected_q <= range_end_protected_q;

                        if (range_overlap_q) begin
                            completion_status_q <=
                                `ASTRA_RENDER_STATUS_BAD_RANGE;
                            completion_fault_q <= range_compare_protected_q ?
                                32'h00060002 : 32'h00060001;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (range_compare_last_q) begin
                            state <= ST_DISPATCH;
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
                        end else if (!command_is_fill_q &&
                                     !command_is_geometry_q &&
                                     !engine_writes_drained) begin
                            // Engines that read pixels start after every
                            // earlier write is answered.
                        end else begin
                            deadline_remaining_us_q <= command_deadline_us_q;
                            deadline_subcycle_q <=
                                {DEADLINE_SUBCYCLE_WIDTH{1'b0}};
                            deadline_active <= 1'b1;
                            if (command_is_records_q) begin
                            end else if (command_is_geometry_q)
                                geometry_start <= 1'b1;
                            else if (command_is_flood_q)
                                flood_start <= 1'b1;
                            else if (command_is_glyph_q)
                                glyph_start <= 1'b1;
                            else if (command_is_triangles_q)
                                texture_start <= 1'b1;
                            else
                                blitter_start <= 1'b1;
                            command_dispatched_q <= 1'b1;
                            state <= ST_EXECUTE;
                            if (command_is_records_q) begin
                                // The array leaves the command words, so each
                                // record runs as a plain FILL or LINE.
                                command_words[10] <= 32'd0;
                                command_words[11] <= 32'd0;
                                rects_remaining_q <= command_words[11][12:0];
                                rects_unfetched_q <= command_words[11][12:0];
                                rects_address_q <= ARENA_BASE +
                                    command_words[10];
                                rects_color_q <= command_words[15];
                                rects_record_color_q <= command_words[12][0];
                                rects_blend_q <= command_is_fill_rects_q &&
                                    command_words[12][1];
                                rects_buffered_q <= 7'd0;
                                rects_index_q <= 7'd0;
                                rects_pixels_q <= 32'd0;
                                rects_ran_q <= 1'b0;
                                rects_slot_valid_q <= {RECT_SLOTS{1'b0}};
                                state <= ST_RECTS_NEXT;
                            end
                        end
                    end

                    // One record per pass: stop on a failed record, a
                    // cancellation or the deadline; refill the buffer; or
                    // load the next record.
                    ST_RECTS_NEXT: begin
                        if (rects_ran_q && (command_is_lines_q ?
                                geometry_status : blitter_status) !=
                                `ASTRA_RENDER_STATUS_OK) begin
                            deadline_active <= 1'b0;
                            state <= ST_CAPTURE_ENGINE_COMPLETION;
                        end else if (soft_reset || deadline_expired_q) begin
                            deadline_active <= 1'b0;
                            completion_status_q <= soft_reset ?
                                `ASTRA_RENDER_STATUS_RESET :
                                `ASTRA_RENDER_STATUS_TIMEOUT;
                            completion_count_q <= 32'd0;
                            completion_fault_q <= soft_reset ?
                                32'h00000001 : 32'h00000002;
                            reset_count <= reset_count + 32'd1;
                            if (!soft_reset)
                                timeout_count <= timeout_count + 32'd1;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (rects_remaining_q == 13'd0) begin
                            deadline_active <= 1'b0;
                            completion_status_q <= `ASTRA_RENDER_STATUS_OK;
                            completion_count_q <= rects_pixels_q;
                            completion_fault_q <= 32'd0;
                            state <= ST_PREPARE_COMPLETION;
                        end else if (rects_index_q == rects_buffered_q) begin
                            // Up to 64 records, not past a 4 KiB page.
                            rects_chunk_q <=
                                {2'b00, rects_page_records} <
                                    {4'd0, rects_limit} ?
                                    rects_page_records[6:0] : rects_limit;
                            state <= ST_RECTS_AR;
                        end else begin
                            load_issue_q <= 4'd0;
                            load_capture_q <= 4'd0;
                            load_valid1_q <= 1'b0;
                            load_valid2_q <= 1'b0;
                            state <= ST_RECTS_LOAD;
                        end
                    end

                    ST_RECTS_AR: begin
                        if (!manager_arvalid) begin
                            manager_araddr <= rects_address_q;
                            manager_arlen <= rects_last_beat;
                            manager_arvalid <= 1'b1;
                            rects_reading_q <= 1'b1;
                            rects_beat_q <= 8'd0;
                            rects_read_error_q <= 1'b0;
                        end else if (m_axi_arready) begin
                            manager_arvalid <= 1'b0;
                            rects_address_q <= rects_address_q +
                                {21'd0, rects_chunk_q, 4'd0};
                            rects_unfetched_q <= rects_unfetched_q -
                                {6'd0, rects_chunk_q};
                            rects_buffered_q <= rects_chunk_q;
                            rects_index_q <= 7'd0;
                            state <= ST_RECTS_R;
                        end
                    end

                    // Two beats per record; a failed or short response
                    // ends the command once its burst has drained.
                    ST_RECTS_R: begin
                        if (rects_accept) begin
                            rects_beat_q <= rects_beat_q + 8'd1;
                            if (m_axi_rid != MANAGER_READ_ID ||
                                m_axi_rresp != 2'b00 ||
                                m_axi_rlast !=
                                    (rects_beat_q ==
                                     rects_last_beat))
                                rects_read_error_q <= 1'b1;
                            if (m_axi_rlast) begin
                                rects_reading_q <= 1'b0;
                                if (rects_read_error_q ||
                                    m_axi_rid != MANAGER_READ_ID ||
                                    m_axi_rresp != 2'b00 ||
                                    rects_beat_q !=
                                        rects_last_beat) begin
                                    deadline_active <= 1'b0;
                                    completion_status_q <=
                                        `ASTRA_RENDER_STATUS_AXI_READ;
                                    completion_count_q <= rects_pixels_q;
                                    completion_fault_q <= 32'h000d0001;
                                    state <= ST_PREPARE_COMPLETION;
                                end else begin
                                    state <= ST_RECTS_NEXT;
                                end
                            end
                        end
                    end

                    // The record's two beats from the local RAM.
                    ST_RECTS_LOAD: begin
                        if (load_issue_q != 4'd2) begin
                            local_ram_raddr_q <= {2'b11, rects_index_q[5:0],
                                                  load_issue_q[0]};
                            load_issue_q <= load_issue_q + 4'd1;
                        end
                        load_valid1_q <= load_issue_q != 4'd2;
                        load_valid2_q <= load_valid1_q;
                        if (load_valid2_q) begin
                            load_capture_q <= load_capture_q + 4'd1;
                            if (load_capture_q == 4'd0) begin
                                // The engine is idle between records. A FILL
                                // takes point and extent in words 12 and 14,
                                // a LINE its endpoints in words 11 and 12.
                                if (command_is_lines_q) begin
                                    command_words[11] <=
                                        swap32(local_ram_q[31:0]);
                                    command_words[12] <=
                                        swap32(local_ram_q[63:32]);
                                end else begin
                                    command_words[12] <=
                                        swap32(local_ram_q[31:0]);
                                    command_words[14] <=
                                        swap32(local_ram_q[63:32]);
                                end
                            end else begin
                                rects_index_q <= rects_index_q + 7'd1;
                                rects_remaining_q <= rects_remaining_q - 13'd1;
                                if (!command_is_lines_q &&
                                    (command_words[14][31:16] == 16'd0 ||
                                     command_words[14][15:0] == 16'd0)) begin
                                    // An empty record is a no-op.
                                    state <= ST_RECTS_NEXT;
                                end else begin
                                    command_words[15] <= rects_record_color_q ?
                                        swap32(local_ram_q[31:0]) :
                                        rects_color_q;
                                    if (rects_blend_q) begin
                                        state <= ST_RECTS_WAIT;
                                    end else begin
                                        if (command_is_lines_q)
                                            geometry_start <= 1'b1;
                                        else
                                            blitter_start <= 1'b1;
                                        rects_ran_q <= 1'b1;
                                        state <= ST_EXECUTE;
                                    end
                                end
                            end
                        end
                    end

                    // A blended record reads the pixels it blends into: it
                    // waits while a record whose writes may be unanswered
                    // overlaps it.
                    ST_RECTS_WAIT: begin
                        if (!rect_conflict && rect_slot_free) begin
                            blitter_start <= 1'b1;
                            rects_ran_q <= 1'b1;
                            rects_slot_q <= rect_free_slot;
                            rects_slot_valid_q[rect_free_slot] <= 1'b1;
                            rects_slot_marked_q[rect_free_slot] <= 1'b0;
                            rects_slot_x0_q[rect_free_slot] <= rect_x0;
                            rects_slot_y0_q[rect_free_slot] <= rect_y0;
                            rects_slot_x1_q[rect_free_slot] <= rect_x1;
                            rects_slot_y1_q[rect_free_slot] <= rect_y1;
                            state <= ST_EXECUTE;
                        end
                    end

                    ST_EXECUTE: begin
                        if ((soft_reset || deadline_expired_q) &&
                            command_active) begin
                            deadline_active <= 1'b0;
                            if (engine_geometry_q)
                                geometry_abort <= 1'b1;
                            else if (command_is_flood_q)
                                flood_abort <= 1'b1;
                            else if (command_is_glyph_q)
                                glyph_abort <= 1'b1;
                            else if (command_is_triangles_q)
                                texture_abort <= 1'b1;
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
                            reset_count <= reset_count + 32'd1;
                            if (!soft_reset)
                                timeout_count <= timeout_count + 32'd1;
                            state <= ST_ABORT_WAIT;
                        end else if ((engine_geometry_q && geometry_done) ||
                                     (command_is_flood_q && flood_done) ||
                                     (command_is_glyph_q && glyph_done) ||
                                     (command_is_triangles_q && texture_done) ||
                                     (!engine_geometry_q &&
                                      !command_is_flood_q &&
                                      !command_is_glyph_q &&
                                      !command_is_triangles_q &&
                                      blitter_done)) begin
                            if (command_is_records_q) begin
                                // Every write of this record is issued.
                                rects_slot_mark_q[rects_slot_q] <=
                                    engine_issued_q;
                                rects_slot_marked_q[rects_slot_q] <= 1'b1;
                                rects_pixels_q <= rects_pixels_q +
                                    (command_is_lines_q ?
                                        geometry_completed_pixels :
                                        blitter_completed_pixels);
                                state <= ST_RECTS_NEXT;
                            end else begin
                                deadline_active <= 1'b0;
                                state <= ST_CAPTURE_ENGINE_COMPLETION;
                            end
                        end
                    end

                    ST_CAPTURE_ENGINE_COMPLETION: begin
                        completion_status_q <= command_is_triangles_q ?
                            texture_status : command_is_glyph_q ?
                            glyph_status : command_is_flood_q ?
                            flood_status : engine_geometry_q ?
                            geometry_status : blitter_status;
                        completion_count_q <= command_is_records_q ?
                            rects_pixels_q : command_is_triangles_q ?
                            texture_completed_pixels : command_is_glyph_q ?
                            glyph_completed_pixels : command_is_flood_q ?
                            flood_completed_pixels : engine_geometry_q ?
                            geometry_completed_pixels :
                            blitter_completed_pixels;
                        completion_fault_q <= command_is_triangles_q ?
                            texture_fault_detail : command_is_glyph_q ?
                            glyph_fault_detail : command_is_flood_q ?
                            flood_fault_detail : engine_geometry_q ?
                            geometry_fault_detail : blitter_fault_detail;
                        state <= ST_PREPARE_COMPLETION;
                    end

                    ST_PREPARE_COMPLETION: begin
                        deadline_active <= 1'b0;
                        cancel_before_dispatch <= 1'b0;
                        // Only descriptors this command referenced (and so
                        // range-checked against its writes) stay cached.
                        slot_valid_q <= slot_valid_q & slot_used_q;
                        if (completion_fatal_q) begin
                            busy <= 1'b0;
                            state <= ST_FATAL;
                        end else if (cq_count_q != CQ_DEPTH) begin
                            // A write error answered while this command ran
                            // is its status, as when engines waited for it.
                            if (cur_werr_q && completion_status_q ==
                                    `ASTRA_RENDER_STATUS_OK) begin
                                completion_status_q <=
                                    `ASTRA_RENDER_STATUS_AXI_WRITE;
                                completion_fault_q <= {16'h0002, 2'b00,
                                    cur_werr_detail_q[7:2], 6'd0,
                                    cur_werr_detail_q[1:0]};
                            end
                            completion_end_cycle_q <= cycle_counter;
                            // The record waits for every engine write issued
                            // so far, this command's included.
                            cq_mark_q[cq_tail_q] <= engine_issued_q;
                            cq_slot_q[cq_tail_q] <=
                                completion_producer[9:0] + {{(9-CQ_BITS){1'b0}}, cq_count_q};
                            enqueue_beat_q <= 2'd0;
                            state <= ST_COMPLETION_AW;
                        end
                    end

                    // Queue the record, one beat per clock.
                    ST_COMPLETION_AW: begin
                        cq_beat_we_q <= 1'b1;
                        cq_beat_waddr_q <= {cq_tail_q, enqueue_beat_q};
                        case (enqueue_beat_q)
                            2'd0: cq_beat_wdata_q <= {
                                swap32({command_opcode_q, completion_status_q}),
                                swap32({16'(`ASTRA_RENDER_ABI_VERSION),
                                        16'(`ASTRA_RENDER_COMPLETION_BYTES)})
                            };
                            2'd1: cq_beat_wdata_q <= {
                                swap32(completion_count_q),
                                swap32(command_sequence_q)
                            };
                            2'd2: cq_beat_wdata_q <= {
                                swap32(completion_end_cycle_q),
                                swap32(command_start_cycle)
                            };
                            default: cq_beat_wdata_q <= {
                                swap32(command_generation_q),
                                swap32(completion_fault_q)
                            };
                        endcase
                        if (enqueue_beat_q == 2'd0) begin
                            completion_failed_q <= completion_status_q !=
                                `ASTRA_RENDER_STATUS_OK;
                            cq_info_we_q <= 1'b1;
                            cq_info_waddr_q <= cq_tail_q;
                            cq_info_wdata_q <= {
                                completion_status_q != `ASTRA_RENDER_STATUS_OK,
                                completion_fault_q, command_sequence_q};
                        end
                        enqueue_beat_q <= enqueue_beat_q + 2'd1;
                        if (enqueue_beat_q == 2'd3)
                            state <= ST_COMPLETION_W;
                    end

                    // The last beat lands this cycle; publish the entry.
                    ST_COMPLETION_W: begin
                        // Errors answered after the status was captured.
                        if (cur_werr_q || werr_to_current) begin
                            cq_werr_q[cq_tail_q] <= 1'b1;
                            cq_werr_detail_q[cq_tail_q] <= cur_werr_q ?
                                cur_werr_detail_q :
                                {response_id[5:0], m_axi_bresp};
                        end
                        cq_tail_q <= cq_tail_q + 1'b1;
                        state <= ST_RETIRE;
                    end

                    ST_RETIRE: begin
                        state <= ST_IDLE;
                    end

                    ST_ABORT_WAIT: begin
                        engine_reset_active <= 1'b1;
                        if (engine_geometry_q)
                            geometry_abort <= 1'b1;
                        else if (command_is_flood_q)
                            flood_abort <= 1'b1;
                        else if (command_is_glyph_q)
                            glyph_abort <= 1'b1;
                        else if (command_is_triangles_q)
                            texture_abort <= 1'b1;
                        else
                            blitter_abort <= 1'b1;
                        if ((engine_geometry_q && geometry_done) ||
                            (command_is_flood_q && flood_done) ||
                            (command_is_glyph_q && glyph_done) ||
                            (command_is_triangles_q && texture_done) ||
                            (!engine_geometry_q && !command_is_flood_q &&
                             !command_is_glyph_q && !command_is_triangles_q &&
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
                        prefetch_count_q <= 6'd0;
                        prefetch_next_q <= 6'd0;
                        slot_valid_q <= 3'd0;
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
