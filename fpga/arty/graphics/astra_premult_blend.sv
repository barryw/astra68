// Copyright (c) 2026 Astra68 contributors
//
// Exact round-to-nearest /255 blend pipelines used by the sprite line builder
// and final pixel compositor. Sprite line storage is premultiplied ARGB so a
// complete stack of translucent sprites can be represented by one line plane.
`timescale 1ns/1ps
`default_nettype none

module astra_blend_premult_pipeline (
    input  wire        clk,
    input  wire        reset,
    input  wire        input_valid,
    input  wire [31:0] destination_premult_argb,
    input  wire [31:0] source_straight_argb,
    input  wire [7:0]  opacity,
    input  wire        apply_source,
    output wire        output_valid,
    output reg  [31:0] output_premult_argb
);
    function automatic [7:0] divide_255_round(input [16:0] numerator);
        reg [17:0] adjusted;
        begin
            adjusted = {1'b0, numerator} + 18'd128;
            divide_255_round = (adjusted + (adjusted >> 8)) >> 8;
        end
    endfunction

    function automatic [7:0] divide_255_finish(input [17:0] adjusted);
        begin
            divide_255_finish = (adjusted + (adjusted >> 8)) >> 8;
        end
    endfunction

    reg [31:0] destination_input_q;
    reg [31:0] source_input_q;
    reg [7:0] opacity_input_q;
    reg apply_input_q;

    (* use_dsp = "yes" *) reg [15:0] alpha_product_q;
    reg [31:0] destination0_q;
    reg [23:0] source0_q;
    reg apply0_q;

    // Keep the effective-alpha divide and channel multiplies in separate
    // cycles. Vivado otherwise retimes this boundary into a two-DSP path.
    (* dont_touch = "yes" *) reg [7:0] alpha_q;
    reg [31:0] destination1_q;
    reg [23:0] source1_q;
    reg apply1_q;

    reg [7:0] blend_alpha_q;
    reg [7:0] blend_inverse_alpha_q;
    reg [31:0] blend_destination_q;
    reg [23:0] blend_source_q;
    reg blend_apply_q;

    (* use_dsp = "yes" *) reg [15:0] source_red_product_q;
    (* use_dsp = "yes" *) reg [15:0] source_green_product_q;
    (* use_dsp = "yes" *) reg [15:0] source_blue_product_q;
    reg [15:0] destination_red_product_q;
    reg [15:0] destination_green_product_q;
    reg [15:0] destination_blue_product_q;
    (* use_dsp = "yes" *) reg [15:0] destination_alpha_product_q;
    reg [15:0] source_alpha_product_q;
    reg [31:0] destination2_q;
    reg [7:0] alpha2_q;
    reg apply2_q;

    reg [16:0] red_numerator_q;
    reg [16:0] green_numerator_q;
    reg [16:0] blue_numerator_q;
    reg [16:0] alpha_numerator_q;
    reg [31:0] destination3_q;
    reg [7:0] alpha3_q;
    reg apply3_q;
    reg [17:0] red_adjusted_q;
    reg [17:0] green_adjusted_q;
    reg [17:0] blue_adjusted_q;
    reg [17:0] alpha_adjusted_q;
    reg [31:0] destination4_q;
    reg [7:0] alpha4_q;
    reg apply4_q;
    reg [7:0] valid_pipeline;

    always @(posedge clk) begin
        if (reset)
            valid_pipeline <= 8'd0;
        else
            valid_pipeline <= {valid_pipeline[6:0], input_valid};
    end

    // Validity flushes the pipeline on reset, so datapath registers do not
    // consume the device-wide reset network.
    always @(posedge clk) begin
        destination_input_q <= destination_premult_argb;
        source_input_q <= source_straight_argb;
        opacity_input_q <= opacity;
        apply_input_q <= apply_source;

        alpha_product_q <= source_input_q[31:24] * opacity_input_q;
        destination0_q <= destination_input_q;
        source0_q <= source_input_q[23:0];
        apply0_q <= apply_input_q;

        alpha_q <= divide_255_round({1'b0, alpha_product_q});
        destination1_q <= destination0_q;
        source1_q <= source0_q;
        apply1_q <= apply0_q;

        blend_alpha_q <= alpha_q;
        blend_inverse_alpha_q <= 8'd255 - alpha_q;
        blend_destination_q <= destination1_q;
        blend_source_q <= source1_q;
        blend_apply_q <= apply1_q;

        source_red_product_q <= blend_source_q[23:16] * blend_alpha_q;
        source_green_product_q <= blend_source_q[15:8] * blend_alpha_q;
        source_blue_product_q <= blend_source_q[7:0] * blend_alpha_q;
        destination_red_product_q <=
            blend_destination_q[23:16] * blend_inverse_alpha_q;
        destination_green_product_q <=
            blend_destination_q[15:8] * blend_inverse_alpha_q;
        destination_blue_product_q <=
            blend_destination_q[7:0] * blend_inverse_alpha_q;
        destination_alpha_product_q <=
            blend_destination_q[31:24] * blend_inverse_alpha_q;
        source_alpha_product_q <= {blend_alpha_q, 8'd0} -
                                  {8'd0, blend_alpha_q};
        destination2_q <= blend_destination_q;
        alpha2_q <= blend_alpha_q;
        apply2_q <= blend_apply_q;

        red_numerator_q <= source_red_product_q +
                           destination_red_product_q;
        green_numerator_q <= source_green_product_q +
                             destination_green_product_q;
        blue_numerator_q <= source_blue_product_q +
                            destination_blue_product_q;
        alpha_numerator_q <= source_alpha_product_q +
                             destination_alpha_product_q;
        destination3_q <= destination2_q;
        alpha3_q <= alpha2_q;
        apply3_q <= apply2_q;

        red_adjusted_q <= {1'b0, red_numerator_q} + 18'd128;
        green_adjusted_q <= {1'b0, green_numerator_q} + 18'd128;
        blue_adjusted_q <= {1'b0, blue_numerator_q} + 18'd128;
        alpha_adjusted_q <= {1'b0, alpha_numerator_q} + 18'd128;
        destination4_q <= destination3_q;
        alpha4_q <= alpha3_q;
        apply4_q <= apply3_q;

        if (!apply4_q || alpha4_q == 8'd0) begin
            output_premult_argb <= destination4_q;
        end else begin
            output_premult_argb[31:24] <=
                divide_255_finish(alpha_adjusted_q);
            output_premult_argb[23:16] <=
                divide_255_finish(red_adjusted_q);
            output_premult_argb[15:8] <=
                divide_255_finish(green_adjusted_q);
            output_premult_argb[7:0] <=
                divide_255_finish(blue_adjusted_q);
        end
    end

    assign output_valid = valid_pipeline[7];
endmodule

module astra_blend_premult_opaque_pipeline (
    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire        input_valid,
    input  wire [23:0] destination_rgb,
    input  wire [31:0] source_premult_argb,
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

    reg [31:0] source0_q;
    reg [23:0] destination0_q;
    reg apply0_q;
    reg [7:0] inverse_alpha_q;
    reg [31:0] source1_q;
    reg [23:0] destination1_q;
    reg apply1_q;
    reg [16:0] source_red_scaled_q;
    reg [16:0] source_green_scaled_q;
    reg [16:0] source_blue_scaled_q;
    reg [15:0] destination_red_product_q;
    reg [15:0] destination_green_product_q;
    reg [15:0] destination_blue_product_q;
    reg [23:0] destination2_q;
    reg apply2_q;
    reg [16:0] red_numerator_q;
    reg [16:0] green_numerator_q;
    reg [16:0] blue_numerator_q;
    reg [23:0] destination3_q;
    reg apply3_q;
    reg [4:0] valid_pipeline;

    always @(posedge pixel_clk) begin
        if (pixel_reset)
            valid_pipeline <= 5'd0;
        else
            valid_pipeline <= {valid_pipeline[3:0], input_valid};
    end

    always @(posedge pixel_clk) begin
        source0_q <= source_premult_argb;
        destination0_q <= destination_rgb;
        apply0_q <= apply_source;

        inverse_alpha_q <= 8'd255 - source0_q[31:24];
        source1_q <= source0_q;
        destination1_q <= destination0_q;
        apply1_q <= apply0_q;

        source_red_scaled_q <= source1_q[23:16] * 8'd255;
        source_green_scaled_q <= source1_q[15:8] * 8'd255;
        source_blue_scaled_q <= source1_q[7:0] * 8'd255;
        destination_red_product_q <=
            destination1_q[23:16] * inverse_alpha_q;
        destination_green_product_q <=
            destination1_q[15:8] * inverse_alpha_q;
        destination_blue_product_q <=
            destination1_q[7:0] * inverse_alpha_q;
        destination2_q <= destination1_q;
        apply2_q <= apply1_q;

        red_numerator_q <= source_red_scaled_q +
                           destination_red_product_q;
        green_numerator_q <= source_green_scaled_q +
                             destination_green_product_q;
        blue_numerator_q <= source_blue_scaled_q +
                            destination_blue_product_q;
        destination3_q <= destination2_q;
        apply3_q <= apply2_q;

        if (!apply3_q) begin
            output_rgb <= destination3_q;
        end else begin
            output_rgb[23:16] <= divide_255_round(red_numerator_q);
            output_rgb[15:8] <= divide_255_round(green_numerator_q);
            output_rgb[7:0] <= divide_255_round(blue_numerator_q);
        end
    end

    assign output_valid = valid_pipeline[4];
endmodule

`default_nettype wire
