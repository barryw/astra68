/* SDL.h is the only include: programs written against SDL rely on it for
   the C library prototypes. A missing HAVE_*_H in SDL_config_minimal.h is
   an implicit declaration here, which the build makes an error. */
#include <SDL.h>

int sdl_libc_prelude(const char *text, char *copy, size_t size)
{
    float root = sqrtf((float)atof(text)) + (float)sin(0.5);

    memcpy(copy, text, size);
    if (!isdigit((unsigned char)copy[0])) {
        exit(1);
    }
    return rand() + (int)root;
}
