// Copyright (c) 2026 Astra68 contributors
//
// Whitelist and timing-class boundary for copper-visible visual state. The
// committed scene remains the baseline; every frame restores it before copper
// mutations are accepted.
`timescale 1ns/1ps
`default_nettype none

module astra_copper_registers (
    input  wire        clk,
    input  wire        reset,
    input  wire        baseline_restore,

    input  wire [23:0] baseline_backdrop_rgb,
    input  wire signed [31:0] baseline_framebuffer_viewport_x,
    input  wire signed [31:0] baseline_framebuffer_viewport_y,
    input  wire        baseline_framebuffer_wrap_x,
    input  wire        baseline_framebuffer_wrap_y,
    input  wire        baseline_framebuffer_key_enable,
    input  wire [31:0] baseline_framebuffer_key,
    input  wire        baseline_tile0_enable,
    input  wire        baseline_tile0_above,
    input  wire [7:0]  baseline_tile0_opacity,
    input  wire        baseline_tile0_wrap_x,
    input  wire        baseline_tile0_wrap_y,
    input  wire        baseline_tile0_transparent_enable,
    input  wire [7:0]  baseline_tile0_transparent_index,
    input  wire signed [31:0] baseline_tile0_scroll_x,
    input  wire signed [31:0] baseline_tile0_scroll_y,
    input  wire        baseline_tile1_enable,
    input  wire        baseline_tile1_above,
    input  wire [7:0]  baseline_tile1_opacity,
    input  wire        baseline_tile1_wrap_x,
    input  wire        baseline_tile1_wrap_y,
    input  wire        baseline_tile1_transparent_enable,
    input  wire [7:0]  baseline_tile1_transparent_index,
    input  wire signed [31:0] baseline_tile1_scroll_x,
    input  wire signed [31:0] baseline_tile1_scroll_y,
    input  wire        baseline_sprite_enable,

    input  wire [15:0] validate_target,
    input  wire [31:0] validate_data,
    output wire        validate_allowed,
    output wire [1:0]  validate_timing_class,
    input  wire [15:0] move_target,
    input  wire [31:0] move_data,
    output wire        move_allowed,
    output wire [1:0]  move_timing_class,
    input  wire        move_valid,
    output wire        move_ready,

    input  wire        palette_write_ready,
    output reg         framebuffer_palette_write_enable,
    output reg  [7:0]  framebuffer_palette_write_index,
    output reg  [31:0] framebuffer_palette_write_argb,
    output reg         tile_palette_write_enable,
    output reg  [3:0]  tile_palette_write_bank,
    output reg  [7:0]  tile_palette_write_index,
    output reg  [31:0] tile_palette_write_argb,
    output reg         sprite_palette_write_enable,
    output reg  [3:0]  sprite_palette_write_bank,
    output reg  [7:0]  sprite_palette_write_index,
    output reg  [31:0] sprite_palette_write_argb,

    output reg  [23:0] backdrop_rgb,
    output reg signed [31:0] framebuffer_viewport_x,
    output reg signed [31:0] framebuffer_viewport_y,
    output reg         framebuffer_wrap_x,
    output reg         framebuffer_wrap_y,
    output reg         framebuffer_key_enable,
    output reg  [31:0] framebuffer_key,
    output reg         tile0_enable,
    output reg         tile0_above,
    output reg  [7:0]  tile0_opacity,
    output reg         tile0_wrap_x,
    output reg         tile0_wrap_y,
    output reg         tile0_transparent_enable,
    output reg  [7:0]  tile0_transparent_index,
    output reg signed [31:0] tile0_scroll_x,
    output reg signed [31:0] tile0_scroll_y,
    output reg         tile1_enable,
    output reg         tile1_above,
    output reg  [7:0]  tile1_opacity,
    output reg         tile1_wrap_x,
    output reg         tile1_wrap_y,
    output reg         tile1_transparent_enable,
    output reg  [7:0]  tile1_transparent_index,
    output reg signed [31:0] tile1_scroll_x,
    output reg signed [31:0] tile1_scroll_y,
    output reg         sprite_enable
);
    localparam [15:0] TARGET_BACKDROP = 16'h0018;
    localparam [15:0] TARGET_FB_VIEWPORT_X = 16'h004c;
    localparam [15:0] TARGET_FB_VIEWPORT_Y = 16'h0050;
    localparam [15:0] TARGET_FB_VISUAL_CONTROL = 16'h0054;
    localparam [15:0] TARGET_FB_KEY = 16'h0058;
    localparam [15:0] TARGET_TILE0_SCROLL_X = 16'h0088;
    localparam [15:0] TARGET_TILE0_SCROLL_Y = 16'h008c;
    localparam [15:0] TARGET_TILE0_CONTROL = 16'h0098;
    localparam [15:0] TARGET_TILE1_SCROLL_X = 16'h00c8;
    localparam [15:0] TARGET_TILE1_SCROLL_Y = 16'h00cc;
    localparam [15:0] TARGET_TILE1_CONTROL = 16'h00d8;
    localparam [15:0] TARGET_SPRITE_CONTROL = 16'h0180;
    localparam [15:0] TARGET_FB_PALETTE_FIRST = 16'h1000;
    localparam [15:0] TARGET_FB_PALETTE_LAST = 16'h13fc;
    localparam [15:0] TARGET_TILE_PALETTE_FIRST = 16'h2000;
    localparam [15:0] TARGET_TILE_PALETTE_LAST = 16'h5ffc;
    localparam [15:0] TARGET_SPRITE_PALETTE_FIRST = 16'h6000;
    localparam [15:0] TARGET_SPRITE_PALETTE_LAST = 16'h9ffc;

    function automatic target_allowed(
        input [15:0] target,
        input [31:0] data
    );
        begin
            case (target)
                TARGET_BACKDROP: target_allowed = data[31:24] == 8'd0;
                TARGET_FB_VIEWPORT_X,
                TARGET_FB_VIEWPORT_Y,
                TARGET_FB_KEY,
                TARGET_TILE0_SCROLL_X,
                TARGET_TILE0_SCROLL_Y,
                TARGET_TILE1_SCROLL_X,
                TARGET_TILE1_SCROLL_Y: target_allowed = 1'b1;
                TARGET_FB_VISUAL_CONTROL:
                    target_allowed = (data & 32'hffffffc7) == 32'd0;
                TARGET_TILE0_CONTROL:
                    target_allowed =
                        (data & 32'hff00fff8) == 32'd0 &&
                        (!data[0] || baseline_tile0_enable);
                TARGET_TILE1_CONTROL:
                    target_allowed =
                        (data & 32'hff00fff8) == 32'd0 &&
                        (!data[0] || baseline_tile1_enable);
                TARGET_SPRITE_CONTROL:
                    target_allowed = (data & 32'hfffffffe) == 32'd0;
                default: target_allowed = target[1:0] == 2'b00 &&
                    ((target >= TARGET_FB_PALETTE_FIRST &&
                      target <= TARGET_FB_PALETTE_LAST) ||
                     (target >= TARGET_TILE_PALETTE_FIRST &&
                      target <= TARGET_TILE_PALETTE_LAST) ||
                     (target >= TARGET_SPRITE_PALETTE_FIRST &&
                      target <= TARGET_SPRITE_PALETTE_LAST));
            endcase
        end
    endfunction

    function automatic [1:0] target_class(input [15:0] target);
        begin
            if (target == TARGET_BACKDROP ||
                (target >= TARGET_FB_PALETTE_FIRST &&
                 target <= TARGET_FB_PALETTE_LAST) ||
                (target >= TARGET_TILE_PALETTE_FIRST &&
                 target <= TARGET_TILE_PALETTE_LAST))
                target_class = 2'd0;
            else
                target_class = 2'd1;
        end
    endfunction

    wire move_is_palette =
        (move_target >= TARGET_FB_PALETTE_FIRST &&
         move_target <= TARGET_FB_PALETTE_LAST) ||
        (move_target >= TARGET_TILE_PALETTE_FIRST &&
         move_target <= TARGET_TILE_PALETTE_LAST) ||
        (move_target >= TARGET_SPRITE_PALETTE_FIRST &&
         move_target <= TARGET_SPRITE_PALETTE_LAST);
    reg move_pending_q;
    reg [15:0] move_target_q;
    reg [31:0] move_data_q;
    assign validate_allowed = target_allowed(validate_target, validate_data);
    assign validate_timing_class = target_class(validate_target);
    assign move_allowed = target_allowed(move_target, move_data);
    assign move_timing_class = target_class(move_target);
    assign move_ready = !reset && !baseline_restore && !move_pending_q &&
        (!move_is_palette || palette_write_ready);

    task automatic restore_baseline;
        begin
            backdrop_rgb <= baseline_backdrop_rgb;
            framebuffer_viewport_x <= baseline_framebuffer_viewport_x;
            framebuffer_viewport_y <= baseline_framebuffer_viewport_y;
            framebuffer_wrap_x <= baseline_framebuffer_wrap_x;
            framebuffer_wrap_y <= baseline_framebuffer_wrap_y;
            framebuffer_key_enable <= baseline_framebuffer_key_enable;
            framebuffer_key <= baseline_framebuffer_key;
            tile0_enable <= baseline_tile0_enable;
            tile0_above <= baseline_tile0_above;
            tile0_opacity <= baseline_tile0_opacity;
            tile0_wrap_x <= baseline_tile0_wrap_x;
            tile0_wrap_y <= baseline_tile0_wrap_y;
            tile0_transparent_enable <=
                baseline_tile0_transparent_enable;
            tile0_transparent_index <= baseline_tile0_transparent_index;
            tile0_scroll_x <= baseline_tile0_scroll_x;
            tile0_scroll_y <= baseline_tile0_scroll_y;
            tile1_enable <= baseline_tile1_enable;
            tile1_above <= baseline_tile1_above;
            tile1_opacity <= baseline_tile1_opacity;
            tile1_wrap_x <= baseline_tile1_wrap_x;
            tile1_wrap_y <= baseline_tile1_wrap_y;
            tile1_transparent_enable <=
                baseline_tile1_transparent_enable;
            tile1_transparent_index <= baseline_tile1_transparent_index;
            tile1_scroll_x <= baseline_tile1_scroll_x;
            tile1_scroll_y <= baseline_tile1_scroll_y;
            sprite_enable <= baseline_sprite_enable;
        end
    endtask

    always @(posedge clk) begin
        framebuffer_palette_write_enable <= 1'b0;
        tile_palette_write_enable <= 1'b0;
        sprite_palette_write_enable <= 1'b0;
        if (reset || baseline_restore) begin
            move_pending_q <= 1'b0;
            move_target_q <= 16'd0;
            move_data_q <= 32'd0;
            restore_baseline();
            framebuffer_palette_write_index <= 8'd0;
            framebuffer_palette_write_argb <= 32'd0;
            tile_palette_write_bank <= 4'd0;
            tile_palette_write_index <= 8'd0;
            tile_palette_write_argb <= 32'd0;
            sprite_palette_write_bank <= 4'd0;
            sprite_palette_write_index <= 8'd0;
            sprite_palette_write_argb <= 32'd0;
        end else begin
            if (move_pending_q) begin
                move_pending_q <= 1'b0;
                case (move_target_q)
                TARGET_BACKDROP: backdrop_rgb <= move_data_q[23:0];
                TARGET_FB_VIEWPORT_X: framebuffer_viewport_x <= move_data_q;
                TARGET_FB_VIEWPORT_Y: framebuffer_viewport_y <= move_data_q;
                TARGET_FB_VISUAL_CONTROL: begin
                    framebuffer_wrap_x <= move_data_q[3];
                    framebuffer_wrap_y <= move_data_q[4];
                    framebuffer_key_enable <= move_data_q[5];
                end
                TARGET_FB_KEY: framebuffer_key <= move_data_q;
                TARGET_TILE0_SCROLL_X: tile0_scroll_x <= move_data_q;
                TARGET_TILE0_SCROLL_Y: tile0_scroll_y <= move_data_q;
                TARGET_TILE0_CONTROL: begin
                    tile0_enable <= move_data_q[0];
                    tile0_above <= move_data_q[1];
                    tile0_transparent_enable <= move_data_q[2];
                    tile0_transparent_index <= move_data_q[15:8];
                    tile0_opacity <= move_data_q[23:16];
                end
                TARGET_TILE1_SCROLL_X: tile1_scroll_x <= move_data_q;
                TARGET_TILE1_SCROLL_Y: tile1_scroll_y <= move_data_q;
                TARGET_TILE1_CONTROL: begin
                    tile1_enable <= move_data_q[0];
                    tile1_above <= move_data_q[1];
                    tile1_transparent_enable <= move_data_q[2];
                    tile1_transparent_index <= move_data_q[15:8];
                    tile1_opacity <= move_data_q[23:16];
                end
                TARGET_SPRITE_CONTROL: sprite_enable <= move_data_q[0];
                default: begin end
                endcase
            end

            // The active Copper bank was fully validated before promotion.
            if (move_valid && move_ready) begin
                if (!move_is_palette) begin
                    move_pending_q <= 1'b1;
                    move_target_q <= move_target;
                    move_data_q <= move_data;
                end else begin
                    if (move_target >= TARGET_FB_PALETTE_FIRST &&
                        move_target <= TARGET_FB_PALETTE_LAST) begin
                        framebuffer_palette_write_enable <= 1'b1;
                        framebuffer_palette_write_index <=
                            (move_target - TARGET_FB_PALETTE_FIRST) >> 2;
                        framebuffer_palette_write_argb <= move_data;
                    end else if (move_target >= TARGET_TILE_PALETTE_FIRST &&
                        move_target <= TARGET_TILE_PALETTE_LAST) begin
                        tile_palette_write_enable <= 1'b1;
                        {tile_palette_write_bank,
                         tile_palette_write_index} <=
                            (move_target - TARGET_TILE_PALETTE_FIRST) >> 2;
                        tile_palette_write_argb <= move_data;
                    end else begin
                        sprite_palette_write_enable <= 1'b1;
                        {sprite_palette_write_bank,
                         sprite_palette_write_index} <=
                            (move_target - TARGET_SPRITE_PALETTE_FIRST) >> 2;
                        sprite_palette_write_argb <= move_data;
                    end
                end
            end
        end
    end
endmodule

`default_nettype wire
