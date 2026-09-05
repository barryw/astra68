`timescale 1ns/1ps

module tb_astra_i2s_transmitter;
    reg mclk = 1'b0;
    reg reset = 1'b1;
    reg [1:0][23:0] sample_word = {24'h5a69c3, 24'ha5963c};
    wire bclk;
    wire sample_clk;
    wire lrclk;
    wire [3:0] i2s;
    integer bit_index;
    integer mclk_edges = 0;
    integer frame_mclk_edges;
    reg expected;

    always #5 mclk = ~mclk;
    always @(posedge mclk) mclk_edges = mclk_edges + 1;
    always @(i2s or lrclk) begin
        if (!reset && mclk !== 1'b0)
            $fatal(1, "I2S data changed outside the MCLK falling edge");
    end
    always @(sample_clk) begin
        if (!reset && mclk !== 1'b1)
            $fatal(1, "sample clock changed outside the MCLK rising edge");
    end

    astra_i2s_transmitter dut (
        .mclk(mclk),
        .reset(reset),
        .sample_word(sample_word),
        .bclk(bclk),
        .sample_clk(sample_clk),
        .lrclk(lrclk),
        .i2s(i2s)
    );

    initial begin
        repeat (4) @(posedge mclk);
        @(negedge mclk);
        reset = 1'b0;
        @(negedge lrclk);
        frame_mclk_edges = mclk_edges;
        for (bit_index = 0; bit_index < 64; bit_index = bit_index + 1) begin
            @(posedge bclk);
            if (bit_index == 0 || bit_index == 32)
                expected = 1'b0;
            else if (bit_index <= 24)
                expected = sample_word[0][24 - bit_index];
            else if (bit_index >= 33 && bit_index <= 56)
                expected = sample_word[1][56 - bit_index];
            else
                expected = 1'b0;
            if (i2s !== {4{expected}})
                $fatal(1, "I2S bit %0d mismatch: %b", bit_index, i2s);
            if (lrclk !== (bit_index >= 32))
                $fatal(1, "LRCLK bit %0d mismatch", bit_index);
            if (sample_clk !== lrclk)
                $fatal(1, "sample clock/LRCLK phase mismatch at bit %0d",
                       bit_index);
        end
        @(negedge lrclk);
        if (mclk_edges - frame_mclk_edges != 256)
            $fatal(1, "MCLK/LRCLK ratio mismatch: %0d",
                   mclk_edges - frame_mclk_edges);
        $display("ASTRA I2S 24-BIT STEREO PASS");
        $finish;
    end
endmodule
