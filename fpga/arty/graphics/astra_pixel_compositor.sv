// Copyright (c) 2026 Astra68 contributors
//
// Fixed-order Vega pixel compositor for the framebuffer and
// precomposed front/behind sprite planes. Palette values are straight-alpha
// ARGB8888; sprite planes are premultiplied ARGB8888. Every blend starts from
// an opaque backdrop, so the resulting HDMI pixel is always opaque RGB888.
`timescale 1ns/1ps
`default_nettype none

// Five registered stages keep each blend below one 74.25 MHz pixel period:
// alpha product, effective-alpha reduction, channel products, channel sums,
// and final divide-by-255 reduction. The stream remains one pixel per clock.
module astra_blend_opaque_pipeline (
    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire        input_valid,
    input  wire [23:0] destination_rgb,
    input  wire [31:0] source_argb,
    input  wire [7:0]  opacity,
    input  wire        apply_source,
    output wire        output_valid,
    output reg  [23:0] output_rgb
);
    function automatic [7:0] divide_255_round(input [16:0] numerator);
        reg [17:0] adjusted;
        begin
            adjusted = {1'b0, numerator} + 18'd128;
            divide_255_round = (adjusted + (adjusted >> 8)) >> 8;
        end
    endfunction

    reg [15:0] alpha_product_q;
    reg [23:0] destination0_q;
    reg [23:0] source0_q;
    reg apply0_q;

    reg [7:0] alpha_q;
    reg [23:0] destination1_q;
    reg [23:0] source1_q;
    reg apply1_q;

    reg [15:0] source_red_product_q;
    reg [15:0] source_green_product_q;
    reg [15:0] source_blue_product_q;
    reg [15:0] destination_red_product_q;
    reg [15:0] destination_green_product_q;
    reg [15:0] destination_blue_product_q;
    reg [23:0] destination2_q;
    reg [7:0] alpha2_q;
    reg apply2_q;

    reg [16:0] red_numerator_q;
    reg [16:0] green_numerator_q;
    reg [16:0] blue_numerator_q;
    reg [23:0] destination3_q;
    reg [7:0] alpha3_q;
    reg apply3_q;

    reg [4:0] valid_pipeline;

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            alpha_product_q <= 16'd0;
            destination0_q <= 24'd0;
            source0_q <= 24'd0;
            apply0_q <= 1'b0;
            alpha_q <= 8'd0;
            destination1_q <= 24'd0;
            source1_q <= 24'd0;
            apply1_q <= 1'b0;
            source_red_product_q <= 16'd0;
            source_green_product_q <= 16'd0;
            source_blue_product_q <= 16'd0;
            destination_red_product_q <= 16'd0;
            destination_green_product_q <= 16'd0;
            destination_blue_product_q <= 16'd0;
            destination2_q <= 24'd0;
            alpha2_q <= 8'd0;
            apply2_q <= 1'b0;
            red_numerator_q <= 17'd0;
            green_numerator_q <= 17'd0;
            blue_numerator_q <= 17'd0;
            destination3_q <= 24'd0;
            alpha3_q <= 8'd0;
            apply3_q <= 1'b0;
            output_rgb <= 24'd0;
            valid_pipeline <= 5'd0;
        end else begin
            alpha_product_q <= source_argb[31:24] * opacity;
            destination0_q <= destination_rgb;
            source0_q <= source_argb[23:0];
            apply0_q <= apply_source;

            alpha_q <= divide_255_round({1'b0, alpha_product_q});
            destination1_q <= destination0_q;
            source1_q <= source0_q;
            apply1_q <= apply0_q;

            source_red_product_q <= source1_q[23:16] * alpha_q;
            source_green_product_q <= source1_q[15:8] * alpha_q;
            source_blue_product_q <= source1_q[7:0] * alpha_q;
            destination_red_product_q <=
                destination1_q[23:16] * (8'd255 - alpha_q);
            destination_green_product_q <=
                destination1_q[15:8] * (8'd255 - alpha_q);
            destination_blue_product_q <=
                destination1_q[7:0] * (8'd255 - alpha_q);
            destination2_q <= destination1_q;
            alpha2_q <= alpha_q;
            apply2_q <= apply1_q;

            red_numerator_q <= source_red_product_q +
                               destination_red_product_q;
            green_numerator_q <= source_green_product_q +
                                 destination_green_product_q;
            blue_numerator_q <= source_blue_product_q +
                                destination_blue_product_q;
            destination3_q <= destination2_q;
            alpha3_q <= alpha2_q;
            apply3_q <= apply2_q;

            if (!apply3_q || alpha3_q == 8'd0) begin
                output_rgb <= destination3_q;
            end else begin
                output_rgb[23:16] <= divide_255_round(red_numerator_q);
                output_rgb[15:8] <= divide_255_round(green_numerator_q);
                output_rgb[7:0] <= divide_255_round(blue_numerator_q);
            end

            valid_pipeline <= {valid_pipeline[3:0], input_valid};
        end
    end

    assign output_valid = valid_pipeline[4];
endmodule

module astra_compositor_layer_delay #(
    parameter integer DELAY = 5
) (
    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire [31:0] input_argb,
    input  wire [7:0]  input_opacity,
    input  wire        input_apply,
    output wire [31:0] output_argb,
    output wire [7:0]  output_opacity,
    output wire        output_apply
);
    reg [31:0] argb_pipeline [0:DELAY-1];
    reg [7:0] opacity_pipeline [0:DELAY-1];
    reg apply_pipeline [0:DELAY-1];
    integer index;

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            for (index = 0; index < DELAY; index = index + 1) begin
                argb_pipeline[index] <= 32'd0;
                opacity_pipeline[index] <= 8'd0;
                apply_pipeline[index] <= 1'b0;
            end
        end else begin
            argb_pipeline[0] <= input_argb;
            opacity_pipeline[0] <= input_opacity;
            apply_pipeline[0] <= input_apply;
            for (index = 1; index < DELAY; index = index + 1) begin
                argb_pipeline[index] <= argb_pipeline[index - 1];
                opacity_pipeline[index] <= opacity_pipeline[index - 1];
                apply_pipeline[index] <= apply_pipeline[index - 1];
            end
        end
    end

    assign output_argb = argb_pipeline[DELAY - 1];
    assign output_opacity = opacity_pipeline[DELAY - 1];
    assign output_apply = apply_pipeline[DELAY - 1];
endmodule

module astra_pixel_compositor (
    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire        input_valid,
    input  wire [23:0] backdrop_rgb,

    input  wire        sprite_enable,
    input  wire [31:0] sprite_behind_premult_argb,
    input  wire [31:0] sprite_front_premult_argb,

    input  wire        framebuffer_enable,
    input  wire [1:0]  framebuffer_format,
    input  wire        framebuffer_pixel_valid,
    input  wire [31:0] framebuffer_pixel_value,
    input  wire        framebuffer_key_enable,
    input  wire [31:0] framebuffer_key,
    output wire [7:0]  framebuffer_palette_index,
    input  wire [31:0] framebuffer_palette_argb,

    output wire        output_valid,
    output wire [23:0] output_rgb
);
    localparam [1:0] FORMAT_INDEX8 = 2'd0;
    localparam [1:0] FORMAT_RGB565 = 2'd1;
    localparam [1:0] FORMAT_XRGB8888 = 2'd2;
    localparam integer BLEND_LATENCY = 5;

    assign framebuffer_palette_index = framebuffer_pixel_value[7:0];

    // One clock aligns direct-format pixels and sprite controls with the
    // synchronous framebuffer-palette read.
    reg input_valid_q;
    reg [23:0] backdrop_q;
    reg sprite_enable_q;
    reg [31:0] sprite_behind_q;
    reg [31:0] sprite_front_q;
    reg framebuffer_enable_q;
    reg [1:0] framebuffer_format_q;
    reg framebuffer_pixel_valid_q;
    reg [31:0] framebuffer_pixel_value_q;
    reg framebuffer_key_enable_q;
    reg [31:0] framebuffer_key_q;

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            input_valid_q <= 1'b0;
            backdrop_q <= 24'd0;
            sprite_enable_q <= 1'b0;
            sprite_behind_q <= 32'd0;
            sprite_front_q <= 32'd0;
            framebuffer_enable_q <= 1'b0;
            framebuffer_format_q <= FORMAT_INDEX8;
            framebuffer_pixel_valid_q <= 1'b0;
            framebuffer_pixel_value_q <= 32'd0;
            framebuffer_key_enable_q <= 1'b0;
            framebuffer_key_q <= 32'd0;
        end else begin
            input_valid_q <= input_valid;
            backdrop_q <= backdrop_rgb;
            sprite_enable_q <= sprite_enable;
            sprite_behind_q <= sprite_behind_premult_argb;
            sprite_front_q <= sprite_front_premult_argb;
            framebuffer_enable_q <= framebuffer_enable;
            framebuffer_format_q <= framebuffer_format;
            framebuffer_pixel_valid_q <= framebuffer_pixel_valid;
            framebuffer_pixel_value_q <= framebuffer_pixel_value;
            framebuffer_key_enable_q <= framebuffer_key_enable;
            framebuffer_key_q <= framebuffer_key;
        end
    end

    wire [4:0] rgb565_red = framebuffer_pixel_value_q[15:11];
    wire [5:0] rgb565_green = framebuffer_pixel_value_q[10:5];
    wire [4:0] rgb565_blue = framebuffer_pixel_value_q[4:0];
    wire [31:0] rgb565_argb = {
        8'hff,
        rgb565_red, rgb565_red[4:2],
        rgb565_green, rgb565_green[5:4],
        rgb565_blue, rgb565_blue[4:2]
    };
    wire [31:0] framebuffer_argb =
        framebuffer_format_q == FORMAT_INDEX8 ? framebuffer_palette_argb :
        framebuffer_format_q == FORMAT_RGB565 ? rgb565_argb :
        framebuffer_pixel_value_q;
    wire framebuffer_apply = input_valid_q && framebuffer_enable_q &&
        framebuffer_pixel_valid_q &&
        (!framebuffer_key_enable_q ||
         framebuffer_pixel_value_q != framebuffer_key_q) &&
        (framebuffer_format_q == FORMAT_INDEX8 ||
         framebuffer_format_q == FORMAT_RGB565 ||
         framebuffer_format_q == FORMAT_XRGB8888);
    wire sprite_behind_apply = input_valid_q && sprite_enable_q &&
                               sprite_behind_q[31:24] != 8'd0;
    wire sprite_front_apply = input_valid_q && sprite_enable_q &&
                              sprite_front_q[31:24] != 8'd0;

    // Bottom to top: behind sprites, framebuffer, front sprites.
    wire behind_valid;
    wire [23:0] behind_rgb;
    astra_blend_premult_opaque_pipeline behind_i (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_valid(input_valid_q),
        .destination_rgb(backdrop_q),
        .source_premult_argb(sprite_behind_q),
        .apply_source(sprite_behind_apply),
        .output_valid(behind_valid),
        .output_rgb(behind_rgb)
    );

    wire [31:0] framebuffer_delayed_argb;
    wire [7:0] framebuffer_delayed_opacity;
    wire framebuffer_delayed_apply;
    astra_compositor_layer_delay #(
        .DELAY(BLEND_LATENCY)
    ) framebuffer_delay_i (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_argb(framebuffer_argb),
        .input_opacity(8'd255),
        .input_apply(framebuffer_apply),
        .output_argb(framebuffer_delayed_argb),
        .output_opacity(framebuffer_delayed_opacity),
        .output_apply(framebuffer_delayed_apply)
    );
    wire framebuffer_valid;
    wire [23:0] framebuffer_rgb;
    astra_blend_opaque_pipeline framebuffer_i (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_valid(behind_valid),
        .destination_rgb(behind_rgb),
        .source_argb(framebuffer_delayed_argb),
        .opacity(framebuffer_delayed_opacity),
        .apply_source(framebuffer_delayed_apply),
        .output_valid(framebuffer_valid),
        .output_rgb(framebuffer_rgb)
    );

    wire [31:0] front_delayed_argb;
    wire front_delayed_apply;
    astra_compositor_layer_delay #(
        .DELAY(BLEND_LATENCY * 2)
    ) front_delay_i (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_argb(sprite_front_q),
        .input_opacity(8'd255),
        .input_apply(sprite_front_apply),
        .output_argb(front_delayed_argb),
        .output_opacity(),
        .output_apply(front_delayed_apply)
    );
    astra_blend_premult_opaque_pipeline front_i (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_valid(framebuffer_valid),
        .destination_rgb(framebuffer_rgb),
        .source_premult_argb(front_delayed_argb),
        .apply_source(front_delayed_apply),
        .output_valid(output_valid),
        .output_rgb(output_rgb)
    );
endmodule

`default_nettype wire
