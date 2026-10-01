# MIDI music

`pcm.library` 2.3 plays Standard MIDI Files on the Linux audio host's
SoundFont synthesizer. A program sends a song once; the host synthesizes it
and mixes it with every PCM stream, so the MC68040 does no per-note work.
Open a song with the same `PCM` capability `astra_pcm_open` takes.

Every song plays through Astra's default SoundFont set (GeneralUser GS).
A program can stack more fonts on top, either one of the system's own by
name (`astra_midi_add_system_font(song, "TimGM6mb")`) or the bytes of an
`.sf2` file it has read (`astra_midi_add_font`). A preset in a later font
hides the same bank and program in an earlier one. The host keeps fonts by
the SHA-256 of their contents, so a font it has already seen is not stored
twice.

```c
AstraMidiSong song = ASTRA_MIDI_SONG_INIT;

if (astra_midi_open(pcm, &song) == ASTRA_OK &&
    astra_midi_load(&song, file, file_bytes) == ASTRA_OK &&
    astra_midi_play(&song, ASTRA_MIDI_FOREVER) == ASTRA_OK) {
    /* ... */
}
(void)astra_midi_close(&song);
```

`astra_midi_load` replaces the song and stops playback. `astra_midi_pause`
holds the song where it is and releases held notes. `astra_midi_stop` ends
it at once. `astra_midi_active` reports whether it is still playing or its
last notes are still ringing. Each call is a round trip to the media
service, so ask `astra_midi_active` a few times a second, not once per audio
buffer. Calls on one `AstraMidiSong` must be serialized by the caller.

SDL programs need none of this. SDL_mixer's native MIDI backend uses it for
`Mix_LoadMUS` of a MIDI file, and fonts named through `Mix_SetSoundFonts` or
`SDL_SOUNDFONTS` are stacked on the default set.

```{doxygenfile} midi.h
```
