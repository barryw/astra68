#include <SDL.h>

#include <stdio.h>

int main(void)
{
    const char *driver;

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("SDL_INIT_FAIL %s\n", SDL_GetError());
        return 1;
    }
    driver = SDL_GetCurrentAudioDriver();
    printf("SDL_DRIVER %s\n", driver == NULL ? "none" : driver);
    SDL_Quit();
    return driver == NULL ? 1 : 0;
}
