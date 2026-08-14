`timescale 1ns/1ps
`default_nettype none

module tb_astra_front_panel_axi;
    reg clk = 0;
    reg reset = 1;
    always #2.5 clk = ~clk;

    reg [5:0] buttons = 0;
    reg [3:0] switches = 0;
    reg [7:0] diagnostic_leds = 8'h07;
    wire [7:0] leds;
    reg [31:0] s_axi_awaddr = 0;
    reg [2:0] s_axi_awprot = 0;
    reg s_axi_awvalid = 0;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata = 0;
    reg [3:0] s_axi_wstrb = 0;
    reg s_axi_wvalid = 0;
    wire s_axi_wready;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready = 0;
    reg [31:0] s_axi_araddr = 0;
    reg [2:0] s_axi_arprot = 0;
    reg s_axi_arvalid = 0;
    wire s_axi_arready;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rvalid;
    reg s_axi_rready = 0;

    astra_front_panel_axi #(.CLK_HZ(2000)) dut (.*);

    task automatic write_reg(input [7:0] address, input [31:0] value,
                             input [1:0] response);
        begin
            @(negedge clk);
            s_axi_awaddr = {24'h43c070, address};
            s_axi_wdata = value;
            s_axi_wstrb = 4'hf;
            s_axi_awvalid = 1;
            s_axi_wvalid = 1;
            while (!s_axi_awready || !s_axi_wready) @(negedge clk);
            @(negedge clk);
            s_axi_awvalid = 0;
            s_axi_wvalid = 0;
            while (!s_axi_bvalid) @(negedge clk);
            if (s_axi_bresp != response)
                $fatal(1, "write %02x response=%b", address, s_axi_bresp);
            s_axi_bready = 1;
            @(negedge clk);
            s_axi_bready = 0;
        end
    endtask

    task automatic read_reg(input [7:0] address, input [31:0] value,
                            input [1:0] response);
        begin
            @(negedge clk);
            s_axi_araddr = {24'h43c070, address};
            s_axi_arvalid = 1;
            while (!s_axi_arready) @(negedge clk);
            @(negedge clk);
            s_axi_arvalid = 0;
            while (!s_axi_rvalid) @(negedge clk);
            if (s_axi_rdata != value || s_axi_rresp != response)
                $fatal(1, "read %02x data=%08x response=%b",
                       address, s_axi_rdata, s_axi_rresp);
            s_axi_rready = 1;
            @(negedge clk);
            s_axi_rready = 0;
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        reset = 0;
        read_reg(8'h00, 32'h504e4c30, 2'b00);
        read_reg(8'h08, 32'h0f020004, 2'b00);
        write_reg(8'h18, 32'h00000005, 2'b00);
        write_reg(8'h1c, 32'h0000000f, 2'b00);
        if (leds[3:0] != 4'h5)
            $fatal(1, "owned LEDs=%x", leds[3:0]);
        switches = 4'b0010;
        repeat (4) @(posedge clk);
        read_reg(8'h10, 32'h00000200, 2'b00);
        write_reg(8'h30, 32'd1, 2'b00);
        write_reg(8'h2c, 32'd1, 2'b00);
        if (!leds[3])
            $fatal(1, "activity LED did not assert");
        repeat (4) @(posedge clk);
        if (leds[3])
            $fatal(1, "activity LED did not expire");
        write_reg(8'h04, 32'd0, 2'b11);
        read_reg(8'h20, 32'd0, 2'b11);
        $display("ASTRA ARTY FRONT PANEL PASS");
        $finish;
    end
endmodule

`default_nettype wire
