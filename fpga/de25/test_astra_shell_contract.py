#!/usr/bin/env python3
"""Contract check for reproducible LPDDR4B integration."""

from pathlib import Path
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


tcl = (HERE / "add_lpddr4b.tcl").read_text(encoding="utf-8")
for required in (
    "remove_instance ocm",
    "add_component astra_lpddr4b",
    "add_instance astra_hps_pmon pmon 4.0.1",
    "set_instance_parameter_value astra_hps_pmon EXPORT_JTAG true",
    "set_instance_parameter_value astra_hps_pmon MONITOR_0_ADVANCED_LAT true",
    "set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_ARADDR_WIDTH 30",
    "set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_AWADDR_WIDTH 30",
    "set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_ARID_WIDTH 4",
    "set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_AWID_WIDTH 4",
    "set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_RDATA_WIDTH 128",
    "set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_WDATA_WIDTH 128",
    "add_connection subsys_hps.hps2fpga astra_hps_pmon.sink_axi4",
    "subsys_hps.hps2fpga/astra_hps_pmon.sink_axi4 baseAddress 0x40000000",
    "add_connection astra_hps_pmon.src_axi4 astra_lpddr4b.s0_axi4",
    "astra_hps_pmon.src_axi4/astra_lpddr4b.s0_axi4 baseAddress 0x0",
    "add_connection clk_100.out_clk astra_lpddr4b.s0_axi4_clock_in",
    "add_connection clk_100.out_clk astra_lpddr4b.s0_axi4lite_clock",
    "add_connection rst_in.out_reset astra_lpddr4b.s0_axi4lite_reset_n",
    "add_instance astra_lpddr4b_status altera_avalon_pio",
    "set_instance_parameter_value astra_lpddr4b_status width 3",
    "set_instance_parameter_value astra_lpddr4b_status direction Input",
    "add_connection subsys_hps.lwhps2fpga astra_lpddr4b_status.s1",
    "subsys_hps.lwhps2fpga/astra_lpddr4b_status.s1 baseAddress 0x20000",
    "add_connection clk_100.out_clk astra_lpddr4b_status.clk",
    "add_connection rst_in.out_reset astra_lpddr4b_status.reset",
    "astra_lpddr4b_calibration axi4lite end",
    "astra_lpddr4b_status conduit end",
    "astra_lpddr4b_mem conduit end",
    "add_instance astra_control_bridge altera_axi_bridge",
    "set_instance_parameter_value astra_control_bridge ADDR_WIDTH 16",
    "subsys_hps.lwhps2fpga astra_control_bridge.s0",
    "subsys_hps.lwhps2fpga/astra_control_bridge.s0 baseAddress 0x100000",
    "astra_control axi4lite start",
    "proc add_memory_bridge",
    "add_connection $name.m0 astra_lpddr4b.s0_axi4",
    "add_memory_bridge astra_fb_bridge astra_fb 1 0 1",
    "add_memory_bridge astra_scene_bridge astra_scene 1 0 2",
    "add_memory_bridge astra_render_bridge astra_render 1 1 3",
):
    assert required in tcl, required
assert tcl.count("qsys_mm.enableOutOfOrderSupport true") == 2
assert "subsys_debug.fpga_m_master/astra_control_bridge.s0" not in tcl
assert "subsys_hps.hps2fpga astra_lpddr4b.s0_axi4" not in tcl

build = (HERE / "build_astra_shell.sh").read_text(encoding="utf-8")
build += (HERE / "build_common.sh").read_text(encoding="utf-8")
for required in (
    "check_platform.py",
    "--resource",
    "unzip -p",
    "sha256sum -c",
    "add_lpddr4b.tcl",
    "patch_vendor_top.py",
    "compile_de25_tree",
    "golden_top_hps.sof",
    "golden_top_boot.core.rbf",
    "astra68.hps.jic",
    "flash_loader=A5EB013BB23BE4SCS",
    "device=MT25QU128",
    "mode=ASX4",
    "golden_top.tq.drc.signoff.rpt",
    "astra_de25.dawf",
    "require_zero_high_severity",
    "-o hps=1",
    "astra_de25_graphics.sv",
    "video_timing.sv",
    "I2C_HDMI_Config.v",
    "sys_pll.ip",
    "av_pll.ip",
    "astra-shell.failed",
    "mv \"$incoming\" \"$out\"",
):
    assert required in build, required

