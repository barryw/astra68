/*
 * SDL's two directories, as Astra grants them.
 *
 * The base path is the application bundle's resources, reached through the
 * APP namespace the supervisor grants every application it launches. SDL
 * answers the same for an application bundle on other systems, and it is where
 * upstream programs look for files they ship with (testutils.c).
 *
 * The preference path is the application's STORE: private storage the
 * supervisor provisions when the manifest asks for it. Preferences are kept
 * apart by that capability rather than by an organisation and application
 * naming convention, so org and app do not take part in finding it.
 *
 * Neither exists outside a launched bundle, and SDL is told so rather than
 * given a directory that would fail later for a reason nobody can see.
 */
#include "../../SDL_internal.h"

#ifdef SDL_FILESYSTEM_ASTRA

#include "SDL_error.h"
#include "SDL_filesystem.h"
#include "SDL_stdinc.h"

#include <sys/stat.h>

static char *granted_directory(const char *directory, const char *missing)
{
    struct stat info;
    size_t length = SDL_strlen(directory);
    char *path;

    if (stat(directory, &info) != 0 || !S_ISDIR(info.st_mode)) {
        SDL_SetError("%s", missing);
        return NULL;
    }
    /* SDL's contract: the path ends in a separator. */
    path = SDL_malloc(length + 2u);
    if (path == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }
    SDL_memcpy(path, directory, length);
    path[length] = '/';
    path[length + 1u] = '\0';
    return path;
}

char *SDL_GetBasePath(void)
{
    return granted_directory("/app/resources",
                             "not running from an application bundle");
}

char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void)org;
    if (app == NULL) {
        SDL_InvalidParamError("app");
        return NULL;
    }
    return granted_directory("/store",
                             "the application holds no STORE capability");
}

#endif
