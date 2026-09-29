// Copyright (c) 2026 Astra68 contributors
//
// Next-vblank copper state. Structural writes build a private candidate during
// one frame. The complete framebuffer candidate is validated after the
// list stops and can replace the committed baseline only at a later vblank.
`timescale 1ns/1ps
`default_nettype none

module astra_copper_structural_state #(
    parameter [31:0] ARENA_BASE = 32'h18000000,
    parameter [31:0] ARENA_LIMIT = 32'h20000000,
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720
) (
    input  wire        clk,
    input  wire        reset,
    input  wire        frame_boundary,
    input  wire        baseline_changed,
    input  wire        copper_running,
    input  wire        copper_fault,

    input  wire        baseline_scene_enable,
    input  wire        baseline_framebuffer_enable,
    input  wire [1:0]  baseline_framebuffer_format,
    input  wire [31:0] baseline_framebuffer_base,
    input  wire [31:0] baseline_framebuffer_pitch,
    input  wire [12:0] baseline_framebuffer_width,
    input  wire [12:0] baseline_framebuffer_height,
    input  wire signed [31:0] baseline_framebuffer_viewport_x,
    input  wire signed [31:0] baseline_framebuffer_viewport_y,
    input  wire        baseline_framebuffer_wrap_x,
    input  wire        baseline_framebuffer_wrap_y,

    input  wire [15:0] validate_target,
    input  wire [31:0] validate_data,
    output wire        validate_allowed,
    input  wire        move_valid,
    input  wire [15:0] move_target,
    input  wire [31:0] move_data,
    output wire        move_allowed,
    output wire        move_ready,

    output reg         scene_enable,
    output reg         framebuffer_enable,
    output reg  [1:0]  framebuffer_format,
    output reg  [31:0] framebuffer_base,
    output reg  [31:0] framebuffer_pitch,
    output reg  [12:0] framebuffer_width,
    output reg  [12:0] framebuffer_height,
    output reg  [31:0] candidates_accepted,
    output reg  [31:0] candidates_rejected,
    output reg  [31:0] candidates_deferred
);
    localparam [15:0] TARGET_GLOBAL_STRUCTURE = 16'h001c;
    localparam [15:0] TARGET_FB_BASE = 16'h0040;
    localparam [15:0] TARGET_FB_PITCH = 16'h0044;
    localparam [15:0] TARGET_FB_SIZE = 16'h0048;
    localparam [15:0] TARGET_FB_STRUCTURE = 16'h005c;

    function automatic structural_target_allowed(
        input [15:0] target,
        input [31:0] data
    );
        begin
            case (target)
                TARGET_GLOBAL_STRUCTURE:
                    structural_target_allowed = data[31:1] == 31'd0;
                TARGET_FB_BASE,
                TARGET_FB_PITCH:
                    structural_target_allowed = 1'b1;
                TARGET_FB_SIZE:
                    structural_target_allowed = data[31:29] == 3'd0 &&
                        data[15:13] == 3'd0 && data[28:16] != 13'd0 &&
                        data[28:16] <= 13'd4096 && data[12:0] != 13'd0 &&
                        data[12:0] <= 13'd4096;
                TARGET_FB_STRUCTURE:
                    structural_target_allowed = data[31:3] == 29'd0 &&
                        data[2:1] != 2'b11;
                default: structural_target_allowed = 1'b0;
            endcase
        end
    endfunction

    function automatic structural_target_known(input [15:0] target);
        begin
            case (target)
                TARGET_GLOBAL_STRUCTURE,
                TARGET_FB_BASE,
                TARGET_FB_PITCH,
                TARGET_FB_SIZE,
                TARGET_FB_STRUCTURE:
                    structural_target_known = 1'b1;
                default: structural_target_known = 1'b0;
            endcase
        end
    endfunction

    assign validate_allowed = structural_target_allowed(
        validate_target, validate_data);
    // The active bank is immutable and its complete payload was validated
    // before promotion. Runtime only needs to classify the target.
    assign move_allowed = structural_target_known(move_target);

    reg move_pending_q;
    reg [15:0] move_target_q;
    reg [31:0] move_data_q;

    reg candidate_scene_enable;
    reg candidate_framebuffer_enable;
    reg [1:0] candidate_framebuffer_format;
    reg [31:0] candidate_framebuffer_base;
    reg [31:0] candidate_framebuffer_pitch;
    reg [12:0] candidate_framebuffer_width;
    reg [12:0] candidate_framebuffer_height;
    reg candidate_dirty;

    reg snapshot_scene_enable;
    reg snapshot_framebuffer_enable;
    reg [1:0] snapshot_framebuffer_format;
    reg [31:0] snapshot_framebuffer_base;
    reg [31:0] snapshot_framebuffer_pitch;
    reg [12:0] snapshot_framebuffer_width;
    reg [12:0] snapshot_framebuffer_height;

    reg pending_valid;
    reg pending_scene_enable;
    reg pending_framebuffer_enable;
    reg [1:0] pending_framebuffer_format;
    reg [31:0] pending_framebuffer_base;
    reg [31:0] pending_framebuffer_pitch;
    reg [12:0] pending_framebuffer_width;
    reg [12:0] pending_framebuffer_height;

    reg running_q;
    reg running_qq;
    reg validating;
    reg validator_start;
    reg framebuffer_done_seen;
    reg framebuffer_valid_q;
    reg validation_result_pending_q;
    reg validation_result_valid_q;
    wire framebuffer_busy;
    wire framebuffer_done;
    wire framebuffer_valid;
    wire validators_done = framebuffer_done_seen || framebuffer_done;
    wire validators_valid = !snapshot_scene_enable ||
        !snapshot_framebuffer_enable ||
        (framebuffer_done ? framebuffer_valid : framebuffer_valid_q);

    // Capacity is independent of the request payload.  The copper snapshots
    // move_allowed before asserting move_valid, so feeding the target decode
    // back through ready only lengthens the retirement path.
    assign move_ready = !validating && !move_pending_q;

    astra_framebuffer_config_validator #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT)
    ) framebuffer_validator_i (
        .clk(clk), .reset(reset), .start(validator_start),
        .format(snapshot_framebuffer_format),
        .framebuffer_base(snapshot_framebuffer_base),
        .pitch(snapshot_framebuffer_pitch),
        .virtual_width(snapshot_framebuffer_width),
        .virtual_height(snapshot_framebuffer_height),
        .viewport_x(baseline_framebuffer_viewport_x),
        .viewport_y(baseline_framebuffer_viewport_y),
        .wrap_x(baseline_framebuffer_wrap_x),
        .wrap_y(baseline_framebuffer_wrap_y),
        .arena_base(ARENA_BASE), .arena_limit(ARENA_LIMIT),
        .busy(framebuffer_busy), .done(framebuffer_done),
        .config_valid(framebuffer_valid), .surface_bytes()
    );

    task automatic load_candidate_baseline;
        begin
            candidate_scene_enable <= baseline_scene_enable;
            candidate_framebuffer_enable <= baseline_framebuffer_enable;
            candidate_framebuffer_format <= baseline_framebuffer_format;
            candidate_framebuffer_base <= baseline_framebuffer_base;
            candidate_framebuffer_pitch <= baseline_framebuffer_pitch;
            candidate_framebuffer_width <= baseline_framebuffer_width;
            candidate_framebuffer_height <= baseline_framebuffer_height;
            candidate_dirty <= 1'b0;
        end
    endtask

    task automatic load_active_baseline;
        begin
            scene_enable <= baseline_scene_enable;
            framebuffer_enable <= baseline_framebuffer_enable;
            framebuffer_format <= baseline_framebuffer_format;
            framebuffer_base <= baseline_framebuffer_base;
            framebuffer_pitch <= baseline_framebuffer_pitch;
            framebuffer_width <= baseline_framebuffer_width;
            framebuffer_height <= baseline_framebuffer_height;
        end
    endtask

    task automatic load_active_pending;
        begin
            scene_enable <= pending_scene_enable;
            framebuffer_enable <= pending_framebuffer_enable;
            framebuffer_format <= pending_framebuffer_format;
            framebuffer_base <= pending_framebuffer_base;
            framebuffer_pitch <= pending_framebuffer_pitch;
            framebuffer_width <= pending_framebuffer_width;
            framebuffer_height <= pending_framebuffer_height;
        end
    endtask

    always @(posedge clk) begin
        validator_start <= 1'b0;
        running_q <= copper_running;
        running_qq <= running_q;
        if (reset) begin
            running_q <= 1'b0;
            running_qq <= 1'b0;
            validating <= 1'b0;
            validator_start <= 1'b0;
            framebuffer_done_seen <= 1'b0;
            framebuffer_valid_q <= 1'b0;
            validation_result_pending_q <= 1'b0;
            validation_result_valid_q <= 1'b0;
            pending_valid <= 1'b0;
            candidates_accepted <= 32'd0;
            candidates_rejected <= 32'd0;
            candidates_deferred <= 32'd0;
            move_pending_q <= 1'b0;
            move_target_q <= 16'd0;
            move_data_q <= 32'd0;
            load_candidate_baseline();
            load_active_baseline();
        end else begin
            if (frame_boundary) begin
                load_candidate_baseline();
                load_active_baseline();
                if (pending_valid) begin
                    load_active_pending();
                    candidates_accepted <= candidates_accepted + 32'd1;
                end else if (validating)
                    candidates_deferred <= candidates_deferred + 32'd1;
                pending_valid <= 1'b0;
            end

            // A software scene promotion completes within vblank rather than
            // on its first cycle. It supersedes a candidate derived from the
            // previous committed scene; copper restarts after this pulse.
            if (baseline_changed) begin
                load_candidate_baseline();
                load_active_baseline();
                pending_valid <= 1'b0;
                validating <= 1'b0;
            end

            if (move_valid && move_ready) begin
                move_pending_q <= 1'b1;
                move_target_q <= move_target;
                move_data_q <= move_data;
            end

            if (move_pending_q) begin
                move_pending_q <= 1'b0;
                candidate_dirty <= 1'b1;
                case (move_target_q)
                    TARGET_GLOBAL_STRUCTURE:
                        candidate_scene_enable <= move_data_q[0];
                    TARGET_FB_BASE:
                        candidate_framebuffer_base <= move_data_q;
                    TARGET_FB_PITCH:
                        candidate_framebuffer_pitch <= move_data_q;
                    TARGET_FB_SIZE: begin
                        candidate_framebuffer_width <= move_data_q[12:0];
                        candidate_framebuffer_height <= move_data_q[28:16];
                    end
                    TARGET_FB_STRUCTURE: begin
                        candidate_framebuffer_enable <= move_data_q[0];
                        candidate_framebuffer_format <= move_data_q[2:1];
                    end
                    default: begin end
                endcase
            end

            if (running_qq && !running_q && candidate_dirty &&
                !validating) begin
                if (copper_fault) begin
                    candidate_dirty <= 1'b0;
                    candidates_rejected <= candidates_rejected + 32'd1;
                end else begin
                    snapshot_scene_enable <= candidate_scene_enable;
                    snapshot_framebuffer_enable <=
                        candidate_framebuffer_enable;
                    snapshot_framebuffer_format <=
                        candidate_framebuffer_format;
                    snapshot_framebuffer_base <= candidate_framebuffer_base;
                    snapshot_framebuffer_pitch <= candidate_framebuffer_pitch;
                    snapshot_framebuffer_width <= candidate_framebuffer_width;
                    snapshot_framebuffer_height <=
                        candidate_framebuffer_height;
                    validator_start <= 1'b1;
                    validating <= 1'b1;
                    framebuffer_done_seen <= 1'b0;
                    candidate_dirty <= 1'b0;
                end
            end

            if (validating && framebuffer_done) begin
                framebuffer_done_seen <= 1'b1;
                framebuffer_valid_q <= framebuffer_valid;
            end
            if (validating && validators_done) begin
                validating <= 1'b0;
                validation_result_pending_q <= 1'b1;
                validation_result_valid_q <= validators_valid;
            end
            if (validation_result_pending_q) begin
                validation_result_pending_q <= 1'b0;
                if (validation_result_valid_q) begin
                    pending_valid <= 1'b1;
                    pending_scene_enable <= snapshot_scene_enable;
                    pending_framebuffer_enable <=
                        snapshot_framebuffer_enable;
                    pending_framebuffer_format <= snapshot_framebuffer_format;
                    pending_framebuffer_base <= snapshot_framebuffer_base;
                    pending_framebuffer_pitch <= snapshot_framebuffer_pitch;
                    pending_framebuffer_width <= snapshot_framebuffer_width;
                    pending_framebuffer_height <= snapshot_framebuffer_height;
                end else begin
                    pending_valid <= 1'b0;
                    candidates_rejected <= candidates_rejected + 32'd1;
                end
            end
        end
    end

    wire unused_busy = framebuffer_busy;
endmodule

`default_nettype wire
