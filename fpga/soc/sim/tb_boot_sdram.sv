// Full TG030 boot-ROM gate with the native controller and pin-level SDRAM.
`timescale 1ns/1ps

module tb_boot_sdram #(
    parameter [31:0] BUILD_ID = 32'h00000000,
    parameter integer TEST_BYTES = 262144,
    parameter bit PROGRESS = 1'b0,
    parameter bit CPU_MMU2 = 1'b0
);
    reg clk25 = 1'b0;
    reg rstn = 1'b0;
    always #20 clk25 = ~clk25;

    wire tx;
    wire [7:0] leds;
    wire [3:0] gpdi;
    wire sdram_clk;
    wire sdram_cke;
    wire sdram_csn;
    wire sdram_wen;
    wire sdram_rasn;
    wire sdram_casn;
    wire [1:0] sdram_ba;
    wire [1:0] sdram_dqm;
    wire [12:0] sdram_a;
    wire [15:0] sdram_d;

    astra_soc #(
        .RST_MAX(16'd16),
        .CPU_TG68K(1'b1),
        .SDRAM_ENABLE(1'b1),
        .SDRAM_BIST_BYTES(TEST_BYTES),
        .SDRAM_READY_DELAY(10000),
        .HDMI_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0),
        .UART_BAUD(12500000),
        .CPU_MODEL(32'h00068030),
        .CPU_IMPLEMENTATION(CPU_MMU2 ? 32'h54474d32 : 32'h54473330),
        .CPU_FEATURES(32'h0000000d),
        .SOC_BUILD_ID(BUILD_ID)
    ) dut (
        .clk25_mhz(clk25), .reset_n(rstn),
        .ftdi_rxd(tx), .ftdi_txd(1'b1), .leds(leds), .gpdi_dp(gpdi),
        .sdram_clk(sdram_clk), .sdram_cke(sdram_cke),
        .sdram_csn(sdram_csn), .sdram_wen(sdram_wen),
        .sdram_rasn(sdram_rasn), .sdram_casn(sdram_casn),
        .sdram_ba(sdram_ba), .sdram_dqm(sdram_dqm),
        .sdram_a(sdram_a), .sdram_d(sdram_d)
    );

    wire [15:0] model_dq;
    wire model_dq_oe;
    assign sdram_d = model_dq_oe ? model_dq : 16'hzzzz;

    astra_sdram_model memory (
        .sdram_clk(sdram_clk), .cke(sdram_cke), .cs(sdram_csn),
        .ras(sdram_rasn), .cas(sdram_casn), .we(sdram_wen),
        .addr(sdram_a), .ba(sdram_ba), .dqm(sdram_dqm),
        .dq_in(sdram_d), .dq_out(model_dq), .dq_oe(model_dq_oe)
    );

    initial begin
        repeat (20) @(posedge clk25);
        rstn = 1'b1;
    end

    initial begin
        if (PROGRESS) begin
            forever begin
                #1_000_000;
                $display("PROGRESS t=%0t pc/bus=%08x SDRAM=%b/%b count=%0d PLL=%b/%b reset=%b/%b bus=%0d bridge=%b/%b/%0d req=%b/%b/%b native=%b/%b/%b BIST=%0d/%08x",
                         $time, dut.cpu_adr,
                         dut.g_sdram_enabled.sd_ready,
                         dut.sdram_ready_cpu,
                         dut.g_sdram_enabled.sd_boot_count,
                         dut.g_sdram_enabled.sd_pll_locked,
                         dut.g_sdram_enabled.sd_lock_sync,
                         rstn,
                         dut.g_sdram_enabled.sd_reset_sync,
                         dut.bs,
                         dut.sdram_bridge_busy,
                         dut.sdram_bridge_done,
                         dut.g_sdram_enabled.cpu_sdram_bridge.mem_state,
                         dut.g_sdram_enabled.cpu_sdram_bridge.req_toggle_cpu,
                         dut.g_sdram_enabled.cpu_sdram_bridge.req_sync_mem,
                         dut.g_sdram_enabled.cpu_sdram_bridge.req_seen_mem,
                         dut.g_sdram_enabled.cpu_mem_valid,
                         dut.g_sdram_enabled.cpu_mem_ready,
                         dut.g_sdram_enabled.cpu_mem_rsp_valid,
                         dut.sdram_bist_phase,
                         dut.sdram_bist_progress);
                $display("DMA owner=%0d busy=%b/%b state=%0d start=%b/%b/%b lock=%b native=%b/%b/%b count=%0d/%0d/%0d",
                         dut.g_sdram_enabled.dma_owner,
                         dut.astraea_busy,
                         dut.g_sdram_enabled.astraea_i.busy_mem,
                         dut.g_sdram_enabled.astraea_i.state_mem,
                         dut.g_sdram_enabled.astraea_i.start_pending_cpu,
                         dut.g_sdram_enabled.astraea_i.start_sync_mem,
                         dut.g_sdram_enabled.astraea_i.start_seen_mem,
                         dut.g_sdram_enabled.blit_mem_lock,
                         dut.g_sdram_enabled.blit_mem_valid,
                         dut.g_sdram_enabled.blit_mem_ready,
                         dut.g_sdram_enabled.blit_mem_rsp_valid,
                         dut.g_sdram_enabled.astraea_i.issue_count_mem,
                         dut.g_sdram_enabled.astraea_i.chunk_count_mem,
                         dut.g_sdram_enabled.astraea_i.response_count_mem);
                $fflush();
            end
        end
    end

    string uart_line = "";
    reg banner_seen = 1'b0;
    reg build_seen = 1'b0;
    reg cpu_seen = 1'b0;
    reg lane_test_seen = 1'b0;
    reg address_test_seen = 1'b0;
    reg cache_test_seen = 1'b0;
    reg astraea_test_seen = 1'b0;
    reg astraea_result_seen = 1'b0;
    integer bist_cycles = 0;
    real bist_mbps;

    always @(posedge dut.g_sdram_enabled.sd_domain_clk) begin
        if (dut.g_sdram_enabled.bist_mem_lock)
            bist_cycles <= bist_cycles + 1;
    end

    always @(posedge dut.clk) begin
        if (dut.uart_start) begin
            if (dut.uart_data == 8'h0d) begin
                // CR is part of the wire protocol but not the line comparison.
            end else if (dut.uart_data == 8'h0a) begin
                $display("UART: %s", uart_line);
                $fflush();
                if (uart_line.len() > 21 &&
                    uart_line.substr(0, 20) == "ASTRA 68 SYSTEM ROM v")
                    banner_seen <= 1'b1;
                if (uart_line.len() >= 74 &&
                    uart_line.substr(0, 6) == "Built: ")
                    build_seen <= 1'b1;
                if ((!CPU_MMU2 &&
                     uart_line == "CPU:    TG68K.C 68030 @ 12500000 Hz") ||
                    (CPU_MMU2 &&
                     uart_line == "CPU:    TG68K.C 68030 MMU2 @ 12500000 Hz"))
                    cpu_seen <= 1'b1;
                if (uart_line == "  Data/byte lanes .... OK")
                    lane_test_seen <= 1'b1;
                if (uart_line == "  Address lines ...... OK")
                    address_test_seen <= 1'b1;
                if (uart_line == "  Cache coherence .... OK")
                    cache_test_seen <= 1'b1;
                if (uart_line.len() >= 15 &&
                    uart_line.substr(0, 14) == "  Astraea DMA (")
                    astraea_test_seen <= 1'b1;
                if (uart_line.substr(0, 8) == "    fill=")
                    astraea_result_seen <= 1'b1;
                if (uart_line == "POST FAIL" ||
                    uart_line == "HALTED: POST FAILURE")
                    $fatal(1, "boot ROM reported POST failure");
                if (uart_line == "POST PASS") begin
                    if (!banner_seen || !build_seen || !cpu_seen || !lane_test_seen ||
                        !address_test_seen || !cache_test_seen ||
                        !astraea_test_seen || !astraea_result_seen)
                        $fatal(1, "POST passed without all prerequisite checks");
                    if (dut.sdram_bist_errors != 0)
                        $fatal(1, "POST passed with %0d BIST errors",
                               dut.sdram_bist_errors);
                    if (dut.tg_icache_hits < 100 || dut.tg_dcache_hits < 100)
                        $fatal(1, "cache path not exercised I=%0d D=%0d",
                               dut.tg_icache_hits, dut.tg_dcache_hits);
                    bist_mbps = (TEST_BYTES * 4.0 * 75.0) / bist_cycles;
                    $display("BOOT SDRAM PASS BIST=%0.2f MB/s cycles=%0d I$=%0d/%0d D$=%0d",
                             bist_mbps, bist_cycles, dut.tg_icache_hits,
                             dut.tg_icache_misses, dut.tg_dcache_hits);
                    if (bist_mbps < 120.0)
                        $fatal(1, "integrated BIST bandwidth target missed");
                    $finish;
                end
                uart_line = "";
            end else begin
                uart_line = {uart_line, dut.uart_data};
            end
        end
    end

    initial begin
        #2_000_000_000;
        $fatal(1, "boot SDRAM timeout adr=%08x phase=%0d progress=%08x",
               dut.cpu_adr, dut.sdram_bist_phase, dut.sdram_bist_progress);
    end
endmodule
