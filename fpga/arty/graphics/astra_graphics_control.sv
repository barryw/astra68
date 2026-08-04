// Copyright (c) 2026 Astra68 contributors
//
// AXI4-Lite control plane for the first Arty Vega integration. Software edits
// shadow registers and requests an atomic frame-boundary promotion. Palette
// writes are accepted only while scanout is disabled.
`timescale 1ns/1ps
`default_nettype none

module astra_graphics_control #(
    parameter [31:0] ARENA_BASE = 32'h18000000,
    parameter [31:0] ARENA_LIMIT = 32'h20000000,
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720,
    parameter integer BOOT_TEXT_COLS = 36,
    parameter integer BOOT_TEXT_ROWS = 4,
    parameter integer BOOT_TEXT_ORIGIN_X = 264,
    parameter integer BOOT_TEXT_ORIGIN_Y = 496,
    parameter integer BOOT_TEXT_CELL_WIDTH = 16,
    parameter integer BOOT_TEXT_ROW_PITCH = 32
) (
    input  wire        clk,
    input  wire        reset,
    input  wire        frame_boundary,
    input  wire        commit_safe,
    output reg         scene_changed,
    output wire [31:0] active_generation,
    output wire        commit_pending_status,
    output reg         commit_quiesce,
    output reg  [31:0] commit_errors,
    output reg  [31:0] commit_deferrals,

    output wire        scene_enable,
    output wire [23:0] backdrop_rgb,

    output wire        framebuffer_enable,
    output wire [1:0]  framebuffer_format,
    output wire [31:0] framebuffer_base,
    output wire [31:0] framebuffer_pitch,
    output wire [12:0] framebuffer_width,
    output wire [12:0] framebuffer_height,
    output wire signed [31:0] framebuffer_viewport_x,
    output wire signed [31:0] framebuffer_viewport_y,
    output wire        framebuffer_wrap_x,
    output wire        framebuffer_wrap_y,
    output wire        framebuffer_key_enable,
    output wire [31:0] framebuffer_key,

    output wire        tile0_enable,
    output wire        tile0_above_framebuffer,
    output wire [7:0]  tile0_opacity,
    output wire        tile0_tile_16,
    output wire        tile0_index_8,
    output wire [3:0]  tile0_map_width_log2,
    output wire [3:0]  tile0_map_height_log2,
    output wire        tile0_wrap_x,
    output wire        tile0_wrap_y,
    output wire        tile0_transparent_enable,
    output wire [7:0]  tile0_transparent_index,
    output wire signed [31:0] tile0_scroll_x,
    output wire signed [31:0] tile0_scroll_y,
    output wire [31:0] tile0_map_base,
    output wire [31:0] tile0_pattern_base,
    output wire [16:0] tile0_tile_count,

    output wire        tile1_enable,
    output wire        tile1_above_framebuffer,
    output wire [7:0]  tile1_opacity,
    output wire        tile1_tile_16,
    output wire        tile1_index_8,
    output wire [3:0]  tile1_map_width_log2,
    output wire [3:0]  tile1_map_height_log2,
    output wire        tile1_wrap_x,
    output wire        tile1_wrap_y,
    output wire        tile1_transparent_enable,
    output wire [7:0]  tile1_transparent_index,
    output wire signed [31:0] tile1_scroll_x,
    output wire signed [31:0] tile1_scroll_y,
    output wire [31:0] tile1_map_base,
    output wire [31:0] tile1_pattern_base,
    output wire [16:0] tile1_tile_count,

    output wire        sprite_enable,
    output reg         sprite_descriptor_write_enable,
    output reg  [5:0]  sprite_descriptor_write_index,
    output reg  [2:0]  sprite_descriptor_write_word,
    output reg  [31:0] sprite_descriptor_write_data,
    output reg         sprite_palette_write_enable,
    output reg  [3:0]  sprite_palette_write_bank,
    output reg  [7:0]  sprite_palette_write_index,
    output reg  [31:0] sprite_palette_write_argb,
    input  wire        sprite_scene_write_ready,
    output reg         sprite_validate_start,
    input  wire        sprite_validate_busy,
    input  wire        sprite_validate_done,
    input  wire        sprite_validate_valid,
    output reg         sprite_accept_pending,
    input  wire        sprite_pending_ready,
    input  wire        sprite_pending_valid,
    output reg         sprite_activate_start,
    input  wire        sprite_activate_busy,
    input  wire        sprite_activate_done,
    input  wire        sprite_builder_busy,
    input  wire [3:0]  sprite_slot_valid,
    input  wire        sprite_fetch_error,
    input  wire        sprite_deadline_error,
    input  wire [31:0] sprite_build_cycles,
    input  wire [31:0] sprite_max_build_cycles,
    input  wire [31:0] sprite_axi_error_count,
    input  wire [31:0] sprite_deadline_error_count,
    input  wire [31:0] sprite_read_bytes,
    input  wire [63:0] sprite_overflow_bitmap,
    input  wire [9:0]  sprite_overflow_line,
    input  wire [31:0] sprite_overflow_count,
    input  wire [31:0] sprite_pixels_admitted,
    input  wire [31:0] sprite_pixels_dropped,
    output reg  [5:0]  sprite_collision_read_row,
    input  wire [63:0] sprite_collision_read_data,
    input  wire [31:0] sprite_collision_frame,
    input  wire        sprite_collision_event,

    output reg         framebuffer_palette_write_enable,
    output reg  [7:0]  framebuffer_palette_write_index,
    output reg  [31:0] framebuffer_palette_write_argb,
    output reg         tile_palette_write_enable,
    output reg  [3:0]  tile_palette_write_bank,
    output reg  [7:0]  tile_palette_write_index,
    output reg  [31:0] tile_palette_write_argb,
    input  wire        palette_write_ready,

    output wire        boot_text_shadow_enable,
    output reg         boot_text_write_enable,
    output reg  [7:0]  boot_text_write_index,
    output reg  [15:0] boot_text_write_cell,
    output reg         boot_text_commit_enable,
    input  wire        boot_text_write_ready,
    input  wire        boot_text_commit_ready,
    input  wire        boot_text_active_enable,
    input  wire [31:0] boot_text_generation,

    output wire        render_enable,
    output reg         render_queue_rebase,
    output reg         render_soft_reset,
    output reg  [31:0] render_submission_ring_offset,
    output reg  [10:0] render_submission_producer,
    input  wire [10:0] render_submission_consumer,
    input  wire        copper_dispatch_valid,
    input  wire [10:0] copper_dispatch_submission_producer,
    output wire        copper_dispatch_ready,
    output wire        copper_dispatch_allowed,
    output reg  [31:0] render_completion_ring_offset,
    input  wire [10:0] render_completion_producer,
    output reg  [10:0] render_completion_consumer,
    output reg  [31:0] render_resource_generation,
    input  wire        render_busy,
    input  wire        render_engine_reset_active,
    input  wire        render_configuration_fault,
    input  wire        render_completion_irq,
    input  wire [31:0] render_retired_fence,
    input  wire [31:0] render_commands_submitted,
    input  wire [31:0] render_commands_completed,
    input  wire [31:0] render_commands_failed,
    input  wire [31:0] render_backpressure_cycles,
    input  wire [31:0] render_timeout_count,
    input  wire [31:0] render_reset_count,
    input  wire [31:0] render_last_fault_detail,
    output wire        render_interrupt,
    output wire        render_protected0_valid,
    output wire [31:0] render_protected0_offset,
    output wire [31:0] render_protected0_bytes,
    output wire        render_protected1_valid,
    output wire [31:0] render_protected1_offset,
    output wire [31:0] render_protected1_bytes,

    input  wire [31:0] s_axi_awaddr,
    input  wire [2:0]  s_axi_awprot,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output reg  [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [31:0] s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output reg  [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready
);
    localparam [31:0] DEVICE_ID = 32'h41535452;
    localparam [31:0] VERSION = 32'h00010005;
    localparam [31:0] CAPABILITIES = 32'h000003ff;
    localparam integer BOOT_TEXT_CELLS = BOOT_TEXT_COLS * BOOT_TEXT_ROWS;

    function automatic [31:0] merge_write(
        input [31:0] previous,
        input [31:0] value,
        input [3:0] strobes
    );
        integer lane;
        begin
            merge_write = previous;
            for (lane = 0; lane < 4; lane = lane + 1)
                if (strobes[lane])
                    merge_write[lane * 8 +: 8] = value[lane * 8 +: 8];
        end
    endfunction

    reg [31:0] shadow_global_control;
    reg [31:0] shadow_backdrop;
    reg [31:0] shadow_fb_base;
    reg [31:0] shadow_fb_pitch;
    reg [31:0] shadow_fb_size;
    reg [31:0] shadow_fb_viewport_x;
    reg [31:0] shadow_fb_viewport_y;
    reg [31:0] shadow_fb_control;
    reg [31:0] shadow_fb_key;
    reg [31:0] shadow_tile0_map_base;
    reg [31:0] shadow_tile0_pattern_base;
    reg [31:0] shadow_tile0_scroll_x;
    reg [31:0] shadow_tile0_scroll_y;
    reg [31:0] shadow_tile0_geometry;
    reg [31:0] shadow_tile0_count;
    reg [31:0] shadow_tile0_control;
    reg [31:0] shadow_tile1_map_base;
    reg [31:0] shadow_tile1_pattern_base;
    reg [31:0] shadow_tile1_scroll_x;
    reg [31:0] shadow_tile1_scroll_y;
    reg [31:0] shadow_tile1_geometry;
    reg [31:0] shadow_tile1_count;
    reg [31:0] shadow_tile1_control;
    reg [31:0] shadow_sprite_control;

    reg [31:0] pending_global_control_q;
    reg [31:0] pending_backdrop_q;
    reg [31:0] pending_fb_base_q;
    reg [31:0] pending_fb_pitch_q;
    reg [31:0] pending_fb_size_q;
    reg [31:0] pending_fb_viewport_x_q;
    reg [31:0] pending_fb_viewport_y_q;
    reg [31:0] pending_fb_control_q;
    reg [31:0] pending_fb_key_q;
    reg        pending_fb_protection_valid_q;
    reg [31:0] pending_fb_protection_offset_q;
    reg [31:0] pending_fb_protection_bytes_q;
    reg [31:0] pending_tile0_map_base_q;
    reg [31:0] pending_tile0_pattern_base_q;
    reg [31:0] pending_tile0_scroll_x_q;
    reg [31:0] pending_tile0_scroll_y_q;
    reg [31:0] pending_tile0_geometry_q;
    reg [31:0] pending_tile0_count_q;
    reg [31:0] pending_tile0_control_q;
    reg [31:0] pending_tile1_map_base_q;
    reg [31:0] pending_tile1_pattern_base_q;
    reg [31:0] pending_tile1_scroll_x_q;
    reg [31:0] pending_tile1_scroll_y_q;
    reg [31:0] pending_tile1_geometry_q;
    reg [31:0] pending_tile1_count_q;
    reg [31:0] pending_tile1_control_q;
    reg [31:0] pending_sprite_control_q;

    reg [31:0] active_global_control_q;
    reg [31:0] active_backdrop_q;
    reg [31:0] active_fb_base_q;
    reg [31:0] active_fb_pitch_q;
    reg [31:0] active_fb_size_q;
    reg [31:0] active_fb_viewport_x_q;
    reg [31:0] active_fb_viewport_y_q;
    reg [31:0] active_fb_control_q;
    reg [31:0] active_fb_key_q;
    reg        active_fb_protection_valid_q;
    reg [31:0] active_fb_protection_offset_q;
    reg [31:0] active_fb_protection_bytes_q;
    reg [31:0] active_tile0_map_base_q;
    reg [31:0] active_tile0_pattern_base_q;
    reg [31:0] active_tile0_scroll_x_q;
    reg [31:0] active_tile0_scroll_y_q;
    reg [31:0] active_tile0_geometry_q;
    reg [31:0] active_tile0_count_q;
    reg [31:0] active_tile0_control_q;
    reg [31:0] active_tile1_map_base_q;
    reg [31:0] active_tile1_pattern_base_q;
    reg [31:0] active_tile1_scroll_x_q;
    reg [31:0] active_tile1_scroll_y_q;
    reg [31:0] active_tile1_geometry_q;
    reg [31:0] active_tile1_count_q;
    reg [31:0] active_tile1_control_q;
    reg [31:0] active_sprite_control_q;
    reg [31:0] generation_q;
    reg commit_pending_q;

    reg [31:0] framebuffer_palette_selector;
    reg [31:0] tile_palette_selector;
    reg [10:0] sprite_descriptor_selector;
    reg [11:0] sprite_palette_selector;
    reg [31:0] shadow_boot_text_control;
    reg [7:0] boot_text_selector;
    reg [31:0] render_control_q;
    reg render_irq_pending_q;
    reg boot_text_write_busy_q;
    reg boot_text_write_busy_seen_q;
    reg boot_text_commit_busy_q;
    reg boot_text_commit_busy_seen_q;

    assign boot_text_shadow_enable = shadow_boot_text_control[0];
    wire boot_text_write_available = boot_text_write_ready &&
        !boot_text_write_busy_q && !boot_text_commit_busy_q;
    wire boot_text_commit_available = boot_text_commit_ready &&
        !boot_text_write_busy_q && !boot_text_commit_busy_q;

    reg commit_validation_pending_q;
    reg validator_start_q;
    reg shadow_config_dirty_q;
    reg shadow_scene_valid_q;
    reg framebuffer_validator_done_seen_q;
    reg framebuffer_validator_result_q;
    reg tile0_validator_done_seen_q;
    reg tile0_validator_result_q;
    reg tile1_validator_done_seen_q;
    reg tile1_validator_result_q;
    reg sprite_validator_done_seen_q;
    reg sprite_validator_result_q;
    reg sprite_clone_pending_q;
    reg sprite_activation_pending_q;

    wire framebuffer_validator_busy;
    wire framebuffer_validator_done;
    wire framebuffer_validator_valid;
    wire [31:0] framebuffer_validator_surface_bytes;
    astra_framebuffer_config_validator #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT)
    ) framebuffer_validator_i (
        .clk(clk),
        .reset(reset),
        .start(validator_start_q),
        .format(shadow_fb_control[2:1]),
        .framebuffer_base(shadow_fb_base),
        .pitch(shadow_fb_pitch),
        .virtual_width(shadow_fb_size[12:0]),
        .virtual_height(shadow_fb_size[28:16]),
        .viewport_x(shadow_fb_viewport_x),
        .viewport_y(shadow_fb_viewport_y),
        .wrap_x(shadow_fb_control[3]),
        .wrap_y(shadow_fb_control[4]),
        .arena_base(ARENA_BASE),
        .arena_limit(ARENA_LIMIT),
        .busy(framebuffer_validator_busy),
        .done(framebuffer_validator_done),
        .config_valid(framebuffer_validator_valid),
        .surface_bytes(framebuffer_validator_surface_bytes)
    );

    wire tile0_validator_busy;
    wire tile0_validator_done;
    wire tile0_validator_valid;
    astra_tile_config_validator tile0_validator_i (
        .clk(clk),
        .reset(reset),
        .start(validator_start_q),
        .tile_16(shadow_tile0_geometry[8]),
        .index_8(shadow_tile0_geometry[9]),
        .map_width_log2(shadow_tile0_geometry[3:0]),
        .map_height_log2(shadow_tile0_geometry[7:4]),
        .map_base(shadow_tile0_map_base),
        .pattern_base(shadow_tile0_pattern_base),
        .tile_count(shadow_tile0_count[16:0]),
        .arena_base(ARENA_BASE),
        .arena_limit(ARENA_LIMIT),
        .busy(tile0_validator_busy),
        .done(tile0_validator_done),
        .config_valid(tile0_validator_valid)
    );

    wire tile1_validator_busy;
    wire tile1_validator_done;
    wire tile1_validator_valid;
    astra_tile_config_validator tile1_validator_i (
        .clk(clk),
        .reset(reset),
        .start(validator_start_q),
        .tile_16(shadow_tile1_geometry[8]),
        .index_8(shadow_tile1_geometry[9]),
        .map_width_log2(shadow_tile1_geometry[3:0]),
        .map_height_log2(shadow_tile1_geometry[7:4]),
        .map_base(shadow_tile1_map_base),
        .pattern_base(shadow_tile1_pattern_base),
        .tile_count(shadow_tile1_count[16:0]),
        .arena_base(ARENA_BASE),
        .arena_limit(ARENA_LIMIT),
        .busy(tile1_validator_busy),
        .done(tile1_validator_done),
        .config_valid(tile1_validator_valid)
    );

    wire validators_done =
        framebuffer_validator_done_seen_q &&
        tile0_validator_done_seen_q &&
        tile1_validator_done_seen_q &&
        sprite_validator_done_seen_q;
    wire framebuffer_validator_result = framebuffer_validator_result_q;
    wire tile0_validator_result = tile0_validator_result_q;
    wire tile1_validator_result = tile1_validator_result_q;
    wire sprite_validator_result = sprite_validator_result_q;
    // shadow_sprite_control is assigned only after the write-time validator
    // has proved that bit 0 is the sole settable bit.
    wire validated_scene_valid = sprite_validator_result &&
        (!shadow_global_control[0] ||
         ((!shadow_fb_control[0] || framebuffer_validator_result) &&
          (!shadow_tile0_control[0] || tile0_validator_result) &&
          (!shadow_tile1_control[0] || tile1_validator_result)));
    wire shadow_scene_valid_status =
        !shadow_config_dirty_q && shadow_scene_valid_q;

    assign active_generation = generation_q;
    assign commit_pending_status = commit_pending_q ||
                                   commit_validation_pending_q;
    assign scene_enable = active_global_control_q[0];
    assign backdrop_rgb = active_backdrop_q[23:0];
    assign framebuffer_enable = active_fb_control_q[0];
    assign framebuffer_format = active_fb_control_q[2:1];
    assign framebuffer_base = active_fb_base_q;
    assign framebuffer_pitch = active_fb_pitch_q;
    assign framebuffer_width = active_fb_size_q[12:0];
    assign framebuffer_height = active_fb_size_q[28:16];
    assign framebuffer_viewport_x = active_fb_viewport_x_q;
    assign framebuffer_viewport_y = active_fb_viewport_y_q;
    assign framebuffer_wrap_x = active_fb_control_q[3];
    assign framebuffer_wrap_y = active_fb_control_q[4];
    assign framebuffer_key_enable = active_fb_control_q[5];
    assign framebuffer_key = active_fb_key_q;

    assign tile0_enable = active_tile0_control_q[0];
    assign tile0_above_framebuffer = active_tile0_control_q[1];
    assign tile0_transparent_enable = active_tile0_control_q[2];
    assign tile0_transparent_index = active_tile0_control_q[15:8];
    assign tile0_opacity = active_tile0_control_q[23:16];
    assign tile0_map_width_log2 = active_tile0_geometry_q[3:0];
    assign tile0_map_height_log2 = active_tile0_geometry_q[7:4];
    assign tile0_tile_16 = active_tile0_geometry_q[8];
    assign tile0_index_8 = active_tile0_geometry_q[9];
    assign tile0_wrap_x = active_tile0_geometry_q[10];
    assign tile0_wrap_y = active_tile0_geometry_q[11];
    assign tile0_map_base = active_tile0_map_base_q;
    assign tile0_pattern_base = active_tile0_pattern_base_q;
    assign tile0_scroll_x = active_tile0_scroll_x_q;
    assign tile0_scroll_y = active_tile0_scroll_y_q;
    assign tile0_tile_count = active_tile0_count_q[16:0];

    assign tile1_enable = active_tile1_control_q[0];
    assign tile1_above_framebuffer = active_tile1_control_q[1];
    assign tile1_transparent_enable = active_tile1_control_q[2];
    assign tile1_transparent_index = active_tile1_control_q[15:8];
    assign tile1_opacity = active_tile1_control_q[23:16];
    assign tile1_map_width_log2 = active_tile1_geometry_q[3:0];
    assign tile1_map_height_log2 = active_tile1_geometry_q[7:4];
    assign tile1_tile_16 = active_tile1_geometry_q[8];
    assign tile1_index_8 = active_tile1_geometry_q[9];
    assign tile1_wrap_x = active_tile1_geometry_q[10];
    assign tile1_wrap_y = active_tile1_geometry_q[11];
    assign tile1_map_base = active_tile1_map_base_q;
    assign tile1_pattern_base = active_tile1_pattern_base_q;
    assign tile1_scroll_x = active_tile1_scroll_x_q;
    assign tile1_scroll_y = active_tile1_scroll_y_q;
    assign tile1_tile_count = active_tile1_count_q[16:0];
    assign sprite_enable = active_sprite_control_q[0];
    assign render_enable = render_control_q[0];
    assign render_interrupt = render_irq_pending_q;

    assign render_protected0_valid = active_fb_protection_valid_q;
    assign render_protected0_offset = active_fb_protection_offset_q;
    assign render_protected0_bytes = active_fb_protection_bytes_q;
    assign render_protected1_valid = commit_pending_q &&
        pending_fb_protection_valid_q;
    assign render_protected1_offset = pending_fb_protection_offset_q;
    assign render_protected1_bytes = pending_fb_protection_bytes_q;

    reg aw_pending;
    reg [31:0] awaddr_q;
    reg w_pending;
    reg [31:0] wdata_q;
    reg [3:0] wstrb_q;
    reg write_execute_q;
    reg write_render_busy_q;
    reg write_alignment_valid_q;
    reg write_prefix_valid_q;
    reg write_full_strobe_q;
    reg write_commit_request_valid_q;
    reg write_boot_control_valid_q;
    reg write_boot_selector_valid_q;
    reg write_boot_cell_valid_q;
    reg write_boot_commit_valid_q;
    reg write_sprite_control_valid_q;
    reg write_sprite_descriptor_selector_valid_q;
    reg write_sprite_palette_selector_valid_q;
    reg write_collision_row_valid_q;
    reg ar_pending;
reg [11:0] araddr_q;
reg read_decode_pending_q;
reg read_response_pending_q;
reg [3:0] read_bank_q;
reg read_prefix_valid_q;
reg [31:0] read_bank_data_q [0:9];
reg [9:0] read_bank_valid_q;
// Keep a local copy of the word selector beside each read bank. The routed
// design otherwise fans one ARADDR bit through every bank decoder and across
// the full control block before reaching these registers.
(* dont_touch = "yes" *) reg [3:0] read_bank_word_q [0:9];
    assign s_axi_awready = !aw_pending && !s_axi_bvalid &&
                           !commit_validation_pending_q && !write_execute_q &&
                           !boot_text_write_busy_q &&
                           !boot_text_commit_busy_q;
    assign s_axi_wready = !w_pending && !s_axi_bvalid &&
                          !commit_validation_pending_q && !write_execute_q &&
                          !boot_text_write_busy_q &&
                          !boot_text_commit_busy_q;
    assign s_axi_arready = !ar_pending && !read_decode_pending_q &&
                           !read_response_pending_q && !s_axi_rvalid;
    wire write_fire = aw_pending && w_pending && !s_axi_bvalid &&
                      !commit_validation_pending_q && !write_execute_q;
    wire [10:0] copper_dispatch_advance =
        copper_dispatch_submission_producer - render_submission_producer;
    wire [10:0] copper_dispatch_submission_used =
        copper_dispatch_submission_producer - render_submission_consumer;
    wire copper_dispatch_host_conflict = write_execute_q &&
        awaddr_q[11:0] == 12'h208;
    // Replaying an ACTIVE copper list after its endpoint was already
    // published is an idempotent success, not a frame-to-frame fault.
    assign copper_dispatch_allowed = render_enable &&
        !render_configuration_fault &&
        copper_dispatch_advance <= 11'd1024 &&
        copper_dispatch_submission_used <= 11'd1024;
    assign copper_dispatch_ready = copper_dispatch_allowed &&
        !copper_dispatch_host_conflict && !render_queue_rebase;
    wire write_changes_scene =
        awaddr_q[11:0] == 12'h00c || awaddr_q[11:0] == 12'h018 ||
        (awaddr_q[11:0] >= 12'h040 && awaddr_q[11:0] <= 12'h058) ||
        (awaddr_q[11:0] >= 12'h080 && awaddr_q[11:0] <= 12'h098) ||
        (awaddr_q[11:0] >= 12'h0c0 && awaddr_q[11:0] <= 12'h0d8) ||
        awaddr_q[11:0] == 12'h180 || awaddr_q[11:0] == 12'h188 ||
        awaddr_q[11:0] == 12'h190;

    wire unused_protection = &{1'b0, s_axi_awprot, s_axi_arprot};

    function automatic [31:0] read_bank0(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank0 = DEVICE_ID;
                4'h1: read_bank0 = VERSION;
                4'h2: read_bank0 = CAPABILITIES;
                4'h3: read_bank0 = shadow_global_control;
                4'h4: read_bank0 = {generation_q[15:0], 13'd0,
                    shadow_scene_valid_status, active_global_control_q[0],
                    commit_pending_status};
                4'h5: read_bank0 = generation_q;
                4'h6: read_bank0 = shadow_backdrop;
                4'h7: read_bank0 = ARENA_BASE;
                4'h8: read_bank0 = ARENA_LIMIT;
                4'h9: read_bank0 = commit_errors;
                4'ha: read_bank0 = commit_deferrals;
                default: read_bank0 = 32'd0;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank1(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank1 = shadow_fb_base;
                4'h1: read_bank1 = shadow_fb_pitch;
                4'h2: read_bank1 = shadow_fb_size;
                4'h3: read_bank1 = shadow_fb_viewport_x;
                4'h4: read_bank1 = shadow_fb_viewport_y;
                4'h5: read_bank1 = shadow_fb_control;
                4'h6: read_bank1 = shadow_fb_key;
                default: read_bank1 = 32'd0;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank2(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank2 = shadow_tile0_map_base;
                4'h1: read_bank2 = shadow_tile0_pattern_base;
                4'h2: read_bank2 = shadow_tile0_scroll_x;
                4'h3: read_bank2 = shadow_tile0_scroll_y;
                4'h4: read_bank2 = shadow_tile0_geometry;
                4'h5: read_bank2 = shadow_tile0_count;
                4'h6: read_bank2 = shadow_tile0_control;
                default: read_bank2 = 32'd0;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank3(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank3 = shadow_tile1_map_base;
                4'h1: read_bank3 = shadow_tile1_pattern_base;
                4'h2: read_bank3 = shadow_tile1_scroll_x;
                4'h3: read_bank3 = shadow_tile1_scroll_y;
                4'h4: read_bank3 = shadow_tile1_geometry;
                4'h5: read_bank3 = shadow_tile1_count;
                4'h6: read_bank3 = shadow_tile1_control;
                default: read_bank3 = 32'd0;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank4(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank4 = framebuffer_palette_selector;
                4'h2: read_bank4 = tile_palette_selector;
                default: read_bank4 = 32'd0;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank5(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank5 = shadow_boot_text_control;
                4'h1: read_bank5 = {24'd0, boot_text_selector};
                4'h3: read_bank5 = {29'd0, boot_text_active_enable,
                    boot_text_commit_available, boot_text_write_available};
                4'h4: read_bank5 = boot_text_generation;
                4'h5: read_bank5 = {BOOT_TEXT_ROWS[7:0],
                    BOOT_TEXT_COLS[7:0], BOOT_TEXT_ROW_PITCH[7:0],
                    BOOT_TEXT_CELL_WIDTH[7:0]};
                4'h6: read_bank5 = {BOOT_TEXT_ORIGIN_Y[15:0],
                    BOOT_TEXT_ORIGIN_X[15:0]};
                default: read_bank5 = 32'd0;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank6(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank6 = shadow_sprite_control;
                4'h1: read_bank6 = {21'd0, sprite_descriptor_selector};
                4'h2: read_bank6 = 32'd0;
                4'h3: read_bank6 = {20'd0, sprite_palette_selector};
                4'h4: read_bank6 = 32'd0;
                4'h5: read_bank6 = {16'd0, sprite_collision_event,
                    sprite_activation_pending_q, sprite_clone_pending_q,
                    sprite_enable, sprite_slot_valid, sprite_deadline_error,
                    sprite_fetch_error, sprite_builder_busy,
                    sprite_activate_busy, sprite_pending_ready,
                    sprite_pending_valid, sprite_validate_busy,
                    sprite_scene_write_ready};
                4'h6: read_bank6 = sprite_build_cycles;
                4'h7: read_bank6 = sprite_read_bytes;
                4'h8: read_bank6 = sprite_pixels_admitted;
                4'h9: read_bank6 = sprite_pixels_dropped;
                4'ha: read_bank6 = sprite_overflow_bitmap[31:0];
                4'hb: read_bank6 = sprite_overflow_bitmap[63:32];
                4'hc: read_bank6 = {22'd0, sprite_overflow_line};
                4'hd: read_bank6 = sprite_overflow_count;
                4'he: read_bank6 = {26'd0, sprite_collision_read_row};
                4'hf: read_bank6 = sprite_collision_read_data[31:0];
            endcase
        end
    endfunction

    function automatic [31:0] read_bank7(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank7 = sprite_collision_read_data[63:32];
                4'h1: read_bank7 = sprite_collision_frame;
                4'h2: read_bank7 = sprite_axi_error_count;
                4'h3: read_bank7 = sprite_deadline_error_count;
                4'h4: read_bank7 = sprite_max_build_cycles;
                default: read_bank7 = 32'd0;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank8(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank8 = render_control_q;
                4'h1: read_bank8 = render_submission_ring_offset;
                4'h2: read_bank8 = {21'd0,
                    render_submission_producer};
                4'h3: read_bank8 = {21'd0,
                    render_submission_consumer};
                4'h4: read_bank8 = render_completion_ring_offset;
                4'h5: read_bank8 = {21'd0,
                    render_completion_producer};
                4'h6: read_bank8 = {21'd0,
                    render_completion_consumer};
                4'h7: read_bank8 = render_resource_generation;
                4'h8: read_bank8 = {27'd0, render_enable,
                    render_irq_pending_q, render_configuration_fault,
                    render_engine_reset_active, render_busy};
                4'h9: read_bank8 = render_retired_fence;
                4'ha: read_bank8 = render_last_fault_detail;
                4'hb: read_bank8 = render_commands_submitted;
                4'hc: read_bank8 = render_commands_completed;
                4'hd: read_bank8 = render_commands_failed;
                4'he: read_bank8 = render_backpressure_cycles;
                4'hf: read_bank8 = render_timeout_count;
            endcase
        end
    endfunction

    function automatic [31:0] read_bank9(input [3:0] word);
        begin
            case (word)
                4'h0: read_bank9 = render_reset_count;
                4'h1: read_bank9 = {31'd0, render_irq_pending_q};
                default: read_bank9 = 32'd0;
            endcase
        end
    endfunction

    task automatic capture_pending_scene;
        begin
            pending_global_control_q <= shadow_global_control;
            pending_backdrop_q <= shadow_backdrop;
            pending_fb_base_q <= shadow_fb_base;
            pending_fb_pitch_q <= shadow_fb_pitch;
            pending_fb_size_q <= shadow_fb_size;
            pending_fb_viewport_x_q <= shadow_fb_viewport_x;
            pending_fb_viewport_y_q <= shadow_fb_viewport_y;
            pending_fb_control_q <= shadow_fb_control;
            pending_fb_key_q <= shadow_fb_key;
            pending_fb_protection_valid_q <= shadow_global_control[0] &&
                shadow_fb_control[0] && framebuffer_validator_valid;
            pending_fb_protection_offset_q <= shadow_fb_base - ARENA_BASE;
            pending_fb_protection_bytes_q <=
                framebuffer_validator_surface_bytes;
            pending_tile0_map_base_q <= shadow_tile0_map_base;
            pending_tile0_pattern_base_q <= shadow_tile0_pattern_base;
            pending_tile0_scroll_x_q <= shadow_tile0_scroll_x;
            pending_tile0_scroll_y_q <= shadow_tile0_scroll_y;
            pending_tile0_geometry_q <= shadow_tile0_geometry;
            pending_tile0_count_q <= shadow_tile0_count;
            pending_tile0_control_q <= shadow_tile0_control;
            pending_tile1_map_base_q <= shadow_tile1_map_base;
            pending_tile1_pattern_base_q <= shadow_tile1_pattern_base;
            pending_tile1_scroll_x_q <= shadow_tile1_scroll_x;
            pending_tile1_scroll_y_q <= shadow_tile1_scroll_y;
            pending_tile1_geometry_q <= shadow_tile1_geometry;
            pending_tile1_count_q <= shadow_tile1_count;
            pending_tile1_control_q <= shadow_tile1_control;
            pending_sprite_control_q <= shadow_sprite_control;
        end
    endtask

    task automatic promote_pending_scene;
        begin
            active_global_control_q <= pending_global_control_q;
            active_backdrop_q <= pending_backdrop_q;
            active_fb_base_q <= pending_fb_base_q;
            active_fb_pitch_q <= pending_fb_pitch_q;
            active_fb_size_q <= pending_fb_size_q;
            active_fb_viewport_x_q <= pending_fb_viewport_x_q;
            active_fb_viewport_y_q <= pending_fb_viewport_y_q;
            active_fb_control_q <= pending_fb_control_q;
            active_fb_key_q <= pending_fb_key_q;
            active_fb_protection_valid_q <= pending_fb_protection_valid_q;
            active_fb_protection_offset_q <= pending_fb_protection_offset_q;
            active_fb_protection_bytes_q <= pending_fb_protection_bytes_q;
            active_tile0_map_base_q <= pending_tile0_map_base_q;
            active_tile0_pattern_base_q <= pending_tile0_pattern_base_q;
            active_tile0_scroll_x_q <= pending_tile0_scroll_x_q;
            active_tile0_scroll_y_q <= pending_tile0_scroll_y_q;
            active_tile0_geometry_q <= pending_tile0_geometry_q;
            active_tile0_count_q <= pending_tile0_count_q;
            active_tile0_control_q <= pending_tile0_control_q;
            active_tile1_map_base_q <= pending_tile1_map_base_q;
            active_tile1_pattern_base_q <= pending_tile1_pattern_base_q;
            active_tile1_scroll_x_q <= pending_tile1_scroll_x_q;
            active_tile1_scroll_y_q <= pending_tile1_scroll_y_q;
            active_tile1_geometry_q <= pending_tile1_geometry_q;
            active_tile1_count_q <= pending_tile1_count_q;
            active_tile1_control_q <= pending_tile1_control_q;
            active_sprite_control_q <= pending_sprite_control_q;
        end
    endtask

    always @(posedge clk) begin
        if (reset) begin
            aw_pending <= 1'b0;
            awaddr_q <= 32'd0;
            w_pending <= 1'b0;
            wdata_q <= 32'd0;
            wstrb_q <= 4'd0;
            write_execute_q <= 1'b0;
            write_render_busy_q <= 1'b0;
            write_prefix_valid_q <= 1'b0;
            ar_pending <= 1'b0;
            araddr_q <= 12'd0;
            read_decode_pending_q <= 1'b0;
            read_response_pending_q <= 1'b0;
            s_axi_bresp <= 2'b00;
            s_axi_bvalid <= 1'b0;
            s_axi_rdata <= 32'd0;
            s_axi_rresp <= 2'b00;
            s_axi_rvalid <= 1'b0;
            shadow_global_control <= 32'd0;
            shadow_backdrop <= 32'h00101820;
            shadow_fb_base <= ARENA_BASE;
            shadow_fb_pitch <= 32'd2560;
            shadow_fb_size <= {3'd0, 13'd720, 3'd0, 13'd1280};
            shadow_fb_viewport_x <= 32'd0;
            shadow_fb_viewport_y <= 32'd0;
            shadow_fb_control <= 32'h00000003;
            shadow_fb_key <= 32'd0;
            shadow_tile0_map_base <= ARENA_BASE + 32'h00400000;
            shadow_tile0_pattern_base <= ARENA_BASE + 32'h00420000;
            shadow_tile0_scroll_x <= 32'd0;
            shadow_tile0_scroll_y <= 32'd0;
            shadow_tile0_geometry <= 32'h00000e78;
            shadow_tile0_count <= 32'd1;
            shadow_tile0_control <= 32'h00ff0000;
            shadow_tile1_map_base <= ARENA_BASE + 32'h00440000;
            shadow_tile1_pattern_base <= ARENA_BASE + 32'h00460000;
            shadow_tile1_scroll_x <= 32'd0;
            shadow_tile1_scroll_y <= 32'd0;
            shadow_tile1_geometry <= 32'h00000e78;
            shadow_tile1_count <= 32'd1;
            shadow_tile1_control <= 32'h00ff0000;
            shadow_sprite_control <= 32'd0;
            pending_global_control_q <= 32'd0;
            pending_backdrop_q <= 32'h00101820;
            pending_fb_base_q <= ARENA_BASE;
            pending_fb_pitch_q <= 32'd2560;
            pending_fb_size_q <= {3'd0, 13'd720, 3'd0, 13'd1280};
            pending_fb_viewport_x_q <= 32'd0;
            pending_fb_viewport_y_q <= 32'd0;
            pending_fb_control_q <= 32'h00000003;
            pending_fb_key_q <= 32'd0;
            pending_fb_protection_valid_q <= 1'b0;
            pending_fb_protection_offset_q <= 32'd0;
            pending_fb_protection_bytes_q <= 32'd0;
            pending_tile0_map_base_q <= ARENA_BASE + 32'h00400000;
            pending_tile0_pattern_base_q <= ARENA_BASE + 32'h00420000;
            pending_tile0_scroll_x_q <= 32'd0;
            pending_tile0_scroll_y_q <= 32'd0;
            pending_tile0_geometry_q <= 32'h00000e78;
            pending_tile0_count_q <= 32'd1;
            pending_tile0_control_q <= 32'h00ff0000;
            pending_tile1_map_base_q <= ARENA_BASE + 32'h00440000;
            pending_tile1_pattern_base_q <= ARENA_BASE + 32'h00460000;
            pending_tile1_scroll_x_q <= 32'd0;
            pending_tile1_scroll_y_q <= 32'd0;
            pending_tile1_geometry_q <= 32'h00000e78;
            pending_tile1_count_q <= 32'd1;
            pending_tile1_control_q <= 32'h00ff0000;
            pending_sprite_control_q <= 32'd0;
            active_global_control_q <= 32'd0;
            active_backdrop_q <= 32'h00101820;
            active_fb_base_q <= ARENA_BASE;
            active_fb_pitch_q <= 32'd2560;
            active_fb_size_q <= {3'd0, 13'd720, 3'd0, 13'd1280};
            active_fb_viewport_x_q <= 32'd0;
            active_fb_viewport_y_q <= 32'd0;
            active_fb_control_q <= 32'h00000003;
            active_fb_key_q <= 32'd0;
            active_fb_protection_valid_q <= 1'b0;
            active_fb_protection_offset_q <= 32'd0;
            active_fb_protection_bytes_q <= 32'd0;
            active_tile0_map_base_q <= ARENA_BASE + 32'h00400000;
            active_tile0_pattern_base_q <= ARENA_BASE + 32'h00420000;
            active_tile0_scroll_x_q <= 32'd0;
            active_tile0_scroll_y_q <= 32'd0;
            active_tile0_geometry_q <= 32'h00000e78;
            active_tile0_count_q <= 32'd1;
            active_tile0_control_q <= 32'h00ff0000;
            active_tile1_map_base_q <= ARENA_BASE + 32'h00440000;
            active_tile1_pattern_base_q <= ARENA_BASE + 32'h00460000;
            active_tile1_scroll_x_q <= 32'd0;
            active_tile1_scroll_y_q <= 32'd0;
            active_tile1_geometry_q <= 32'h00000e78;
            active_tile1_count_q <= 32'd1;
            active_tile1_control_q <= 32'h00ff0000;
            active_sprite_control_q <= 32'd0;
            generation_q <= 32'd0;
            commit_pending_q <= 1'b0;
            commit_quiesce <= 1'b0;
            commit_validation_pending_q <= 1'b0;
            validator_start_q <= 1'b0;
            shadow_config_dirty_q <= 1'b0;
            shadow_scene_valid_q <= 1'b1;
            framebuffer_validator_done_seen_q <= 1'b0;
            framebuffer_validator_result_q <= 1'b0;
            tile0_validator_done_seen_q <= 1'b0;
            tile0_validator_result_q <= 1'b0;
            tile1_validator_done_seen_q <= 1'b0;
            tile1_validator_result_q <= 1'b0;
            sprite_validator_done_seen_q <= 1'b0;
            sprite_validator_result_q <= 1'b0;
            sprite_clone_pending_q <= 1'b0;
            sprite_activation_pending_q <= 1'b0;
            scene_changed <= 1'b0;
            commit_errors <= 32'd0;
            commit_deferrals <= 32'd0;
            framebuffer_palette_selector <= 32'd0;
            tile_palette_selector <= 32'd0;
            framebuffer_palette_write_enable <= 1'b0;
            framebuffer_palette_write_index <= 8'd0;
            framebuffer_palette_write_argb <= 32'd0;
            tile_palette_write_enable <= 1'b0;
            tile_palette_write_bank <= 4'd0;
            tile_palette_write_index <= 8'd0;
            tile_palette_write_argb <= 32'd0;
            sprite_descriptor_selector <= 11'd0;
            sprite_palette_selector <= 12'd0;
            sprite_descriptor_write_enable <= 1'b0;
            sprite_descriptor_write_index <= 6'd0;
            sprite_descriptor_write_word <= 3'd0;
            sprite_descriptor_write_data <= 32'd0;
            sprite_palette_write_enable <= 1'b0;
            sprite_palette_write_bank <= 4'd0;
            sprite_palette_write_index <= 8'd0;
            sprite_palette_write_argb <= 32'd0;
            sprite_validate_start <= 1'b0;
            sprite_accept_pending <= 1'b0;
            sprite_activate_start <= 1'b0;
            sprite_collision_read_row <= 6'd0;
            shadow_boot_text_control <= 32'd0;
            boot_text_selector <= 8'd0;
            boot_text_write_enable <= 1'b0;
            boot_text_write_index <= 8'd0;
            boot_text_write_cell <= 16'h0020;
            boot_text_commit_enable <= 1'b0;
            boot_text_write_busy_q <= 1'b0;
            boot_text_write_busy_seen_q <= 1'b0;
            boot_text_commit_busy_q <= 1'b0;
            boot_text_commit_busy_seen_q <= 1'b0;
            render_control_q <= 32'd0;
            render_queue_rebase <= 1'b0;
            render_soft_reset <= 1'b0;

            render_submission_ring_offset <= 32'd0;
            render_submission_producer <= 11'd0;
            render_completion_ring_offset <= 32'h00010000;
            render_completion_consumer <= 11'd0;
            render_resource_generation <= 32'd0;
            render_irq_pending_q <= 1'b0;
        end else begin
            scene_changed <= 1'b0;
            validator_start_q <= 1'b0;
            framebuffer_palette_write_enable <= 1'b0;
            tile_palette_write_enable <= 1'b0;
            sprite_descriptor_write_enable <= 1'b0;
            sprite_palette_write_enable <= 1'b0;
            sprite_validate_start <= 1'b0;
            sprite_accept_pending <= 1'b0;
            sprite_activate_start <= 1'b0;
            boot_text_write_enable <= 1'b0;
            boot_text_commit_enable <= 1'b0;
            render_queue_rebase <= 1'b0;
            render_soft_reset <= 1'b0;

            if (copper_dispatch_valid && copper_dispatch_ready)
                render_submission_producer <=
                    copper_dispatch_submission_producer;

            if (boot_text_write_busy_q) begin
                if (!boot_text_write_ready)
                    boot_text_write_busy_seen_q <= 1'b1;
                if (boot_text_write_busy_seen_q &&
                    boot_text_write_ready) begin
                    boot_text_write_busy_q <= 1'b0;
                    boot_text_write_busy_seen_q <= 1'b0;
                end
            end
            if (boot_text_commit_busy_q) begin
                if (!boot_text_commit_ready)
                    boot_text_commit_busy_seen_q <= 1'b1;
                if (boot_text_commit_busy_seen_q &&
                    boot_text_commit_ready) begin
                    boot_text_commit_busy_q <= 1'b0;
                    boot_text_commit_busy_seen_q <= 1'b0;
                end
            end

            if (s_axi_awvalid && s_axi_awready) begin
                aw_pending <= 1'b1;
                awaddr_q <= s_axi_awaddr;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_pending <= 1'b1;
                wdata_q <= s_axi_wdata;
                wstrb_q <= s_axi_wstrb;
            end
            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;

            if (write_fire) begin
                aw_pending <= 1'b0;
                w_pending <= 1'b0;
                write_execute_q <= 1'b1;
                write_render_busy_q <= render_busy;
                write_alignment_valid_q <= awaddr_q[1:0] == 2'b00;
                write_prefix_valid_q <= awaddr_q[11:10] == 2'b00;
                write_full_strobe_q <= wstrb_q == 4'hf;
                write_commit_request_valid_q <= wstrb_q[0] && wdata_q[0];
                write_boot_control_valid_q <= wstrb_q == 4'hf &&
                    (wdata_q & 32'hfffffffe) == 32'd0;
                write_boot_selector_valid_q <= wstrb_q == 4'hf &&
                    wdata_q < BOOT_TEXT_CELLS;
                write_boot_cell_valid_q <= wstrb_q == 4'hf &&
                    wdata_q[31:10] == 22'd0;
                write_boot_commit_valid_q <= wstrb_q == 4'hf &&
                    wdata_q == 32'd1;
                write_sprite_control_valid_q <= wstrb_q == 4'hf &&
                    (wdata_q & 32'hfffffffe) == 32'd0;
                write_sprite_descriptor_selector_valid_q <=
                    wstrb_q == 4'hf &&
                    (wdata_q & 32'hfffff8c0) == 32'd0;
                write_sprite_palette_selector_valid_q <=
                    wstrb_q == 4'hf &&
                    (wdata_q & 32'hfffff000) == 32'd0;
                write_collision_row_valid_q <= wstrb_q == 4'hf &&
                    (wdata_q & 32'hffffffc0) == 32'd0;
            end

            if (write_execute_q) begin
                write_execute_q <= 1'b0;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp <= 2'b00;
                if (!write_alignment_valid_q || !write_prefix_valid_q) begin
                    s_axi_bresp <= 2'b11;
                end else begin
                    case (awaddr_q[9:0])
                        12'h00c: shadow_global_control <= merge_write(
                            shadow_global_control, wdata_q, wstrb_q);
                        12'h010: begin
                            if (!write_commit_request_valid_q) begin
                                s_axi_bresp <= 2'b10;
                            end else if (commit_pending_q ||
                                sprite_pending_valid ||
                                sprite_activation_pending_q ||
                                !sprite_scene_write_ready) begin
                                s_axi_bresp <= 2'b10;
                                commit_errors <= commit_errors + 32'd1;
                            end else begin
                                // Hold the response through validation and
                                // cloning. The acknowledged PENDING snapshot
                                // is immutable even if EDITABLE changes later.
                                s_axi_bvalid <= 1'b0;
                                commit_validation_pending_q <= 1'b1;
                                validator_start_q <= 1'b1;
                                sprite_validate_start <= 1'b1;
                                framebuffer_validator_done_seen_q <= 1'b0;
                                tile0_validator_done_seen_q <= 1'b0;
                                tile1_validator_done_seen_q <= 1'b0;
                                sprite_validator_done_seen_q <= 1'b0;
                            end
                        end
                        12'h018: shadow_backdrop <= merge_write(
                            shadow_backdrop, wdata_q, wstrb_q);
                        12'h040: shadow_fb_base <= merge_write(
                            shadow_fb_base, wdata_q, wstrb_q);
                        12'h044: shadow_fb_pitch <= merge_write(
                            shadow_fb_pitch, wdata_q, wstrb_q);
                        12'h048: shadow_fb_size <= merge_write(
                            shadow_fb_size, wdata_q, wstrb_q);
                        12'h04c: shadow_fb_viewport_x <= merge_write(
                            shadow_fb_viewport_x, wdata_q, wstrb_q);
                        12'h050: shadow_fb_viewport_y <= merge_write(
                            shadow_fb_viewport_y, wdata_q, wstrb_q);
                        12'h054: shadow_fb_control <= merge_write(
                            shadow_fb_control, wdata_q, wstrb_q);
                        12'h058: shadow_fb_key <= merge_write(
                            shadow_fb_key, wdata_q, wstrb_q);
                        12'h080: shadow_tile0_map_base <= merge_write(
                            shadow_tile0_map_base, wdata_q, wstrb_q);
                        12'h084: shadow_tile0_pattern_base <= merge_write(
                            shadow_tile0_pattern_base, wdata_q, wstrb_q);
                        12'h088: shadow_tile0_scroll_x <= merge_write(
                            shadow_tile0_scroll_x, wdata_q, wstrb_q);
                        12'h08c: shadow_tile0_scroll_y <= merge_write(
                            shadow_tile0_scroll_y, wdata_q, wstrb_q);
                        12'h090: shadow_tile0_geometry <= merge_write(
                            shadow_tile0_geometry, wdata_q, wstrb_q);
                        12'h094: shadow_tile0_count <= merge_write(
                            shadow_tile0_count, wdata_q, wstrb_q);
                        12'h098: shadow_tile0_control <= merge_write(
                            shadow_tile0_control, wdata_q, wstrb_q);
                        12'h0c0: shadow_tile1_map_base <= merge_write(
                            shadow_tile1_map_base, wdata_q, wstrb_q);
                        12'h0c4: shadow_tile1_pattern_base <= merge_write(
                            shadow_tile1_pattern_base, wdata_q, wstrb_q);
                        12'h0c8: shadow_tile1_scroll_x <= merge_write(
                            shadow_tile1_scroll_x, wdata_q, wstrb_q);
                        12'h0cc: shadow_tile1_scroll_y <= merge_write(
                            shadow_tile1_scroll_y, wdata_q, wstrb_q);
                        12'h0d0: shadow_tile1_geometry <= merge_write(
                            shadow_tile1_geometry, wdata_q, wstrb_q);
                        12'h0d4: shadow_tile1_count <= merge_write(
                            shadow_tile1_count, wdata_q, wstrb_q);
                        12'h0d8: shadow_tile1_control <= merge_write(
                            shadow_tile1_control, wdata_q, wstrb_q);
                        12'h100: framebuffer_palette_selector <= merge_write(
                            framebuffer_palette_selector, wdata_q, wstrb_q);
                        12'h104: begin
                            if (active_global_control_q[0] ||
                                commit_pending_q || !write_full_strobe_q ||
                                !palette_write_ready) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                framebuffer_palette_write_enable <= 1'b1;
                                framebuffer_palette_write_index <=
                                    framebuffer_palette_selector[7:0];
                                framebuffer_palette_write_argb <= wdata_q;
                            end
                        end
                        12'h108: tile_palette_selector <= merge_write(
                            tile_palette_selector, wdata_q, wstrb_q);
                        12'h10c: begin
                            if (active_global_control_q[0] ||
                                commit_pending_q || !write_full_strobe_q ||
                                !palette_write_ready) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                tile_palette_write_enable <= 1'b1;
                                tile_palette_write_bank <=
                                    tile_palette_selector[11:8];
                                tile_palette_write_index <=
                                    tile_palette_selector[7:0];
                                tile_palette_write_argb <= wdata_q;
                            end
                        end
                        12'h140: begin
                            if (!write_boot_control_valid_q ||
                                !boot_text_commit_available) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                shadow_boot_text_control <= wdata_q;
                            end
                        end
                        12'h144: begin
                            if (!write_boot_selector_valid_q ||
                                !boot_text_write_available) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                boot_text_selector <= wdata_q[7:0];
                            end
                        end
                        12'h148: begin
                            if (!write_boot_cell_valid_q ||
                                !boot_text_write_available) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                boot_text_write_enable <= 1'b1;
                                boot_text_write_index <=
                                    boot_text_selector;
                                boot_text_write_cell <= wdata_q[15:0];
                                boot_text_write_busy_q <= 1'b1;
                                boot_text_write_busy_seen_q <= 1'b0;
                                if (boot_text_selector <
                                    BOOT_TEXT_CELLS - 1)
                                    boot_text_selector <=
                                        boot_text_selector + 8'd1;
                            end
                        end
                        12'h14c: begin
                            if (!write_boot_commit_valid_q ||
                                !boot_text_commit_available) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                boot_text_commit_enable <= 1'b1;
                                boot_text_commit_busy_q <= 1'b1;
                                boot_text_commit_busy_seen_q <= 1'b0;
                            end
                        end
                        12'h180: begin
                            if (!write_sprite_control_valid_q)
                                s_axi_bresp <= 2'b10;
                            else
                                shadow_sprite_control <= wdata_q;
                        end
                        12'h184: begin
                            if (!write_sprite_descriptor_selector_valid_q)
                                s_axi_bresp <= 2'b10;
                            else
                                sprite_descriptor_selector <= wdata_q[10:0];
                        end
                        12'h188: begin
                            if (!write_full_strobe_q ||
                                !sprite_scene_write_ready) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                sprite_descriptor_write_enable <= 1'b1;
                                sprite_descriptor_write_index <=
                                    sprite_descriptor_selector[5:0];
                                sprite_descriptor_write_word <=
                                    sprite_descriptor_selector[10:8];
                                sprite_descriptor_write_data <= wdata_q;
                            end
                        end
                        12'h18c: begin
                            if (!write_sprite_palette_selector_valid_q)
                                s_axi_bresp <= 2'b10;
                            else
                                sprite_palette_selector <= wdata_q[11:0];
                        end
                        12'h190: begin
                            if (!write_full_strobe_q ||
                                !sprite_scene_write_ready) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                sprite_palette_write_enable <= 1'b1;
                                sprite_palette_write_bank <=
                                    sprite_palette_selector[11:8];
                                sprite_palette_write_index <=
                                    sprite_palette_selector[7:0];
                                sprite_palette_write_argb <= wdata_q;
                            end
                        end
                        12'h1b8: begin
                            if (!write_collision_row_valid_q)
                                s_axi_bresp <= 2'b10;
                            else
                                sprite_collision_read_row <= wdata_q[5:0];
                        end
                        12'h200: begin
                            if (!write_full_strobe_q ||
                                wdata_q[31:3] != 29'd0 ||
                                (wdata_q[1] && render_busy)) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                render_control_q <= {31'd0, wdata_q[0]};
                                if (wdata_q[1])
                                    render_queue_rebase <= 1'b1;
                                if (wdata_q[2])
                                    render_soft_reset <= 1'b1;
                            end
                        end
                        12'h204: begin
                            if (!write_full_strobe_q || render_enable ||
                                write_render_busy_q)
                                s_axi_bresp <= 2'b10;
                            else
                                render_submission_ring_offset <= wdata_q;
                        end
                        12'h208: begin
                            if (!write_full_strobe_q ||
                                wdata_q[31:11] != 21'd0)
                                s_axi_bresp <= 2'b10;
                            else
                                render_submission_producer <=
                                    wdata_q[10:0];
                        end
                        12'h210: begin
                            if (!write_full_strobe_q || render_enable ||
                                write_render_busy_q)
                                s_axi_bresp <= 2'b10;
                            else
                                render_completion_ring_offset <= wdata_q;
                        end
                        12'h218: begin
                            if (!write_full_strobe_q ||
                                wdata_q[31:11] != 21'd0)
                                s_axi_bresp <= 2'b10;
                            else
                                render_completion_consumer <=
                                    wdata_q[10:0];
                        end
                        12'h21c: begin
                            if (!write_full_strobe_q || render_enable ||
                                write_render_busy_q || wdata_q == 32'd0)
                                s_axi_bresp <= 2'b10;
                            else
                                render_resource_generation <= wdata_q;
                        end
                        12'h244: begin
                            if (!write_full_strobe_q ||
                                wdata_q[31:1] != 31'd0)
                                s_axi_bresp <= 2'b10;
                            else if (wdata_q[0])
                                render_irq_pending_q <= 1'b0;
                        end
                        default: s_axi_bresp <= 2'b11;
                    endcase
                    if (write_changes_scene)
                        shadow_config_dirty_q <= 1'b1;
                end
            end

            if (commit_validation_pending_q &&
                framebuffer_validator_done) begin
                framebuffer_validator_done_seen_q <= 1'b1;
                framebuffer_validator_result_q <=
                    framebuffer_validator_valid;
            end
            if (commit_validation_pending_q && tile0_validator_done) begin
                tile0_validator_done_seen_q <= 1'b1;
                tile0_validator_result_q <= tile0_validator_valid;
            end
            if (commit_validation_pending_q && tile1_validator_done) begin
                tile1_validator_done_seen_q <= 1'b1;
                tile1_validator_result_q <= tile1_validator_valid;
            end
            if (commit_validation_pending_q && sprite_validate_done) begin
                sprite_validator_done_seen_q <= 1'b1;
                sprite_validator_result_q <= sprite_validate_valid;
            end

            if (commit_validation_pending_q &&
                !sprite_clone_pending_q && validators_done) begin
                if (validated_scene_valid) begin
                    sprite_accept_pending <= 1'b1;
                    sprite_clone_pending_q <= 1'b1;
                end else begin
                    commit_validation_pending_q <= 1'b0;
                    shadow_config_dirty_q <= 1'b0;
                    shadow_scene_valid_q <= 1'b0;
                    s_axi_bvalid <= 1'b1;
                    s_axi_bresp <= 2'b10;
                    commit_errors <= commit_errors + 32'd1;
                end
            end

            if (commit_validation_pending_q && sprite_clone_pending_q &&
                sprite_pending_ready) begin
                capture_pending_scene();
                sprite_clone_pending_q <= 1'b0;
                commit_validation_pending_q <= 1'b0;
                shadow_config_dirty_q <= 1'b0;
                shadow_scene_valid_q <= 1'b1;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp <= 2'b00;
                commit_pending_q <= 1'b1;
            end

            if (frame_boundary && commit_pending_q) begin
                if (commit_quiesce)
                    commit_deferrals <= commit_deferrals + 32'd1;
                commit_quiesce <= 1'b1;
            end

            if (commit_quiesce && commit_pending_q &&
                !sprite_activation_pending_q && commit_safe &&
                sprite_pending_ready) begin
                sprite_activate_start <= 1'b1;
                sprite_activation_pending_q <= 1'b1;
            end

            if (sprite_activation_pending_q && sprite_activate_done) begin
                promote_pending_scene();
                generation_q <= generation_q + 32'd1;
                commit_pending_q <= 1'b0;
                commit_quiesce <= 1'b0;
                sprite_activation_pending_q <= 1'b0;
                scene_changed <= 1'b1;
            end

            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
            if (render_completion_irq)
                render_irq_pending_q <= 1'b1;
            if (s_axi_arvalid && s_axi_arready) begin
                ar_pending <= 1'b1;
                araddr_q <= s_axi_araddr[11:0];
            end
            if (ar_pending) begin
                ar_pending <= 1'b0;
                read_decode_pending_q <= 1'b1;
                read_bank_q <= araddr_q[9:6];
                read_prefix_valid_q <= araddr_q[11:10] == 2'd0 &&
                    araddr_q[9:6] <= 4'd9 && araddr_q[1:0] == 2'b00;
                read_bank_word_q[0] <= araddr_q[5:2];
                read_bank_word_q[1] <= araddr_q[5:2];
                read_bank_word_q[2] <= araddr_q[5:2];
                read_bank_word_q[3] <= araddr_q[5:2];
                read_bank_word_q[4] <= araddr_q[5:2];
                read_bank_word_q[5] <= araddr_q[5:2];
                read_bank_word_q[6] <= araddr_q[5:2];
                read_bank_word_q[7] <= araddr_q[5:2];
                read_bank_word_q[8] <= araddr_q[5:2];
                read_bank_word_q[9] <= araddr_q[5:2];
            end
            if (read_decode_pending_q) begin
                read_decode_pending_q <= 1'b0;
                read_response_pending_q <= 1'b1;
                read_bank_data_q[0] <= read_bank0(read_bank_word_q[0]);
                read_bank_data_q[1] <= read_bank1(read_bank_word_q[1]);
                read_bank_data_q[2] <= read_bank2(read_bank_word_q[2]);
                read_bank_data_q[3] <= read_bank3(read_bank_word_q[3]);
                read_bank_data_q[4] <= read_bank4(read_bank_word_q[4]);
                read_bank_data_q[5] <= read_bank5(read_bank_word_q[5]);
                read_bank_data_q[6] <= read_bank6(read_bank_word_q[6]);
                read_bank_data_q[7] <= read_bank7(read_bank_word_q[7]);
                read_bank_data_q[8] <= read_bank8(read_bank_word_q[8]);
                read_bank_data_q[9] <= read_bank9(read_bank_word_q[9]);
                read_bank_valid_q[0] <= read_bank_word_q[0] <= 4'ha;
                read_bank_valid_q[1] <= read_bank_word_q[1] <= 4'h6;
                read_bank_valid_q[2] <= read_bank_word_q[2] <= 4'h6;
                read_bank_valid_q[3] <= read_bank_word_q[3] <= 4'h6;
                read_bank_valid_q[4] <= read_bank_word_q[4] == 4'h0 ||
                    read_bank_word_q[4] == 4'h2;
                read_bank_valid_q[5] <= read_bank_word_q[5] == 4'h0 ||
                    read_bank_word_q[5] == 4'h1 ||
                    (read_bank_word_q[5] >= 4'h3 &&
                     read_bank_word_q[5] <= 4'h6);
                read_bank_valid_q[6] <= 1'b1;
                read_bank_valid_q[7] <= read_bank_word_q[7] <= 4'h4;
                read_bank_valid_q[8] <= 1'b1;
                read_bank_valid_q[9] <= read_bank_word_q[9] <= 4'h1;
            end
            if (read_response_pending_q) begin
                read_response_pending_q <= 1'b0;
                s_axi_rvalid <= 1'b1;
                if (!read_prefix_valid_q) begin
                    s_axi_rdata <= 32'd0;
                    s_axi_rresp <= 2'b11;
                end else begin
                    case (read_bank_q)
                        3'd0: begin
                            s_axi_rdata <= read_bank_data_q[0];
                            s_axi_rresp <= read_bank_valid_q[0] ?
                                2'b00 : 2'b11;
                        end
                        3'd1: begin
                            s_axi_rdata <= read_bank_data_q[1];
                            s_axi_rresp <= read_bank_valid_q[1] ?
                                2'b00 : 2'b11;
                        end
                        3'd2: begin
                            s_axi_rdata <= read_bank_data_q[2];
                            s_axi_rresp <= read_bank_valid_q[2] ?
                                2'b00 : 2'b11;
                        end
                        3'd3: begin
                            s_axi_rdata <= read_bank_data_q[3];
                            s_axi_rresp <= read_bank_valid_q[3] ?
                                2'b00 : 2'b11;
                        end
                        3'd4: begin
                            s_axi_rdata <= read_bank_data_q[4];
                            s_axi_rresp <= read_bank_valid_q[4] ?
                                2'b00 : 2'b11;
                        end
                        3'd5: begin
                            s_axi_rdata <= read_bank_data_q[5];
                            s_axi_rresp <= read_bank_valid_q[5] ?
                                2'b00 : 2'b11;
                        end
                        3'd6: begin
                            s_axi_rdata <= read_bank_data_q[6];
                            s_axi_rresp <= read_bank_valid_q[6] ?
                                2'b00 : 2'b11;
                        end
                        4'd7: begin
                            s_axi_rdata <= read_bank_data_q[7];
                            s_axi_rresp <= read_bank_valid_q[7] ?
                                2'b00 : 2'b11;
                        end
                        4'd8: begin
                            s_axi_rdata <= read_bank_data_q[8];
                            s_axi_rresp <= read_bank_valid_q[8] ?
                                2'b00 : 2'b11;
                        end
                        default: begin
                            s_axi_rdata <= read_bank_data_q[9];
                            s_axi_rresp <= read_bank_valid_q[9] ?
                                2'b00 : 2'b11;
                        end
                    endcase
                end
            end
        end
    end
endmodule

`default_nettype wire
