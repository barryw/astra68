`timescale 1ns/1ps
`default_nettype none

module tb_astra_graphics_control;
    wire palette_write_ready = 1'b1;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg reset = 1'b1;
    reg frame_boundary = 1'b0;
    reg commit_safe = 1'b1;
    wire scene_changed;
    wire [31:0] active_generation;
    wire commit_pending_status;
    wire commit_quiesce;
    wire [31:0] commit_errors;
    wire [31:0] commit_deferrals;
    wire scene_enable;
    wire [23:0] backdrop_rgb;
    wire framebuffer_enable;
    wire [1:0] framebuffer_format;
    wire [31:0] framebuffer_base;
    wire [31:0] framebuffer_pitch;
    wire [12:0] framebuffer_width;
    wire [12:0] framebuffer_height;
    wire signed [31:0] framebuffer_viewport_x;
    wire signed [31:0] framebuffer_viewport_y;
    wire framebuffer_wrap_x;
    wire framebuffer_wrap_y;
    wire framebuffer_key_enable;
    wire [31:0] framebuffer_key;

    wire tile0_enable;
    wire tile0_above_framebuffer;
    wire [7:0] tile0_opacity;
    wire tile0_tile_16;
    wire tile0_index_8;
    wire [3:0] tile0_map_width_log2;
    wire [3:0] tile0_map_height_log2;
    wire tile0_wrap_x;
    wire tile0_wrap_y;
    wire tile0_transparent_enable;
    wire [7:0] tile0_transparent_index;
    wire signed [31:0] tile0_scroll_x;
    wire signed [31:0] tile0_scroll_y;
    wire [31:0] tile0_map_base;
    wire [31:0] tile0_pattern_base;
    wire [16:0] tile0_tile_count;
    wire tile1_enable;
    wire tile1_above_framebuffer;
    wire [7:0] tile1_opacity;
    wire tile1_tile_16;
    wire tile1_index_8;
    wire [3:0] tile1_map_width_log2;
    wire [3:0] tile1_map_height_log2;
    wire tile1_wrap_x;
    wire tile1_wrap_y;
    wire tile1_transparent_enable;
    wire [7:0] tile1_transparent_index;
    wire signed [31:0] tile1_scroll_x;
    wire signed [31:0] tile1_scroll_y;
    wire [31:0] tile1_map_base;
    wire [31:0] tile1_pattern_base;
    wire [16:0] tile1_tile_count;

    wire framebuffer_palette_write_enable;
    wire [7:0] framebuffer_palette_write_index;
    wire [31:0] framebuffer_palette_write_argb;
    wire tile_palette_write_enable;
    wire [3:0] tile_palette_write_bank;
    wire [7:0] tile_palette_write_index;
    wire [31:0] tile_palette_write_argb;

    wire sprite_enable;
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

    wire sprite_order_read_enable = 1'b0;
    wire [5:0] sprite_order_read_position = 6'd0;
    wire [5:0] sprite_order_read_index;
    wire sprite_descriptor_read_enable = 1'b0;
    wire [5:0] sprite_descriptor_read_index = 6'd0;
    wire [31:0] sprite_descriptor_word0;
    wire [31:0] sprite_descriptor_word1;
    wire [31:0] sprite_descriptor_word2;
    wire [31:0] sprite_descriptor_word3;
    wire [31:0] sprite_descriptor_word4;
    wire [31:0] sprite_descriptor_word5;
    wire [31:0] sprite_descriptor_word6;
