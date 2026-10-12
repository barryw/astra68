# Physical Astra OS captures

Captured on 2026-10-11 EDT (2026-10-12 UTC) from the DE25-Nano through its authenticated RFB
service, at 1920×1080. The raw RGB frames were converted to PNG on Beast;
their pixels were not retouched or composited.

Selected software release:
`194b9202da7eb0ff61a05ad03633bd63f687ac7afce5fe05c08601fc357af249`.
It carries the current Filesystem ABI 12/Kit 9/STOR 36 guest, worker-only
resolver optimization and native SDL instrument
icons. QEMU runs as `astra`, UID/GID 999. The retained FPGA image is
`b61dbda8`; this capture does not establish a new timing checkpoint.

- `astra-desktop.png`: clean desktop, identical before gameplay and after
  normal Doom quit; PNG SHA-256
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
