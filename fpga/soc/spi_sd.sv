`timescale 1ns/1ps
`default_nettype none

// Byte-oriented SPI mode-0 master for the Vesta SD-card registers.
// SCLK = clk / (2 * (clkdiv + 1)); software owns chip select explicitly.
module spi_sd (
    input  wire       clk,
    input  wire       rst,
    input  wire       ctrl_we,
    input  wire [7:0] ctrl_wdata,
    input  wire       data_we,
    input  wire [7:0] data_wdata,
    output reg  [7:0] data_rdata,
    output reg        busy,
    output reg        cs_n,
    output reg  [3:0] clkdiv,
    output reg        sd_clk,
    output reg        sd_mosi,
    input  wire       sd_miso
);
    reg [3:0] divider_count;
    reg [2:0] bit_index;
    reg [7:0] tx_shift;
    reg [7:0] rx_shift;

    always @(posedge clk) begin
        if (rst) begin
            data_rdata <= 8'hff;
            busy <= 1'b0;
            cs_n <= 1'b1;
            clkdiv <= 4'hf;
            sd_clk <= 1'b0;
            sd_mosi <= 1'b1;
            divider_count <= 4'd0;
            bit_index <= 3'd0;
            tx_shift <= 8'hff;
            rx_shift <= 8'hff;
        end else begin
            if (ctrl_we && !busy) begin
                cs_n <= ctrl_wdata[0];
                clkdiv <= ctrl_wdata[7:4];
                sd_clk <= 1'b0;
                if (ctrl_wdata[0]) sd_mosi <= 1'b1;
            end

            if (data_we && !busy) begin
                busy <= 1'b1;
                sd_clk <= 1'b0;
                divider_count <= clkdiv;
                bit_index <= 3'd7;
                tx_shift <= data_wdata;
                rx_shift <= 8'd0;
                sd_mosi <= data_wdata[7];
            end else if (busy) begin
                if (divider_count != 0) begin
                    divider_count <= divider_count - 1'b1;
                end else begin
                    divider_count <= clkdiv;
                    if (!sd_clk) begin
                        sd_clk <= 1'b1;
                        rx_shift <= {rx_shift[6:0], sd_miso};
                    end else begin
                        sd_clk <= 1'b0;
                        if (bit_index == 0) begin
                            busy <= 1'b0;
                            data_rdata <= rx_shift;
                            sd_mosi <= 1'b1;
                        end else begin
                            bit_index <= bit_index - 1'b1;
                            tx_shift <= {tx_shift[6:0], 1'b1};
                            sd_mosi <= tx_shift[6];
                        end
                    end
                end
            end
        end
    end
endmodule

`default_nettype wire
