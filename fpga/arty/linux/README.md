# Arty Linux support

The Arty Z7-20 boots a deliberately small Linux appliance with a read-only root
and persistent writable state under `/data`. The rootfs overlay in this
directory is the Astra-owned source for board-specific early-boot files.

`retire_nova_runtime.sh` performs the one-time migration from the inherited
Nova image. It removes the Nova daemon, its init links, and the legacy
`novacap` module and autoload paths; installs a persistent module blacklist;
installs `astra-firstboot` after `mountall` and before `udev`; changes the
board identity to `astra-arty`; exposes
`/data/astra/share` through Samba; preserves persistent SSH state and legacy
`/data/nova` contents; and restores the root filesystem read-only. Files
removed from the immutable root are backed up under `/data/astra/retired-nova`
before migration.

Removing `/etc/modules-load.d/novacap.conf` alone is insufficient. The
inherited device tree supplies a modalias, so udev auto-probes any installed
`novacap.ko` and the driver stalls on the retired Nova AXI aperture. The
migration archives and removes the module, runs `depmod`, and restarts udev to
release its mappings of atomically replaced `modules.*.bin` files. Linux 6.6
otherwise returns `EBUSY` from `sb_prepare_remount_readonly()` while those
deleted mappings contribute to the superblock remove count. The migration now
fails if it cannot prove that `/` was restored read-only.

The deployed image now uses the integrated 1280x720p60 PS/DDR graphics design,
not the pattern shell. The release pipeline is deliberately split into
auditable stages:

- `../build_fsbl.sh` generates and compiles the Zynq FSBL from the exact XSA.
- `build_device_tree.sh` removes retired Nova nodes, caps Linux System RAM at
  384 MiB, reserves the remaining 128 MiB graphics arena as `no-map`, and
  assigns the 100/166.667 MHz fabric clocks used by the geometry-qualified
  release.
- `build_fit_image.sh` reuses the unchanged Linux kernel and constructs a new
  verified FIT with the Astra device tree.
- `make` cross-builds the static ARM graphics loader and live boot-status
  utility with strict warnings; `make analyze test-host` is required.
- `../package_boot.sh` builds an exact FSBL/bitstream/U-Boot/DTB `BOOT.BIN`.
- `deploy_graphics_release.sh` verifies every input hash, retains hash-named
  rollback files, atomically installs the boot files, assets, and boot/status/
  sprite/renderer utilities, and leaves `/` read-only.

At early boot, `astra-firstboot` runs `astra-graphics-boot`. The loader maps
only the reserved physical arena, copies the exact big-endian RGB565 splash,
executes a data-synchronization barrier, reads every byte back, verifies CRC32,
programs the validated shadow scene, and waits for frame-boundary promotion.
It then writes four real boot rows through the boot-text mailbox. The hardware
renders CP437 glyphs over the blank image and promotes each text bank only at
vertical blank. The loader has finite timeouts and reports a precise failure;
it never prints `OK` for a check it did not perform.

`astra-boot-status` updates one row after boot without rebuilding the complete
plane. Resolve it from the release selected by the BOOT image actually running:

```sh
boot=$(sha256sum /run/media/boot-mmcblk0p1/BOOT.BIN | awk '{print $1}')
/data/astra/graphics/by-boot/$boot/bin/astra-boot-status \
  stage 3 'Axiom launch' READY amber
```

The common MMIO layer validates device identity, version, capabilities, text
geometry, and origin before use. Both tools are static ARM executables. The
host formatter tests and GCC static analyzer are part of release packaging.

Current physical identities and the cold-boot result live only in
`docs/CURRENT_STATE.md`; duplicating an active hash table here previously made
this document another stale selector. The blank RGB565 splash remains pinned
by the deploy gate to SHA-256
`86eb30739db77b85f4deb1915fb9cb9263ab4755ae318ffb1b7a4a95b7017ba4`.

The board readback passes all 1,843,200 bytes with CRC32 `611029ee`, activates
scene generation 1 with zero deferrals, publishes final boot-text generation
2, and reports the FPGA manager `operating`. Graphics capabilities are
`0x000003ff`. Ten consecutive renderer-certification runs each execute 29
fenced commands and verify 1,196,651 result pixels with zero backpressure or
engine errors. They cover fill, overlap-safe copy, scaling, reflection,
clipping, keying, every ROP, direct-color conversion, source-over/opacity,
palette expansion, and MASK1 suppression. The complete sprite hardware
regression also passes unchanged. Ten consecutive copper certifications pass
dual-bank execution, WAIT/SKIP, validated MOVE, IRQ, command dispatch, and
invalid-target containment. Boot-text status is `0x00000007`:
write-ready, commit-ready, and active.
Linux System RAM ends at `0x17ffffff`; the arena begins at `0x18000000`, so the
kernel cannot allocate over graphics memory. An unattended boot reaches login,
keeps `/` read-only and `/data` read-write, restores persistent SSH, starts
Samba, and acquires `192.168.1.188`. Complete copper-release hardware evidence
is retained under
`/mnt/Documents/astra68/work/render-v1/copper-1/integration-13/hardware-cert`.
The retained physical HDMI frame is
`docs/evidence/astra-arty-boot-text6-hdmi-20260730.png`; it confirms all four
dynamic rows render correctly inside the lower panel.

The 512 MiB DDR budget is deliberate: graphics owns the physical 128 MiB
`no-map` arena, the Astra QEMU process preallocates 128 MiB of normal cached
Linux RAM at launch, and the remaining 256 MiB budget belongs to Linux and its
host services. Linux reports 384 MiB before QEMU starts because guest RAM uses
the normal cached allocator; after preallocation those pages are resident and,
with this appliance's no-swap policy, cannot be reclaimed for another process.
This avoids running the emulated CPU against an uncached `/dev/mem` mapping.

The inherited userspace still emits nonfatal volatile-directory, unclean FAT,
RTC, resolver, and interface-naming warnings. The retired Nova reservations and
device aliases are gone; the remaining warnings are host-image cleanup work,
not graphics-memory-map failures.
