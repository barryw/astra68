// Copyright (c) 2026 Astra68 contributors
//
// Boot-only tear-free text overlay for the Arty splash screen. The ARM writes
// an inactive character bank through a bundled-data CDC mailbox. A commit
// swaps banks only at vertical blank, then clones the visible bank back to the
// shadow bank so later software updates can replace one row without rebuilding
// the complete plane.
`timescale 1ns/1ps
`default_nettype none

module astra_boot_text_overlay #(
    parameter FONT_HEX = "post_fonts.hex",
    parameter integer COLS = 36,
    parameter integer ROWS = 4,
    parameter integer ORIGIN_X = 264,
    parameter integer ORIGIN_Y = 496,
    parameter integer CELL_WIDTH = 16,
    parameter integer ROW_PITCH = 32
) (
    input  wire        build_clk,
    input  wire        build_reset,
    input  wire        shadow_enable,
    input  wire        write_strobe,
    input  wire [7:0]  write_index,
    input  wire [15:0] write_cell,
    output wire        write_ready,
    input  wire        commit_strobe,
    output wire        commit_ready,
    output wire        active_enable,
    output reg  [31:0] generation,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire        pixel_frame_boundary,
    input  wire        input_valid,
    input  wire [10:0] pixel_x,
    input  wire [9:0]  pixel_y,
    input  wire [23:0] input_rgb,
    output wire        output_valid,
    output reg  [23:0] output_rgb
);
    localparam integer CELLS = COLS * ROWS;
    localparam integer FONT_BYTES = 256 * 8;
    localparam integer TEXT_WIDTH = COLS * CELL_WIDTH;
    localparam integer TEXT_HEIGHT = ROWS * ROW_PITCH;

    // Cell bits 7:0 select CP437, bits 9:8 select the fixed boot palette.
    // The memories live entirely in the pixel domain; the mailbox below is
    // the only control-to-pixel crossing.
    (* ram_style = "distributed", ramstyle = "MLAB" *) reg [15:0] cell_bank0 [0:CELLS-1];
    (* ram_style = "distributed", ramstyle = "MLAB" *) reg [15:0] cell_bank1 [0:CELLS-1];
    (* rom_style = "distributed" *) reg [7:0] font_rom [0:FONT_BYTES-1];

    integer init_index;
    initial begin
        for (init_index = 0; init_index < CELLS;
             init_index = init_index + 1) begin
            cell_bank0[init_index] = 16'h0020;
            cell_bank1[init_index] = 16'h0020;
        end
        $readmemh(FONT_HEX, font_rom, 0, FONT_BYTES - 1);
    end

    reg [7:0] write_index_hold;
    reg [15:0] write_cell_hold;
    reg write_request_toggle;
    reg commit_enable_hold;
    reg commit_request_toggle;

    reg write_ack_toggle_pixel;
    reg commit_ack_toggle_pixel;
    reg active_bank_pixel;
    reg active_enable_pixel;
    reg commit_waiting_pixel;
    reg clone_active_pixel;
    reg [7:0] clone_index_pixel;

    (* ASYNC_REG = "TRUE" *) reg write_request_meta;
    (* ASYNC_REG = "TRUE" *) reg write_request_sync;
    (* ASYNC_REG = "TRUE" *) reg [7:0] write_index_meta;
    (* ASYNC_REG = "TRUE" *) reg [7:0] write_index_sync;
    (* ASYNC_REG = "TRUE" *) reg [15:0] write_cell_meta;
    (* ASYNC_REG = "TRUE" *) reg [15:0] write_cell_sync;
    (* ASYNC_REG = "TRUE" *) reg commit_request_meta;
    (* ASYNC_REG = "TRUE" *) reg commit_request_sync;
    (* ASYNC_REG = "TRUE" *) reg commit_enable_meta;
    (* ASYNC_REG = "TRUE" *) reg commit_enable_sync;

    // hdl-util-hdmi presents the pixel requested in this cycle for capture on
    // the next rising edge. Look two pixels ahead so the cell RAM and font ROM
    // each get a register stage before the final HDMI video-data path.
    wire [10:0] render_x = pixel_x + 11'd2;
    wire in_text_bounds =
        render_x >= ORIGIN_X && render_x < ORIGIN_X + TEXT_WIDTH &&
        pixel_y >= ORIGIN_Y && pixel_y < ORIGIN_Y + TEXT_HEIGHT;
    wire [10:0] relative_x = render_x - ORIGIN_X;
    wire [9:0] relative_y = pixel_y - ORIGIN_Y;
    wire [5:0] text_col = relative_x / CELL_WIDTH;
    wire [1:0] text_row = relative_y / ROW_PITCH;
    wire [3:0] cell_x = relative_x % CELL_WIDTH;
    wire [4:0] cell_y = relative_y % ROW_PITCH;
    wire glyph_region = in_text_bounds && cell_y < 16;
    wire [2:0] glyph_col = cell_x[3:1];
    wire [2:0] glyph_row = cell_y[3:1];
    wire [7:0] cell_address_raw = text_row * COLS + text_col;
    wire [7:0] cell_address = in_text_bounds ? cell_address_raw : 8'd0;

    wire apply_mailbox_write = !clone_active_pixel &&
        !commit_waiting_pixel &&
        write_request_sync != write_ack_toggle_pixel;
    wire apply_clone_write = clone_active_pixel;
    wire bank0_write_enable = active_bank_pixel &&
        (apply_mailbox_write || apply_clone_write);
    wire bank1_write_enable = !active_bank_pixel &&
        (apply_mailbox_write || apply_clone_write);
    wire [7:0] memory_write_index = apply_mailbox_write ?
        write_index_sync : clone_index_pixel;
    wire [7:0] memory_read_index = clone_active_pixel ?
        clone_index_pixel : cell_address;
    wire [15:0] bank0_read_data = cell_bank0[memory_read_index];
    wire [15:0] bank1_read_data = cell_bank1[memory_read_index];
    wire [15:0] bank0_write_data = apply_mailbox_write ?
        write_cell_sync : bank1_read_data;
    wire [15:0] bank1_write_data = apply_mailbox_write ?
        write_cell_sync : bank0_read_data;

    always @(posedge pixel_clk) begin
        if (bank0_write_enable)
            cell_bank0[memory_write_index] <= bank0_write_data;
        if (bank1_write_enable)
            cell_bank1[memory_write_index] <= bank1_write_data;
    end

    (* ASYNC_REG = "TRUE" *) reg write_ack_meta;
    (* ASYNC_REG = "TRUE" *) reg write_ack_sync;
    (* ASYNC_REG = "TRUE" *) reg commit_ack_meta;
    (* ASYNC_REG = "TRUE" *) reg commit_ack_sync;
    (* ASYNC_REG = "TRUE" *) reg active_enable_meta;
    (* ASYNC_REG = "TRUE" *) reg active_enable_sync;
    reg commit_ack_seen;

    assign write_ready = write_ack_sync == write_request_toggle &&
                         commit_ack_sync == commit_request_toggle;
    assign commit_ready = write_ready;
    assign active_enable = active_enable_sync;

    always @(posedge build_clk or posedge build_reset) begin
        if (build_reset) begin
            write_ack_meta <= 1'b0;
            write_ack_sync <= 1'b0;
            commit_ack_meta <= 1'b0;
            commit_ack_sync <= 1'b0;
            active_enable_meta <= 1'b0;
            active_enable_sync <= 1'b0;
        end else begin
            write_ack_meta <= write_ack_toggle_pixel;
            write_ack_sync <= write_ack_meta;
            commit_ack_meta <= commit_ack_toggle_pixel;
            commit_ack_sync <= commit_ack_meta;
            active_enable_meta <= active_enable_pixel;
            active_enable_sync <= active_enable_meta;
        end
    end

    always @(posedge build_clk) begin
        if (build_reset) begin
            write_index_hold <= 8'd0;
            write_cell_hold <= 16'h0020;
            write_request_toggle <= 1'b0;
            commit_enable_hold <= 1'b0;
            commit_request_toggle <= 1'b0;
            commit_ack_seen <= 1'b0;
            generation <= 32'd0;
        end else begin
            if (write_strobe && write_ready) begin
                write_index_hold <= write_index;
                write_cell_hold <= write_cell;
                write_request_toggle <= ~write_request_toggle;
            end
            if (commit_strobe && commit_ready) begin
                commit_enable_hold <= shadow_enable;
                commit_request_toggle <= ~commit_request_toggle;
            end
            if (commit_ack_sync != commit_ack_seen) begin
                commit_ack_seen <= commit_ack_sync;
                generation <= generation + 32'd1;
            end
        end
    end

    always @(posedge pixel_clk or posedge pixel_reset) begin
        if (pixel_reset) begin
            write_request_meta <= 1'b0;
            write_request_sync <= 1'b0;
            write_index_meta <= 8'd0;
            write_index_sync <= 8'd0;
            write_cell_meta <= 16'h0020;
            write_cell_sync <= 16'h0020;
            commit_request_meta <= 1'b0;
            commit_request_sync <= 1'b0;
            commit_enable_meta <= 1'b0;
            commit_enable_sync <= 1'b0;
        end else begin
            write_request_meta <= write_request_toggle;
            write_request_sync <= write_request_meta;
            write_index_meta <= write_index_hold;
            write_index_sync <= write_index_meta;
            write_cell_meta <= write_cell_hold;
            write_cell_sync <= write_cell_meta;
            commit_request_meta <= commit_request_toggle;
            commit_request_sync <= commit_request_meta;
            commit_enable_meta <= commit_enable_hold;
            commit_enable_sync <= commit_enable_meta;
        end
    end

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            write_ack_toggle_pixel <= 1'b0;
            commit_ack_toggle_pixel <= 1'b0;
            active_bank_pixel <= 1'b0;
            active_enable_pixel <= 1'b0;
            commit_waiting_pixel <= 1'b0;
            clone_active_pixel <= 1'b0;
            clone_index_pixel <= 8'd0;
        end else begin
            if (apply_mailbox_write) begin
                write_ack_toggle_pixel <= write_request_sync;
            end

            if (!clone_active_pixel && !commit_waiting_pixel &&
                write_request_sync == write_ack_toggle_pixel &&
                commit_request_sync != commit_ack_toggle_pixel)
                commit_waiting_pixel <= 1'b1;

            if (commit_waiting_pixel && pixel_frame_boundary) begin
                active_bank_pixel <= ~active_bank_pixel;
                active_enable_pixel <= commit_enable_sync;
                commit_waiting_pixel <= 1'b0;
                clone_active_pixel <= 1'b1;
                clone_index_pixel <= 8'd0;
            end

            if (clone_active_pixel) begin
                if (clone_index_pixel == CELLS - 1) begin
                    clone_active_pixel <= 1'b0;
                    commit_ack_toggle_pixel <= commit_request_sync;
                end else begin
                    clone_index_pixel <= clone_index_pixel + 8'd1;
                end
            end
        end
    end

    wire [15:0] active_cell = active_bank_pixel ?
        bank1_read_data : bank0_read_data;

    reg [15:0] render_cell_q;
    reg [2:0] render_glyph_col_q;
    reg [2:0] render_glyph_row_q;
    reg render_glyph_region_q;
    reg render_enable_q;
    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            render_cell_q <= 16'h0020;
            render_glyph_col_q <= 3'd0;
            render_glyph_row_q <= 3'd0;
            render_glyph_region_q <= 1'b0;
            render_enable_q <= 1'b0;
        end else begin
            render_cell_q <= active_cell;
            render_glyph_col_q <= glyph_col;
            render_glyph_row_q <= glyph_row;
            render_glyph_region_q <= glyph_region;
            render_enable_q <= active_enable_pixel;
        end
    end

    wire [7:0] font_row =
        font_rom[{render_cell_q[7:0], render_glyph_row_q}];
    wire glyph_pixel = render_glyph_region_q &&
        font_row[7 - render_glyph_col_q];

    reg glyph_visible_q;
    reg [23:0] foreground_rgb_q;
    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            glyph_visible_q <= 1'b0;
            foreground_rgb_q <= 24'd0;
        end else begin
            glyph_visible_q <= render_enable_q && glyph_pixel;
            case (render_cell_q[9:8])
                2'd0: foreground_rgb_q <= 24'h00e5e5;
                2'd1: foreground_rgb_q <= 24'hff9d00;
                2'd2: foreground_rgb_q <= 24'he8edf2;
                default: foreground_rgb_q <= 24'hff4d5a;
            endcase
        end
    end

    assign output_valid = input_valid;
    always @* begin
        if (glyph_visible_q)
            output_rgb = foreground_rgb_q;
        else
            output_rgb = input_rgb;
    end
endmodule

`default_nettype wire
