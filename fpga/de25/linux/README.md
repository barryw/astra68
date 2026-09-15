# DE25 Linux capture boundary

`astra_display_capture.ko` is the host-facing boundary for remote display. It
captures the final physical RGB888 stream, after framebuffer/window, tile,
sprite, Copper, boot-overlay, scanline-replay, and native-pointer composition.
The module exposes `/dev/astra-display-capture` with one operation:

1. open the device exclusively;
2. map `PAGE_ALIGN(ASTRA_DISPLAY_CAPTURE_FRAME_BYTES)` read-only;
3. issue `ASTRA_DISPLAY_CAPTURE_IOC_CAPTURE`;
4. consume the first `frame_bytes` bytes described by the returned metadata;
5. close the device.

Opening allocates one coherent Linux frame and claims up to the four physical
channels on one DesignWare AXI DMA controller. Closing releases all of those
resources. Hardware acquisition and DMA ownership are serialized, and Media
RAM is mapped for DMA only after the capture producer has completed.

The DE25 Linux image must provide `CONFIG_DW_AXI_DMAC`. The current vendor
kernel has it disabled, so its exact-source module is loaded before Astra's
module. Keep `dw_axi_dmac_platform` resident until shutdown: Linux 6.12.11's
upstream remove path omits DMA-device unregistration and cannot be safely
unloaded/reinserted. Astra's capture module itself is unload-safe when closed.

Build the module against the exact running kernel source and output tree:

```sh
make -C "$KERNEL_SOURCE" O="$KERNEL_OUTPUT" \
    M="$ASTRA_SOURCE/fpga/de25/linux" ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu- modules
```

Build the physical certifier separately:

```sh
make -C fpga/de25/linux CROSS_COMPILE=aarch64-linux-gnu-
```

The certifier rejects writable mappings, captures one frame through the ioctl,
and writes exactly the meaningful RGB888 payload from the read-only mapping.

`astra-remote-desktop` exposes that final frame through standard RFB/VNC. It
binds only to `127.0.0.1:5900`; connect through an SSH tunnel so authentication
and encryption remain SSH's responsibility rather than VNC password security.
It is demand-driven: with no viewer attached it performs no captures. Keyboard
and pointer events use QEMU's dedicated
`/run/astra/remote-desktop-qmp.sock` monitor and existing `input-send-event`
path, leaving the diagnostics monitor available. Per-client reference tracking
releases held keys and buttons when a viewer disconnects.

Build it against the same Ubuntu 22.04 AArch64 sysroot used for QEMU, with the
target's `libvncserver-dev` package extracted into that sysroot:

```sh
make -C fpga/de25/linux CROSS_COMPILE=aarch64-linux-gnu- remote-desktop
```

The board needs Ubuntu's `libvncserver1` runtime and the qualified capture
modules described above. Install `astra_display_capture.ko` and
`dw-axi-dmac-platform.ko` under the running kernel's module tree, run
`depmod`, and install `fpga/de25/astra-display-capture.conf` in
`/etc/modules-load.d`; the capture module's soft dependency loads DesignWare
DMA first and both remain resident until shutdown. The immutable Astra release
contains `astra-remote-desktop.service`, and the release deployer installs the
unit without enabling it; remote display is opt-in and starts only on request.
