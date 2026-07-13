// Behavioral clock source for SoC simulations that instantiate ecp5pll.
`timescale 1ns/1ps

module ecp5pll #(
    parameter integer in_hz = 25000000,
    parameter integer out0_hz = 25000000,
    parameter integer out0_deg = 0,
    parameter integer out0_tol_hz = 0,
    parameter integer out1_hz = 0,
    parameter integer out1_deg = 0,
    parameter integer out1_tol_hz = 0,
    parameter integer out2_hz = 0,
    parameter integer out2_deg = 0,
    parameter integer out2_tol_hz = 0,
    parameter integer out3_hz = 0,
    parameter integer out3_deg = 0,
    parameter integer out3_tol_hz = 0,
    parameter integer reset_en = 0,
    parameter integer standby_en = 0,
    parameter integer dynamic_en = 0
) (
    input  wire       clk_i,
    output reg  [3:0] clk_o = 4'd0,
    input  wire       reset,
    input  wire       standby,
    input  wire [1:0] phasesel,
    input  wire       phasedir,
    input  wire       phasestep,
    input  wire       phaseloadreg,
    output reg        locked = 1'b0
);
    if (out0_hz > 0) begin : g_clk0
        localparam realtime HALF_PERIOD = 500000000.0 / out0_hz;
        initial forever #(HALF_PERIOD) clk_o[0] = ~clk_o[0];
    end
    if (out1_hz > 0) begin : g_clk1
        localparam realtime HALF_PERIOD = 500000000.0 / out1_hz;
        initial forever #(HALF_PERIOD) clk_o[1] = ~clk_o[1];
    end
    if (out2_hz > 0) begin : g_clk2
        localparam realtime HALF_PERIOD = 500000000.0 / out2_hz;
        initial forever #(HALF_PERIOD) clk_o[2] = ~clk_o[2];
    end
    if (out3_hz > 0) begin : g_clk3
        localparam realtime HALF_PERIOD = 500000000.0 / out3_hz;
        initial forever #(HALF_PERIOD) clk_o[3] = ~clk_o[3];
    end

    initial begin
        repeat (8) @(posedge clk_i);
        locked = 1'b1;
    end
endmodule
