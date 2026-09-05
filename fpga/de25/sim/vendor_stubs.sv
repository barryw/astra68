module sys_pll(input wire refclk, input wire rst, output wire outclk_0,
               output wire locked);
    assign outclk_0 = refclk;
    assign locked = ~rst;
endmodule

module av_pll(input wire refclk, input wire rst, output wire outclk_0,
              output wire locked);
    assign outclk_0 = refclk;
    assign locked = ~rst;
endmodule

module I2C_HDMI_Config(
    input wire iCLK, input wire iRST_N, output wire I2C_SCLK,
    inout wire I2C_SDAT, input wire HDMI_TX_INT, output wire READY
);
    assign I2C_SCLK = 1'b1;
    assign I2C_SDAT = 1'bz;
    assign READY = iRST_N;
    wire unused = &{1'b0, iCLK, HDMI_TX_INT};
endmodule
