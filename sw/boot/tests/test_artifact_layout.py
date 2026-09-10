import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def test_product_artifacts_stay_in_sync_excluded_build_directories():
    boot = (ROOT / "sw/boot/Makefile").read_text()
    kernel = (ROOT / "sw/kernel/Makefile").read_text()
    splash = (ROOT / "sw/boot/splash_blob.S").read_text()
    converter = (ROOT / "sw/boot/bin2hex.py").read_text()
    arty = (ROOT / "fpga/arty/linux/Makefile").read_text()
    graphics_build = (ROOT / "fpga/arty/scripts/build_graphics.tcl").read_text()
    boot_font = ROOT / "fpga/arty/graphics/post_fonts.hex"
    agent_rules = (ROOT / "AGENTS.md").read_text()
    blank_splash = ROOT / "sw/boot/assets/astra_boot_splash_1920x1080_blank.png"

    assert "BOOT_BIN = build/astra_boot.bin" in boot
    assert "BOOT_ELF = build/astra_boot.elf" in boot
    assert "BOOT_MAP = build/astra_boot.map" in boot
    assert "astra68.rom" not in boot
    assert "SPLASH_PAYLOAD = build/astra_boot_splash.pal8.lz4" in boot
    assert "HOST_PLATFORM ?= ARTY_Z7_20" in boot
    assert "HOST=$(HOST_PLATFORM)" in boot
    assert "KERNEL_BIN = $(BUILD_DIR)/astra_kernel.bin" in kernel
    assert "KERNEL_ELF = $(BUILD_DIR)/astra_kernel.elf" in kernel
    assert "KERNEL_MAP = $(BUILD_DIR)/astra_kernel.map" in kernel
    assert '.incbin "build/astra_boot_splash.pal8.lz4"' in splash
    assert 'else "build/astra_boot.bin"' in converter
    assert "SPLASH_SOURCE := ../../../sw/boot/assets/" \
           "astra_boot_splash_1920x1080_blank.png" in arty
    assert "GRAPHICS_SOURCES := $(GRAPHICS)/src/surface.c" in arty
    assert "$(GRAPHICS)/src/render_builder.c" in arty
    assert "$(GRAPHICS_GENERATED)/astra_mono_font.inc" in arty
    assert "astra_terminal_font.inc" not in arty
    assert "tools fonts afnt.py" in graphics_build
    assert "emit-cp437-hex" in graphics_build
    assert "build fpga post_fonts.hex" in graphics_build
    assert not boot_font.exists()
    assert "--checksum --no-times" in agent_rules
    assert "--exclude '*/build'" in agent_rules
    assert hashlib.sha256(blank_splash.read_bytes()).hexdigest() == (
        "db26450ae49471e81d90d4fdf554f1dd7226cf85f402af09d3f9e7e28cd2b687"
    )
