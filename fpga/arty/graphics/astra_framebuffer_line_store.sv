// Copyright (c) 2026 Astra68 contributors
//
// Four complete canonical framebuffer scanlines. Each pixel is stored as a
// validity bit plus a 32-bit canonical value: INDEX8 in bits 7:0, RGB565 in
// bits 15:0, or ARGB8888 after decoding XRGB8888 source bytes.
`timescale 1ns/1ps
`default_nettype none

module astra_framebuffer_line_store #(
    parameter integer OUTPUT_WIDTH = 1280
) (
    input  wire        build_clk,
    input  wire        write_enable,
    input  wire [1:0]  write_slot,
    input  wire [10:0] write_x,
    input  wire [32:0] write_pixel,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire [1:0]  read_slot,
    input  wire [10:0] read_x,
    output wire        read_valid,
    output wire [31:0] read_pixel
);
    localparam integer ADDRESS_WIDTH = $clog2(OUTPUT_WIDTH);

    // Separate memories avoid a slot multiplier on both BRAM address ports.
    // This is the Vivado simple-dual-port, dual-clock inference template.
    (* ram_style = "block" *) reg [32:0] line0 [0:OUTPUT_WIDTH-1];
    (* ram_style = "block" *) reg [32:0] line1 [0:OUTPUT_WIDTH-1];
    (* ram_style = "block" *) reg [32:0] line2 [0:OUTPUT_WIDTH-1];
    (* ram_style = "block" *) reg [32:0] line3 [0:OUTPUT_WIDTH-1];

    wire [ADDRESS_WIDTH-1:0] write_address =
        write_x[ADDRESS_WIDTH-1:0];
    wire [ADDRESS_WIDTH-1:0] read_address =
        read_x[ADDRESS_WIDTH-1:0];

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd0)
            line0[write_address] <= write_pixel;

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd1)
            line1[write_address] <= write_pixel;

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd2)
            line2[write_address] <= write_pixel;

    always @(posedge build_clk)
        if (write_enable && write_slot == 2'd3)
            line3[write_address] <= write_pixel;

    reg [32:0] read0;
    reg [32:0] read1;
    reg [32:0] read2;
    reg [32:0] read3;
    reg [1:0] read_slot_q;

    always @(posedge pixel_clk)
        read0 <= line0[read_address];

    always @(posedge pixel_clk)
        read1 <= line1[read_address];

    always @(posedge pixel_clk)
        read2 <= line2[read_address];

    always @(posedge pixel_clk)
        read3 <= line3[read_address];

    always @(posedge pixel_clk) begin
        if (pixel_reset)
            read_slot_q <= 2'd0;
        else
            read_slot_q <= read_slot;
    end

    wire [32:0] selected = read_slot_q == 2'd0 ? read0 :
                           read_slot_q == 2'd1 ? read1 :
                           read_slot_q == 2'd2 ? read2 : read3;
    assign read_valid = selected[32];
    assign read_pixel = selected[31:0];
endmodule

`default_nettype wire
