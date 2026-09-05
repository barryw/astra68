#!/usr/bin/env python3
"""Connect the pinned LPDDR4B component to the vendor GHRD top level."""

import argparse
from pathlib import Path


MARKER = "// ASTRA LPDDR4B BEGIN"

CALIBRATION = r'''// ASTRA LPDDR4B BEGIN
wire [26:0] astra_lpddr4b_awaddr;
wire [2:0]  astra_lpddr4b_awprot;
wire        astra_lpddr4b_awvalid;
wire        astra_lpddr4b_awready;
wire [26:0] astra_lpddr4b_araddr;
wire [2:0]  astra_lpddr4b_arprot;
wire        astra_lpddr4b_arvalid;
wire        astra_lpddr4b_arready;
wire [31:0] astra_lpddr4b_wdata;
wire [3:0]  astra_lpddr4b_wstrb;
wire        astra_lpddr4b_wvalid;
wire        astra_lpddr4b_wready;
wire        astra_lpddr4b_bready;
wire [1:0]  astra_lpddr4b_bresp;
wire        astra_lpddr4b_bvalid;
wire        astra_lpddr4b_rready;
wire [31:0] astra_lpddr4b_rdata;
wire [1:0]  astra_lpddr4b_rresp;
wire        astra_lpddr4b_rvalid;
wire        astra_lpddr4b_cal_done_n;
wire        astra_lpddr4b_ctrl_ready;
reg         astra_lpddr4b_cal_failed;
wire [2:0]  astra_lpddr4b_calibration_status;

// bit 0: calibration passed; bit 1: calibration failed; bit 2: data path ready.
assign astra_lpddr4b_calibration_status = {
    astra_lpddr4b_ctrl_ready,
    astra_lpddr4b_cal_failed,
    astra_lpddr4b_cal_done_n
};

always @(posedge system_clk_100_internal or negedge system_reset_n) begin
    if (!system_reset_n)
        astra_lpddr4b_cal_failed <= 1'b0;
    else if (astra_lpddr4b_rvalid)
        astra_lpddr4b_cal_failed <= astra_lpddr4b_rdata[1];
end

axil_driver_calibration #(
    .AXIL_DRIVER_ADDRESS_WIDTH (27)
) astra_lpddr4b_calibration_driver (
    .axil_driver_clk     (system_clk_100_internal),
    .axil_driver_rst_n   (system_reset_n),
    .axil_driver_araddr  (astra_lpddr4b_araddr),
    .axil_driver_arprot  (astra_lpddr4b_arprot),
    .axil_driver_arvalid (astra_lpddr4b_arvalid),
    .axil_driver_arready (astra_lpddr4b_arready),
    .axil_driver_rdata   (astra_lpddr4b_rdata),
    .axil_driver_rresp   (astra_lpddr4b_rresp),
    .axil_driver_rvalid  (astra_lpddr4b_rvalid),
    .axil_driver_rready  (astra_lpddr4b_rready),
    .axil_driver_awaddr  (astra_lpddr4b_awaddr),
    .axil_driver_awprot  (astra_lpddr4b_awprot),
    .axil_driver_awvalid (astra_lpddr4b_awvalid),
    .axil_driver_awready (astra_lpddr4b_awready),
    .axil_driver_wdata   (astra_lpddr4b_wdata),
    .axil_driver_wstrb   (astra_lpddr4b_wstrb),
    .axil_driver_wvalid  (astra_lpddr4b_wvalid),
    .axil_driver_wready  (astra_lpddr4b_wready),
    .axil_driver_bresp   (astra_lpddr4b_bresp),
    .axil_driver_bvalid  (astra_lpddr4b_bvalid),
    .axil_driver_bready  (astra_lpddr4b_bready),
    .cal_done_rst_n      (astra_lpddr4b_cal_done_n)
);
// ASTRA LPDDR4B END

'''

