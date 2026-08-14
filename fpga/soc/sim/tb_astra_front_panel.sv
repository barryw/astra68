`timescale 1ns/1ps
`default_nettype none

module tb_astra_front_panel;
    reg clk = 1'b0;
    reg rst = 1'b1;
    reg [5:0] buttons = 6'd0;
    reg [3:0] switches = 4'd0;
    reg select = 1'b0;
    reg [5:0] reg_index = 6'h3f;
    reg write_strobe = 1'b0;
    reg [31:0] write_data = 32'd0;
    reg [3:0] byte_enable = 4'd0;
    wire [31:0] read_data;
    reg [7:0] diagnostic_leds = 8'ha5;
    wire [7:0] leds;

    always #5 clk = ~clk;

    astra_front_panel #(
        .CLK_HZ(1000),
        .SAMPLE_HZ(100),
        .DEBOUNCE_SAMPLES(3),
        .ACTIVITY_LED(7)
    ) dut (
        .clk(clk), .rst(rst),
        .buttons(buttons), .switches(switches),
        .select(select), .reg_index(reg_index),
        .write_strobe(write_strobe), .write_data(write_data),
        .byte_enable(byte_enable), .read_data(read_data),
        .diagnostic_leds(diagnostic_leds), .leds(leds)
    );

    task automatic expect_read(input [5:0] index, input [31:0] expected);
        begin
            reg_index = index;
            #1;
            if (read_data !== expected)
                $fatal(1, "register %0d expected %08x got %08x",
                       index, expected, read_data);
        end
    endtask

    task automatic write_reg(input [5:0] index, input [31:0] value,
                             input [3:0] enables);
        begin
            @(negedge clk);
            select = 1'b1;
            reg_index = index;
            write_data = value;
            byte_enable = enables;
            write_strobe = 1'b1;
            @(posedge clk);
            #1;
            write_strobe = 1'b0;
            select = 1'b0;
            byte_enable = 4'd0;
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        rst = 1'b0;
        @(posedge clk);
        #1;

        expect_read(6'h00, 32'h504e4c30);
        expect_read(6'h01, 32'h00010000);
        expect_read(6'h02, 32'h0f040608);
        if (leds !== diagnostic_leds)
            $fatal(1, "diagnostic LEDs not selected after reset");

        write_reg(6'h06, 32'h0000003c, 4'b1111);
        write_reg(6'h07, 32'h0000000f, 4'b1111);
        if (leds !== 8'hac)
            $fatal(1, "partial LED ownership expected ac got %02x", leds);
        write_reg(6'h07, 32'h000000ff, 4'b1111);
        if (leds !== 8'h3c)
            $fatal(1, "full LED ownership expected 3c got %02x", leds);
        write_reg(6'h08, 32'h00000003, 4'b0001);
        expect_read(6'h06, 32'h0000003f);
        write_reg(6'h09, 32'h0000000c, 4'b0001);
        expect_read(6'h06, 32'h00000033);
        write_reg(6'h0a, 32'h00000055, 4'b0001);
        expect_read(6'h06, 32'h00000066);

        write_reg(6'h0c, 32'd2, 4'b0011);
        write_reg(6'h0b, 32'd1, 4'b0001);
        if (leds[7] !== 1'b1)
            $fatal(1, "activity LED did not assert");
        repeat (24) @(posedge clk);
        if (leds[7] !== 1'b0)
            $fatal(1, "activity LED did not expire");

        buttons = 6'b100101;
        switches = 4'b1010;
        repeat (4) @(posedge clk);
        expect_read(6'h04, 32'h00000a25);
        expect_read(6'h03, 32'h00000000);
        repeat (32) @(posedge clk);
        expect_read(6'h03, 32'h00000a25);
        repeat (2) @(posedge clk);
        expect_read(6'h05, 32'h00000a25);

        write_reg(6'h05, 32'h00000a25, 4'b1111);
        expect_read(6'h05, 32'h00000000);

        // A pulse shorter than the debounce interval is visible only as raw.
        buttons[1] = 1'b1;
        repeat (4) @(posedge clk);
        buttons[1] = 1'b0;
        repeat (32) @(posedge clk);
        expect_read(6'h03, 32'h00000a25);
        expect_read(6'h05, 32'h00000000);

        $display("PASS Astra front-panel MMIO, debounce, changes, and LEDs");
        $finish;
    end
endmodule

`default_nettype wire
