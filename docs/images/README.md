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
