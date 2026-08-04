// Copyright (c) 2026 Astra68 contributors
//
// Deterministic 1280x720 qualification raster. This is the reset-time output
// of the first Arty shell, not an application-visible graphics interface.
`timescale 1ns/1ps
`default_nettype none

module astra_720p_pattern (
    input  wire [10:0] x,
    input  wire [9:0]  y,
    output reg  [23:0] rgb
);
    localparam [23:0] WHITE   = 24'hffffff;
    localparam [23:0] YELLOW  = 24'hffff00;
    localparam [23:0] CYAN    = 24'h00ffff;
    localparam [23:0] GREEN   = 24'h00ff00;
    localparam [23:0] MAGENTA = 24'hff00ff;
    localparam [23:0] RED     = 24'hff0000;
    localparam [23:0] BLUE    = 24'h0000ff;
    localparam [23:0] BLACK   = 24'h000000;
    localparam [23:0] GRID    = 24'h304050;
    localparam [23:0] ACCENT  = 24'h00dce8;

    always @* begin
        rgb = BLACK;

        if (x < 11'd1280 && y < 10'd720) begin
            if (x == 11'd0 || x == 11'd1279 ||
                y == 10'd0 || y == 10'd719) begin
                rgb = WHITE;
            end else if (y < 10'd120) begin
                if (x < 11'd160)
                    rgb = WHITE;
                else if (x < 11'd320)
                    rgb = YELLOW;
                else if (x < 11'd480)
                    rgb = CYAN;
                else if (x < 11'd640)
                    rgb = GREEN;
                else if (x < 11'd800)
                    rgb = MAGENTA;
                else if (x < 11'd960)
                    rgb = RED;
                else if (x < 11'd1120)
                    rgb = BLUE;
                else
                    rgb = BLACK;
            end else if ((x >= 11'd384 && x < 11'd896 &&
                          (y == 10'd256 || y == 10'd511)) ||
                         (y >= 10'd256 && y < 10'd512 &&
                          (x == 11'd384 || x == 11'd895))) begin
                rgb = ACCENT;
            end else if (x[5:0] == 6'd0 || y[5:0] == 6'd0) begin
                rgb = GRID;
            end else begin
                rgb = {x[7:0], y[7:0], x[8:1] ^ y[7:0]};
            end
        end
    end
endmodule

`default_nettype wire
