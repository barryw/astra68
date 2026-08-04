// Copyright (c) 2026 Astra68 contributors
//
// Four complete tile-layer scanlines. The build side never writes the slot
// selected by scanout; ownership is transferred only after line_complete.
`timescale 1ns/1ps
`default_nettype none

module astra_tile_line_store #(
    parameter integer OUTPUT_WIDTH = 1280
) (
    input  wire        build_clk,
    input  wire        write_enable,
    input  wire [1:0]  write_slot,
    input  wire [8:0]  write_quad,
    input  wire [51:0] write_pixels,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire [1:0]  read_slot,
    input  wire [10:0] read_x,
    output wire        read_valid,
    output wire [3:0]  read_palette_bank,
    output wire [7:0]  read_index
);
    localparam integer LINE_QUADS = (OUTPUT_WIDTH + 3) / 4;
    localparam integer LINE_ADDRESS_WIDTH = $clog2(LINE_QUADS);

    // Each slot maps to one independent 320x52 true dual-port RAM. Keeping
    // the slots separate removes a multiplier from both BRAM address paths.
    (* ram_style = "block" *) reg [51:0] line0 [0:LINE_QUADS-1];
    (* ram_style = "block" *) reg [51:0] line1 [0:LINE_QUADS-1];
    (* ram_style = "block" *) reg [51:0] line2 [0:LINE_QUADS-1];
    (* ram_style = "block" *) reg [51:0] line3 [0:LINE_QUADS-1];

    wire [LINE_ADDRESS_WIDTH-1:0] write_address =
        write_quad[LINE_ADDRESS_WIDTH-1:0];
    wire [LINE_ADDRESS_WIDTH-1:0] read_address =
        read_x[LINE_ADDRESS_WIDTH+1:2];
    reg [1:0] read_slot_q;
    reg [1:0] read_select_q;
    reg [51:0] read_quad0;
    reg [51:0] read_quad1;
    reg [51:0] read_quad2;
    reg [51:0] read_quad3;
    wire [51:0] read_quad = read_slot_q == 2'd0 ? read_quad0 :
                            read_slot_q == 2'd1 ? read_quad1 :
                            read_slot_q == 2'd2 ? read_quad2 :
                                                 read_quad3;
    wire [12:0] read_pixel = read_select_q == 2'd0 ? read_quad[12:0] :
                             read_select_q == 2'd1 ? read_quad[25:13] :
                             read_select_q == 2'd2 ? read_quad[38:26] :
                                                     read_quad[51:39];

    assign read_valid = read_pixel[12];
    assign read_palette_bank = read_pixel[11:8];
    assign read_index = read_pixel[7:0];

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd0)
            line0[write_address] <= write_pixels;

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd1)
            line1[write_address] <= write_pixels;

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd2)
            line2[write_address] <= write_pixels;

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd3)
            line3[write_address] <= write_pixels;

    // Keep one independent read process per memory. This is the Vivado
    // simple-dual-port, dual-clock template; sharing the read register behind
    // a case statement causes these 100 Kib line stores to become LUTRAM.
    always @(posedge pixel_clk)
        read_quad0 <= line0[read_address];

    always @(posedge pixel_clk)
        read_quad1 <= line1[read_address];

    always @(posedge pixel_clk)
        read_quad2 <= line2[read_address];

    always @(posedge pixel_clk)
        read_quad3 <= line3[read_address];

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            read_slot_q <= 2'd0;
            read_select_q <= 2'd0;
        end else begin
            read_slot_q <= read_slot;
            read_select_q <= read_x[1:0];
        end
    end
endmodule

`default_nettype wire
