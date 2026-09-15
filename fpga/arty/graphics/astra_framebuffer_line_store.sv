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
    input  wire [1:0]  write_enable,
    input  wire [1:0]  write_slot,
    input  wire [10:0] write_x,
    input  wire [32:0] write_pixel0,
    input  wire [32:0] write_pixel1,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire [1:0]  read_slot,
    input  wire [10:0] read_x,
    output wire        read_valid,
    output wire [31:0] read_pixel
);
    localparam integer BANK_DEPTH = (OUTPUT_WIDTH + 1) / 2;
    localparam integer ADDRESS_WIDTH = $clog2(BANK_DEPTH);

    // Even and odd banks accept two consecutive pixels per build clock while
    // retaining one pixel-clock read port. Total stored bits are unchanged.
    (* ram_style = "block" *) reg [32:0] line0_even [0:BANK_DEPTH-1];
    (* ram_style = "block" *) reg [32:0] line0_odd [0:BANK_DEPTH-1];
    (* ram_style = "block" *) reg [32:0] line1_even [0:BANK_DEPTH-1];
    (* ram_style = "block" *) reg [32:0] line1_odd [0:BANK_DEPTH-1];
    (* ram_style = "block" *) reg [32:0] line2_even [0:BANK_DEPTH-1];
    (* ram_style = "block" *) reg [32:0] line2_odd [0:BANK_DEPTH-1];
    (* ram_style = "block" *) reg [32:0] line3_even [0:BANK_DEPTH-1];
    (* ram_style = "block" *) reg [32:0] line3_odd [0:BANK_DEPTH-1];

    wire [10:0] write_x1 = write_x + 11'd1;
    wire [ADDRESS_WIDTH-1:0] write_address0 = write_x[10:1];
    wire [ADDRESS_WIDTH-1:0] write_address1 = write_x1[10:1];
    wire [ADDRESS_WIDTH-1:0] read_address =
        read_x[10:1];

    always @(posedge build_clk)
        if (write_slot == 2'd0) begin
            if (write_enable[0] && !write_x[0])
                line0_even[write_address0] <= write_pixel0;
            else if (write_enable[1] && !write_x1[0])
                line0_even[write_address1] <= write_pixel1;
        end

    always @(posedge build_clk)
        if (write_slot == 2'd0) begin
            if (write_enable[0] && write_x[0])
                line0_odd[write_address0] <= write_pixel0;
            else if (write_enable[1] && write_x1[0])
                line0_odd[write_address1] <= write_pixel1;
        end

    always @(posedge build_clk)
        if (write_slot == 2'd1) begin
            if (write_enable[0] && !write_x[0])
                line1_even[write_address0] <= write_pixel0;
            else if (write_enable[1] && !write_x1[0])
                line1_even[write_address1] <= write_pixel1;
        end

    always @(posedge build_clk)
        if (write_slot == 2'd1) begin
            if (write_enable[0] && write_x[0])
                line1_odd[write_address0] <= write_pixel0;
            else if (write_enable[1] && write_x1[0])
                line1_odd[write_address1] <= write_pixel1;
        end

    always @(posedge build_clk)
        if (write_slot == 2'd2) begin
            if (write_enable[0] && !write_x[0])
                line2_even[write_address0] <= write_pixel0;
            else if (write_enable[1] && !write_x1[0])
                line2_even[write_address1] <= write_pixel1;
        end

    always @(posedge build_clk)
        if (write_slot == 2'd2) begin
            if (write_enable[0] && write_x[0])
                line2_odd[write_address0] <= write_pixel0;
            else if (write_enable[1] && write_x1[0])
                line2_odd[write_address1] <= write_pixel1;
        end

    always @(posedge build_clk)
        if (write_slot == 2'd3) begin
            if (write_enable[0] && !write_x[0])
                line3_even[write_address0] <= write_pixel0;
            else if (write_enable[1] && !write_x1[0])
                line3_even[write_address1] <= write_pixel1;
        end

    always @(posedge build_clk)
        if (write_slot == 2'd3) begin
            if (write_enable[0] && write_x[0])
                line3_odd[write_address0] <= write_pixel0;
            else if (write_enable[1] && write_x1[0])
                line3_odd[write_address1] <= write_pixel1;
        end

    reg [32:0] read0_even, read0_odd;
    reg [32:0] read1_even, read1_odd;
    reg [32:0] read2_even, read2_odd;
    reg [32:0] read3_even, read3_odd;
    reg [1:0] read_slot_q;
    reg read_odd_q;

    always @(posedge pixel_clk)
        read0_even <= line0_even[read_address];

    always @(posedge pixel_clk)
        read0_odd <= line0_odd[read_address];

    always @(posedge pixel_clk)
        read1_even <= line1_even[read_address];

    always @(posedge pixel_clk)
        read1_odd <= line1_odd[read_address];

    always @(posedge pixel_clk)
        read2_even <= line2_even[read_address];

    always @(posedge pixel_clk)
        read2_odd <= line2_odd[read_address];

    always @(posedge pixel_clk)
        read3_even <= line3_even[read_address];

    always @(posedge pixel_clk)
        read3_odd <= line3_odd[read_address];

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            read_slot_q <= 2'd0;
            read_odd_q <= 1'b0;
        end else begin
            read_slot_q <= read_slot;
            read_odd_q <= read_x[0];
        end
    end

    wire [32:0] selected_even = read_slot_q == 2'd0 ? read0_even :
                                read_slot_q == 2'd1 ? read1_even :
                                read_slot_q == 2'd2 ? read2_even : read3_even;
    wire [32:0] selected_odd = read_slot_q == 2'd0 ? read0_odd :
                               read_slot_q == 2'd1 ? read1_odd :
                               read_slot_q == 2'd2 ? read2_odd : read3_odd;
    wire [32:0] selected = read_odd_q ? selected_odd : selected_even;
    assign read_valid = selected[32];
    assign read_pixel = selected[31:0];
endmodule

`default_nettype wire
