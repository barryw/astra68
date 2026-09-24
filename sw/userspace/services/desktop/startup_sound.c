#include "startup_sound.h"

#include <stddef.h>

typedef struct SoundNote {
    uint32_t start;
    uint32_t end;
    uint32_t step;
    uint16_t gain;
} SoundNote;

/* A short Astra motif: a rising, overlapping G-C-E-G-D constellation. */
#define NOTE_STEP(millihertz) ((uint32_t)((((uint64_t)(millihertz)) << 32) / \
                                          (ASTRA_STARTUP_SOUND_RATE * 1000u)))
static const SoundNote notes[5] = {
    {    0u, 60000u, NOTE_STEP(392000u), 3200u },
    { 4800u, 64800u, NOTE_STEP(523251u), 3500u },
    { 9600u, 69600u, NOTE_STEP(659255u), 3500u },
    {14400u, 74400u, NOTE_STEP(783991u), 3300u },
    {24000u, 79200u, NOTE_STEP(1174659u), 2500u }
};

static const int16_t quarter_sine[17] = {
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170,
    25330, 27245, 28898, 30273, 31356, 32138, 32610, 32767
};

static int32_t sine_point(uint32_t index)
{
    index &= 63u;
    if (index <= 16u)
        return quarter_sine[index];
    if (index <= 32u)
        return quarter_sine[32u - index];
    if (index <= 48u)
        return -quarter_sine[index - 32u];
    return -quarter_sine[64u - index];
}

static int32_t sine_sample(uint32_t phase)
{
    uint32_t index = phase >> 26;
    uint32_t fraction = (phase >> 16) & 1023u;
    int32_t first = sine_point(index);
    int32_t second = sine_point(index + 1u);

    return first + (((second - first) * (int32_t)fraction) >> 10);
}

uint32_t astra_startup_sound_fill(AstraStartupSound *sound, uint8_t *output,
                                  uint32_t capacity)
{
    uint32_t count;

    if (sound == NULL || output == NULL || sound->frame >=
        ASTRA_STARTUP_SOUND_FRAMES)
        return 0u;
    count = ASTRA_STARTUP_SOUND_FRAMES - sound->frame;
    if (count > capacity)
        count = capacity;
    for (uint32_t frame = 0u; frame < count; ++frame) {
        int32_t mixed = 0;

        for (uint32_t index = 0u; index < 5u; ++index) {
            const SoundNote *note = &notes[index];
            uint32_t position;
            uint32_t remaining;
            uint32_t gain;

            if (sound->frame < note->start || sound->frame >= note->end)
                continue;
            position = sound->frame - note->start;
            remaining = note->end - sound->frame;
            gain = note->gain;
            if (position < 240u)
                gain = gain * position / 240u;
            if (remaining < 9600u)
                gain = gain * remaining / 9600u;
            mixed += (sine_sample(sound->phases[index]) * (int32_t)gain) >> 15;
            sound->phases[index] += note->step;
        }
        /* The sum of all five peak gains is below signed-16-bit full scale. */
        output[frame * 4u] = (uint8_t)((uint32_t)mixed >> 8);
        output[frame * 4u + 1u] = (uint8_t)mixed;
        output[frame * 4u + 2u] = output[frame * 4u];
        output[frame * 4u + 3u] = output[frame * 4u + 1u];
        ++sound->frame;
    }
    return count;
}
