# PCM playback

`pcm.library.2` is the Audio Kit. The guide is [Audio](../audio.md): which
of its three ways to use, the buffer-group loop and its sizing, one-shot
voices, conversion and the wavetable synthesizer.

Every function takes the `PCM` startup capability. Formats are
`ASTRA_PCM_FORMAT()` words: any encoding in `pcm_format.h`, one or two
channels, 8 to 192 kHz. The Linux host converts and resamples each source to
its 48 kHz stereo mix, so a program gives samples as it has them.

- **Buffer groups** (2.5): {c:func}`astra_pcm_buffers_open`,
  {c:func}`astra_pcm_buffers_get`, {c:func}`astra_pcm_buffers_queue`,
  {c:func}`astra_pcm_buffers_wait`, {c:func}`astra_pcm_buffers_close`. The
  application's own buffers, read by the host in place, on a kernel audio
  stream (`audio_stream.h`). SDL2's audio device is one.
- **Voices** (2.0, 2.1): {c:func}`astra_pcm_open`, {c:func}`astra_pcm_write`,
  {c:func}`astra_pcm_wait` and the controls. Each call is a round trip
  through the media service. Calls on one `AstraPcmStream` must be
  serialized by the caller; a dead media service invalidates it.
- **Conversion** (2.2): {c:func}`astra_pcm_convert` on the host, with the
  mixer's resampler. The result is exactly
  `floor(source_frames * target_rate / source_rate)` frames. SDL2's
  `SDL_BuildAudioCVT` uses it for every conversion the host can take.

```{doxygenfile} pcm.h
:project: astra-ndk
```

```{doxygenfile} pcm_format.h
:project: astra-ndk
```

```{doxygenfile} audio_stream.h
:project: astra-ndk
```
