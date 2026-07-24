// Full TG030 boot-ROM gate with the native controller and pin-level SDRAM.
`timescale 1ns/1ps

module tb_boot_sdram #(
    parameter [31:0] BUILD_ID = 32'h00000000,
    parameter integer TEST_BYTES = 262144,
    parameter bit PROGRESS = 1'b0,
    parameter bit EXPECT_KERNEL_PANIC = 1'b0,
    parameter longint unsigned BOOT_TIMEOUT_NS =
        64'd500_000_000 + (TEST_BYTES * 64'd40_000)
);
    localparam [31:0] KERNEL_STATUS_READY = 32'h4b314f4b;
    localparam [31:0] KERNEL_STATUS_SOAK = 32'h4b31534b;
    localparam [31:0] KERNEL_STATUS_PANIC = 32'h4b50414e;
    localparam [31:0] EARLY_LOG_MAGIC = 32'h41364c47;
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
        .SDRAM_ENABLE(1'b1),
        .SDRAM_BIST_BYTES(TEST_BYTES),
        .SDRAM_READY_DELAY(10000),
        .HDMI_ENABLE(1'b0),
        .USB_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0),
        .UART_BAUD(12500000),
        .SOC_BUILD_ID(BUILD_ID)
    ) dut (
        .clk25_mhz(clk25), .reset_n(rstn),
        .buttons(6'd0), .switches(4'd0),
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
                         dut.g_sdram_enabled.astraea_i.blitter_i.busy_mem,
                         dut.g_sdram_enabled.astraea_i.blitter_i.state_mem,
                         dut.g_sdram_enabled.astraea_i.blitter_i.start_pending_cpu,
                         dut.g_sdram_enabled.astraea_i.blitter_i.start_sync_mem,
                         dut.g_sdram_enabled.astraea_i.blitter_i.start_seen_mem,
                         dut.g_sdram_enabled.blit_mem_lock,
                         dut.g_sdram_enabled.blit_mem_valid,
                         dut.g_sdram_enabled.blit_mem_ready,
                         dut.g_sdram_enabled.blit_mem_rsp_valid,
                         dut.g_sdram_enabled.astraea_i.blitter_i.issue_count_mem,
                         dut.g_sdram_enabled.astraea_i.blitter_i.chunk_count_mem,
                         dut.g_sdram_enabled.astraea_i.blitter_i.response_count_mem);
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
    reg front_panel_test_seen = 1'b0;
    reg astraea_test_seen = 1'b0;
    reg astraea_result_seen = 1'b0;
    reg post_seen = 1'b0;
    reg k3_quantum_seen = 1'b0;
    reg k3_deadline_seen = 1'b0;
    reg k3_performance_seen = 1'b0;
    reg k4_sync_seen = 1'b0;
    reg k4_marker_seen = 1'b0;
    reg expect_kernel_panic;
    reg expect_kernel_soak = 1'b0;
    reg expect_kernel_guard = 1'b0;
    reg guard_reason_seen = 1'b0;
    reg guard_fault_seen = 1'b0;
    reg [31:0] expected_guard_address = 32'd0;
    reg [31:0] parsed_fault_address = 32'd0;
    integer parsed_deadline_cycles = 0;
    integer parsed_deadline_budget = 0;
    integer parsed_deadline_overruns = 0;
    integer parsed_sync_events = 0;
    integer parsed_sync_semaphores = 0;
    integer parsed_sync_cancellations = 0;
    integer parsed_sync_close_wakeups = 0;
    integer parsed_sync_owner_deaths = 0;
    integer bist_cycles = 0;
    real bist_mbps;

    initial begin
        expect_kernel_panic = EXPECT_KERNEL_PANIC;
        if ($test$plusargs("expect-kernel-panic")) expect_kernel_panic = 1'b1;
        if ($test$plusargs("expect-kernel-soak")) expect_kernel_soak = 1'b1;
        if ($value$plusargs("expect-kernel-guard=%h", expected_guard_address)) begin
            expect_kernel_guard = 1'b1;
            expect_kernel_panic = 1'b1;
        end
        if (expect_kernel_panic && expect_kernel_soak)
            $fatal(1, "panic and soak expectations are mutually exclusive");
    end

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
                if (uart_line.len() >= 7 &&
                    uart_line.substr(0, 6) == "Built: ")
                    build_seen <= 1'b1;
                if (uart_line == "CPU:    TG68K.C 68030 MMU2 @ 12500000 Hz")
                    cpu_seen <= 1'b1;
                if (uart_line == "  Data/byte lanes .... OK")
                    lane_test_seen <= 1'b1;
                if (uart_line == "  Address lines ...... OK")
                    address_test_seen <= 1'b1;
                if (uart_line == "  Cache coherence .... OK")
                    cache_test_seen <= 1'b1;
                if (uart_line == "  Front panel ....... OK")
                    front_panel_test_seen <= 1'b1;
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
                        !address_test_seen || !cache_test_seen || !front_panel_test_seen ||
                        !astraea_test_seen || !astraea_result_seen)
                        $fatal(1, "POST passed without all prerequisite checks");
                    if (dut.sdram_bist_errors != 0)
                        $fatal(1, "POST passed with %0d BIST errors",
                               dut.sdram_bist_errors);
                    if (!$test$plusargs("allow-no-cache-check") &&
                        (dut.tg_icache_hits < 100 || dut.tg_dcache_hits < 100))
                        $fatal(1, "cache path not exercised I=%0d D=%0d",
                               dut.tg_icache_hits, dut.tg_dcache_hits);
                    bist_mbps = (TEST_BYTES * 4.0 * 60.0) / bist_cycles;
                    $display("BOOT SDRAM PASS BIST=%0.2f MB/s cycles=%0d I$=%0d/%0d D$=%0d",
                             bist_mbps, bist_cycles, dut.tg_icache_hits,
                             dut.tg_icache_misses, dut.tg_dcache_hits);
                    if (bist_mbps < 110.0)
                        $fatal(1, "integrated BIST bandwidth target missed");
                    post_seen <= 1'b1;
                end
                if ($sscanf(uart_line,
                            "K3 PERF deadline expire=%d/%d overruns=%d",
                            parsed_deadline_cycles,
                            parsed_deadline_budget,
                            parsed_deadline_overruns) == 3) begin
                    if (parsed_deadline_cycles <= 0 ||
                        parsed_deadline_cycles > 20000 ||
                        parsed_deadline_budget != 20000 ||
                        parsed_deadline_overruns != 0)
                        $fatal(1,
                               "K3 deadline performance invalid: %0d/%0d overruns=%0d",
                               parsed_deadline_cycles,
                               parsed_deadline_budget,
                               parsed_deadline_overruns);
                    k3_performance_seen <= 1'b1;
                end
                if (uart_line == "K3 ONE-SHOT SCHEDULER PASS")
                    k3_quantum_seen <= 1'b1;
                if (uart_line == "K3 DEADLINE QUEUE PASS")
                    k3_deadline_seen <= 1'b1;
                if ($sscanf(uart_line,
                            "Sync objects ........ %d event, %d sem; cancel/close/death %d/%d/%d",
                            parsed_sync_events,
                            parsed_sync_semaphores,
                            parsed_sync_cancellations,
                            parsed_sync_close_wakeups,
                            parsed_sync_owner_deaths) == 5) begin
                    if (parsed_sync_events < 4 ||
                        parsed_sync_semaphores != 1 ||
                        parsed_sync_cancellations != 1 ||
                        parsed_sync_close_wakeups != 1 ||
                        parsed_sync_owner_deaths < 1)
                        $fatal(1,
                               "K4 synchronization evidence invalid: event=%0d sem=%0d cancel/close/death=%0d/%0d/%0d",
                               parsed_sync_events,
                               parsed_sync_semaphores,
                               parsed_sync_cancellations,
                               parsed_sync_close_wakeups,
                               parsed_sync_owner_deaths);
                    if (!expect_kernel_soak &&
                        (parsed_sync_events != 4 ||
                         parsed_sync_owner_deaths != 1))
                        $fatal(1,
                               "K4 normal synchronization counts invalid: event=%0d death=%0d",
                               parsed_sync_events,
                               parsed_sync_owner_deaths);
                    k4_sync_seen <= 1'b1;
                end
                if (uart_line == "K4 HANDLE SYNCHRONIZATION PASS")
                    k4_marker_seen <= 1'b1;
                if (uart_line == "Reason: unhandled processor exception")
                    guard_reason_seen <= 1'b1;
                if ($sscanf(uart_line, "Fault:  0x%h", parsed_fault_address) == 1 &&
                    parsed_fault_address == expected_guard_address)
                    guard_fault_seen <= 1'b1;
                uart_line = "";
            end else begin
                uart_line = {uart_line, dut.uart_data};
            end
        end
    end

    function automatic [23:0] model_key(input [24:0] byte_offset);
        model_key = {byte_offset[9], byte_offset[11:10],
                     byte_offset[24:12], byte_offset[8:2], 1'b0};
    endfunction

    function automatic [31:0] sdram_be32(input [24:0] byte_offset);
        reg [23:0] key;
        begin
            key = model_key(byte_offset);
            sdram_be32 = {memory.memory[key][7:0],
                          memory.memory[key][15:8],
                          memory.memory[key + 1'b1][7:0],
                          memory.memory[key + 1'b1][15:8]};
        end
    endfunction

    always @(posedge dut.clk) begin
        if (post_seen && dut.sys_scratch == KERNEL_STATUS_PANIC &&
            !expect_kernel_panic) begin
            $display("VESTA TIMER0 load=%08x value=%08x ctrl=%08x status=%08x enable=%08x cfg=%08x pending=%08x active=%0d ipl_n=%b iack=%b/%b",
                     dut.vesta_irq_timer_i.timer_load[0],
                     dut.vesta_irq_timer_i.timer_value[0],
                     dut.vesta_irq_timer_i.timer_ctrl[0],
                     dut.vesta_irq_timer_i.timer_status[0],
                     dut.vesta_irq_timer_i.irq_enable,
                     dut.vesta_irq_timer_i.irq_cfg[0],
                     dut.vesta_irq_timer_i.pending_raw,
                     dut.vesta_irq_timer_i.active_level,
                     dut.vesta_irq_timer_i.cpu_ipln_n,
                     dut.vesta_irq_timer_i.iack_strobe,
                     dut.vesta_irq_timer_i.iack_valid);
            $fatal(1, "kernel panicked during normal boot, log_flags=%08x",
                   sdram_be32(25'h0000018));
        end
        if (post_seen &&
            dut.sys_scratch == (expect_kernel_panic ? KERNEL_STATUS_PANIC :
                                expect_kernel_soak ? KERNEL_STATUS_SOAK :
                                                     KERNEL_STATUS_READY)) begin
            if (sdram_be32(25'h0000000) != EARLY_LOG_MAGIC)
                $fatal(1, "early log header missing: %08x",
                       sdram_be32(25'h0000000));
            if (sdram_be32(25'h0000008) != 32'h00004000)
                $fatal(1, "early log size mismatch: %08x",
                       sdram_be32(25'h0000008));
            if (expect_kernel_panic &&
                (sdram_be32(25'h0000018) & 32'h1) == 0)
                $fatal(1, "panic did not mark early log");
            if (expect_kernel_guard &&
                (!guard_reason_seen || !guard_fault_seen))
                $fatal(1, "guard panic mismatch reason=%b fault=%b expected=%08x",
                       guard_reason_seen, guard_fault_seen,
                       expected_guard_address);
            if (!expect_kernel_panic && sdram_be32(25'h0000018) != 0)
                $fatal(1, "normal boot marked early log flags: %08x",
                       sdram_be32(25'h0000018));
            if (!expect_kernel_panic &&
                (!k3_quantum_seen || !k3_deadline_seen ||
                 !k3_performance_seen || !k4_sync_seen ||
                 !k4_marker_seen))
                $fatal(1,
                       "kernel ready without K3/K4 evidence quantum=%b deadline=%b performance=%b sync=%b marker=%b",
                       k3_quantum_seen, k3_deadline_seen,
                       k3_performance_seen, k4_sync_seen,
                       k4_marker_seen);
            $display("KERNEL %s PASS status=%08x log_write=%0d wraps=%0d",
                     expect_kernel_panic ? "PANIC" :
                     expect_kernel_soak ? "SOAK" : "ENTRY",
                     dut.sys_scratch, sdram_be32(25'h0000010),
                     sdram_be32(25'h0000014));
            $finish;
        end
    end

    initial begin
        #(BOOT_TIMEOUT_NS);
        $fatal(1, "boot SDRAM timeout pc=%08x adr=%08x dbg=%08x/%08x bus=%0d as=%b rw=%b dsack=%b ipl=%b BIST=%b/%b/%0d/%08x",
               dut.tg_dbg_status, dut.cpu_adr, dut.tg_dbg_imm,
               dut.tg_dbg_arin, dut.bs, dut.tg_as_n, dut.tg_rw_n,
               dut.cpu_dsack_n, dut.cpu_ipln, dut.sdram_bist_busy,
               dut.sdram_bist_done, dut.sdram_bist_phase,
               dut.sdram_bist_progress);
    end
endmodule
