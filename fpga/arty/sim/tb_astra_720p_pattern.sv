`timescale 1ns/1ps
`default_nettype none

module tb_astra_720p_pattern;
    reg [10:0] x;
    reg [9:0] y;
    wire [23:0] rgb;

    astra_720p_pattern dut (.x(x), .y(y), .rgb(rgb));

    task automatic expect_pixel;
        input [10:0] tx;
        input [9:0] ty;
        input [23:0] expected;
        begin
            x = tx;
            y = ty;
            #1;
            if (rgb !== expected) begin
                $display("FAIL x=%0d y=%0d expected=%06x actual=%06x",
                         tx, ty, expected, rgb);
                $fatal(1);
            end
        end
    endtask

    initial begin
        expect_pixel(11'd1280, 10'd0, 24'h000000);
        expect_pixel(11'd0, 10'd0, 24'hffffff);
        expect_pixel(11'd1279, 10'd719, 24'hffffff);
        expect_pixel(11'd80, 10'd60, 24'hffffff);
        expect_pixel(11'd240, 10'd60, 24'hffff00);
        expect_pixel(11'd400, 10'd60, 24'h00ffff);
        expect_pixel(11'd560, 10'd60, 24'h00ff00);
        expect_pixel(11'd720, 10'd60, 24'hff00ff);
        expect_pixel(11'd880, 10'd60, 24'hff0000);
        expect_pixel(11'd1040, 10'd60, 24'h0000ff);
        expect_pixel(11'd1200, 10'd60, 24'h000000);
        expect_pixel(11'd384, 10'd300, 24'h00dce8);
        expect_pixel(11'd500, 10'd256, 24'h00dce8);
        expect_pixel(11'd64, 10'd200, 24'h304050);
        expect_pixel(11'd65, 10'd192, 24'h304050);
        expect_pixel(11'd65, 10'd193, 24'h41c1e1);
        $display("ASTRA 720P PATTERN PASS");
        $finish;
    end
endmodule

`default_nettype wire