boot_rbf = build.index("output_files/golden_top_boot.rbf")
boot_conversion = build[boot_rbf - 160:boot_rbf + 120]
assert "output_files/golden_top.sof" in boot_conversion
assert "-o hps_path=u-boot-spl-dtb.hex" in boot_conversion
assert "output_files/golden_top_hps.sof" not in boot_conversion

jic = build.index("output_files/astra68.hps.jic")
jic_conversion = build[jic - 160:jic + 220]
assert "output_files/golden_top_hps.sof" in jic_conversion
assert "flash_loader=A5EB013BB23BE4SCS" in jic_conversion

waivers = (HERE / "astra_de25.dawf").read_text(encoding="utf-8")
for required in (
    "CDC-50012",
    "TMC-20011",
    "TMC-20012",
    "pixel_line_visual_q",
    "Port =~ 'LED['",
    "Port == 'HDMI_I2C_SCL'",
    "Port == 'HDMI_SCLK'",
    "Port == 'HDMI_TX_INT'",
    "-stages {{Timing Signoff}}",
):
    assert required in waivers, required

for relative, reset_blocks in (
    ("fpga/arty/graphics/astra_boot_text_overlay.sv", (
        "always @(posedge build_clk or posedge build_reset)",
        "always @(posedge pixel_clk or posedge pixel_reset)",
    )),
    ("fpga/arty/graphics/astra_palette_store.sv", (
        "always @(posedge control_clk or posedge control_reset)",
    )),
    ("fpga/arty/graphics/astra_line_scheduler.sv", (
        "always @(posedge build_clk or posedge build_reset)",
        "always @(posedge pixel_clk or posedge pixel_reset)",
    )),
):
    source = (ROOT / relative).read_text(encoding="utf-8")
    for reset_block in reset_blocks:
        assert reset_block in source, f"{relative}: {reset_block}"

with tempfile.TemporaryDirectory() as report_dir_text:
    report_dir = Path(report_dir_text)
    good_report = report_dir / "good.rpt"
    bad_report = report_dir / "bad.rpt"
    good_report.write_text(
        "; Rule ; Severity ; Violations ; Waived ;\n"
        "; CLK-1 ; High ; 0 ; 0 ;\n"
        "; TMC-1 ; Medium ; 3 ; 0 ;\n",
        encoding="utf-8",
    )
    bad_report.write_text(
        "; Rule ; Severity ; Violations ; Waived ;\n"
        "; CLK-1 ; High ; 1 ; 0 ;\n",
        encoding="utf-8",
    )
    check = '. "$1"; require_zero_high_severity "$2"'
    common = str(HERE / "build_common.sh")
    assert subprocess.run(
        ["bash", "-c", check, "_", common, str(good_report)],
        capture_output=True,
    ).returncode == 0
    assert subprocess.run(
        ["bash", "-c", check, "_", common, str(bad_report)],
        capture_output=True,
    ).returncode != 0

graphics = (HERE / "astra_de25_graphics.sv").read_text(encoding="utf-8")
for required in (
    "module astra_de25_graphics",
    "sys_pll pixel_pll_i",
    "av_pll audio_pll_i",
    "video_timing #(",
    "astra_graphics_pipeline #(",
    ".framebuffer_axi_debug_status(framebuffer_axi_debug_status)",
    ".ARENA_BASE(32'h40000000)",
    ".ARENA_LIMIT(32'h80000000)",
    ".AXI_ID_WIDTH(3)",
    "astra_axi_read_3to1 #(",
    "astra_hdmi_audio audio_i",
    "astra_i2s_transmitter i2s_i",
    ".sample_clk(audio_sample_clk)",
    "astra_front_panel_axi #(",
    "I2C_HDMI_Config hdmi_config_i",
    "always @(negedge pixel_clk)",
    "hdmi_int_sync_q",
):
    assert required in graphics, required

