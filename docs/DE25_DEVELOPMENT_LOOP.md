# DE25 development loop

The DE25 is an active development machine. A selected release is a checkpoint,
not a production deployment gate. Use the focused installer for source edits;
build a complete release image only after the fix works on hardware.

Run from the Mac repository root. The command mirrors source to Beast, runs only
the requested `make` target there, transfers only named artifacts, updates the
board, restarts Astra once, and waits for a **new** QEMU process and HOST0 boot:

```sh
rtk proxy python3 tools/de25-dev.py \
  --make sw/userspace/services/desktop \
  --guest sw/userspace/services/desktop/build/m68k/desktop=/services/desktop
```

Use the narrowest Makefile target available. For SDL_mixer:

```sh
rtk proxy python3 tools/de25-dev.py \
  --make sw/userspace/sdl2:mixer-library-contract \
  --guest sw/userspace/sdl2/build/m68k/libraries/SDL2_mixer.library.3=/libs/SDL2_mixer.library.3
```

Host components use the same command. This example builds and installs the
AArch64 Codec worker without touching the guest image:

```sh
rtk proxy python3 tools/de25-dev.py \
  --make fpga/de25/linux:codec-worker-target \
  --host build/de25-capture/astra-codec-worker-aarch64=bin/astra-codec-worker
```

Repeat `--guest` and `--host` to install coupled artifacts in one restart.
`--make DIR:TARGET` accepts a specific target; `--make DIR` uses `all`.
Guest targets must be release-owned paths under `/commands`, `/services`,
`/libs`, `/apps`, or `/fonts/system`, or the three named system files. User
settings and files are never installer targets.

Guest updates stop Astra, wait for the storage provider's lease to retire,
mount the **persistent** system image, replace only named files, unmount, and
restart. No image copy, migration, release packaging, automatic rollback, or
restoration of an older image occurs. Failed code remains installed for a
fix-forward iteration. If unmount fails, installer leaves Astra stopped rather
than let the provider open an image still mounted by Linux.

Host updates prepare a development release with hard links to unchanged files
from the selected release. Changed files and the manifest get new inodes;
the immutable selected release stays intact. Board unit files then launch the
development tree. Full release deployment overwrites those units and removes
the development tree after its own checks pass.

Hardware proof on 2026-10-27: reinstalling Desktop with a focused build and
new HOST0 boot took about 31 seconds. Reinstalling the unchanged Codec worker
through the host shadow took about 39 seconds with the corrected boot gate.
All Astra, audio, and remote desktop services were active after both trials.
These were deployment-path probes; they did not claim a new functional fix.

Haiku reference: `src/servers/package/PackageManager.cpp` commits prepared
activation through a volume transaction, and
`src/kits/package/ActivationTransaction.cpp` describes added and removed
packages. Astra adopts the prepare-then-activate boundary for host binaries;
its current ext4-backed guest needs a stopped-image file update until native
package activation exists. No PackageFS or second guest filesystem is added.
