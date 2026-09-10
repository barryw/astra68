// Copyright (c) 2026 Astra68 contributors
//
// Replays a completed composed scanline for vertically scaled rows. This keeps
// exact copper and palette effects identical across every physical copy while
// unique source rows continue through the normal one-pixel-per-clock path.
`timescale 1ns/1ps
`default_nettype none

module astra_scanline_replay #(
    parameter integer OUTPUT_WIDTH = 1920,
    parameter integer TOTAL_WIDTH = 2200
) (
    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire [11:0] pixel_x,
    input  wire        next_frame,
    input  wire        line_source_active,
    input  wire [10:0] line_source_y,
    input  wire        input_valid,
    input  wire [23:0] input_rgb,
    output wire        output_valid,
    output wire [23:0] output_rgb,
    output wire        replaying
);
    (* ramstyle = "M20K, no_rw_check" *)
    reg [23:0] line_memory [0:OUTPUT_WIDTH-1];
    reg [23:0] replay_rgb_q;
    reg repeat_line_q;
    reg last_source_valid_q;
    reg [10:0] last_source_y_q;
    wire [10:0] replay_read_x = pixel_x < OUTPUT_WIDTH - 1 ?
        pixel_x[10:0] + 11'd1 : 11'd0;

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            replay_rgb_q <= 24'd0;
            repeat_line_q <= 1'b0;
            last_source_valid_q <= 1'b0;
            last_source_y_q <= 11'd0;
        end else begin
            replay_rgb_q <= line_memory[replay_read_x];

            if (pixel_x < OUTPUT_WIDTH && line_source_active &&
                !repeat_line_q)
                line_memory[pixel_x[10:0]] <= input_valid ? input_rgb : 24'd0;

            if (pixel_x == TOTAL_WIDTH - 1) begin
                repeat_line_q <= !next_frame && line_source_active &&
                    last_source_valid_q &&
                    line_source_y == last_source_y_q;
                if (line_source_active) begin
                    last_source_valid_q <= 1'b1;
                    last_source_y_q <= line_source_y;
                end else begin
                    last_source_valid_q <= 1'b0;
                end
            end
        end
    end

    assign replaying = repeat_line_q;
    assign output_valid = repeat_line_q ? line_source_active : input_valid;
    assign output_rgb = repeat_line_q ? replay_rgb_q : input_rgb;

`ifndef SYNTHESIS
    initial begin
        if (OUTPUT_WIDTH < 1 || OUTPUT_WIDTH > 2048 ||
            TOTAL_WIDTH <= OUTPUT_WIDTH || TOTAL_WIDTH > 4096)
            $fatal(1, "invalid scanline replay geometry");
    end
`endif
endmodule

`default_nettype wire
