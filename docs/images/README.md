# Physical Astra OS captures

## DH0 desktop and Atlas

`astra-desktop.png` and `astra-atlas.png` were captured on 2026-10-12 from the
physical DE25-Nano through authenticated RFB at 1920×1080. Selected release:
`5bc3bbeae347c964d73971ec12cfbb8036608e0a26d503bc3dc187ddae62849e`.
FPGA remains `b61dbda8`; no new timing checkpoint is claimed.

Real input double-clicks the native DH0 volume target. Atlas reads the actual
root through the native Filesystem Kit: 16 entries, 15 folders and one file.
Fresh capture shows their named icons in the ordinary root window. Close retires
the process and restores byte-identical desktop pixels. A persistent controller
keeps capture active; fresh viewers wait for refresh before reading frames.

Beast converts raw RGB to PNG and verifies decoded pixels equal the source
bytes. No pixels were edited. PNG SHA-256:

- `astra-desktop.png`:
  `cb49cf0b14fa0538f0df478cf48338ea8fed6e6b8c69447da1e7ca98a6917798`.
- `astra-atlas.png`:
  `94d33c467ac476b9512d2406df092ee7309a260b59a1cca7bc64a2e9e3c8169c`.

Evidence: `/mnt/Documents/astra68/builds/atlas-dh0-20261012/board-2`.
Runtime/audio/remote services retain their identities, remain active with zero
restarts, and the panic record's hash/mtime stays unchanged. Deployment verifies
QEMU UID/GID 999, empty groups, zero capabilities and NoNewPrivs=1, and preserves
`/home` and `/local` contents/metadata. The first `board-1` attempt checked an old
one-second process sample; Atlas remained alive. The accepted retry waits for
fresh samples and repeats launch/capture/close. Guest scrolling/resize/error
gates and the remaining production limits are in
[Atlas continuation](../HANDOVER_2026-10-12_ATLAS.md).

## Doom gameplay and historical desktop

Captured on 2026-10-11 EDT (2026-10-12 UTC) from the DE25-Nano through its authenticated RFB
service, at 1920×1080. The raw RGB frames were converted to PNG on Beast;
their pixels were not retouched or composited.

Selected software release:
`194b9202da7eb0ff61a05ad03633bd63f687ac7afce5fe05c08601fc357af249`.
It carries the current Filesystem ABI 12/Kit 9/STOR 36 guest, worker-only
resolver optimization and native SDL instrument
icons. QEMU runs as `astra`, UID/GID 999. The retained FPGA image is
`b61dbda8`; this capture does not establish a new timing checkpoint.

- Historical desktop frame retained under `review-visual-2`: clean desktop,
  identical before gameplay and after normal Doom quit; PNG SHA-256
  `9e138479a04579974fb92ae7dbd7083c7413d5e3a2c5cf44a1a5162894cb4a5c`.
- `astra-doom.png`: live Chocolate Doom gameplay after a keyboard turn.
  PNG SHA-256
  `b35ed75b995a5cc2d0800bc98687bd511a85447fcd5fffd994e0d655bdd43097`.
  A persistent controller keeps capture active; fresh viewers wait for
  refresh before requesting their frames. Gameplay pixels change after
  the turn; normal Esc/Up/Enter/Y/Space quit returns to the desktop.

Source frames and validation logs are retained on Beast under
`/mnt/Documents/astra68/builds/fs-phase1-performance-20261011/review-visual-2`.
Lossless conversion verifies decoded PNG pixels equal the raw RGB bytes.
Installed-release verification and all nine QEMU threads' UID/GID 999,
empty groups, zero effective/bounding capabilities and NoNewPrivs=1 pass
before and after capture. Runtime, audio and remote desktop stay active with
zero restarts. Failed cached-frame attempt `review-visual-1` is retained;
the server intentionally skips hardware capture without connected clients.
Earlier ABI 11 captures remain under `visual-refresh` and are historical.

## Astra quit confirmation

`astra-doom-quit.png` was captured on 2026-10-12 from selected release
`36791f1e24e86a94856446a7389392127f6e6db5fe831d5bd8d3cc88cc97f175`.
The same authenticated RFB path reads the physical 1920×1080 HDMI output;
RGB-to-PNG conversion runs on Beast. No pixels were edited. F10 opens the
confirmation, which now says “PRESS Y TO QUIT TO ASTRA.” Y and Space then
close the actual Doom process, verified through the existing process sampler.
Runtime/audio/remote service identities and the old panic hash/mtime remain
unchanged. QEMU remains UID/GID 999 without groups/capabilities, NoNewPrivs=1.
Evidence: `/mnt/Documents/astra68/builds/app-latency-20261011/board-quit-2`.
The initial `board-quit-1` navigation captured the skill menu and is not used.
PNG SHA-256:
`08b6b99ef67a82c56b786a81292ee0df2924ccb11514cfc7acb5d935cd53b18b`.
