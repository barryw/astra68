`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_surface_validator;
    reg clk = 1'b0;
    reg reset = 1'b1;
    always #2.5 clk = ~clk;

    reg start = 1'b0;
    reg [31:0] expected_generation = 32'd7;
    reg [1:0] required_access = `ASTRA_RENDER_SURFACE_WRITE;
    reg palette_required = 1'b0;
    reg [31:0] arena_bytes = 32'h08000000;
    reg [31:0] version_size;
    reg [31:0] generation;
    reg [31:0] data_offset;
    reg [31:0] data_bytes;
    reg [31:0] pitch;
    reg [31:0] width_height;
    reg [31:0] format_flags;
    reg [31:0] palette_offset;
    wire busy;
    wire done;
    wire descriptor_valid;
    wire [7:0] format;
    wire [2:0] bytes_per_pixel;
    wire [15:0] width;
    wire [15:0] height;
    wire [31:0] validated_data_offset;
    wire [31:0] validated_data_bytes;
    wire [31:0] validated_pitch;
    wire [31:0] validated_palette_offset;

    astra_render_surface_validator dut (
        .clk(clk),
        .reset(reset),
        .start(start),
        .expected_generation(expected_generation),
        .required_access(required_access),
        .palette_required(palette_required),
        .arena_bytes(arena_bytes),
        .version_size(version_size),
        .generation(generation),
        .data_offset(data_offset),
        .data_bytes(data_bytes),
        .pitch(pitch),
        .width_height(width_height),
        .format_flags(format_flags),
        .palette_offset(palette_offset),
        .busy(busy),
        .done(done),
        .descriptor_valid(descriptor_valid),
        .format(format),
        .bytes_per_pixel(bytes_per_pixel),
        .width(width),
        .height(height),
        .validated_data_offset(validated_data_offset),
        .validated_data_bytes(validated_data_bytes),
        .validated_pitch(validated_pitch),
        .validated_palette_offset(validated_palette_offset)
    );

    task automatic set_descriptor(
        input [7:0] descriptor_format,
        input [7:0] descriptor_flags,
        input [15:0] descriptor_width,
        input [15:0] descriptor_height,
        input [31:0] descriptor_offset,
        input [31:0] descriptor_bytes,
        input [31:0] descriptor_pitch
    );
        begin
            version_size = {16'd1, 16'd32};
            generation = expected_generation;
            data_offset = descriptor_offset;
            data_bytes = descriptor_bytes;
            pitch = descriptor_pitch;
            width_height = {descriptor_width, descriptor_height};
            format_flags = {descriptor_format, descriptor_flags, 16'd0};
            palette_offset = 32'd0;
        end
    endtask

    task automatic validate(input expected, input [255:0] label);
        begin
            @(negedge clk);
            start = 1'b1;
            @(negedge clk);
            start = 1'b0;
            wait (done);
            if (descriptor_valid !== expected)
                $fatal(1, "%0s expected valid=%0d got=%0d",
                       label, expected, descriptor_valid);
            @(negedge clk);
        end
    endtask

    initial begin
        version_size = 32'd0;
        generation = 32'd0;
        data_offset = 32'd0;
        data_bytes = 32'd0;
        pitch = 32'd0;
        width_height = 32'd0;
        format_flags = 32'd0;
        palette_offset = 32'd0;

        repeat (4) @(posedge clk);
        reset = 1'b0;

        set_descriptor(`ASTRA_RENDER_FORMAT_INDEX8,
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd1280, 16'd720, 32'h00100000,
                       32'd921600, 32'd1280);
        validate(1'b1, "INDEX8 exact allocation");
        if (format != `ASTRA_RENDER_FORMAT_INDEX8 ||
            bytes_per_pixel != 3'd1 || width != 16'd1280 ||
            height != 16'd720 || validated_data_offset != 32'h00100000 ||
            validated_data_bytes != 32'd921600 ||
            validated_pitch != 32'd1280)
            $fatal(1, "INDEX8 decoded outputs mismatch");

        set_descriptor(`ASTRA_RENDER_FORMAT_RGB565,
                       `ASTRA_RENDER_SURFACE_READ |
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd4096, 16'd4096, 32'h02000000,
                       32'h02000000, 32'd8192);
        validate(1'b1, "maximum RGB565 surface");
        if (bytes_per_pixel != 3'd2)
            $fatal(1, "RGB565 bytes-per-pixel mismatch");

        set_descriptor(`ASTRA_RENDER_FORMAT_ARGB8888,
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd64, 16'd64, 32'h07ffc000,
                       32'h00004000, 32'd256);
        validate(1'b1, "ARGB8888 exact arena end");

        data_bytes = data_bytes - 32'd1;
        validate(1'b0, "allocation one byte short");

        set_descriptor(`ASTRA_RENDER_FORMAT_RGB565,
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd32, 16'd32, 32'h00010001,
                       32'd4096, 32'd64);
        validate(1'b0, "misaligned RGB565 base");

        set_descriptor(`ASTRA_RENDER_FORMAT_XRGB8888,
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd32, 16'd32, 32'h00010000,
                       32'd4096, 32'd130);
        validate(1'b0, "misaligned XRGB8888 pitch");

        set_descriptor(`ASTRA_RENDER_FORMAT_INDEX8,
                       `ASTRA_RENDER_SURFACE_READ,
                       16'd32, 16'd32, 32'h00010000,
                       32'd1024, 32'd32);
        validate(1'b0, "missing required write right");

        required_access = `ASTRA_RENDER_SURFACE_READ;
        validate(1'b1, "read right accepted");
        required_access = `ASTRA_RENDER_SURFACE_WRITE;

        generation = expected_generation + 32'd1;
        validate(1'b0, "stale generation");

        set_descriptor(`ASTRA_RENDER_FORMAT_INDEX8,
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd4097, 16'd1, 32'h00010000,
                       32'd8192, 32'd8192);
        validate(1'b0, "width over implementation limit");

        set_descriptor(8'd8, `ASTRA_RENDER_SURFACE_WRITE,
                       16'd32, 16'd32, 32'h00010000,
                       32'd4096, 32'd128);
        validate(1'b0, "unknown format");

        set_descriptor(`ASTRA_RENDER_FORMAT_MASK1,
                       `ASTRA_RENDER_SURFACE_READ,
                       16'd13, 16'd2, 32'h00010000,
                       32'd4, 32'd2);
        required_access = `ASTRA_RENDER_SURFACE_READ;
        validate(1'b1, "packed MASK1 surface");
        if (bytes_per_pixel != 3'd0)
            $fatal(1, "MASK1 bytes-per-pixel mismatch");

        set_descriptor(`ASTRA_RENDER_FORMAT_INDEX8,
                       `ASTRA_RENDER_SURFACE_READ,
                       16'd32, 16'd32, 32'h00010000,
                       32'd1024, 32'd32);
        palette_required = 1'b1;
        palette_offset = 32'h00020000;
        validate(1'b1, "INDEX8 palette attachment");
        if (validated_palette_offset != 32'h00020000)
            $fatal(1, "palette offset output mismatch");
        palette_offset = 32'h00020001;
        validate(1'b0, "misaligned palette attachment");

        set_descriptor(`ASTRA_RENDER_FORMAT_INDEX4,
                       `ASTRA_RENDER_SURFACE_READ,
                       16'd32, 16'd32, 32'h00010000,
                       32'd512, 32'd16);
        palette_offset = arena_bytes - 32'd64;
        validate(1'b1, "INDEX4 64-byte palette at arena end");
        palette_offset = arena_bytes - 32'd32;
        validate(1'b0, "INDEX4 palette beyond arena");

        set_descriptor(`ASTRA_RENDER_FORMAT_A8,
                       `ASTRA_RENDER_SURFACE_READ,
                       16'd32, 16'd32, 32'h00010000,
                       32'd1024, 32'd32);
        palette_offset = 32'd0;
        validate(1'b1, "A8 glyph strike needs no palette");
        palette_offset = 32'h00020000;
        validate(1'b0, "A8 glyph strike rejects palette");
        palette_required = 1'b0;
        required_access = `ASTRA_RENDER_SURFACE_WRITE;

        set_descriptor(`ASTRA_RENDER_FORMAT_INDEX8,
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd32, 16'd32, 32'h07ffff00,
                       32'd512, 32'd32);
        validate(1'b0, "allocation beyond arena");

        set_descriptor(`ASTRA_RENDER_FORMAT_INDEX8,
                       `ASTRA_RENDER_SURFACE_WRITE,
                       16'd32, 16'd32, 32'h00010000,
                       32'd1024, 32'd32);
        version_size[15:0] = 16'd31;
        validate(1'b0, "wrong descriptor size");
        version_size = {16'd1, 16'd32};
        palette_offset = 32'd64;
        validate(1'b0, "palette without command flag");

        $display("PASS astra_render_surface_validator");
        $finish;
    end
endmodule

`default_nettype wire
