/*
 * The software-renderer path games take (Doom, DevilutionX, most SDL ports):
 * one streaming texture the size of the window, a whole new frame uploaded,
 * copied to the window and presented every frame, with no vsync. The frames
 * are drawn before timing starts and alternate, so each measured frame is
 * only the SDL upload, copy and present.
 *
 * Launched without arguments (from the desktop) it cycles through the
 * texture modes games use -- ARGB8888 with SDL's default blending, ARGB8888
 * opaque, RGB565 -- for three windows each, then starts over. Every window
 * of two seconds logs
 *   SDL_FRAME_BENCH mode=M fps=N upload_us=N copy_us=N present_us=N
 * usage: SDLFrameBench [WIDTH HEIGHT [argb-blend|argb|rgb565 [update|lock]]]
 */
#include <SDL.h>
#include <astra/runtime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_COUNT 2
#define WINDOWS_PER_MODE 3

typedef struct BenchMode {
    const char *name;
    Uint32 format;
    SDL_BlendMode blend;
} BenchMode;

static const BenchMode modes[] = {
    { "argb-blend", SDL_PIXELFORMAT_ARGB8888, SDL_BLENDMODE_BLEND },
    { "argb", SDL_PIXELFORMAT_ARGB8888, SDL_BLENDMODE_NONE },
    { "rgb565", SDL_PIXELFORMAT_RGB565, SDL_BLENDMODE_NONE },
};

static int fail(const char *marker)
{
    (void)astra_log(marker);
    printf("%s %s\n", marker, SDL_GetError());
    return 1;
}

static Uint32 micros(Uint64 ticks, Uint64 frequency, Uint32 frames)
{
    return frames == 0u ? 0u :
        (Uint32)(ticks * 1000000u / frequency / frames);
}

static int prepare(SDL_Renderer *renderer, const BenchMode *mode, int width,
                   int height, SDL_Texture **texture,
                   Uint8 *frames[FRAME_COUNT], int *pitch)
{
    if (*texture != NULL)
        SDL_DestroyTexture(*texture);
    *texture = SDL_CreateTexture(renderer, mode->format,
                                 SDL_TEXTUREACCESS_STREAMING, width, height);
    if (*texture == NULL || SDL_SetTextureBlendMode(*texture, mode->blend))
        return 0;
    *pitch = width * SDL_BYTESPERPIXEL(mode->format);
    for (int index = 0; index < FRAME_COUNT; ++index)
        /* Opaque pixels: the alpha byte of ARGB is 0xff, as a game's is. */
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < *pitch; ++x)
                frames[index][y * *pitch + x] =
                    mode->format == SDL_PIXELFORMAT_ARGB8888 && (x & 3) == 3 ?
                        0xffu : (Uint8)((x ^ y) + index * 97);
    return 1;
}

int main(int argc, char **argv)
{
    int width = argc > 2 ? atoi(argv[1]) : 640;
    int height = argc > 2 ? atoi(argv[2]) : 480;
    int fixed = -1;
    int lock = argc > 4 && strcmp(argv[4], "lock") == 0;
    int mode_index = 0;
    int pitch = 0;
    Uint8 *frames[FRAME_COUNT];
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture = NULL;
    Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 upload = 0u, copy = 0u, present = 0u;
    Uint64 window_start;
    Uint32 count = 0u;
    Uint32 windows = 0u;
    char line[160];

    for (int index = 0; argc > 3 &&
         index < (int)(sizeof(modes) / sizeof(modes[0])); ++index)
        if (strcmp(argv[3], modes[index].name) == 0)
            fixed = mode_index = index;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        (argc > 3 && fixed < 0))
        return fail("SDL_FRAME_BENCH_ARGS_FAIL");
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return fail("SDL_FRAME_BENCH_INIT_FAIL");
    window = SDL_CreateWindow("SDL Frame Bench", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_SHOWN);
    renderer = window != NULL ? SDL_CreateRenderer(window, -1, 0) : NULL;
    if (renderer == NULL)
        return fail("SDL_FRAME_BENCH_CREATE_FAIL");
    for (int index = 0; index < FRAME_COUNT; ++index) {
        frames[index] = SDL_malloc((size_t)width * 4u * (size_t)height);
        if (frames[index] == NULL)
            return fail("SDL_FRAME_BENCH_MEMORY_FAIL");
    }
    if (!prepare(renderer, &modes[mode_index], width, height, &texture,
                 frames, &pitch))
        return fail("SDL_FRAME_BENCH_CREATE_FAIL");
    (void)astra_log("SDL_FRAME_BENCH_READY");
    window_start = SDL_GetPerformanceCounter();
    for (Uint32 frame = 0u;; ++frame) {
        const Uint8 *pixels = frames[frame % FRAME_COUNT];
        Uint64 start = SDL_GetPerformanceCounter();
        Uint64 uploaded;
        Uint64 copied;
        Uint64 end;
        SDL_Event event;

        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT)
                return 0;
        if (lock) {
            void *target;
            int target_pitch;

            if (SDL_LockTexture(texture, NULL, &target, &target_pitch) != 0)
                return fail("SDL_FRAME_BENCH_LOCK_FAIL");
            for (int y = 0; y < height; ++y)
                SDL_memcpy((Uint8 *)target + y * target_pitch,
                           pixels + y * pitch, (size_t)pitch);
            SDL_UnlockTexture(texture);
        } else if (SDL_UpdateTexture(texture, NULL, pixels, pitch) != 0) {
            return fail("SDL_FRAME_BENCH_UPLOAD_FAIL");
        }
        uploaded = SDL_GetPerformanceCounter();
        if (SDL_RenderCopy(renderer, texture, NULL, NULL) != 0)
            return fail("SDL_FRAME_BENCH_COPY_FAIL");
        copied = SDL_GetPerformanceCounter();
        SDL_RenderPresent(renderer);
        end = SDL_GetPerformanceCounter();
        upload += uploaded - start;
        copy += copied - uploaded;
        present += end - copied;
        ++count;
        if (end - window_start >= 2u * frequency) {
            SDL_snprintf(line, sizeof(line),
                         "SDL_FRAME_BENCH mode=%s fps=%u upload_us=%u "
                         "copy_us=%u present_us=%u",
                         modes[mode_index].name,
                         (unsigned)(count * frequency / (end - window_start)),
                         (unsigned)micros(upload, frequency, count),
                         (unsigned)micros(copy, frequency, count),
                         (unsigned)micros(present, frequency, count));
            (void)astra_log(line);
            printf("%s\n", line);
            upload = copy = present = 0u;
            count = 0u;
            if (fixed < 0 && ++windows % WINDOWS_PER_MODE == 0) {
                mode_index = (mode_index + 1) %
                             (int)(sizeof(modes) / sizeof(modes[0]));
                if (!prepare(renderer, &modes[mode_index], width, height,
                             &texture, frames, &pitch))
                    return fail("SDL_FRAME_BENCH_CREATE_FAIL");
            }
            window_start = SDL_GetPerformanceCounter();
        }
    }
}
