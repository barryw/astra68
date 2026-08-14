# Arty HDMI audio transport

This block is the bounded Linux-to-HDMI PCM sink for the Arty target. Linux
owns mixing and writes signed 24-bit stereo frames; the PL consumes one frame
per 48 kHz audio-clock edge. Applications and the Astra guest do not map this
transport directly.

PCM players, wavetable synthesis, speech synthesis, and later sources all feed
the ARM mixer as ordinary stereo streams. Source count, resampling, per-source
gain, and effects are software policy; the mixer always presents this same
single 48 kHz/24-bit stereo sink to HDMI. Adding an audio source therefore
does not change the FPGA or HDMI packetizer.

The device occupies `0x43c06000..0x43c060ff` inside the existing 64 KiB PS
control aperture. Registers are 32-bit little-endian AXI4-Lite words.

| Offset | Access | Contract |
|---:|---|---|
| `0x00` | R | ID `AUD0` (`0x41554430`). |
| `0x04` | R | Version `0x00010000`. |
| `0x08` | R | 24-bit, 48 kHz, stereo capabilities. |
| `0x0c` | R/W | Bit 0 enables playback; bit 1 drains queued frames muted. |
| `0x10` | R | Enable, underrun/overflow state, and queue level. |
| `0x14` | R/W | Left signed 24-bit sample in bits 23:0. |
| `0x18` | W | Right signed 24-bit sample and atomic stereo-frame enqueue. |
| `0x1c` | R | Playback-underrun count. |
| `0x20` | R | Rejected-enqueue count. |
| `0x24` | R | Sample rate, 48000. |
| `0x28` | R | Guaranteed queue capacity, 512 stereo frames. |

Writes require all four byte strobes and zero in reserved bits. Invalid,
unknown, or full-queue writes return an AXI error. The 512-frame FIFO is an
existing `astra_async_fifo`; control and counters cross clock domains through
two-stage or Gray-coded synchronizers.

Run the directed RTL gate on Beast:

```sh
./fpga/arty/audio/run_tests.sh
```

`fpga/arty/linux/astra-audio-certify` validates the live register contract,
prefills the queue, streams a one-second 440 Hz fixed-point test tone without
queue faults, and leaves playback disabled.
