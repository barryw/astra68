# Codec streams

`codec.library.1` opens bounded source streams through the `CODEC` service.
The caller keeps file or memory ownership and supplies bytes by offset; an
isolated host provider returns typed audio or video chunks. Request `CODEC` in
the application manifest and pass its startup capability to `astra_codec_open`.

Current providers recognize PCM WAV, Ogg Vorbis, and progressive YUV4MPEG2
video by content. Audio samples are interleaved little-endian PCM. Video
chunks carry YUV420P frame bytes, stride, timestamp, duration, and frame
offset, so a caller can assemble a frame before presenting it through Display.
Close streams after use; closing the client process also retires its sessions.

```{doxygenfile} codec.h
:project: astra-ndk
```
