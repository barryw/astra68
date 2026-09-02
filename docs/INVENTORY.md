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
- 512 MiB DDR: 128 MiB Astra guest RAM, 128 MiB graphics RAM at the contiguous
  `no-map` range `0x18000000..0x1fffffff`, and 256 MiB for Linux/host services.
- **The PL carries the 1280x720p60 graphics design. It does not carry the
  MC68030.** The m68k is QEMU on the ARM cores.

Filesystem layout on the board:

| Path | Notes |
|---|---|
| `/` | **read-only** root |
| `/data` | the only writable area, `/dev/mmcblk0p3`, ext4, ~108 GiB free |
| `/run/media/boot-mmcblk0p1` | `/dev/mmcblk0p1`, vfat boot partition |
| `/data/astra/current` | atomic selector for the active immutable runtime release |
| `/data/astra/releases/<identity>` | verified QEMU, ROM, base image, libraries, and helpers |
| `/data/astra/state/<identity>` | writable runtime disk state for one exact release |
| `/data/astra/graphics/releases/<identity>` | immutable graphics/Linux helper releases |
| `/data/astra/graphics/by-boot/<BOOT-SHA256>` | active-boot-derived graphics release selector |
| `/data/astra/deploy` | historical rollback evidence; never an active selector |
| `/data/astra/log` | writable log target |
| `/data/astra/share` | Samba export, a deployment path from the Mac |

Userland is **BusyBox**. No `truncate`, `timeout`, `pkill`, no compiler.
`losetup` uses `-o OFS LOOPDEV FILE`, not `--find`. `od` lacks `-A`.

The runtime refuses to start unless every declared release file, path,
executable bit, and installed read-only mode verifies. It copies the immutable
base image into release-keyed writable state and launches only resolved
content-addressed paths. For a headless diagnostic, resolve and verify the
selected release first:

```sh
root=$(readlink -f /data/astra/current)
python3 "$root/bin/astra-release.py" verify --installed "$root"
LD_LIBRARY_PATH="$root/qemu/lib" "$root/qemu/bin/qemu-system-m68k-astra" \
  -M astra68 -m 128M -bios "$root/rom/astra_boot.bin" \
  -nographic -monitor none -serial stdio -no-reboot
```

---

## 2. Machines and toolchains

| | Mac | `beast` | `astra-arty` |
|---|---|---|---|
| Address | local | 192.168.1.3 | 192.168.1.188 |
| CPU / RAM | Apple Silicon | 32 core / 61 GB | ARMv7 dual A9 |
| `m68k-elf-gcc` | **16.1.0** | – | – |
| `m68k-linux-gnu-gcc` | – | **13.3** | – |
| `arm-linux-gnueabihf-gcc` | – | **13.3** | – |
| Vivado | – | `/tools/Xilinx` | – |
| `openFPGALoader` | yes | yes | – |
| `verilator` / `iverilog` | – | yes | – |
| `mke2fs` | Android platform-tools' | yes | yes |
| `e2fsck` / `dumpe2fs` | **absent** | yes | **absent** |
| `pytest` | yes | **absent** | – |

Notes that matter:

- The Mac's `mke2fs` on `PATH` is **Android platform-tools'**, not e2fsprogs,
  and there is no `e2fsck` beside it. It formats well enough for host gates; the
  independent-judge half of any filesystem gate only exists on `beast`.
- The Mac cannot build the kernel image or `test_process`.
- `pytest` missing on `beast` means `sw/boot`'s 38 Python tests run on the Mac.
- Vivado is not on `beast`'s default `PATH`; use the component build scripts.

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
| `sw/userspace` | – | runtime, alloc, metrics, storage, input, shell, supervisor | active |
| `sw/include/astra` | – | shared ABI headers (`boot.h`, `block.h`, `supervisor.h`, …) | active |
| `third_party/lwext4` | 55 | BSD-3 ext4, vendored 2026-08-05 | active |
| `emu/qemu` | – | **Astra QEMU 9.2.4 fork — the emulator** | active |
| `fpga/arty` | 133 | Arty Z7 Linux, FSBL, device tree, FIT, graphics loader | **active** |
| `ndk/` | 59 | Astra NDK, the stable developer surface | active |
| `tools/`, `mk/` | – | shared build, test, deployment, and profiling tools | active |
| `build/` | **0** | untracked build output at the repo root | should not exist |
| `docs/evidence/` | 0 | ignored working view of retained evidence | never stage |

