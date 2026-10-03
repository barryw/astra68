#include <SDL.h>
#include <astra/runtime.h>

#include <stdio.h>

/* Palette expansion into 32-bit pixels against the palette itself: every
 * index, widths with each remainder, and a sub-rectangle so both surfaces
 * skip bytes between rows. */
static int blit_index8_matches(void)
{
    static const int widths[] = { 1, 2, 3, 4, 5, 7, 8, 13, 320 };
    SDL_Surface *source = SDL_CreateRGBSurface(0, 333, 9, 8, 0, 0, 0, 0);
    SDL_Surface *target = SDL_CreateRGBSurfaceWithFormat(
        0, 337, 9, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Color colors[256];
    int ok = source != NULL && target != NULL;

    for (int i = 0; ok && i < 256; ++i) {
        colors[i].r = (Uint8)(i * 7);
        colors[i].g = (Uint8)(255 - i);
        colors[i].b = (Uint8)(i ^ 0x5a);
        colors[i].a = 255;
    }
    ok = ok && SDL_SetPaletteColors(source->format->palette, colors, 0,
                                    256) == 0;
    for (int y = 0; ok && y < source->h; ++y)
        for (int x = 0; x < source->w; ++x)
            ((Uint8 *)source->pixels)[y * source->pitch + x] =
                (Uint8)(x * 31 + y * 101);
    for (unsigned w = 0; ok && w < sizeof(widths) / sizeof(widths[0]); ++w) {
        SDL_Rect from = { 3, 1, widths[w], 7 };
        SDL_Rect to = { 5, 2, widths[w], 7 };

        SDL_FillRect(target, NULL, 0x12345678u);
        ok = SDL_LowerBlit(source, &from, target, &to) == 0;
        for (int y = 0; ok && y < target->h; ++y)
            for (int x = 0; ok && x < target->w; ++x) {
                Uint32 got = ((Uint32 *)((Uint8 *)target->pixels +
                                         y * target->pitch))[x];
                Uint32 want = 0x12345678u;

                if (x >= to.x && x < to.x + to.w &&
                    y >= to.y && y < to.y + to.h) {
                    SDL_Color c = colors[((Uint8 *)source->pixels)
                        [(y - to.y + from.y) * source->pitch +
                         x - to.x + from.x]];

                    want = 0xff000000u | (Uint32)c.r << 16 |
                           (Uint32)c.g << 8 | c.b;
                }
                if (got != want) {
                    printf("SDL_BLIT_INDEX8 width=%d x=%d y=%d "
                           "got=%08lx want=%08lx\n", widths[w], x, y,
                           (unsigned long)got, (unsigned long)want);
                    ok = 0;
                }
            }
    }
    SDL_FreeSurface(target);
    SDL_FreeSurface(source);
    return ok;
}

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
    if (!blit_index8_matches()) {
        (void)astra_log("SDL_BLIT_INDEX8_FAIL");
        puts("SDL_BLIT_INDEX8_FAIL");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
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
