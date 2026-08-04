// Copyright (c) 2026 Astra68 contributors
//
// Validates one v1 surface descriptor before an engine can issue pixel DMA.
// Products are split across cycles to keep descriptor validation off the
// renderer's 200 MHz critical path.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_surface_validator (
    input  wire        clk,
    input  wire        reset,
    input  wire        start,
    input  wire [31:0] expected_generation,
    input  wire [1:0]  required_access,
    input  wire        palette_required,
    input  wire [31:0] arena_bytes,
    input  wire [31:0] version_size,
    input  wire [31:0] generation,
    input  wire [31:0] data_offset,
    input  wire [31:0] data_bytes,
    input  wire [31:0] pitch,
    input  wire [31:0] width_height,
    input  wire [31:0] format_flags,
    input  wire [31:0] palette_offset,
    output reg         busy,
    output reg         done,
    output reg         descriptor_valid,
    output reg  [7:0]  format,
    output reg  [2:0]  bytes_per_pixel,
    output reg  [15:0] width,
    output reg  [15:0] height,
    output reg  [31:0] validated_data_offset,
    output reg  [31:0] validated_data_bytes,
    output reg  [31:0] validated_pitch,
    output reg  [31:0] validated_palette_offset
);
    localparam [2:0] ST_IDLE = 3'd0;
    localparam [2:0] ST_MULTIPLY = 3'd1;
    localparam [2:0] ST_COMBINE = 3'd2;
    localparam [2:0] ST_SPAN = 3'd3;
    localparam [2:0] ST_RANGE = 3'd4;
    localparam [2:0] ST_FINISH = 3'd5;
    localparam [2:0] ST_LAYOUT = 3'd6;

    reg [2:0] state;
    reg header_valid_q;
    reg geometry_valid_q;
    reg access_valid_q;
    reg storage_valid_q;
    reg simple_valid_q;
    reg [15:0] pitch_low_q;
    reg [15:0] pitch_high_q;
    reg [15:0] input_height_q;
    reg [15:0] rows_before_last_q;
    reg [31:0] row_bytes_q;
    reg [31:0] arena_bytes_q;
    reg [31:0] data_offset_q;
    reg [31:0] data_bytes_q;
    reg [32:0] palette_end_q;
    reg palette_required_q;
    reg [31:0] product_low_q;
    reg [31:0] product_high_q;
    reg [47:0] row_product_q;
    reg [48:0] required_span_q;
    reg [32:0] allocation_end_q;

    wire [7:0] input_format = format_flags[31:24];
    wire [7:0] input_flags = format_flags[23:16];
    wire [15:0] input_width = width_height[31:16];
    wire [15:0] input_height = width_height[15:0];
    wire input_format_valid =
        input_format <= `ASTRA_RENDER_FORMAT_INDEX4;
    wire [2:0] input_bpp =
        input_format == `ASTRA_RENDER_FORMAT_INDEX8 ||
        input_format == `ASTRA_RENDER_FORMAT_A8 ? 3'd1 :
        input_format == `ASTRA_RENDER_FORMAT_RGB565 ? 3'd2 :
        input_format >= `ASTRA_RENDER_FORMAT_MASK1 ? 3'd0 : 3'd4;
    wire [31:0] input_row_bytes =
        input_format == `ASTRA_RENDER_FORMAT_MASK1 ?
            ({16'd0, input_width} + 32'd7) >> 3 :
        input_format == `ASTRA_RENDER_FORMAT_A4 ||
        input_format == `ASTRA_RENDER_FORMAT_INDEX4 ?
            ({16'd0, input_width} + 32'd1) >> 1 :
        input_format == `ASTRA_RENDER_FORMAT_INDEX8 ||
        input_format == `ASTRA_RENDER_FORMAT_A8 ?
            {16'd0, input_width} :
        input_format == `ASTRA_RENDER_FORMAT_RGB565 ?
            ({16'd0, input_width} << 1) :
            ({16'd0, input_width} << 2);
    wire input_alignment_valid =
        input_format == `ASTRA_RENDER_FORMAT_INDEX8 ||
        input_format >= `ASTRA_RENDER_FORMAT_MASK1 ? 1'b1 :
        input_format == `ASTRA_RENDER_FORMAT_RGB565 ?
            !(data_offset[0] || pitch[0]) :
            !(|data_offset[1:0] || |pitch[1:0]);
    wire input_is_indexed_palette =
        input_format == `ASTRA_RENDER_FORMAT_INDEX8 ||
        input_format == `ASTRA_RENDER_FORMAT_INDEX4;
    // Glyph strikes use one descriptor contract for mask, coverage, and
    // indexed sources. In that mode indexed formats require an aligned
    // palette while non-indexed formats require no palette.
    wire palette_contract_valid = palette_required ?
        (input_is_indexed_palette ?
            palette_offset != 32'd0 && palette_offset[5:0] == 6'd0 :
            palette_offset == 32'd0) :
        palette_offset == 32'd0;

    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            header_valid_q <= 1'b0;
            geometry_valid_q <= 1'b0;
            access_valid_q <= 1'b0;
            storage_valid_q <= 1'b0;
            simple_valid_q <= 1'b0;
            pitch_low_q <= 16'd0;
            pitch_high_q <= 16'd0;
            input_height_q <= 16'd0;
            rows_before_last_q <= 16'd0;
            row_bytes_q <= 32'd0;
            arena_bytes_q <= 32'd0;
            data_offset_q <= 32'd0;
            data_bytes_q <= 32'd0;
            palette_end_q <= 33'd0;
            palette_required_q <= 1'b0;
            product_low_q <= 32'd0;
            product_high_q <= 32'd0;
            row_product_q <= 48'd0;
            required_span_q <= 49'd0;
            allocation_end_q <= 33'd0;
            busy <= 1'b0;
            done <= 1'b0;
            descriptor_valid <= 1'b0;
            format <= 8'd0;
            bytes_per_pixel <= 3'd0;
            width <= 16'd0;
            height <= 16'd0;
            validated_data_offset <= 32'd0;
            validated_data_bytes <= 32'd0;
            validated_pitch <= 32'd0;
            validated_palette_offset <= 32'd0;
        end else begin
            done <= 1'b0;
            case (state)
                ST_IDLE: begin
                    if (start) begin
                        header_valid_q <=
                            version_size[31:16] ==
                                `ASTRA_RENDER_ABI_VERSION &&
                            version_size[15:0] ==
                                `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES &&
                            generation == expected_generation &&
                            expected_generation != 32'd0;
                        geometry_valid_q <=
                            input_format_valid &&
                            input_width != 16'd0 &&
                            input_height != 16'd0 &&
                            input_width <=
                                `ASTRA_RENDER_MAX_SURFACE_DIMENSION &&
                            input_height <=
                                `ASTRA_RENDER_MAX_SURFACE_DIMENSION;
                        access_valid_q <=
                            input_flags[7:2] == 6'd0 &&
                            (input_flags[1:0] & required_access) ==
                                required_access &&
                            required_access != 2'd0;
                        storage_valid_q <=
                            palette_contract_valid &&
                            data_bytes != 32'd0 &&
                            input_alignment_valid;
                        pitch_low_q <= pitch[15:0];
                        pitch_high_q <= pitch[31:16];
                        input_height_q <= input_height;
                        row_bytes_q <= input_row_bytes;
                        arena_bytes_q <= arena_bytes;
                        data_offset_q <= data_offset;
                        data_bytes_q <= data_bytes;
                        palette_end_q <= {1'b0, palette_offset} +
                            (input_format == `ASTRA_RENDER_FORMAT_INDEX4 ?
                                33'd64 : 33'd1024);
                        palette_required_q <= palette_required &&
                            input_is_indexed_palette;
                        format <= input_format;
                        bytes_per_pixel <= input_bpp;
                        width <= input_width;
                        height <= input_height;
                        validated_data_offset <= data_offset;
                        validated_data_bytes <= data_bytes;
                        validated_pitch <= pitch;
                        validated_palette_offset <= palette_offset;
                        descriptor_valid <= 1'b0;
                        busy <= 1'b1;
                        state <= ST_LAYOUT;
                    end
                end

                ST_LAYOUT: begin
                    rows_before_last_q <= input_height_q - 16'd1;
                    simple_valid_q <= header_valid_q &&
                        geometry_valid_q &&
                        access_valid_q &&
                        storage_valid_q &&
                        (!palette_required_q ||
                         palette_end_q <= {1'b0, arena_bytes_q}) &&
                        ({pitch_high_q, pitch_low_q} >= row_bytes_q);
                    state <= ST_MULTIPLY;
                end

                ST_MULTIPLY: begin
                    product_low_q <= pitch_low_q * rows_before_last_q;
                    product_high_q <= pitch_high_q * rows_before_last_q;
                    state <= ST_COMBINE;
                end

                ST_COMBINE: begin
                    row_product_q <= {product_high_q, 16'd0} +
                                     {16'd0, product_low_q};
                    state <= ST_SPAN;
                end

                ST_SPAN: begin
                    required_span_q <= {1'b0, row_product_q} +
                                       {17'd0, row_bytes_q};
                    allocation_end_q <= {1'b0, data_offset_q} +
                                        {1'b0, data_bytes_q};
                    state <= ST_RANGE;
                end

                ST_RANGE: begin
                    descriptor_valid <= simple_valid_q &&
                        required_span_q <= {17'd0, data_bytes_q} &&
                        allocation_end_q <= {1'b0, arena_bytes_q};
                    state <= ST_FINISH;
                end

                ST_FINISH: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end

                default: begin
                    descriptor_valid <= 1'b0;
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
