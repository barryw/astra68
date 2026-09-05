// Copyright (c) 2026 Astra68 contributors
//
// Committed framebuffer/tile palettes plus pixel-domain active copies. Host
// writes update the committed baseline and cross through a bounded FIFO. At
// each vblank the complete baseline is replayed before copper execution. The
// active memories use separate pixel-clock read and write ports so an exact
// raster palette MOVE never steals either layer's lookup port.
`timescale 1ns/1ps
`default_nettype none

module astra_palette_store (
    input  wire        control_clk,
    input  wire        control_reset,
    input  wire        baseline_restore_start,
    output wire        baseline_restore_busy,
    output reg         baseline_restore_done,
    output wire        host_write_ready,

    input  wire        framebuffer_write_enable,
    input  wire [7:0]  framebuffer_write_index,
    input  wire [31:0] framebuffer_write_argb,
    input  wire        tile_write_enable,
    input  wire [3:0]  tile_write_bank,
    input  wire [7:0]  tile_write_index,
    input  wire [31:0] tile_write_argb,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire        copper_write_enable,
    input  wire        copper_write_tile,
    input  wire [3:0]  copper_write_bank,
    input  wire [7:0]  copper_write_index,
    input  wire [31:0] copper_write_argb,
    output wire        copper_write_ready,

    input  wire [7:0]  framebuffer_read_index,
    output wire [31:0] framebuffer_read_argb,
    input  wire [3:0]  tile0_read_bank,
    input  wire [7:0]  tile0_read_index,
    output wire [31:0] tile0_read_argb,
    input  wire [3:0]  tile1_read_bank,
    input  wire [7:0]  tile1_read_index,
    output wire [31:0] tile1_read_argb
);
    localparam [2:0] RESTORE_IDLE = 3'd0;
    localparam [2:0] RESTORE_READ_REQUEST = 3'd1;
    localparam [2:0] RESTORE_READ_CAPTURE = 3'd2;
    localparam [2:0] RESTORE_SEND = 3'd3;
    localparam [2:0] RESTORE_WAIT_ACK = 3'd4;
    localparam integer UPDATE_WIDTH = 46;

    reg [2:0] restore_state;
    reg [12:0] restore_index;
    reg restore_tile_q;
    reg [11:0] restore_address_q;
    reg [31:0] restore_data_q;
    reg restore_ack_start_q;

    wire host_framebuffer_write = framebuffer_write_enable &&
                                  !tile_write_enable;
    wire host_tile_write = tile_write_enable &&
                           !framebuffer_write_enable;
    wire host_write = host_framebuffer_write || host_tile_write;
    wire restore_send = restore_state == RESTORE_SEND;
    wire framebuffer_restore_read =
        restore_state == RESTORE_READ_REQUEST && restore_index < 13'd256;
    wire tile_restore_read =
        restore_state == RESTORE_READ_REQUEST && restore_index >= 13'd256;
    wire [31:0] framebuffer_baseline_read_data;
    wire [31:0] tile_baseline_read_data;

    astra_palette_baseline_ram #(
        .ADDRESS_WIDTH(8)
    ) framebuffer_baseline_i (
        .clk(control_clk),
        .write_enable(host_framebuffer_write && host_write_ready),
        .write_address(framebuffer_write_index),
        .write_data(framebuffer_write_argb),
        .read_enable(framebuffer_restore_read),
        .read_address(restore_address_q[7:0]),
        .read_data(framebuffer_baseline_read_data)
    );

    astra_palette_baseline_ram #(
        .ADDRESS_WIDTH(12)
    ) tile_baseline_i (
        .clk(control_clk),
        .write_enable(host_tile_write && host_write_ready),
        .write_address({tile_write_bank, tile_write_index}),
        .write_data(tile_write_argb),
        .read_enable(tile_restore_read),
        .read_address(restore_address_q),
        .read_data(tile_baseline_read_data)
    );

    wire update_fifo_ready;
    reg update_fifo_valid;
    reg [UPDATE_WIDTH-1:0] update_fifo_data;
    wire [4:0] update_write_level;
    wire [UPDATE_WIDTH-1:0] update_pixel_data;
    wire update_pixel_valid;
    wire update_pixel_ready;
    wire [4:0] update_read_level;
    wire update_overflow;
    wire update_underflow;

    assign baseline_restore_busy = restore_state != RESTORE_IDLE;
    assign host_write_ready = restore_state == RESTORE_IDLE &&
                              update_fifo_ready;

    always @* begin
        update_fifo_valid = 1'b0;
        update_fifo_data = {UPDATE_WIDTH{1'b0}};
        if (restore_send) begin
            update_fifo_valid = 1'b1;
            update_fifo_data = {
                restore_index == 13'd4351,
                restore_tile_q,
                restore_address_q,
                restore_data_q
            };
        end else if (host_write && host_write_ready) begin
            update_fifo_valid = 1'b1;
            update_fifo_data = {
                1'b0,
                host_tile_write,
                host_tile_write ? {tile_write_bank, tile_write_index} :
                                  {4'd0, framebuffer_write_index},
                host_tile_write ? tile_write_argb :
                                  framebuffer_write_argb
            };
        end
    end

    reg restore_ack_toggle_pixel;
    (* ASYNC_REG = "TRUE" *) reg restore_ack_meta;
    (* ASYNC_REG = "TRUE" *) reg restore_ack_sync;

    always @(posedge control_clk or posedge control_reset) begin
        if (control_reset) begin
            restore_ack_meta <= 1'b0;
            restore_ack_sync <= 1'b0;
        end else begin
            restore_ack_meta <= restore_ack_toggle_pixel;
            restore_ack_sync <= restore_ack_meta;
        end
    end

    always @(posedge control_clk) begin
        baseline_restore_done <= 1'b0;
        if (control_reset) begin
            restore_state <= RESTORE_IDLE;
            restore_index <= 13'd0;
            restore_tile_q <= 1'b0;
            restore_address_q <= 12'd0;
            restore_data_q <= 32'd0;
            restore_ack_start_q <= 1'b0;
        end else begin
            case (restore_state)
                RESTORE_IDLE: if (baseline_restore_start) begin
                    restore_index <= 13'd0;
                    restore_ack_start_q <= restore_ack_sync;
                    restore_state <= RESTORE_READ_REQUEST;
                end
                RESTORE_READ_REQUEST: begin
                    restore_tile_q <= restore_index >= 13'd256;
                    restore_state <= RESTORE_READ_CAPTURE;
                end
                RESTORE_READ_CAPTURE: begin
                    restore_data_q <= restore_tile_q ?
                        tile_baseline_read_data :
                        framebuffer_baseline_read_data;
                    restore_state <= RESTORE_SEND;
                end
                RESTORE_SEND: if (update_fifo_ready) begin
                    if (restore_index == 13'd4351)
                        restore_state <= RESTORE_WAIT_ACK;
                    else begin
                        restore_index <= restore_index + 13'd1;
                        restore_address_q <= restore_index == 13'd255 ?
                            12'd0 : restore_address_q + 12'd1;
                        restore_state <= RESTORE_READ_REQUEST;
                    end
                end
                RESTORE_WAIT_ACK: if (restore_ack_sync !=
                                         restore_ack_start_q) begin
                    baseline_restore_done <= 1'b1;
                    restore_state <= RESTORE_IDLE;
                end
                default: restore_state <= RESTORE_IDLE;
            endcase
        end
    end

    astra_async_fifo #(
        .DATA_WIDTH(UPDATE_WIDTH),
        .ADDR_WIDTH(4)
    ) update_fifo_i (
        .wr_clk(control_clk),
        .wr_rst(control_reset),
        .wr_data(update_fifo_data),
        .wr_valid(update_fifo_valid),
        .wr_ready(update_fifo_ready),
        .wr_level(update_write_level),
        .rd_clk(pixel_clk),
        .rd_rst(pixel_reset),
        .rd_data(update_pixel_data),
        .rd_valid(update_pixel_valid),
        .rd_ready(update_pixel_ready),
        .rd_level(update_read_level),
        .overflow(update_overflow),
        .underflow(update_underflow)
    );

    wire update_last = update_pixel_data[45];
    wire update_tile = update_pixel_data[44];
    wire [11:0] update_address = update_pixel_data[43:32];
    wire [31:0] update_argb = update_pixel_data[31:0];
    assign copper_write_ready = !update_pixel_valid;
    assign update_pixel_ready = update_pixel_valid && !copper_write_enable;

    wire active_write = copper_write_enable && copper_write_ready ||
                        update_pixel_ready;
    wire active_write_tile = copper_write_enable ? copper_write_tile :
                                                    update_tile;
    wire [11:0] active_write_address = copper_write_enable ?
        {copper_write_bank, copper_write_index} : update_address;
    wire [31:0] active_write_argb = copper_write_enable ?
        copper_write_argb : update_argb;
    wire [11:0] tile0_read_address =
        {tile0_read_bank, tile0_read_index};
    wire [11:0] tile1_read_address =
        {tile1_read_bank, tile1_read_index};

    wire framebuffer_active_write = active_write && !active_write_tile;
    wire tile_active_write = active_write && active_write_tile;

    // Keep each scanout lookup in an independent true block RAM.  Expressing
    // all three memories in the control process below made Vivado reject the
    // block-RAM attribute and consume thousands of LUTRAMs instead.
    astra_palette_active_ram #(
        .ADDRESS_WIDTH(8)
    ) framebuffer_active_i (
        .clk(pixel_clk),
        .write_enable(framebuffer_active_write),
        .write_address(active_write_address[7:0]),
        .write_data(active_write_argb),
        .read_address(framebuffer_read_index),
        .read_data(framebuffer_read_argb)
    );

    astra_palette_active_ram #(
        .ADDRESS_WIDTH(12)
    ) tile_active0_i (
        .clk(pixel_clk),
        .write_enable(tile_active_write),
        .write_address(active_write_address),
        .write_data(active_write_argb),
        .read_address(tile0_read_address),
        .read_data(tile0_read_argb)
    );

    astra_palette_active_ram #(
        .ADDRESS_WIDTH(12)
    ) tile_active1_i (
        .clk(pixel_clk),
        .write_enable(tile_active_write),
        .write_address(active_write_address),
        .write_data(active_write_argb),
        .read_address(tile1_read_address),
        .read_data(tile1_read_argb)
    );

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            restore_ack_toggle_pixel <= 1'b0;
        end else begin
            if (update_pixel_ready && update_last)
                restore_ack_toggle_pixel <= ~restore_ack_toggle_pixel;
        end
    end

    wire unused_fifo_status = &{
        1'b0, update_write_level, update_read_level,
        update_overflow, update_underflow
    };
