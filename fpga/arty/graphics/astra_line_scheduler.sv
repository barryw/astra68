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

    output reg         line_prepare_valid,
    output reg  [9:0]  line_prepare_y,
    input  wire        line_prepare_ready,

    output reg         client_start,
    output reg  [1:0]  client_build_slot,
    output reg  [9:0]  client_line_y,
    output reg  [3:0]  client_enable,
    input  wire [3:0]  client_done,
    input  wire [3:0]  client_line_complete,

    output reg  [31:0] lines_built,
    output reg  [31:0] lines_failed,
    output reg  [31:0] scheduler_overruns,
    output wire        scheduler_idle,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire [10:0] pixel_x,
    input  wire [9:0]  pixel_y,
    output reg  [1:0]  pixel_read_slot,
    output reg         pixel_line_available,
    output reg  [31:0] pixel_underruns,
    output reg  [3:0]  pixel_slot_valid,
    output reg  [9:0]  pixel_slot_tag0,
    output reg  [9:0]  pixel_slot_tag1,
    output reg  [9:0]  pixel_slot_tag2,
    output reg  [9:0]  pixel_slot_tag3
);
    localparam [1:0] SCHED_IDLE = 2'd0;
    localparam [1:0] SCHED_PREPARE = 2'd1;
    localparam [1:0] SCHED_WAIT = 2'd2;

    reg [1:0] scheduler_state;
    reg [3:0] required_clients;
    reg [3:0] completed_clients;
    reg [3:0] successful_clients;

    reg bootstrap_active;
    reg [2:0] bootstrap_line;

    reg [9:0] request_fifo [0:3];
    reg [1:0] request_write_ptr;
    reg [1:0] request_read_ptr;
    reg [2:0] request_count;
    reg queue_launch_pending_q;
    reg [9:0] queued_line_q;

    reg [9:0] retired_target_pixel;
    reg retired_toggle_pixel;
    reg held_slot_valid_pixel;
    reg [1:0] held_slot_pixel;

    (* ASYNC_REG = "TRUE" *) reg retired_toggle_meta;
    (* ASYNC_REG = "TRUE" *) reg retired_toggle_sync;
    (* ASYNC_REG = "TRUE" *) reg [9:0] retired_target_meta;
    (* ASYNC_REG = "TRUE" *) reg [9:0] retired_target_sync;
    (* ASYNC_REG = "TRUE" *) reg held_valid_meta;
    (* ASYNC_REG = "TRUE" *) reg held_valid_sync;
    (* ASYNC_REG = "TRUE" *) reg [1:0] held_slot_meta;
    (* ASYNC_REG = "TRUE" *) reg [1:0] held_slot_sync;
    reg retired_toggle_seen;
    reg event_capture_pending;

    reg [9:0] slot_tag0;
    reg [9:0] slot_tag1;
    reg [9:0] slot_tag2;
    reg [9:0] slot_tag3;
    reg [3:0] slot_success;
    reg [3:0] slot_toggle;
    reg scene_epoch_toggle;

    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_toggle_meta;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_toggle_sync;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_success_meta;
    (* ASYNC_REG = "TRUE" *) reg [3:0] slot_success_sync;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag0_meta;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag0_sync;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag1_meta;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag1_sync;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag2_meta;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag2_sync;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag3_meta;
    (* ASYNC_REG = "TRUE" *) reg [9:0] slot_tag3_sync;
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
    wire [9:0] queued_line = request_fifo[request_read_ptr];
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
        input [9:0] line,
        input success
    );
        begin
            slot_success[slot] <= success;
            slot_toggle[slot] <= ~slot_toggle[slot];
            case (slot)
                2'd0: slot_tag0 <= line;
                2'd1: slot_tag1 <= line;
                2'd2: slot_tag2 <= line;
                default: slot_tag3 <= line;
            endcase
        end
    endtask

    task automatic launch_line(input [9:0] line);
        begin
            client_build_slot <= line[1:0];
            client_line_y <= line;
            client_enable <= configured_clients;
            required_clients <= configured_clients;
            completed_clients <= 4'd0;
            successful_clients <= 4'd0;
            if (configured_clients == 4'd0) begin
                scheduler_state <= SCHED_IDLE;
                publish_slot(line[1:0], line, scene_enable);
                if (scene_enable)
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

    always @(posedge build_clk) begin
        if (build_reset) begin
            retired_toggle_meta <= 1'b0;
            retired_toggle_sync <= 1'b0;
            retired_target_meta <= 10'd0;
            retired_target_sync <= 10'd0;
            held_valid_meta <= 1'b0;
            held_valid_sync <= 1'b0;
            held_slot_meta <= 2'd0;
            held_slot_sync <= 2'd0;
        end else begin
            retired_toggle_meta <= retired_toggle_pixel;
            retired_toggle_sync <= retired_toggle_meta;
            retired_target_meta <= retired_target_pixel;
            retired_target_sync <= retired_target_meta;
            held_valid_meta <= held_slot_valid_pixel;
            held_valid_sync <= held_valid_meta;
            held_slot_meta <= held_slot_pixel;
            held_slot_sync <= held_slot_meta;
        end
    end

    always @(posedge build_clk) begin
        if (build_reset) begin
            scheduler_state <= SCHED_IDLE;
            client_start <= 1'b0;
            line_prepare_valid <= 1'b0;
            line_prepare_y <= 10'd0;
            client_build_slot <= 2'd0;
            client_line_y <= 10'd0;
            client_enable <= 4'd0;
            required_clients <= 4'd0;
            completed_clients <= 4'd0;
            successful_clients <= 4'd0;
            bootstrap_active <= 1'b0;
            bootstrap_line <= 3'd0;
            request_write_ptr <= 2'd0;
            request_read_ptr <= 2'd0;
            request_count <= 3'd0;
            queue_launch_pending_q <= 1'b0;
            queued_line_q <= 10'd0;
            retired_toggle_seen <= 1'b0;
            event_capture_pending <= 1'b0;
            slot_tag0 <= 10'd0;
            slot_tag1 <= 10'd0;
            slot_tag2 <= 10'd0;
            slot_tag3 <= 10'd0;
            slot_success <= 4'd0;
            slot_toggle <= 4'd0;
            scene_epoch_toggle <= 1'b0;
            lines_built <= 32'd0;
            lines_failed <= 32'd0;
            scheduler_overruns <= 32'd0;
        end else begin
            client_start <= 1'b0;

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
                client_enable <= 4'd0;
                required_clients <= 4'd0;
                completed_clients <= 4'd0;
                successful_clients <= 4'd0;
                bootstrap_active <= scene_enable;
                bootstrap_line <= 3'd0;
                request_write_ptr <= 2'd0;
                request_read_ptr <= 2'd0;
                request_count <= 3'd0;
                queue_launch_pending_q <= 1'b0;
                event_capture_pending <= 1'b0;
                slot_success <= 4'd0;
                scene_epoch_toggle <= ~scene_epoch_toggle;
            end else begin
                if (quiesce) begin
                    bootstrap_active <= 1'b0;
                    request_write_ptr <= 2'd0;
                    request_read_ptr <= 2'd0;
                    request_count <= 3'd0;
                    queue_launch_pending_q <= 1'b0;
                    event_capture_pending <= 1'b0;
                    line_prepare_valid <= 1'b0;
                end
                if (scheduler_state == SCHED_PREPARE) begin
                    if (line_prepare_ready) begin
                        line_prepare_valid <= 1'b0;
                        launch_line(line_prepare_y);
                    end
                end else if (scheduler_state == SCHED_WAIT) begin
                    completed_clients <= completed_now;
                    successful_clients <= successful_now;
                    if (build_finished) begin
                        publish_slot(client_build_slot, client_line_y,
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
                    line_prepare_valid <= 1'b1;
                    scheduler_state <= SCHED_PREPARE;
                end else if (!quiesce && queue_launch_pending_q) begin
                    queue_launch_pending_q <= 1'b0;
                    line_prepare_y <= queued_line_q;
                    line_prepare_valid <= 1'b1;
                    scheduler_state <= SCHED_PREPARE;
                end
            end
        end
    end

    wire [9:0] pixel_candidate_line =
        pixel_y == TOTAL_HEIGHT - 1 ? 10'd0 : pixel_y + 10'd1;
    wire [1:0] pixel_candidate_slot = pixel_candidate_line[1:0];
    wire [9:0] pixel_candidate_tag =
        pixel_candidate_slot == 2'd0 ? pixel_slot_tag0 :
        pixel_candidate_slot == 2'd1 ? pixel_slot_tag1 :
        pixel_candidate_slot == 2'd2 ? pixel_slot_tag2 : pixel_slot_tag3;
    wire pixel_candidate_ready = pixel_slot_valid[pixel_candidate_slot] &&
        pixel_candidate_tag == pixel_candidate_line;
    wire select_next_active_line = pixel_x == OUTPUT_WIDTH - 1 &&
        (pixel_y < OUTPUT_HEIGHT - 1 || pixel_y == TOTAL_HEIGHT - 1);
    wire retire_active_line = pixel_x == OUTPUT_WIDTH - 1 &&
                              pixel_y < OUTPUT_HEIGHT;
    wire [10:0] retired_plus_four = {1'b0, pixel_y} + 11'd4;
    wire [9:0] retired_target = retired_plus_four >= OUTPUT_HEIGHT ?
        retired_plus_four - OUTPUT_HEIGHT : retired_plus_four[9:0];

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            retired_target_pixel <= 10'd0;
            retired_toggle_pixel <= 1'b0;
            held_slot_valid_pixel <= 1'b0;
            held_slot_pixel <= 2'd0;
            slot_toggle_meta <= 4'd0;
            slot_toggle_sync <= 4'd0;
            slot_success_meta <= 4'd0;
            slot_success_sync <= 4'd0;
            slot_tag0_meta <= 10'd0;
            slot_tag0_sync <= 10'd0;
            slot_tag1_meta <= 10'd0;
            slot_tag1_sync <= 10'd0;
            slot_tag2_meta <= 10'd0;
            slot_tag2_sync <= 10'd0;
            slot_tag3_meta <= 10'd0;
            slot_tag3_sync <= 10'd0;
            scene_epoch_meta <= 1'b0;
            scene_epoch_sync <= 1'b0;
            slot_toggle_seen <= 4'd0;
            slot_capture_pending <= 4'd0;
            scene_epoch_seen <= 1'b0;
            pixel_read_slot <= 2'd0;
            pixel_line_available <= 1'b0;
            pixel_underruns <= 32'd0;
            pixel_slot_valid <= 4'd0;
            pixel_slot_tag0 <= 10'd0;
            pixel_slot_tag1 <= 10'd0;
            pixel_slot_tag2 <= 10'd0;
            pixel_slot_tag3 <= 10'd0;
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
            scene_epoch_meta <= scene_epoch_toggle;
            scene_epoch_sync <= scene_epoch_meta;

            if (scene_epoch_sync != scene_epoch_seen) begin
                scene_epoch_seen <= scene_epoch_sync;
                pixel_line_available <= 1'b0;
                held_slot_valid_pixel <= 1'b0;
                pixel_slot_valid <= 4'd0;
                slot_capture_pending <= 4'd0;
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
