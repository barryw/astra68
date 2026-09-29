#include <SDL.h>
#include <astra/runtime.h>

#include <stdio.h>

int main(void)
{
    SDL_Window *window;
    SDL_Surface *surface;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        (void)astra_log("SDL_VIDEO_INIT_FAIL");
        printf("SDL_VIDEO_INIT_FAIL %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("SDL Video Probe", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 320, 200,
                              SDL_WINDOW_SHOWN);
    if (window == NULL) {
        (void)astra_log("SDL_WINDOW_FAIL");
        printf("SDL_WINDOW_FAIL %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    surface = SDL_GetWindowSurface(window);
    if (surface == NULL ||
        SDL_FillRect(surface, NULL,
                     SDL_MapRGB(surface->format, 255u, 0u, 0u)) != 0 ||
        SDL_UpdateWindowSurface(window) != 0) {
        (void)astra_log("SDL_PRESENT_FAIL");
        printf("SDL_PRESENT_FAIL %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 3;
    }
    puts("SDL_VIDEO_READY");
    (void)astra_log("SDL_VIDEO_READY");
    SDL_Delay(10000u);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
