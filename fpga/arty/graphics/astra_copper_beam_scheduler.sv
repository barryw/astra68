// Copyright (c) 2026 Astra68 contributors
//
// Advances the copper's virtual beam only as far as line construction permits.
// The renderer may build ahead, but a line cannot launch until copper has
// stopped or reached a WAIT beyond the state required by that line.
`timescale 1ns/1ps
`default_nettype none

module astra_copper_beam_scheduler #(
    parameter integer OUTPUT_HEIGHT = 720,
    parameter integer TOTAL_WIDTH = 1650,
    parameter integer TOTAL_HEIGHT = 750
) (
    input  wire        clk,
    input  wire        reset,
    input  wire        frame_start,
    input  wire        baseline_ready,
    input  wire        copper_enabled,
    input  wire        copper_running,
    input  wire        copper_waiting,

    input  wire        line_prepare_valid,
    input  wire [9:0]  line_prepare_y,
    output wire        line_prepare_ready,

    output reg  [10:0] beam_x,
    output reg  [9:0]  beam_y
);
    localparam [10:0] LAST_BEAM_X = TOTAL_WIDTH - 1;
    localparam [9:0] LAST_BEAM_Y = TOTAL_HEIGHT - 1;
    localparam [9:0] LAST_ACTIVE_Y = OUTPUT_HEIGHT - 1;

    reg prepare_active_q;
    reg [9:0] prepared_y_q;
    reg finalize_pending_q;

    wire copper_settled = !copper_enabled || !copper_running ||
                           copper_waiting;
    assign line_prepare_ready = prepare_active_q && baseline_ready &&
        line_prepare_valid && line_prepare_y == prepared_y_q &&
        copper_settled;

    always @(posedge clk) begin
        if (reset || frame_start) begin
            prepare_active_q <= 1'b0;
            prepared_y_q <= 10'd0;
            finalize_pending_q <= 1'b0;
            beam_x <= 11'd0;
            beam_y <= 10'd0;
        end else begin
            if (!prepare_active_q && line_prepare_valid) begin
                prepare_active_q <= 1'b1;
                prepared_y_q <= line_prepare_y;
                if (line_prepare_y == 10'd0) begin
                    beam_x <= 11'd0;
                    beam_y <= 10'd0;
                end else begin
                    beam_x <= LAST_BEAM_X;
                    beam_y <= line_prepare_y - 10'd1;
                end
            end

            if (line_prepare_ready) begin
                prepare_active_q <= 1'b0;
                if (prepared_y_q == LAST_ACTIVE_Y)
                    finalize_pending_q <= 1'b1;
            end

            // Once line 719 has captured all state inherited from line 718,
            // the remaining active line and vertical blank may execute. Their
            // next-scanline writes cannot affect an already launched line.
            if (finalize_pending_q) begin
                beam_x <= LAST_BEAM_X;
                beam_y <= LAST_BEAM_Y;
                finalize_pending_q <= 1'b0;
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (OUTPUT_HEIGHT < 1 || OUTPUT_HEIGHT > TOTAL_HEIGHT)
            $fatal(1, "invalid copper beam output height");
        if (TOTAL_WIDTH < 1 || TOTAL_WIDTH > 2048 ||
            TOTAL_HEIGHT < 1 || TOTAL_HEIGHT > 1024)
            $fatal(1, "copper beam dimensions exceed coordinate width");
    end
`endif
endmodule

`default_nettype wire
