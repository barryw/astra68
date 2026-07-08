# Lyra — Audio Chip Register Map (v0.1)

Lyra is the Astra 68 audio chip (Paula analog): 16 PCM sample-playback voices,
16 wavetable oscillator voices, and a time-multiplexed stereo mixer feeding a
48 kHz output. PCM samples stream from the SDRAM audio pool; wavetables live in
on-chip **wave RAM** (BRAM).

Authoritative contract; `sw/include/lyra.h` is the hand-maintained C mirror.

---

## 1. Overview & signal flow

```
16 PCM voices ─┐
               ├─► per-voice L/R volume ─► wide stereo accumulator ─► saturate
16 WT voices  ─┘                                                         │
                                          master volume ◄────────────────┘
                                              │
                                     48 kHz output FIFO ─► sigma-delta DAC (jack)
```

The mixer is **time-multiplexed**, not 32 parallel paths: at 50–75 MHz there are
~1000–1500 sysclk cycles per 48 kHz output sample, plenty to walk all 32 voices
serially (~30–45 cycles each: fetch → volume multiply → accumulate). PCM voices
fetch from SDRAM (tiny, ~1–2 MB/s worst case); wavetable voices read wave RAM
(BRAM, free). See SPEC §9.

---

## 2. Addressing & conventions

- **Base:** `LYRA_BASE = 0xFFF30000`.
- 32-bit registers, 4-byte stride, big-endian; RO / RW / RW1C. Supervisor-only.
- **Volume fields** pack L/R: `[15:0] left, [31:16] right` (linear). Panning is
  just independent L/R.

```
Block map (LYRA_BASE +):
  0x0000  global (id/ver/ctrl/status/irq)
  0x0020  mixer (ctrl/master-vol/rate/status)
  0x0040  PCM global (active/end/keyon/keyoff)
  0x0060  wavetable global (active/keyon/keyoff)
  0x0100  PCM voices     (16 x 32 bytes)
  0x0400  wavetable voices (16 x 32 bytes)
  0x8000  wave RAM (32 KB = 16384 x int16 samples)
```

---

## 3. Global (0x0000)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0000 | `ID` | RO | `0x4C595241` | "LYRA" |
| 0x0004 | `VERSION` | RO | `0x00010000` | major/minor |
| 0x0008 | `CTRL` | RW | 0 | global control |
| 0x000C | `STATUS` | RO | — | global status |
| 0x0010 | `IRQ_EN` | RW | 0 | `[0]PCM_END` |
| 0x0014 | `IRQ_STAT` | RW1C | 0 | pending; routes to Vesta `LYRA` line |

---

## 4. Mixer (0x0020)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0020 | `MIX_CTRL` | RW | `[0]ENABLE` (master output enable) |
| 0x0024 | `MIX_VOL` | RW | master volume `[15:0]L [31:16]R` |
| 0x0028 | `MIX_RATE` | RW | output sample-rate divisor (sysclk → 48 kHz) |
| 0x002C | `MIX_STATUS` | RO | `[0]FIFO_UNDERRUN [1]CLIP` (sticky) |

---

## 5. PCM global (0x0040)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0040 | `PCM_ACTIVE` | RO | [15:0] bitmap of voices currently playing |
| 0x0044 | `PCM_END` | RW1C | [15:0] voices that reached end since last read (IRQ source) |
| 0x0048 | `PCM_KEYON` | WO | write 1s to (re)trigger voices — `CUR ← START`, play |
| 0x004C | `PCM_KEYOFF` | WO | write 1s to stop voices |

`KEYON`/`KEYOFF` are bitmaps → start/stop several voices in one write for
sample-accurate synchronized triggers (chords, drum hits, tracker rows).

---

## 6. PCM voices (0x0100) — 16 voices

Voice `i` at `0x0100 + i*0x20`, 8 longs.

| +Off | Name | Description |
|---|---|---|
| 0x00 | `CTRL` | flags (below) |
| 0x04 | `START` | [24:0] SDRAM byte address of sample start |
| 0x08 | `LEN` | [24:0] sample length in bytes |
| 0x0C | `LOOP_START` | [24:0] loop start (byte offset from START) |
| 0x10 | `LOOP_END` | [24:0] loop end (byte offset from START) |
| 0x14 | `STEP` | 16.16 fixed-point sample step (pitch); `0x10000` = native rate |
| 0x18 | `VOL` | `[15:0]L [31:16]R` |
| 0x1C | `CUR` | RO current byte position (16.16) |

