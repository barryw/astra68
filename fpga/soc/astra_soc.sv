// =============================================================================
// Astra 68 SoC: TG68K.C 68030/PMMU, boot ROM, BRAM scratch RAM, 32 MB SDRAM,
// and the initial Vesta machine-control blocks.
//
// Goal: boot astra_boot.bin from ROM and print the banner over the FTDI UART.
// The stack remains in BRAM during SDRAM bring-up. SDRAM is initially exposed
// through a high alias; the reset overlay is a separate milestone.
//
// 68030 bus: async, split DATA_IN (to CPU) / DATA_OUT (from CPU), no tristate.
// A cycle: ASn low -> decode ADR_OUT -> present read data / do write -> assert
// DSACKn=00 (32-bit port) -> CPU finishes, ASn high -> deassert.
// =============================================================================
`timescale 1ns/1ps
`default_nettype none

module astra_soc #(
    parameter [15:0] RST_MAX = 16'hFFFF,  // power-on reset length in CPU clocks (sim shortens it)
    parameter        UART_MONITOR = 1'b0, // diagnostic build: UART prints CPU bus state from hardware
    parameter        SDRAM_ENABLE = 1'b1, // hardware default; functional CPU sims disable it
    parameter integer SDRAM_BIST_BYTES = 33554432,
    parameter integer SDRAM_READY_DELAY = 1048575,
    parameter integer SDRAM_READ_LATENCY = 3,
    parameter        HDMI_ENABLE = 1'b1,  // 720x480 POST console on ULX3S GPDI
    parameter integer CPU_CLK_DIV_BIT = 2, // 0=12.5 MHz, 1=6.25 MHz, 2=3.125 MHz
    parameter integer UART_BAUD = 115200,
    parameter integer UART_RX_FIFO_DEPTH = 128,
    parameter        SD_BOOT_ENABLE = 1'b0,
    parameter        ASTRA_HOST_ENABLE = 1'b0,
    parameter integer ROM_WORDS = 65536,
    parameter [31:0] SOC_BUILD_ID = 32'h00000000
) (
    input  wire       clk25_mhz,
    input  wire       reset_n,     // btn[0] / BTN_PWRn, active low
    input  wire [5:0] buttons,     // FIRE1, FIRE2, UP, DOWN, LEFT, RIGHT
    input  wire [3:0] switches,    // SW1..SW4, active high
    output wire       ftdi_rxd,    // FPGA TX -> host
    input  wire       ftdi_txd,    // host -> FPGA RX
    output wire [7:0] leds,
    output wire [3:0] gpdi_dp,
    inout  wire       sd_clk,
    inout  wire       sd_cmd,
    inout  wire [3:0] sd_d,
    output wire       wifi_en,
    output wire       wifi_gpio0,
    output wire       sdram_clk,
    output wire       sdram_cke,
    output wire       sdram_csn,
    output wire       sdram_wen,
    output wire       sdram_rasn,
    output wire       sdram_casn,
    output wire [1:0] sdram_ba,
    output wire [1:0] sdram_dqm,
    output wire [12:0] sdram_a,
    inout  wire [15:0] sdram_d
`ifdef ASTRA_SOC_SIM_IRQ
    , input wire [2:0] sim_ipln
    , input wire       sim_avecn
    , input wire       sim_berrn
`endif
);
    localparam [31:0] CPU_MODEL = 32'h00068030;
    localparam [31:0] CPU_IMPLEMENTATION = 32'h54474d32; // "TGM2"
    localparam [31:0] CPU_FEATURES = 32'h0000000d;       // PMMU, DATA32, ADDR32

    // Production hardware leaves the shared SD bus under AstraHost's exclusive
    // control. The direct FPGA SPI path remains available as a recovery and
    // simulation backend when ASTRA_HOST_ENABLE is clear.
    assign wifi_en = ASTRA_HOST_ENABLE;
    assign wifi_gpio0 = 1'b1;
    // -------------------------------------------------------------------------
    // CPU/bus clock from a fabric divider. Hardware defaults to 3.125 MHz while
    // timing and stress images can select 6.25 or 12.5 MHz at synthesis time.
    reg [2:0] clkdiv = 3'd0;
    always @(posedge clk25_mhz) clkdiv <= clkdiv + 1'b1;
    wire clk = clkdiv[CPU_CLK_DIV_BIT];
    localparam integer CPU_CLK_HZ = 25000000 >> (CPU_CLK_DIV_BIT + 1);

    // Dedicated 27/135 MHz video clocks. This is the same PLL relationship as
    // NovaVM's hardware-proven ULX3S 720x480 HDMI path.
    wire video_pixel_clk;
    wire video_shift_clk;
    wire video_pll_locked;
    generate
        if (HDMI_ENABLE) begin : g_video_clocks
            wire [3:0] video_pll_o;
            ecp5pll #(
                .in_hz(25000000),
                .out0_hz(135000000),
                .out1_hz(27000000),
                .out2_hz(0),
                .out3_hz(0)
            ) video_pll (
                .clk_i(clk25_mhz),
                .clk_o(video_pll_o),
                .locked(video_pll_locked),
                .reset(1'b0),
                .standby(1'b0),
                .phasesel(2'b00),
                .phasedir(1'b0),
                .phasestep(1'b0),
                .phaseloadreg(1'b0)
            );
            assign video_shift_clk = video_pll_o[0];
            assign video_pixel_clk = video_pll_o[1];
        end else begin : g_video_clocks_disabled
            assign video_shift_clk = 1'b0;
            assign video_pixel_clk = 1'b0;
            assign video_pll_locked = 1'b0;
        end
    endgenerate

    // Power-on reset: button + a counter (no PLL to wait on).
    reg [15:0] rst_cnt = 16'd0;
    reg        rst = 1'b1;
    always @(posedge clk) begin
        if (!reset_n)                  begin rst_cnt <= 0; rst <= 1'b1; end
        else if (rst_cnt != RST_MAX)   rst_cnt <= rst_cnt + 1'b1;
        else                           rst <= 1'b0;
    end

    reg [63:0] cpu_cycle_count = 64'd0;
    always @(posedge clk) begin
        if (rst) cpu_cycle_count <= 64'd0;
        else cpu_cycle_count <= cpu_cycle_count + 64'd1;
    end

    reg [1:0] video_lock_sync_cpu = 2'b00;
    always @(posedge clk) begin
        if (rst) video_lock_sync_cpu <= 2'b00;
        else video_lock_sync_cpu <= {video_lock_sync_cpu[0], video_pll_locked};
    end
    wire video_ready_cpu = video_lock_sync_cpu[1];

    // -------------------------------------------------------------------------
    // 32 MB MT48LC16M16 SDRAM. A pipelined 32-bit controller runs at 75 MHz;
    // CPU transfers cross once per 68k bus cycle and POST uses the DMA port.
    // -------------------------------------------------------------------------
    reg         sdram_cpu_start;
    reg         sdram_bist_start;
    wire [24:0] sdram_cpu_addr;
    wire        sdram_bridge_busy;
    wire        sdram_bridge_done;
    wire [31:0] sdram_bridge_rdata;
    wire        sdram_ready_cpu;
    wire        sdram_bist_busy;
    wire        sdram_bist_done;
    wire [2:0]  sdram_bist_phase;
    wire [24:0] sdram_bist_progress;
    wire [31:0] sdram_bist_errors;
    wire [24:0] sdram_bist_first_fail;
    wire [7:0]  sdram_bist_expected;
    wire [7:0]  sdram_bist_actual;
    wire [31:0] astraea_rdata;
    wire        astraea_busy;
    wire        astraea_done;
    wire        astraea_irq;
    wire        astraea_cache_flush;
    wire [31:0] sdram_line_hits;
    wire [31:0] sdram_line_misses;
    wire [31:0] sdram_posted_writes;
    wire        sdram_fill_valid;
    wire [24:0] sdram_fill_addr;
    wire [127:0] sdram_fill_data;
    wire        sdram_fill_instruction;
    reg         host_boot_request_cpu = 1'b0;
    wire        host_seen_mem;
    wire        host_boot_busy_mem;
    wire        host_boot_done_mem;
    wire        host_boot_error_mem;
    wire [7:0]  host_error_code_mem;
    wire [31:0] host_payload_size_mem;
    wire [31:0] host_payload_crc_mem;
    wire [31:0] host_initial_sp_mem;
    wire [31:0] host_initial_pc_mem;
    wire [31:0] host_bytes_received_mem;
    wire        host_spi_miso;
    wire        host_spi_miso_oe;
    wire        sd_clk_in;
    wire        sd_cmd_in;
    wire [3:0]  sd_d_in;

    generate
        if (SDRAM_ENABLE) begin : g_sdram_enabled
            wire [3:0] sd_pll_o;
            wire       sd_pll_locked;
`ifdef VERILATOR
            reg        sd_domain_clk = 1'b0;
            always #6.666 sd_domain_clk = ~sd_domain_clk;
`else
            wire       sd_domain_clk;
            assign sd_domain_clk = sd_pll_o[0];
`endif

            ecp5pll #(
                .in_hz(25000000),
                .out0_hz(75000000),
                .out1_hz(0),
                .out2_hz(0),
                .out3_hz(0)
            ) sdram_pll (
                .clk_i(clk25_mhz),
                .clk_o(sd_pll_o),
                .locked(sd_pll_locked),
                .reset(1'b0),
                .standby(1'b0),
                .phasesel(2'b00),
                .phasedir(1'b0),
                .phasestep(1'b0),
                .phaseloadreg(1'b0)
            );

            reg [1:0] sd_lock_sync = 2'b00;
            reg [1:0] sd_reset_sync = 2'b11;
            always @(posedge sd_domain_clk) begin
                sd_lock_sync <= {sd_lock_sync[0], sd_pll_locked};
                sd_reset_sync <= {sd_reset_sync[0], reset_n};
            end
            wire sd_locked = sd_lock_sync[1];
            wire sd_manual_reset = !sd_reset_sync[1];

            reg [19:0] sd_boot_count = 20'd0;
            reg        sd_ready = 1'b0;
            always @(posedge sd_domain_clk) begin
                if (!sd_locked || sd_manual_reset) begin
                    sd_boot_count <= 20'd0;
                    sd_ready <= 1'b0;
                end else if (!sd_ready) begin
                    if (sd_boot_count >= SDRAM_READY_DELAY) sd_ready <= 1'b1;
                    else sd_boot_count <= sd_boot_count + 20'd1;
                end
            end
            reg [1:0] sd_ready_sync_cpu = 2'b00;
            always @(posedge clk) begin
                if (rst) sd_ready_sync_cpu <= 2'b00;
                else sd_ready_sync_cpu <= {sd_ready_sync_cpu[0], sd_ready};
            end
            assign sdram_ready_cpu = sd_ready_sync_cpu[1];

            wire [15:0] sd_data_out;
            wire        sd_data_oe;
            wire [12:0] sd_addr;
            wire [1:0]  sd_mask;
            wire [1:0]  sd_bank;
            wire        sd_cke;
            wire        sd_cs;
            wire        sd_we;
            wire        sd_ras;
            wire        sd_cas;

            wire        cpu_mem_valid;
            wire        cpu_mem_ready;
            wire        cpu_mem_lock;
            wire        cpu_mem_write;
            wire [24:0] cpu_mem_addr;
            wire [3:0]  cpu_mem_be;
            wire [31:0] cpu_mem_wdata;
            wire        cpu_mem_rsp_valid;
            wire [31:0] cpu_mem_rdata;

            wire        bist_mem_lock;
            wire        bist_mem_valid;
            wire        bist_mem_ready;
            wire        bist_mem_write;
            wire [24:0] bist_mem_addr;
            wire [3:0]  bist_mem_be;
            wire [31:0] bist_mem_wdata;
            wire        bist_mem_rsp_valid;
            wire [31:0] bist_mem_rdata;

            wire        blit_mem_lock;
            wire        blit_mem_valid;
            wire        blit_mem_ready;
            wire        blit_mem_write;
            wire [24:0] blit_mem_addr;
            wire [3:0]  blit_mem_be;
            wire [31:0] blit_mem_wdata;
            wire        blit_mem_rsp_valid;
            wire [31:0] blit_mem_rdata;

            wire        host_mem_lock;
            wire        host_mem_valid;
            wire        host_mem_ready;
            wire        host_mem_write;
            wire [24:0] host_mem_addr;
            wire [3:0]  host_mem_be;
            wire [31:0] host_mem_wdata;
            wire        host_mem_rsp_valid;

            reg [1:0] host_request_sync_mem = 2'b00;
            always @(posedge sd_domain_clk) begin
                if (!sd_ready)
                    host_request_sync_mem <= 2'b00;
                else
                    host_request_sync_mem <= {host_request_sync_mem[0],
                                              host_boot_request_cpu};
            end

            if (ASTRA_HOST_ENABLE) begin : g_astra_host
                    wire [7:0] host_rx_data;
                    wire host_rx_valid;
                    wire host_rx_ready;
                    wire [7:0] host_tx_data;
                    wire host_tx_start;
                    wire host_tx_busy;

                    astra_host_spi_slave host_spi_i (
                        .clk(sd_domain_clk), .rst(!sd_ready),
                        .spi_sck(sd_clk_in), .spi_cs_n(sd_d_in[1]),
                        .spi_mosi(sd_cmd_in), .spi_miso(host_spi_miso),
                        .spi_miso_oe(host_spi_miso_oe),
                        .rx_data(host_rx_data), .rx_valid(host_rx_valid),
                        .rx_ready(host_rx_ready), .tx_data(host_tx_data),
                        .tx_start(host_tx_start), .tx_busy(host_tx_busy),
                        .rx_overflow(), .tx_overflow(), .tx_underflow(),
                        .selected_seen()
                    );

                    astra_host_boot host_boot_i (
                        .clk(sd_domain_clk), .rst(!sd_ready),
                        .rx_data(host_rx_data), .rx_valid(host_rx_valid),
                        .rx_ready(host_rx_ready), .tx_data(host_tx_data),
                        .tx_start(host_tx_start), .tx_busy(host_tx_busy),
                        .boot_request(host_request_sync_mem[1]),
                        .host_seen(host_seen_mem),
                        .boot_busy(host_boot_busy_mem),
                        .boot_done(host_boot_done_mem),
                        .boot_error(host_boot_error_mem),
                        .error_code(host_error_code_mem),
                        .payload_size(host_payload_size_mem),
                        .payload_crc32(host_payload_crc_mem),
                        .initial_sp(host_initial_sp_mem),
                        .initial_pc(host_initial_pc_mem),
                        .bytes_received(host_bytes_received_mem),
                        .mem_lock(host_mem_lock), .mem_valid(host_mem_valid),
                        .mem_ready(host_mem_ready), .mem_write(host_mem_write),
                        .mem_addr(host_mem_addr), .mem_be(host_mem_be),
                        .mem_wdata(host_mem_wdata),
                        .mem_rsp_valid(host_mem_rsp_valid)
                    );
            end else begin : g_astra_host_disabled
                    assign host_seen_mem = 1'b0;
                    assign host_boot_busy_mem = 1'b0;
                    assign host_boot_done_mem = 1'b0;
                    assign host_boot_error_mem = 1'b0;
                    assign host_error_code_mem = 8'd0;
                    assign host_payload_size_mem = 32'd0;
                    assign host_payload_crc_mem = 32'd0;
                    assign host_initial_sp_mem = 32'd0;
                    assign host_initial_pc_mem = 32'd0;
                    assign host_bytes_received_mem = 32'd0;
                    assign host_spi_miso = 1'b1;
                    assign host_spi_miso_oe = 1'b0;
                    assign host_mem_lock = 1'b0;
                    assign host_mem_valid = 1'b0;
                    assign host_mem_write = 1'b0;
                    assign host_mem_addr = 25'd0;
                    assign host_mem_be = 4'd0;
                    assign host_mem_wdata = 32'd0;
            end

            localparam [1:0] DMA_OWNER_NONE = 2'd0;
            localparam [1:0] DMA_OWNER_BIST = 2'd1;
            localparam [1:0] DMA_OWNER_BLIT = 2'd2;
            localparam [1:0] DMA_OWNER_HOST = 2'd3;
            reg [1:0] dma_owner = DMA_OWNER_NONE;

            // Hold one DMA owner for the complete transaction stream. Besides
            // making arbitration deterministic, this breaks the long path from
            // one engine's lock signal through the other engine's response mux.
            always @(posedge sd_domain_clk) begin
                if (!sd_locked || sd_manual_reset) begin
                    dma_owner <= DMA_OWNER_NONE;
                end else begin
                    case (dma_owner)
                        DMA_OWNER_NONE: begin
                            if (bist_mem_lock)
                                dma_owner <= DMA_OWNER_BIST;
                            else if (host_mem_lock)
                                dma_owner <= DMA_OWNER_HOST;
                            else if (blit_mem_lock)
                                dma_owner <= DMA_OWNER_BLIT;
                        end
                        DMA_OWNER_BIST: begin
                            if (!bist_mem_lock)
                                dma_owner <= DMA_OWNER_NONE;
                        end
                        DMA_OWNER_BLIT: begin
                            if (!blit_mem_lock)
                                dma_owner <= DMA_OWNER_NONE;
                        end
                        DMA_OWNER_HOST: begin
                            if (!host_mem_lock)
                                dma_owner <= DMA_OWNER_NONE;
                        end
                        default: dma_owner <= DMA_OWNER_NONE;
                    endcase
                end
            end

            wire        dma_use_bist = dma_owner == DMA_OWNER_BIST;
            wire        dma_use_blit = dma_owner == DMA_OWNER_BLIT;
            wire        dma_use_host = dma_owner == DMA_OWNER_HOST;
            wire        dma_mem_lock = dma_owner != DMA_OWNER_NONE;
            wire        dma_mem_valid = dma_use_bist ? bist_mem_valid :
                                        dma_use_host ? host_mem_valid :
                                            dma_use_blit ? blit_mem_valid : 1'b0;
            wire        dma_mem_ready;
            wire        dma_mem_write = dma_use_bist ? bist_mem_write :
                                        dma_use_host ? host_mem_write :
                                            dma_use_blit ? blit_mem_write : 1'b0;
            wire [24:0] dma_mem_addr = dma_use_bist ? bist_mem_addr :
                                       dma_use_host ? host_mem_addr :
                                           dma_use_blit ? blit_mem_addr : 25'd0;
            wire [3:0]  dma_mem_be = dma_use_bist ? bist_mem_be :
                                      dma_use_host ? host_mem_be :
                                         dma_use_blit ? blit_mem_be : 4'd0;
            wire [31:0] dma_mem_wdata = dma_use_bist ? bist_mem_wdata :
                                         dma_use_host ? host_mem_wdata :
                                            dma_use_blit ? blit_mem_wdata : 32'd0;
            wire        dma_mem_rsp_valid;
            wire [31:0] dma_mem_rdata;

            assign bist_mem_ready = dma_use_bist && dma_mem_ready;
            assign bist_mem_rsp_valid = dma_use_bist && dma_mem_rsp_valid;
            assign bist_mem_rdata = dma_mem_rdata;
            assign blit_mem_ready = dma_use_blit && dma_mem_ready;
            assign blit_mem_rsp_valid = dma_use_blit && dma_mem_rsp_valid;
            assign blit_mem_rdata = dma_mem_rdata;
            assign host_mem_ready = dma_use_host && dma_mem_ready;
            assign host_mem_rsp_valid = dma_use_host && dma_mem_rsp_valid;

            assign sdram_cke = sd_cke;
            assign sdram_csn = sd_cs;
            assign sdram_wen = sd_we;
            assign sdram_rasn = sd_ras;
            assign sdram_casn = sd_cas;
            assign sdram_ba = sd_bank;
            assign sdram_dqm = sd_mask;
            assign sdram_a = sd_addr;
            assign sdram_d = sd_data_oe ? sd_data_out : 16'hzzzz;

            sdram32_controller #(
                .SDRAM_READ_LATENCY(SDRAM_READ_LATENCY)
            ) sdram_i (
                .clk(sd_domain_clk),
                .rst(!sd_locked || sd_manual_reset),
                .cpu_valid(cpu_mem_valid),
                .cpu_ready(cpu_mem_ready),
                .cpu_write(cpu_mem_write),
                .cpu_addr(cpu_mem_addr),
                .cpu_be(cpu_mem_be),
                .cpu_wdata(cpu_mem_wdata),
                .cpu_lock(cpu_mem_lock),
                .cpu_rsp_valid(cpu_mem_rsp_valid),
                .cpu_rdata(cpu_mem_rdata),
                .dma_lock(dma_mem_lock),
                .dma_valid(dma_mem_valid),
                .dma_ready(dma_mem_ready),
                .dma_write(dma_mem_write),
                .dma_addr(dma_mem_addr),
                .dma_be(dma_mem_be),
                .dma_wdata(dma_mem_wdata),
                .dma_rsp_valid(dma_mem_rsp_valid),
                .dma_rdata(dma_mem_rdata),
                .sdram_data_in(sdram_d),
                .sdram_data_out(sd_data_out),
                .sdram_data_oe(sd_data_oe),
                .sdram_clk(sdram_clk),
                .sdram_cke(sd_cke),
                .sdram_cs(sd_cs),
                .sdram_ras(sd_ras),
                .sdram_cas(sd_cas),
                .sdram_we(sd_we),
                .sdram_dqm(sd_mask),
                .sdram_addr(sd_addr),
                .sdram_ba(sd_bank)
            );

            sdram32_cpu_bridge cpu_sdram_bridge (
                .cpu_clk(clk),
                .cpu_rst(rst || !sdram_ready_cpu),
                .cpu_start(sdram_cpu_start),
                .cpu_addr(sdram_cpu_addr),
                .cpu_write(!cpu_rw_n),
                .cpu_be(be),
                .cpu_wdata(cpu_dout),
                .cpu_lock(!cpu_rmc_n),
                .cpu_cacheable(tg_cacheable_out),
                .cpu_instruction(cpu_fc[1] && cpu_fc != 3'b111),
                .cpu_postable(tg_postable_out),
                .cpu_cache_flush(sdram_bist_busy | astraea_cache_flush),
                .cpu_busy(sdram_bridge_busy),
                .cpu_done(sdram_bridge_done),
                .cpu_rdata(sdram_bridge_rdata),
                .cpu_line_hits(sdram_line_hits),
                .cpu_line_misses(sdram_line_misses),
                .cpu_posted_writes(sdram_posted_writes),
                .cpu_fill_valid(sdram_fill_valid),
                .cpu_fill_addr(sdram_fill_addr),
                .cpu_fill_data(sdram_fill_data),
                .cpu_fill_instruction(sdram_fill_instruction),
                .mem_clk(sd_domain_clk),
                .mem_rst(!sd_ready),
                .mem_valid(cpu_mem_valid),
                .mem_ready(cpu_mem_ready),
                .mem_write(cpu_mem_write),
                .mem_addr(cpu_mem_addr),
                .mem_be(cpu_mem_be),
                .mem_wdata(cpu_mem_wdata),
                .mem_lock(cpu_mem_lock),
                .mem_rsp_valid(cpu_mem_rsp_valid),
                .mem_rdata(cpu_mem_rdata)
            );

            sdram32_bist #(
                .MEM_BYTES(SDRAM_BIST_BYTES)
            ) sdram_bist_i (
                .cpu_clk(clk),
                .cpu_rst(rst || !sdram_ready_cpu),
                .cpu_start(sdram_bist_start),
                .cpu_busy(sdram_bist_busy),
                .cpu_done(sdram_bist_done),
                .cpu_phase(sdram_bist_phase),
                .cpu_progress(sdram_bist_progress),
                .cpu_error_count(sdram_bist_errors),
                .cpu_first_fail(sdram_bist_first_fail),
                .cpu_expected(sdram_bist_expected),
                .cpu_actual(sdram_bist_actual),
                .mem_clk(sd_domain_clk),
                .mem_rst(!sd_ready),
                .mem_lock(bist_mem_lock),
                .mem_valid(bist_mem_valid),
                .mem_ready(bist_mem_ready),
                .mem_write(bist_mem_write),
                .mem_addr(bist_mem_addr),
                .mem_be(bist_mem_be),
                .mem_wdata(bist_mem_wdata),
                .mem_rsp_valid(bist_mem_rsp_valid),
                .mem_rdata(bist_mem_rdata)
            );

            astraea_blitter astraea_i (
                .cpu_clk(clk),
                .cpu_rst(rst || !sdram_ready_cpu),
                .cpu_write_stb(astraea_write_stb),
                .cpu_reg(cpu_adr[6:2]),
                .cpu_be(be),
                .cpu_wdata(cpu_dout),
                .cpu_rdata(astraea_rdata),
                .cpu_busy(astraea_busy),
                .cpu_done(astraea_done),
                .cpu_irq(astraea_irq),
                .cache_flush(astraea_cache_flush),
                .mem_clk(sd_domain_clk),
                .mem_rst(!sd_ready),
                .mem_lock(blit_mem_lock),
                .mem_valid(blit_mem_valid),
                .mem_ready(blit_mem_ready),
                .mem_write(blit_mem_write),
                .mem_addr(blit_mem_addr),
                .mem_be(blit_mem_be),
                .mem_wdata(blit_mem_wdata),
                .mem_rsp_valid(blit_mem_rsp_valid),
                .mem_rdata(blit_mem_rdata)
            );
        end else begin : g_sdram_disabled
            assign sdram_clk = 1'b0;
            assign sdram_cke = 1'b0;
            assign sdram_csn = 1'b1;
            assign sdram_wen = 1'b1;
            assign sdram_rasn = 1'b1;
            assign sdram_casn = 1'b1;
            assign sdram_ba = 2'b00;
            assign sdram_dqm = 2'b11;
            assign sdram_a = 13'd0;
            assign sdram_d = 16'hzzzz;
            assign sdram_bridge_busy = 1'b0;
            assign sdram_bridge_done = 1'b0;
            assign sdram_bridge_rdata = 32'd0;
            assign sdram_ready_cpu = 1'b0;
            assign sdram_bist_busy = 1'b0;
            assign sdram_bist_done = 1'b0;
            assign sdram_bist_phase = 3'd0;
            assign sdram_bist_progress = 25'd0;
            assign sdram_bist_errors = 32'd0;
            assign sdram_bist_first_fail = 25'd0;
            assign sdram_bist_expected = 8'd0;
            assign sdram_bist_actual = 8'd0;
            assign astraea_rdata = 32'd0;
            assign astraea_busy = 1'b0;
            assign astraea_done = 1'b0;
            assign astraea_irq = 1'b0;
            assign astraea_cache_flush = 1'b0;
            assign sdram_line_hits = 32'd0;
            assign sdram_line_misses = 32'd0;
            assign sdram_posted_writes = 32'd0;
            assign sdram_fill_valid = 1'b0;
            assign sdram_fill_addr = 25'd0;
            assign sdram_fill_data = 128'd0;
            assign sdram_fill_instruction = 1'b0;
            assign host_seen_mem = 1'b0;
            assign host_boot_busy_mem = 1'b0;
            assign host_boot_done_mem = 1'b0;
            assign host_boot_error_mem = 1'b0;
            assign host_error_code_mem = 8'd0;
            assign host_payload_size_mem = 32'd0;
            assign host_payload_crc_mem = 32'd0;
            assign host_initial_sp_mem = 32'd0;
            assign host_initial_pc_mem = 32'd0;
            assign host_bytes_received_mem = 32'd0;
            assign host_spi_miso = 1'b1;
            assign host_spi_miso_oe = 1'b0;
        end
    endgenerate

    reg [1:0] host_seen_sync_cpu = 2'b00;
    reg [1:0] host_busy_sync_cpu = 2'b00;
    reg [1:0] host_done_sync_cpu = 2'b00;
    reg [1:0] host_error_sync_cpu = 2'b00;
    reg [7:0] host_error_meta_cpu = 8'd0;
    reg [7:0] host_error_cpu = 8'd0;
    reg [31:0] host_size_meta_cpu = 32'd0;
    reg [31:0] host_size_cpu = 32'd0;
    reg [31:0] host_crc_meta_cpu = 32'd0;
    reg [31:0] host_crc_cpu = 32'd0;
    reg [31:0] host_sp_meta_cpu = 32'd0;
    reg [31:0] host_sp_cpu = 32'd0;
    reg [31:0] host_pc_meta_cpu = 32'd0;
    reg [31:0] host_pc_cpu = 32'd0;
    reg [31:0] host_bytes_meta_cpu = 32'd0;
    reg [31:0] host_bytes_cpu = 32'd0;
    always @(posedge clk) begin
        if (rst) begin
            host_seen_sync_cpu <= 2'b00;
            host_busy_sync_cpu <= 2'b00;
            host_done_sync_cpu <= 2'b00;
            host_error_sync_cpu <= 2'b00;
            host_error_meta_cpu <= 8'd0;
            host_error_cpu <= 8'd0;
            host_size_meta_cpu <= 32'd0;
            host_size_cpu <= 32'd0;
            host_crc_meta_cpu <= 32'd0;
            host_crc_cpu <= 32'd0;
            host_sp_meta_cpu <= 32'd0;
            host_sp_cpu <= 32'd0;
            host_pc_meta_cpu <= 32'd0;
            host_pc_cpu <= 32'd0;
            host_bytes_meta_cpu <= 32'd0;
            host_bytes_cpu <= 32'd0;
        end else begin
            host_seen_sync_cpu <= {host_seen_sync_cpu[0], host_seen_mem};
            host_busy_sync_cpu <= {host_busy_sync_cpu[0], host_boot_busy_mem};
            host_done_sync_cpu <= {host_done_sync_cpu[0], host_boot_done_mem};
            host_error_sync_cpu <= {host_error_sync_cpu[0], host_boot_error_mem};
            host_error_meta_cpu <= host_error_code_mem;
            host_error_cpu <= host_error_meta_cpu;
            host_size_meta_cpu <= host_payload_size_mem;
            host_size_cpu <= host_size_meta_cpu;
            host_crc_meta_cpu <= host_payload_crc_mem;
            host_crc_cpu <= host_crc_meta_cpu;
            host_sp_meta_cpu <= host_initial_sp_mem;
            host_sp_cpu <= host_sp_meta_cpu;
            host_pc_meta_cpu <= host_initial_pc_mem;
            host_pc_cpu <= host_pc_meta_cpu;
            host_bytes_meta_cpu <= host_bytes_received_mem;
            host_bytes_cpu <= host_bytes_meta_cpu;
        end
    end

    // -------------------------------------------------------------------------
    // CPU signals
    // -------------------------------------------------------------------------
    wire [31:0] cpu_adr;
    wire [31:0] cpu_dout;      // data FROM cpu (writes)
    reg  [31:0] cpu_din;       // data TO cpu (reads)
    wire [31:0] cpu_din_visible;
    wire        cpu_data_en;   // cpu driving data (write)
    wire        cpu_as_n, cpu_ds_n, cpu_rw_n;
    wire        cpu_rmc_n;
    wire [1:0]  cpu_siz;
    wire [2:0]  cpu_fc;
    wire [31:0] tg_adr;
    wire [31:0] tg_dout;
    wire        tg_data_en;
    wire        tg_as_n, tg_ds_n, tg_rw_n;
    wire        tg_rmc_n;
    wire [1:0]  tg_siz;
    wire [2:0]  tg_fc;
    wire [31:0] tg_dbg_status;
    wire [31:0] tg_dbg_imm;
    wire [31:0] tg_dbg_arin;
    wire [31:0] tg_icache_hits;
    wire [31:0] tg_icache_misses;
    wire [31:0] tg_dcache_hits;
    wire [31:0] tg_dcache_misses;
    wire tg_cacheable_out;
    wire tg_postable_out;
    wire        tg_cache_ihit;
    wire [31:0] tg_cache_idata;
    wire        tg_cache_dhit;
    wire [31:0] tg_cache_ddata;
    wire [31:0] tg_cache_lookup_addr;
    wire        tg_cache_lookup_insn;
    wire        tg_cache_lookup_data;
    wire        tg_cache_store_valid;
    wire [31:0] tg_cache_store_addr;
    wire [31:0] tg_cache_store_data;
    wire        tg_cache_store_insn;
    wire        tg_cache_invalidate_valid;
    wire [31:0] tg_cache_invalidate_addr;
    wire        tg_cache_invalidate_all;
    wire        tg_cache_ifreeze;
    wire        tg_cache_dfreeze;
    wire tg_cacheable = (tg_adr[31:18] == 14'h3ff8) ||
                        (tg_adr[31:18] == 14'd0) ||
                        (SDRAM_ENABLE && tg_adr[31:25] == 7'b0000001);
    reg  [1:0]  dsack_n = 2'b11;
    wire [1:0]  cpu_dsack_n;
    reg         bus_write_stb;   // 1-cycle write pulse to memory/uart (driven by the bus FSM below)
    reg         bus_read_stb;    // 1-cycle read-commit pulse for read-clears-flag regs

    // Bus-control inputs {BGACKn,BRn,STERMn,AVECn,HALT_INn,BERRn}.
    // HALT_INn (bit 1) is asserted low with RESET_INn, matching the MC68030
    // reset contract. The remaining asynchronous bus-control inputs stay inactive.
    // Keep them behind a preserved register so implementation tools retain the
    // complete external-control interface used by simulation fault injection.
    // BKPT acknowledge cycles use CPU space at low addresses. There is no
    // external debugger/breakpoint ROM in this SoC, so respond with ILLEGAL as
    // the replacement instruction instead of letting the unmapped read return 0.
    localparam [31:0] BKPT_ILLEGAL_DIN = 32'h4afc0000; // word read at A[1:0]=00 samples [31:16]
    (* syn_preserve = 1 *) reg [5:0] cpu_ctl = 6'b111111;
    always @(posedge clk) cpu_ctl <= {4'b1111, ~rst, 1'b1};   // bit1 = HALT_INn = ~rst
    wire bkpt_ack_read = !cpu_as_n && cpu_rw_n
                       && (cpu_fc == 3'b111)
                       && (cpu_adr[31:8] == 24'h000000);
`ifdef ASTRA_SOC_SIM_IRQ
    wire [2:0]  cpu_ipln = sim_ipln;
    wire        cpu_avecn = sim_avecn;
    wire        cpu_berrn = sim_berrn;
`else
    wire [2:0]  cpu_ipln = 3'b111;
    wire        cpu_avecn = cpu_ctl[2];
    wire        cpu_berrn = cpu_ctl[0];
`endif

    assign cpu_adr     = tg_adr;
    assign cpu_dout    = tg_dout;
    assign cpu_data_en = tg_data_en;
    assign cpu_as_n    = tg_as_n;
    assign cpu_ds_n    = tg_ds_n;
    assign cpu_rw_n    = tg_rw_n;
    assign cpu_rmc_n   = tg_rmc_n;
    assign cpu_siz     = tg_siz;
    assign cpu_fc      = tg_fc;

    tg68k_cache_store tg_cache_store_i (
        .clk(clk),
        .rst(rst),
        .flush(sdram_bist_busy | astraea_cache_flush),
        .lookup_addr(tg_cache_lookup_addr),
        .lookup_insn(tg_cache_lookup_insn),
        .lookup_data(tg_cache_lookup_data),
        .lookup_ihit(tg_cache_ihit),
        .lookup_idata(tg_cache_idata),
        .lookup_dhit(tg_cache_dhit),
        .lookup_ddata(tg_cache_ddata),
        .store_valid(tg_cache_store_valid),
        .store_addr(tg_cache_store_addr),
        .store_data(tg_cache_store_data),
        .store_insn(tg_cache_store_insn),
        .invalidate_valid(tg_cache_invalidate_valid),
        .invalidate_addr(tg_cache_invalidate_addr),
        .invalidate_all(tg_cache_invalidate_all),
        .ifreeze(tg_cache_ifreeze),
        .dfreeze(tg_cache_dfreeze),
        .fill_valid(sdram_fill_valid),
        .fill_addr({7'b0000001, sdram_fill_addr}),
        .fill_data(sdram_fill_data),
        .fill_insn(sdram_fill_instruction)
    );

    generate
        begin : g_tg68k_enabled
            tg68k_wrap tg_cpu (
                .CLK        (clk),
                .ADR_OUT    (tg_adr),
                .DATA_IN    (cpu_din_visible),
                .DATA_OUT   (tg_dout),
                .DATA_EN    (tg_data_en),
                .RESET_INn  (~rst),
                .FC_OUT     (tg_fc),
                .IPLn       (cpu_ipln),
                .DSACKn     (cpu_dsack_n),
                .SIZE       (tg_siz),
                .ASn        (tg_as_n),
                .RWn        (tg_rw_n),
                .RMCn       (tg_rmc_n),
                .DSn        (tg_ds_n),
                .BERRn      (cpu_berrn),
                .HALT_INn   (cpu_ctl[1]),
                .AVECn      (cpu_avecn),
                .STERMn     (cpu_ctl[3]),
                .BRn        (cpu_ctl[4]),
                .BGACKn     (cpu_ctl[5]),
                .CACHEABLE_IN(tg_cacheable),
                .CACHE_FLUSH_IN(sdram_bist_busy | astraea_cache_flush),
                .CACHE_FILL_VALID(sdram_fill_valid),
                .CACHE_FILL_ADDR({7'b0000001, sdram_fill_addr}),
                .CACHE_FILL_DATA(sdram_fill_data),
                .CACHE_FILL_INSN(sdram_fill_instruction),
                .CACHE_IHIT_IN(tg_cache_ihit),
                .CACHE_IDATA_IN(tg_cache_idata),
                .CACHE_DHIT_IN(tg_cache_dhit),
                .CACHE_DDATA_IN(tg_cache_ddata),
                .CACHE_LOOKUP_ADDR(tg_cache_lookup_addr),
                .CACHE_LOOKUP_INSN(tg_cache_lookup_insn),
                .CACHE_LOOKUP_DATA(tg_cache_lookup_data),
                .CACHE_STORE_VALID(tg_cache_store_valid),
                .CACHE_STORE_ADDR(tg_cache_store_addr),
                .CACHE_STORE_DATA(tg_cache_store_data),
                .CACHE_STORE_INSN(tg_cache_store_insn),
                .CACHE_INVALIDATE_VALID(tg_cache_invalidate_valid),
                .CACHE_INVALIDATE_ADDR(tg_cache_invalidate_addr),
                .CACHE_INVALIDATE_ALL(tg_cache_invalidate_all),
                .CACHE_IFREEZE_OUT(tg_cache_ifreeze),
                .CACHE_DFREEZE_OUT(tg_cache_dfreeze),
                .CACHEABLE_OUT(tg_cacheable_out),
                .POSTABLE_OUT(tg_postable_out),
                .MEMORY_BUSY_IN(sdram_bridge_busy),
                .DBG_ICACHE_HITS(tg_icache_hits),
                .DBG_ICACHE_MISSES(tg_icache_misses),
                .DBG_DCACHE_HITS(tg_dcache_hits),
                .DBG_DCACHE_MISSES(tg_dcache_misses),
                .DBG_D2C    (tg_dbg_status),
                .DBG_IMM    (tg_dbg_imm),
                .DBG_ARIN   (tg_dbg_arin)
            );
        end
    endgenerate

    // -------------------------------------------------------------------------
    // Address decode (bring-up map)
    //   Stage 0 0xFFFC0000 (8 KB BRAM; reset vectors temporarily aliased at 0)
    //   ROM     0xFFE00000 (256 KB virtual aperture backed by SDRAM after handoff)
    //   BRAM 0x01FF8000..0x01FFFFFF (32 KB, temporary boot stack/scratch)
    //   SDRAM alias 0x02000000..0x03FFFFFF (32 MB; low mapping follows overlay work)
    //   Vesta identity 0xFFF00000..0xFFF000FF
    //   UART 0xFFF00500..0xFFF0050F (Vesta UART)
    // -------------------------------------------------------------------------
    localparam RAM_WORDS = 8192;              // 32 KB system RAM (BRAM)

    reg rom_overlay_sdram = 1'b0;
    wire sel_rom;
    wire sel_stage2_rom;
    wire sel_sdram;
    boot_memory_map #(
        .SD_BOOT_ENABLE(SD_BOOT_ENABLE),
        .SDRAM_ENABLE(SDRAM_ENABLE)
    ) boot_memory_map_i (
        .address(cpu_adr), .overlay_sdram(rom_overlay_sdram),
        .boot_bram_select(sel_rom), .stage2_select(sel_stage2_rom),
        .sdram_select(sel_sdram), .sdram_address(sdram_cpu_addr)
    );
    wire sel_ram  = (cpu_adr[31:15] == 17'h03FF); // 0x01FF8000..0x01FFFFFF
    wire sel_sys  = (cpu_adr[31:9]  == 23'h7FF800);
    wire sel_astraea = SDRAM_ENABLE && (cpu_adr[31:8] == 24'hFFF100);
    wire sel_uart = (cpu_adr[31:8]  == 24'hFFF005);
    wire sel_spi = (cpu_adr[31:8] == 24'hFFF006);
    wire sel_panel = (cpu_adr[31:12] == 20'hFFF01);
    wire sel_vega_regs = HDMI_ENABLE && (cpu_adr[31:8] == 24'hFFF200);
    wire sel_vega_text = HDMI_ENABLE && (cpu_adr[31:12] == 20'hFFF22);
    wire [31:0] panel_rdata;
    reg [7:0] led_r;

    astra_front_panel #(.CLK_HZ(CPU_CLK_HZ)) front_panel_i (
        .clk(clk), .rst(rst),
        .buttons(buttons), .switches(switches),
        .select(sel_panel), .reg_index(cpu_adr[7:2]),
        .write_strobe(bus_write_stb), .write_data(cpu_dout),
        .byte_enable(be), .read_data(panel_rdata),
        .diagnostic_leds(led_r), .leds(leds)
    );

    // -------------------------------------------------------------------------
    // Read-only Vesta machine identity. Boot software consumes this instead of
    // hardcoding the selected core, clock, memory map, or instantiated chips.
    // Personality descriptors are four words: ID, version, base, aperture size.
    // Only blocks actually present in this SoC are advertised.
    // -------------------------------------------------------------------------
    reg [31:0] sys_rdata;
    reg [31:0] sys_scratch = 32'd0;
    always @* begin
        sys_rdata = 32'd0;
        case (cpu_adr[8:2])
            7'h00: sys_rdata = 32'h56535441; // ID: "VSTA"
            7'h01: sys_rdata = 32'h00010000; // Vesta v1.0
            7'h02: sys_rdata = 32'h41363801; // Astra 68, board ABI 1
            7'h03: sys_rdata = {29'd0, host_boot_request_cpu,
                                rom_overlay_sdram, 1'b0};
            7'h04: sys_rdata = {26'd0, ASTRA_HOST_ENABLE, 1'b1, video_ready_cpu,
                                !rom_overlay_sdram, sdram_ready_cpu,
                                SDRAM_ENABLE ? 1'b1 : 1'b0};
            7'h05: sys_rdata = 32'd0;        // power-on reset
            7'h06: sys_rdata = sys_scratch; // SCRATCH
            7'h07: sys_rdata = CPU_MODEL;
            7'h08: sys_rdata = CPU_IMPLEMENTATION;
            7'h09: sys_rdata = CPU_FEATURES;
            7'h0a: sys_rdata = CPU_CLK_HZ;
            7'h0b: sys_rdata = SDRAM_ENABLE ? 32'h02000000 : 32'd0;
            7'h0c: sys_rdata = SDRAM_ENABLE ? 32'h02000000 : 32'd0;
            7'h0d: sys_rdata = 32'hffe00000;
            7'h0e: sys_rdata = 32'h00040000;
            7'h0f: sys_rdata = SOC_BUILD_ID;
            7'h10: sys_rdata = 32'd1 + (SDRAM_ENABLE ? 32'd1 : 32'd0) +
                                           (HDMI_ENABLE ? 32'd1 : 32'd0);
            7'h11: sys_rdata = 32'd16;       // descriptor stride
            7'h12: sys_rdata = 32'd0;        // NVRAM capabilities: none yet
            7'h14: sys_rdata = 32'h56535441; // personality 0: "VSTA"
            7'h15: sys_rdata = 32'h00010000;
            7'h16: sys_rdata = 32'hfff00000;
            7'h17: sys_rdata = 32'h00010000;
            7'h18: sys_rdata = SDRAM_ENABLE ? 32'h41535452 : 32'h56454741;
            7'h19: sys_rdata = 32'h00010000;
            7'h1a: sys_rdata = SDRAM_ENABLE ? 32'hfff10000 : 32'hfff20000;
            7'h1b: sys_rdata = 32'h00010000;
            7'h1c: sys_rdata = 32'h56454741; // personality 2: "VEGA"
            7'h1d: sys_rdata = 32'h00010000;
            7'h1e: sys_rdata = 32'hfff20000;
            7'h1f: sys_rdata = 32'h00010000;
            7'h34: sys_rdata = 32'd0; // SDRAM BIST control
            7'h35: sys_rdata = {21'd0, sdram_bist_phase, 5'd0,
                                 sdram_bist_errors != 0,
                                 sdram_bist_done, sdram_bist_busy};
            7'h36: sys_rdata = {7'd0, sdram_bist_progress};
            7'h37: sys_rdata = sdram_bist_errors;
            7'h38: sys_rdata = {7'd0, sdram_bist_first_fail};
            7'h39: sys_rdata = {24'd0, sdram_bist_expected};
            7'h3a: sys_rdata = {24'd0, sdram_bist_actual};
            7'h3b: sys_rdata = cpu_cycle_count[31:0];
            7'h3c: sys_rdata = cpu_cycle_count[63:32];
            7'h3d: sys_rdata = tg_icache_hits;
            7'h3e: sys_rdata = tg_icache_misses;
            7'h3f: sys_rdata = tg_dcache_hits;
            7'h44: sys_rdata = tg_dcache_misses;
            7'h45: sys_rdata = cpu_sdram_reads;
            7'h46: sys_rdata = cpu_sdram_writes;
            7'h47: sys_rdata = cpu_sdram_wait_cycles;
            7'h48: sys_rdata = sdram_line_hits;
            7'h49: sys_rdata = sdram_line_misses;
            7'h4a: sys_rdata = sdram_posted_writes;
            7'h4c: sys_rdata = {31'd0, host_boot_request_cpu};
            7'h4d: sys_rdata = {24'd0, host_seen_sync_cpu[1], 3'd0,
                                 host_error_sync_cpu[1], host_done_sync_cpu[1],
                                 host_busy_sync_cpu[1], host_boot_request_cpu};
            7'h4e: sys_rdata = host_size_cpu;
            7'h4f: sys_rdata = host_crc_cpu;
            7'h50: sys_rdata = host_sp_cpu;
            7'h51: sys_rdata = host_pc_cpu;
            7'h52: sys_rdata = host_bytes_cpu;
            7'h53: sys_rdata = {24'd0, host_error_cpu};
            default: sys_rdata = 32'd0;
        endcase
    end

    // -------------------------------------------------------------------------
    // ROM (32-bit words, big-endian; init from boot image)
    // -------------------------------------------------------------------------
    (* rom_style = "block" *) reg [31:0] rom [0:ROM_WORDS-1];
    initial $readmemh("rom_init.hex", rom);
    reg [31:0] rom_q;
    always @(posedge clk) rom_q <= rom[cpu_adr[$clog2(ROM_WORDS)+1:2]];

    // -------------------------------------------------------------------------
    // System RAM (32-bit words + per-byte write enable), big-endian lanes
    // -------------------------------------------------------------------------
    reg [7:0] ram0 [0:RAM_WORDS-1]; // [31:24]
    reg [7:0] ram1 [0:RAM_WORDS-1]; // [23:16]
    reg [7:0] ram2 [0:RAM_WORDS-1]; // [15:8]
    reg [7:0] ram3 [0:RAM_WORDS-1]; // [7:0]
    wire [12:0] ram_a = cpu_adr[$clog2(RAM_WORDS)+1:2];
    reg  [31:0] ram_q;

    // 68030 32-bit-port byte enables from SIZE + A[1:0] (big-endian lanes).
    // SIZE is the current transfer portion from the CPU: 00=4 bytes, 11=3
    // bytes, 10=2 bytes, 01=1 byte. Unaligned word/long cycles are split by
    // the TG68K wrapper, so each portion enables only the lanes
    // reachable from the current address to the end of this 32-bit port word.
    // lane3=[31:24]=A..00, lane2=A..01, lane1=A..10, lane0=A..11.
    reg [3:0] be;
    always @* begin
        case (cpu_siz)
            2'b00: begin                                      // long / line
                case (cpu_adr[1:0])
                    2'b00: be = 4'b1111;
                    2'b01: be = 4'b0111;
                    2'b10: be = 4'b0011;
                    default: be = 4'b0001;
                endcase
            end
            2'b11: begin                                      // 3 bytes
                case (cpu_adr[1:0])
                    2'b00: be = 4'b1110;
                    2'b01: be = 4'b0111;
                    2'b10: be = 4'b0011;
                    default: be = 4'b0001;
                endcase
            end
            2'b10: begin                                      // word
                case (cpu_adr[1:0])
                    2'b00: be = 4'b1100;
                    2'b01: be = 4'b0110;
                    2'b10: be = 4'b0011;
                    default: be = 4'b0001;
                endcase
            end
            default: be = 4'b1000 >> cpu_adr[1:0];            // byte
        endcase
    end

    wire ram_we = sel_ram & cpu_data_en & ~cpu_rw_n & bus_write_stb;
    wire astraea_write_stb = sel_astraea & cpu_data_en & ~cpu_rw_n &
                             bus_write_stb;
    always @(posedge clk) begin
        if (ram_we) begin
            if (be[3]) ram0[ram_a] <= cpu_dout[31:24];
            if (be[2]) ram1[ram_a] <= cpu_dout[23:16];
            if (be[1]) ram2[ram_a] <= cpu_dout[15:8];
            if (be[0]) ram3[ram_a] <= cpu_dout[7:0];
        end
        ram_q <= {ram0[ram_a], ram1[ram_a], ram2[ram_a], ram3[ram_a]};
    end

    // -------------------------------------------------------------------------
    // Vesta UART (minimal: TX + status). Reuse e6502 uart_tx.
    // -------------------------------------------------------------------------
    wire       uart_start;
    wire [7:0] uart_data;
    reg        uart_cpu_start;
    reg  [7:0] uart_cpu_data;
    wire       uart_mon_start;
    wire [7:0] uart_mon_data;
    wire       uart_busy;
    wire       uart_rst = UART_MONITOR ? !reset_n : rst;
    assign uart_start = UART_MONITOR ? uart_mon_start : uart_cpu_start;
    assign uart_data = UART_MONITOR ? uart_mon_data : uart_cpu_data;
    uart_tx #(.CLK_HZ(CPU_CLK_HZ), .BAUD(UART_BAUD)) uart_i (
        .clk(clk), .rst(uart_rst), .data(uart_data), .start(uart_start),
        .tx(ftdi_rxd), .busy(uart_busy)
    );
    // UART RX (host -> FPGA). A complete Harte RUN frame fits in this FIFO, so
    // back-to-back FTDI bytes cannot overwrite the single MMIO data register.
    wire [7:0] rx_byte; wire rx_valid;
    uart_rx #(.CLK_HZ(CPU_CLK_HZ), .BAUD(UART_BAUD)) urx (.clk(clk), .rst(rst), .rx(ftdi_txd),
        .data(rx_byte), .valid(rx_valid));
    // Read-commit strobe, not bus_write_stb: cpu_data_en is WRITE_ACCESS-only (see
    // the processor's write qualifier), so it (and bus_write_stb, which is only
    // ever set alongside it) is always 0 during a read. bus_read_stb below is
    // its read-side twin, set in the bus FSM when cpu_rw_n=1 (verified: 1 = read).
    wire [1:0] uart_reg = cpu_adr[3:2];
    // TG's 16-bit bus reads a 32-bit MMIO register as high and low words. Pop
    // only on the low-word beat; a native 32-bit CPU read pops on its one beat.
    wire rx_data_rd = sel_uart & (uart_reg == 2'd3) & bus_read_stb &
                      ((cpu_siz == 2'b00) | cpu_adr[1]);
    wire rx_status_clear = sel_uart & (uart_reg == 2'd2) & cpu_data_en &
                           ~cpu_rw_n & bus_write_stb & be[0] & cpu_dout[1];
    wire [7:0] rx_data;
    wire [7:0] rx_level;
    wire rx_empty;
    wire rx_overrun;
    uart_rx_fifo #(.DEPTH(UART_RX_FIFO_DEPTH)) uart_rx_fifo_i (
        .clk(clk), .rst(rst), .push_data(rx_byte), .push(rx_valid),
        .pop(rx_data_rd), .clear_overrun(rx_status_clear), .data(rx_data),
        .empty(rx_empty), .full(), .overrun(rx_overrun), .level(rx_level)
    );
    // MMIO regs are accessed as 32-bit longs (volatile uint32_t), value natural
    // in [31:0]. UART_DATA @ 0xFFF00500 (write, char in [7:0]);
    // UART_STATUS @ 0xFFF00504 (read, [0]=TX_READY, [1]=BUSY).
    // UART_RXSTATUS @ 0xFFF00508: [0]=ready, [1]=sticky overrun, [15:8]=level.
    // Writing bit 1 clears overrun. Reading UART_RXDATA pops one byte.
    wire uart_data_wr_req = sel_uart & (uart_reg == 2'd0) & be[0]
                           & cpu_data_en & ~cpu_rw_n;
    wire uart_data_wr = uart_data_wr_req & bus_write_stb;
    wire [7:0] uart_wdata =
        be[0] ? cpu_dout[7:0] :
        be[1] ? cpu_dout[15:8] :
        be[2] ? cpu_dout[23:16] :
                cpu_dout[31:24];
    wire [31:0] uart_rdata =
        (uart_reg == 2'd1) ? {30'd0, uart_busy, ~uart_busy} :     // TX status (existing)
        (uart_reg == 2'd2) ? {16'd0, rx_level, 6'd0, rx_overrun, ~rx_empty} :
        (uart_reg == 2'd3) ? {24'd0, rx_data}               :     // RX data
        32'd0;

    // -------------------------------------------------------------------------
    // Vesta SPI / SD. The card is wired in SPI mode: CMD=MOSI, D0=MISO,
    // D3=CSn. D1/D2 remain released for compatibility with the shared ESP32
    // wiring on ULX3S.
    // -------------------------------------------------------------------------
    wire [1:0] spi_reg = cpu_adr[3:2];
    wire spi_ctrl_wr = sel_spi & (spi_reg == 2'd0) & be[0] &
                       cpu_data_en & ~cpu_rw_n & bus_write_stb;
    wire spi_data_wr = sel_spi & (spi_reg == 2'd2) & be[0] &
                       cpu_data_en & ~cpu_rw_n & bus_write_stb;
    wire [7:0] spi_rx_data;
    wire spi_busy;
    wire spi_cs_n;
    wire [3:0] spi_clkdiv;
    wire sd_spi_clk;
    wire sd_spi_mosi;

    spi_sd spi_sd_i (
        .clk(clk), .rst(rst),
        .ctrl_we(spi_ctrl_wr), .ctrl_wdata(uart_wdata),
        .data_we(spi_data_wr), .data_wdata(uart_wdata),
        .data_rdata(spi_rx_data), .busy(spi_busy),
        .cs_n(spi_cs_n), .clkdiv(spi_clkdiv),
        .sd_clk(sd_spi_clk), .sd_mosi(sd_spi_mosi), .sd_miso(sd_d_in[0])
    );

`ifdef SYNTHESIS
    // Explicit pad cells are required here. With a parameter-selected top-level
    // `Z` assignment, Yosys can demote the shared inout to an output and then
    // constant-fold the ESP clock/select inputs. These BIDIR cells keep the
    // physical input path and output-enable visible through synthesis.
    TRELLIS_IO #(.DIR("BIDIR")) sd_clk_pad (
        .B(sd_clk), .I(sd_spi_clk), .T(ASTRA_HOST_ENABLE), .O(sd_clk_in)
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_cmd_pad (
        .B(sd_cmd), .I(sd_spi_mosi), .T(ASTRA_HOST_ENABLE), .O(sd_cmd_in)
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d0_pad (
        .B(sd_d[0]), .I(host_spi_miso),
        .T(ASTRA_HOST_ENABLE ? !host_spi_miso_oe : 1'b1), .O(sd_d_in[0])
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d1_pad (
        .B(sd_d[1]), .I(1'b0), .T(1'b1), .O(sd_d_in[1])
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d2_pad (
        .B(sd_d[2]), .I(1'b0), .T(1'b1), .O(sd_d_in[2])
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d3_pad (
        .B(sd_d[3]), .I(spi_cs_n), .T(ASTRA_HOST_ENABLE), .O(sd_d_in[3])
    );
`else
    assign sd_clk = ASTRA_HOST_ENABLE ? 1'bz : sd_spi_clk;
    assign sd_cmd = ASTRA_HOST_ENABLE ? 1'bz : sd_spi_mosi;
    assign sd_d[0] = ASTRA_HOST_ENABLE && host_spi_miso_oe ?
                     host_spi_miso : 1'bz;
    assign sd_d[1] = 1'bz;
    assign sd_d[2] = 1'bz;
    assign sd_d[3] = ASTRA_HOST_ENABLE ? 1'bz : spi_cs_n;
    assign sd_clk_in = sd_clk;
    assign sd_cmd_in = sd_cmd;
    assign sd_d_in = sd_d;
`endif

    wire [31:0] spi_rdata =
        (spi_reg == 2'd0) ? {24'd0, spi_clkdiv, 3'd0, spi_cs_n} :
        (spi_reg == 2'd1) ? {31'd0, spi_busy} :
        (spi_reg == 2'd2) ? {24'd0, spi_rx_data} : 32'd0;

    // -------------------------------------------------------------------------
    // Vega bootstrap display: a 90x30 ASCII plane feeding NovaVM's proven
    // 720x480 ECP5 TMDS serializer. This path is independent of SDRAM so POST
    // can display a memory failure. The later framebuffer Vega implementation
    // will take over this aperture after boot.
    // -------------------------------------------------------------------------
    wire [9:0] video_cx;
    wire [9:0] video_cy;
    wire [7:0] console_cpu_rdata;
    wire       console_cpu_we = sel_vega_text & cpu_data_en & ~cpu_rw_n & bus_write_stb;

    generate
        if (HDMI_ENABLE) begin : g_hdmi
            reg [3:0] video_reset_pipe = 4'b0000;
            always @(posedge video_pixel_clk) begin
                if (!reset_n || !video_pll_locked)
                    video_reset_pipe <= 4'b0000;
                else
                    video_reset_pipe <= {video_reset_pipe[2:0], 1'b1};
            end
            wire video_rst = !video_reset_pipe[3];
            wire [23:0] console_rgb;
            wire [2:0] hdmi_tmds;
            wire hdmi_tmds_clock;

            post_console post_console_i (
                .cpu_clk(clk),
                .cpu_addr(cpu_adr[11:0]),
                .cpu_wdata(uart_wdata),
                .cpu_we(console_cpu_we),
                .cpu_rdata(console_cpu_rdata),
                .pixel_clk(video_pixel_clk),
                .pixel_rst(video_rst),
                .pixel_x(video_cx),
                .pixel_y(video_cy),
                .rgb(console_rgb)
            );

            hdmi #(
                .VIDEO_ID_CODE(2),
                .DVI_OUTPUT(1'b1),
                .VIDEO_REFRESH_RATE_MILLIHZ(59940),
                .START_X(856),
                .START_Y(524),
                .AUDIO_RATE(48000),
                .AUDIO_BIT_WIDTH(16),
                .VENDOR_NAME({"Astra", 24'd0}),
                .PRODUCT_DESCRIPTION({"Astra 68 POST", 24'd0}),
                .SOURCE_DEVICE_INFORMATION(8'h09)
            ) hdmi_inst (
                .clk_pixel_x5(video_shift_clk),
                .clk_pixel(video_pixel_clk),
                .clk_audio(1'b0),
                .reset(video_rst),
                .rgb(console_rgb),
                .audio_sample_word(32'd0),
                .tmds(hdmi_tmds),
                .tmds_clock(hdmi_tmds_clock),
                .cx(video_cx),
                .cy(video_cy),
                .frame_width(),
                .frame_height(),
                .screen_width(),
                .screen_height()
            );

            assign gpdi_dp = {hdmi_tmds_clock, hdmi_tmds};
        end else begin : g_hdmi_disabled
            assign video_cx = 10'd0;
            assign video_cy = 10'd0;
            assign console_cpu_rdata = 8'h20;
            assign gpdi_dp = 4'd0;
        end
    endgenerate

    reg [31:0] vega_rdata;
    always @* begin
        case (cpu_adr[7:2])
            6'h00: vega_rdata = 32'h56454741; // "VEGA"
            6'h01: vega_rdata = 32'h00010000;
            6'h02: vega_rdata = 32'h00000001; // display enabled
            6'h03: vega_rdata = {27'd0, video_ready_cpu, 2'd0,
                                 video_cx >= 10'd720, video_cy >= 10'd480};
            6'h06: vega_rdata = 32'd0;        // 720x480 mode
            6'h07: vega_rdata = 32'h00000001; // bootstrap text capability
            6'h08: vega_rdata = {6'd0, video_cy, 6'd0, video_cx};
            6'h0a: vega_rdata = {16'd480, 16'd720};
            6'h0c: vega_rdata = 32'h00101820; // backdrop RGB888
            default: vega_rdata = 32'd0;
        endcase
    end

    // -------------------------------------------------------------------------
    // Bus interface FSM (async 68030 slave, registered, a few wait states)
    // -------------------------------------------------------------------------
    localparam [2:0] BS_IDLE=3'd0, BS_WAIT=3'd1, BS_ACK=3'd2,
                     BS_END=3'd3, BS_SDRAM=3'd4;
    reg [2:0] bs;
    reg [1:0] waitc;
    reg       sdram_cycle_started;
    wire      sdram_post_cycle = sel_sdram && !cpu_rw_n && tg_postable_out;
    reg [31:0] cpu_sdram_reads = 32'd0;
    reg [31:0] cpu_sdram_writes = 32'd0;
    reg [31:0] cpu_sdram_wait_cycles = 32'd0;
    // The bridge response toggle has already crossed two synchronizer stages,
    // so its bundled data is stable before this direct CPU sampling window.
    // Keep the registered bus FSM for ownership and cycle teardown.
    assign cpu_din_visible = (bs == BS_SDRAM && sdram_bridge_done) ?
                             sdram_bridge_rdata : cpu_din;
    assign cpu_dsack_n = (bs == BS_SDRAM && sdram_bridge_done) ?
                         2'b00 : dsack_n;
    reg [31:0] dbg_last_vec_adr = 32'd0;
    reg [31:0] dbg_last_wr_adr  = 32'd0;
    reg [31:0] dbg_last_wr_data = 32'd0;
    reg [3:0]  dbg_last_wr_be   = 4'd0;
    reg [31:0] dbg_prev_wr_adr  = 32'd0;
    reg [31:0] dbg_prev_wr_data = 32'd0;
    reg [3:0]  dbg_prev_wr_be   = 4'd0;
    reg        dbg_fault_valid = 1'b0;
    reg [31:0] dbg_fault_vec_adr = 32'd0;
    reg [31:0] dbg_prog_adr0 = 32'd0;
    reg [31:0] dbg_prog_adr1 = 32'd0;
    reg [31:0] dbg_prog_adr2 = 32'd0;
    reg [31:0] dbg_prog_adr3 = 32'd0;
    reg [31:0] dbg_prog_data0 = 32'd0;
    reg [31:0] dbg_prog_data1 = 32'd0;
    reg [31:0] dbg_prog_data2 = 32'd0;
    reg [31:0] dbg_prog_data3 = 32'd0;
    reg [1:0]  dbg_prog_siz0 = 2'd0;
    reg [1:0]  dbg_prog_siz1 = 2'd0;
    reg [1:0]  dbg_prog_siz2 = 2'd0;
    reg [1:0]  dbg_prog_siz3 = 2'd0;
    reg [31:0] dbg_fault_prog_adr0 = 32'd0;
    reg [31:0] dbg_fault_prog_adr1 = 32'd0;
    reg [31:0] dbg_fault_prog_adr2 = 32'd0;
    reg [31:0] dbg_fault_prog_adr3 = 32'd0;
    reg [31:0] dbg_fault_prog_data0 = 32'd0;
    reg [31:0] dbg_fault_prog_data1 = 32'd0;
    reg [31:0] dbg_fault_prog_data2 = 32'd0;
    reg [31:0] dbg_fault_prog_data3 = 32'd0;
    reg [1:0]  dbg_fault_prog_siz0 = 2'd0;
    reg [1:0]  dbg_fault_prog_siz1 = 2'd0;
    reg [1:0]  dbg_fault_prog_siz2 = 2'd0;
    reg [1:0]  dbg_fault_prog_siz3 = 2'd0;

    always @(posedge clk) begin
        uart_cpu_start <= 1'b0;
        sdram_cpu_start <= 1'b0;
        sdram_bist_start <= 1'b0;
        bus_write_stb <= 1'b0;
        bus_read_stb  <= 1'b0;
        if (rst) begin
            bs <= BS_IDLE; dsack_n <= 2'b11;
            rom_overlay_sdram <= 1'b0;
            host_boot_request_cpu <= 1'b0;
            sdram_cycle_started <= 1'b0;
            cpu_sdram_reads <= 32'd0;
            cpu_sdram_writes <= 32'd0;
            cpu_sdram_wait_cycles <= 32'd0;
            sys_scratch <= 32'd0;
            sdram_bist_start <= 1'b0;
            dbg_last_vec_adr <= 32'd0;
            dbg_last_wr_adr  <= 32'd0;
            dbg_last_wr_data <= 32'd0;
            dbg_last_wr_be   <= 4'd0;
            dbg_prev_wr_adr  <= 32'd0;
            dbg_prev_wr_data <= 32'd0;
            dbg_prev_wr_be   <= 4'd0;
            dbg_fault_valid <= 1'b0;
            dbg_fault_vec_adr <= 32'd0;
            dbg_prog_adr0 <= 32'd0;
            dbg_prog_adr1 <= 32'd0;
            dbg_prog_adr2 <= 32'd0;
            dbg_prog_adr3 <= 32'd0;
            dbg_prog_data0 <= 32'd0;
            dbg_prog_data1 <= 32'd0;
            dbg_prog_data2 <= 32'd0;
            dbg_prog_data3 <= 32'd0;
            dbg_prog_siz0 <= 2'd0;
            dbg_prog_siz1 <= 2'd0;
            dbg_prog_siz2 <= 2'd0;
            dbg_prog_siz3 <= 2'd0;
            dbg_fault_prog_adr0 <= 32'd0;
            dbg_fault_prog_adr1 <= 32'd0;
            dbg_fault_prog_adr2 <= 32'd0;
            dbg_fault_prog_adr3 <= 32'd0;
            dbg_fault_prog_data0 <= 32'd0;
            dbg_fault_prog_data1 <= 32'd0;
            dbg_fault_prog_data2 <= 32'd0;
            dbg_fault_prog_data3 <= 32'd0;
            dbg_fault_prog_siz0 <= 2'd0;
            dbg_fault_prog_siz1 <= 2'd0;
            dbg_fault_prog_siz2 <= 2'd0;
            dbg_fault_prog_siz3 <= 2'd0;
        end else begin
        if (bs == BS_SDRAM)
            cpu_sdram_wait_cycles <= cpu_sdram_wait_cycles + 32'd1;
        case (bs)
            BS_IDLE: begin
                dsack_n <= 2'b11;
                if (!cpu_as_n) begin
                    // BERR terminates the cycle without committing a local
                    // slave side effect. This also makes injected faults test
                    // the processor's RTE replay path rather than a leaked write.
                    if (!cpu_berrn) begin
                        bs <= BS_END;
                    // A posted SDRAM write still owns the external bus. Cache
                    // hits may execute inside the TG030, but no later ROM,
                    // MMIO, PMMU, or SDRAM cycle can pass it.
                    end else if (sdram_bridge_busy) begin
                        bs <= BS_IDLE;
                    end else if (sel_sdram) begin
                        sdram_cycle_started <= 1'b0;
                        if (sdram_ready_cpu) begin
                            sdram_cpu_start <= 1'b1;
                            sdram_cycle_started <= 1'b1;
                            if (cpu_rw_n)
                                cpu_sdram_reads <= cpu_sdram_reads + 32'd1;
                            else
                                cpu_sdram_writes <= cpu_sdram_writes + 32'd1;
                            if (!cpu_rw_n && tg_postable_out) begin
                                // The bridge captures this stable request on
                                // the next edge and retains external-bus
                                // ownership until SDRAM responds. Terminate
                                // the 68030-facing cycle now so cache-only
                                // execution can overlap the physical write.
                                bus_write_stb <= 1'b1;
                                dsack_n <= 2'b00;
                                bs <= BS_END;
                            end else begin
                                bs <= BS_SDRAM;
                            end
                        end else begin
                            bs <= BS_SDRAM;
                        end
                    end else begin
                        // ROM and BRAM are synchronous. The decode edge starts
                        // their read; one BS_WAIT edge is sufficient to use q.
                        waitc <= 2'd0;
                        bs <= BS_WAIT;
                    end
                end
            end
            BS_WAIT: begin                        // let BRAM read settle / hold addr
                if (waitc != 0) waitc <= waitc - 1'b1;
                else begin
                    if (!cpu_rw_n && cpu_data_en) begin
                        bus_write_stb <= 1'b1;             // commit write this cycle
                        if (sel_ram) begin
                            dbg_prev_wr_adr  <= dbg_last_wr_adr;
                            dbg_prev_wr_data <= dbg_last_wr_data;
                            dbg_prev_wr_be   <= dbg_last_wr_be;
                            dbg_last_wr_adr  <= cpu_adr;
                            dbg_last_wr_data <= cpu_dout;
                            dbg_last_wr_be   <= be;
                        end
                        if (uart_data_wr_req) begin
                            uart_cpu_data <= uart_wdata; uart_cpu_start <= 1'b1;
                        end
                        if (sel_sys && cpu_adr[8:2] == 7'h34 && !astraea_busy &&
                            be[0] && cpu_dout[0]) begin
                            sdram_bist_start <= 1'b1;
                        end
                        if (sel_sys && cpu_adr[8:2] == 7'h06) begin
                            if (be[3]) sys_scratch[31:24] <= cpu_dout[31:24];
                            if (be[2]) sys_scratch[23:16] <= cpu_dout[23:16];
                            if (be[1]) sys_scratch[15:8] <= cpu_dout[15:8];
                            if (be[0]) sys_scratch[7:0] <= cpu_dout[7:0];
                        end
                        if (sel_sys && cpu_adr[8:2] == 7'h03 && SD_BOOT_ENABLE &&
                            sdram_ready_cpu && be[0] && cpu_dout[1]) begin
                            rom_overlay_sdram <= 1'b1;
                        end
                        if (sel_sys && cpu_adr[8:2] == 7'h03 &&
                            ASTRA_HOST_ENABLE && sdram_ready_cpu && be[0] &&
                            cpu_dout[2]) begin
                            host_boot_request_cpu <= 1'b1;
                        end
                    end else if (cpu_rw_n) begin
                        cpu_din <= bkpt_ack_read ? BKPT_ILLEGAL_DIN :
                                   sel_rom  ? rom_q :
                                   sel_ram  ? ram_q :
                                   sel_sys  ? sys_rdata :
                                   sel_astraea ? astraea_rdata :
                                   sel_vega_regs ? vega_rdata :
                                   sel_vega_text ? {4{console_cpu_rdata}} :
                                   sel_panel ? panel_rdata :
                                   sel_spi ? spi_rdata :
                                   sel_uart ? uart_rdata : 32'd0;
                        bus_read_stb <= 1'b1;              // commit read this cycle
                        if (!dbg_fault_valid && sel_rom && cpu_adr[31:20] == 12'hFFE && cpu_fc == 3'b110) begin
                            dbg_prog_adr3 <= dbg_prog_adr2;
                            dbg_prog_adr2 <= dbg_prog_adr1;
                            dbg_prog_adr1 <= dbg_prog_adr0;
                            dbg_prog_adr0 <= {cpu_adr[31:2], 2'b00};
                            dbg_prog_data3 <= dbg_prog_data2;
                            dbg_prog_data2 <= dbg_prog_data1;
                            dbg_prog_data1 <= dbg_prog_data0;
                            dbg_prog_data0 <= rom_q;
                            dbg_prog_siz3 <= dbg_prog_siz2;
                            dbg_prog_siz2 <= dbg_prog_siz1;
                            dbg_prog_siz1 <= dbg_prog_siz0;
                            dbg_prog_siz0 <= cpu_siz;
                        end
                        if (sel_rom && cpu_adr[31:8] == 24'h000000 && cpu_adr[7:2] > 6'd1) begin
                            dbg_last_vec_adr <= {cpu_adr[31:2], 2'b00};
                            if (!dbg_fault_valid) begin
                                dbg_fault_valid <= 1'b1;
                                dbg_fault_vec_adr <= {cpu_adr[31:2], 2'b00};
                                dbg_fault_prog_adr0 <= dbg_prog_adr0;
                                dbg_fault_prog_adr1 <= dbg_prog_adr1;
                                dbg_fault_prog_adr2 <= dbg_prog_adr2;
                                dbg_fault_prog_adr3 <= dbg_prog_adr3;
                                dbg_fault_prog_data0 <= dbg_prog_data0;
                                dbg_fault_prog_data1 <= dbg_prog_data1;
                                dbg_fault_prog_data2 <= dbg_prog_data2;
                                dbg_fault_prog_data3 <= dbg_prog_data3;
                                dbg_fault_prog_siz0 <= dbg_prog_siz0;
                                dbg_fault_prog_siz1 <= dbg_prog_siz1;
                                dbg_fault_prog_siz2 <= dbg_prog_siz2;
                                dbg_fault_prog_siz3 <= dbg_prog_siz3;
                            end
                        end
                    end
                    // Read data and write side effects are now registered.
                    // Assert DSACK on this completion edge; the CPU samples
                    // both on the following clock.
                    dsack_n <= 2'b00;
                    bs <= BS_END;
                end
            end
            BS_SDRAM: begin
                if (!sdram_cycle_started && sdram_ready_cpu &&
                    !sdram_bridge_busy) begin
                    sdram_cpu_start <= 1'b1;
                    sdram_cycle_started <= 1'b1;
                    if (cpu_rw_n)
                        cpu_sdram_reads <= cpu_sdram_reads + 32'd1;
                    else
                        cpu_sdram_writes <= cpu_sdram_writes + 32'd1;
                end else if (sdram_cycle_started && sdram_bridge_done) begin
                    if (cpu_rw_n) begin
                        cpu_din <= sdram_bridge_rdata;
                        bus_read_stb <= 1'b1;
                    end else begin
                        bus_write_stb <= 1'b1;
                    end
                    dsack_n <= 2'b00;
                    bs <= BS_END;
                end
            end
            BS_ACK: begin
                // Read data was presented in BS_WAIT so it is stable before DSACK.
                cpu_din <= bkpt_ack_read ? BKPT_ILLEGAL_DIN :
                           sel_rom  ? rom_q :
                           sel_ram  ? ram_q :
                           sel_sdram ? sdram_bridge_rdata :
                           sel_sys  ? sys_rdata :
                           sel_astraea ? astraea_rdata :
                           sel_vega_regs ? vega_rdata :
                           sel_vega_text ? {4{console_cpu_rdata}} :
                           sel_panel ? panel_rdata :
                           sel_spi ? spi_rdata :
                           sel_uart ? uart_rdata : 32'd0;
                dsack_n <= 2'b00;                 // 32-bit port ack
                bs <= BS_END;
            end
            BS_END: begin
                dsack_n <= 2'b00;
                if (cpu_as_n) begin
                    dsack_n <= 2'b11;
                    sdram_cycle_started <= 1'b0;
                    bs <= BS_IDLE;
                end
            end
            default: bs <= BS_IDLE;
        endcase
        end
    end

    // Diagnostic UART monitor. This is disabled in normal builds. In monitor
    // builds, the UART is driven from hardware so a silent CPU still yields a
    // bus snapshot. Format:
    //   FLT V<vector-adr> A<current-adr>
    //       0<last-op-adr>R<last-op-data> ... 3<older-op-adr>R<older-op-data> S<sizes>
    reg        mon_start = 1'b0;
    reg  [7:0] mon_data = 8'h00;
    reg  [6:0] mon_idx = 7'd0;
    reg [20:0] mon_gap = 21'd0;
    reg        mon_snap_rst = 1'b0;
    reg  [2:0] mon_snap_bs = 3'd0;
    reg  [3:0] mon_snap_bus = 4'd0;
    reg  [1:0] mon_snap_dsack = 2'd0;
    reg  [2:0] mon_snap_fc = 3'd0;
    reg        mon_snap_hang = 1'b0;
    reg [31:0] mon_snap_adr = 32'd0;
    reg [31:0] mon_snap_vec = 32'd0;
    reg [31:0] mon_snap_wr_adr = 32'd0;
    reg [31:0] mon_snap_wr_data = 32'd0;
    reg [3:0]  mon_snap_wr_be = 4'd0;
    reg [31:0] mon_snap_prev_wr_adr = 32'd0;
    reg [31:0] mon_snap_prev_wr_data = 32'd0;
    reg [3:0]  mon_snap_prev_wr_be = 4'd0;
    reg [31:0] mon_snap_fault_vec = 32'd0;
    reg [31:0] mon_snap_fault_prog_adr0 = 32'd0;
    reg [31:0] mon_snap_fault_prog_adr1 = 32'd0;
    reg [31:0] mon_snap_fault_prog_adr2 = 32'd0;
    reg [31:0] mon_snap_fault_prog_adr3 = 32'd0;
    reg [31:0] mon_snap_fault_prog_data0 = 32'd0;
    reg [31:0] mon_snap_fault_prog_data1 = 32'd0;
    reg [31:0] mon_snap_fault_prog_data2 = 32'd0;
    reg [31:0] mon_snap_fault_prog_data3 = 32'd0;
    reg [1:0]  mon_snap_fault_prog_siz0 = 2'd0;
    reg [1:0]  mon_snap_fault_prog_siz1 = 2'd0;
    reg [1:0]  mon_snap_fault_prog_siz2 = 2'd0;
    reg [1:0]  mon_snap_fault_prog_siz3 = 2'd0;
    assign uart_mon_start = mon_start;
    assign uart_mon_data = mon_data;

    function automatic [7:0] hexchar(input [3:0] v);
        hexchar = (v < 4'd10) ? (8'h30 + {4'd0, v}) : (8'h41 + {4'd0, v - 4'd10});
    endfunction

    function automatic [7:0] hex32_at(input [31:0] v, input [2:0] n);
        begin
            case (n)
                3'd0: hex32_at = hexchar(v[31:28]);
                3'd1: hex32_at = hexchar(v[27:24]);
                3'd2: hex32_at = hexchar(v[23:20]);
                3'd3: hex32_at = hexchar(v[19:16]);
                3'd4: hex32_at = hexchar(v[15:12]);
                3'd5: hex32_at = hexchar(v[11:8]);
                3'd6: hex32_at = hexchar(v[7:4]);
                default: hex32_at = hexchar(v[3:0]);
            endcase
        end
    endfunction

    function automatic [7:0] mon_char(input [6:0] i);
        begin
            if (i == 7'd0) mon_char = "F";
            else if (i == 7'd1) mon_char = "L";
            else if (i == 7'd2) mon_char = "T";
            else if (i == 7'd3) mon_char = " ";
            else if (i == 7'd4) mon_char = "V";
            else if (i >= 7'd5 && i <= 7'd12) mon_char = hex32_at(mon_snap_fault_vec, i - 7'd5);
            else if (i == 7'd13) mon_char = " ";
            else if (i == 7'd14) mon_char = "A";
            else if (i >= 7'd15 && i <= 7'd22) mon_char = hex32_at(mon_snap_adr, i - 7'd15);
            else if (i == 7'd23) mon_char = " ";
            else if (i == 7'd24) mon_char = "0";
            else if (i >= 7'd25 && i <= 7'd32) mon_char = hex32_at(mon_snap_fault_prog_adr0, i - 7'd25);
            else if (i == 7'd33) mon_char = "R";
            else if (i >= 7'd34 && i <= 7'd41) mon_char = hex32_at(mon_snap_fault_prog_data0, i - 7'd34);
            else if (i == 7'd42) mon_char = " ";
            else if (i == 7'd43) mon_char = "1";
            else if (i >= 7'd44 && i <= 7'd51) mon_char = hex32_at(mon_snap_fault_prog_adr1, i - 7'd44);
            else if (i == 7'd52) mon_char = "R";
            else if (i >= 7'd53 && i <= 7'd60) mon_char = hex32_at(mon_snap_fault_prog_data1, i - 7'd53);
            else if (i == 7'd61) mon_char = " ";
            else if (i == 7'd62) mon_char = "2";
            else if (i >= 7'd63 && i <= 7'd70) mon_char = hex32_at(mon_snap_fault_prog_adr2, i - 7'd63);
            else if (i == 7'd71) mon_char = "R";
            else if (i >= 7'd72 && i <= 7'd79) mon_char = hex32_at(mon_snap_fault_prog_data2, i - 7'd72);
            else if (i == 7'd80) mon_char = " ";
            else if (i == 7'd81) mon_char = "3";
            else if (i >= 7'd82 && i <= 7'd89) mon_char = hex32_at(mon_snap_fault_prog_adr3, i - 7'd82);
            else if (i == 7'd90) mon_char = "R";
            else if (i >= 7'd91 && i <= 7'd98) mon_char = hex32_at(mon_snap_fault_prog_data3, i - 7'd91);
            else if (i == 7'd99) mon_char = " ";
            else if (i == 7'd100) mon_char = "S";
            else if (i == 7'd101) mon_char = hexchar({2'b00, mon_snap_fault_prog_siz0});
            else if (i == 7'd102) mon_char = hexchar({2'b00, mon_snap_fault_prog_siz1});
            else if (i == 7'd103) mon_char = hexchar({2'b00, mon_snap_fault_prog_siz2});
            else if (i == 7'd104) mon_char = hexchar({2'b00, mon_snap_fault_prog_siz3});
            else if (i == 7'd105) mon_char = "\r";
            else if (i == 7'd106) mon_char = "\n";
            else mon_char = 8'h00;
        end
    endfunction

    always @(posedge clk) begin
        mon_start <= 1'b0;
        if (!reset_n) begin
            mon_idx <= 7'd0;
            mon_gap <= 21'd0;
        end else if (UART_MONITOR && mon_gap != 21'd0) begin
            mon_gap <= mon_gap - 1'b1;
        end else if (UART_MONITOR && !uart_busy && !mon_start) begin
            if (mon_idx == 7'd0) begin
                mon_snap_rst     <= rst;
                mon_snap_bs      <= bs;
                mon_snap_bus     <= {cpu_as_n, cpu_rw_n, cpu_ds_n, cpu_data_en};
                mon_snap_dsack   <= dsack_n;
                mon_snap_fc      <= cpu_fc;
                mon_snap_hang    <= hang_seen;
                mon_snap_adr     <= cpu_adr;
                mon_snap_vec     <= dbg_last_vec_adr;
                mon_snap_wr_adr  <= dbg_last_wr_adr;
                mon_snap_wr_data <= dbg_last_wr_data;
                mon_snap_wr_be   <= dbg_last_wr_be;
                mon_snap_prev_wr_adr  <= dbg_prev_wr_adr;
                mon_snap_prev_wr_data <= dbg_prev_wr_data;
                mon_snap_prev_wr_be   <= dbg_prev_wr_be;
                mon_snap_fault_vec <= dbg_fault_valid ? dbg_fault_vec_adr : dbg_last_vec_adr;
                mon_snap_fault_prog_adr0 <= dbg_fault_prog_adr0;
                mon_snap_fault_prog_adr1 <= dbg_fault_prog_adr1;
                mon_snap_fault_prog_adr2 <= dbg_fault_prog_adr2;
                mon_snap_fault_prog_adr3 <= dbg_fault_prog_adr3;
                mon_snap_fault_prog_data0 <= dbg_fault_prog_data0;
                mon_snap_fault_prog_data1 <= dbg_fault_prog_data1;
                mon_snap_fault_prog_data2 <= dbg_fault_prog_data2;
                mon_snap_fault_prog_data3 <= dbg_fault_prog_data3;
                mon_snap_fault_prog_siz0 <= dbg_fault_prog_siz0;
                mon_snap_fault_prog_siz1 <= dbg_fault_prog_siz1;
                mon_snap_fault_prog_siz2 <= dbg_fault_prog_siz2;
                mon_snap_fault_prog_siz3 <= dbg_fault_prog_siz3;
            end
            mon_data <= mon_char(mon_idx);
            mon_start <= 1'b1;
            if (mon_idx == 7'd106) begin
                mon_idx <= 7'd0;
                mon_gap <= 21'd312500; // about 100 ms at 3.125 MHz
            end else begin
                mon_idx <= mon_idx + 1'b1;
            end
        end
    end

    // -------------------------------------------------------------------------
    // LEDs: liveness (heartbeat) + a few CPU bus signals for at-a-glance debug
    // -------------------------------------------------------------------------
    reg [23:0] hb;
    always @(posedge clk) hb <= hb + 1'b1;

    // DEBUG hang capture: if the CPU holds ASn low for ~4096 clocks (a real stall — normal
    // cycles are <10), latch the frozen address bus + rw/siz/fc. Then the LEDs cycle the whole
    // 32-bit address out for reading: phase 0 = 0xFF marker, phases 1-4 = addr bytes MSB..LSB,
    // ~0.34s each. If nothing ever latches, the CPU is LOOPING (ASn toggling), not stuck.
    // Restore the default {hb[23],~as_n,~rw_n,uart_busy,adr[3:0]} liveness view after debugging.
    reg [31:0] hang_adr = 32'd0;
    reg [2:0]  hang_aux = 3'd0;      // {rw_n, siz[0]... } spare
    reg        hang_seen = 1'b0;
    reg [11:0] stallc = 12'd0;
    always @(posedge clk) begin
        if (rst) begin stallc <= 0; hang_seen <= 1'b0; end
        else if (cpu_as_n) stallc <= 0;                       // cycle ended -> reset stall count
        else if (!hang_seen) begin
            stallc <= stallc + 1'b1;
            if (&stallc) begin hang_adr <= cpu_adr; hang_aux <= {cpu_rw_n, cpu_siz}; hang_seen <= 1'b1; end
        end
    end
    always @* begin
        if (!hang_seen) led_r = {hb[23], ~cpu_as_n, ~cpu_rw_n, uart_busy, cpu_adr[3:0]}; // live
        else case (hb[22:20])
            3'd0: led_r = 8'hFF;                 // marker (all on)
            3'd1: led_r = hang_adr[31:24];
            3'd2: led_r = hang_adr[23:16];
            3'd3: led_r = hang_adr[15:8];
            3'd4: led_r = hang_adr[7:0];
            3'd5: led_r = {5'd0, hang_aux};      // rw_n + siz
            default: led_r = 8'h00;              // blank between repeats
        endcase
    end
endmodule
`default_nettype wire
