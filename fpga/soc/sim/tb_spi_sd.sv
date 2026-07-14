`timescale 1ns/1ps
`default_nettype none

module tb_spi_sd;
    reg clk = 1'b0;
    reg rst = 1'b1;
    reg ctrl_we = 1'b0;
    reg [7:0] ctrl_wdata = 8'hf1;
    reg data_we = 1'b0;
    reg [7:0] data_wdata = 8'hff;
    wire [7:0] data_rdata;
    wire busy;
    wire cs_n;
    wire [3:0] clkdiv;
    wire sd_clk;
    wire sd_mosi;
    reg sd_miso = 1'b1;

    reg [7:0] response = 8'h3c;
    reg [7:0] observed = 8'd0;
    integer response_bit = 7;
    integer observed_bits = 0;

    always #5 clk = ~clk;

    spi_sd dut (
        .clk(clk), .rst(rst),
        .ctrl_we(ctrl_we), .ctrl_wdata(ctrl_wdata),
        .data_we(data_we), .data_wdata(data_wdata),
        .data_rdata(data_rdata), .busy(busy),
        .cs_n(cs_n), .clkdiv(clkdiv),
        .sd_clk(sd_clk), .sd_mosi(sd_mosi), .sd_miso(sd_miso)
    );

    always @(negedge cs_n) begin
        response_bit = 7;
        sd_miso = response[7];
    end

    always @(posedge sd_clk) begin
        observed = {observed[6:0], sd_mosi};
        observed_bits = observed_bits + 1;
    end

    always @(negedge sd_clk) begin
        if (!cs_n && response_bit > 0) begin
            response_bit = response_bit - 1;
            sd_miso = response[response_bit];
        end
    end

    task write_ctrl(input [7:0] value);
        begin
            @(negedge clk);
            ctrl_wdata = value;
            ctrl_we = 1'b1;
            @(negedge clk);
            ctrl_we = 1'b0;
        end
    endtask

    task transfer(input [7:0] value);
        begin
            @(negedge clk);
            data_wdata = value;
            data_we = 1'b1;
            @(negedge clk);
            data_we = 1'b0;
            wait (busy == 1'b0);
            @(negedge clk);
        end
    endtask

    initial begin
        repeat (4) @(negedge clk);
        rst = 1'b0;
        repeat (2) @(negedge clk);

        if (!cs_n || clkdiv != 4'hf || sd_clk)
            $fatal(1, "invalid reset state");

        write_ctrl(8'h00); // selected, divide by two
        if (cs_n || clkdiv != 0)
            $fatal(1, "control register did not update");

        transfer(8'ha5);
        if (observed_bits != 8 || observed != 8'ha5)
            $fatal(1, "MOSI mismatch bits=%0d data=%02x", observed_bits, observed);
        if (data_rdata != 8'h3c)
            $fatal(1, "MISO mismatch data=%02x", data_rdata);
        if (sd_clk)
            $fatal(1, "clock did not return to mode-0 idle");

        write_ctrl(8'hf1);
        if (!cs_n || clkdiv != 4'hf || !sd_mosi)
            $fatal(1, "deselect state mismatch");

        $display("PASS spi_sd mode-0 byte transfer");
        $finish;
    end
endmodule

`default_nettype wire