**`PCM_CTRL`** `[0]ENABLE [1]LOOP [2]PINGPONG [3]REVERSE [5:4]FORMAT
(0=8-bit unsigned, 1=16-bit signed, 2=8-bit signed) [6]IRQ_END [7]INTERP (linear)`.

Playback: read the sample at `START + (CUR>>16)`, advance `CUR += STEP`. At
`LEN`: if `LOOP`, wrap to `LOOP_START` (or reverse direction if `PINGPONG`);
else stop, set the `PCM_END` bit (and IRQ if `IRQ_END`). `STEP` for a sample
recorded at rate `R`: `STEP = (R << 16) / 48000`.

---

## 7. Wavetable global (0x0060)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0060 | `WT_ACTIVE` | RO | [15:0] bitmap of active oscillators |
| 0x0064 | `WT_KEYON` | WO | write 1s to start voices (`PHASE ← 0`) |
| 0x0068 | `WT_KEYOFF` | WO | write 1s to stop voices (or release envelope) |

---

## 8. Wavetable voices (0x0400) — 16 voices

Voice `i` at `0x0400 + i*0x20`, 8 longs.

| +Off | Name | Description |
|---|---|---|
| 0x00 | `CTRL` | `[0]ENABLE [1]ENV_EN` |
| 0x04 | `WAVE` | `[15:0]` base sample index in wave RAM, `[19:16]` table size log2 |
| 0x08 | `PHASE` | phase accumulator (RW) |
| 0x0C | `STEP` | phase step (frequency) |
| 0x10 | `VOL` | `[15:0]L [31:16]R` |
| 0x14 | `ENV` | ADSR envelope params (packed; when `ENV_EN`) |
| 0x18 | reserved ×2 | |

Each output sample: `PHASE += STEP`; index = `base + ((PHASE >> P) & (2^size-1))`;
sample = `WAVE_RAM[index]` → ×VOL (×envelope) → accumulate. Frequency ≈
`STEP * 48000 / 2^phasebits`. Wave tables are power-of-two length so phase wraps
by mask.

---

## 9. Wave RAM (0x8000) — 32 KB

`WAVE_RAM[16384]`, `int16_t` samples (16-bit signed), CPU-writable, oscillator-
readable (dual-port BRAM). Load waveforms/instruments here (a single-cycle sine,
a multisample instrument, etc.). Access with `move.w`.

---

## 10. Programming sketches

**One-shot PCM sound effect**
```c
LyraPCM *v = &LYRA->PCM[0];
v->START = sfx_addr;
v->LEN   = sfx_len;
v->STEP  = (22050u << 16) / 48000u;              // 22.05 kHz sample
v->VOL   = LYRA_VOL(0xC000, 0xC000);             // ~75% both sides
v->CTRL  = PCM_ENABLE | PCM_FMT_16S | PCM_IRQ_END;
LYRA->PCM_KEYON = (1u << 0);                       // trigger

LYRA->MIX_VOL = LYRA_VOL(0xFFFF, 0xFFFF);
LYRA->MIX_CTRL = MIX_ENABLE;
```

**A wavetable note (sine in wave RAM)**
```c
for (int i = 0; i < 256; i++)                       // 256-sample sine table @ index 0
    LYRA->WAVE_RAM[i] = (int16_t)(32767.0 * sin(2*M_PI*i/256));
LyraWT *w = &LYRA->WT[0];
w->WAVE = WT_WAVE(0, 8);                             // base 0, 2^8 = 256 samples
w->STEP = note_to_step(440);                         // A4
w->VOL  = LYRA_VOL(0x8000, 0x8000);
w->CTRL = WT_ENABLE;
LYRA->WT_KEYON = (1u << 0);
```

**Play a chord sample-accurately** — set several voices, one `KEYON`:
```c
LYRA->WT_KEYON = (1u<<0) | (1u<<1) | (1u<<2);
```

---

## 11. Decisions & open

**Decided:** L/R volume packed (panning = independent L/R) · PCM streams from
SDRAM, wavetables from BRAM wave RAM · key-on/off bitmaps for synchronized
triggers · 16.16 pitch for PCM, phase-accumulator for WT · 32 KB wave RAM.

**Open:**
1. Wave RAM **64 KB** (SPEC target) vs 32 KB — bump if EBR budget allows.
2. Envelope: full ADSR per WT voice (`ENV`) vs simple gain ramp — define packing.
3. PCM **linear interpolation** (`INTERP`) — v0.1 or defer (cost: a multiply/voice).
4. Output: sigma-delta jack (v0.1) — add I2S / HDMI-audio path later.
5. A modulation matrix / ring-mod / filters (SID-style) — stretch.
