// Focused dual-clock/rendering gate for the boot HDMI text plane.
`timescale 1ns/1ps

module tb_post_console;
    reg cpu_clk = 1'b0;
    reg pixel_clk = 1'b0;
    reg pixel_rst = 1'b1;
    reg [11:0] cpu_addr = 12'd0;
    reg [7:0] cpu_wdata = 8'd0;
    reg cpu_we = 1'b0;
    wire [7:0] cpu_rdata;
    reg [9:0] pixel_x = 10'd0;
    reg [9:0] pixel_y = 10'd0;
    wire [23:0] rgb;

    always #5 cpu_clk = ~cpu_clk;
    always #19 pixel_clk = ~pixel_clk;

    post_console dut (
        .cpu_clk(cpu_clk),
        .cpu_addr(cpu_addr),
        .cpu_wdata(cpu_wdata),
        .cpu_we(cpu_we),
        .cpu_rdata(cpu_rdata),
        .pixel_clk(pixel_clk),
        .pixel_rst(pixel_rst),
        .pixel_x(pixel_x),
        .pixel_y(pixel_y),
        .rgb(rgb)
    );

    task automatic write_char(input [11:0] address, input [7:0] value);
        begin
            @(negedge cpu_clk);
            cpu_addr = address;
            cpu_wdata = value;
            cpu_we = 1'b1;
            @(negedge cpu_clk);
            cpu_we = 1'b0;
        end
    endtask

    integer x;
    integer y;
    integer foreground = 0;
    integer background = 0;
    initial begin
        if ($size(dut.font_rom) != 2048)
            $fatal(1, "POST font ROM must contain exactly one 2 KiB bank");

        repeat (3) @(posedge pixel_clk);
        pixel_rst = 1'b0;
        write_char(12'd0, 8'h41); // 'A'
        repeat (3) @(posedge pixel_clk);

        // Hold each coordinate long enough for character and font BRAM reads.
        for (y = 0; y < 16; y = y + 1) begin
            for (x = 0; x < 8; x = x + 1) begin
                pixel_x = x;
                pixel_y = y;
                repeat (3) @(posedge pixel_clk);
                #1;
                if (rgb == 24'he8edf2) foreground = foreground + 1;
                else if (rgb == 24'h101820) background = background + 1;
                else $fatal(1, "unexpected active RGB %06x at %0d,%0d", rgb, x, y);
            end
        end
        if (foreground == 0 || background == 0)
            $fatal(1, "glyph did not contain both foreground and background pixels");

        pixel_x = 10'd720;
        pixel_y = 10'd0;
        repeat (3) @(posedge pixel_clk);
        #1;
        if (rgb != 24'h000000) $fatal(1, "blanking pixel was not black: %06x", rgb);

        $display("POST CONSOLE PASS foreground=%0d background=%0d", foreground, background);
        $finish;
    end
endmodule
