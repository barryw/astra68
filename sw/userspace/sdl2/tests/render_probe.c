/*
 * Drives every texture-engine path of the astra renderer through SDL's
 * public API: color modulation, ADD/MOD/MUL, linear scaling, rotation,
 * geometry, an ARGB8888 target, and readback with and without conversion.
 * Each failure logs its own marker; success logs SDL_RENDER_READY.
 */
#include <SDL.h>
#include <astra/runtime.h>

#include <stdio.h>

static int fail(const char *marker)
{
    (void)astra_log(marker);
    printf("%s %s\n", marker, SDL_GetError());
    return 1;
}

int main(void)
{
    static Uint32 pixels[32 * 32];
    static Uint32 readback[64 * 64];
    static const SDL_Vertex triangles[4] = {
        { { 10.0f, 150.0f }, { 255, 0, 0, 255 }, { 0.0f, 0.0f } },
        { { 60.0f, 150.0f }, { 0, 255, 0, 255 }, { 1.0f, 0.0f } },
        { { 10.0f, 190.0f }, { 0, 0, 255, 255 }, { 0.0f, 1.0f } },
        { { 60.0f, 190.0f }, { 255, 255, 255, 128 }, { 1.0f, 1.0f } },
    };
    static const int indices[6] = { 0, 1, 2, 2, 1, 3 };
    SDL_RendererInfo info;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *sprite;
    SDL_Texture *target;
    SDL_Rect whole = { 0, 0, 64, 64 };

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return fail("SDL_RENDER_INIT_FAIL");
    window = SDL_CreateWindow("SDL Render Probe", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 320, 200,
                              SDL_WINDOW_SHOWN);
    renderer = window != NULL ?
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (renderer == NULL || SDL_GetRendererInfo(renderer, &info) != 0 ||
        SDL_strcmp(info.name, "astra") != 0)
        return fail("SDL_RENDER_CREATE_FAIL");
    for (int index = 0; index < 32 * 32; ++index)
        pixels[index] = 0x80000000u | (Uint32)index * 0x010203u;
    sprite = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STATIC, 32, 32);
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_TARGET, 64, 64);
    if (sprite == NULL || target == NULL ||
        SDL_UpdateTexture(sprite, NULL, pixels, 32 * 4) != 0)
        return fail("SDL_RENDER_TEXTURE_FAIL");

    /* Into the ARGB8888 target: a transparent clear, a MOD fill, and an
       ADD copy with color modulation and linear scaling. */
    if (SDL_SetRenderTarget(renderer, target) != 0 ||
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) != 0 ||
        SDL_RenderClear(renderer) != 0 ||
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_MOD) != 0 ||
        SDL_SetRenderDrawColor(renderer, 200, 100, 50, 255) != 0 ||
        SDL_RenderFillRect(renderer, &(SDL_Rect){ 4, 4, 40, 40 }) != 0 ||
        SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_ADD) != 0 ||
        SDL_SetTextureColorMod(sprite, 255, 128, 64) != 0 ||
        SDL_SetTextureScaleMode(sprite, SDL_ScaleModeLinear) != 0 ||
        SDL_RenderCopy(renderer, sprite, NULL, &whole) != 0)
        return fail("SDL_RENDER_TARGET_FAIL");
    if (SDL_RenderReadPixels(renderer, &whole, SDL_PIXELFORMAT_ARGB8888,
                             readback, 64 * 4) != 0)
        return fail("SDL_RENDER_READ_TARGET_FAIL");

    /* Onto the window: the target blended back, a rotated MUL copy, a
       geometry fan, and a translucent line. */
    SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_MUL);
    SDL_SetTextureColorMod(sprite, 255, 255, 255);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    if (SDL_SetRenderTarget(renderer, NULL) != 0 ||
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) != 0 ||
        SDL_SetRenderDrawColor(renderer, 30, 60, 90, 255) != 0 ||
        SDL_RenderClear(renderer) != 0 ||
        SDL_RenderCopy(renderer, target, NULL,
                       &(SDL_Rect){ 200, 20, 64, 64 }) != 0 ||
        SDL_RenderCopyEx(renderer, sprite, NULL,
                         &(SDL_Rect){ 100, 40, 64, 32 }, 30.0, NULL,
                         SDL_FLIP_VERTICAL) != 0 ||
        SDL_RenderGeometry(renderer, sprite, triangles, 4, indices, 6) != 0 ||
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) != 0 ||
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 96) != 0 ||
        SDL_RenderDrawLine(renderer, 0, 0, 319, 199) != 0)
        return fail("SDL_RENDER_DRAW_FAIL");
    SDL_RenderPresent(renderer);
    /* The window in its own RGB565 and converted to ABGR8888. */
    if (SDL_RenderReadPixels(renderer, &(SDL_Rect){ 0, 0, 16, 16 },
                             SDL_PIXELFORMAT_RGB565, readback, 16 * 2) != 0 ||
        SDL_RenderReadPixels(renderer, &(SDL_Rect){ 0, 0, 16, 16 },
                             SDL_PIXELFORMAT_ABGR8888, readback,
                             16 * 4) != 0)
        return fail("SDL_RENDER_READ_WINDOW_FAIL");
    puts("SDL_RENDER_READY");
    (void)astra_log("SDL_RENDER_READY");
    SDL_Delay(10000u);
    SDL_DestroyTexture(target);
    SDL_DestroyTexture(sprite);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
