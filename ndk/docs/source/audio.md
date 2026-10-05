# Audio

All sound is made on the Linux audio host: it decodes, resamples and mixes
every source to one 48 kHz stereo output, and it runs the wavetable
synthesizer. The MC68040 hands it samples or MIDI and does no signal
processing. Every audio API takes the `PCM` startup capability
(`ASTRA_CAPABILITY_PCM`), which an application's manifest asks for with
`capability PCM`. The functions are in `pcm.library` (the Audio Kit); link
`-l:pcm.library.2`.

There are three ways to make sound. Pick by what the program has:

| The program has | Use | Cost per buffer |
|---|---|---|
| a steady stream it generates (a game's mixer, a player) | a buffer group, {c:func}`astra_pcm_buffers_open` | one system call, no copy |
| a sound to play once, already in memory | {c:func}`astra_pcm_open` and {c:func}`astra_pcm_write` | a round trip to the media service |
| music or notes | the wavetable synthesizer, {c:func}`astra_midi_open` | nothing per note but its few bytes |

## Streams: the buffer group

A buffer group is the way to play continuously. It is Haiku's `BBufferGroup`
and `BSoundPlayer`: the application owns a ring of equal buffers in memory
the host can read, fills them in place from its own audio thread, and hands
each to the host in order. The host's mixer takes each one as the output
clock needs it and gives it back. The media service grants the stream once,
at open, and is not involved again.

1. Open the group with {c:func}`astra_pcm_buffers_open`: a format word
   (`ASTRA_PCM_FORMAT()` -- any encoding in `pcm_format.h`, one or two
   channels, 8 to 192 kHz; the host converts it, so give samples as you
   have them), frames per buffer, and the number of buffers.
2. Loop:
   - {c:func}`astra_pcm_buffers_get` names the buffer to fill; write exactly
     the buffer's frames into it.
   - {c:func}`astra_pcm_buffers_queue` hands it over. It does not enter the
     kernel.
   - {c:func}`astra_pcm_buffers_wait` tells the host about every queued
     buffer and sleeps until one is free again. This is the loop's only
     system call.
3. Close with {c:func}`astra_pcm_buffers_close`. Sound stops at once.

Size the group as Haiku does: buffers of about 10 ms, and
`max(3, latency / buffer + 2)` of them. Latency is the group plus two
buffers in the host's queue, so 3 x 10 ms at 48 kHz (480 frames) is about
50 ms from filling a buffer to hearing it. Fewer, shorter buffers lower
latency and wake the thread more often; a thread that is late for a buffer
is heard as a gap.

Give the filling thread the priority the deadline needs, not the program:
audio is the deadline, the rest of the program is not.

A host without audio streams answers `ASTRA_ERROR_UNSUPPORTED` from
{c:func}`astra_pcm_buffers_open`. A program that must play there falls back
to {c:func}`astra_pcm_open` and {c:func}`astra_pcm_write`, as SDL2 does.

The checked example is `examples/pcm_tone.c`.

## One-shot sounds and conversion

{c:func}`astra_pcm_open` opens a voice in the media service;
{c:func}`astra_pcm_write` copies frames to it and {c:func}`astra_pcm_wait`
waits for room. Each call is a round trip through the media service, which
is fine for a sound effect and wrong for a stream.

{c:func}`astra_pcm_convert` converts a block of PCM between formats and
rates on the host -- the mixer's own resampler -- and hands it back, for a
program that wants its sounds in one format before it mixes them itself.

## The wavetable synthesizer

{c:func}`astra_midi_open` opens a SoundFont synthesizer on the host, mixed
like any other source. It plays Standard MIDI Files -- the file is sent once
({c:func}`astra_midi_load`, {c:func}`astra_midi_play`) and the MC68040 does
nothing per note -- and takes live channel messages:
{c:func}`astra_midi_note_on`, {c:func}`astra_midi_note_off` and
{c:func}`astra_midi_send` for anything else (program change, controllers,
pitch bend). A note costs four bytes.

Every synthesizer starts with the default SoundFont set listed in
`SOUND:soundfonts/default`. Stack more with
{c:func}`astra_midi_add_system_font` (a shared font, by file name) or
{c:func}`astra_midi_add_font` (the program's own bytes, which the host keeps
by digest so it is sent once). {c:func}`astra_midi_presets` lists the
instruments the stack provides; {c:func}`astra_midi_set` changes reverb,
chorus, polyphony, tempo and position.

The checked example is `examples/midi_notes.c`.
