// Full immutable-BRAM -> FAT32 SD -> SDRAM stage-2 boot test.
`timescale 1ns/1ps

module tb_sd_boot #(
    parameter [31:0] BUILD_ID = 32'h00000000,
    parameter integer TEST_BYTES = 65536,
    parameter bit PROGRESS = 1'b0
);
    reg clk25 = 1'b0;
    reg rstn = 1'b0;
    always #20 clk25 = ~clk25;

    wire tx;
    wire [7:0] leds;
    wire [3:0] gpdi;
    wire sd_clk;
    wire sd_cmd;
    tri [3:0] sd_d;
    wire sd_miso;
    wire wifi_en;
    wire wifi_gpio0;
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

    assign sd_d[0] = sd_miso;

    astra_soc #(
        .RST_MAX(16'd16),
        .SDRAM_ENABLE(1'b1),
        .SDRAM_BIST_BYTES(TEST_BYTES),
        .SDRAM_READY_DELAY(10000),
        .HDMI_ENABLE(1'b0),
        .USB_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0),
        .UART_BAUD(12500000),
        .SD_BOOT_ENABLE(1'b1),
        .ROM_WORDS(2048),
        .SOC_BUILD_ID(BUILD_ID)
    ) dut (
        .clk25_mhz(clk25), .reset_n(rstn),
        .buttons(6'd0), .switches(4'd0),
        .ftdi_rxd(tx), .ftdi_txd(1'b1), .leds(leds), .gpdi_dp(gpdi),
        .sd_clk(sd_clk), .sd_cmd(sd_cmd), .sd_d(sd_d),
        .wifi_en(wifi_en), .wifi_gpio0(wifi_gpio0),
        .sdram_clk(sdram_clk), .sdram_cke(sdram_cke),
        .sdram_csn(sdram_csn), .sdram_wen(sdram_wen),
        .sdram_rasn(sdram_rasn), .sdram_casn(sdram_casn),
        .sdram_ba(sdram_ba), .sdram_dqm(sdram_dqm),
        .sdram_a(sdram_a), .sdram_d(sdram_d)
    );

    sd_card_spi_model card (
        .cs_n(sd_d[3]), .sclk(sd_clk), .mosi(sd_cmd), .miso(sd_miso)
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
                #5_000_000;
                $display("PROGRESS t=%0t pc=%08x overlay=%b SD=%b/%b BIST=%0d/%08x",
                         $time, dut.cpu_adr, dut.rom_overlay_sdram,
                         sd_d[3], sd_clk, dut.sdram_bist_phase,
                         dut.sdram_bist_progress);
                $fflush();
            end
        end
    end

    string uart_line = "";
    reg stage0_seen = 1'b0;
    reg bist_seen = 1'b0;
    reg sd_seen = 1'b0;
    reg fat_seen = 1'b0;
    reg handoff_seen = 1'b0;
    reg stage2_seen = 1'b0;

    always @(posedge dut.clk) begin
        if (dut.uart_start) begin
            if (dut.uart_data == 8'h0d) begin
                // CR is part of the serial protocol, but not line matching.
            end else if (dut.uart_data == 8'h0a) begin
                $display("UART: %s", uart_line);
                $fflush();
                if (uart_line == "ASTRA 68 STAGE 0 v0.2") stage0_seen <= 1'b1;
                if (uart_line == "SDRAM full BIST ... OK") bist_seen <= 1'b1;
                if (uart_line == "SD card init ...... OK") sd_seen <= 1'b1;
                if (uart_line == "FAT /ASTRA68.ROM .. OK") fat_seen <= 1'b1;
                if (uart_line == "Starting system ROM") handoff_seen <= 1'b1;
                if (uart_line.len() > 21 &&
                    uart_line.substr(0, 20) == "ASTRA 68 SYSTEM ROM v")
                    stage2_seen <= 1'b1;
                if (uart_line.len() >= 6 && uart_line.substr(0, 5) == "FAILED")
                    $fatal(1, "stage 0 failed: %s", uart_line);
                if (uart_line == "POST FAIL" ||
                    uart_line == "HALTED: POST FAILURE")
                    $fatal(1, "stage 2 reported POST failure");
                if (uart_line == "POST PASS") begin
                    if (!stage0_seen || !bist_seen || !sd_seen || !fat_seen ||
                        !handoff_seen || !stage2_seen)
                        $fatal(1, "POST passed without complete SD boot sequence");
                    if (!dut.rom_overlay_sdram)
                        $fatal(1, "stage 2 executed without SDRAM ROM overlay");
                    if (dut.sdram_bist_errors != 0)
                        $fatal(1, "POST passed with %0d BIST errors",
                               dut.sdram_bist_errors);
                    if (wifi_en !== 1'b0 || wifi_gpio0 !== 1'b1)
                        $fatal(1, "ESP32 was not held out of the shared SD bus");
                    $display("SD BOOT PASS overlay=%b I$=%0d/%0d D$=%0d",
                             dut.rom_overlay_sdram, dut.tg_icache_hits,
                             dut.tg_icache_misses, dut.tg_dcache_hits);
                    $finish;
                end
                uart_line = "";
            end else begin
                uart_line = {uart_line, dut.uart_data};
            end
        end
    end

    initial begin
        #3_000_000_000;
        $fatal(1, "SD boot timeout pc=%08x overlay=%b", dut.cpu_adr,
               dut.rom_overlay_sdram);
    end
endmodule
