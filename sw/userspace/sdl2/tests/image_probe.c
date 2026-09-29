#include <SDL_image.h>
#include <astra/runtime.h>

static int check_image(const char *path, const char *failure)
{
    SDL_Surface *surface = IMG_Load(path);
    int valid = surface != NULL && surface->w == 23 && surface->h == 42;

    if (!valid) {
        (void)astra_log(failure);
        (void)astra_log(SDL_GetError());
    }
    if (surface != NULL)
        SDL_FreeSurface(surface);
    return valid;
}

static int check_round_trip(const char *path, int jpeg)
{
    unsigned char bytes[65536];
    SDL_Surface *original = IMG_Load(path), *decoded = NULL;
    SDL_RWops *stream = NULL;
    int valid = 0;

    if (original == NULL)
        goto done;
    stream = SDL_RWFromMem(bytes, sizeof(bytes));
    if (stream == NULL)
        goto done;
    if ((jpeg ? IMG_SaveJPG_RW(original, stream, 0, 80) :
                IMG_SavePNG_RW(original, stream, 0)) != 0 ||
        SDL_RWseek(stream, 0, RW_SEEK_SET) != 0)
        goto done;
    decoded = IMG_Load_RW(stream, 0);
    valid = decoded != NULL && decoded->w == original->w &&
            decoded->h == original->h;
done:
    if (!valid)
        (void)astra_log(SDL_GetError());
    if (decoded != NULL)
        SDL_FreeSurface(decoded);
    if (stream != NULL)
        SDL_RWclose(stream);
    if (original != NULL)
        SDL_FreeSurface(original);
    return valid;
}

/* The fixtures ship in the bundle; SDL finds them as upstream demos do. */
static char base[256];

static const char *resource(const char *name)
{
    static char path[320];

    SDL_snprintf(path, sizeof(path), "%s%s", base, name);
    return path;
}

/* The application's STORE, through SDL's preference path: written, then
   read back. */
static int check_preferences(void)
{
    static const char text[] = "astra";
    char buffer[sizeof(text)] = {0};
    char *directory = SDL_GetPrefPath("Astra", "SDLImageProbe");
    char path[320];
    SDL_RWops *stream;
    int valid = 0;

    if (directory == NULL)
        return 0;
    SDL_snprintf(path, sizeof(path), "%sprobe.txt", directory);
    SDL_free(directory);
    stream = SDL_RWFromFile(path, "wb");
    if (stream == NULL ||
        SDL_RWwrite(stream, text, 1, sizeof(text)) != sizeof(text)) {
        if (stream != NULL)
            SDL_RWclose(stream);
        return 0;
    }
    SDL_RWclose(stream);
    stream = SDL_RWFromFile(path, "rb");
    if (stream != NULL) {
        valid = SDL_RWread(stream, buffer, 1, sizeof(buffer)) ==
                    sizeof(text) &&
                SDL_memcmp(buffer, text, sizeof(text)) == 0;
        SDL_RWclose(stream);
    }
    return valid;
}

int main(void)
{
    static const char broken_png[] = "\211PNG\r\n\032\n";
    SDL_RWops *source;
    SDL_Surface *surface;
    char *base_path = SDL_GetBasePath();
    int available;

    if (base_path == NULL) {
        (void)astra_log("SDL_IMAGE_BASE_PATH_FAIL");
        (void)astra_log(SDL_GetError());
        return 1;
    }
    SDL_strlcpy(base, base_path, sizeof(base));
    SDL_free(base_path);
    if (!check_preferences()) {
        (void)astra_log("SDL_IMAGE_PREF_PATH_FAIL");
        (void)astra_log(SDL_GetError());
        return 1;
    }
    available = IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    if ((available & (IMG_INIT_PNG | IMG_INIT_JPG)) !=
        (IMG_INIT_PNG | IMG_INIT_JPG)) {
        (void)astra_log("SDL_IMAGE_INIT_FAIL");
        return 1;
    }
    if (!check_image(resource("sample.png"),
                     "SDL_IMAGE_PNG_FAIL") ||
        !check_image(resource("sample.jpg"),
                     "SDL_IMAGE_JPEG_FAIL") ||
        !check_image(resource("sample.qoi"),
                     "SDL_IMAGE_QOI_FAIL") ||
        !check_image(resource("misnamed.jpg"),
                     "SDL_IMAGE_MISNAMED_FAIL")) {
        IMG_Quit();
        return 1;
    }
    if (!check_round_trip(resource("sample.png"), 0) ||
        !check_round_trip(resource("sample.jpg"), 1)) {
        (void)astra_log("SDL_IMAGE_SAVE_FAIL");
        IMG_Quit();
        return 1;
    }
    if (IMG_SavePNG_RW(NULL, NULL, 0) == 0 ||
        IMG_SaveJPG_RW(NULL, NULL, 0, 80) == 0) {
        (void)astra_log("SDL_IMAGE_BAD_SAVE_FAIL");
        IMG_Quit();
        return 1;
    }
    source = SDL_RWFromConstMem(broken_png, sizeof(broken_png) - 1u);
    if (source == NULL) {
        (void)astra_log("SDL_IMAGE_RW_FAIL");
        IMG_Quit();
        return 1;
    }
    surface = IMG_Load_RW(source, 1);
    if (surface != NULL) {
        SDL_FreeSurface(surface);
        (void)astra_log("SDL_IMAGE_BAD_DATA_FAIL");
        IMG_Quit();
        return 1;
    }
    if (IMG_Load(resource("missing.png")) != NULL) {
        (void)astra_log("SDL_IMAGE_MISSING_FILE_FAIL");
        IMG_Quit();
        return 1;
    }
    IMG_Quit();
    (void)astra_log("SDL_IMAGE_READY");
    return 0;
}
