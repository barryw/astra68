// Copyright (c) 2026 Astra68 contributors
//
// First production Arty graphics pipeline: frame-safe control, four-line
// scheduling, independent DDR readers, dual-clock line stores, palettes, and
// the fixed-order framebuffer/tile compositor.
`timescale 1ns/1ps
`default_nettype none

module astra_graphics_pipeline #(
    parameter [31:0] ARENA_BASE = 32'h18000000,
    parameter [31:0] ARENA_LIMIT = 32'h20000000,
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720,
    parameter integer TOTAL_WIDTH = 1650,
    parameter integer TOTAL_HEIGHT = 750,
    parameter integer OUTPUT_PREFETCH = 37,
    parameter integer AXI_ID_WIDTH = 6,
    parameter BOOT_FONT_HEX = "post_fonts.hex",
    parameter integer BOOT_TEXT_COLS = 36,
    parameter integer BOOT_TEXT_ROWS = 4,
    parameter integer BOOT_TEXT_ORIGIN_X = 264,
    parameter integer BOOT_TEXT_ORIGIN_Y = 496,
    parameter integer BOOT_TEXT_CELL_WIDTH = 16,
    parameter integer BOOT_TEXT_ROW_PITCH = 32
) (
    input  wire                         build_clk,
    input  wire                         build_reset,
    input  wire                         pixel_clk,
    input  wire                         pixel_reset,
    input  wire [10:0]                  pixel_x,
    input  wire [9:0]                   pixel_y,
    output wire                         pixel_output_valid,
    output wire [23:0]                  pixel_output_rgb,

    output wire [31:0]                  active_generation,
    output wire [31:0]                  lines_built,
    output wire [31:0]                  lines_failed,
    output wire [31:0]                  scheduler_overruns,
    output wire [31:0]                  pixel_underruns,
    output wire [31:0]                  commit_errors,
    output wire [31:0]                  commit_deferrals,
    output wire                         scene_active,
    output wire                         render_interrupt,

    input  wire [31:0]                  s_axi_awaddr,
    input  wire [2:0]                   s_axi_awprot,
    input  wire                         s_axi_awvalid,
    output wire                         s_axi_awready,
    input  wire [31:0]                  s_axi_wdata,
    input  wire [3:0]                   s_axi_wstrb,
    input  wire                         s_axi_wvalid,
    output wire                         s_axi_wready,
    output wire [1:0]                   s_axi_bresp,
    output wire                         s_axi_bvalid,
    input  wire                         s_axi_bready,
    input  wire [31:0]                  s_axi_araddr,
    input  wire [2:0]                   s_axi_arprot,
    input  wire                         s_axi_arvalid,
    output wire                         s_axi_arready,
    output wire [31:0]                  s_axi_rdata,
    output wire [1:0]                   s_axi_rresp,
    output wire                         s_axi_rvalid,
    input  wire                         s_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      fb_axi_arid,
    output wire [31:0]                  fb_axi_araddr,
    output wire [7:0]                   fb_axi_arlen,
    output wire [2:0]                   fb_axi_arsize,
    output wire [1:0]                   fb_axi_arburst,
    output wire [3:0]                   fb_axi_arcache,
    output wire [2:0]                   fb_axi_arprot,
    output wire [3:0]                   fb_axi_arqos,
    output wire                         fb_axi_arvalid,
    input  wire                         fb_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      fb_axi_rid,
    input  wire [63:0]                  fb_axi_rdata,
    input  wire [1:0]                   fb_axi_rresp,
    input  wire                         fb_axi_rlast,
    input  wire                         fb_axi_rvalid,
    output wire                         fb_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      tile0_axi_arid,
    output wire [31:0]                  tile0_axi_araddr,
    output wire [7:0]                   tile0_axi_arlen,
    output wire [2:0]                   tile0_axi_arsize,
    output wire [1:0]                   tile0_axi_arburst,
    output wire [3:0]                   tile0_axi_arcache,
    output wire [2:0]                   tile0_axi_arprot,
    output wire [3:0]                   tile0_axi_arqos,
    output wire                         tile0_axi_arvalid,
    input  wire                         tile0_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      tile0_axi_rid,
    input  wire [63:0]                  tile0_axi_rdata,
    input  wire [1:0]                   tile0_axi_rresp,
    input  wire                         tile0_axi_rlast,
    input  wire                         tile0_axi_rvalid,
    output wire                         tile0_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      tile1_axi_arid,
    output wire [31:0]                  tile1_axi_araddr,
    output wire [7:0]                   tile1_axi_arlen,
    output wire [2:0]                   tile1_axi_arsize,
    output wire [1:0]                   tile1_axi_arburst,
    output wire [3:0]                   tile1_axi_arcache,
    output wire [2:0]                   tile1_axi_arprot,
    output wire [3:0]                   tile1_axi_arqos,
    output wire                         tile1_axi_arvalid,
    input  wire                         tile1_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      tile1_axi_rid,
    input  wire [63:0]                  tile1_axi_rdata,
    input  wire [1:0]                   tile1_axi_rresp,
    input  wire                         tile1_axi_rlast,
    input  wire                         tile1_axi_rvalid,
    output wire                         tile1_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      sprite_axi_arid,
    output wire [31:0]                  sprite_axi_araddr,
    output wire [7:0]                   sprite_axi_arlen,
    output wire [2:0]                   sprite_axi_arsize,
    output wire [1:0]                   sprite_axi_arburst,
    output wire [3:0]                   sprite_axi_arcache,
    output wire [2:0]                   sprite_axi_arprot,
    output wire [3:0]                   sprite_axi_arqos,
    output wire                         sprite_axi_arvalid,
    input  wire                         sprite_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      sprite_axi_rid,
    input  wire [63:0]                  sprite_axi_rdata,
    input  wire [1:0]                   sprite_axi_rresp,
    input  wire                         sprite_axi_rlast,
    input  wire                         sprite_axi_rvalid,
    output wire                         sprite_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      render_axi_arid,
    output wire [31:0]                  render_axi_araddr,
    output wire [7:0]                   render_axi_arlen,
    output wire [2:0]                   render_axi_arsize,
    output wire [1:0]                   render_axi_arburst,
    output wire [3:0]                   render_axi_arcache,
    output wire [2:0]                   render_axi_arprot,
    output wire [3:0]                   render_axi_arqos,
    output wire                         render_axi_arvalid,
    input  wire                         render_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      render_axi_rid,
    input  wire [63:0]                  render_axi_rdata,
    input  wire [1:0]                   render_axi_rresp,
    input  wire                         render_axi_rlast,
    input  wire                         render_axi_rvalid,
    output wire                         render_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      render_axi_awid,
    output wire [31:0]                  render_axi_awaddr,
    output wire [7:0]                   render_axi_awlen,
    output wire [2:0]                   render_axi_awsize,
    output wire [1:0]                   render_axi_awburst,
    output wire [3:0]                   render_axi_awcache,
    output wire [2:0]                   render_axi_awprot,
    output wire [3:0]                   render_axi_awqos,
    output wire                         render_axi_awvalid,
    input  wire                         render_axi_awready,
    output wire [63:0]                  render_axi_wdata,
    output wire [7:0]                   render_axi_wstrb,
    output wire                         render_axi_wlast,
    output wire                         render_axi_wvalid,
    input  wire                         render_axi_wready,
    input  wire [AXI_ID_WIDTH-1:0]      render_axi_bid,
    input  wire [1:0]                   render_axi_bresp,
    input  wire                         render_axi_bvalid,
    output wire                         render_axi_bready
);
    initial begin
        if (OUTPUT_PREFETCH >= OUTPUT_WIDTH)
            $error("OUTPUT_PREFETCH must be smaller than OUTPUT_WIDTH");
        if (OUTPUT_PREFETCH > TOTAL_WIDTH - OUTPUT_WIDTH)
            $error("horizontal blanking must cover OUTPUT_PREFETCH cycles");
    end

    wire frame_boundary_build;
    wire commit_quiesce;
    reg frame_toggle_pixel;
    (* ASYNC_REG = "TRUE" *) reg frame_toggle_meta;
    (* ASYNC_REG = "TRUE" *) reg frame_toggle_sync;
    reg frame_toggle_seen;

    always @(posedge pixel_clk) begin
        if (pixel_reset)
            frame_toggle_pixel <= 1'b0;
        else if (pixel_x == 11'd0 && pixel_y == OUTPUT_HEIGHT)
            frame_toggle_pixel <= ~frame_toggle_pixel;
    end

    always @(posedge build_clk) begin
        if (build_reset) begin
            frame_toggle_meta <= 1'b0;
            frame_toggle_sync <= 1'b0;
            frame_toggle_seen <= 1'b0;
        end else begin
            frame_toggle_meta <= frame_toggle_pixel;
            frame_toggle_sync <= frame_toggle_meta;
            frame_toggle_seen <= frame_toggle_sync;
        end
    end
    assign frame_boundary_build = frame_toggle_sync != frame_toggle_seen;

    wire scene_changed;
    reg scene_changed_q;
    reg scene_changed_qq;
    wire commit_pending_status;
    wire scheduler_idle;
    wire framebuffer_busy;
    wire tile0_busy;
    wire tile1_busy;
    wire sprite_busy;
    wire commit_safe = scheduler_idle && !framebuffer_busy &&
                       !tile0_busy && !tile1_busy && !sprite_busy;

    wire scene_enable_build;
    wire scene_enable_baseline;
    wire [23:0] backdrop_rgb_build;
    wire [23:0] backdrop_rgb_baseline;
    wire framebuffer_enable_build;
    wire [1:0] framebuffer_format_build;
    wire [31:0] framebuffer_base_build;
    wire [31:0] framebuffer_pitch_build;
    wire [12:0] framebuffer_width_build;
    wire [12:0] framebuffer_height_build;
    wire framebuffer_enable_baseline;
    wire [1:0] framebuffer_format_baseline;
    wire [31:0] framebuffer_base_baseline;
    wire [31:0] framebuffer_pitch_baseline;
    wire [12:0] framebuffer_width_baseline;
    wire [12:0] framebuffer_height_baseline;
    wire signed [31:0] framebuffer_viewport_x_build;
    wire signed [31:0] framebuffer_viewport_y_build;
    wire signed [31:0] framebuffer_viewport_x_baseline;
    wire signed [31:0] framebuffer_viewport_y_baseline;
    wire framebuffer_wrap_x_build;
    wire framebuffer_wrap_y_build;
    wire framebuffer_key_enable_build;
    wire [31:0] framebuffer_key_build;
    wire framebuffer_wrap_x_baseline;
    wire framebuffer_wrap_y_baseline;
    wire framebuffer_key_enable_baseline;
    wire [31:0] framebuffer_key_baseline;

    wire tile0_enable_build;
    wire tile0_above_build;
    wire [7:0] tile0_opacity_build;
    wire tile0_enable_baseline;
    wire tile0_above_baseline;
    wire [7:0] tile0_opacity_baseline;
    wire tile0_tile_16_build;
    wire tile0_index_8_build;
    wire [3:0] tile0_map_width_log2_build;
    wire [3:0] tile0_map_height_log2_build;
    wire tile0_tile_16_baseline;
    wire tile0_index_8_baseline;
    wire [3:0] tile0_map_width_log2_baseline;
    wire [3:0] tile0_map_height_log2_baseline;
    wire tile0_wrap_x_build;
    wire tile0_wrap_y_build;
    wire tile0_transparent_enable_build;
    wire [7:0] tile0_transparent_index_build;
    wire signed [31:0] tile0_scroll_x_build;
    wire signed [31:0] tile0_scroll_y_build;
    wire tile0_wrap_x_baseline;
    wire tile0_wrap_y_baseline;
    wire tile0_transparent_enable_baseline;
    wire [7:0] tile0_transparent_index_baseline;
    wire signed [31:0] tile0_scroll_x_baseline;
    wire signed [31:0] tile0_scroll_y_baseline;
    wire [31:0] tile0_map_base_build;
    wire [31:0] tile0_pattern_base_build;
    wire [16:0] tile0_tile_count_build;
    wire [31:0] tile0_map_base_baseline;
    wire [31:0] tile0_pattern_base_baseline;
    wire [16:0] tile0_tile_count_baseline;

    wire tile1_enable_build;
    wire tile1_above_build;
    wire [7:0] tile1_opacity_build;
    wire tile1_enable_baseline;
    wire tile1_above_baseline;
    wire [7:0] tile1_opacity_baseline;
    wire tile1_tile_16_build;
    wire tile1_index_8_build;
    wire [3:0] tile1_map_width_log2_build;
    wire [3:0] tile1_map_height_log2_build;
    wire tile1_tile_16_baseline;
    wire tile1_index_8_baseline;
    wire [3:0] tile1_map_width_log2_baseline;
    wire [3:0] tile1_map_height_log2_baseline;
    wire tile1_wrap_x_build;
    wire tile1_wrap_y_build;
    wire tile1_transparent_enable_build;
    wire [7:0] tile1_transparent_index_build;
    wire signed [31:0] tile1_scroll_x_build;
    wire signed [31:0] tile1_scroll_y_build;
    wire tile1_wrap_x_baseline;
    wire tile1_wrap_y_baseline;
    wire tile1_transparent_enable_baseline;
    wire [7:0] tile1_transparent_index_baseline;
    wire signed [31:0] tile1_scroll_x_baseline;
    wire signed [31:0] tile1_scroll_y_baseline;
    wire [31:0] tile1_map_base_build;
    wire [31:0] tile1_pattern_base_build;
    wire [16:0] tile1_tile_count_build;
    wire [31:0] tile1_map_base_baseline;
    wire [31:0] tile1_pattern_base_baseline;
    wire [16:0] tile1_tile_count_baseline;

    wire sprite_enable_build;
    wire sprite_enable_baseline;
    wire copper_sprite_palette_write_enable;
    wire [3:0] copper_sprite_palette_write_bank;
    wire [7:0] copper_sprite_palette_write_index;
    wire [31:0] copper_sprite_palette_write_argb;
    wire sprite_palette_restore_busy;
    wire sprite_palette_restore_done;
    wire copper_sprite_palette_write_ready;
    wire sprite_descriptor_write_enable;
    wire [5:0] sprite_descriptor_write_index;
    wire [2:0] sprite_descriptor_write_word;
    wire [31:0] sprite_descriptor_write_data;
    wire sprite_palette_write_enable;
    wire [3:0] sprite_palette_write_bank;
    wire [7:0] sprite_palette_write_index;
    wire [31:0] sprite_palette_write_argb;
    wire sprite_scene_write_ready;
    wire sprite_validate_start;
    wire sprite_validate_busy;
    wire sprite_validate_done;
    wire sprite_validate_valid;
    wire sprite_accept_pending;
    wire sprite_pending_ready;
    wire sprite_pending_valid;
    wire sprite_activate_start;
    wire sprite_activate_busy;
    wire sprite_activate_done;
    wire [5:0] sprite_collision_read_row;

    wire sprite_order_read_enable;
    wire [5:0] sprite_order_read_position;
    wire [5:0] sprite_order_read_index;
    wire sprite_descriptor_read_enable;
    wire [5:0] sprite_descriptor_read_index;
    wire [31:0] sprite_descriptor_word0;
    wire [31:0] sprite_descriptor_word1;
    wire [31:0] sprite_descriptor_word2;
    wire [31:0] sprite_descriptor_word3;
    wire [31:0] sprite_descriptor_word4;
    wire [31:0] sprite_descriptor_word5;
    wire [31:0] sprite_descriptor_word6;
    wire [31:0] sprite_scale_step_x;
    wire [63:0] sprite_collision_compatible;
    wire [3:0] sprite_palette0_read_bank;
    wire [7:0] sprite_palette0_read_index;
    wire [31:0] sprite_palette0_read_argb;
    wire [3:0] sprite_palette1_read_bank;
    wire [7:0] sprite_palette1_read_index;
    wire [31:0] sprite_palette1_read_argb;
    wire [3:0] sprite_palette2_read_bank;
    wire [7:0] sprite_palette2_read_index;
    wire [31:0] sprite_palette2_read_argb;
    wire [3:0] sprite_palette3_read_bank;
    wire [7:0] sprite_palette3_read_index;
    wire [31:0] sprite_palette3_read_argb;
    wire sprite_done;
    wire sprite_line_complete;
    wire [1:0] sprite_completed_slot;
    wire [3:0] sprite_slot_valid;
    wire sprite_fetch_error;
    wire sprite_deadline_error;
    wire [31:0] sprite_build_cycles;
    wire [31:0] sprite_max_build_cycles;
    wire [31:0] sprite_axi_error_count;
    wire [31:0] sprite_deadline_error_count;
    wire [31:0] sprite_read_bytes;
    wire [63:0] sprite_overflow_bitmap;
    wire [9:0] sprite_overflow_line;
    wire [31:0] sprite_overflow_count;
    wire [31:0] sprite_pixels_admitted;
    wire [31:0] sprite_pixels_dropped;
    wire [63:0] sprite_collision_read_data;
    wire [31:0] sprite_collision_frame;
    wire sprite_collision_event;
    wire [31:0] sprite_front_argb;
    wire [31:0] sprite_behind_argb;

    wire framebuffer_palette_write_enable;
    wire [7:0] framebuffer_palette_write_index;
    wire [31:0] framebuffer_palette_write_argb;
    wire tile_palette_write_enable;
    wire [3:0] tile_palette_write_bank;
    wire [7:0] tile_palette_write_index;
    wire [31:0] tile_palette_write_argb;
    wire palette_host_write_ready;
    wire palette_restore_busy;
    wire palette_restore_done;
    wire palette_copper_write_ready;
    wire boot_text_shadow_enable;
    wire boot_text_write_enable;
    wire [7:0] boot_text_write_index;
    wire [15:0] boot_text_write_cell;
    wire boot_text_commit_enable;
    wire boot_text_write_ready;
    wire boot_text_commit_ready;
    wire boot_text_active_enable;
    wire [31:0] boot_text_generation;
    wire render_enable_build;
    wire render_queue_rebase;
    wire render_soft_reset;
    wire [31:0] render_submission_ring_offset;
    wire [10:0] render_submission_producer;
    wire [10:0] render_submission_consumer;
    wire [31:0] render_completion_ring_offset;
    wire [10:0] render_completion_producer;
    wire [10:0] render_completion_consumer;
    wire [31:0] render_resource_generation;
    wire render_busy;
    wire render_engine_reset_active;
    wire render_configuration_fault;
    wire render_completion_irq;
    wire [31:0] render_retired_fence;
    wire [31:0] render_commands_submitted;
    wire [31:0] render_commands_completed;
    wire [31:0] render_commands_failed;
    wire [31:0] render_backpressure_cycles;
    wire [31:0] render_timeout_count;
    wire [31:0] render_reset_count;
    wire [31:0] render_last_fault_detail;
    wire render_protected0_valid;
    wire [31:0] render_protected0_offset;
    wire [31:0] render_protected0_bytes;
    wire render_protected1_valid;
    wire [31:0] render_protected1_offset;
    wire [31:0] render_protected1_bytes;

    wire [31:0] control_axi_awaddr;
    wire [2:0] control_axi_awprot;
    wire control_axi_awvalid;
    wire control_axi_awready;
    wire [31:0] control_axi_wdata;
    wire [3:0] control_axi_wstrb;
    wire control_axi_wvalid;
    wire control_axi_wready;
    wire [1:0] control_axi_bresp;
    wire control_axi_bvalid;
    wire control_axi_bready;
    wire [31:0] control_axi_araddr;
    wire [2:0] control_axi_arprot;
    wire control_axi_arvalid;
    wire control_axi_arready;
    wire [31:0] control_axi_rdata;
    wire [1:0] control_axi_rresp;
    wire control_axi_rvalid;
    wire control_axi_rready;

    wire [31:0] copper_axi_awaddr;
    wire [2:0] copper_axi_awprot;
    wire copper_axi_awvalid;
    wire copper_axi_awready;
    wire [31:0] copper_axi_wdata;
    wire [3:0] copper_axi_wstrb;
    wire copper_axi_wvalid;
    wire copper_axi_wready;
    wire [1:0] copper_axi_bresp;
    wire copper_axi_bvalid;
    wire copper_axi_bready;
    wire [31:0] copper_axi_araddr;
    wire [2:0] copper_axi_arprot;
    wire copper_axi_arvalid;
    wire copper_axi_arready;
    wire [31:0] copper_axi_rdata;
    wire [1:0] copper_axi_rresp;
    wire copper_axi_rvalid;
    wire copper_axi_rready;
    wire copper_move_valid;
    wire [15:0] copper_move_target;
    wire [31:0] copper_move_data;
    wire [10:0] copper_move_beam_x;
    wire [9:0] copper_move_beam_y;
    wire [1:0] copper_move_class;
    wire [15:0] copper_validate_move_target;
    wire [31:0] copper_validate_move_data;
    wire copper_dispatch_valid;
    wire [15:0] copper_dispatch_id;
    wire [10:0] copper_dispatch_submission_producer;
    wire copper_dispatch_ready;
    wire copper_dispatch_allowed;
    wire [15:0] copper_validate_dispatch_id;
    wire copper_irq_event;
    wire [15:0] copper_irq_sources;
    wire [10:0] copper_irq_beam_x;
    wire [9:0] copper_irq_beam_y;
    wire copper_interrupt;
    wire copper_baseline_restore;
    wire copper_enabled;
    wire copper_running;
    wire copper_waiting;
    wire copper_faulted;
    wire [10:0] copper_virtual_beam_x;
    wire [9:0] copper_virtual_beam_y;
    wire graphics_render_interrupt;
    wire copper_register_move_ready;
    wire copper_register_move_allowed;
    wire copper_move_allowed;
    wire [1:0] copper_move_timing_class;
    wire copper_register_validate_allowed;
    wire copper_validate_move_allowed;
    wire [1:0] copper_validate_timing_class;
    wire copper_structural_move_allowed;
    wire copper_structural_move_ready;
    wire copper_structural_validate_allowed;
    wire copper_exact_enqueue_ready;
    wire copper_irq_delivery_build;
    wire copper_pixel_event_irq;
    wire [9:0] copper_pixel_event_y;
    wire [10:0] copper_pixel_event_x;
    wire [15:0] copper_pixel_event_target;
    wire [31:0] copper_pixel_event_data;
    wire copper_pixel_event_valid;
    wire copper_pixel_event_ready;
    wire [12:0] copper_exact_enqueue_level;
    wire [12:0] copper_pixel_event_level;
    wire copper_exact_overflow;
    wire copper_exact_stale;
    wire copper_exact_late;
    wire copper_exact_enqueue_valid;
    wire copper_frame_start =
        (frame_boundary_build && !commit_pending_status) || scene_changed_qq;

    always @(posedge build_clk) begin
        if (build_reset)
            scene_changed_q <= 1'b0;
        else begin
            scene_changed_q <= scene_changed;
            scene_changed_qq <= scene_changed_q;
        end
        if (build_reset)
            scene_changed_qq <= 1'b0;
    end

    astra_axi_lite_1to2 control_split_i (
        .clk(build_clk),
        .reset(build_reset),
        .s_awaddr(s_axi_awaddr),
        .s_awprot(s_axi_awprot),
        .s_awvalid(s_axi_awvalid),
        .s_awready(s_axi_awready),
        .s_wdata(s_axi_wdata),
        .s_wstrb(s_axi_wstrb),
        .s_wvalid(s_axi_wvalid),
        .s_wready(s_axi_wready),
        .s_bresp(s_axi_bresp),
        .s_bvalid(s_axi_bvalid),
        .s_bready(s_axi_bready),
        .s_araddr(s_axi_araddr),
        .s_arprot(s_axi_arprot),
        .s_arvalid(s_axi_arvalid),
        .s_arready(s_axi_arready),
        .s_rdata(s_axi_rdata),
        .s_rresp(s_axi_rresp),
        .s_rvalid(s_axi_rvalid),
        .s_rready(s_axi_rready),
        .m0_awaddr(control_axi_awaddr),
        .m0_awprot(control_axi_awprot),
        .m0_awvalid(control_axi_awvalid),
        .m0_awready(control_axi_awready),
        .m0_wdata(control_axi_wdata),
        .m0_wstrb(control_axi_wstrb),
        .m0_wvalid(control_axi_wvalid),
        .m0_wready(control_axi_wready),
        .m0_bresp(control_axi_bresp),
        .m0_bvalid(control_axi_bvalid),
        .m0_bready(control_axi_bready),
        .m0_araddr(control_axi_araddr),
        .m0_arprot(control_axi_arprot),
        .m0_arvalid(control_axi_arvalid),
        .m0_arready(control_axi_arready),
        .m0_rdata(control_axi_rdata),
        .m0_rresp(control_axi_rresp),
        .m0_rvalid(control_axi_rvalid),
        .m0_rready(control_axi_rready),
        .m1_awaddr(copper_axi_awaddr),
        .m1_awprot(copper_axi_awprot),
        .m1_awvalid(copper_axi_awvalid),
        .m1_awready(copper_axi_awready),
        .m1_wdata(copper_axi_wdata),
        .m1_wstrb(copper_axi_wstrb),
        .m1_wvalid(copper_axi_wvalid),
        .m1_wready(copper_axi_wready),
        .m1_bresp(copper_axi_bresp),
        .m1_bvalid(copper_axi_bvalid),
        .m1_bready(copper_axi_bready),
        .m1_araddr(copper_axi_araddr),
        .m1_arprot(copper_axi_arprot),
        .m1_arvalid(copper_axi_arvalid),
        .m1_arready(copper_axi_arready),
        .m1_rdata(copper_axi_rdata),
        .m1_rresp(copper_axi_rresp),
        .m1_rvalid(copper_axi_rvalid),
        .m1_rready(copper_axi_rready)
    );

    astra_copper_registers copper_registers_i (
        .clk(build_clk),
        .reset(build_reset),
        .baseline_restore(copper_baseline_restore || scene_changed_q),
        .baseline_backdrop_rgb(backdrop_rgb_baseline),
        .baseline_framebuffer_viewport_x(framebuffer_viewport_x_baseline),
        .baseline_framebuffer_viewport_y(framebuffer_viewport_y_baseline),
        .baseline_framebuffer_wrap_x(framebuffer_wrap_x_baseline),
        .baseline_framebuffer_wrap_y(framebuffer_wrap_y_baseline),
        .baseline_framebuffer_key_enable(framebuffer_key_enable_baseline),
        .baseline_framebuffer_key(framebuffer_key_baseline),
        .baseline_tile0_enable(tile0_enable_baseline),
        .baseline_tile0_above(tile0_above_baseline),
        .baseline_tile0_opacity(tile0_opacity_baseline),
        .baseline_tile0_wrap_x(tile0_wrap_x_baseline),
        .baseline_tile0_wrap_y(tile0_wrap_y_baseline),
        .baseline_tile0_transparent_enable(
            tile0_transparent_enable_baseline),
        .baseline_tile0_transparent_index(
            tile0_transparent_index_baseline),
        .baseline_tile0_scroll_x(tile0_scroll_x_baseline),
        .baseline_tile0_scroll_y(tile0_scroll_y_baseline),
        .baseline_tile1_enable(tile1_enable_baseline),
        .baseline_tile1_above(tile1_above_baseline),
        .baseline_tile1_opacity(tile1_opacity_baseline),
        .baseline_tile1_wrap_x(tile1_wrap_x_baseline),
        .baseline_tile1_wrap_y(tile1_wrap_y_baseline),
        .baseline_tile1_transparent_enable(
            tile1_transparent_enable_baseline),
        .baseline_tile1_transparent_index(
            tile1_transparent_index_baseline),
        .baseline_tile1_scroll_x(tile1_scroll_x_baseline),
        .baseline_tile1_scroll_y(tile1_scroll_y_baseline),
        .baseline_sprite_enable(sprite_enable_baseline),
        .validate_target(copper_validate_move_target),
        .validate_data(copper_validate_move_data),
        .validate_allowed(copper_register_validate_allowed),
        .validate_timing_class(copper_validate_timing_class),
        .move_target(copper_move_target),
        .move_data(copper_move_data),
        .move_allowed(copper_register_move_allowed),
        .move_timing_class(copper_move_timing_class),
        .move_valid(copper_move_valid && copper_move_class == 2'd1),
        .move_ready(copper_register_move_ready),
        .palette_write_ready(copper_sprite_palette_write_ready),
        .framebuffer_palette_write_enable(),
        .framebuffer_palette_write_index(),
        .framebuffer_palette_write_argb(),
        .tile_palette_write_enable(),
        .tile_palette_write_bank(),
        .tile_palette_write_index(),
        .tile_palette_write_argb(),
        .sprite_palette_write_enable(
            copper_sprite_palette_write_enable),
        .sprite_palette_write_bank(copper_sprite_palette_write_bank),
        .sprite_palette_write_index(copper_sprite_palette_write_index),
        .sprite_palette_write_argb(copper_sprite_palette_write_argb),
        .backdrop_rgb(backdrop_rgb_build),
        .framebuffer_viewport_x(framebuffer_viewport_x_build),
        .framebuffer_viewport_y(framebuffer_viewport_y_build),
        .framebuffer_wrap_x(framebuffer_wrap_x_build),
        .framebuffer_wrap_y(framebuffer_wrap_y_build),
        .framebuffer_key_enable(framebuffer_key_enable_build),
        .framebuffer_key(framebuffer_key_build),
        .tile0_enable(tile0_enable_build),
        .tile0_above(tile0_above_build),
        .tile0_opacity(tile0_opacity_build),
        .tile0_wrap_x(tile0_wrap_x_build),
        .tile0_wrap_y(tile0_wrap_y_build),
        .tile0_transparent_enable(tile0_transparent_enable_build),
        .tile0_transparent_index(tile0_transparent_index_build),
        .tile0_scroll_x(tile0_scroll_x_build),
        .tile0_scroll_y(tile0_scroll_y_build),
        .tile1_enable(tile1_enable_build),
        .tile1_above(tile1_above_build),
        .tile1_opacity(tile1_opacity_build),
        .tile1_wrap_x(tile1_wrap_x_build),
        .tile1_wrap_y(tile1_wrap_y_build),
        .tile1_transparent_enable(tile1_transparent_enable_build),
        .tile1_transparent_index(tile1_transparent_index_build),
        .tile1_scroll_x(tile1_scroll_x_build),
        .tile1_scroll_y(tile1_scroll_y_build),
        .sprite_enable(sprite_enable_build)
    );

    astra_copper_structural_state #(
        .ARENA_BASE(ARENA_BASE),
        .ARENA_LIMIT(ARENA_LIMIT),
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT)
    ) copper_structural_state_i (
        .clk(build_clk),
        .reset(build_reset),
        .frame_boundary(frame_boundary_build),
        .baseline_changed(scene_changed_q),
        .copper_running(copper_running),
        .copper_fault(copper_faulted),
        .baseline_scene_enable(scene_enable_baseline),
        .baseline_framebuffer_enable(framebuffer_enable_baseline),
        .baseline_framebuffer_format(framebuffer_format_baseline),
        .baseline_framebuffer_base(framebuffer_base_baseline),
        .baseline_framebuffer_pitch(framebuffer_pitch_baseline),
        .baseline_framebuffer_width(framebuffer_width_baseline),
        .baseline_framebuffer_height(framebuffer_height_baseline),
        .baseline_framebuffer_viewport_x(framebuffer_viewport_x_baseline),
        .baseline_framebuffer_viewport_y(framebuffer_viewport_y_baseline),
        .baseline_framebuffer_wrap_x(framebuffer_wrap_x_baseline),
        .baseline_framebuffer_wrap_y(framebuffer_wrap_y_baseline),
        .baseline_tile0_enable(tile0_enable_baseline),
        .baseline_tile0_tile_16(tile0_tile_16_baseline),
        .baseline_tile0_index_8(tile0_index_8_baseline),
        .baseline_tile0_map_width_log2(tile0_map_width_log2_baseline),
        .baseline_tile0_map_height_log2(tile0_map_height_log2_baseline),
        .baseline_tile0_map_base(tile0_map_base_baseline),
        .baseline_tile0_pattern_base(tile0_pattern_base_baseline),
        .baseline_tile0_tile_count(tile0_tile_count_baseline),
        .baseline_tile1_enable(tile1_enable_baseline),
        .baseline_tile1_tile_16(tile1_tile_16_baseline),
        .baseline_tile1_index_8(tile1_index_8_baseline),
        .baseline_tile1_map_width_log2(tile1_map_width_log2_baseline),
        .baseline_tile1_map_height_log2(tile1_map_height_log2_baseline),
        .baseline_tile1_map_base(tile1_map_base_baseline),
        .baseline_tile1_pattern_base(tile1_pattern_base_baseline),
        .baseline_tile1_tile_count(tile1_tile_count_baseline),
        .validate_target(copper_validate_move_target),
        .validate_data(copper_validate_move_data),
        .validate_allowed(copper_structural_validate_allowed),
        .move_valid(copper_move_valid && copper_move_class == 2'd2),
        .move_target(copper_move_target),
        .move_data(copper_move_data),
        .move_allowed(copper_structural_move_allowed),
        .move_ready(copper_structural_move_ready),
        .scene_enable(scene_enable_build),
        .framebuffer_enable(framebuffer_enable_build),
        .framebuffer_format(framebuffer_format_build),
        .framebuffer_base(framebuffer_base_build),
        .framebuffer_pitch(framebuffer_pitch_build),
        .framebuffer_width(framebuffer_width_build),
        .framebuffer_height(framebuffer_height_build),
        .tile0_tile_16(tile0_tile_16_build),
        .tile0_index_8(tile0_index_8_build),
        .tile0_map_width_log2(tile0_map_width_log2_build),
        .tile0_map_height_log2(tile0_map_height_log2_build),
        .tile0_map_base(tile0_map_base_build),
        .tile0_pattern_base(tile0_pattern_base_build),
        .tile0_tile_count(tile0_tile_count_build),
        .tile1_tile_16(tile1_tile_16_build),
        .tile1_index_8(tile1_index_8_build),
        .tile1_map_width_log2(tile1_map_width_log2_build),
        .tile1_map_height_log2(tile1_map_height_log2_build),
        .tile1_map_base(tile1_map_base_build),
        .tile1_pattern_base(tile1_pattern_base_build),
        .tile1_tile_count(tile1_tile_count_build),
        .candidates_accepted(),
        .candidates_rejected(),
        .candidates_deferred()
    );

    wire copper_move_exact_supported = copper_move_target == 16'h0018 ||
        (copper_move_target >= 16'h1000 &&
         copper_move_target <= 16'h5ffc);
    wire copper_validate_exact_supported =
        copper_validate_move_target == 16'h0018 ||
        (copper_validate_move_target >= 16'h1000 &&
         copper_validate_move_target <= 16'h5ffc);
    assign copper_move_allowed = copper_structural_move_allowed ||
        (copper_register_move_allowed &&
         (copper_move_timing_class != 2'd0 ||
          copper_move_exact_supported));
    assign copper_validate_move_allowed =
        copper_structural_validate_allowed ||
        (copper_register_validate_allowed &&
         (copper_validate_timing_class != 2'd0 ||
          copper_validate_exact_supported));
    wire [1:0] copper_effective_move_timing_class =
        copper_structural_move_allowed ? 2'd2 : copper_move_timing_class;
    wire copper_move_ready = copper_move_class == 2'd0 ?
        copper_exact_enqueue_ready :
        copper_move_class == 2'd1 ? copper_register_move_ready :
        copper_structural_move_ready;

    astra_copper_control #(
        .TOTAL_WIDTH(TOTAL_WIDTH),
        .TOTAL_HEIGHT(TOTAL_HEIGHT)
    ) copper_control_i (
        .clk(build_clk),
        .reset(build_reset),
        .frame_boundary(frame_boundary_build),
        .frame_start(copper_frame_start),
        .beam_x(copper_virtual_beam_x),
        .beam_y(copper_virtual_beam_y),
        .move_valid(copper_move_valid),
        .move_ready(copper_move_ready),
        .move_target(copper_move_target),
        .move_data(copper_move_data),
        .move_beam_x(copper_move_beam_x),
        .move_beam_y(copper_move_beam_y),
        .move_allowed(copper_move_allowed),
        .move_timing_class(copper_effective_move_timing_class),
        .move_class(copper_move_class),
        .validate_move_target(copper_validate_move_target),
        .validate_move_data(copper_validate_move_data),
        .validate_move_allowed(copper_validate_move_allowed),
        .dispatch_valid(copper_dispatch_valid),
        .dispatch_ready(copper_dispatch_ready),
        .dispatch_id(copper_dispatch_id),
        .dispatch_submission_producer(
            copper_dispatch_submission_producer),
        .dispatch_allowed(copper_dispatch_allowed),
        .validate_dispatch_id(copper_validate_dispatch_id),
        .validate_dispatch_allowed(1'b1),
        .irq_event(copper_irq_event),
        .irq_ready(copper_exact_enqueue_ready),
        .irq_delivered(copper_irq_delivery_build),
        .irq_sources(copper_irq_sources),
        .irq_beam_x(copper_irq_beam_x),
        .irq_beam_y(copper_irq_beam_y),
        .interrupt(copper_interrupt),
        .baseline_restore(copper_baseline_restore),
        .enabled(copper_enabled),
        .running(copper_running),
        .waiting(copper_waiting),
        .faulted(copper_faulted),
        .s_axi_awaddr(copper_axi_awaddr),
        .s_axi_awprot(copper_axi_awprot),
        .s_axi_awvalid(copper_axi_awvalid),
        .s_axi_awready(copper_axi_awready),
        .s_axi_wdata(copper_axi_wdata),
        .s_axi_wstrb(copper_axi_wstrb),
        .s_axi_wvalid(copper_axi_wvalid),
        .s_axi_wready(copper_axi_wready),
        .s_axi_bresp(copper_axi_bresp),
        .s_axi_bvalid(copper_axi_bvalid),
        .s_axi_bready(copper_axi_bready),
        .s_axi_araddr(copper_axi_araddr),
        .s_axi_arprot(copper_axi_arprot),
        .s_axi_arvalid(copper_axi_arvalid),
        .s_axi_arready(copper_axi_arready),
        .s_axi_rdata(copper_axi_rdata),
        .s_axi_rresp(copper_axi_rresp),
        .s_axi_rvalid(copper_axi_rvalid),
        .s_axi_rready(copper_axi_rready)
    );

    assign render_interrupt = graphics_render_interrupt | copper_interrupt;

    astra_graphics_control #(
        .ARENA_BASE(ARENA_BASE),
        .ARENA_LIMIT(ARENA_LIMIT),
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .BOOT_TEXT_COLS(BOOT_TEXT_COLS),
        .BOOT_TEXT_ROWS(BOOT_TEXT_ROWS),
        .BOOT_TEXT_ORIGIN_X(BOOT_TEXT_ORIGIN_X),
        .BOOT_TEXT_ORIGIN_Y(BOOT_TEXT_ORIGIN_Y),
        .BOOT_TEXT_CELL_WIDTH(BOOT_TEXT_CELL_WIDTH),
        .BOOT_TEXT_ROW_PITCH(BOOT_TEXT_ROW_PITCH)
    ) control_i (
        .clk(build_clk),
        .reset(build_reset),
        .frame_boundary(frame_boundary_build),
        .commit_safe(commit_safe),
        .scene_changed(scene_changed),
        .active_generation(active_generation),
        .commit_pending_status(commit_pending_status),
        .commit_quiesce(commit_quiesce),
        .commit_errors(commit_errors),
        .commit_deferrals(commit_deferrals),
        .scene_enable(scene_enable_baseline),
        .backdrop_rgb(backdrop_rgb_baseline),
        .framebuffer_enable(framebuffer_enable_baseline),
        .framebuffer_format(framebuffer_format_baseline),
        .framebuffer_base(framebuffer_base_baseline),
        .framebuffer_pitch(framebuffer_pitch_baseline),
        .framebuffer_width(framebuffer_width_baseline),
        .framebuffer_height(framebuffer_height_baseline),
        .framebuffer_viewport_x(framebuffer_viewport_x_baseline),
        .framebuffer_viewport_y(framebuffer_viewport_y_baseline),
        .framebuffer_wrap_x(framebuffer_wrap_x_baseline),
        .framebuffer_wrap_y(framebuffer_wrap_y_baseline),
        .framebuffer_key_enable(framebuffer_key_enable_baseline),
        .framebuffer_key(framebuffer_key_baseline),
        .tile0_enable(tile0_enable_baseline),
        .tile0_above_framebuffer(tile0_above_baseline),
        .tile0_opacity(tile0_opacity_baseline),
        .tile0_tile_16(tile0_tile_16_baseline),
        .tile0_index_8(tile0_index_8_baseline),
        .tile0_map_width_log2(tile0_map_width_log2_baseline),
        .tile0_map_height_log2(tile0_map_height_log2_baseline),
        .tile0_wrap_x(tile0_wrap_x_baseline),
        .tile0_wrap_y(tile0_wrap_y_baseline),
        .tile0_transparent_enable(tile0_transparent_enable_baseline),
        .tile0_transparent_index(tile0_transparent_index_baseline),
        .tile0_scroll_x(tile0_scroll_x_baseline),
        .tile0_scroll_y(tile0_scroll_y_baseline),
        .tile0_map_base(tile0_map_base_baseline),
        .tile0_pattern_base(tile0_pattern_base_baseline),
        .tile0_tile_count(tile0_tile_count_baseline),
        .tile1_enable(tile1_enable_baseline),
        .tile1_above_framebuffer(tile1_above_baseline),
        .tile1_opacity(tile1_opacity_baseline),
        .tile1_tile_16(tile1_tile_16_baseline),
        .tile1_index_8(tile1_index_8_baseline),
        .tile1_map_width_log2(tile1_map_width_log2_baseline),
        .tile1_map_height_log2(tile1_map_height_log2_baseline),
        .tile1_wrap_x(tile1_wrap_x_baseline),
        .tile1_wrap_y(tile1_wrap_y_baseline),
        .tile1_transparent_enable(tile1_transparent_enable_baseline),
        .tile1_transparent_index(tile1_transparent_index_baseline),
        .tile1_scroll_x(tile1_scroll_x_baseline),
        .tile1_scroll_y(tile1_scroll_y_baseline),
        .tile1_map_base(tile1_map_base_baseline),
        .tile1_pattern_base(tile1_pattern_base_baseline),
        .tile1_tile_count(tile1_tile_count_baseline),
        .sprite_enable(sprite_enable_baseline),
        .sprite_descriptor_write_enable(
            sprite_descriptor_write_enable),
        .sprite_descriptor_write_index(sprite_descriptor_write_index),
        .sprite_descriptor_write_word(sprite_descriptor_write_word),
        .sprite_descriptor_write_data(sprite_descriptor_write_data),
        .sprite_palette_write_enable(sprite_palette_write_enable),
        .sprite_palette_write_bank(sprite_palette_write_bank),
        .sprite_palette_write_index(sprite_palette_write_index),
        .sprite_palette_write_argb(sprite_palette_write_argb),
        .sprite_scene_write_ready(sprite_scene_write_ready),
        .sprite_validate_start(sprite_validate_start),
        .sprite_validate_busy(sprite_validate_busy),
        .sprite_validate_done(sprite_validate_done),
        .sprite_validate_valid(sprite_validate_valid),
        .sprite_accept_pending(sprite_accept_pending),
        .sprite_pending_ready(sprite_pending_ready),
        .sprite_pending_valid(sprite_pending_valid),
        .sprite_activate_start(sprite_activate_start),
        .sprite_activate_busy(sprite_activate_busy),
        .sprite_activate_done(sprite_activate_done),
        .sprite_builder_busy(sprite_busy),
        .sprite_slot_valid(sprite_slot_valid),
        .sprite_fetch_error(sprite_fetch_error),
        .sprite_deadline_error(sprite_deadline_error),
        .sprite_build_cycles(sprite_build_cycles),
        .sprite_max_build_cycles(sprite_max_build_cycles),
        .sprite_axi_error_count(sprite_axi_error_count),
        .sprite_deadline_error_count(sprite_deadline_error_count),
        .sprite_read_bytes(sprite_read_bytes),
        .sprite_overflow_bitmap(sprite_overflow_bitmap),
        .sprite_overflow_line(sprite_overflow_line),
        .sprite_overflow_count(sprite_overflow_count),
        .sprite_pixels_admitted(sprite_pixels_admitted),
        .sprite_pixels_dropped(sprite_pixels_dropped),
        .sprite_collision_read_row(sprite_collision_read_row),
        .sprite_collision_read_data(sprite_collision_read_data),
        .sprite_collision_frame(sprite_collision_frame),
        .sprite_collision_event(sprite_collision_event),
        .framebuffer_palette_write_enable(framebuffer_palette_write_enable),
        .framebuffer_palette_write_index(framebuffer_palette_write_index),
        .framebuffer_palette_write_argb(framebuffer_palette_write_argb),
        .tile_palette_write_enable(tile_palette_write_enable),
        .tile_palette_write_bank(tile_palette_write_bank),
        .tile_palette_write_index(tile_palette_write_index),
        .tile_palette_write_argb(tile_palette_write_argb),
        .palette_write_ready(palette_host_write_ready),
        .boot_text_shadow_enable(boot_text_shadow_enable),
        .boot_text_write_enable(boot_text_write_enable),
        .boot_text_write_index(boot_text_write_index),
        .boot_text_write_cell(boot_text_write_cell),
        .boot_text_commit_enable(boot_text_commit_enable),
        .boot_text_write_ready(boot_text_write_ready),
        .boot_text_commit_ready(boot_text_commit_ready),
        .boot_text_active_enable(boot_text_active_enable),
        .boot_text_generation(boot_text_generation),
        .render_enable(render_enable_build),
        .render_queue_rebase(render_queue_rebase),
        .render_soft_reset(render_soft_reset),
        .render_submission_ring_offset(render_submission_ring_offset),
        .render_submission_producer(render_submission_producer),
        .render_submission_consumer(render_submission_consumer),
        .copper_dispatch_valid(copper_dispatch_valid),
        .copper_dispatch_submission_producer(
            copper_dispatch_submission_producer),
        .copper_dispatch_ready(copper_dispatch_ready),
        .copper_dispatch_allowed(copper_dispatch_allowed),
        .render_completion_ring_offset(render_completion_ring_offset),
        .render_completion_producer(render_completion_producer),
        .render_completion_consumer(render_completion_consumer),
        .render_resource_generation(render_resource_generation),
        .render_busy(render_busy),
        .render_engine_reset_active(render_engine_reset_active),
        .render_configuration_fault(render_configuration_fault),
        .render_completion_irq(render_completion_irq),
        .render_retired_fence(render_retired_fence),
        .render_commands_submitted(render_commands_submitted),
        .render_commands_completed(render_commands_completed),
        .render_commands_failed(render_commands_failed),
        .render_backpressure_cycles(render_backpressure_cycles),
        .render_timeout_count(render_timeout_count),
        .render_reset_count(render_reset_count),
        .render_last_fault_detail(render_last_fault_detail),
        .render_interrupt(graphics_render_interrupt),
        .render_protected0_valid(render_protected0_valid),
        .render_protected0_offset(render_protected0_offset),
        .render_protected0_bytes(render_protected0_bytes),
        .render_protected1_valid(render_protected1_valid),
        .render_protected1_offset(render_protected1_offset),
        .render_protected1_bytes(render_protected1_bytes),
        .s_axi_awaddr(control_axi_awaddr),
        .s_axi_awprot(control_axi_awprot),
        .s_axi_awvalid(control_axi_awvalid),
        .s_axi_awready(control_axi_awready),
        .s_axi_wdata(control_axi_wdata),
        .s_axi_wstrb(control_axi_wstrb),
        .s_axi_wvalid(control_axi_wvalid),
        .s_axi_wready(control_axi_wready),
        .s_axi_bresp(control_axi_bresp),
        .s_axi_bvalid(control_axi_bvalid),
        .s_axi_bready(control_axi_bready),
        .s_axi_araddr(control_axi_araddr),
        .s_axi_arprot(control_axi_arprot),
        .s_axi_arvalid(control_axi_arvalid),
        .s_axi_arready(control_axi_arready),
        .s_axi_rdata(control_axi_rdata),
        .s_axi_rresp(control_axi_rresp),
        .s_axi_rvalid(control_axi_rvalid),
        .s_axi_rready(control_axi_rready)
    );

    astra_render_command_processor #(
        .ARENA_BASE(ARENA_BASE),
        .ARENA_LIMIT(ARENA_LIMIT),
        .AXI_ID_WIDTH(AXI_ID_WIDTH)
    ) render_command_i (
        .clk(build_clk),
        .reset(build_reset),
        .enable(render_enable_build),
        .queue_rebase(render_queue_rebase),
        .soft_reset(render_soft_reset),
        .submission_ring_offset(render_submission_ring_offset),
        .submission_producer(render_submission_producer),
        .submission_consumer(render_submission_consumer),
        .completion_ring_offset(render_completion_ring_offset),
        .completion_producer(render_completion_producer),
        .completion_consumer(render_completion_consumer),
        .resource_generation(render_resource_generation),
        .protected0_valid(render_protected0_valid),
        .protected0_offset(render_protected0_offset),
        .protected0_bytes(render_protected0_bytes),
        .protected1_valid(render_protected1_valid),
        .protected1_offset(render_protected1_offset),
        .protected1_bytes(render_protected1_bytes),
        .busy(render_busy),
        .completion_irq(render_completion_irq),
        .engine_reset_active(render_engine_reset_active),
        .configuration_fault(render_configuration_fault),
        .retired_fence(render_retired_fence),
        .commands_submitted(render_commands_submitted),
        .commands_completed(render_commands_completed),
        .commands_failed(render_commands_failed),
        .backpressure_cycles(render_backpressure_cycles),
        .timeout_count(render_timeout_count),
        .reset_count(render_reset_count),
        .last_fault_detail(render_last_fault_detail),
        .m_axi_arid(render_axi_arid),
        .m_axi_araddr(render_axi_araddr),
        .m_axi_arlen(render_axi_arlen),
        .m_axi_arsize(render_axi_arsize),
        .m_axi_arburst(render_axi_arburst),
        .m_axi_arcache(render_axi_arcache),
        .m_axi_arprot(render_axi_arprot),
        .m_axi_arqos(render_axi_arqos),
        .m_axi_arvalid(render_axi_arvalid),
        .m_axi_arready(render_axi_arready),
        .m_axi_rid(render_axi_rid),
        .m_axi_rdata(render_axi_rdata),
        .m_axi_rresp(render_axi_rresp),
        .m_axi_rlast(render_axi_rlast),
        .m_axi_rvalid(render_axi_rvalid),
        .m_axi_rready(render_axi_rready),
        .m_axi_awid(render_axi_awid),
        .m_axi_awaddr(render_axi_awaddr),
        .m_axi_awlen(render_axi_awlen),
        .m_axi_awsize(render_axi_awsize),
        .m_axi_awburst(render_axi_awburst),
        .m_axi_awcache(render_axi_awcache),
        .m_axi_awprot(render_axi_awprot),
        .m_axi_awqos(render_axi_awqos),
        .m_axi_awvalid(render_axi_awvalid),
        .m_axi_awready(render_axi_awready),
        .m_axi_wdata(render_axi_wdata),
        .m_axi_wstrb(render_axi_wstrb),
        .m_axi_wlast(render_axi_wlast),
        .m_axi_wvalid(render_axi_wvalid),
        .m_axi_wready(render_axi_wready),
        .m_axi_bid(render_axi_bid),
        .m_axi_bresp(render_axi_bresp),
        .m_axi_bvalid(render_axi_bvalid),
        .m_axi_bready(render_axi_bready)
    );

    astra_sprite_scene_store #(
        .ARENA_BASE(ARENA_BASE),
        .ARENA_LIMIT(ARENA_LIMIT)
    ) sprite_scene_i (
        .clk(build_clk),
        .reset(build_reset),
        .descriptor_write_enable(sprite_descriptor_write_enable),
        .descriptor_write_index(sprite_descriptor_write_index),
        .descriptor_write_word(sprite_descriptor_write_word),
        .descriptor_write_data(sprite_descriptor_write_data),
        .palette_write_enable(sprite_palette_write_enable),
        .palette_write_bank(sprite_palette_write_bank),
        .palette_write_index(sprite_palette_write_index),
        .palette_write_argb(sprite_palette_write_argb),
        .write_ready(sprite_scene_write_ready),
        .validate_start(sprite_validate_start),
        .validate_busy(sprite_validate_busy),
        .validate_done(sprite_validate_done),
        .validate_valid(sprite_validate_valid),
        .accept_pending(sprite_accept_pending),
        .pending_ready(sprite_pending_ready),
        .pending_valid(sprite_pending_valid),
        .activate_start(sprite_activate_start),
        .activate_busy(sprite_activate_busy),
        .activate_done(sprite_activate_done),
        .baseline_restore_start(copper_baseline_restore),
        .baseline_restore_busy(sprite_palette_restore_busy),
        .baseline_restore_done(sprite_palette_restore_done),
        .copper_palette_write_enable(
            copper_sprite_palette_write_enable),
        .copper_palette_write_bank(copper_sprite_palette_write_bank),
        .copper_palette_write_index(copper_sprite_palette_write_index),
        .copper_palette_write_argb(copper_sprite_palette_write_argb),
        .copper_palette_write_ready(copper_sprite_palette_write_ready),
        .order_read_enable(sprite_order_read_enable),
        .order_read_position(sprite_order_read_position),
        .order_read_index(sprite_order_read_index),
        .descriptor_read_enable(sprite_descriptor_read_enable),
        .descriptor_read_index(sprite_descriptor_read_index),
        .descriptor_word0(sprite_descriptor_word0),
        .descriptor_word1(sprite_descriptor_word1),
        .descriptor_word2(sprite_descriptor_word2),
        .descriptor_word3(sprite_descriptor_word3),
        .descriptor_word4(sprite_descriptor_word4),
        .descriptor_word5(sprite_descriptor_word5),
        .descriptor_word6(sprite_descriptor_word6),
        .descriptor_scale_step_x(sprite_scale_step_x),
        .descriptor_collision_compatible(sprite_collision_compatible),
        .palette0_read_bank(sprite_palette0_read_bank),
        .palette0_read_index(sprite_palette0_read_index),
        .palette0_read_argb(sprite_palette0_read_argb),
        .palette1_read_bank(sprite_palette1_read_bank),
        .palette1_read_index(sprite_palette1_read_index),
        .palette1_read_argb(sprite_palette1_read_argb),
        .palette2_read_bank(sprite_palette2_read_bank),
        .palette2_read_index(sprite_palette2_read_index),
        .palette2_read_argb(sprite_palette2_read_argb),
        .palette3_read_bank(sprite_palette3_read_bank),
        .palette3_read_index(sprite_palette3_read_index),
        .palette3_read_argb(sprite_palette3_read_argb)
    );

    wire scheduler_start;
    wire scheduler_line_prepare_valid;
    wire [9:0] scheduler_line_prepare_y;
    wire scheduler_line_prepare_ready;
    wire [1:0] scheduler_build_slot;
    wire [9:0] scheduler_line_y;
    wire [3:0] scheduler_client_enable;
    wire [3:0] scheduler_client_done;
    wire [3:0] scheduler_client_complete;
    wire [1:0] pixel_read_slot;
    wire pixel_line_available;
    wire [3:0] pixel_slot_valid;
    wire [9:0] pixel_slot_tag0;
    wire [9:0] pixel_slot_tag1;
    wire [9:0] pixel_slot_tag2;
    wire [9:0] pixel_slot_tag3;

    astra_copper_beam_scheduler #(
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .TOTAL_WIDTH(TOTAL_WIDTH),
        .TOTAL_HEIGHT(TOTAL_HEIGHT)
    ) copper_beam_i (
        .clk(build_clk),
        .reset(build_reset),
        .frame_start(frame_boundary_build),
        .baseline_ready(!copper_baseline_restore && !palette_restore_busy &&
                        !sprite_palette_restore_busy),
        .copper_enabled(copper_enabled),
        .copper_running(copper_running),
        .copper_waiting(copper_waiting),
        .line_prepare_valid(scheduler_line_prepare_valid),
        .line_prepare_y(scheduler_line_prepare_y),
        .line_prepare_ready(scheduler_line_prepare_ready),
        .beam_x(copper_virtual_beam_x),
        .beam_y(copper_virtual_beam_y)
    );

    astra_line_scheduler #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .TOTAL_HEIGHT(TOTAL_HEIGHT)
    ) scheduler_i (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .scene_changed(scene_changed_qq ||
                       (frame_boundary_build && copper_enabled)),
        .quiesce(commit_quiesce),
        .scene_enable(scene_enable_build),
        .framebuffer_enable(framebuffer_enable_build),
        .tile0_enable(tile0_enable_build),
        .tile1_enable(tile1_enable_build),
        .sprite_enable(sprite_enable_build),
        .line_prepare_valid(scheduler_line_prepare_valid),
        .line_prepare_y(scheduler_line_prepare_y),
        .line_prepare_ready(scheduler_line_prepare_ready),
        .client_start(scheduler_start),
        .client_build_slot(scheduler_build_slot),
        .client_line_y(scheduler_line_y),
        .client_enable(scheduler_client_enable),
        .client_done(scheduler_client_done),
        .client_line_complete(scheduler_client_complete),
        .lines_built(lines_built),
        .lines_failed(lines_failed),
        .scheduler_overruns(scheduler_overruns),
        .scheduler_idle(scheduler_idle),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_x(pixel_x),
        .pixel_y(pixel_y),
        .pixel_read_slot(pixel_read_slot),
        .pixel_line_available(pixel_line_available),
        .pixel_underruns(pixel_underruns),
        .pixel_slot_valid(pixel_slot_valid),
        .pixel_slot_tag0(pixel_slot_tag0),
        .pixel_slot_tag1(pixel_slot_tag1),
        .pixel_slot_tag2(pixel_slot_tag2),
        .pixel_slot_tag3(pixel_slot_tag3)
    );


    wire framebuffer_done;
    wire framebuffer_line_complete;
    wire framebuffer_pixel_valid;
    wire [31:0] framebuffer_pixel_value;
    wire [3:0] framebuffer_slot_valid;
    wire framebuffer_config_error;
    wire framebuffer_fetch_error;
    wire framebuffer_deadline_error;
    wire [1:0] framebuffer_completed_slot;
    wire [31:0] framebuffer_build_cycles;
    wire [31:0] framebuffer_read_bytes;

    wire tile0_done;
    wire tile0_line_complete;
    wire tile0_pixel_valid;
    wire [3:0] tile0_pixel_bank;
    wire [7:0] tile0_pixel_index;
    wire [3:0] tile0_slot_valid;
    wire tile0_config_error;
    wire tile0_descriptor_error;
    wire tile0_fetch_error;
    wire [1:0] tile0_completed_slot;
    wire [31:0] tile0_build_cycles;
    wire [31:0] tile0_map_read_bytes;
    wire [31:0] tile0_pattern_read_bytes;

    wire tile1_done;
    wire tile1_line_complete;
    wire tile1_pixel_valid;
    wire [3:0] tile1_pixel_bank;
    wire [7:0] tile1_pixel_index;
    wire [3:0] tile1_slot_valid;
    wire tile1_config_error;
    wire tile1_descriptor_error;
    wire tile1_fetch_error;
    wire [1:0] tile1_completed_slot;
    wire [31:0] tile1_build_cycles;
    wire [31:0] tile1_map_read_bytes;
    wire [31:0] tile1_pattern_read_bytes;

    wire [10:0] line_read_x;

    astra_framebuffer_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID({AXI_ID_WIDTH{1'b0}}),
        .TRUSTED_CONFIG(1)
    ) framebuffer_builder_i (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .start(scheduler_start && scheduler_client_enable[0]),
        .build_slot(scheduler_build_slot),
        .line_y(scheduler_line_y),
        .format(framebuffer_format_build),
        .framebuffer_base(framebuffer_base_build),
        .pitch(framebuffer_pitch_build),
        .virtual_width(framebuffer_width_build),
        .virtual_height(framebuffer_height_build),
        .viewport_x(framebuffer_viewport_x_build),
        .viewport_y(framebuffer_viewport_y_build),
        .wrap_x(framebuffer_wrap_x_build),
        .wrap_y(framebuffer_wrap_y_build),
        .arena_base(ARENA_BASE),
        .arena_limit(ARENA_LIMIT),
        .busy(framebuffer_busy),
        .done(framebuffer_done),
        .line_complete(framebuffer_line_complete),
        .completed_slot(framebuffer_completed_slot),
        .slot_valid(framebuffer_slot_valid),
        .config_error(framebuffer_config_error),
        .fetch_error(framebuffer_fetch_error),
        .deadline_error(framebuffer_deadline_error),
        .build_cycles(framebuffer_build_cycles),
        .read_bytes(framebuffer_read_bytes),
        .m_axi_arid(fb_axi_arid),
        .m_axi_araddr(fb_axi_araddr),
        .m_axi_arlen(fb_axi_arlen),
        .m_axi_arsize(fb_axi_arsize),
        .m_axi_arburst(fb_axi_arburst),
        .m_axi_arcache(fb_axi_arcache),
        .m_axi_arprot(fb_axi_arprot),
        .m_axi_arqos(fb_axi_arqos),
        .m_axi_arvalid(fb_axi_arvalid),
        .m_axi_arready(fb_axi_arready),
        .m_axi_rid(fb_axi_rid),
        .m_axi_rdata(fb_axi_rdata),
        .m_axi_rresp(fb_axi_rresp),
        .m_axi_rlast(fb_axi_rlast),
        .m_axi_rvalid(fb_axi_rvalid),
        .m_axi_rready(fb_axi_rready),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_read_slot(pixel_read_slot),
        .pixel_read_x(line_read_x),
        .pixel_valid(framebuffer_pixel_valid),
        .pixel_value(framebuffer_pixel_value)
    );

    astra_tile_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID({AXI_ID_WIDTH{1'b0}}),
        .TRUSTED_CONFIG(1)
    ) tile0_builder_i (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .start(scheduler_start && scheduler_client_enable[1]),
        .build_slot(scheduler_build_slot),
        .line_y({1'b0, scheduler_line_y}),
        .scroll_x(tile0_scroll_x_build),
        .scroll_y(tile0_scroll_y_build),
        .tile_16(tile0_tile_16_build),
        .index_8(tile0_index_8_build),
        .map_width_log2(tile0_map_width_log2_build),
        .map_height_log2(tile0_map_height_log2_build),
        .wrap_x(tile0_wrap_x_build),
        .wrap_y(tile0_wrap_y_build),
        .transparent_enable(tile0_transparent_enable_build),
        .transparent_index(tile0_transparent_index_build),
        .map_base(tile0_map_base_build),
        .pattern_base(tile0_pattern_base_build),
        .tile_count(tile0_tile_count_build),
        .arena_base(ARENA_BASE),
        .arena_limit(ARENA_LIMIT),
        .busy(tile0_busy),
        .done(tile0_done),
        .line_complete(tile0_line_complete),
        .completed_slot(tile0_completed_slot),
        .slot_valid(tile0_slot_valid),
        .config_error(tile0_config_error),
        .descriptor_error(tile0_descriptor_error),
        .fetch_error(tile0_fetch_error),
        .build_cycles(tile0_build_cycles),
        .map_read_bytes(tile0_map_read_bytes),
        .pattern_read_bytes(tile0_pattern_read_bytes),
        .m_axi_arid(tile0_axi_arid),
        .m_axi_araddr(tile0_axi_araddr),
        .m_axi_arlen(tile0_axi_arlen),
        .m_axi_arsize(tile0_axi_arsize),
        .m_axi_arburst(tile0_axi_arburst),
        .m_axi_arcache(tile0_axi_arcache),
        .m_axi_arprot(tile0_axi_arprot),
        .m_axi_arqos(tile0_axi_arqos),
        .m_axi_arvalid(tile0_axi_arvalid),
        .m_axi_arready(tile0_axi_arready),
        .m_axi_rid(tile0_axi_rid),
        .m_axi_rdata(tile0_axi_rdata),
        .m_axi_rresp(tile0_axi_rresp),
        .m_axi_rlast(tile0_axi_rlast),
        .m_axi_rvalid(tile0_axi_rvalid),
        .m_axi_rready(tile0_axi_rready),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_read_slot(pixel_read_slot),
        .pixel_read_x(line_read_x),
        .pixel_valid(tile0_pixel_valid),
        .pixel_palette_bank(tile0_pixel_bank),
        .pixel_index(tile0_pixel_index)
    );

    astra_tile_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID({AXI_ID_WIDTH{1'b0}}),
        .TRUSTED_CONFIG(1)
    ) tile1_builder_i (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .start(scheduler_start && scheduler_client_enable[2]),
        .build_slot(scheduler_build_slot),
        .line_y({1'b0, scheduler_line_y}),
        .scroll_x(tile1_scroll_x_build),
        .scroll_y(tile1_scroll_y_build),
        .tile_16(tile1_tile_16_build),
        .index_8(tile1_index_8_build),
        .map_width_log2(tile1_map_width_log2_build),
        .map_height_log2(tile1_map_height_log2_build),
        .wrap_x(tile1_wrap_x_build),
        .wrap_y(tile1_wrap_y_build),
        .transparent_enable(tile1_transparent_enable_build),
        .transparent_index(tile1_transparent_index_build),
        .map_base(tile1_map_base_build),
        .pattern_base(tile1_pattern_base_build),
        .tile_count(tile1_tile_count_build),
        .arena_base(ARENA_BASE),
        .arena_limit(ARENA_LIMIT),
        .busy(tile1_busy),
        .done(tile1_done),
        .line_complete(tile1_line_complete),
        .completed_slot(tile1_completed_slot),
        .slot_valid(tile1_slot_valid),
        .config_error(tile1_config_error),
        .descriptor_error(tile1_descriptor_error),
        .fetch_error(tile1_fetch_error),
        .build_cycles(tile1_build_cycles),
        .map_read_bytes(tile1_map_read_bytes),
        .pattern_read_bytes(tile1_pattern_read_bytes),
        .m_axi_arid(tile1_axi_arid),
        .m_axi_araddr(tile1_axi_araddr),
        .m_axi_arlen(tile1_axi_arlen),
        .m_axi_arsize(tile1_axi_arsize),
        .m_axi_arburst(tile1_axi_arburst),
        .m_axi_arcache(tile1_axi_arcache),
        .m_axi_arprot(tile1_axi_arprot),
        .m_axi_arqos(tile1_axi_arqos),
        .m_axi_arvalid(tile1_axi_arvalid),
        .m_axi_arready(tile1_axi_arready),
        .m_axi_rid(tile1_axi_rid),
        .m_axi_rdata(tile1_axi_rdata),
        .m_axi_rresp(tile1_axi_rresp),
        .m_axi_rlast(tile1_axi_rlast),
        .m_axi_rvalid(tile1_axi_rvalid),
        .m_axi_rready(tile1_axi_rready),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_read_slot(pixel_read_slot),
        .pixel_read_x(line_read_x),
        .pixel_valid(tile1_pixel_valid),
        .pixel_palette_bank(tile1_pixel_bank),
        .pixel_index(tile1_pixel_index)
    );

    astra_sprite_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .AXI_ID({AXI_ID_WIDTH{1'b0}}),
        .PIXEL_BUDGET(8192),
        .MAX_BUILD_CYCLES(4300)
    ) sprite_builder_i (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .start(scheduler_start && scheduler_client_enable[3]),
        .build_slot(scheduler_build_slot),
        .line_y(scheduler_line_y),
        .order_read_enable(sprite_order_read_enable),
        .order_read_position(sprite_order_read_position),
        .order_read_index(sprite_order_read_index),
        .descriptor_read_enable(sprite_descriptor_read_enable),
        .descriptor_read_index(sprite_descriptor_read_index),
        .descriptor_word0(sprite_descriptor_word0),
        .descriptor_word1(sprite_descriptor_word1),
        .descriptor_word2(sprite_descriptor_word2),
        .descriptor_word3(sprite_descriptor_word3),
        .descriptor_word4(sprite_descriptor_word4),
        .descriptor_word5(sprite_descriptor_word5),
        .descriptor_word6(sprite_descriptor_word6),
        .descriptor_scale_step_x(sprite_scale_step_x),
        .descriptor_collision_compatible(sprite_collision_compatible),
        .palette0_read_bank(sprite_palette0_read_bank),
        .palette0_read_index(sprite_palette0_read_index),
        .palette0_read_argb(sprite_palette0_read_argb),
        .palette1_read_bank(sprite_palette1_read_bank),
        .palette1_read_index(sprite_palette1_read_index),
        .palette1_read_argb(sprite_palette1_read_argb),
        .palette2_read_bank(sprite_palette2_read_bank),
        .palette2_read_index(sprite_palette2_read_index),
        .palette2_read_argb(sprite_palette2_read_argb),
        .palette3_read_bank(sprite_palette3_read_bank),
        .palette3_read_index(sprite_palette3_read_index),
        .palette3_read_argb(sprite_palette3_read_argb),
        .busy(sprite_busy),
        .done(sprite_done),
        .line_complete(sprite_line_complete),
        .completed_slot(sprite_completed_slot),
        .slot_valid(sprite_slot_valid),
        .fetch_error(sprite_fetch_error),
        .deadline_error(sprite_deadline_error),
        .build_cycles(sprite_build_cycles),
        .max_build_cycles(sprite_max_build_cycles),
        .axi_error_count(sprite_axi_error_count),
        .deadline_error_count(sprite_deadline_error_count),
        .read_bytes(sprite_read_bytes),
        .overflow_bitmap(sprite_overflow_bitmap),
        .overflow_line(sprite_overflow_line),
        .overflow_count(sprite_overflow_count),
        .pixels_admitted(sprite_pixels_admitted),
        .pixels_dropped(sprite_pixels_dropped),
        .collision_read_row(sprite_collision_read_row),
        .collision_read_data(sprite_collision_read_data),
        .collision_frame(sprite_collision_frame),
        .collision_event(sprite_collision_event),
        .m_axi_arid(sprite_axi_arid),
        .m_axi_araddr(sprite_axi_araddr),
        .m_axi_arlen(sprite_axi_arlen),
        .m_axi_arsize(sprite_axi_arsize),
        .m_axi_arburst(sprite_axi_arburst),
        .m_axi_arcache(sprite_axi_arcache),
        .m_axi_arprot(sprite_axi_arprot),
        .m_axi_arqos(sprite_axi_arqos),
        .m_axi_arvalid(sprite_axi_arvalid),
        .m_axi_arready(sprite_axi_arready),
        .m_axi_rid(sprite_axi_rid),
        .m_axi_rdata(sprite_axi_rdata),
        .m_axi_rresp(sprite_axi_rresp),
        .m_axi_rlast(sprite_axi_rlast),
        .m_axi_rvalid(sprite_axi_rvalid),
        .m_axi_rready(sprite_axi_rready),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_read_slot(pixel_read_slot),
        .pixel_read_x(line_read_x),
        .pixel_front_argb(sprite_front_argb),
        .pixel_behind_argb(sprite_behind_argb)
    );

    assign scheduler_client_done = {
        sprite_done, tile1_done, tile0_done, framebuffer_done
    };
    assign scheduler_client_complete = {
        sprite_line_complete, tile1_line_complete, tile0_line_complete,
        framebuffer_line_complete
    };

    // Structural pixel configuration remains stable for a complete frame.
    // Visual controls are captured separately with each completed line slot
    // so next-scanline copper changes cannot affect an earlier prefetched
    // line.
    wire [26:0] build_pixel_config = {
        scene_enable_build,
        backdrop_rgb_build,
        framebuffer_format_build
    };
    (* ASYNC_REG = "TRUE" *) reg [26:0] pixel_config_meta;
    (* ASYNC_REG = "TRUE" *) reg [26:0] pixel_config_sync;
    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            pixel_config_meta <= 27'd0;
            pixel_config_sync <= 27'd0;
        end else begin
            pixel_config_meta <= build_pixel_config;
            pixel_config_sync <= pixel_config_meta;
        end
    end

    wire scene_enable_pixel;
    wire [23:0] backdrop_rgb_pixel_baseline;
    wire [1:0] framebuffer_format_pixel;
    assign {
        scene_enable_pixel,
        backdrop_rgb_pixel_baseline,
        framebuffer_format_pixel
    } = pixel_config_sync;
    assign scene_active = scene_enable_build;

    wire [54:0] build_line_visual = {
        sprite_enable_build,
        framebuffer_enable_build,
        framebuffer_key_enable_build,
        framebuffer_key_build,
        tile0_enable_build,
        tile0_above_build,
        tile0_opacity_build,
        tile1_enable_build,
        tile1_above_build,
        tile1_opacity_build
    };
    reg [54:0] line_visual_slot0;
    reg [54:0] line_visual_slot1;
    reg [54:0] line_visual_slot2;
    reg [54:0] line_visual_slot3;
    always @(posedge build_clk) begin
        if (scheduler_start) begin
            case (scheduler_build_slot)
                2'd0: line_visual_slot0 <= build_line_visual;
                2'd1: line_visual_slot1 <= build_line_visual;
                2'd2: line_visual_slot2 <= build_line_visual;
                2'd3: line_visual_slot3 <= build_line_visual;
            endcase
        end
    end

    wire [9:0] visual_candidate_line =
        pixel_y == TOTAL_HEIGHT - 1 ? 10'd0 : pixel_y + 10'd1;
    wire [1:0] visual_candidate_slot = visual_candidate_line[1:0];
    wire [9:0] visual_candidate_tag =
        visual_candidate_slot == 2'd0 ? pixel_slot_tag0 :
        visual_candidate_slot == 2'd1 ? pixel_slot_tag1 :
        visual_candidate_slot == 2'd2 ? pixel_slot_tag2 : pixel_slot_tag3;
    wire visual_candidate_ready =
        pixel_slot_valid[visual_candidate_slot] &&
        visual_candidate_tag == visual_candidate_line;
    wire visual_select_next_line = pixel_x == OUTPUT_WIDTH - 1 &&
        (pixel_y < OUTPUT_HEIGHT - 1 || pixel_y == TOTAL_HEIGHT - 1);
    wire [54:0] visual_candidate_data =
        visual_candidate_slot == 2'd0 ? line_visual_slot0 :
        visual_candidate_slot == 2'd1 ? line_visual_slot1 :
        visual_candidate_slot == 2'd2 ? line_visual_slot2 : line_visual_slot3;
    reg [54:0] pixel_line_visual_q;
    always @(posedge pixel_clk) begin
        if (pixel_reset)
            pixel_line_visual_q <= 55'd0;
        else if (visual_select_next_line && visual_candidate_ready)
            pixel_line_visual_q <= visual_candidate_data;
    end

    wire sprite_enable_line_pixel;
    wire framebuffer_enable_line_pixel;
    wire framebuffer_key_enable_line_pixel;
    wire [31:0] framebuffer_key_line_pixel;
    wire tile0_enable_line_pixel;
    wire tile0_above_line_pixel;
    wire [7:0] tile0_opacity_line_pixel;
    wire tile1_enable_line_pixel;
    wire tile1_above_line_pixel;
    wire [7:0] tile1_opacity_line_pixel;
    assign {
        sprite_enable_line_pixel,
        framebuffer_enable_line_pixel,
        framebuffer_key_enable_line_pixel,
        framebuffer_key_line_pixel,
        tile0_enable_line_pixel,
        tile0_above_line_pixel,
        tile0_opacity_line_pixel,
        tile1_enable_line_pixel,
        tile1_above_line_pixel,
        tile1_opacity_line_pixel
    } = pixel_line_visual_q;

    reg [23:0] backdrop_rgb_pixel_active;
    reg backdrop_pixel_mutated;
    wire copper_backdrop_event = copper_pixel_event_valid &&
        copper_pixel_event_ready && !copper_pixel_event_irq &&
        copper_pixel_event_target == 16'h0018;
    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            backdrop_rgb_pixel_active <= 24'd0;
            backdrop_pixel_mutated <= 1'b0;
        end else begin
            if (pixel_x == 11'd0 && pixel_y == OUTPUT_HEIGHT)
                backdrop_pixel_mutated <= 1'b0;
            if (!backdrop_pixel_mutated)
                backdrop_rgb_pixel_active <= backdrop_rgb_pixel_baseline;
            if (copper_backdrop_event) begin
                backdrop_rgb_pixel_active <= copper_pixel_event_data[23:0];
                backdrop_pixel_mutated <= 1'b1;
            end
        end
    end
    wire [23:0] backdrop_rgb_pixel_effective = copper_backdrop_event ?
        copper_pixel_event_data[23:0] : backdrop_rgb_pixel_active;

    wire prefetch_current = pixel_x < OUTPUT_WIDTH - OUTPUT_PREFETCH;
    wire prefetch_next = pixel_x >= TOTAL_WIDTH - OUTPUT_PREFETCH;
    wire current_line_active = pixel_y < OUTPUT_HEIGHT;
    wire next_line_active = pixel_y < OUTPUT_HEIGHT - 1 ||
                            pixel_y == TOTAL_HEIGHT - 1;
    assign line_read_x = prefetch_current ?
        pixel_x + OUTPUT_PREFETCH :
        prefetch_next ? pixel_x - (TOTAL_WIDTH - OUTPUT_PREFETCH) : 11'd0;
    wire line_source_request = scene_enable_pixel &&
        pixel_line_available &&
        ((prefetch_current && current_line_active) ||
         (prefetch_next && next_line_active));
    wire [9:0] line_source_y = prefetch_current ? pixel_y :
        (pixel_y == TOTAL_HEIGHT - 1 ? 10'd0 : pixel_y + 10'd1);
    reg line_source_valid_q;
    reg [10:0] line_source_x_q;
    reg [9:0] line_source_y_q;
    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            line_source_valid_q <= 1'b0;
            line_source_x_q <= 11'd0;
            line_source_y_q <= 10'd0;
        end else begin
            line_source_valid_q <= line_source_request;
            line_source_x_q <= line_read_x;
            line_source_y_q <= line_source_y;
        end
    end

    wire copper_framebuffer_palette_event = !copper_pixel_event_irq &&
        copper_pixel_event_target >= 16'h1000 &&
        copper_pixel_event_target <= 16'h13fc;
    wire copper_tile_palette_event = !copper_pixel_event_irq &&
        copper_pixel_event_target >= 16'h2000 &&
        copper_pixel_event_target <= 16'h5ffc;
    wire copper_palette_event = copper_framebuffer_palette_event ||
                                copper_tile_palette_event;
    wire [15:0] copper_framebuffer_palette_offset =
        copper_pixel_event_target - 16'h1000;
    wire [15:0] copper_tile_palette_offset =
        copper_pixel_event_target - 16'h2000;
    assign copper_pixel_event_ready = copper_pixel_event_irq ||
        copper_pixel_event_target == 16'h0018 ||
        (copper_palette_event && palette_copper_write_ready);
    assign copper_exact_enqueue_valid = copper_irq_event ||
        (copper_move_valid && copper_move_class == 2'd0);

    astra_copper_pixel_events #(
        .ADDR_WIDTH(12),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT)
    ) copper_pixel_events_i (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .enqueue_frame(frame_toggle_sync),
        .enqueue_irq(copper_irq_event),
        .enqueue_y(copper_irq_event ? copper_irq_beam_y :
                   copper_move_beam_y),
        .enqueue_x(copper_irq_event ? copper_irq_beam_x :
                   copper_move_beam_x),
        .enqueue_target(copper_irq_event ? 16'd0 : copper_move_target),
        .enqueue_data(copper_irq_event ? {16'd0, copper_irq_sources} :
                      copper_move_data),
        .enqueue_valid(copper_exact_enqueue_valid),
        .enqueue_ready(copper_exact_enqueue_ready),
        .enqueue_level(copper_exact_enqueue_level),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_frame(frame_toggle_pixel),
        .source_valid(line_source_valid_q),
        .source_y(line_source_y_q),
        .source_x(line_source_x_q),
        .event_irq(copper_pixel_event_irq),
        .event_y(copper_pixel_event_y),
        .event_x(copper_pixel_event_x),
        .event_target(copper_pixel_event_target),
        .event_data(copper_pixel_event_data),
        .event_valid(copper_pixel_event_valid),
        .event_ready(copper_pixel_event_ready),
        .event_level(copper_pixel_event_level),
        .overflow(copper_exact_overflow),
        .stale_event(copper_exact_stale),
        .late_event(copper_exact_late)
    );

    reg copper_irq_delivery_toggle_pixel;
    (* ASYNC_REG = "TRUE" *) reg copper_irq_delivery_meta;
    (* ASYNC_REG = "TRUE" *) reg copper_irq_delivery_sync;
    reg copper_irq_delivery_seen;
    always @(posedge pixel_clk) begin
        if (pixel_reset)
            copper_irq_delivery_toggle_pixel <= 1'b0;
        else if (copper_pixel_event_valid && copper_pixel_event_ready &&
                 copper_pixel_event_irq)
            copper_irq_delivery_toggle_pixel <=
                ~copper_irq_delivery_toggle_pixel;
    end
    always @(posedge build_clk) begin
        if (build_reset) begin
            copper_irq_delivery_meta <= 1'b0;
            copper_irq_delivery_sync <= 1'b0;
            copper_irq_delivery_seen <= 1'b0;
        end else begin
            copper_irq_delivery_meta <= copper_irq_delivery_toggle_pixel;
            copper_irq_delivery_sync <= copper_irq_delivery_meta;
            copper_irq_delivery_seen <= copper_irq_delivery_sync;
        end
    end
    assign copper_irq_delivery_build =
        copper_irq_delivery_sync != copper_irq_delivery_seen;

    wire [7:0] framebuffer_palette_index;
    wire [31:0] framebuffer_palette_argb;
    wire [3:0] tile0_palette_read_bank;
    wire [7:0] tile0_palette_read_index;
    wire [31:0] tile0_palette_argb;
    wire [3:0] tile1_palette_read_bank;
    wire [7:0] tile1_palette_read_index;
    wire [31:0] tile1_palette_argb;

    astra_palette_store palette_i (
        .control_clk(build_clk),
        .control_reset(build_reset),
        .baseline_restore_start(copper_baseline_restore),
        .baseline_restore_busy(palette_restore_busy),
        .baseline_restore_done(palette_restore_done),
        .host_write_ready(palette_host_write_ready),
        .framebuffer_write_enable(framebuffer_palette_write_enable),
        .framebuffer_write_index(framebuffer_palette_write_index),
        .framebuffer_write_argb(framebuffer_palette_write_argb),
        .tile_write_enable(tile_palette_write_enable),
        .tile_write_bank(tile_palette_write_bank),
        .tile_write_index(tile_palette_write_index),
        .tile_write_argb(tile_palette_write_argb),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .copper_write_enable(copper_pixel_event_valid &&
                             copper_pixel_event_ready &&
                             copper_palette_event),
        .copper_write_tile(copper_tile_palette_event),
        .copper_write_bank(copper_tile_palette_offset[13:10]),
        .copper_write_index(
            copper_framebuffer_palette_event ?
                copper_framebuffer_palette_offset[9:2] :
                copper_tile_palette_offset[9:2]),
        .copper_write_argb(copper_pixel_event_data),
        .copper_write_ready(palette_copper_write_ready),
        .framebuffer_read_index(framebuffer_palette_index),
        .framebuffer_read_argb(framebuffer_palette_argb),
        .tile0_read_bank(tile0_palette_read_bank),
        .tile0_read_index(tile0_palette_read_index),
        .tile0_read_argb(tile0_palette_argb),
        .tile1_read_bank(tile1_palette_read_bank),
        .tile1_read_index(tile1_palette_read_index),
        .tile1_read_argb(tile1_palette_argb)
    );

    wire compositor_output_valid;
    wire [23:0] compositor_output_rgb;
    astra_pixel_compositor compositor_i (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_valid(line_source_valid_q),
        .backdrop_rgb(backdrop_rgb_pixel_effective),
        .sprite_enable(sprite_enable_line_pixel),
        .sprite_behind_premult_argb(sprite_behind_argb),
        .sprite_front_premult_argb(sprite_front_argb),
        .framebuffer_enable(framebuffer_enable_line_pixel),
        .framebuffer_format(framebuffer_format_pixel),
        .framebuffer_pixel_valid(framebuffer_pixel_valid),
        .framebuffer_pixel_value(framebuffer_pixel_value),
        .framebuffer_key_enable(framebuffer_key_enable_line_pixel),
        .framebuffer_key(framebuffer_key_line_pixel),
        .framebuffer_palette_index(framebuffer_palette_index),
        .framebuffer_palette_argb(framebuffer_palette_argb),
        .tile0_enable(tile0_enable_line_pixel),
        .tile0_above_framebuffer(tile0_above_line_pixel),
        .tile0_opacity(tile0_opacity_line_pixel),
        .tile0_pixel_valid(tile0_pixel_valid),
        .tile0_palette_bank(tile0_pixel_bank),
        .tile0_palette_index(tile0_pixel_index),
        .tile0_palette_read_bank(tile0_palette_read_bank),
        .tile0_palette_read_index(tile0_palette_read_index),
        .tile0_palette_argb(tile0_palette_argb),
        .tile1_enable(tile1_enable_line_pixel),
        .tile1_above_framebuffer(tile1_above_line_pixel),
        .tile1_opacity(tile1_opacity_line_pixel),
        .tile1_pixel_valid(tile1_pixel_valid),
        .tile1_palette_bank(tile1_pixel_bank),
        .tile1_palette_index(tile1_pixel_index),
        .tile1_palette_read_bank(tile1_palette_read_bank),
        .tile1_palette_read_index(tile1_palette_read_index),
        .tile1_palette_argb(tile1_palette_argb),
        .output_valid(compositor_output_valid),
        .output_rgb(compositor_output_rgb)
    );

    astra_boot_text_overlay #(
        .FONT_HEX(BOOT_FONT_HEX),
        .COLS(BOOT_TEXT_COLS),
        .ROWS(BOOT_TEXT_ROWS),
        .ORIGIN_X(BOOT_TEXT_ORIGIN_X),
        .ORIGIN_Y(BOOT_TEXT_ORIGIN_Y),
        .CELL_WIDTH(BOOT_TEXT_CELL_WIDTH),
        .ROW_PITCH(BOOT_TEXT_ROW_PITCH)
    ) boot_text_i (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .shadow_enable(boot_text_shadow_enable),
        .write_strobe(boot_text_write_enable),
        .write_index(boot_text_write_index),
        .write_cell(boot_text_write_cell),
        .write_ready(boot_text_write_ready),
        .commit_strobe(boot_text_commit_enable),
        .commit_ready(boot_text_commit_ready),
        .active_enable(boot_text_active_enable),
        .generation(boot_text_generation),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_frame_boundary(
            pixel_x == 11'd0 && pixel_y == OUTPUT_HEIGHT),
        .input_valid(compositor_output_valid),
        .pixel_x(pixel_x),
        .pixel_y(pixel_y),
        .input_rgb(compositor_output_rgb),
        .output_valid(pixel_output_valid),
        .output_rgb(pixel_output_rgb)
    );

    wire unused_status = &{
        1'b0,
        commit_pending_status,
        pixel_slot_valid,
        pixel_slot_tag0,
        pixel_slot_tag1,
        pixel_slot_tag2,
        pixel_slot_tag3,
        framebuffer_slot_valid,
        framebuffer_config_error,
        framebuffer_fetch_error,
        framebuffer_deadline_error,
        framebuffer_completed_slot,
        framebuffer_build_cycles,
        framebuffer_read_bytes,
        tile0_slot_valid,
        tile0_config_error,
        tile0_descriptor_error,
        tile0_fetch_error,
        tile0_completed_slot,
        tile0_build_cycles,
        tile0_map_read_bytes,
        tile0_pattern_read_bytes,
        tile1_slot_valid,
        tile1_config_error,
        tile1_descriptor_error,
        tile1_fetch_error,
        tile1_completed_slot,
        tile1_build_cycles,
        tile1_map_read_bytes,
        tile1_pattern_read_bytes,
        sprite_completed_slot
    };
endmodule

`default_nettype wire
