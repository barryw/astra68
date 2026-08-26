import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def test_product_artifacts_stay_in_sync_excluded_build_directories():
    boot = (ROOT / "sw/boot/Makefile").read_text()
    kernel = (ROOT / "sw/kernel/Makefile").read_text()
    splash = (ROOT / "sw/boot/splash_blob.S").read_text()
    converter = (ROOT / "sw/boot/bin2hex.py").read_text()
    arty = (ROOT / "fpga/arty/linux/Makefile").read_text()
    agent_rules = (ROOT / "AGENTS.md").read_text()
    blank_splash = ROOT / "sw/boot/assets/astra_boot_splash_1280x720_blank.png"

    assert "BOOT_BIN = build/astra_boot.bin" in boot
    assert "BOOT_ELF = build/astra_boot.elf" in boot
    assert "BOOT_MAP = build/astra_boot.map" in boot
    assert "LEGACY_ROM = build/astra68.rom" in boot
    assert "SPLASH_PAYLOAD = build/astra_boot_splash.pal8.lz4" in boot
    assert "KERNEL_BIN = $(BUILD_DIR)/astra_kernel.bin" in kernel
    assert "KERNEL_ELF = $(BUILD_DIR)/astra_kernel.elf" in kernel
    assert "KERNEL_MAP = $(BUILD_DIR)/astra_kernel.map" in kernel
    assert '.incbin "build/astra_boot_splash.pal8.lz4"' in splash
    assert 'else "build/astra_boot.bin"' in converter
    assert "SPLASH_SOURCE := ../../../sw/boot/assets/" \
           "astra_boot_splash_1280x720_blank.png" in arty
    assert "--checksum --no-times" in agent_rules
    assert "--exclude '*/build'" in agent_rules
    assert hashlib.sha256(blank_splash.read_bytes()).hexdigest() == (
        "cdf001bb70e130c9267f5205261eb3855f1b74cbbba832dbcd22fb6d66f77ff9"
    )
