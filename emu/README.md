# AstraVM

AstraVM is the native desktop reference machine for Astra 68. It executes the
unchanged boot ROM on the vendored Musashi 68030/PMMU core, behind a modeled
Astra bus and the MMIO devices needed by POST. The 90x30 Vega ROM console sits
inside the 720x480 display surface in a native Rust `eframe`/`egui`/`wgpu` UI.

![AstraVM after a successful modeled POST](docs/astravm-post.png)

The machine crate is deliberately headless. The desktop app paces it from a
dedicated worker thread and receives bounded snapshots; UI rendering is not
part of machine state or virtual time.

## Build and test

The workspace pins Rust 1.97.0. Install it through rustup once on each build or
desktop host:

```sh
rtk make -C emu toolchain
```

Per the project execution topology, perform repeatable builds on `beast` or
`nuc`:

```sh
cd emu
rtk make fmt
rtk make check
rtk make test
rtk make build
```

Run the unchanged ROM to completion without a window:

```sh
rtk make post
```

Run a freshly built production ROM through PMMU enable, user-copy fault
recovery, vectored timer interrupts, and kernel entry:

```sh
rtk make -C emu kernel ROM=../sw/boot/astra_boot.bin
```

Run natively on a desktop with a GPU/windowing session:

```sh
cd emu
rtk make run
```

`RESET` replays POST and `PAUSE` freezes virtual time. The machine worker never
waits on UI rendering: commands and snapshots cross bounded channels, while
the host renderer consumes only the newest available snapshot.

`READY FOR OS LOADER` is a milestone exposed in snapshots, not a machine halt.
The CPU continues executing the ROM and can proceed into a future loader; only
a POST failure is terminal to an execution slice.

The boot ROM is generated from `sw/boot` and is never stored in Git. Its
reproducible release provenance is recorded in
[`rom/README.md`](rom/README.md). Build it before running AstraVM:

```sh
make -C sw/boot
```

To exercise another image without recompiling AstraVM:

```sh
rtk target/release/astravm --rom ../sw/boot/astra_boot.bin
```

`ASTRA68_BOOT_ROM` remains available as a fallback when `--rom` is omitted.

The current emulated hardware slice covers reset aliasing, ROM, BRAM, SDRAM,
Vesta identity/UART/BIST/timers/vectored IRQs, front-panel MMIO, Vega bootstrap
text and indexed framebuffer presentation, Astraea POST fill/copy and batched
MASK1 glyph drawing, PMMU-enabled kernel entry, and kernel completion status.
New devices should be added behind the same headless bus boundary with focused
unit tests before their UI tooling is introduced. The cross-host test matrix
and explicit model boundaries are recorded in [`RESULTS.md`](RESULTS.md).

MC68030 PMMU descriptor traffic uses a separate fallible physical-bus path.
Mapped Astra apertures return data; an unmapped table address reports a
table-bus fault instead of silently returning a zero descriptor.
