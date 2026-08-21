`timescale 1ns/1ps
`default_nettype none

module tb_hdmi_source_mode;
    reg clk_pixel = 0;
    reg reset = 1;
    reg hdmi_output_enable = 0;
    reg [10:0] cx = 0;
    reg [9:0] cy = 0;
    wire hdmi_output_active;

    always #5 clk_pixel = ~clk_pixel;

    hdmi_mode_control dut (
        .clk_pixel(clk_pixel),
        .reset(reset),
        .hdmi_output_enable(hdmi_output_enable),
        .cx(cx),
        .cy(cy),
        .screen_height(10'd720),
        .hdmi_output_active(hdmi_output_active)
    );

    task automatic tick(input [10:0] x, input [9:0] y);
        begin
            cx = x;
            cy = y;
            @(posedge clk_pixel);
            #1;
        end
    endtask

    initial begin
        repeat (3) @(posedge clk_pixel);
        #1;
        reset = 0;
        if (hdmi_output_active)
            $fatal(1, "reset source enabled HDMI before E-EDID");

        hdmi_output_enable = 1;
        tick(11'd100, 10'd100);
        if (hdmi_output_active)
            $fatal(1, "HDMI enabled during active video");
        tick(11'd0, 10'd720);
        if (!hdmi_output_active)
            $fatal(1, "HDMI request was not applied in vertical blank");

        hdmi_output_enable = 0;
        tick(11'd100, 10'd100);
        if (!hdmi_output_active)
            $fatal(1, "HDMI disabled during active video");
        tick(11'd0, 10'd720);
        if (hdmi_output_active)
            $fatal(1, "DVI request was not applied in vertical blank");

        hdmi_output_enable = 1;
        reset = 1;
        tick(11'd100, 10'd100);
        if (hdmi_output_active)
            $fatal(1, "warm reset did not return source to DVI");

        $display("ASTRA HDMI SOURCE MODE PASS");
        $finish;
    end
endmodule

`default_nettype wire
