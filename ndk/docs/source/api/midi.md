# MIDI and the wavetable synthesizer

`pcm.library` drives the Linux audio host's SoundFont synthesizer. An
`AstraMidiSynth` is one instrument on the host, mixed with every PCM stream.
It plays Standard MIDI Files, which the MC68040 sends once with nothing per
note, and it takes live MIDI messages on its 16 channels. Open one with the
same `PCM` capability `astra_pcm_open` takes.

## SoundFonts

Shared SoundFonts live in the `SOUND:` volume under `soundfonts/`. This is a
HostFS directory, so the guest and the Linux host see the same files, and
a font changed there is what the next synth plays.

- **Default stack.** `soundfonts/default` lists the fonts every synth starts
  with, lowest first.
- **Listing.** `astra_midi_fonts` lists the shared fonts, with sizes and
  which ones are defaults.
- **Stacking.** A program stacks more fonts on top: a shared one by file
  name with `astra_midi_add_system_font(synth, "TimGM6mb.sf2")`, or its own
  bytes with `astra_midi_add_font`. A preset in a later font hides the same
  bank and program in an earlier one.
- **Presets.** `astra_midi_presets` lists, for each bank and program, the
  preset that is heard and the font it comes from. It waits until every
  font has loaded.

## Songs

```c
AstraMidiSynth synth = ASTRA_MIDI_SYNTH_INIT;

if (astra_midi_open(pcm, &synth) == ASTRA_OK &&
    astra_midi_load(&synth, file, file_bytes) == ASTRA_OK &&
    astra_midi_play(&synth, ASTRA_MIDI_FOREVER) == ASTRA_OK) {
    /* ... */
}
(void)astra_midi_close(&synth);
```

- `astra_midi_status` reports the song's position and length in ticks, its
  resolution and tempo, and whether anything still sounds.
- `astra_midi_set(&synth, ASTRA_MIDI_POSITION, tick)` seeks the song.
- `ASTRA_MIDI_TEMPO` scales the song's tempo, in thousandths.

## Playing it live

`astra_midi_send` takes a batch of short MIDI messages and plays them at
once, in order after earlier calls. The inline helpers build the common
ones:

- `astra_midi_note_on` and `astra_midi_note_off`
- `astra_midi_program` (bank select, then program change)
- `astra_midi_control`
- `astra_midi_pitch_bend`
- `astra_midi_all_notes_off`

Send events in batches where you can, because each call is a round trip to
the media service.

## Effects and voices

`astra_midi_set` turns reverb and chorus on or off and sets their
parameters, and sets polyphony.

## SDL

SDL programs need none of this for music. SDL_mixer's native MIDI backend
uses it for `Mix_LoadMUS` of a MIDI file. It stacks every `.sf2` in the
application bundle's `resources/soundfonts/` on the default set, then any
fonts named through `Mix_SetSoundFonts` or `SDL_SOUNDFONTS`.

```{doxygenfile} midi.h
```
