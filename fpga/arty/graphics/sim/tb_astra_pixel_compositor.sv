`timescale 1ns/1ps
`default_nettype none

module tb_astra_pixel_compositor;
    localparam [1:0] FORMAT_INDEX8 = 2'd0;
    localparam [1:0] FORMAT_RGB565 = 2'd1;
    localparam [1:0] FORMAT_XRGB8888 = 2'd2;

    reg control_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #2.5 control_clk = ~control_clk;
    always #6.734 pixel_clk = ~pixel_clk;

    reg pixel_reset = 1'b1;
    reg control_reset = 1'b1;
    reg baseline_restore_start = 1'b0;
    wire baseline_restore_busy;
    wire baseline_restore_done;
    wire host_write_ready;
    reg copper_write_enable = 1'b0;
    reg copper_write_tile = 1'b0;
    reg [3:0] copper_write_bank = 4'd0;
    reg [7:0] copper_write_index = 8'd0;
    reg [31:0] copper_write_argb = 32'd0;
    wire copper_write_ready;
    reg framebuffer_write_enable = 1'b0;
    reg [7:0] framebuffer_write_index = 8'd0;
    reg [31:0] framebuffer_write_argb = 32'd0;
    reg tile_write_enable = 1'b0;
    reg [3:0] tile_write_bank = 4'd0;
    reg [7:0] tile_write_index = 8'd0;
    reg [31:0] tile_write_argb = 32'd0;

    reg input_valid = 1'b0;
    reg [23:0] backdrop_rgb = 24'h102030;
    reg sprite_enable = 1'b0;
    reg [31:0] sprite_behind_premult_argb = 32'd0;
    reg [31:0] sprite_front_premult_argb = 32'd0;
    reg framebuffer_enable = 1'b0;
    reg [1:0] framebuffer_format = FORMAT_INDEX8;
    reg framebuffer_pixel_valid = 1'b0;
    reg [31:0] framebuffer_pixel_value = 32'd0;
    reg framebuffer_key_enable = 1'b0;
    reg [31:0] framebuffer_key = 32'd0;
    wire [7:0] framebuffer_palette_index;
    wire [31:0] framebuffer_palette_argb;

    reg tile0_enable = 1'b0;
    reg tile0_above_framebuffer = 1'b0;
    reg [7:0] tile0_opacity = 8'd255;
    reg tile0_pixel_valid = 1'b0;
    reg [3:0] tile0_palette_bank = 4'd0;
    reg [7:0] tile0_palette_index = 8'd0;
    wire [3:0] tile0_palette_read_bank;
    wire [7:0] tile0_palette_read_index;
    wire [31:0] tile0_palette_argb;

    reg tile1_enable = 1'b0;
    reg tile1_above_framebuffer = 1'b0;
    reg [7:0] tile1_opacity = 8'd255;
    reg tile1_pixel_valid = 1'b0;
    reg [3:0] tile1_palette_bank = 4'd0;
    reg [7:0] tile1_palette_index = 8'd0;
    wire [3:0] tile1_palette_read_bank;
    wire [7:0] tile1_palette_read_index;
    wire [31:0] tile1_palette_argb;

    wire output_valid;
    wire [23:0] output_rgb;

    astra_palette_store palette_i (
        .control_clk(control_clk),
        .control_reset(control_reset),
        .baseline_restore_start(baseline_restore_start),
        .baseline_restore_busy(baseline_restore_busy),
        .baseline_restore_done(baseline_restore_done),
        .host_write_ready(host_write_ready),
        .framebuffer_write_enable(framebuffer_write_enable),
        .framebuffer_write_index(framebuffer_write_index),
        .framebuffer_write_argb(framebuffer_write_argb),
        .tile_write_enable(tile_write_enable),
        .tile_write_bank(tile_write_bank),
        .tile_write_index(tile_write_index),
        .tile_write_argb(tile_write_argb),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .copper_write_enable(copper_write_enable),
        .copper_write_tile(copper_write_tile),
        .copper_write_bank(copper_write_bank),
        .copper_write_index(copper_write_index),
        .copper_write_argb(copper_write_argb),
        .copper_write_ready(copper_write_ready),
        .framebuffer_read_index(framebuffer_palette_index),
        .framebuffer_read_argb(framebuffer_palette_argb),
        .tile0_read_bank(tile0_palette_read_bank),
        .tile0_read_index(tile0_palette_read_index),
        .tile0_read_argb(tile0_palette_argb),
        .tile1_read_bank(tile1_palette_read_bank),
        .tile1_read_index(tile1_palette_read_index),
        .tile1_read_argb(tile1_palette_argb)
    );

    astra_pixel_compositor dut (
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .input_valid(input_valid),
        .backdrop_rgb(backdrop_rgb),
        .sprite_enable(sprite_enable),
        .sprite_behind_premult_argb(sprite_behind_premult_argb),
        .sprite_front_premult_argb(sprite_front_premult_argb),
        .framebuffer_enable(framebuffer_enable),
        .framebuffer_format(framebuffer_format),
        .framebuffer_pixel_valid(framebuffer_pixel_valid),
        .framebuffer_pixel_value(framebuffer_pixel_value),
        .framebuffer_key_enable(framebuffer_key_enable),
        .framebuffer_key(framebuffer_key),
        .framebuffer_palette_index(framebuffer_palette_index),
        .framebuffer_palette_argb(framebuffer_palette_argb),
        .tile0_enable(tile0_enable),
        .tile0_above_framebuffer(tile0_above_framebuffer),
        .tile0_opacity(tile0_opacity),
        .tile0_pixel_valid(tile0_pixel_valid),
        .tile0_palette_bank(tile0_palette_bank),
        .tile0_palette_index(tile0_palette_index),
        .tile0_palette_read_bank(tile0_palette_read_bank),
        .tile0_palette_read_index(tile0_palette_read_index),
        .tile0_palette_argb(tile0_palette_argb),
        .tile1_enable(tile1_enable),
        .tile1_above_framebuffer(tile1_above_framebuffer),
        .tile1_opacity(tile1_opacity),
        .tile1_pixel_valid(tile1_pixel_valid),
        .tile1_palette_bank(tile1_palette_bank),
        .tile1_palette_index(tile1_palette_index),
        .tile1_palette_read_bank(tile1_palette_read_bank),
        .tile1_palette_read_index(tile1_palette_read_index),
        .tile1_palette_argb(tile1_palette_argb),
        .output_valid(output_valid),
        .output_rgb(output_rgb)
    );

    function automatic [23:0] reference_blend(
        input [23:0] destination,
        input [31:0] source,
        input integer opacity,
        input integer apply_source
    );
        integer alpha;
        integer red;
        integer green;
        integer blue;
        begin
            alpha = (source[31:24] * opacity + 127) / 255;
            if (!apply_source || alpha == 0) begin
                reference_blend = destination;
            end else begin
                red = (source[23:16] * alpha +
                    destination[23:16] * (255 - alpha) + 127) / 255;
                green = (source[15:8] * alpha +
                    destination[15:8] * (255 - alpha) + 127) / 255;
                blue = (source[7:0] * alpha +
                    destination[7:0] * (255 - alpha) + 127) / 255;
                reference_blend = {red[7:0], green[7:0], blue[7:0]};
            end
        end
    endfunction

    function automatic [23:0] reference_premult_blend(
        input [23:0] destination,
        input [31:0] source,
        input integer apply_source
    );
        integer inverse_alpha;
        integer red;
        integer green;
        integer blue;
        begin
            if (!apply_source) begin
                reference_premult_blend = destination;
            end else begin
                inverse_alpha = 255 - source[31:24];
                red = (source[23:16] * 255 +
                    destination[23:16] * inverse_alpha + 127) / 255;
                green = (source[15:8] * 255 +
                    destination[15:8] * inverse_alpha + 127) / 255;
                blue = (source[7:0] * 255 +
                    destination[7:0] * inverse_alpha + 127) / 255;
                reference_premult_blend = {
                    red[7:0], green[7:0], blue[7:0]
                };
            end
        end
    endfunction

    task automatic write_framebuffer_palette(
        input [7:0] index,
        input [31:0] argb
    );
        begin
            @(negedge control_clk);
            while (!host_write_ready) @(negedge control_clk);
            framebuffer_write_index = index;
            framebuffer_write_argb = argb;
            framebuffer_write_enable = 1'b1;
            @(negedge control_clk);
            framebuffer_write_enable = 1'b0;
            repeat (8) @(posedge pixel_clk);
        end
    endtask

    task automatic write_tile_palette(
        input [3:0] bank,
        input [7:0] index,
        input [31:0] argb
    );
        begin
            @(negedge control_clk);
            while (!host_write_ready) @(negedge control_clk);
            tile_write_bank = bank;
            tile_write_index = index;
            tile_write_argb = argb;
            tile_write_enable = 1'b1;
            @(negedge control_clk);
            tile_write_enable = 1'b0;
            repeat (8) @(posedge pixel_clk);
        end
    endtask

    task automatic clear_sources;
        begin
            framebuffer_enable = 1'b0;
            framebuffer_pixel_valid = 1'b0;
            framebuffer_key_enable = 1'b0;
            sprite_enable = 1'b0;
            sprite_behind_premult_argb = 32'd0;
            sprite_front_premult_argb = 32'd0;
            tile0_enable = 1'b0;
            tile0_pixel_valid = 1'b0;
            tile0_above_framebuffer = 1'b0;
            tile0_opacity = 8'd255;
            tile1_enable = 1'b0;
            tile1_pixel_valid = 1'b0;
            tile1_above_framebuffer = 1'b0;
            tile1_opacity = 8'd255;
        end
    endtask

    task automatic send_and_expect(input [23:0] expected_rgb);
        integer cycles;
        begin
            @(negedge pixel_clk);
            input_valid = 1'b1;
            @(negedge pixel_clk);
            input_valid = 1'b0;
            cycles = 0;
            while (!output_valid) begin
                @(posedge pixel_clk);
                #1;
                cycles = cycles + 1;
                if (cycles > 50)
                    $fatal(1, "compositor result timed out");
            end
            if (output_rgb !== expected_rgb)
                $fatal(1, "got RGB=%06x expected=%06x",
                       output_rgb, expected_rgb);
            @(posedge pixel_clk);
            #1;
            if (output_valid)
                $fatal(1, "compositor emitted a duplicate pixel");
        end
    endtask

    reg [23:0] expected;
    reg [31:0] alpha_color;
    integer alpha;
    initial begin
        repeat (5) @(posedge pixel_clk);
        pixel_reset = 1'b0;
        control_reset = 1'b0;

        clear_sources();
        backdrop_rgb = 24'h102030;
        send_and_expect(24'h102030);
        $display("backdrop pass");

        write_framebuffer_palette(8'h12, 32'h80ff0000);
        clear_sources();
        framebuffer_enable = 1'b1;
        framebuffer_format = FORMAT_INDEX8;
        framebuffer_pixel_valid = 1'b1;
        framebuffer_pixel_value = 32'h00000012;
        expected = reference_blend(backdrop_rgb, 32'h80ff0000, 255, 1);
        send_and_expect(expected);
        $display("INDEX8 framebuffer palette/alpha pass");

        clear_sources();
        framebuffer_enable = 1'b1;
        framebuffer_format = FORMAT_RGB565;
        framebuffer_pixel_valid = 1'b1;
        framebuffer_pixel_value = 32'h0000f81f;
        send_and_expect(24'hff00ff);
        $display("RGB565 expansion pass");

        clear_sources();
        framebuffer_enable = 1'b1;
        framebuffer_format = FORMAT_XRGB8888;
        framebuffer_pixel_valid = 1'b1;
        framebuffer_pixel_value = 32'hff112233;
        framebuffer_key_enable = 1'b1;
        framebuffer_key = 32'hff112233;
        send_and_expect(backdrop_rgb);
        framebuffer_key_enable = 1'b0;
        send_and_expect(24'h112233);
        $display("XRGB8888/key pass");

        write_tile_palette(4'd2, 8'h03, 32'h80ff0000);
        write_tile_palette(4'd5, 8'h07, 32'hc000ff00);
        clear_sources();
        tile1_enable = 1'b1;
        tile1_pixel_valid = 1'b1;
        tile1_palette_bank = 4'd2;
        tile1_palette_index = 8'h03;
        tile0_enable = 1'b1;
        tile0_pixel_valid = 1'b1;
        tile0_palette_bank = 4'd5;
        tile0_palette_index = 8'h07;
        expected = reference_blend(backdrop_rgb, 32'h80ff0000, 255, 1);
        expected = reference_blend(expected, 32'hc000ff00, 255, 1);
        send_and_expect(expected);

        framebuffer_enable = 1'b1;
        framebuffer_format = FORMAT_XRGB8888;
        framebuffer_pixel_valid = 1'b1;
        framebuffer_pixel_value = 32'hff0000ff;
        tile0_above_framebuffer = 1'b1;
        expected = reference_blend(backdrop_rgb, 32'h80ff0000, 255, 1);
        expected = reference_blend(expected, 32'hff0000ff, 255, 1);
        expected = reference_blend(expected, 32'hc000ff00, 255, 1);
        send_and_expect(expected);
        $display("layer ordering/palette-bank pass");

        write_framebuffer_palette(8'h13, 32'h8000ff00);
        write_tile_palette(4'd2, 8'h04, 32'h80ffff00);
        write_tile_palette(4'd5, 8'h08, 32'h80ff00ff);
        clear_sources();
        tile1_enable = 1'b1;
        tile1_pixel_valid = 1'b1;
        tile1_palette_bank = 4'd2;
        tile1_palette_index = 8'h04;
        sprite_enable = 1'b1;
        sprite_behind_premult_argb = 32'h80008080;
        framebuffer_enable = 1'b1;
        framebuffer_format = FORMAT_INDEX8;
        framebuffer_pixel_valid = 1'b1;
        framebuffer_pixel_value = 32'h00000013;
        sprite_front_premult_argb = 32'h80800000;
        tile0_enable = 1'b1;
        tile0_above_framebuffer = 1'b1;
        tile0_pixel_valid = 1'b1;
        tile0_palette_bank = 4'd5;
        tile0_palette_index = 8'h08;
        expected = reference_blend(
            backdrop_rgb, 32'h80ffff00, 255, 1);
        expected = reference_premult_blend(
            expected, sprite_behind_premult_argb, 1);
        expected = reference_blend(expected, 32'h8000ff00, 255, 1);
        expected = reference_premult_blend(
            expected, sprite_front_premult_argb, 1);
        expected = reference_blend(expected, 32'h80ff00ff, 255, 1);
        send_and_expect(expected);
        $display("seven-layer sprite ordering pass");

        // Exhaust every source alpha while changing global opacity. This
        // proves the constant-255 implementation against integer division.
        clear_sources();
        tile0_enable = 1'b1;
        tile0_pixel_valid = 1'b1;
        tile0_above_framebuffer = 1'b1;
        tile0_palette_bank = 4'd9;
        tile0_palette_index = 8'h55;
        backdrop_rgb = 24'h365a7e;
        for (alpha = 0; alpha < 256; alpha = alpha + 1) begin
            alpha_color = {alpha[7:0], 8'he1, 8'h73, 8'h29};
            write_tile_palette(4'd9, 8'h55, alpha_color);
            tile0_opacity = 255 - alpha;
            expected = reference_blend(
                backdrop_rgb, alpha_color, 255 - alpha, 1);
            send_and_expect(expected);
        end
        $display("alpha/opacity exhaustive pass");

        clear_sources();
        framebuffer_enable = 1'b1;
        framebuffer_format = 2'd3;
        framebuffer_pixel_valid = 1'b1;
        framebuffer_pixel_value = 32'hffffffff;
        send_and_expect(backdrop_rgb);
        $display("reserved-format containment pass");

        $display("ASTRA PIXEL COMPOSITOR PASS");
        $finish;
    end
endmodule

`default_nettype wire
