// Copyright (c) 2026 Astra68 contributors
//
// Four complete front and behind premultiplied sprite scanlines. Four physical
// lane banks allow the builder to publish one four-pixel quad per 200 MHz
// clock while the pixel domain reads one pixel per 74.25 MHz clock.
`timescale 1ns/1ps
`default_nettype none

module astra_sprite_line_store #(
    parameter integer OUTPUT_WIDTH = 1280
) (
    input  wire         build_clk,
    input  wire         write_enable,
    input  wire [1:0]   write_slot,
    input  wire [8:0]   write_quad,
    input  wire [127:0] write_front,
    input  wire [127:0] write_behind,

    input  wire         pixel_clk,
    input  wire         pixel_reset,
    input  wire [1:0]   read_slot,
    input  wire [10:0]  read_x,
    output wire [31:0]  read_front,
    output wire [31:0]  read_behind
);
    localparam integer QUADS = (OUTPUT_WIDTH + 3) / 4;
    localparam integer STORAGE_DEPTH = 2048;

    (* ram_style = "block" *) reg [31:0] front0 [0:STORAGE_DEPTH-1];
    (* ram_style = "block" *) reg [31:0] front1 [0:STORAGE_DEPTH-1];
    (* ram_style = "block" *) reg [31:0] front2 [0:STORAGE_DEPTH-1];
    (* ram_style = "block" *) reg [31:0] front3 [0:STORAGE_DEPTH-1];
    (* ram_style = "block" *) reg [31:0] behind0 [0:STORAGE_DEPTH-1];
    (* ram_style = "block" *) reg [31:0] behind1 [0:STORAGE_DEPTH-1];
    (* ram_style = "block" *) reg [31:0] behind2 [0:STORAGE_DEPTH-1];
    (* ram_style = "block" *) reg [31:0] behind3 [0:STORAGE_DEPTH-1];

    wire [10:0] build_address = {write_slot, write_quad};
    wire [8:0] pixel_quad = read_x[10:2];
    wire [10:0] pixel_address = {read_slot, pixel_quad};

    always @(posedge build_clk) begin
        if (write_enable && write_quad < QUADS) begin
            front0[build_address] <= write_front[31:0];
            front1[build_address] <= write_front[63:32];
            front2[build_address] <= write_front[95:64];
            front3[build_address] <= write_front[127:96];
            behind0[build_address] <= write_behind[31:0];
            behind1[build_address] <= write_behind[63:32];
            behind2[build_address] <= write_behind[95:64];
            behind3[build_address] <= write_behind[127:96];
        end
    end

    reg [31:0] front0_q;
    reg [31:0] front1_q;
    reg [31:0] front2_q;
    reg [31:0] front3_q;
    reg [31:0] behind0_q;
    reg [31:0] behind1_q;
    reg [31:0] behind2_q;
    reg [31:0] behind3_q;
    reg [1:0] read_lane_q;

    always @(posedge pixel_clk) begin
        front0_q <= front0[pixel_address];
        front1_q <= front1[pixel_address];
        front2_q <= front2[pixel_address];
        front3_q <= front3[pixel_address];
        behind0_q <= behind0[pixel_address];
        behind1_q <= behind1[pixel_address];
        behind2_q <= behind2[pixel_address];
        behind3_q <= behind3[pixel_address];
        if (pixel_reset)
            read_lane_q <= 2'd0;
        else
            read_lane_q <= read_x[1:0];
    end

    assign read_front = read_lane_q == 2'd0 ? front0_q :
                        read_lane_q == 2'd1 ? front1_q :
                        read_lane_q == 2'd2 ? front2_q : front3_q;
    assign read_behind = read_lane_q == 2'd0 ? behind0_q :
                         read_lane_q == 2'd1 ? behind1_q :
                         read_lane_q == 2'd2 ? behind2_q : behind3_q;
endmodule

`default_nettype wire
