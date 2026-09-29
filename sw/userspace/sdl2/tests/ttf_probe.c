#include <SDL_ttf.h>
#include <astra/runtime.h>

static const char font_path[] =
    "/apps/SDLTTFProbe.app/resources/AtkinsonHyperlegibleNext-Regular.ttf";

int main(void)
{
    static const unsigned char invalid_font[] = {0u, 1u, 2u, 3u};
    SDL_Color white = {255u, 255u, 255u, 255u};
    SDL_RWops *bad_source;
    SDL_Surface *surface;
    TTF_Font *font;
    int width = 0, height = 0;

    if (TTF_Init() != 0) {
        (void)astra_log("SDL_TTF_INIT_FAIL");
        (void)astra_log(TTF_GetError());
        return 1;
    }
    font = TTF_OpenFont(font_path, 24);
    if (font == NULL) {
        (void)astra_log("SDL_TTF_OPEN_FAIL");
        (void)astra_log(TTF_GetError());
        TTF_Quit();
        return 1;
    }
    surface = TTF_RenderUTF8_Blended(font, "Astra 68", white);
    if (TTF_SizeUTF8(font, "Astra 68", &width, &height) != 0 ||
        width <= 0 || height <= 0 || surface == NULL ||
        surface->w != width || surface->h != height) {
        (void)astra_log("SDL_TTF_RENDER_FAIL");
        (void)astra_log(TTF_GetError());
        if (surface != NULL)
            SDL_FreeSurface(surface);
        TTF_CloseFont(font);
        TTF_Quit();
        return 1;
    }
    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
    font = TTF_OpenFont("/apps/SDLTTFProbe.app/resources/missing.ttf", 24);
    if (font != NULL) {
        (void)astra_log("SDL_TTF_MISSING_FAIL");
        TTF_CloseFont(font);
        TTF_Quit();
        return 1;
    }
    bad_source = SDL_RWFromConstMem(invalid_font, sizeof(invalid_font));
    if (bad_source == NULL) {
        (void)astra_log("SDL_TTF_BAD_SOURCE_FAIL");
        TTF_Quit();
        return 1;
    }
    font = TTF_OpenFontRW(bad_source, 1, 24);
    if (font != NULL) {
        (void)astra_log("SDL_TTF_INVALID_FAIL");
        TTF_CloseFont(font);
        TTF_Quit();
        return 1;
    }
    TTF_Quit();
    (void)astra_log("SDL_TTF_READY");
    return 0;
}
