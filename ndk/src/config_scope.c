#include <astra/config_library.h>

#include <stddef.h>

static int component_character(char value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' ||
           value == '_' || value == '.';
}

static int component_valid(const char *text)
{
    uint32_t at = 0u;

    if (text == NULL || text[0] == '\0' || text[0] == '.')
        return 0;
    while (text[at] != '\0') {
        if (!component_character(text[at]))
            return 0;
        ++at;
    }
    return 1;
}

static uint32_t build_root(const char *parent, const char *directory,
                           const char *owner, char *out, uint32_t capacity)
{
    uint32_t used = 0u;

    if (parent == NULL || parent[0] == '\0' ||
        (directory != NULL && !component_valid(directory)) ||
        (owner != NULL && !component_valid(owner)) ||
        out == NULL || capacity == 0u)
        return ASTRA_CONFIG_INVALID;
#define APPEND(TEXT) do { \
        const char *part = (TEXT); \
        while (*part != '\0') { \
            if (used + 1u >= capacity) { out[0] = '\0'; \
                return ASTRA_CONFIG_BUFFER_TOO_SMALL; } \
            out[used++] = *part++; \
        } \
    } while (0)
    out[0] = '\0';
    APPEND(parent);
    if (directory != NULL) {
        if (used != 0u && out[used - 1u] != '/') APPEND("/");
        APPEND(directory);
    }
    if (owner != NULL) {
        if (used != 0u && out[used - 1u] != '/') APPEND("/");
        APPEND(owner);
    }
    out[used] = '\0';
#undef APPEND
    return ASTRA_CONFIG_OK;
}

static const char *scope_directory(uint32_t owner_kind)
{
    switch (owner_kind) {
    case ASTRA_CONFIG_OWNER_SYSTEM: return "system";
    case ASTRA_CONFIG_OWNER_SERVICE: return "services";
    case ASTRA_CONFIG_OWNER_COMMAND: return "commands";
    case ASTRA_CONFIG_OWNER_APPLICATION: return "applications";
    default: return NULL;
    }
}

uint32_t
astra_config_scope_root(const char *parent, uint32_t owner_kind,
                        char *out, uint32_t capacity)
{
    const char *directory = scope_directory(owner_kind);

    return directory != NULL ? build_root(parent, directory, NULL, out,
                                          capacity) : ASTRA_CONFIG_INVALID;
}

uint32_t
astra_config_owner_root(const char *parent, const char *owner,
                        char *out, uint32_t capacity)
{
    return build_root(parent, NULL, owner, out, capacity);
}

uint32_t
astra_config_capability_root(const char *parent, uint32_t owner_kind,
                             const char *owner, char *out,
                             uint32_t capacity)
{
    const char *directory = scope_directory(owner_kind);

    return directory != NULL ? build_root(parent, directory, owner, out,
                                          capacity) : ASTRA_CONFIG_INVALID;
}
