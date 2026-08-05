# Astra 68 — complete inventory

Audited 2026-08-05 by walking the machines and the tree rather than reading
prior documentation. `CLAUDE.md` is the short orientation; this is the detail
behind it.

Purpose: nothing about this project should have to be rediscovered. If you learn
a fact that took more than five minutes to establish, it belongs here.

---

## 1. Hardware

### Arty Z7-20 — the active machine

- Xilinx **Zynq-7020**: two ARM Cortex-A9 cores (PS) plus FPGA fabric (PL).
- Runs Linux 6.6.40-xilinx as host `astra-arty`, **192.168.1.188**, root ssh
  **from `beast`**. Board identity was set by `retire_nova_runtime.sh`.
- JTAG chain visible from `beast` with
  `openFPGALoader --cable digilent --detect` → ARM Cortex-A9 `0x4ba00477` plus
  Zynq `0x3727093`. Without `--cable digilent` the scan reports empty.
- USB bridge is an FTDI **FT2232H** (`0403:6010`, Digilent Adept), giving
  `/dev/ttyUSB0` (JTAG) and `/dev/ttyUSB1` (console) on `beast`.
- 512 MiB DDR. The device tree reserves `0x18000000..0x1fffffff` (128 MiB)
  `no-map` for graphics.
- **The PL carries the 1280x720p60 graphics design. It does not carry the
  MC68030.** The m68k is QEMU on the ARM cores.

Filesystem layout on the board:

| Path | Notes |
|---|---|
| `/` | **read-only** root |
| `/data` | the only writable area, `/dev/mmcblk0p3`, ext4, ~108 GiB free |
| `/run/media/boot-mmcblk0p1` | `/dev/mmcblk0p1`, vfat boot partition |
| `/data/astra/qemu/bin` | emulator builds, several generations |
| `/data/astra/qemu/lib` | bundled `libpixman` etc; needs `LD_LIBRARY_PATH` |
| `/data/astra/rom` | boot ROMs |
| `/data/astra/bin` | `astra-qemu` launcher, graphics/boot/sprite utilities |
| `/data/astra/deploy` | hash-named rollback sets |
| `/data/astra/log` | writable log target |
| `/data/astra/share` | Samba export, a deployment path from the Mac |

Userland is **BusyBox**. No `truncate`, `timeout`, `pkill`, no compiler.
`losetup` uses `-o OFS LOOPDEV FILE`, not `--find`. `od` lacks `-A`.

`/data/astra/bin/astra-qemu` demands **both** an evdev keyboard and pointer and
exits if either is missing. The board currently has only a Logitech trackball,
so headless runs must invoke the emulator directly:

```sh
LD_LIBRARY_PATH=/data/astra/qemu/lib /data/astra/qemu/bin/<qemu> \
  -M astra68 -m 32M -bios <rom> -nographic -monitor none -serial stdio \
  -no-reboot -drive if=none,format=raw,file=<image>
```

### ULX3S — historical

- Lattice **ECP5**, attached to `nuc` via an FTDI **FT231X** (`0403:6015`,
  UART-only), `/dev/ttyUSB1`.
- Was the production target for the TG68K FPGA CPU line. Superseded by the Arty
  per the 2026-07-30 override. Retained qualification history lives in
  `docs/CURRENT_STATE.md` and `fpga/soc/oss_flow/TIMING_CLOSURE.md`.
- Qualified flash bitstream `25D9CB8E`; `7DDD9C03` was an SRAM-only K9 reroute.

### ESP32 / AstraHost

`docs/ASTRAHOST.md` defines an ESP32 that owns the SD electrical interface and
talks to the FPGA over SPI (never UART). Source in `esp32/`. The override lists
**AstraHost/ESP transport as historical** for the active Arty boundary.

---

## 2. Machines and toolchains

| | Mac | `beast` | `nuc` | `astra-arty` |
|---|---|---|---|---|
| Address | local | 192.168.1.3 | 192.168.1.2 | 192.168.1.188 |
| CPU / RAM | Apple Silicon | 32 core / 61 GB | 8 core / 31 GB | ARMv7 dual A9 |
| `m68k-elf-gcc` | **16.1.0** | – | – | – |
| `m68k-linux-gnu-gcc` | – | **13.3** | yes | – |
| `arm-linux-gnueabihf-gcc` | – | **13.3** | – | – |
| oss-cad-suite (yosys/nextpnr/ecppack) | `/opt/homebrew/oss-cad-suite` | `~/oss-cad-suite` | `~/oss-cad-suite` | – |
| Vivado | – | `/tools/Xilinx` | – | – |
| `openFPGALoader` | yes | yes | yes | – |
| `verilator` / `iverilog` | – | yes | – | – |
| `mke2fs` | Android platform-tools' | yes | yes | yes |
| `e2fsck` / `dumpe2fs` | **absent** | yes | yes | **absent** |
| `pytest` | yes | **absent** | – | – |
| `lz4`, `ninja`, `cargo` | lz4 yes | all yes | – | – |
| Docker | – | none | present | – |

Notes that matter:

- The Mac's `mke2fs` on `PATH` is **Android platform-tools'**, not e2fsprogs,
  and there is no `e2fsck` beside it. It formats well enough for host gates; the
  independent-judge half of any filesystem gate only exists on `beast`.
- The Mac cannot build the kernel image or `test_process`.
- `pytest` missing on `beast` means `sw/boot`'s 38 Python tests run on the Mac.
- Neither `nuc` nor `beast` has FPGA tools on `PATH`; they are under
  `~/oss-cad-suite`.

### Scratch state (all disposable)

| Path | Host | What |
|---|---|---|
| `/tmp/qemu-final-build/qemu-system-m68k` | beast | x86_64 system emulator with block + input models |
| `/tmp/qemu-m68k-user-build/qemu-m68k` | beast | user-mode qemu-m68k 9.2.4 for the m68k gates |
| `/tmp/astra-qemu-arty/build-arty-*/` | beast | **ARM** emulator for the board |
| `/tmp/astra-qemu-final/source-*` | beast | prepared QEMU source |
| `/tmp/storage.img` | beast | 64 MiB image the boot check reads sector 0 from |
| `/mnt/Documents/astra68/` | nas | durable evidence and the cached QEMU tarball |

---

## 3. Repository map

| Path | Tracked | What it is | Status |
|---|---:|---|---|
| `sw/kernel` | – | **Axiom**, the MC68030 kernel | active |
| `sw/boot` | – | firmware / ROM, LZ4 payload packing, POST | active |
| `sw/stage0` | – | 2 KiB immutable FAT loader that fetches `/ASTRA68.ROM` | active |
| `sw/userspace` | – | runtime, alloc, metrics, storage, input, shell, supervisor | active |
| `sw/include/astra` | – | shared ABI headers (`boot.h`, `block.h`, `supervisor.h`, …) | active |
| `sw/harte` | – | Harte CPU test harness | see `docs/HANDOVER-harte-c-harness.md` |
| `third_party/lwext4` | 55 | BSD-3 ext4, vendored 2026-08-05 | active |
| `third_party/musashi` | – | 68030 model + Astra PMMU rewrite | **conformance oracle** |
| `conformance/` | 33 | shared architectural cases; targets `musashi-68030` and `rtl-tg68k030-mmu2` | active oracle |
| `emu/qemu` | – | **Astra QEMU 9.2.4 fork — the emulator** | active |
| `emu/crates` | 36 | AstraVM, Rust desktop machine on Musashi | superseded |
| `fpga/arty` | 133 | Arty Z7 Linux, FSBL, device tree, FIT, graphics loader | **active** |
| `fpga/soc` | 124 | SoC RTL and the oss flow / timing closure | active (ULX3S lineage) |
| `fpga/cpu` | 175 | `tg68k_c_030_mmu2`, the repaired TG68K core | retained oracle |
| `fpga/diagnostics` | 8 | diagnostic RTL | check before deleting |
| `fpga/maintenance` | 5 | last touched 2026-07-16 | likely legacy |
| `fpga/memtest` | 4 | last touched 2026-07-07 | likely legacy |
| `fpga/memtest32` | 4 | last touched 2026-07-16 | likely legacy |
| `esp32/` | 19 | AstraHost firmware | historical per override |
| `ndk/` | 59 | Astra NDK, the stable developer surface | active |
| `tools/`, `mk/` | 1 each | `analyze_pc_profile.py`; `m68k-cross.mk` | active |
| `build/` | **0** | untracked build output at the repo root | should not exist |
| `docs/evidence/` | 0 | ignored working view of retained evidence | never stage |

`emu/target` is 2.1 GB of untracked Rust build artifacts; `fpga/` is 2.3 GB
mostly untracked. Only 36 files under `emu/` are tracked.

---

## 4. Software architecture, as currently true

```
Arty Z7 Linux (ARM Cortex-A9)
  -> Astra QEMU backend  (the m68k machine; TCG, ~30 MHz equivalent)
     -> stage0 (immutable, 2 KiB) reads MBR -> FAT -> /ASTRA68.ROM
        -> firmware / ROM: POST, LZ4 decode + CRC-32 of kernel and user image
           -> Axiom kernel (MC68030, PMMU)
              -> initial user image (supervisor), <= 48 KiB
                 -> block lease + completion IRQ endpoint
```

### Boot memory layout (`sw/include/astra/boot.h`, ABI 0.3)

| Symbol | Address | Size |
|---|---|---|
| `ASTRA_BOOT_INFO/SCRATCH_ADDRESS` | `0x01ff8000` | 32 KiB |
| `ASTRA_EARLY_LOG_ADDRESS` | `0x02000000` | 16 KiB |
| `ASTRA_USER_IMAGE_ADDRESS` | `0x02004000` | **`MAX_SIZE` 48 KiB** |
| `ASTRA_KERNEL_LOAD_ADDRESS` | `0x02010000` | |
| `ASTRA_KERNEL_TRACE_ADDRESS` | `0x02090000` | 64 KiB |
| `ASTRA_KERNEL_USABLE_ADDRESS` | `0x020a0000` | ~29.4 MiB |
| `ASTRA_ROM_BACKING_ADDRESS` | `0x03e00000` | 256 KiB |