assert "altsource_probe" not in graphics

with tempfile.TemporaryDirectory() as temporary_text:
    temporary = Path(temporary_text)
    top = temporary / "golden_top.v"
    qsf = temporary / "golden_top.qsf"
    timing = temporary / "ghrd_timing.sdc"
    top.write_text(
        "//`define ENABLE_LPDDR4B\n"
        "wire system_clk_100_internal;\n"
        "wire system_reset_n;\n"
        "assign                 fpga_led_pio = {heartbeat_led,fpga_led_internal};\n"
        "    qsys_top u0 (\n"
        "        .f2h_irq1_in_irq                       (),\n"
        "        .button_pio_external_connection_export (buttons)\n"
        "    );\n",
        encoding="utf-8",
    )
    qsf.write_text(
        "set_global_assignment -name SDC_FILE ghrd_timing.sdc\n"
        "set_global_assignment -name VERILOG_FILE golden_top.v\n",
        encoding="utf-8",
    )
    timing.write_text(
        "create_clock -name MAIN_CLOCK2 -period 20.0 -add "
        "[get_ports CLOCK2_50]\n"
        "create_clock -name LED_CLOCK_vir -period 100.0\n"
        "set_clock_latency -source 2.5 LED_CLOCK_vir\n"
        "set_output_delay -clock LED_CLOCK_vir -max 0 [get_ports {LED[*]}]\n"
        "set_output_delay -clock LED_CLOCK_vir -min 0 [get_ports {LED[*]}]\n",
        encoding="utf-8",
    )
    command = [sys.executable, str(HERE / "patch_vendor_top.py"),
               str(top), str(qsf), str(timing)]
    passed = subprocess.run(command, text=True, capture_output=True)
    assert passed.returncode == 0, passed.stderr
    patched_top = top.read_text(encoding="utf-8")
    patched_qsf = qsf.read_text(encoding="utf-8")
    patched_timing = timing.read_text(encoding="utf-8")
    assert "`define ENABLE_LPDDR4B" in patched_top
    assert "astra_lpddr4b_calibration_driver" in patched_top
    assert "astra_lpddr4b_cal_failed" in patched_top
    assert "astra_lpddr4b_calibration_status" in patched_top
    assert ".astra_lpddr4b_core_init_n_reset_n" in patched_top
    assert ".astra_lpddr4b_status_export" in patched_top
    assert ".astra_lpddr4b_mem_mem_dq" in patched_top
    assert "astra_de25_graphics graphics_i" in patched_top
    assert ".astra_control_awaddr" in patched_top
    assert ".astra_fb_araddr" in patched_top
    assert ".astra_scene_araddr" in patched_top
    assert ".astra_render_awaddr" in patched_top
    assert ".hdmi_tx_d(HDMI_TX_D)" in patched_top
    assert "fpga_led_pio = astra_leds" in patched_top
    assert "{31'd0, astra_render_interrupt}" in patched_top
    assert patched_top.count("astra_lpddr4b_calibration_driver") == 1
    assert patched_qsf.count("SYSTEMVERILOG_FILE axil_driver_calibration.sv") == 1
    assert patched_qsf.count(
        'set_global_assignment -name HPS_INITIALIZATION "HPS First"'
    ) == 1
    assert patched_qsf.count(
        "set_global_assignment -name DESIGN_ASSISTANT_WAIVER_FILE "
        "astra_de25.dawf"
    ) == 1
    assert patched_qsf.count(
        "set_global_assignment -name SDC_FILE ghrd_timing.sdc"
    ) == 1
    assert patched_qsf.rstrip().endswith(
        "set_global_assignment -name SDC_FILE ghrd_timing.sdc"
    )
    assert "create_clock -name MAIN_CLOCK2" not in patched_timing
    assert (
        "create_generated_clock -name ASTRA_HDMI_I2C_CLOCK "
        "-source [get_pins {graphics_i|hdmi_config_i|mI2C_CTRL_CLK|clk}] "
        "-divide_by 5002 "
        "[get_registers {graphics_i|hdmi_config_i|mI2C_CTRL_CLK}]"
    ) in patched_timing
    assert (
        "create_generated_clock -name ASTRA_AUDIO_SAMPLE_CLOCK "
        "-source [get_pins {graphics_i|i2s_i|sample_clk|clk}] "
        "-divide_by 256 [get_registers {graphics_i|i2s_i|sample_clk}]"
    ) in patched_timing
    assert (
        "create_generated_clock -name ASTRA_HDMI_PIXEL_CLOCK "
        "-source [get_clock_info -targets [get_clocks "
        "{graphics_i|pixel_pll_i|iopll_0_outclk0}]] -divide_by 1 "
        "[get_ports HDMI_TX_CLK]"
    ) in patched_timing
    assert (
        "create_generated_clock -name ASTRA_HDMI_MASTER_CLOCK "
        "-source [get_clock_info -targets [get_clocks "
        "{graphics_i|audio_pll_i|iopll_0_outclk0}]] -divide_by 1 "
        "[get_ports HDMI_MCLK]"
    ) in patched_timing
    assert (
        "create_generated_clock -name ASTRA_AUDIO_BIT_CLOCK "
        "-source [get_pins {graphics_i|i2s_i|bclk|clk}] -divide_by 4 "
        "[get_registers {graphics_i|i2s_i|bclk}]"
    ) in patched_timing
    assert (
        "set_clock_groups -asynchronous "
        "-group [get_clocks {pll_inst|iopll_0_outclk0}]"
    ) in patched_timing
    assert "SYNCHRONIZATION_REGISTER_CHAIN_LENGTH 1" not in patched_qsf
    assert (
        "SYNCHRONIZATION_REGISTER_CHAIN_LENGTH 3 -to "
        '"synchronize_async_rst|mka_rst_meta0"'
    ) in patched_qsf
    for cdc_constraint in (
        "set_max_skew -from $source -to $destination",
        "set_data_delay -from $source -to $destination",
        "set_net_delay -from $gray_sources -to $gray_heads -max",
        "pixel_pll_i*pll_ctrl_reg",
        "audio_pll_i*pll_ctrl_reg",
        "synchronize_async_rst|*|clrn",
        "set sample_clock [get_clocks {ASTRA_AUDIO_SAMPLE_CLOCK}]",
    ):
        assert cdc_constraint in patched_timing, cdc_constraint
    assert "set_false_path -to [get_ports {LED[*]}]" in patched_timing
    assert "LED_CLOCK_vir" not in patched_timing
    for interface_constraint in (
        "create_generated_clock -name ASTRA_HDMI_PIXEL_CLOCK",
        "set_output_delay -clock ASTRA_HDMI_PIXEL_CLOCK -max 1.8",
        "set_output_delay -clock ASTRA_HDMI_PIXEL_CLOCK -min -1.3",
        "create_generated_clock -name ASTRA_HDMI_AUDIO_CLOCK",
        "create_generated_clock -name ASTRA_HDMI_MASTER_CLOCK",
        "set_output_delay -clock ASTRA_HDMI_AUDIO_CLOCK -max 2.0",
        "set_output_delay -clock ASTRA_HDMI_AUDIO_CLOCK -min -2.0",
        "create_generated_clock -name ASTRA_HDMI_I2C_BUS_CLOCK",
        "set_output_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -max 100.0",
        "set_output_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -min -100.0",
        "set_input_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -max 3450.0",
        "set_input_delay -clock ASTRA_HDMI_I2C_BUS_CLOCK -min 0.0",
        "set_false_path -from [get_ports HDMI_TX_INT]",
    ):
        assert interface_constraint in patched_timing, interface_constraint

    rejected = subprocess.run(command, text=True, capture_output=True)
    assert rejected.returncode != 0
    assert "already patched" in rejected.stderr

print("DE25 Astra shell contract: PASS")
