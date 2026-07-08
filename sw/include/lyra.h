// Lyra — audio chip register interface for Astra 68.
// Mirror of docs/LYRA.md (v0.1). Keep in sync with that spec.
//
// 32-bit registers, 4-byte stride, big-endian (m68k). Supervisor-only MMIO.
#ifndef ASTRA_LYRA_H
#define ASTRA_LYRA_H

#include <stdint.h>

#define LYRA_BASE 0xFFF30000u

typedef volatile struct {   // PCM voice (0x20 bytes)
    uint32_t CTRL;       // +0x00  PCM_* bits
    uint32_t START;      // +0x04  [24:0] SDRAM sample start
    uint32_t LEN;        // +0x08  [24:0] length (bytes)
    uint32_t LOOP_START; // +0x0C  [24:0] loop start (offset from START)
    uint32_t LOOP_END;   // +0x10  [24:0] loop end (offset from START)
    uint32_t STEP;       // +0x14  16.16 pitch (0x10000 = native)
    uint32_t VOL;        // +0x18  [15:0]L [31:16]R
    uint32_t CUR;        // +0x1C  RO current position (16.16)
} LyraPCM;

typedef volatile struct {   // wavetable voice (0x20 bytes)
    uint32_t CTRL;    // +0x00  WT_* bits
    uint32_t WAVE;    // +0x04  [15:0] base index, [19:16] table size log2
    uint32_t PHASE;   // +0x08  phase accumulator
    uint32_t STEP;    // +0x0C  phase step (frequency)
    uint32_t VOL;     // +0x10  [15:0]L [31:16]R
    uint32_t ENV;     // +0x14  ADSR params (when ENV_EN)
    uint32_t _rsv[2]; // +0x18
} LyraWT;

typedef volatile struct {
    uint32_t ID;          // 0x000
    uint32_t VERSION;     // 0x004
    uint32_t CTRL;        // 0x008
    uint32_t STATUS;      // 0x00C
    uint32_t IRQ_EN;      // 0x010
    uint32_t IRQ_STAT;    // 0x014
    uint32_t _r0[2];      // 0x018..0x01C
    // mixer
    uint32_t MIX_CTRL;    // 0x020
    uint32_t MIX_VOL;     // 0x024
    uint32_t MIX_RATE;    // 0x028
    uint32_t MIX_STATUS;  // 0x02C
    uint32_t _r1[4];      // 0x030..0x03C
    // PCM global
    uint32_t PCM_ACTIVE;  // 0x040
    uint32_t PCM_END;     // 0x044
    uint32_t PCM_KEYON;   // 0x048
    uint32_t PCM_KEYOFF;  // 0x04C
    uint32_t _r2[4];      // 0x050..0x05C
    // wavetable global
    uint32_t WT_ACTIVE;   // 0x060
    uint32_t WT_KEYON;    // 0x064
    uint32_t WT_KEYOFF;   // 0x068
    uint32_t _r3[(0x100 - 0x06C) / 4];
    LyraPCM  PCM[16];     // 0x100..0x2FF
    uint32_t _r4[(0x400 - 0x300) / 4];
    LyraWT   WT[16];      // 0x400..0x5FF
    uint32_t _r5[(0x8000 - 0x600) / 4];
    int16_t  WAVE_RAM[16384]; // 0x8000..0xFFFF (32 KB)
} LyraRegs;

#define LYRA ((LyraRegs *)LYRA_BASE)

#define LYRA_ID_MAGIC 0x4C595241u   // "LYRA"

// ---- IRQ (IRQ_EN / IRQ_STAT) ----
#define LYRA_IRQ_PCM_END (1u << 0)

// ---- MIX_CTRL / MIX_STATUS ----
#define MIX_ENABLE        (1u << 0)
#define MIX_FIFO_UNDERRUN (1u << 0)   // MIX_STATUS
#define MIX_CLIP          (1u << 1)   // MIX_STATUS

// ---- PCM_CTRL ----
#define PCM_ENABLE   (1u << 0)
#define PCM_LOOP     (1u << 1)
#define PCM_PINGPONG (1u << 2)
#define PCM_REVERSE  (1u << 3)
#define PCM_FMT_8U   (0u << 4)
#define PCM_FMT_16S  (1u << 4)
#define PCM_FMT_8S   (2u << 4)
#define PCM_IRQ_END  (1u << 6)
#define PCM_INTERP   (1u << 7)

// ---- WT_CTRL / WAVE ----
#define WT_ENABLE (1u << 0)
#define WT_ENV_EN (1u << 1)
#define WT_WAVE(base, logsize) (((base) & 0xFFFFu) | (((logsize) & 0xFu) << 16))

// ---- Volume packer (PCM/WT VOL and MIX_VOL) ----
#define LYRA_VOL(l, r) (((uint32_t)(uint16_t)(r) << 16) | (uint16_t)(l))

#endif // ASTRA_LYRA_H