endmodule

module astra_palette_baseline_ram #(
    parameter integer ADDRESS_WIDTH = 12
) (
    input  wire                     clk,
    input  wire                     write_enable,
    input  wire [ADDRESS_WIDTH-1:0] write_address,
    input  wire [31:0]              write_data,
    input  wire                     read_enable,
    input  wire [ADDRESS_WIDTH-1:0] read_address,
    output reg  [31:0]              read_data
);
    localparam integer DEPTH = 1 << ADDRESS_WIDTH;
    (* ram_style = "block" *) reg [31:0] memory [0:DEPTH-1];

    always @(posedge clk) begin
        if (write_enable)
            memory[write_address] <= write_data;
        if (read_enable)
            read_data <= memory[read_address];
    end
endmodule

module astra_palette_active_ram #(
    parameter integer ADDRESS_WIDTH = 12
) (
    input  wire                     clk,
    input  wire                     write_enable,
    input  wire [ADDRESS_WIDTH-1:0] write_address,
    input  wire [31:0]              write_data,
    input  wire [ADDRESS_WIDTH-1:0] read_address,
    output wire [31:0]              read_data
);
    localparam integer DEPTH = 1 << ADDRESS_WIDTH;
    (* ram_style = "block" *) reg [31:0] memory [0:DEPTH-1];
    reg [31:0] memory_read_data_q;
    reg [31:0] forwarded_write_data_q;
    reg forwarded_write_q;

    assign read_data = forwarded_write_q ? forwarded_write_data_q :
                                             memory_read_data_q;

    always @(posedge clk) begin
        if (write_enable)
            memory[write_address] <= write_data;
        memory_read_data_q <= memory[read_address];
        forwarded_write_q <= write_enable &&
                             write_address == read_address;
        forwarded_write_data_q <= write_data;
    end
endmodule

`default_nettype wire
