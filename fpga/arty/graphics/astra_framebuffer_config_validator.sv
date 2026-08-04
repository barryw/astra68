// Copyright (c) 2026 Astra68 contributors
//
// Bounded framebuffer configuration validator. The 32-by-13-bit surface-span
// multiply is split into two registered 16-by-13-bit products so the validator
// has no cascaded-DSP path at the 200 MHz graphics clock.
`timescale 1ns/1ps
`default_nettype none

module astra_framebuffer_config_validator #(
    parameter integer OUTPUT_WIDTH = 1280,
    parameter integer OUTPUT_HEIGHT = 720
) (
    input  wire        clk,
    input  wire        reset,
    input  wire        start,
    input  wire [1:0]  format,
    input  wire [31:0] framebuffer_base,
    input  wire [31:0] pitch,
    input  wire [12:0] virtual_width,
    input  wire [12:0] virtual_height,
    input  wire signed [31:0] viewport_x,
    input  wire signed [31:0] viewport_y,
    input  wire        wrap_x,
    input  wire        wrap_y,
    input  wire [31:0] arena_base,
    input  wire [31:0] arena_limit,
    output reg         busy,
    output reg         done,
    output reg         config_valid,
    output reg  [31:0] surface_bytes
);
    localparam [1:0] FORMAT_INDEX8 = 2'd0;
    localparam [1:0] FORMAT_RGB565 = 2'd1;
    localparam [1:0] FORMAT_XRGB8888 = 2'd2;

    localparam [2:0] ST_IDLE = 3'd0;
    localparam [2:0] ST_MULTIPLY = 3'd1;
    localparam [2:0] ST_COMBINE_PRODUCT = 3'd2;
    localparam [2:0] ST_SUBTRACT_LAST_ROW = 3'd3;
    localparam [2:0] ST_ADD_BASE = 3'd4;
    localparam [2:0] ST_CHECK = 3'd5;

    reg [2:0] state;
    reg simple_format_q;
    reg simple_dimensions_q;
    reg simple_alignment_q;
    reg simple_pitch_q;
    reg simple_arena_range_q;
    reg simple_base_q;
    reg simple_wrap_x_q;
    reg simple_wrap_y_q;
    reg simple_valid_q;
    reg [32:0] row_end_q;
    reg [15:0] pitch_low_q;
    reg [15:0] pitch_high_q;
    reg [31:0] pitch_q;
    reg [12:0] height_q;
    reg [28:0] product_low_q;
    reg [28:0] product_high_q;
    reg [44:0] surface_product_q;
    reg [44:0] surface_span_q;
    reg [45:0] surface_end_q;
    reg [31:0] arena_limit_q;

    wire [1:0] bytes_shift =
        format == FORMAT_INDEX8 ? 2'd0 :
        format == FORMAT_RGB565 ? 2'd1 : 2'd2;
    wire format_valid = format == FORMAT_INDEX8 ||
                        format == FORMAT_RGB565 ||
                        format == FORMAT_XRGB8888;
    wire [31:0] row_bytes = {19'd0, virtual_width} << bytes_shift;
    wire wrap_x_valid = !wrap_x ||
        (!viewport_x[31] && {19'd0, viewport_x[12:0]} < virtual_width &&
         virtual_width >= OUTPUT_WIDTH);
    wire wrap_y_valid = !wrap_y ||
        (!viewport_y[31] && {19'd0, viewport_y[12:0]} < virtual_height &&
         virtual_height >= OUTPUT_HEIGHT);
    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            simple_format_q <= 1'b0;
            simple_dimensions_q <= 1'b0;
            simple_alignment_q <= 1'b0;
            simple_pitch_q <= 1'b0;
            simple_arena_range_q <= 1'b0;
            simple_base_q <= 1'b0;
            simple_wrap_x_q <= 1'b0;
            simple_wrap_y_q <= 1'b0;
            simple_valid_q <= 1'b0;
            row_end_q <= 33'd0;
            pitch_low_q <= 16'd0;
            pitch_high_q <= 16'd0;
            pitch_q <= 32'd0;
            height_q <= 13'd0;
            product_low_q <= 29'd0;
            product_high_q <= 29'd0;
            surface_product_q <= 45'd0;
            surface_span_q <= 45'd0;
            surface_end_q <= 46'd0;
            arena_limit_q <= 32'd0;
            busy <= 1'b0;
            done <= 1'b0;
            config_valid <= 1'b0;
            surface_bytes <= 32'd0;
        end else begin
            done <= 1'b0;
            case (state)
                ST_IDLE: begin
                    if (start) begin
                        simple_format_q <= format_valid;
                        simple_dimensions_q <=
                            virtual_width != 13'd0 &&
                            virtual_width <= 13'd4096 &&
                            virtual_height != 13'd0 &&
                            virtual_height <= 13'd4096;
                        simple_alignment_q <=
                            framebuffer_base[5:0] == 6'd0 &&
                            pitch[5:0] == 6'd0;
                        simple_pitch_q <= pitch >= row_bytes;
                        simple_arena_range_q <= arena_limit > arena_base;
                        simple_base_q <= framebuffer_base >= arena_base;
                        simple_wrap_x_q <= wrap_x_valid;
                        simple_wrap_y_q <= wrap_y_valid;
                        row_end_q <= {1'b0, framebuffer_base} +
                                     {1'b0, row_bytes};
                        pitch_low_q <= pitch[15:0];
                        pitch_high_q <= pitch[31:16];
                        pitch_q <= pitch;
                        height_q <= virtual_height;
                        arena_limit_q <= arena_limit;
                        busy <= 1'b1;
                        state <= ST_MULTIPLY;
                    end
                end

                ST_MULTIPLY: begin
                    simple_valid_q <=
                        simple_format_q && simple_dimensions_q &&
                        simple_alignment_q && simple_pitch_q &&
                        simple_arena_range_q && simple_base_q &&
                        simple_wrap_x_q && simple_wrap_y_q;
                    product_low_q <= pitch_low_q * height_q;
                    product_high_q <= pitch_high_q * height_q;
                    state <= ST_COMBINE_PRODUCT;
                end

                ST_COMBINE_PRODUCT: begin
                    surface_product_q <= {product_high_q, 16'd0} +
                                         {16'd0, product_low_q};
                    state <= ST_SUBTRACT_LAST_ROW;
                end

                ST_SUBTRACT_LAST_ROW: begin
                    surface_span_q <= surface_product_q -
                                      {13'd0, pitch_q};
                    state <= ST_ADD_BASE;
                end

                ST_ADD_BASE: begin
                    surface_end_q <= {1'b0, surface_span_q} +
                                     {13'd0, row_end_q};
                    state <= ST_CHECK;
                end

                ST_CHECK: begin
                    config_valid <= simple_valid_q &&
                        surface_end_q <= {14'd0, arena_limit_q};
                    surface_bytes <= surface_product_q[31:0];
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end

                default: begin
                    config_valid <= 1'b0;
                    surface_bytes <= 32'd0;
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