GRAPHICS = r'''wire [7:0] astra_leds;
wire astra_render_interrupt;
wire [15:0] astra_control_awaddr;
wire [2:0] astra_control_awprot;
wire astra_control_awvalid, astra_control_awready;
wire [31:0] astra_control_wdata;
wire [3:0] astra_control_wstrb;
wire astra_control_wvalid, astra_control_wready;
wire [1:0] astra_control_bresp;
wire astra_control_bvalid, astra_control_bready;
wire [15:0] astra_control_araddr;
wire [2:0] astra_control_arprot;
wire astra_control_arvalid, astra_control_arready;
wire [31:0] astra_control_rdata;
wire [1:0] astra_control_rresp;
wire astra_control_rvalid, astra_control_rready;
wire astra_fb_arid, astra_fb_arvalid, astra_fb_arready;
wire [31:0] astra_fb_araddr;
wire [7:0] astra_fb_arlen;
wire [2:0] astra_fb_arsize, astra_fb_arprot;
wire [1:0] astra_fb_arburst, astra_fb_rresp;
wire [3:0] astra_fb_arcache;
wire astra_fb_rid, astra_fb_rlast, astra_fb_rvalid, astra_fb_rready;
wire [63:0] astra_fb_rdata;
wire [1:0] astra_scene_arid, astra_scene_arburst, astra_scene_rid;
wire [31:0] astra_scene_araddr;
wire [7:0] astra_scene_arlen;
wire [2:0] astra_scene_arsize, astra_scene_arprot;
wire [3:0] astra_scene_arcache;
wire astra_scene_arvalid, astra_scene_arready;
wire [63:0] astra_scene_rdata;
wire [1:0] astra_scene_rresp;
wire astra_scene_rlast, astra_scene_rvalid, astra_scene_rready;
wire [2:0] astra_render_awid, astra_render_awsize;
wire [31:0] astra_render_awaddr;
wire [7:0] astra_render_awlen;
wire [1:0] astra_render_awburst, astra_render_bresp;
wire [3:0] astra_render_awcache;
wire [2:0] astra_render_awprot;
wire astra_render_awvalid, astra_render_awready;
wire [63:0] astra_render_wdata;
wire [7:0] astra_render_wstrb;
wire astra_render_wlast, astra_render_wvalid, astra_render_wready;
wire [2:0] astra_render_bid;
wire astra_render_bvalid, astra_render_bready;
wire [2:0] astra_render_arid, astra_render_arsize, astra_render_arprot;
wire [31:0] astra_render_araddr;
wire [7:0] astra_render_arlen;
wire [1:0] astra_render_arburst, astra_render_rresp;
wire [3:0] astra_render_arcache;
wire astra_render_arvalid, astra_render_arready;
wire [2:0] astra_render_rid;
wire [63:0] astra_render_rdata;
wire astra_render_rlast, astra_render_rvalid, astra_render_rready;

astra_de25_graphics graphics_i (
    .clock_50(system_clk_50), .build_clk(system_clk_100_internal),
    .reset_n(system_reset_n), .buttons(fpga_debounced_buttons),
    .switches(fpga_dipsw_pio), .leds(astra_leds),
    .render_interrupt(astra_render_interrupt),
    .hdmi_tx_clk(HDMI_TX_CLK), .hdmi_tx_hs(HDMI_TX_HS),
    .hdmi_tx_vs(HDMI_TX_VS), .hdmi_tx_d(HDMI_TX_D),
    .hdmi_tx_de(HDMI_TX_DE), .hdmi_lrclk(HDMI_LRCLK),
    .hdmi_mclk(HDMI_MCLK), .hdmi_sclk(HDMI_SCLK),
    .hdmi_i2s(HDMI_I2S), .hdmi_i2c_scl(HDMI_I2C_SCL),
    .hdmi_i2c_sda(HDMI_I2C_SDA), .hdmi_tx_int(HDMI_TX_INT),
    .control_awaddr(astra_control_awaddr),
    .control_awprot(astra_control_awprot),
    .control_awvalid(astra_control_awvalid),
    .control_awready(astra_control_awready),
    .control_wdata(astra_control_wdata),
    .control_wstrb(astra_control_wstrb),
    .control_wvalid(astra_control_wvalid),
    .control_wready(astra_control_wready),
    .control_bresp(astra_control_bresp),
    .control_bvalid(astra_control_bvalid),
    .control_bready(astra_control_bready),
    .control_araddr(astra_control_araddr),
    .control_arprot(astra_control_arprot),
    .control_arvalid(astra_control_arvalid),
    .control_arready(astra_control_arready),
    .control_rdata(astra_control_rdata),
    .control_rresp(astra_control_rresp),
    .control_rvalid(astra_control_rvalid),
    .control_rready(astra_control_rready),
    .fb_arid(astra_fb_arid), .fb_araddr(astra_fb_araddr),
    .fb_arlen(astra_fb_arlen), .fb_arsize(astra_fb_arsize),
    .fb_arburst(astra_fb_arburst), .fb_arcache(astra_fb_arcache),
    .fb_arprot(astra_fb_arprot), .fb_arvalid(astra_fb_arvalid),
    .fb_arready(astra_fb_arready), .fb_rid(astra_fb_rid),
    .fb_rdata(astra_fb_rdata), .fb_rresp(astra_fb_rresp),
    .fb_rlast(astra_fb_rlast), .fb_rvalid(astra_fb_rvalid),
    .fb_rready(astra_fb_rready),
    .scene_arid(astra_scene_arid), .scene_araddr(astra_scene_araddr),
    .scene_arlen(astra_scene_arlen), .scene_arsize(astra_scene_arsize),
    .scene_arburst(astra_scene_arburst),
    .scene_arcache(astra_scene_arcache), .scene_arprot(astra_scene_arprot),
    .scene_arvalid(astra_scene_arvalid), .scene_arready(astra_scene_arready),
    .scene_rid(astra_scene_rid), .scene_rdata(astra_scene_rdata),
    .scene_rresp(astra_scene_rresp), .scene_rlast(astra_scene_rlast),
    .scene_rvalid(astra_scene_rvalid), .scene_rready(astra_scene_rready),
    .render_awid(astra_render_awid), .render_awaddr(astra_render_awaddr),
    .render_awlen(astra_render_awlen), .render_awsize(astra_render_awsize),
    .render_awburst(astra_render_awburst),
    .render_awcache(astra_render_awcache), .render_awprot(astra_render_awprot),
    .render_awvalid(astra_render_awvalid),
    .render_awready(astra_render_awready),
    .render_wdata(astra_render_wdata), .render_wstrb(astra_render_wstrb),
    .render_wlast(astra_render_wlast), .render_wvalid(astra_render_wvalid),
    .render_wready(astra_render_wready), .render_bid(astra_render_bid),
    .render_bresp(astra_render_bresp), .render_bvalid(astra_render_bvalid),
    .render_bready(astra_render_bready), .render_arid(astra_render_arid),
    .render_araddr(astra_render_araddr), .render_arlen(astra_render_arlen),
    .render_arsize(astra_render_arsize),
    .render_arburst(astra_render_arburst),
    .render_arcache(astra_render_arcache), .render_arprot(astra_render_arprot),
    .render_arvalid(astra_render_arvalid),
    .render_arready(astra_render_arready), .render_rid(astra_render_rid),
    .render_rdata(astra_render_rdata), .render_rresp(astra_render_rresp),
    .render_rlast(astra_render_rlast), .render_rvalid(astra_render_rvalid),
    .render_rready(astra_render_rready)
);

'''

