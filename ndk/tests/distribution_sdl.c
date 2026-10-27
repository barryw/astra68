#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_net.h>
#include <SDL_ttf.h>
#include <stdio.h>

int main(void)
{
    const int codecs = MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC;
    if (SDL_Init(0) != 0)
        return 1;
    if (IMG_Linked_Version()->major != SDL_IMAGE_MAJOR_VERSION ||
        Mix_Linked_Version()->major != SDL_MIXER_MAJOR_VERSION ||
        SDLNet_Linked_Version()->major != SDL_NET_MAJOR_VERSION ||
        TTF_Linked_Version()->major != SDL_TTF_MAJOR_VERSION)
        return 1;
    if ((Mix_Init(codecs) & codecs) != codecs)
        return 1;
    Mix_Quit();
    SDL_Quit();
    return puts("Astra CMake SDL2") < 0;
}
