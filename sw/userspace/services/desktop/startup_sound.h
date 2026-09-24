#ifndef ASTRA_DESKTOP_STARTUP_SOUND_H
#define ASTRA_DESKTOP_STARTUP_SOUND_H

#include <stdint.h>

#define ASTRA_STARTUP_SOUND_RATE 48000u
#define ASTRA_STARTUP_SOUND_FRAMES 80000u

typedef struct AstraStartupSound {
    uint32_t frame;
    uint32_t phases[5];
} AstraStartupSound;

/* Render signed-16-bit, big-endian, interleaved stereo frames. */
uint32_t astra_startup_sound_fill(AstraStartupSound *sound, uint8_t *output,
                                  uint32_t capacity);

#endif
