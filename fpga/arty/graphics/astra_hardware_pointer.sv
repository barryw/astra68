// Copyright (c) 2026 Astra68 contributors
//
// Native-output ARGB pointer plane. The pointer is composed after display
// scaling, so its pixels and position are never scaled with the scene.
`timescale 1ns/1ps
`default_nettype none

module astra_hardware_pointer #(
    parameter integer OUTPUT_WIDTH = 1920,
    parameter integer OUTPUT_HEIGHT = 1080
) (
    input  wire        build_clk,
    input  wire        build_reset,
    input  wire        shadow_enable,
    input  wire [10:0] shadow_x,
    input  wire [10:0] shadow_y,
    input  wire [4:0]  shadow_hot_x,
    input  wire [4:0]  shadow_hot_y,
    input  wire        image_write_enable,
    input  wire [9:0]  image_write_index,
    input  wire [31:0] image_write_argb,
    input  wire        commit_strobe,
    input  wire        commit_swap_image,
    output wire        write_ready,
    output wire        commit_ready,
    output reg  [31:0] generation,

    input  wire        pixel_clk,
    input  wire        pixel_reset,
    input  wire        frame_boundary,
    input  wire [11:0] pixel_x,
    input  wire [10:0] pixel_y,
    input  wire        input_valid,
    input  wire [23:0] input_rgb,
    output wire        output_valid,
    output wire [23:0] output_rgb
);
    localparam integer IMAGE_PIXELS = 32 * 32;

    (* ramstyle = "M20K, no_rw_check" *)
    reg [31:0] image_bank0 [0:IMAGE_PIXELS-1];
    (* ramstyle = "M20K, no_rw_check" *)
    reg [31:0] image_bank1 [0:IMAGE_PIXELS-1];

    reg edit_bank_build;
    reg request_toggle_build;
    reg hold_enable;
    reg [10:0] hold_x;
    reg [10:0] hold_y;
    reg [4:0] hold_hot_x;
    reg [4:0] hold_hot_y;
    reg hold_swap_image;

    reg acknowledge_toggle_pixel;
    (* ASYNC_REG = "TRUE" *) reg acknowledge_meta_build;
    (* ASYNC_REG = "TRUE" *) reg acknowledge_sync_build;
    reg acknowledge_seen_build;

    assign write_ready = acknowledge_sync_build == request_toggle_build;
    assign commit_ready = write_ready;

    always @(posedge build_clk or posedge build_reset) begin
        if (build_reset) begin
            edit_bank_build <= 1'b1;
            request_toggle_build <= 1'b0;
            hold_enable <= 1'b0;
            hold_x <= 11'd0;
            hold_y <= 11'd0;
            hold_hot_x <= 5'd0;
            hold_hot_y <= 5'd0;
            hold_swap_image <= 1'b0;
            acknowledge_meta_build <= 1'b0;
            acknowledge_sync_build <= 1'b0;
            acknowledge_seen_build <= 1'b0;
            generation <= 32'd0;
        end else begin
            acknowledge_meta_build <= acknowledge_toggle_pixel;
            acknowledge_sync_build <= acknowledge_meta_build;
            if (acknowledge_sync_build != acknowledge_seen_build) begin
                acknowledge_seen_build <= acknowledge_sync_build;
                generation <= generation + 32'd1;
            end
            if (image_write_enable && write_ready) begin
                if (edit_bank_build)
                    image_bank1[image_write_index] <= image_write_argb;
                else
                    image_bank0[image_write_index] <= image_write_argb;
            end
            if (commit_strobe && commit_ready) begin
                hold_enable <= shadow_enable;
                hold_x <= shadow_x;
                hold_y <= shadow_y;
                hold_hot_x <= shadow_hot_x;
                hold_hot_y <= shadow_hot_y;
                hold_swap_image <= commit_swap_image;
                request_toggle_build <= ~request_toggle_build;
                if (commit_swap_image)
                    edit_bank_build <= ~edit_bank_build;
            end
        end
    end

    (* ASYNC_REG = "TRUE" *) reg request_meta_pixel;
    (* ASYNC_REG = "TRUE" *) reg request_sync_pixel;
    (* ASYNC_REG = "TRUE" *) reg enable_meta_pixel;
    (* ASYNC_REG = "TRUE" *) reg enable_sync_pixel;
    (* ASYNC_REG = "TRUE" *) reg [10:0] x_meta_pixel;
    (* ASYNC_REG = "TRUE" *) reg [10:0] x_sync_pixel;
    (* ASYNC_REG = "TRUE" *) reg [10:0] y_meta_pixel;
    (* ASYNC_REG = "TRUE" *) reg [10:0] y_sync_pixel;
    (* ASYNC_REG = "TRUE" *) reg [4:0] hot_x_meta_pixel;
    (* ASYNC_REG = "TRUE" *) reg [4:0] hot_x_sync_pixel;
    (* ASYNC_REG = "TRUE" *) reg [4:0] hot_y_meta_pixel;
    (* ASYNC_REG = "TRUE" *) reg [4:0] hot_y_sync_pixel;
    (* ASYNC_REG = "TRUE" *) reg swap_meta_pixel;
    (* ASYNC_REG = "TRUE" *) reg swap_sync_pixel;
    reg active_bank_pixel;
    reg active_enable_pixel;
    reg [10:0] active_x_pixel;
    reg [10:0] active_y_pixel;
    reg [4:0] active_hot_x_pixel;
    reg [4:0] active_hot_y_pixel;

    always @(posedge pixel_clk or posedge pixel_reset) begin
        if (pixel_reset) begin
            request_meta_pixel <= 1'b0;
            request_sync_pixel <= 1'b0;
            enable_meta_pixel <= 1'b0;
            enable_sync_pixel <= 1'b0;
            x_meta_pixel <= 11'd0;
            x_sync_pixel <= 11'd0;
            y_meta_pixel <= 11'd0;
            y_sync_pixel <= 11'd0;
            hot_x_meta_pixel <= 5'd0;
            hot_x_sync_pixel <= 5'd0;
            hot_y_meta_pixel <= 5'd0;
            hot_y_sync_pixel <= 5'd0;
            swap_meta_pixel <= 1'b0;
            swap_sync_pixel <= 1'b0;
            acknowledge_toggle_pixel <= 1'b0;
            active_bank_pixel <= 1'b0;
            active_enable_pixel <= 1'b0;
            active_x_pixel <= 11'd0;
            active_y_pixel <= 11'd0;
            active_hot_x_pixel <= 5'd0;
            active_hot_y_pixel <= 5'd0;
        end else begin
            request_meta_pixel <= request_toggle_build;
            request_sync_pixel <= request_meta_pixel;
            enable_meta_pixel <= hold_enable;
            enable_sync_pixel <= enable_meta_pixel;
            x_meta_pixel <= hold_x;
            x_sync_pixel <= x_meta_pixel;
            y_meta_pixel <= hold_y;
            y_sync_pixel <= y_meta_pixel;
            hot_x_meta_pixel <= hold_hot_x;
            hot_x_sync_pixel <= hot_x_meta_pixel;
            hot_y_meta_pixel <= hold_hot_y;
            hot_y_sync_pixel <= hot_y_meta_pixel;
            swap_meta_pixel <= hold_swap_image;
            swap_sync_pixel <= swap_meta_pixel;
            if (frame_boundary &&
                request_sync_pixel != acknowledge_toggle_pixel) begin
                active_enable_pixel <= enable_sync_pixel;
                active_x_pixel <= x_sync_pixel;
                active_y_pixel <= y_sync_pixel;
                active_hot_x_pixel <= hot_x_sync_pixel;
                active_hot_y_pixel <= hot_y_sync_pixel;
                if (swap_sync_pixel)
                    active_bank_pixel <= ~active_bank_pixel;
                acknowledge_toggle_pixel <= request_sync_pixel;
            end
        end
    end

    wire [12:0] adjusted_x = {1'b0, pixel_x} + active_hot_x_pixel;
    wire [11:0] adjusted_y = {1'b0, pixel_y} + active_hot_y_pixel;
    wire [12:0] pointer_right = {2'b00, active_x_pixel} + 13'd32;
    wire [11:0] pointer_bottom = {1'b0, active_y_pixel} + 12'd32;
    wire pointer_pixel = active_enable_pixel &&
        adjusted_x >= {2'b00, active_x_pixel} && adjusted_x < pointer_right &&
        adjusted_y >= {1'b0, active_y_pixel} && adjusted_y < pointer_bottom;
    wire [12:0] pointer_column_wide =
        adjusted_x - {2'b00, active_x_pixel};
    wire [11:0] pointer_row_wide =
        adjusted_y - {1'b0, active_y_pixel};
    wire [4:0] pointer_column = pointer_column_wide[4:0];
    wire [4:0] pointer_row = pointer_row_wide[4:0];
    wire [9:0] pointer_address = {pointer_row, pointer_column};
    wire physical_active = pixel_x < OUTPUT_WIDTH && pixel_y < OUTPUT_HEIGHT;

    reg [31:0] bank0_pixel_q;
    reg [31:0] bank1_pixel_q;
    reg [23:0] background_rgb_q;
    reg pointer_apply_q;
    reg physical_active_q;
    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            bank0_pixel_q <= 32'd0;
            bank1_pixel_q <= 32'd0;
            background_rgb_q <= 24'd0;
            pointer_apply_q <= 1'b0;
            physical_active_q <= 1'b0;
        end else begin
            bank0_pixel_q <= image_bank0[pointer_address];
            bank1_pixel_q <= image_bank1[pointer_address];
            background_rgb_q <= input_valid ? input_rgb : 24'd0;
            pointer_apply_q <= pointer_pixel;
            physical_active_q <= physical_active;
        end
    end

    astra_blend_opaque_pipeline pointer_blend_i (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_valid(physical_active_q),
        .destination_rgb(background_rgb_q),
        .source_argb(active_bank_pixel ? bank1_pixel_q : bank0_pixel_q),
        .opacity(8'd255),
        .apply_source(pointer_apply_q),
        .output_valid(output_valid),
        .output_rgb(output_rgb)
    );

`ifndef SYNTHESIS
    initial begin
        if (OUTPUT_WIDTH < 1 || OUTPUT_WIDTH > 2048 ||
            OUTPUT_HEIGHT < 1 || OUTPUT_HEIGHT > 2048)
            $fatal(1, "invalid hardware pointer output geometry");
    end
`endif
endmodule

`default_nettype wire
