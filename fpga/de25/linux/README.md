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
