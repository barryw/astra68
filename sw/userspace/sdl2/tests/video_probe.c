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
    SDL_Joystick *joystick;
    SDL_GameController *physical = NULL;
    Uint32 probe_duration_ms;
    int virtual_index;
    int mapped_count = 0;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        (void)astra_log("SDL_VIDEO_INIT_FAIL");
        printf("SDL_VIDEO_INIT_FAIL %s\n", SDL_GetError());
        return 1;
    }
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(index);
        char guid_text[33];

        SDL_JoystickGetGUIDString(guid, guid_text, sizeof(guid_text));
        printf("SDL_PHYSICAL_GAMEPAD name=%s guid=%s controller=%s\n",
               SDL_JoystickNameForIndex(index), guid_text,
               SDL_IsGameController(index) ?
                   SDL_GameControllerNameForIndex(index) : "unmapped");
        if (SDL_IsGameController(index)) {
            ++mapped_count;
            (void)astra_log("SDL_GAMEPAD_READY");
            if (physical == NULL)
                physical = SDL_GameControllerOpen(index);
        }
    }
    if (mapped_count > 0 && physical == NULL) {
        (void)astra_log("SDL_GAMEPAD_OPEN_FAIL");
        printf("SDL_GAMEPAD_OPEN_FAIL %s\n", SDL_GetError());
        SDL_Quit();
        return 6;
    }
    virtual_index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                               2, 1, 0);
    joystick = virtual_index >= 0 ? SDL_JoystickOpen(virtual_index) : NULL;
    if (joystick == NULL || SDL_JoystickInstanceID(joystick) < 0) {
        (void)astra_log("SDL_JOYSTICK_INIT_FAIL");
        printf("SDL_JOYSTICK_INIT_FAIL %s\n", SDL_GetError());
        SDL_Quit();
        return 5;
    }
    SDL_JoystickClose(joystick);
    SDL_JoystickDetachVirtual(virtual_index);
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
    probe_duration_ms = physical != NULL ? 120000u : 10000u;
    for (Uint32 elapsed = 0u; elapsed < probe_duration_ms; elapsed += 10u) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_CONTROLLERDEVICEADDED ||
                event.type == SDL_CONTROLLERDEVICEREMOVED) {
                const char *marker = event.type == SDL_CONTROLLERDEVICEADDED ?
                    "SDL_GAMEPAD_ADDED" : "SDL_GAMEPAD_REMOVED";

                printf("%s which=%ld\n", marker,
                       (long)event.cdevice.which);
                (void)astra_log(marker);
                if (event.type == SDL_CONTROLLERDEVICEREMOVED &&
                    physical != NULL &&
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(physical)) ==
                        event.cdevice.which) {
                    SDL_GameControllerClose(physical);
                    physical = NULL;
                } else if (event.type == SDL_CONTROLLERDEVICEADDED &&
                           physical == NULL &&
                           SDL_IsGameController(event.cdevice.which)) {
                    physical = SDL_GameControllerOpen(event.cdevice.which);
                    (void)astra_log(physical != NULL ?
                        "SDL_GAMEPAD_REOPENED" :
                        "SDL_GAMEPAD_REOPEN_FAIL");
                }
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                (event.type == SDL_CONTROLLERAXISMOTION &&
                 (event.caxis.value < -8000 || event.caxis.value > 8000))) {
                printf("SDL_GAMEPAD_INPUT type=%lu control=%u value=%d\n",
                       (unsigned long)event.type,
                       event.type == SDL_CONTROLLERBUTTONDOWN ?
                           event.cbutton.button : event.caxis.axis,
                       event.type == SDL_CONTROLLERBUTTONDOWN ?
                           event.cbutton.state : event.caxis.value);
                (void)astra_log("SDL_GAMEPAD_INPUT");
            }
        }
        SDL_Delay(10u);
    }
    if (physical != NULL)
        SDL_GameControllerClose(physical);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
