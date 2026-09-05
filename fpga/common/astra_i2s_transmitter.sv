// Copyright (c) 2026 Astra68 contributors
//
// 24-bit stereo I2S for a 256fs master clock. Each sample occupies a 32-bit
// slot with the standard one-bit I2S delay; all four ADV7513 lanes carry the
// same stereo pair.
`timescale 1ns/1ps
`default_nettype none

module astra_i2s_transmitter (
    input  wire              mclk,
    input  wire              reset,
    input  wire [1:0][23:0]  sample_word,
    output reg               bclk,
    output reg               sample_clk,
    output reg               lrclk,
    output reg  [3:0]        i2s
);
    reg divider_q;
    reg [5:0] bit_index_q;
    reg [1:0][23:0] sample_q;
    reg next_lrclk_q;
    reg [3:0] next_i2s_q;
    reg data_bit;

    always @(*) begin
        if (bit_index_q == 6'd0 || bit_index_q == 6'd32)
            data_bit = 1'b0;
        else if (bit_index_q <= 6'd24)
            data_bit = sample_q[0][24 - bit_index_q];
        else if (bit_index_q >= 6'd33 && bit_index_q <= 6'd56)
            data_bit = sample_q[1][56 - bit_index_q];
        else
            data_bit = 1'b0;
    end

    always @(posedge mclk) begin
        if (reset) begin
            divider_q <= 1'b0;
            bit_index_q <= 6'd0;
            sample_q <= 48'd0;
            bclk <= 1'b0;
            sample_clk <= 1'b1;
            next_lrclk_q <= 1'b1;
            next_i2s_q <= 4'd0;
        end else begin
            divider_q <= ~divider_q;
            if (divider_q) begin
                bclk <= ~bclk;
                if (bclk) begin
                    if (bit_index_q == 6'd0)
                        sample_q <= sample_word;
                    sample_clk <= bit_index_q >= 6'd32;
                    next_lrclk_q <= bit_index_q >= 6'd32;
                    next_i2s_q <= {4{data_bit}};
                    bit_index_q <= bit_index_q + 1'b1;
                end
            end
        end
    end

    // Launch data between BCLK edges so every statically possible transition
    // satisfies the receiver window without a clock-enable exception.
    always @(negedge mclk) begin
        if (reset) begin
            lrclk <= 1'b1;
            i2s <= 4'd0;
        end else begin
            lrclk <= next_lrclk_q;
            i2s <= next_i2s_q;
        end
    end
endmodule

`default_nettype wire
