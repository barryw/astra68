// Copyright (c) 2026 Astra68 contributors
//
// Schedules framebuffer/tile/sprite construction four scanlines ahead and
// publishes only complete, matching line slots into the pixel clock domain.
`timescale 1ns/1ps
`default_nettype none

module astra_line_scheduler #(
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720,
    parameter integer TOTAL_HEIGHT = 750
) (
    input  wire        build_clk,
    input  wire        build_reset,
    input  wire        scene_changed,
    input  wire        quiesce,
    input  wire        scene_enable,
    input  wire        framebuffer_enable,
    input  wire        tile0_enable,
    input  wire        tile1_enable,
    input  wire        sprite_enable,
    input  wire [10:0] display_viewport_y,
    input  wire [10:0] display_viewport_height,
    input  wire [10:0] display_source_y,
    input  wire [10:0] display_source_height,

    output reg         line_prepare_valid,
    output reg  [10:0] line_prepare_y,
    output reg  [10:0] line_prepare_source_y,
    output reg         line_prepare_source_active,
    input  wire        line_prepare_ready,

    output reg         client_start,
    output reg  [1:0]  client_build_slot,
    output reg  [10:0] client_line_y,
    output reg  [10:0] client_source_y,
    output reg  [3:0]  client_enable,
    input  wire [3:0]  client_done,
    input  wire [3:0]  client_line_complete,

    output reg  [31:0] lines_built,
    output reg  [31:0] lines_failed,
    output reg  [31:0] scheduler_overruns,
    output wire        scheduler_idle,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire [11:0] pixel_x,
    input  wire [10:0] pixel_y,
    output reg  [1:0]  pixel_read_slot,
    output reg         pixel_line_available,
    output reg  [31:0] pixel_underruns,
    output reg  [3:0]  pixel_slot_valid,
    output reg  [10:0] pixel_slot_tag0,
    output reg  [10:0] pixel_slot_tag1,
    output reg  [10:0] pixel_slot_tag2,
    output reg  [10:0] pixel_slot_tag3,
    output reg         pixel_source_active,
    output reg  [10:0] pixel_source_y
);
    localparam [2:0] SCHED_IDLE = 3'd0;
    localparam [2:0] SCHED_MAP = 3'd1;
    localparam [2:0] SCHED_PREPARE = 3'd2;
    localparam [2:0] SCHED_WAIT = 3'd3;
    localparam [2:0] SCHED_LAUNCH = 3'd4;

    reg [2:0] scheduler_state;
    reg [3:0] required_clients;
    reg [3:0] completed_clients;
    reg [3:0] successful_clients;
    reg [3:0] prepared_clients;
    reg prepared_scene_enable;
    reg line_map_sample_q;
    wire line_map_valid;
    wire line_map_active;
    wire [10:0] line_map_source_y;

    astra_display_axis_scaler #(.WIDTH(11)) line_mapper_i (
        .clk(build_clk),
        .reset(build_reset || scene_changed),
        .sample_valid(line_map_sample_q),
        .sequence_start(line_prepare_y == 11'd0),
        .physical_position(line_prepare_y),
        .viewport_origin(display_viewport_y),
        .viewport_extent(display_viewport_height),
        .source_origin(display_source_y),
        .source_extent(display_source_height),
        .output_valid(line_map_valid),
        .output_active(line_map_active),
        .source_position(line_map_source_y)
    );

    reg bootstrap_active;
    reg [2:0] bootstrap_line;

    reg [10:0] request_fifo [0:3];
    reg [1:0] request_write_ptr;
    reg [1:0] request_read_ptr;
    reg [2:0] request_count;
    reg queue_launch_pending_q;
    reg [10:0] queued_line_q;

    reg [10:0] retired_target_pixel;
    reg retired_toggle_pixel;
    reg held_slot_valid_pixel;
    reg [1:0] held_slot_pixel;

    (* ASYNC_REG = "TRUE" *) reg retired_toggle_meta;
    (* ASYNC_REG = "TRUE" *) reg retired_toggle_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] retired_target_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] retired_target_sync;
    (* ASYNC_REG = "TRUE" *) reg held_valid_meta;
    (* ASYNC_REG = "TRUE" *) reg held_valid_sync;
    (* ASYNC_REG = "TRUE" *) reg [1:0] held_slot_meta;
    (* ASYNC_REG = "TRUE" *) reg [1:0] held_slot_sync;
    reg retired_toggle_seen;
    reg event_capture_pending;

    reg [10:0] slot_tag0;
    reg [10:0] slot_tag1;
    reg [10:0] slot_tag2;
    reg [10:0] slot_tag3;
    reg [10:0] slot_source_y0;
    reg [10:0] slot_source_y1;
    reg [10:0] slot_source_y2;
    reg [10:0] slot_source_y3;
    reg [3:0] slot_source_active;
    reg [3:0] slot_success;
    reg [3:0] slot_toggle;
    reg scene_epoch_toggle;
    reg scene_epoch_wait;
    reg scene_epoch_ack_pixel;
    (* ASYNC_REG = "TRUE" *) reg scene_epoch_ack_meta;
    (* ASYNC_REG = "TRUE" *) reg scene_epoch_ack_sync;

    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_toggle_meta;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_toggle_sync;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_success_meta;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_success_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag0_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag0_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag1_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag1_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag2_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag2_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag3_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_tag3_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y0_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y0_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y1_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y1_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y2_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y2_sync;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y3_meta;
    (* ASYNC_REG = "TRUE" *) reg [10:0] slot_source_y3_sync;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_source_active_meta;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_source_active_sync;
    (* ASYNC_REG = "TRUE" *) reg scene_epoch_meta;
    (* ASYNC_REG = "TRUE" *) reg scene_epoch_sync;
    reg [3:0] slot_toggle_seen;
    reg [3:0] slot_capture_pending;
    reg scene_epoch_seen;

    wire [3:0] configured_clients = scene_enable ?
        {sprite_enable, tile1_enable, tile0_enable,
         framebuffer_enable} : 4'b0000;
    wire [3:0] completed_now = completed_clients |
        (client_done & required_clients);
    wire [3:0] successful_now = successful_clients |
        (client_done & client_line_complete & required_clients);
    wire build_finished = scheduler_state == SCHED_WAIT &&
        (completed_now & required_clients) == required_clients;
    wire build_success = build_finished &&
        (successful_now & required_clients) == required_clients;

    wire queue_push = event_capture_pending && scene_enable && !quiesce &&
                      request_count != 3'd4;
    wire queue_full_drop = event_capture_pending && scene_enable && !quiesce &&
                           request_count == 3'd4;
    wire [10:0] queued_line = request_fifo[request_read_ptr];
    wire queued_slot_held = held_valid_sync &&
        queued_line[1:0] == held_slot_sync;
    wire queue_pop = scheduler_state == SCHED_IDLE &&
        !quiesce && !bootstrap_active && !queue_launch_pending_q &&
        request_count != 3'd0 && !queued_slot_held;
    assign scheduler_idle = scheduler_state == SCHED_IDLE &&
                            !bootstrap_active &&
                            !queue_launch_pending_q &&
                            request_count == 3'd0 &&
                            !event_capture_pending;

    task automatic publish_slot(
        input [1:0] slot,
        input [10:0] line,
        input [10:0] source_y,
        input source_active,
        input success
    );
        begin
            slot_success[slot] <= success;
            slot_toggle[slot] <= ~slot_toggle[slot];
            case (slot)
                2'd0: begin slot_tag0 <= line; slot_source_y0 <= source_y; end
                2'd1: begin slot_tag1 <= line; slot_source_y1 <= source_y; end
                2'd2: begin slot_tag2 <= line; slot_source_y2 <= source_y; end
                default: begin slot_tag3 <= line; slot_source_y3 <= source_y; end
            endcase
            slot_source_active[slot] <= source_active;
        end
    endtask

    task automatic launch_line(
        input [10:0] line,
        input [3:0] clients,
        input scene_enabled
    );
        begin
            client_build_slot <= line[1:0];
            client_line_y <= line;
            client_source_y <= line_prepare_source_y;
            client_enable <= clients;
            required_clients <= clients;
            completed_clients <= 4'd0;
            successful_clients <= 4'd0;
            if (clients == 4'd0) begin
                scheduler_state <= SCHED_IDLE;
                publish_slot(line[1:0], line, line_prepare_source_y,
                             line_prepare_source_active, scene_enabled);
                if (scene_enabled)
                    lines_built <= lines_built + 32'd1;
                else
                    lines_failed <= lines_failed + 32'd1;
                if (bootstrap_active) begin
                    if (bootstrap_line == 3'd3)
                        bootstrap_active <= 1'b0;
                    else
                        bootstrap_line <= bootstrap_line + 3'd1;
                end
            end else begin
                client_start <= 1'b1;
                scheduler_state <= SCHED_WAIT;
            end
        end
    endtask

    always @(posedge build_clk or posedge build_reset) begin
        if (build_reset) begin
            retired_toggle_meta <= 1'b0;
            retired_toggle_sync <= 1'b0;
            retired_target_meta <= 11'd0;
            retired_target_sync <= 11'd0;
            held_valid_meta <= 1'b0;
            held_valid_sync <= 1'b0;
            held_slot_meta <= 2'd0;
            held_slot_sync <= 2'd0;
            scene_epoch_ack_meta <= 1'b0;
            scene_epoch_ack_sync <= 1'b0;
        end else begin
            retired_toggle_meta <= retired_toggle_pixel;
            retired_toggle_sync <= retired_toggle_meta;
            retired_target_meta <= retired_target_pixel;
            retired_target_sync <= retired_target_meta;
            held_valid_meta <= held_slot_valid_pixel;
            held_valid_sync <= held_valid_meta;
            held_slot_meta <= held_slot_pixel;
            held_slot_sync <= held_slot_meta;
            scene_epoch_ack_meta <= scene_epoch_ack_pixel;
            scene_epoch_ack_sync <= scene_epoch_ack_meta;
        end
    end

    always @(posedge build_clk) begin
        if (build_reset) begin
            scheduler_state <= SCHED_IDLE;
            client_start <= 1'b0;
            line_prepare_valid <= 1'b0;
            line_prepare_y <= 11'd0;
            line_prepare_source_y <= 11'd0;
            line_prepare_source_active <= 1'b0;
            line_map_sample_q <= 1'b0;
            client_build_slot <= 2'd0;
            client_line_y <= 11'd0;
            client_source_y <= 11'd0;
            client_enable <= 4'd0;
            required_clients <= 4'd0;
            completed_clients <= 4'd0;
            successful_clients <= 4'd0;
            prepared_clients <= 4'd0;
            prepared_scene_enable <= 1'b0;
            bootstrap_active <= 1'b0;
            bootstrap_line <= 3'd0;
            request_write_ptr <= 2'd0;
            request_read_ptr <= 2'd0;
            request_count <= 3'd0;
            queue_launch_pending_q <= 1'b0;
            queued_line_q <= 11'd0;
            retired_toggle_seen <= 1'b0;
            event_capture_pending <= 1'b0;
            slot_tag0 <= 11'd0;
            slot_tag1 <= 11'd0;
            slot_tag2 <= 11'd0;
            slot_tag3 <= 11'd0;
            slot_source_y0 <= 11'd0;
            slot_source_y1 <= 11'd0;
            slot_source_y2 <= 11'd0;
            slot_source_y3 <= 11'd0;
            slot_source_active <= 4'd0;
            slot_success <= 4'd0;
            slot_toggle <= 4'd0;
            scene_epoch_toggle <= 1'b0;
            scene_epoch_wait <= 1'b0;
            lines_built <= 32'd0;
            lines_failed <= 32'd0;
            scheduler_overruns <= 32'd0;
        end else begin
            client_start <= 1'b0;
            line_map_sample_q <= 1'b0;

            if (quiesce) begin
                retired_toggle_seen <= retired_toggle_sync;
                event_capture_pending <= 1'b0;
            end else if (retired_toggle_sync != retired_toggle_seen) begin
                retired_toggle_seen <= retired_toggle_sync;
                event_capture_pending <= 1'b1;
            end else if (event_capture_pending) begin
                event_capture_pending <= 1'b0;
            end

            if (queue_push) begin
                request_fifo[request_write_ptr] <= retired_target_sync;
                request_write_ptr <= request_write_ptr + 2'd1;
            end
            if (queue_pop) begin
                request_read_ptr <= request_read_ptr + 2'd1;
                queue_launch_pending_q <= 1'b1;
                queued_line_q <= queued_line;
            end
            case ({queue_push, queue_pop})
                2'b10: request_count <= request_count + 3'd1;
                2'b01: request_count <= request_count - 3'd1;
                default: begin end
            endcase
            if (queue_full_drop)
                scheduler_overruns <= scheduler_overruns + 32'd1;

            if (scene_changed) begin
                scheduler_state <= SCHED_IDLE;
                line_prepare_valid <= 1'b0;
                line_prepare_source_active <= 1'b0;
                client_enable <= 4'd0;
                required_clients <= 4'd0;
                completed_clients <= 4'd0;
                successful_clients <= 4'd0;
                prepared_clients <= 4'd0;
                prepared_scene_enable <= 1'b0;
                bootstrap_active <= 1'b0;
                bootstrap_line <= 3'd0;
                request_write_ptr <= 2'd0;
                request_read_ptr <= 2'd0;
                request_count <= 3'd0;
                queue_launch_pending_q <= 1'b0;
                event_capture_pending <= 1'b0;
                slot_success <= 4'd0;
                scene_epoch_toggle <= ~scene_epoch_toggle;
                scene_epoch_wait <= 1'b1;
            end else begin
                if (!quiesce && scene_epoch_wait &&
                    scene_epoch_ack_sync == scene_epoch_toggle) begin
                    scene_epoch_wait <= 1'b0;
                    bootstrap_active <= scene_enable;
                end
                if (quiesce) begin
                    bootstrap_active <= 1'b0;
                    request_write_ptr <= 2'd0;
                    request_read_ptr <= 2'd0;
                    request_count <= 3'd0;
                    queue_launch_pending_q <= 1'b0;
                    event_capture_pending <= 1'b0;
                    line_prepare_valid <= 1'b0;
                end
                if (scheduler_state == SCHED_MAP) begin
                    if (line_map_valid) begin
                        line_prepare_source_y <= line_map_source_y;
                        line_prepare_source_active <= line_map_active;
                        prepared_scene_enable <= scene_enable;
                        if (line_map_active) begin
                            line_prepare_valid <= 1'b1;
                            scheduler_state <= SCHED_PREPARE;
                        end else begin
                            prepared_clients <= 4'd0;
                            scheduler_state <= SCHED_LAUNCH;
                        end
                    end
                end else if (scheduler_state == SCHED_PREPARE) begin
                    if (line_prepare_ready) begin
                        line_prepare_valid <= 1'b0;
                        prepared_clients <= configured_clients;
                        prepared_scene_enable <= scene_enable;
                        scheduler_state <= SCHED_LAUNCH;
                    end
                end else if (scheduler_state == SCHED_LAUNCH) begin
                    launch_line(line_prepare_y, prepared_clients,
                                prepared_scene_enable);
                end else if (scheduler_state == SCHED_WAIT) begin
                    completed_clients <= completed_now;
                    successful_clients <= successful_now;
                    if (build_finished) begin
                        publish_slot(client_build_slot, client_line_y,
                                     client_source_y,
                                     line_prepare_source_active,
                                     build_success);
                        if (build_success)
                            lines_built <= lines_built + 32'd1;
                        else
                            lines_failed <= lines_failed + 32'd1;
                        scheduler_state <= SCHED_IDLE;
                        client_enable <= 4'd0;
                        if (bootstrap_active) begin
                            if (bootstrap_line == 3'd3) begin
                                bootstrap_active <= 1'b0;
                            end else begin
                                bootstrap_line <= bootstrap_line + 3'd1;
                            end
                        end
                    end
                end else if (!quiesce && bootstrap_active) begin
                    line_prepare_y <= {7'd0, bootstrap_line};
                    line_map_sample_q <= 1'b1;
                    scheduler_state <= SCHED_MAP;
                end else if (!quiesce && queue_launch_pending_q) begin
                    queue_launch_pending_q <= 1'b0;
                    line_prepare_y <= queued_line_q;
                    line_map_sample_q <= 1'b1;
                    scheduler_state <= SCHED_MAP;
                end
            end
        end
    end

    wire [10:0] pixel_candidate_line =
        pixel_y == TOTAL_HEIGHT - 1 ? 11'd0 : pixel_y + 11'd1;
    wire [1:0] pixel_candidate_slot = pixel_candidate_line[1:0];
    wire [10:0] pixel_candidate_tag =
        pixel_candidate_slot == 2'd0 ? pixel_slot_tag0 :
        pixel_candidate_slot == 2'd1 ? pixel_slot_tag1 :
        pixel_candidate_slot == 2'd2 ? pixel_slot_tag2 : pixel_slot_tag3;
    wire pixel_candidate_ready = pixel_slot_valid[pixel_candidate_slot] &&
        pixel_candidate_tag == pixel_candidate_line;
    wire select_next_active_line = pixel_x == OUTPUT_WIDTH - 1 &&
        (pixel_y < OUTPUT_HEIGHT - 1 || pixel_y == TOTAL_HEIGHT - 1);
    wire retire_active_line = pixel_x == OUTPUT_WIDTH - 1 &&
                              pixel_y < OUTPUT_HEIGHT;
    wire [11:0] retired_plus_four = {1'b0, pixel_y} + 12'd4;
    wire [10:0] retired_target = retired_plus_four >= OUTPUT_HEIGHT ?
        retired_plus_four - OUTPUT_HEIGHT : retired_plus_four[10:0];

    always @(posedge pixel_clk or posedge pixel_reset) begin
        if (pixel_reset) begin
            slot_toggle_meta <= 4'd0;
            slot_toggle_sync <= 4'd0;
            slot_success_meta <= 4'd0;
            slot_success_sync <= 4'd0;
            slot_tag0_meta <= 11'd0;
            slot_tag0_sync <= 11'd0;
            slot_tag1_meta <= 11'd0;
            slot_tag1_sync <= 11'd0;
            slot_tag2_meta <= 11'd0;
            slot_tag2_sync <= 11'd0;
            slot_tag3_meta <= 11'd0;
            slot_tag3_sync <= 11'd0;
            slot_source_y0_meta <= 11'd0;
            slot_source_y0_sync <= 11'd0;
            slot_source_y1_meta <= 11'd0;
            slot_source_y1_sync <= 11'd0;
            slot_source_y2_meta <= 11'd0;
            slot_source_y2_sync <= 11'd0;
            slot_source_y3_meta <= 11'd0;
            slot_source_y3_sync <= 11'd0;
            slot_source_active_meta <= 4'd0;
            slot_source_active_sync <= 4'd0;
            scene_epoch_meta <= 1'b0;
            scene_epoch_sync <= 1'b0;
        end else begin
            slot_toggle_meta <= slot_toggle;
            slot_toggle_sync <= slot_toggle_meta;
            slot_success_meta <= slot_success;
            slot_success_sync <= slot_success_meta;
            slot_tag0_meta <= slot_tag0;
            slot_tag0_sync <= slot_tag0_meta;
            slot_tag1_meta <= slot_tag1;
            slot_tag1_sync <= slot_tag1_meta;
            slot_tag2_meta <= slot_tag2;
            slot_tag2_sync <= slot_tag2_meta;
            slot_tag3_meta <= slot_tag3;
            slot_tag3_sync <= slot_tag3_meta;
            slot_source_y0_meta <= slot_source_y0;
            slot_source_y0_sync <= slot_source_y0_meta;
            slot_source_y1_meta <= slot_source_y1;
            slot_source_y1_sync <= slot_source_y1_meta;
            slot_source_y2_meta <= slot_source_y2;
            slot_source_y2_sync <= slot_source_y2_meta;
            slot_source_y3_meta <= slot_source_y3;
            slot_source_y3_sync <= slot_source_y3_meta;
            slot_source_active_meta <= slot_source_active;
            slot_source_active_sync <= slot_source_active_meta;
            scene_epoch_meta <= scene_epoch_toggle;
            scene_epoch_sync <= scene_epoch_meta;
        end
    end

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            retired_target_pixel <= 11'd0;
            retired_toggle_pixel <= 1'b0;
            held_slot_valid_pixel <= 1'b0;
            held_slot_pixel <= 2'd0;
            slot_toggle_seen <= 4'd0;
            slot_capture_pending <= 4'd0;
            scene_epoch_seen <= 1'b0;
            scene_epoch_ack_pixel <= 1'b0;
            pixel_read_slot <= 2'd0;
            pixel_line_available <= 1'b0;
            pixel_underruns <= 32'd0;
            pixel_slot_valid <= 4'd0;
            pixel_slot_tag0 <= 11'd0;
            pixel_slot_tag1 <= 11'd0;
            pixel_slot_tag2 <= 11'd0;
            pixel_slot_tag3 <= 11'd0;
            pixel_source_active <= 1'b0;
            pixel_source_y <= 11'd0;
        end else begin
            if (scene_epoch_sync != scene_epoch_seen) begin
                scene_epoch_seen <= scene_epoch_sync;
                scene_epoch_ack_pixel <= scene_epoch_sync;
                pixel_line_available <= 1'b0;
                held_slot_valid_pixel <= 1'b0;
                pixel_slot_valid <= 4'd0;
                slot_capture_pending <= 4'd0;
                slot_toggle_seen <= slot_toggle_sync;
            end else begin
                if (slot_capture_pending[0]) begin
                    slot_capture_pending[0] <= 1'b0;
                    pixel_slot_valid[0] <= slot_success_sync[0];
                    pixel_slot_tag0 <= slot_tag0_sync;
                end
                if (slot_capture_pending[1]) begin
                    slot_capture_pending[1] <= 1'b0;
                    pixel_slot_valid[1] <= slot_success_sync[1];
                    pixel_slot_tag1 <= slot_tag1_sync;
                end
                if (slot_capture_pending[2]) begin
                    slot_capture_pending[2] <= 1'b0;
                    pixel_slot_valid[2] <= slot_success_sync[2];
                    pixel_slot_tag2 <= slot_tag2_sync;
                end
                if (slot_capture_pending[3]) begin
                    slot_capture_pending[3] <= 1'b0;
                    pixel_slot_valid[3] <= slot_success_sync[3];
                    pixel_slot_tag3 <= slot_tag3_sync;
                end

                if (slot_toggle_sync[0] != slot_toggle_seen[0]) begin
                    slot_toggle_seen[0] <= slot_toggle_sync[0];
                    slot_capture_pending[0] <= 1'b1;
                end
                if (slot_toggle_sync[1] != slot_toggle_seen[1]) begin
                    slot_toggle_seen[1] <= slot_toggle_sync[1];
                    slot_capture_pending[1] <= 1'b1;
                end
                if (slot_toggle_sync[2] != slot_toggle_seen[2]) begin
                    slot_toggle_seen[2] <= slot_toggle_sync[2];
                    slot_capture_pending[2] <= 1'b1;
                end
                if (slot_toggle_sync[3] != slot_toggle_seen[3]) begin
                    slot_toggle_seen[3] <= slot_toggle_sync[3];
                    slot_capture_pending[3] <= 1'b1;
                end

                if (retire_active_line) begin
                    retired_target_pixel <= retired_target;
                    retired_toggle_pixel <= ~retired_toggle_pixel;
                    if (pixel_y == OUTPUT_HEIGHT - 1)
                        held_slot_valid_pixel <= 1'b0;
                end

                if (select_next_active_line) begin
                    if (pixel_candidate_ready) begin
                        pixel_read_slot <= pixel_candidate_slot;
                        pixel_line_available <= 1'b1;
                        pixel_source_active <=
                            slot_source_active_sync[pixel_candidate_slot];
                        pixel_source_y <=
                            pixel_candidate_slot == 2'd0 ? slot_source_y0_sync :
                            pixel_candidate_slot == 2'd1 ? slot_source_y1_sync :
                            pixel_candidate_slot == 2'd2 ? slot_source_y2_sync :
                            slot_source_y3_sync;
                        held_slot_valid_pixel <= 1'b0;
                    end else begin
                        pixel_underruns <= pixel_underruns + 32'd1;
                        if (pixel_line_available) begin
                            held_slot_valid_pixel <= 1'b1;
                            held_slot_pixel <= pixel_read_slot;
                        end else begin
                            held_slot_valid_pixel <= 1'b0;
                        end
                    end
                end
            end
        end
    end
endmodule

`default_nettype wire
