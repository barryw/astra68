#include "startup_sound.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    AstraStartupSound sound = {0};
    uint8_t buffer[1024u * 4u];
    FILE *output;

    if (argc != 2)
        return 2;
    output = fopen(argv[1], "wb");
    if (output == NULL)
        return 1;
    while (sound.frame < ASTRA_STARTUP_SOUND_FRAMES) {
        uint32_t count = astra_startup_sound_fill(&sound, buffer, 1024u);

        if (count == 0u || fwrite(buffer, 4u, count, output) != count) {
            (void)fclose(output);
            return 1;
        }
    }
    return fclose(output) == 0 ? 0 : 1;
}
