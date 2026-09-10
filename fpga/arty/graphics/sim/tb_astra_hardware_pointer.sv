`timescale 1ns/1ps
`default_nettype none

module tb_astra_hardware_pointer;
    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    always #3.367 pixel_clk = ~pixel_clk;

    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg shadow_enable = 1'b0;
    reg [10:0] shadow_x = 11'd2;
    reg [10:0] shadow_y = 11'd1;
    reg [4:0] shadow_hot_x = 5'd0;
    reg [4:0] shadow_hot_y = 5'd0;
    reg image_write_enable = 1'b0;
    reg [9:0] image_write_index = 10'd0;
    reg [31:0] image_write_argb = 32'd0;
    reg commit_strobe = 1'b0;
    reg commit_swap_image = 1'b0;
    wire write_ready;
    wire commit_ready;
    wire [31:0] generation;
    reg frame_boundary = 1'b0;
    reg [11:0] pixel_x = 12'd0;
    reg [10:0] pixel_y = 11'd0;
    reg input_valid = 1'b1;
    reg [23:0] input_rgb = 24'h0000ff;
    wire output_valid;
    wire [23:0] output_rgb;

    astra_hardware_pointer #(
        .OUTPUT_WIDTH(8), .OUTPUT_HEIGHT(4)
    ) dut (.*);

    task automatic write_image(input [9:0] index, input [31:0] argb);
        begin
            while (!write_ready) @(posedge build_clk);
            @(negedge build_clk);
            image_write_index = index;
            image_write_argb = argb;
            image_write_enable = 1'b1;
            @(posedge build_clk);
            @(negedge build_clk);
            image_write_enable = 1'b0;
        end
    endtask

    task automatic commit_pointer(input enable, input swap_image);
        begin
            while (!commit_ready) @(posedge build_clk);
            @(negedge build_clk);
            shadow_enable = enable;
            commit_swap_image = swap_image;
            commit_strobe = 1'b1;
            @(posedge build_clk);
            @(negedge build_clk);
            commit_strobe = 1'b0;
            repeat (5) @(posedge pixel_clk);
            @(negedge pixel_clk);
            frame_boundary = 1'b1;
            @(posedge pixel_clk);
            @(negedge pixel_clk);
            frame_boundary = 1'b0;
            while (!commit_ready) @(posedge build_clk);
            @(posedge build_clk);
        end
    endtask

    task automatic expect_steady(
        input [11:0] x,
        input [10:0] y,
        input [23:0] expected
    );
        begin
            @(negedge pixel_clk);
            pixel_x = x;
            pixel_y = y;
            repeat (9) @(posedge pixel_clk);
            #1;
            if (!output_valid || output_rgb !== expected)
                $fatal(1, "pointer pixel got valid=%b rgb=%06x expected=%06x",
                       output_valid, output_rgb, expected);
        end
    endtask

    initial begin
        repeat (4) @(posedge build_clk);
        build_reset = 1'b0;
        repeat (4) @(posedge pixel_clk);
        pixel_reset = 1'b0;

        write_image(10'd0, 32'hffff0000);
        commit_pointer(1'b1, 1'b1);
        if (generation != 32'd1)
            $fatal(1, "first pointer generation missing");
        expect_steady(12'd2, 11'd1, 24'hff0000);
        expect_steady(12'd1, 11'd1, 24'h0000ff);

        shadow_hot_x = 5'd1;
        commit_pointer(1'b1, 1'b0);
        expect_steady(12'd1, 11'd1, 24'hff0000);
        shadow_hot_x = 5'd0;
        commit_pointer(1'b1, 1'b0);

        commit_pointer(1'b0, 1'b0);
        expect_steady(12'd2, 11'd1, 24'h0000ff);
        commit_pointer(1'b1, 1'b0);
        expect_steady(12'd2, 11'd1, 24'hff0000);

        $display("ASTRA HARDWARE POINTER PASS");
        $finish;
    end
endmodule

`default_nettype wire
