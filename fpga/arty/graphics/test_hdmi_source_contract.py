#!/usr/bin/env python3
"""Fail a graphics build that omits the Arty Z7 HDMI source contract."""

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[3]


def require(path: str, *needles: str) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    missing = [needle for needle in needles if needle not in text]
    if missing:
        raise SystemExit(f"{path}: missing " + ", ".join(repr(x) for x in missing))


def forbid(path: str, needle: str) -> None:
    if needle in (ROOT / path).read_text(encoding="utf-8"):
        raise SystemExit(f"{path}: forbidden {needle!r}")


require(
    "fpga/arty/constraints/astra_arty_720p.xdc",
    "PACKAGE_PIN R19 IOSTANDARD LVCMOS33",
    "PACKAGE_PIN M17 IOSTANDARD LVCMOS33",
    "PACKAGE_PIN M18 IOSTANDARD LVCMOS33",
)
require(
    "fpga/arty/scripts/build_graphics.tcl",
    "CONFIG.PCW_I2C0_PERIPHERAL_ENABLE {1}",
    "CONFIG.PCW_I2C0_I2C0_IO {EMIO}",
    "CONFIG.PCW_GPIO_EMIO_GPIO_ENABLE {1}",
    "make_bd_intf_pins_external -name IIC_0 [get_bd_intf_pins ps7/IIC_0]",
    "make_bd_intf_pins_external -name GPIO_0 [get_bd_intf_pins ps7/GPIO_0]",
)
require(
    "fpga/arty/rtl/astra_arty_graphics_top.sv",
    "hdmi_tx_scl",
    "hdmi_tx_sda",
    ".GPIO_0_tri_io(hdmi_tx_hpdn)",
    ".IIC_0_scl_io(hdmi_tx_scl)",
    ".IIC_0_sda_io(hdmi_tx_sda)",
    ".hdmi_output_enable(hdmi_request_pixel_sync_q[1])",
)
forbid(
    "fpga/arty/rtl/astra_arty_graphics_top.sv",
    "unused_status = &{\n        1'b0,\n        hdmi_tx_hpdn",
)
require(
    "fpga/arty/linux/astra_hdmi_link.c",
    "GPIOHANDLE_REQUEST_ACTIVE_LOW",
    "GPIOHANDLE_GET_LINE_VALUES_IOCTL",
    "poll(&hpd_event, 1, -1)",
    "setvbuf(stdout, NULL, _IOLBF, 0)",
)
require(
    "third_party/hdl-util-hdmi/hdmi.sv",
    "input logic hdmi_output_enable",
    "hdmi_mode_control",
    "!hdmi_output_active",
)
require(
    "fpga/arty/linux/rootfs-overlay/etc/init.d/astra-firstboot",
    "/data/astra/bin/astra-hdmi-link",
)
subprocess.run(
    [sys.executable, str(ROOT / "fpga/arty/linux/test-astra-chip-reset.py")],
    check=True,
)

print("ASTRA HDMI SOURCE CONTRACT PASS")
