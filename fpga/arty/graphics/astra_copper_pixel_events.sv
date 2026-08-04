// Copyright (c) 2026 Astra68 contributors
//
// Transfers exact copper beam effects from the ahead-of-scanline renderer
// clock to the pixel-source boundary. Coordinates refer to the source pixel
// entering the compositor, not the later physical output of its pipeline.
`timescale 1ns/1ps
`default_nettype none

module astra_copper_pixel_events #(
    parameter integer ADDR_WIDTH = 12,
    parameter integer OUTPUT_HEIGHT = 720
) (
    input  wire        build_clk,
    input  wire        build_reset,
    input  wire        enqueue_frame,
    input  wire        enqueue_irq,
    input  wire [9:0]  enqueue_y,
    input  wire [10:0] enqueue_x,
    input  wire [15:0] enqueue_target,
    input  wire [31:0] enqueue_data,
    input  wire        enqueue_valid,
    output wire        enqueue_ready,
    output wire [ADDR_WIDTH:0] enqueue_level,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire        pixel_frame,
    input  wire        source_valid,
    input  wire [9:0]  source_y,
    input  wire [10:0] source_x,
    output wire        event_irq,
    output wire [9:0]  event_y,
    output wire [10:0] event_x,
    output wire [15:0] event_target,
    output wire [31:0] event_data,
    output wire        event_valid,
    input  wire        event_ready,
    output wire [ADDR_WIDTH:0] event_level,

    output wire        overflow,
    output reg         stale_event,
    output reg         late_event
);
    localparam integer RECORD_WIDTH = 72;

    wire [RECORD_WIDTH-1:0] fifo_write_data = {
        enqueue_frame, enqueue_irq, enqueue_y, enqueue_x,
        enqueue_target, enqueue_data, 1'b0
    };
    wire [RECORD_WIDTH-1:0] fifo_read_data;
    wire fifo_read_valid;
    wire fifo_underflow;

    wire record_frame = fifo_read_data[71];
    assign event_irq = fifo_read_data[70];
    assign event_y = fifo_read_data[69:60];
    assign event_x = fifo_read_data[59:49];
    assign event_target = fifo_read_data[48:33];
    assign event_data = fifo_read_data[32:1];

    wire frame_matches = record_frame == pixel_frame;
    // Vertical-blank events belong to the frame whose active line zero follows
    // that blank. The source stream is prefetched and contains active pixels
    // only, so line zero is the first observable point after such an event.
    wire event_in_vertical_blank = event_y >= OUTPUT_HEIGHT;
    wire coordinate_reached = event_in_vertical_blank ?
        source_y < OUTPUT_HEIGHT :
        (source_y > event_y ||
         (source_y == event_y && source_x >= event_x));
    wire coordinate_late = event_in_vertical_blank ?
        1'b0 :
        (source_y > event_y ||
         (source_y == event_y && source_x > event_x));
    assign event_valid = fifo_read_valid && frame_matches && source_valid &&
                         coordinate_reached;
    wire discard_stale = fifo_read_valid && !frame_matches;
    wire fifo_read_ready = discard_stale || (event_valid && event_ready);

    astra_async_fifo #(
        .DATA_WIDTH(RECORD_WIDTH),
        .ADDR_WIDTH(ADDR_WIDTH)
    ) event_fifo_i (
        .wr_clk(build_clk),
        .wr_rst(build_reset),
        .wr_data(fifo_write_data),
        .wr_valid(enqueue_valid),
        .wr_ready(enqueue_ready),
        .wr_level(enqueue_level),
        .rd_clk(pixel_clk),
        .rd_rst(pixel_reset),
        .rd_data(fifo_read_data),
        .rd_valid(fifo_read_valid),
        .rd_ready(fifo_read_ready),
        .rd_level(event_level),
        .overflow(overflow),
        .underflow(fifo_underflow)
    );

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            stale_event <= 1'b0;
            late_event <= 1'b0;
        end else begin
            if (discard_stale)
                stale_event <= 1'b1;
            if (event_valid && event_ready && coordinate_late)
                late_event <= 1'b1;
        end
    end

    wire unused_underflow = fifo_underflow;
endmodule

`default_nettype wire
