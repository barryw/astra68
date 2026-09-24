#include "../startup_sound.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    AstraStartupSound sound = {0};
    uint8_t output[1024u * 4u];
    uint32_t audible = 0u;
    uint32_t silent_tail = 0u;
    int32_t peak = 0;

    assert(astra_startup_sound_fill(NULL, output, 1u) == 0u);
    assert(astra_startup_sound_fill(&sound, NULL, 1u) == 0u);
    assert(sound.frame == 0u);
    while (sound.frame < ASTRA_STARTUP_SOUND_FRAMES) {
        uint32_t start = sound.frame;
        uint32_t count = astra_startup_sound_fill(&sound, output, 1024u);

        assert(count != 0u && count <= 1024u);
        for (uint32_t i = 0u; i < count; ++i) {
            int16_t sample = (int16_t)(((uint16_t)output[i * 4u] << 8) |
                                       output[i * 4u + 1u]);
            int32_t magnitude = sample < 0 ? -(int32_t)sample : sample;

            assert(output[i * 4u] == output[i * 4u + 2u]);
            assert(output[i * 4u + 1u] == output[i * 4u + 3u]);
            if (magnitude > peak)
                peak = magnitude;
            if (sample != 0)
                ++audible;
            if (start + i >= 79200u) {
                assert(sample == 0);
                ++silent_tail;
            }
        }
    }
    assert(sound.frame == ASTRA_STARTUP_SOUND_FRAMES);
    assert(astra_startup_sound_fill(&sound, output, 1024u) == 0u);
    assert(audible > ASTRA_STARTUP_SOUND_RATE);
    assert(peak > 1000 && peak < 16000);
    assert(silent_tail == 800u);
    return 0;
}