**The 48 KiB user image cap is the hole between `0x02004000` and the kernel.**
It is not a policy number; raising it moves the kernel and is a boot ABI change.
This — not the ROM budget — is why the filesystem cannot be the initial image.

### ROM

Fixed 256 KiB window decoded in RTL. Both loadable images ship LZ4 and are
CRC-32 verified after decode. `/ASTRA68.ROM` is a **file on the FAT partition**,
so the "ROM" is updatable without touching hardware.

### Storage stack

```
lwext4 (BSD-3 subset, vendored)
  ext4_port.c        window-enforced, splits transfers, status -> errno
  AstraBlockDevice   the facade: validation, generations, deadlines, metrics
  lease_block.c | memory_block.c | tests/file_block.c
```

Frozen mkfs profile:

```
-b 4096 -I 256 -O ^64bit,^casefold,^extent,^ext_attr,^metadata_csum_seed -J size=4
```

Held identically by `sw/userspace/storage/Makefile` and the qualification rig.
MBR only — `stage0` lives in FPGA BRAM and scans the four primary slots for FAT,
so **GPT does not boot** and slot 1 must stay FAT. That leaves up to three ext4
volumes without an extended partition.

---

## 5. Build and gate commands

```sh
# userspace: host tests, sanitizers, analyzer, MC68030 cross-build   (beast)
cd sw/userspace && make test && make sanitize && make analyze && make all

# kernel: 30 suites, default image, qualification image              (beast)
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1

# boot ROM                                                          (beast)
cd sw/boot && make astra_boot.bin && make test   # pytest half: Mac only

# filesystem, host + freestanding link
cd sw/userspace/storage && make ext4-test && make linkcheck

# filesystem on big-endian MC68030, judged by e2fsck                (beast)
cd sw/userspace/storage/lwext4-eval
export QEMU_M68K=/tmp/qemu-m68k-user-build/qemu-m68k
make interop && make reread && make partitioned && make measure && make bigvolume

# QEMU device certifiers                                            (beast)
python3 emu/qemu/test-block.py "$(./emu/qemu/build.sh host)"
python3 emu/qemu/test-input.py "$(./emu/qemu/build.sh host)"

# emulator builds
emu/qemu/build.sh host      # x86_64 / native
emu/qemu/build.sh desktop   # with UI
emu/qemu/build.sh arty      # ARM, for the board

# conformance, both oracles
cd conformance && make            # musashi-68030 and rtl-tg68k030-mmu2
```

### Deploying to the board

```sh
# from beast
scp <rom> root@192.168.1.188:/data/astra/rom/<name>.bin
scp $(ls /tmp/astra-qemu-arty/build-arty-*/qemu-system-m68k) \
    root@192.168.1.188:/data/astra/qemu/bin/<name>
```

Verify the emulator actually has the storage model before blaming the ROM:

```sh
strings <qemu> | grep -c "Astra68 storage image"   # 2 = present, 0 = too old
```

---

## 6. Cleanup analysis

Evidence-based, from this audit. **Nothing here has been deleted yet** except
where noted.

### Safe to remove

| Item | Evidence |
|---|---|
| `emu/crates` (AstraVM) + `Cargo.*`, `rust-toolchain.toml` | A second emulator, on Musashi, superseded by the QEMU fork. `git grep astravm` outside `emu/` returns **0**. Frees 2.1 GB of untracked artifacts. |
| `build/` at repo root | **0 tracked files**; build output that should never have been there. |

### Needs a decision — do not delete on inference

| Item | Why it is not obviously dead |
|---|---|
| `third_party/musashi` | **It is the CPU conformance oracle.** `conformance/` runs each architectural case against `musashi-68030` *and* `rtl-tg68k030-mmu2` and compares. `CURRENT_STATE.md` explicitly retains both. Removing it deletes the only independent check on the RTL CPU, and QEMU cannot stand in without a 68030 PMMU implementation it does not have. |
| `fpga/cpu` (TG68K core) | The other half of that same oracle. |
| `fpga/soc` | ULX3S lineage, but carries the SoC RTL and timing-closure record still referenced by `AGENTS.md`. |
| `esp32/` | Historical per the override, but `docs/ASTRAHOST.md` is a LOCKED transport contract. |
| `fpga/{memtest,memtest32,maintenance}` | Small and stale (Jul 7–16) — probably dead, but they are RTL and I have not traced whether the SoC build includes them. |

**On "replace Musashi with QEMU":** QEMU already *is* the emulator — the
override made that call on 2026-07-30, and every storage and boot gate in this
repo runs on the QEMU fork. Musashi's surviving job is verification, not
emulation. The two things share a name and nothing else.

---

## 7. Keeping this current

Add to this file whenever you establish a fact that cost real time: a host, a
device path, a tool that is missing where you expected it, a constant that turns
out to be load-bearing. A fact rediscovered twice is a documentation bug.

`CLAUDE.md` is loaded every session and should stay short. Detail lands here.