wire [31:0] sprite_scale_step_x;
    wire [63:0] sprite_collision_compatible;
    wire [31:0] sprite_palette0_argb;
    wire [31:0] sprite_palette1_argb;
    wire [31:0] sprite_palette2_argb;
    wire [31:0] sprite_palette3_argb;
    wire boot_text_shadow_enable;
    wire boot_text_write_enable;
    wire [7:0] boot_text_write_index;
    wire [15:0] boot_text_write_cell;
    wire boot_text_commit_enable;
    reg boot_text_write_ready = 1'b1;
    reg boot_text_commit_ready = 1'b1;
    reg boot_text_active_enable = 1'b0;
    reg [31:0] boot_text_generation = 32'd0;
    wire render_enable;
    wire render_queue_rebase;
    wire render_soft_reset;
    wire [31:0] render_submission_ring_offset;
    wire [10:0] render_submission_producer;
    reg [10:0] render_submission_consumer = 11'd3;
    reg copper_dispatch_valid = 1'b0;
    reg [10:0] copper_dispatch_submission_producer = 11'd0;
    wire copper_dispatch_ready;
    wire copper_dispatch_allowed;
    wire [31:0] render_completion_ring_offset;
    reg [10:0] render_completion_producer = 11'd5;
    wire [10:0] render_completion_consumer;
    wire [31:0] render_resource_generation;
    reg render_busy = 1'b0;
    reg render_engine_reset_active = 1'b0;
    reg render_configuration_fault = 1'b0;
    reg render_completion_irq = 1'b0;
    reg [31:0] render_retired_fence = 32'h10203040;
    reg [31:0] render_commands_submitted = 32'd11;
    reg [31:0] render_commands_completed = 32'd10;
    reg [31:0] render_commands_failed = 32'd2;
    reg [31:0] render_backpressure_cycles = 32'd33;
    reg [31:0] render_timeout_count = 32'd4;
    reg [31:0] render_reset_count = 32'd5;
    reg [31:0] render_last_fault_detail = 32'hbad0c0de;
    wire render_interrupt;
    wire render_protected0_valid;
    wire [31:0] render_protected0_offset;
    wire [31:0] render_protected0_bytes;
    wire render_protected1_valid;
    wire [31:0] render_protected1_offset;
    wire [31:0] render_protected1_bytes;

    reg [31:0] s_axi_awaddr = 32'd0;
    reg [2:0] s_axi_awprot = 3'd0;
    reg s_axi_awvalid = 1'b0;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata = 32'd0;
    reg [3:0] s_axi_wstrb = 4'd0;
    reg s_axi_wvalid = 1'b0;
    wire s_axi_wready;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready = 1'b0;
    reg [31:0] s_axi_araddr = 32'd0;
    reg [2:0] s_axi_arprot = 3'd0;
    reg s_axi_arvalid = 1'b0;
    wire s_axi_arready;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rvalid;
    reg s_axi_rready = 1'b0;

    astra_sprite_scene_store sprite_scene_i (
        .clk(clk),
        .reset(reset),
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
        .baseline_restore_start(1'b0),
        .baseline_restore_busy(),
        .baseline_restore_done(),
        .copper_palette_write_enable(1'b0),
        .copper_palette_write_bank(4'd0),
        .copper_palette_write_index(8'd0),
        .copper_palette_write_argb(32'd0),
        .copper_palette_write_ready(),
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
        .palette0_read_bank(4'd0),
        .palette0_read_index(8'd0),
        .palette0_read_argb(sprite_palette0_argb),
        .palette1_read_bank(4'd0),
        .palette1_read_index(8'd0),
        .palette1_read_argb(sprite_palette1_argb),
        .palette2_read_bank(4'd0),
        .palette2_read_index(8'd0),
        .palette2_read_argb(sprite_palette2_argb),
        .palette3_read_bank(4'd0),
        .palette3_read_index(8'd0),
        .palette3_read_argb(sprite_palette3_argb)
    );

    astra_graphics_control dut (
        .clk(clk),
        .reset(reset),
        .frame_boundary(frame_boundary),
        .commit_safe(commit_safe),
        .scene_changed(scene_changed),
        .active_generation(active_generation),
        .commit_pending_status(commit_pending_status),
        .commit_quiesce(commit_quiesce),
        .commit_errors(commit_errors),
        .commit_deferrals(commit_deferrals),
        .scene_enable(scene_enable),
        .backdrop_rgb(backdrop_rgb),
        .framebuffer_enable(framebuffer_enable),
        .framebuffer_format(framebuffer_format),
        .framebuffer_base(framebuffer_base),
        .framebuffer_pitch(framebuffer_pitch),
        .framebuffer_width(framebuffer_width),
        .framebuffer_height(framebuffer_height),
        .framebuffer_viewport_x(framebuffer_viewport_x),
        .framebuffer_viewport_y(framebuffer_viewport_y),
        .framebuffer_wrap_x(framebuffer_wrap_x),
        .framebuffer_wrap_y(framebuffer_wrap_y),
        .framebuffer_key_enable(framebuffer_key_enable),
        .framebuffer_key(framebuffer_key),
        .tile0_enable(tile0_enable),
        .tile0_above_framebuffer(tile0_above_framebuffer),
        .tile0_opacity(tile0_opacity),
        .tile0_tile_16(tile0_tile_16),
        .tile0_index_8(tile0_index_8),
        .tile0_map_width_log2(tile0_map_width_log2),
        .tile0_map_height_log2(tile0_map_height_log2),
        .tile0_wrap_x(tile0_wrap_x),
        .tile0_wrap_y(tile0_wrap_y),
        .tile0_transparent_enable(tile0_transparent_enable),
        .tile0_transparent_index(tile0_transparent_index),
        .tile0_scroll_x(tile0_scroll_x),
        .tile0_scroll_y(tile0_scroll_y),
        .tile0_map_base(tile0_map_base),
        .tile0_pattern_base(tile0_pattern_base),
        .tile0_tile_count(tile0_tile_count),
        .tile1_enable(tile1_enable),
        .tile1_above_framebuffer(tile1_above_framebuffer),
        .tile1_opacity(tile1_opacity),
        .tile1_tile_16(tile1_tile_16),
        .tile1_index_8(tile1_index_8),
        .tile1_map_width_log2(tile1_map_width_log2),
        .tile1_map_height_log2(tile1_map_height_log2),
        .tile1_wrap_x(tile1_wrap_x),
        .tile1_wrap_y(tile1_wrap_y),
        .tile1_transparent_enable(tile1_transparent_enable),
        .tile1_transparent_index(tile1_transparent_index),
        .tile1_scroll_x(tile1_scroll_x),
        .tile1_scroll_y(tile1_scroll_y),
        .tile1_map_base(tile1_map_base),
        .tile1_pattern_base(tile1_pattern_base),
        .tile1_tile_count(tile1_tile_count),
        .sprite_enable(sprite_enable),
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
        .sprite_builder_busy(1'b0),
        .sprite_slot_valid(4'd0),
        .sprite_fetch_error(1'b0),
        .sprite_deadline_error(1'b0),
        .sprite_build_cycles(32'd0),
        .sprite_max_build_cycles(32'h00000ed1),
        .sprite_axi_error_count(32'h00000012),
        .sprite_deadline_error_count(32'h00000034),
        .sprite_read_bytes(32'd0),
        .sprite_overflow_bitmap(64'd0),
        .sprite_overflow_line(10'd0),
        .sprite_overflow_count(32'd0),
        .sprite_pixels_admitted(32'd0),
        .sprite_pixels_dropped(32'd0),
        .sprite_collision_read_row(sprite_collision_read_row),
        .sprite_collision_read_data(64'd0),
        .sprite_collision_frame(32'd0),
        .sprite_collision_event(1'b0),
        .framebuffer_palette_write_enable(
            framebuffer_palette_write_enable),
        .framebuffer_palette_write_index(
            framebuffer_palette_write_index),
        .framebuffer_palette_write_argb(
            framebuffer_palette_write_argb),
        .tile_palette_write_enable(tile_palette_write_enable),
        .tile_palette_write_bank(tile_palette_write_bank),
        .tile_palette_write_index(tile_palette_write_index),
        .tile_palette_write_argb(tile_palette_write_argb),
        .palette_write_ready(palette_write_ready),
        .boot_text_shadow_enable(boot_text_shadow_enable),
        .boot_text_write_enable(boot_text_write_enable),
        .boot_text_write_index(boot_text_write_index),
        .boot_text_write_cell(boot_text_write_cell),
        .boot_text_commit_enable(boot_text_commit_enable),
        .boot_text_write_ready(boot_text_write_ready),
        .boot_text_commit_ready(boot_text_commit_ready),
        .boot_text_active_enable(boot_text_active_enable),
        .boot_text_generation(boot_text_generation),
        .render_enable(render_enable),
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
        .render_interrupt(render_interrupt),
        .render_protected0_valid(render_protected0_valid),
        .render_protected0_offset(render_protected0_offset),
        .render_protected0_bytes(render_protected0_bytes),
        .render_protected1_valid(render_protected1_valid),
        .render_protected1_offset(render_protected1_offset),
        .render_protected1_bytes(render_protected1_bytes),
        .s_axi_awaddr(s_axi_awaddr),
        .s_axi_awprot(s_axi_awprot),
        .s_axi_awvalid(s_axi_awvalid),
        .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata),
        .s_axi_wstrb(s_axi_wstrb),
        .s_axi_wvalid(s_axi_wvalid),
        .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp),
        .s_axi_bvalid(s_axi_bvalid),
        .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr),
        .s_axi_arprot(s_axi_arprot),
        .s_axi_arvalid(s_axi_arvalid),
        .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata),
        .s_axi_rresp(s_axi_rresp),
        .s_axi_rvalid(s_axi_rvalid),
        .s_axi_rready(s_axi_rready)
    );

    integer framebuffer_palette_writes = 0;
    integer tile_palette_writes = 0;
    integer boot_text_writes = 0;
    integer boot_text_commits = 0;
    integer boot_text_write_countdown = 0;
    integer boot_text_commit_countdown = 0;
    reg boot_text_auto_handshake = 1'b1;
    integer render_rebase_pulses = 0;
    integer render_soft_reset_pulses = 0;
    always @(posedge clk) begin
        if (render_queue_rebase)
            render_rebase_pulses <= render_rebase_pulses + 1;
        if (render_soft_reset)
            render_soft_reset_pulses <= render_soft_reset_pulses + 1;
        if (framebuffer_palette_write_enable) begin
            framebuffer_palette_writes <= framebuffer_palette_writes + 1;
            if (framebuffer_palette_write_index != 8'h12 ||
                framebuffer_palette_write_argb != 32'h80abcdef)
                $fatal(1, "wrong framebuffer palette write");
        end
        if (tile_palette_write_enable) begin
            tile_palette_writes <= tile_palette_writes + 1;
            if (tile_palette_write_bank != 4'h5 ||
                tile_palette_write_index != 8'h34 ||
                tile_palette_write_argb != 32'hc0123456)
                $fatal(1, "wrong tile palette write");
        end
        if (boot_text_write_enable) begin
            boot_text_writes <= boot_text_writes + 1;
            if (boot_text_write_index != 8'd35 ||
                boot_text_write_cell != 16'h0141)
                $fatal(1, "wrong boot text write");
            if (boot_text_auto_handshake) begin
                boot_text_write_ready <= 1'b0;
                boot_text_write_countdown <= 3;
            end
        end
        if (boot_text_commit_enable) begin
            boot_text_commits <= boot_text_commits + 1;
            if (boot_text_auto_handshake) begin
                boot_text_commit_ready <= 1'b0;
                boot_text_commit_countdown <= 3;
            end
        end
        if (boot_text_write_countdown > 0) begin
            boot_text_write_countdown <= boot_text_write_countdown - 1;
            if (boot_text_write_countdown == 1)
                boot_text_write_ready <= 1'b1;
        end
        if (boot_text_commit_countdown > 0) begin
            boot_text_commit_countdown <= boot_text_commit_countdown - 1;
            if (boot_text_commit_countdown == 1)
                boot_text_commit_ready <= 1'b1;
        end
    end

    task automatic send_aw(input [31:0] address);
        begin
            @(negedge clk);
            s_axi_awaddr = address;
            s_axi_awvalid = 1'b1;
            while (!s_axi_awready)
                @(negedge clk);
            @(posedge clk);
            @(negedge clk);
            s_axi_awvalid = 1'b0;
        end
    endtask

    task automatic send_w(input [31:0] data, input [3:0] strobes);
        begin
            @(negedge clk);
            s_axi_wdata = data;
            s_axi_wstrb = strobes;
            s_axi_wvalid = 1'b1;
            while (!s_axi_wready)
                @(negedge clk);
            @(posedge clk);
            @(negedge clk);
            s_axi_wvalid = 1'b0;
        end
    endtask

    task automatic collect_b(input [1:0] expected_response);
        integer cycles;
        begin
            cycles = 0;
            while (!s_axi_bvalid) begin
                @(posedge clk);
                #1;
                cycles = cycles + 1;
                if (cycles > 20000)
                    $fatal(1, "AXI write response timed out");
            end
            if (s_axi_bresp !== expected_response)
                $fatal(1, "BRESP=%b expected=%b",
                       s_axi_bresp, expected_response);
            @(negedge clk);
            s_axi_bready = 1'b1;
            @(posedge clk);
            @(negedge clk);
            s_axi_bready = 1'b0;
        end
    endtask

    task automatic axi_write(
        input [31:0] address,
        input [31:0] data,
        input [3:0] strobes,
        input integer data_first,
        input [1:0] expected_response
    );
        begin
            if (data_first) begin
                send_w(data, strobes);
                send_aw(address);
            end else begin
                send_aw(address);
                send_w(data, strobes);
            end
            collect_b(expected_response);
        end
    endtask

    task automatic axi_read(
        input [31:0] address,
        input [31:0] expected_data,
        input [1:0] expected_response
    );
        integer cycles;
        begin
            @(negedge clk);
            s_axi_araddr = address;
            s_axi_arvalid = 1'b1;
            while (!s_axi_arready)
                @(negedge clk);
            @(posedge clk);
            @(negedge clk);
            s_axi_arvalid = 1'b0;
            cycles = 0;
            while (!s_axi_rvalid) begin
                @(posedge clk);
                #1;
                cycles = cycles + 1;
                if (cycles > 20)
                    $fatal(1, "AXI read response timed out");
            end
            if (s_axi_rresp !== expected_response ||
                s_axi_rdata !== expected_data)
                $fatal(1, "read %08x got %b/%08x expected %b/%08x",
                       address, s_axi_rresp, s_axi_rdata,
                       expected_response, expected_data);
            @(negedge clk);
            s_axi_rready = 1'b1;
            @(posedge clk);
            @(negedge clk);
            s_axi_rready = 1'b0;
        end
    endtask

    task automatic axi_read_backpressure;
        integer cycles;
        reg [31:0] held_data;
        reg [1:0] held_response;
        begin
            @(negedge clk);
            s_axi_araddr = 32'h00000004;
            s_axi_arvalid = 1'b1;
            while (!s_axi_arready)
                @(negedge clk);
            @(posedge clk);
            @(negedge clk);
            s_axi_arvalid = 1'b0;

            cycles = 0;
            while (!s_axi_rvalid) begin
                @(posedge clk);
                #1;
                cycles = cycles + 1;
                if (cycles > 20)
                    $fatal(1, "backpressured AXI read timed out");
            end
            held_data = s_axi_rdata;
            held_response = s_axi_rresp;
            if (held_data !== 32'h00010005 || held_response !== 2'b00)
                $fatal(1, "backpressured AXI read returned bad data");

            // Present a second request while the first response is held.
            // It must not be accepted, and the response must remain stable.
            @(negedge clk);
            s_axi_araddr = 32'h00000008;
            s_axi_arvalid = 1'b1;
            repeat (4) begin
                @(posedge clk);
                #1;
                if (!s_axi_rvalid || s_axi_rdata !== held_data ||
                    s_axi_rresp !== held_response)
                    $fatal(1, "AXI read response changed under backpressure");
                if (s_axi_arready)
                    $fatal(1, "AXI accepted a second outstanding read");
            end

            @(negedge clk);
            s_axi_rready = 1'b1;
            @(posedge clk);
            #1;
            @(negedge clk);
            s_axi_rready = 1'b0;
            s_axi_arvalid = 1'b0;
            if (s_axi_rvalid || !s_axi_arready)
                $fatal(1, "AXI read channel did not return to idle");
        end
    endtask

    task automatic pulse_frame;
        begin
            @(negedge clk);
            frame_boundary = 1'b1;
            @(posedge clk);
            #1;
            @(negedge clk);
            frame_boundary = 1'b0;
        end
    endtask

    task automatic wait_promotion;
        integer cycles;
        begin
            cycles = 0;
            while (!scene_changed && cycles < 20000) begin
                @(posedge clk);
                #1;
                cycles = cycles + 1;
            end
            if (!scene_changed)
                $fatal(1, "scene activation timed out");
        end
    endtask

    task automatic pulse_frame_and_wait_promotion;
        begin
            pulse_frame();
            wait_promotion();
        end
    endtask

    initial begin
        repeat (5) @(posedge clk);
        reset = 1'b0;

        axi_read(32'h00000000, 32'h41535452, 2'b00);
        axi_read(32'h00000004, 32'h00010005, 2'b00);
        axi_read(32'h00000008, 32'h000003ff, 2'b00);
        axi_read(32'h0000001c, 32'h18000000, 2'b00);
        axi_read(32'h00000020, 32'h20000000, 2'b00);
        axi_read(32'h00000003, 32'd0, 2'b11);
        axi_read_backpressure();
        $display("identity/read-decode pass");

        axi_read(32'h00000200, 32'd0, 2'b00);
        axi_read(32'h00000204, 32'd0, 2'b00);
        axi_read(32'h0000020c, 32'd3, 2'b00);
        axi_read(32'h00000210, 32'h00010000, 2'b00);
        axi_read(32'h00000214, 32'd5, 2'b00);
        axi_write(32'h00000204, 32'h00020000, 4'hf, 0, 2'b00);
        axi_write(32'h00000210, 32'h00030000, 4'hf, 1, 2'b00);
        axi_write(32'h0000021c, 32'h00000055, 4'hf, 0, 2'b00);
        axi_write(32'h00000208, 32'd7, 4'hf, 1, 2'b00);
        axi_write(32'h00000218, 32'd6, 4'hf, 0, 2'b00);
        axi_read(32'h00000204, 32'h00020000, 2'b00);
        axi_read(32'h00000208, 32'd7, 2'b00);
        axi_read(32'h00000218, 32'd6, 2'b00);
        axi_read(32'h0000021c, 32'h00000055, 2'b00);
        axi_read(32'h00000224, 32'h10203040, 2'b00);
        axi_read(32'h00000228, 32'hbad0c0de, 2'b00);
        axi_read(32'h0000022c, 32'd11, 2'b00);
        axi_read(32'h00000230, 32'd10, 2'b00);
        axi_read(32'h00000234, 32'd2, 2'b00);
        axi_read(32'h00000238, 32'd33, 2'b00);
        axi_read(32'h0000023c, 32'd4, 2'b00);
        axi_read(32'h00000240, 32'd5, 2'b00);
        axi_write(32'h00000200, 32'd3, 4'hf, 1, 2'b00);
        repeat (2) @(posedge clk);
        if (!render_enable || render_rebase_pulses != 1)
            $fatal(1, "render enable/rebase control failed");

        copper_dispatch_submission_producer = 11'd8;
        #1;
        if (copper_dispatch_allowed || copper_dispatch_ready)
            $fatal(1, "idle copper dispatch reported a result");
        @(negedge clk);
        copper_dispatch_valid = 1'b1;
        @(posedge clk);
        #1;
        if (!copper_dispatch_ready || !copper_dispatch_allowed)
            $fatal(1, "forward copper dispatch endpoint was not validated");
        @(posedge clk);
        #1;
        @(negedge clk);
        copper_dispatch_valid = 1'b0;
        @(posedge clk);
        #1;
        if (render_submission_producer != 11'd8)
            $fatal(1, "copper dispatch did not publish producer endpoint");
        if (copper_dispatch_allowed || copper_dispatch_ready)
            $fatal(1, "completed copper dispatch remained visible");

        @(negedge clk);
        copper_dispatch_valid = 1'b1;
        @(posedge clk);
        #1;
        if (!copper_dispatch_allowed || !copper_dispatch_ready)
            $fatal(1, "idempotent copper dispatch replay was rejected");
        @(posedge clk);
        @(negedge clk);
        copper_dispatch_valid = 1'b0;
        @(posedge clk);

        copper_dispatch_submission_producer = 11'd7;
        @(negedge clk);
        copper_dispatch_valid = 1'b1;
        @(posedge clk);
        #1;
        if (copper_dispatch_allowed || !copper_dispatch_ready)
            $fatal(1, "backward copper dispatch endpoint was accepted");
        @(posedge clk);
        @(negedge clk);
        copper_dispatch_valid = 1'b0;
        copper_dispatch_submission_producer = 11'd1028;
        @(negedge clk);
        copper_dispatch_valid = 1'b1;
        @(posedge clk);
        #1;
        if (copper_dispatch_allowed || !copper_dispatch_ready)
            $fatal(1, "overfull copper dispatch endpoint was accepted");
        @(posedge clk);
        @(negedge clk);
        copper_dispatch_valid = 1'b0;
        copper_dispatch_submission_producer = 11'd8;
        render_configuration_fault = 1'b1;
        @(negedge clk);
        copper_dispatch_valid = 1'b1;
        @(posedge clk);
        #1;
        if (copper_dispatch_allowed || !copper_dispatch_ready)
            $fatal(1, "dispatch ignored renderer configuration fault");
        @(posedge clk);
        @(negedge clk);
        copper_dispatch_valid = 1'b0;
        render_configuration_fault = 1'b0;
        $display("copper dispatch ring-boundary pass");

        axi_write(32'h00000204, 32'd0, 4'hf, 0, 2'b10);
        render_busy = 1'b1;
        axi_write(32'h00000200, 32'd3, 4'hf, 1, 2'b10);
        axi_write(32'h00000200, 32'd5, 4'hf, 0, 2'b00);
        repeat (2) @(posedge clk);
        if (render_soft_reset_pulses != 1)
            $fatal(1, "render soft reset pulse missing");
        render_busy = 1'b0;
        axi_write(32'h00000200, 32'd0, 4'hf, 1, 2'b00);
        render_busy = 1'b1;
        axi_write(32'h00000204, 32'h00040000, 4'hf, 0, 2'b10);
        axi_write(32'h00000210, 32'h00050000, 4'hf, 1, 2'b10);
        axi_write(32'h0000021c, 32'h00000066, 4'hf, 0, 2'b10);
        axi_read(32'h00000204, 32'h00020000, 2'b00);
        axi_read(32'h00000210, 32'h00030000, 2'b00);
        axi_read(32'h0000021c, 32'h00000055, 2'b00);
        render_busy = 1'b0;
        @(negedge clk);
        render_completion_irq = 1'b1;
        @(negedge clk);
        render_completion_irq = 1'b0;
        repeat (2) @(posedge clk);
        if (!render_interrupt)
            $fatal(1, "render completion interrupt was not retained");
        axi_read(32'h00000244, 32'd1, 2'b00);
        axi_write(32'h00000244, 32'd1, 4'hf, 0, 2'b00);
        axi_read(32'h00000244, 32'd0, 2'b00);
        if (render_interrupt)
            $fatal(1, "render completion interrupt did not clear");
        $display("render MMIO/interrupt programming pass");

        axi_write(32'h00000100, 32'h00000012, 4'hf, 0, 2'b00);
        axi_write(32'h00000104, 32'h80abcdef, 4'hf, 1, 2'b00);
        axi_write(32'h00000108, 32'h00000534, 4'hf, 1, 2'b00);
        axi_write(32'h0000010c, 32'hc0123456, 4'hf, 0, 2'b00);
        if (framebuffer_palette_writes != 1 || tile_palette_writes != 1)
            $fatal(1, "palette writes were not emitted exactly once");
        $display("disabled-scene palette programming pass");

        axi_write(32'h00000180, 32'h80000000, 4'hf, 0, 2'b10);
        axi_read(32'h00000180, 32'h00000000, 2'b00);
        axi_write(32'h00000180, 32'h00000001, 4'hf, 0, 2'b00);
        axi_write(32'h00000184, 32'h00000103, 4'hf, 1, 2'b00);
        axi_write(32'h00000188, 32'hfff00010, 4'hf, 0, 2'b00);
        axi_write(32'h0000018c, 32'h00000234, 4'hf, 1, 2'b00);
        axi_write(32'h00000190, 32'h80fedcba, 4'hf, 0, 2'b00);
        axi_read(32'h00000184, 32'h00000103, 2'b00);
        axi_read(32'h0000018c, 32'h00000234, 2'b00);
        axi_read(32'h000001c8, 32'h00000012, 2'b00);
        axi_read(32'h000001cc, 32'h00000034, 2'b00);
        axi_read(32'h000001d0, 32'h00000ed1, 2'b00);
        $display("sprite MMIO/editable programming pass");

        axi_read(32'h0000014c, 32'h00000003, 2'b00);
        axi_read(32'h00000150, 32'h00000000, 2'b00);
        axi_read(32'h00000154, 32'h04242010, 2'b00);
        axi_read(32'h00000158, 32'h01f00108, 2'b00);
        axi_write(32'h00000140, 32'h00000001, 4'hf, 0, 2'b00);
        axi_write(32'h00000144, 32'd35, 4'hf, 1, 2'b00);
        axi_write(32'h00000148, 32'h00000141, 4'hf, 0, 2'b00);
        axi_read(32'h00000144, 32'd36, 2'b00);
        if (!boot_text_shadow_enable || boot_text_writes != 1)
            $fatal(1, "boot text cell was not accepted exactly once");
        axi_write(32'h0000014c, 32'h00000001, 4'hf, 1, 2'b00);
        if (boot_text_commits != 1)
            $fatal(1, "boot text commit was not emitted exactly once");
        repeat (8) @(posedge clk);
        boot_text_active_enable = 1'b1;
        boot_text_generation = 32'd7;
        axi_read(32'h0000014c, 32'h00000007, 2'b00);
        axi_read(32'h00000150, 32'h00000007, 2'b00);

        boot_text_auto_handshake = 1'b0;
        boot_text_write_ready = 1'b0;
        axi_write(32'h00000148, 32'h00000142, 4'hf, 0, 2'b10);
        boot_text_write_ready = 1'b1;
        boot_text_commit_ready = 1'b0;
        axi_write(32'h0000014c, 32'h00000001, 4'hf, 0, 2'b10);
        boot_text_commit_ready = 1'b1;
        axi_write(32'h00000144, 32'd144, 4'hf, 0, 2'b10);
        axi_write(32'h00000148, 32'h00000441, 4'hf, 0, 2'b10);
        if (boot_text_writes != 1 || boot_text_commits != 1)
            $fatal(1, "rejected boot text operation emitted a pulse");
        $display("boot text MMIO/flow-control pass");

        // Byte strobes update only the selected backdrop bytes.
        axi_write(32'h00000018, 32'h00aabbcc, 4'b0011, 0, 2'b00);
        axi_read(32'h00000018, 32'h0010bbcc, 2'b00);

        axi_write(32'h0000000c, 32'h00000001, 4'hf, 1, 2'b00);
        axi_write(32'h00000010, 32'h00000001, 4'hf, 0, 2'b00);
        if (!commit_pending_status)
            $fatal(1, "valid scene was not queued");
        if (!render_protected1_valid ||
            render_protected1_offset != 32'd0 ||
            render_protected1_bytes != 32'h001c2000)
            $fatal(1, "pending framebuffer protection is incorrect");
        // Editing SHADOW after submission must not mutate PENDING.
        axi_write(32'h00000018, 32'h00ddee11, 4'hf, 0, 2'b00);
        axi_read(32'h00000018, 32'h00ddee11, 2'b00);
        commit_safe = 1'b0;
        pulse_frame();
        if (!commit_pending_status || !commit_quiesce ||
            commit_deferrals != 32'd0 || scene_enable)
            $fatal(1, "unsafe frame boundary did not latch quiesce");
        pulse_frame();
        if (commit_deferrals != 32'd1)
            $fatal(1, "missed-frame deferral was not counted");
        commit_safe = 1'b1;
        wait_promotion();
        if (commit_pending_status || !scene_enable ||
            commit_quiesce ||
            active_generation != 32'd1 || !scene_changed ||
            backdrop_rgb != 24'h10bbcc ||
            !sprite_enable ||
            !framebuffer_enable || framebuffer_format != 2'd1 ||
            framebuffer_base != 32'h18000000 ||
            framebuffer_pitch != 32'd2560 ||
            framebuffer_width != 13'd1280 ||
            framebuffer_height != 13'd720)
            $fatal(1, "frame-boundary promotion failed");
        if (!render_protected0_valid ||
            render_protected0_offset != 32'd0 ||
            render_protected0_bytes != 32'h001c2000)
            $fatal(1, "active framebuffer protection is incorrect");
        $display("validated/deferred/atomic promotion pass");

        // Active scanout rejects palette mutation.
        axi_write(32'h00000104, 32'h80abcdef, 4'hf, 0, 2'b10);
        if (framebuffer_palette_writes != 1)
            $fatal(1, "active palette was modified");

        // Only one pending generation exists.
        axi_write(32'h0000000c, 32'h00000000, 4'hf, 0, 2'b00);
        axi_write(32'h00000010, 32'h00000001, 4'hf, 0, 2'b00);
        axi_write(32'h00000010, 32'h00000001, 4'hf, 1, 2'b10);
        pulse_frame_and_wait_promotion();
        if (scene_enable || active_generation != 32'd2)
            $fatal(1, "disable generation was not promoted");
        $display("single-pending backpressure pass");

        // The shared framebuffer validator rejects an unaligned allocation.
        axi_write(32'h00000040, 32'h18000001, 4'hf, 0, 2'b00);
        axi_write(32'h0000000c, 32'h00000001, 4'hf, 0, 2'b00);
        axi_write(32'h00000010, 32'h00000001, 4'hf, 0, 2'b10);
        if (commit_pending_status || scene_enable || commit_errors != 32'd2)
            $fatal(1, "invalid framebuffer scene was accepted");

        // The same commit gate rejects an invalid enabled tile allocation.
        axi_write(32'h00000040, 32'h18000000, 4'hf, 0, 2'b00);
        axi_write(32'h00000080, 32'h18400001, 4'hf, 1, 2'b00);
        axi_write(32'h00000098, 32'h00ff0001, 4'hf, 0, 2'b00);
        axi_write(32'h00000010, 32'h00000001, 4'hf, 0, 2'b10);
        if (commit_pending_status || commit_errors != 32'd3)
            $fatal(1, "invalid tile scene was accepted");
        $display("shared-validator commit rejection pass");

        // Restore tile state, then prove an enabled malformed sprite is
        // rejected by the same atomic submission gate.
        axi_write(32'h00000080, 32'h18400000, 4'hf, 0, 2'b00);
        axi_write(32'h00000098, 32'h00ff0000, 4'hf, 0, 2'b00);
        axi_write(32'h00000184, 32'h00000000, 4'hf, 0, 2'b00);
        axi_write(32'h00000188, 32'h00000003, 4'hf, 0, 2'b00);
        axi_write(32'h00000010, 32'h00000001, 4'hf, 0, 2'b10);
        if (commit_pending_status || commit_errors != 32'd4)
            $fatal(1, "invalid sprite scene was accepted");
        axi_write(32'h00000188, 32'h00000000, 4'hf, 0, 2'b00);
        $display("sprite-validator commit rejection pass");

        axi_write(32'h00000248, 32'd0, 4'hf, 0, 2'b11);
        axi_read(32'h00000248, 32'd0, 2'b11);
        axi_write(32'h00000041, 32'd0, 4'hf, 1, 2'b11);
        axi_read(32'h000000d8, 32'h00ff0000, 2'b00);
        axi_write(32'h000004d8, 32'h00000001, 4'hf, 0, 2'b11);
        axi_read(32'h000000d8, 32'h00ff0000, 2'b00);
        $display("write decode/alignment pass");

        $display("ASTRA GRAPHICS CONTROL PASS");
        $finish;
    end
endmodule

`default_nettype wire