PORTS = r'''        .astra_control_awaddr                   (astra_control_awaddr),
        .astra_control_awprot                   (astra_control_awprot),
        .astra_control_awvalid                  (astra_control_awvalid),
        .astra_control_awready                  (astra_control_awready),
        .astra_control_wdata                    (astra_control_wdata),
        .astra_control_wstrb                    (astra_control_wstrb),
        .astra_control_wvalid                   (astra_control_wvalid),
        .astra_control_wready                   (astra_control_wready),
        .astra_control_bresp                    (astra_control_bresp),
        .astra_control_bvalid                   (astra_control_bvalid),
        .astra_control_bready                   (astra_control_bready),
        .astra_control_araddr                   (astra_control_araddr),
        .astra_control_arprot                   (astra_control_arprot),
        .astra_control_arvalid                  (astra_control_arvalid),
        .astra_control_arready                  (astra_control_arready),
        .astra_control_rdata                    (astra_control_rdata),
        .astra_control_rresp                    (astra_control_rresp),
        .astra_control_rvalid                   (astra_control_rvalid),
        .astra_control_rready                   (astra_control_rready),
        .astra_fb_arid                          (astra_fb_arid),
        .astra_fb_araddr                        (astra_fb_araddr),
        .astra_fb_arlen                         (astra_fb_arlen),
        .astra_fb_arsize                        (astra_fb_arsize),
        .astra_fb_arburst                       (astra_fb_arburst),
        .astra_fb_arlock                        (1'b0),
        .astra_fb_arcache                       (astra_fb_arcache),
        .astra_fb_arprot                        (astra_fb_arprot),
        .astra_fb_arvalid                       (astra_fb_arvalid),
        .astra_fb_arready                       (astra_fb_arready),
        .astra_fb_rid                           (astra_fb_rid),
        .astra_fb_rdata                         (astra_fb_rdata),
        .astra_fb_rresp                         (astra_fb_rresp),
        .astra_fb_rlast                         (astra_fb_rlast),
        .astra_fb_rvalid                        (astra_fb_rvalid),
        .astra_fb_rready                        (astra_fb_rready),
        .astra_scene_arid                       (astra_scene_arid),
        .astra_scene_araddr                     (astra_scene_araddr),
        .astra_scene_arlen                      (astra_scene_arlen),
        .astra_scene_arsize                     (astra_scene_arsize),
        .astra_scene_arburst                    (astra_scene_arburst),
        .astra_scene_arlock                     (1'b0),
        .astra_scene_arcache                    (astra_scene_arcache),
        .astra_scene_arprot                     (astra_scene_arprot),
        .astra_scene_arvalid                    (astra_scene_arvalid),
        .astra_scene_arready                    (astra_scene_arready),
        .astra_scene_rid                        (astra_scene_rid),
        .astra_scene_rdata                      (astra_scene_rdata),
        .astra_scene_rresp                      (astra_scene_rresp),
        .astra_scene_rlast                      (astra_scene_rlast),
        .astra_scene_rvalid                     (astra_scene_rvalid),
        .astra_scene_rready                     (astra_scene_rready),
        .astra_render_awid                      (astra_render_awid),
        .astra_render_awaddr                    (astra_render_awaddr),
        .astra_render_awlen                     (astra_render_awlen),
        .astra_render_awsize                    (astra_render_awsize),
        .astra_render_awburst                   (astra_render_awburst),
        .astra_render_awlock                    (1'b0),
        .astra_render_awcache                   (astra_render_awcache),
        .astra_render_awprot                    (astra_render_awprot),
        .astra_render_awvalid                   (astra_render_awvalid),
        .astra_render_awready                   (astra_render_awready),
        .astra_render_wdata                     (astra_render_wdata),
        .astra_render_wstrb                     (astra_render_wstrb),
        .astra_render_wlast                     (astra_render_wlast),
        .astra_render_wvalid                    (astra_render_wvalid),
        .astra_render_wready                    (astra_render_wready),
        .astra_render_bid                       (astra_render_bid),
        .astra_render_bresp                     (astra_render_bresp),
        .astra_render_bvalid                    (astra_render_bvalid),
        .astra_render_bready                    (astra_render_bready),
        .astra_render_arid                      (astra_render_arid),
        .astra_render_araddr                    (astra_render_araddr),
        .astra_render_arlen                     (astra_render_arlen),
        .astra_render_arsize                    (astra_render_arsize),
        .astra_render_arburst                   (astra_render_arburst),
        .astra_render_arlock                    (1'b0),
        .astra_render_arcache                   (astra_render_arcache),
        .astra_render_arprot                    (astra_render_arprot),
        .astra_render_arvalid                   (astra_render_arvalid),
        .astra_render_arready                   (astra_render_arready),
        .astra_render_rid                       (astra_render_rid),
        .astra_render_rdata                     (astra_render_rdata),
        .astra_render_rresp                     (astra_render_rresp),
        .astra_render_rlast                     (astra_render_rlast),
        .astra_render_rvalid                    (astra_render_rvalid),
        .astra_render_rready                    (astra_render_rready),
        .astra_lpddr4b_core_init_n_reset_n     (astra_lpddr4b_cal_done_n),
        .astra_lpddr4b_ctrl_ready_reset_n      (astra_lpddr4b_ctrl_ready),
        .astra_lpddr4b_status_export            (astra_lpddr4b_calibration_status),
        .astra_lpddr4b_calibration_awaddr      (astra_lpddr4b_awaddr),
        .astra_lpddr4b_calibration_awprot      (astra_lpddr4b_awprot),
        .astra_lpddr4b_calibration_awvalid     (astra_lpddr4b_awvalid),
        .astra_lpddr4b_calibration_awready     (astra_lpddr4b_awready),
        .astra_lpddr4b_calibration_araddr      (astra_lpddr4b_araddr),
        .astra_lpddr4b_calibration_arprot      (astra_lpddr4b_arprot),
        .astra_lpddr4b_calibration_arvalid     (astra_lpddr4b_arvalid),
        .astra_lpddr4b_calibration_arready     (astra_lpddr4b_arready),
        .astra_lpddr4b_calibration_wdata       (astra_lpddr4b_wdata),
        .astra_lpddr4b_calibration_wstrb       (astra_lpddr4b_wstrb),
        .astra_lpddr4b_calibration_wvalid      (astra_lpddr4b_wvalid),
        .astra_lpddr4b_calibration_wready      (astra_lpddr4b_wready),
        .astra_lpddr4b_calibration_bready      (astra_lpddr4b_bready),
        .astra_lpddr4b_calibration_bresp       (astra_lpddr4b_bresp),
        .astra_lpddr4b_calibration_bvalid      (astra_lpddr4b_bvalid),
        .astra_lpddr4b_calibration_rready      (astra_lpddr4b_rready),
        .astra_lpddr4b_calibration_rdata       (astra_lpddr4b_rdata),
        .astra_lpddr4b_calibration_rresp       (astra_lpddr4b_rresp),
        .astra_lpddr4b_calibration_rvalid      (astra_lpddr4b_rvalid),
        .astra_lpddr4b_mem_mem_cs              (LPDDR4B_CS_n),
        .astra_lpddr4b_mem_mem_ca              (LPDDR4B_CA),
        .astra_lpddr4b_mem_mem_cke             (LPDDR4B_CKE),
        .astra_lpddr4b_mem_mem_dq              (LPDDR4B_DQ),
        .astra_lpddr4b_mem_mem_dqs_t           (LPDDR4B_DQS),
        .astra_lpddr4b_mem_mem_dqs_c           (LPDDR4B_DQS_n),
        .astra_lpddr4b_mem_mem_dmi             (LPDDR4B_DM),
        .astra_lpddr4b_mem_ck_mem_ck_t         (LPDDR4B_CK),
        .astra_lpddr4b_mem_ck_mem_ck_c         (LPDDR4B_CK_n),
        .astra_lpddr4b_mem_reset_n_mem_reset_n (LPDDR4B_RESET_n),
        .astra_lpddr4b_oct_oct_rzqin           (LPDDR4B_RZQ),
        .astra_lpddr4b_ref_clk_clk             (LPDDR4B_REFCLK_p),
'''


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("top", type=Path)
    parser.add_argument("qsf", type=Path)
    parser.add_argument("timing", type=Path)
    args = parser.parse_args()
    top = args.top.read_text(encoding="utf-8")
    qsf = args.qsf.read_text(encoding="utf-8")
    timing = args.timing.read_text(encoding="utf-8")
    if MARKER in top or "SYSTEMVERILOG_FILE axil_driver_calibration.sv" in qsf:
        raise SystemExit("vendor top is already patched")

    top = replace_once(top, "//`define ENABLE_LPDDR4B",
                       "`define ENABLE_LPDDR4B", "LPDDR4B define")
    top = replace_once(top, "    qsys_top u0 (",
                       CALIBRATION + GRAPHICS + "    qsys_top u0 (",
                       "qsys instance")
    top = replace_once(top,
                       "        .button_pio_external_connection_export",
                       PORTS + "        .button_pio_external_connection_export",
                       "qsys port list")
    top = replace_once(
        top, ".f2h_irq1_in_irq                       (),",
        ".f2h_irq1_in_irq                       ({31'd0, astra_render_interrupt}),",
        "render interrupt",
    )
    top = replace_once(
        top, "assign                 fpga_led_pio = {heartbeat_led,fpga_led_internal};",
        "assign                 fpga_led_pio = astra_leds;",
        "front-panel LEDs",
    )
    qsf = replace_once(
        qsf, "set_global_assignment -name VERILOG_FILE golden_top.v",
        'set_global_assignment -name HPS_INITIALIZATION "HPS First"\n'
        "set_global_assignment -name DESIGN_ASSISTANT_WAIVER_FILE "
        "astra_de25.dawf\n"
        "set_instance_assignment -name "
        "SYNCHRONIZATION_REGISTER_CHAIN_LENGTH 3 -to "
        "\"synchronize_async_rst|mka_rst_meta0\"\n"
        "set_global_assignment -name SYSTEMVERILOG_FILE axil_driver_calibration.sv\n"
        "set_global_assignment -name IP_FILE ip/qsys_top/astra_lpddr4b.ip\n"
        "set_global_assignment -name VERILOG_FILE golden_top.v",
        "top-level source",
    )
    sdc_assignment = "set_global_assignment -name SDC_FILE ghrd_timing.sdc"
    qsf = replace_once(qsf, sdc_assignment + "\n", "", "timing assignment")
    qsf = qsf.rstrip() + "\n" + sdc_assignment + "\n"
    timing = replace_once(
        timing,
        "create_clock -name MAIN_CLOCK2 -period 20.0 -add "
        "[get_ports CLOCK2_50]",
        "create_generated_clock -name ASTRA_HDMI_I2C_CLOCK "
        "-source [get_pins {graphics_i|hdmi_config_i|mI2C_CTRL_CLK|clk}] "
        "-divide_by 5002 "
        "[get_registers {graphics_i|hdmi_config_i|mI2C_CTRL_CLK}]\n"
        "create_generated_clock -name ASTRA_AUDIO_SAMPLE_CLOCK "
        "-source [get_pins {graphics_i|i2s_i|sample_clk|clk}] "
        "-divide_by 256 "
        "[get_registers {graphics_i|i2s_i|sample_clk}]\n"
        "create_generated_clock -name ASTRA_HDMI_MASTER_CLOCK "
        "-source [get_clock_info -targets [get_clocks "
        "{graphics_i|audio_pll_i|iopll_0_outclk0}]] -divide_by 1 "
        "[get_ports HDMI_MCLK]\n"
        "\n"
        "# ADV7513 Rev. B input requirements, with video launched on the "
        "falling pixel edge.\n"
        "create_generated_clock -name ASTRA_HDMI_PIXEL_CLOCK "
        "-source [get_clock_info -targets [get_clocks "
        "{graphics_i|pixel_pll_i|iopll_0_outclk0}]] -divide_by 1 "
        "[get_ports HDMI_TX_CLK]\n"
        "set_output_delay -clock ASTRA_HDMI_PIXEL_CLOCK -max 1.8 "
        "[get_ports {HDMI_TX_D[*] HDMI_TX_DE HDMI_TX_HS HDMI_TX_VS}]\n"
        "set_output_delay -clock ASTRA_HDMI_PIXEL_CLOCK -min -1.3 "
        "[get_ports {HDMI_TX_D[*] HDMI_TX_DE HDMI_TX_HS HDMI_TX_VS}]\n"
        "create_generated_clock -name ASTRA_AUDIO_BIT_CLOCK "
        "-source [get_pins {graphics_i|i2s_i|bclk|clk}] -divide_by 4 "
        "[get_registers {graphics_i|i2s_i|bclk}]\n"
        "create_generated_clock -name ASTRA_HDMI_AUDIO_CLOCK "
        "-source [get_pins {graphics_i|i2s_i|bclk|q}] -divide_by 1 "
        "[get_ports HDMI_SCLK]\n"
        "set_output_delay -clock ASTRA_HDMI_AUDIO_CLOCK -max 2.0 "
        "[get_ports {HDMI_I2S HDMI_LRCLK}]\n"
        "set_output_delay -clock ASTRA_HDMI_AUDIO_CLOCK -min -2.0 "
        "[get_ports {HDMI_I2S HDMI_LRCLK}]\n"
        "\n"
        "# Standard-mode I2C: 100 ns receiver setup/hold and 3.45 us "
        "maximum slave data-valid delay.\n"
        "create_generated_clock -name ASTRA_HDMI_I2C_BUS_CLOCK "
        "-source [get_pins "
        "{graphics_i|hdmi_config_i|mI2C_CTRL_CLK|q}] "
        "-divide_by 1 -invert "
        "[get_ports HDMI_I2C_SCL]\n"
        "set_output_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -max 100.0 "
        "[get_ports HDMI_I2C_SDA]\n"
        "set_output_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -min -100.0 "
        "[get_ports HDMI_I2C_SDA]\n"
        "set_input_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -max 3450.0 "
        "[get_ports HDMI_I2C_SDA]\n"
        "set_input_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -min 0.0 "
        "[get_ports HDMI_I2C_SDA]\n"
        "set_clock_groups -asynchronous "
        "-group [get_clocks {pll_inst|iopll_0_outclk0}] "
        "-group [get_clocks {graphics_i|pixel_pll_i|iopll_0_outclk0 "
        "ASTRA_HDMI_PIXEL_CLOCK}] "
        "-group [get_clocks {graphics_i|audio_pll_i|iopll_0_outclk0 "
        "ASTRA_AUDIO_BIT_CLOCK ASTRA_AUDIO_SAMPLE_CLOCK "
        "ASTRA_HDMI_AUDIO_CLOCK ASTRA_HDMI_MASTER_CLOCK}]\n"
        "set pixel_lock_source [get_registers -nowarn "
        "{*pixel_pll_i*pll_ctrl_reg}]\n"
        "set audio_lock_source [get_registers -nowarn "
        "{*audio_pll_i*pll_ctrl_reg}]\n"
        "set_false_path -from $pixel_lock_source -to "
        "[get_registers {graphics_i|pixel_reset_sync_q*}]\n"
        "set_false_path -from $audio_lock_source -to "
        "[get_registers {graphics_i|audio_reset_sync_q*}]\n"
        "set build_clock [get_clocks {pll_inst|iopll_0_outclk0}]\n"
        "set pixel_clock [get_clocks "
        "{graphics_i|pixel_pll_i|iopll_0_outclk0}]\n"
        "set audio_clock [get_clocks "
        "{graphics_i|audio_pll_i|iopll_0_outclk0}]\n"
        "set sample_clock [get_clocks {ASTRA_AUDIO_SAMPLE_CLOCK}]\n"
        "foreach pair [list [list $build_clock $pixel_clock] "
        "[list $pixel_clock $build_clock] "
        "[list $build_clock $audio_clock] "
        "[list $audio_clock $build_clock] "
        "[list $build_clock $sample_clock] "
        "[list $sample_clock $build_clock]] {\n"
        "    lassign $pair source destination\n"
        "    set_max_skew -from $source -to $destination "
        "-get_skew_value_from_clock_period min_clock_period "
        "-skew_value_multiplier 0.8\n"
        "    set_data_delay -from $source -to $destination "
        "-get_value_from_clock_period dst_clock_period "
        "-value_multiplier 0.8\n"
        "}\n"
        "set gray_sources [get_registers -nowarn "
        "{*|rd_gray* *|wr_gray*}]\n"
        "set gray_heads [get_registers -nowarn "
        "{*|rd_gray_wr_1* *|wr_gray_rd_1*}]\n"
        "set_net_delay -from $gray_sources -to $gray_heads -max "
        "-get_value_from_clock_period min_clock_period "
        "-value_multiplier 0.8\n"
        "set_false_path -through [get_pins -nowarn "
        "{synchronize_async_rst|*|clrn}]\n"
        "set_false_path -from [get_ports HDMI_TX_INT] "
        "-to [get_registers {graphics_i|hdmi_int_sync_q[0]}]",
        "50 MHz fabric reference clock",
    )
    timing = replace_once(
        timing,
        "create_clock -name LED_CLOCK_vir -period 100.0\n"
        "set_clock_latency -source 2.5 LED_CLOCK_vir",
        "# LEDs are human-visible status outputs with no external capture clock.\n"
        "set_false_path -to [get_ports {LED[*]}]",
        "LED timing model",
    )
    timing = replace_once(
        timing,
        "set_output_delay -clock LED_CLOCK_vir -max 0 [get_ports {LED[*]}]\n"
        "set_output_delay -clock LED_CLOCK_vir -min 0 [get_ports {LED[*]}]",
        "",
        "LED output delays",
    )
    args.top.write_text(top, encoding="utf-8")
    args.qsf.write_text(qsf, encoding="utf-8")
    args.timing.write_text(timing, encoding="utf-8")


if __name__ == "__main__":
    main()