Generated build products are excluded from source syncs and version control.

---

## 4. Software architecture, as currently true

```
Arty Z7 Linux (ARM Cortex-A9)
  -> Astra QEMU backend  (the m68k machine; TCG, ~30 MHz equivalent)
     -> firmware / ROM: POST, LZ4 decode + CRC-32 of kernel and user image
        -> Axiom kernel (MC68030, PMMU)
           -> initial user image (supervisor)
```

### Boot memory layout (`sw/include/astra/boot.h`, ABI 0.6)

| Symbol | Address | Size |
|---|---|---|
| `ASTRA_BOOT_INFO/SCRATCH_ADDRESS` | `0x01ff8000` | 32 KiB |
| `ASTRA_EARLY_LOG_ADDRESS` | `0x02000000` | 16 KiB |
| `ASTRA_USER_IMAGE_ADDRESS` | `0x02004000` | **`MAX_SIZE` 256 KiB** (48 KiB before ABI 0.4) |
| `ASTRA_KERNEL_LOAD_ADDRESS` | `0x02044000` | |
| `ASTRA_KERNEL_TRACE_ADDRESS` | `0x020c4000` | 64 KiB |
| `ASTRA_KERNEL_USABLE_ADDRESS` | `0x02354000` | usable RAM begins here |

**The user image ceiling is the hole between `0x02004000` and the kernel.** It
was 48 KiB, which was not a policy number but whatever happened to fit; boot ABI
0.4 moved the kernel up and made it 256 KiB. Firmware reserves only the pages
the image fills and returns the remainder to the allocator, so the ceiling costs
nothing when unused — free frames were 7939/8192 before and after.

### ROM

The 512 KiB QEMU ROM aperture contains LZ4-compressed, CRC-32-verified kernel
and initial-user images.

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
The storage stack accepts the partition formats documented by its active tests.

---

## 5. Build and gate commands

```sh
# userspace: host tests, sanitizers, analyzer, MC68030 cross-build   (beast)
cd sw/userspace && make test && make sanitize && make analyze && make all

# kernel: 30 suites, default image, qualification image              (beast)
cd sw/kernel && make test && make && make clean && make K1_QUALIFICATION=1

# kernel line coverage over those suites, per source file            (beast)
cd sw/kernel && make coverage

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

# the terminal end to end: types over QMP, judges the character plane   (beast)
python3 emu/qemu/test-terminal.py /tmp/qemu-final-build/qemu-system-m68k \
    sw/boot/build/astra_boot.bin --image /tmp/part.img

# a debugger, with ROM, kernel and user symbols                        (beast)
QEMU=/tmp/qemu-final-build/qemu-system-m68k ./emu/qemu/debug.sh --image /tmp/part.img

# an address from a panic or a fault report, named
python3 tools/symbolize.py 0x0010044a

# the ROM packer's guard, and the symbolizer's routing (pytest: Mac only)
cd sw/boot && python3 -m pytest tests/test_pack_payload.py
python3 -m pytest tools/tests/test_symbolize.py

# emulator builds
emu/qemu/build.sh host      # x86_64 / native
emu/qemu/build.sh desktop   # with UI
emu/qemu/build.sh arty      # ARM, for the board

```

### Deploying to the board

```sh
# from beast; creation accepts only explicitly named inputs and a new output
ASTRA_ARTY_QEMU=<qemu> ASTRA_ARTY_ROM=<rom> \
ASTRA_ARTY_STORAGE=<clean-image> ASTRA_ARTY_TERMINAL_DISPLAY=<display> \
ASTRA_ARTY_QEMU_LIBDIR=/usr/lib/arm-linux-gnueabihf \
  emu/qemu/create-arty-release.sh <release-directory>
emu/qemu/deploy-arty-release.sh <release-directory>
```

The deployer re-verifies after transfer, installs the content-addressed tree
read-only, and atomically replaces `/data/astra/current`. Verify the emulator
actually has the storage model before blaming the ROM:

```sh
strings <qemu> | grep -c "Astra68 storage image"   # 2 = present, 0 = too old
```

---

## 6. Keeping this current

Add to this file whenever you establish a fact that cost real time: a host, a
device path, a tool that is missing where you expected it, a constant that turns
out to be load-bearing. A fact rediscovered twice is a documentation bug.

`CLAUDE.md` is loaded every session and should stay short. Detail lands here.
