`timescale 1ns/1ps
`default_nettype none

module tb_astra_copper_registers;
    reg clk = 0;
    reg reset = 1;
    always #3 clk = ~clk;
    reg baseline_restore = 0;
    reg [23:0] baseline_backdrop_rgb = 24'h010203;
    reg signed [31:0] baseline_framebuffer_viewport_x = 11;
    reg signed [31:0] baseline_framebuffer_viewport_y = 22;
    reg baseline_framebuffer_wrap_x = 0;
    reg baseline_framebuffer_wrap_y = 1;
    reg baseline_framebuffer_key_enable = 0;
    reg [31:0] baseline_framebuffer_key = 32'h1234;
    reg baseline_sprite_enable = 1;
    reg [15:0] validate_target = 0;
    reg [31:0] validate_data = 0;
    wire validate_allowed;
    wire [1:0] validate_timing_class;
    reg [15:0] move_target = 0;
    reg [31:0] move_data = 0;
    wire move_allowed;
    wire [1:0] move_timing_class;
    reg move_valid = 0;
    wire move_ready;
    reg palette_write_ready = 1;
    wire framebuffer_palette_write_enable;
    wire [7:0] framebuffer_palette_write_index;
    wire [31:0] framebuffer_palette_write_argb;
    wire sprite_palette_write_enable;
    wire [3:0] sprite_palette_write_bank;
    wire [7:0] sprite_palette_write_index;
    wire [31:0] sprite_palette_write_argb;
    wire [23:0] backdrop_rgb;
    wire signed [31:0] framebuffer_viewport_x;
    wire signed [31:0] framebuffer_viewport_y;
    wire framebuffer_wrap_x, framebuffer_wrap_y;
    wire framebuffer_key_enable;
    wire [31:0] framebuffer_key;
    wire sprite_enable;

    astra_copper_registers dut (.*);

    task automatic move(input [15:0] target, input [31:0] data);
        begin
            @(negedge clk);
            move_target = target;
            move_data = data;
            move_valid = 1;
            #1;
            if (!move_allowed)
                $fatal(1, "legal target rejected %04x data=%08x", target, data);
            while (!move_ready) @(negedge clk);
            @(negedge clk);
            move_valid = 0;
            if (target < 16'h1000) begin
                @(posedge clk);
                #1;
            end
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        reset = 0;
        @(posedge clk);
        #1;
        if (backdrop_rgb != baseline_backdrop_rgb ||
            framebuffer_viewport_x != 11 || framebuffer_viewport_y != 22 ||
            !sprite_enable)
            $fatal(1, "baseline did not initialize");

        validate_target = 16'h0040;
        validate_data = 0;
        #1;
        if (validate_allowed)
            $fatal(1, "structural framebuffer base was whitelisted");
        validate_target = 16'h0054;
        validate_data = 32'h1;
        #1;
        if (validate_allowed)
            $fatal(1, "structural framebuffer control bit accepted");
        validate_data = 32'h38;
        #1;
        if (!validate_allowed)
            $fatal(1, "visual framebuffer control rejected");
        validate_target = 16'h0098;
        validate_data = 32'h00000001;
        #1;
        if (validate_allowed)
            $fatal(1, "retired tile0 control accepted");
        validate_data = 32'h00000000;
        #1;
        if (validate_allowed)
            $fatal(1, "retired tile0 disable accepted");
        validate_target = 16'h00d8;
        #1;
        if (validate_allowed)
            $fatal(1, "retired tile1 control accepted");

        move(16'h0018, 32'h00abcdef);
        if (backdrop_rgb != 24'habcdef || move_timing_class != 0)
            $fatal(1, "pixel-class backdrop failed");
        move(16'h004c, -32'sd50);
        move(16'h0054, 32'h38);
        if (framebuffer_viewport_x != -50 || !framebuffer_wrap_x ||
            !framebuffer_wrap_y || !framebuffer_key_enable ||
            move_timing_class != 1)
            $fatal(1, "next-scanline visual state failed");

        move_target = 16'h00d8;
        move_data = 32'h00000001;
        #1;
        if (move_allowed || !move_ready)
            $fatal(1, "retired tile target remained writable");

        palette_write_ready = 0;
        @(negedge clk);
        move_target = 16'h1120;
        move_data = 32'hff123456;
        move_valid = 1;
        @(posedge clk);
        #1;
        if (move_ready)
            $fatal(1, "palette MOVE ignored backpressure");
        repeat (3) @(negedge clk);
        palette_write_ready = 1;
        @(posedge clk);
        #1;
        if (!move_ready)
            $fatal(1, "palette MOVE did not register available capacity");
        @(posedge clk);
        #1;
        if (!framebuffer_palette_write_enable ||
            framebuffer_palette_write_index != 8'h48 ||
            framebuffer_palette_write_argb != 32'hff123456)
            $fatal(1, "framebuffer palette target decode failed");
        @(negedge clk);
        move_valid = 0;

        @(negedge clk);
        validate_target = 16'h3554;
        validate_data = 32'hff010203;
        move_target = validate_target;
        move_data = validate_data;
        #1;
        if (validate_allowed || move_allowed)
            $fatal(1, "removed tile palette target was accepted");
        move(16'h8aa8, 32'hff040506);
        if (!sprite_palette_write_enable ||
            sprite_palette_write_bank != 4'ha ||
            sprite_palette_write_index != 8'haa)
            $fatal(1, "sprite palette target decode failed");

        baseline_restore = 1;
        @(posedge clk);
        #1;
        baseline_restore = 0;
        if (backdrop_rgb != 24'h010203 || framebuffer_viewport_x != 11 ||
            framebuffer_wrap_x || !framebuffer_wrap_y ||
            !sprite_enable)
            $fatal(1, "frame baseline restore failed");
        $display("ASTRA COPPER REGISTERS PASS");
        $finish;
    end
endmodule

`default_nettype wire
