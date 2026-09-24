# PCM playback

`pcm.library.2` is the native Audio Kit PCM slice. Its implementation is in
the system library and media service; NDK headers contain declarations only.
The application receives a `PCM` service capability at startup and passes its
handle and an explicit format to `astra_pcm_open`. It does not open AstraHost
or a Linux socket.

Both input formats are **48,000 frames/second, stereo, interleaved**:
signed 24-bit little-endian (six bytes per frame) and signed 16-bit
big-endian (four bytes per frame). The latter matches SDL2's `AUDIO_S16SYS`
on the MC68040. The Linux mixer converts either to the physical 24-bit sink.
Use
`astra_pcm_write` for any frame count; it batches internally, reports how many
frames the service accepted, and returns `ASTRA_ERROR_BUSY` when the queue is
full. Retry only the remainder. Calls on the same `AstraPcmStream` must be
serialized by the caller. `astra_pcm_pause` preserves the queue while stopping
playback; `astra_pcm_clear` discards queued frames without closing the stream.
`astra_pcm_finish` drains queued audio; `astra_pcm_close` discards it. A dead
media service invalidates the stream;
open a new one rather than replaying an old handle.

This is **not yet a general game-audio API**. SDL2 can advertise a 48 kHz
`AUDIO_S16MSB` device and perform its normal conversion for applications that
request other formats and rates. That is a correctness path, not a measured
MC68040 performance claim. Chocolate Doom uses SDL2_mixer and DevilutionX's
SDL2 build uses SDL_audiolib: their sound-effect mixing remains in those
libraries until host-backed adapters are measured and built. The native mixer
already isolates multiple PCM streams, but reusable/looping samples, pan,
host-side resampling, accurate completion, and broader physical timing gates
remain before the system is game-ready. Wavetable and speech engines will
ultimately supply PCM into this same mixer; neither engine is implemented by
this PCM library.

```{doxygenfile} pcm.h
```

```{doxygenfile} pcm_format.h
```
