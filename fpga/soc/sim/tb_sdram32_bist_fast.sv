// End-to-end complementary POST gate over the pipelined SDRAM controller.
`timescale 1ns/1ps

module tb_sdram32_bist_fast;
    localparam integer TEST_BYTES = 262144;

    reg mem_clk = 1'b0;
    reg cpu_clk = 1'b0;
    reg rst = 1'b1;
    always #8.333 mem_clk = ~mem_clk; // 60 MHz
    always #40 cpu_clk = ~cpu_clk;    // 12.5 MHz

    wire dma_lock, dma_valid, dma_ready, dma_write;
    wire [24:0] dma_addr;
    wire [3:0] dma_be;
    wire [31:0] dma_wdata;
    wire dma_rsp_valid;
    wire [31:0] dma_rdata;

    reg cpu_start = 1'b0;
    wire cpu_busy, cpu_done;
    wire [2:0] cpu_phase;
    wire [24:0] cpu_progress;
    wire [31:0] cpu_errors;
    wire [24:0] cpu_first;
    wire [7:0] cpu_expected, cpu_actual;

    sdram32_bist #(.MEM_BYTES(TEST_BYTES)) bist (
        .cpu_clk(cpu_clk), .cpu_rst(rst), .cpu_start(cpu_start),
        .cpu_busy(cpu_busy), .cpu_done(cpu_done), .cpu_phase(cpu_phase),
        .cpu_progress(cpu_progress), .cpu_error_count(cpu_errors),
        .cpu_first_fail(cpu_first), .cpu_expected(cpu_expected),
        .cpu_actual(cpu_actual),
        .mem_clk(mem_clk), .mem_rst(rst), .mem_lock(dma_lock),
        .mem_valid(dma_valid), .mem_ready(dma_ready),
        .mem_write(dma_write), .mem_addr(dma_addr), .mem_be(dma_be),
        .mem_wdata(dma_wdata), .mem_rsp_valid(dma_rsp_valid),
        .mem_rdata(dma_rdata)
    );

    wire [15:0] sd_din, sd_dout;
    wire sd_doe, sd_clk, sd_cke, sd_cs, sd_ras, sd_cas, sd_we;
    wire [1:0] sd_dqm, sd_ba;
    wire [12:0] sd_addr;

    sdram32_controller controller (
        .clk(mem_clk), .rst(rst),
        .cpu_valid(1'b0), .cpu_ready(), .cpu_write(1'b0),
        .cpu_addr(25'd0), .cpu_be(4'd0), .cpu_wdata(32'd0),
        .cpu_lock(1'b0),
        .cpu_rsp_valid(), .cpu_rdata(),
        .video_lock(1'b0), .video_valid(1'b0), .video_ready(),
        .video_write(1'b0), .video_addr(25'd0), .video_be(4'd0),
        .video_wdata(32'd0), .video_rsp_valid(), .video_rdata(),
        .dma_lock(dma_lock), .dma_valid(dma_valid), .dma_ready(dma_ready),
        .dma_write(dma_write), .dma_addr(dma_addr), .dma_be(dma_be),
        .dma_wdata(dma_wdata), .dma_rsp_valid(dma_rsp_valid),
        .dma_rdata(dma_rdata),
        .sdram_data_in(sd_din), .sdram_data_out(sd_dout),
        .sdram_data_oe(sd_doe), .sdram_clk(sd_clk), .sdram_cke(sd_cke),
        .sdram_cs(sd_cs), .sdram_ras(sd_ras), .sdram_cas(sd_cas),
        .sdram_we(sd_we), .sdram_dqm(sd_dqm), .sdram_addr(sd_addr),
        .sdram_ba(sd_ba)
    );

    astra_sdram_model memory (
        .sdram_clk(sd_clk), .cke(sd_cke), .cs(sd_cs), .ras(sd_ras),
        .cas(sd_cas), .we(sd_we), .addr(sd_addr), .ba(sd_ba),
        .dqm(sd_dqm), .dq_in(sd_dout), .dq_out(sd_din)
    );

    integer active_cycles = 0;
    integer timeout_cycles = 0;
    real effective_mbps;
    real projected_full_seconds;
    always @(posedge mem_clk) begin
        if (dma_lock) active_cycles <= active_cycles + 1;
        timeout_cycles <= timeout_cycles + 1;
        if (timeout_cycles > 2000000)
            $fatal(1, "fast BIST timeout");
    end

    integer phase_count = 0;
    reg [2:0] last_phase = 3'd0;
    always @(posedge cpu_clk) begin
        if (cpu_phase != last_phase) begin
            if (cpu_phase == 3'd1 || cpu_phase == 3'd2)
                phase_count <= phase_count + 1;
            last_phase <= cpu_phase;
        end
    end

    initial begin
        repeat (5) @(posedge mem_clk);
        rst = 1'b0;
        repeat (8000) @(posedge mem_clk);

        @(negedge cpu_clk);
        cpu_start = 1'b1;
        @(negedge cpu_clk);
        cpu_start = 1'b0;

        wait (cpu_busy);
        wait (cpu_done && !cpu_busy);
        repeat (4) @(posedge cpu_clk);

        if (cpu_errors != 0 || cpu_phase != 3'd3)
            $fatal(1, "fast BIST failed errors=%0d phase=%0d first=%x exp=%02x got=%02x",
                   cpu_errors, cpu_phase, cpu_first, cpu_expected, cpu_actual);
        if (phase_count != 4)
            $fatal(1, "expected W/R/W/R phase sequence, saw %0d phases", phase_count);

        effective_mbps = (TEST_BYTES * 4.0 * 60.0) / active_cycles;
        projected_full_seconds = (33554432.0 * 4.0) /
                                 (effective_mbps * 1000000.0);
        $display("SDRAM32 BIST PASS effective=%0.2f MB/s projected-32MiB=%0.3f s cycles=%0d",
                 effective_mbps, projected_full_seconds, active_cycles);
        if (effective_mbps < 110.0 || projected_full_seconds > 1.5)
            $fatal(1, "fast BIST bandwidth target missed");
        $finish;
    end
endmodule
