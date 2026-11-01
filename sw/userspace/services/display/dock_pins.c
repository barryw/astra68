#include "dock_pins.h"

#include <astra/runtime.h>
#include <astra/string.h>
#include <astra/utf8.h>

#include <stdint.h>
#include <string.h>

static uint32_t digits(uint32_t value)
{
    uint32_t count = 1u;
    while (value >= 10u) { value /= 10u; ++count; }
    return count;
}

static int valid(const char *id, const char *name, const char *path)
{
    size_t id_length, name_length, path_length;
    if (id == NULL || name == NULL || path == NULL || path[0] != '/')
        return 0;
    id_length = strlen(id);
    name_length = strlen(name);
    path_length = strlen(path);
    return id_length != 0u && id_length < ASTRA_BUNDLE_ID_MAX &&
        name_length != 0u && name_length < ASTRA_BUNDLE_NAME_MAX &&
        path_length > 1u && path_length < ASTRA_VFS_PATH_MAX &&
        astra_utf8_validate(id, (uint32_t)id_length, 0u) &&
        astra_utf8_validate(name, (uint32_t)name_length, 0u) &&
        astra_utf8_validate(path, (uint32_t)path_length, 0u);
}

void display_dock_pins_clear(DisplayDockPins *pins)
{
    if (pins == NULL) return;
    for (uint32_t at = 0u; at < pins->count; ++at) {
        astra_runtime_deallocate(pins->entries[at].path);
        astra_runtime_deallocate(pins->entries[at].icon_bytes);
    }
    *pins = (DisplayDockPins){0};
}

int32_t display_dock_pins_find(const DisplayDockPins *pins, const char *id)
{
    if (pins == NULL || id == NULL) return -1;
    for (uint32_t at = 0u; at < pins->count; ++at)
        if (strcmp(pins->entries[at].id, id) == 0) return (int32_t)at;
    return -1;
}

uint32_t display_dock_pins_set(DisplayDockPins *pins, const char *id,
                               const char *name, const char *path)
{
    char *copy;
    int32_t existing;
    if (pins == NULL || !valid(id, name, path)) return ASTRA_STATUS_INVALID;
    existing = display_dock_pins_find(pins, id);
    if (existing < 0 && pins->count == DISPLAY_DOCK_PIN_LIMIT)
        return ASTRA_STATUS_LIMIT;
    copy = astra_runtime_allocate(strlen(path) + 1u);
    if (copy == NULL) return ASTRA_STATUS_LIMIT;
    strcpy(copy, path);
    uint32_t at = existing < 0 ? pins->count++ : (uint32_t)existing;
    astra_runtime_deallocate(pins->entries[at].path);
    astra_runtime_deallocate(pins->entries[at].icon_bytes);
    pins->entries[at].icon_bytes = NULL;
    pins->entries[at].icon_length = 0u;
    pins->entries[at].online = 0u;
    pins->entries[at].path = copy;
    strcpy(pins->entries[at].id, id);
    strcpy(pins->entries[at].name, name);
    return ASTRA_STATUS_OK;
}

uint32_t display_dock_pins_remove(DisplayDockPins *pins, uint32_t index)
{
    if (pins == NULL || index >= pins->count) return ASTRA_STATUS_INVALID;
    astra_runtime_deallocate(pins->entries[index].path);
    astra_runtime_deallocate(pins->entries[index].icon_bytes);
    for (uint32_t at = index; at + 1u < pins->count; ++at)
        pins->entries[at] = pins->entries[at + 1u];
    --pins->count;
    pins->entries[pins->count] = (DisplayDockPin){0};
    return ASTRA_STATUS_OK;
}

uint32_t display_dock_pins_move(DisplayDockPins *pins, uint32_t from,
                                uint32_t to)
{
    if (pins == NULL || from >= pins->count || to >= pins->count)
        return ASTRA_STATUS_INVALID;
    DisplayDockPin moved = pins->entries[from];
    if (from < to)
        for (uint32_t at = from; at < to; ++at)
            pins->entries[at] = pins->entries[at + 1u];
    else
        for (uint32_t at = from; at > to; --at)
            pins->entries[at] = pins->entries[at - 1u];
    pins->entries[to] = moved;
    return ASTRA_STATUS_OK;
}

uint32_t display_dock_pins_encode(const DisplayDockPins *pins, char *out,
                                  uint32_t capacity, uint32_t *required)
{
    AstraString result;
    uint32_t needed = 1u;
    if (pins == NULL || required == NULL || pins->count > DISPLAY_DOCK_PIN_LIMIT)
        return ASTRA_STATUS_INVALID;
    for (uint32_t at = 0u; at < pins->count; ++at) {
        const DisplayDockPin *pin = &pins->entries[at];
        uint32_t id, name, path;
        if (!valid(pin->id, pin->name, pin->path) ||
            display_dock_pins_find(pins, pin->id) != (int32_t)at)
            return ASTRA_STATUS_INVALID;
        id = (uint32_t)strlen(pin->id);
        name = (uint32_t)strlen(pin->name);
        path = (uint32_t)strlen(pin->path);
        needed += digits(id) + digits(name) + digits(path) +
                  3u + id + name + path;
    }
    *required = needed;
    if (out == NULL || capacity < needed) return ASTRA_STATUS_LIMIT;
    astra_string_init(&result, out, capacity);
    for (uint32_t at = 0u; at < pins->count; ++at) {
        const DisplayDockPin *pin = &pins->entries[at];
        uint32_t id = (uint32_t)strlen(pin->id);
        uint32_t name = (uint32_t)strlen(pin->name);
        uint32_t path = (uint32_t)strlen(pin->path);
        if (!astra_string_append_u64(&result, id) ||
            !astra_string_append_char(&result, ':') ||
            !astra_string_append_u64(&result, name) ||
            !astra_string_append_char(&result, ':') ||
            !astra_string_append_u64(&result, path) ||
            !astra_string_append_char(&result, ':') ||
            !astra_string_append_bytes(&result, pin->id, id) ||
            !astra_string_append_bytes(&result, pin->name, name) ||
            !astra_string_append_bytes(&result, pin->path, path))
            return ASTRA_STATUS_LIMIT;
    }
    return ASTRA_STATUS_OK;
}

static int read_length(const char *text, uint32_t length, uint32_t *cursor,
                       uint32_t maximum, uint32_t *value)
{
    uint32_t at = *cursor, number = 0u;
    if (at == length || text[at] < '0' || text[at] > '9') return 0;
    do {
        uint32_t digit = (uint32_t)(text[at] - '0');
        if (number > (maximum - digit) / 10u) return 0;
        number = number * 10u + digit;
        ++at;
    } while (at < length && text[at] >= '0' && text[at] <= '9');
    if (at == length || text[at] != ':' || number == 0u || number >= maximum)
        return 0;
    *cursor = at + 1u;
    *value = number;
    return 1;
}

uint32_t display_dock_pins_decode(DisplayDockPins *pins, const char *text,
                                  uint32_t length)
{
    DisplayDockPins decoded = {0};
    uint32_t at = 0u;
    if (pins == NULL || text == NULL || memchr(text, '\0', length) != NULL)
        return ASTRA_STATUS_INVALID;
    while (at < length) {
        uint32_t id, name, path;
        char id_text[ASTRA_BUNDLE_ID_MAX];
        char name_text[ASTRA_BUNDLE_NAME_MAX];
        char path_text[ASTRA_VFS_PATH_MAX];
        if (decoded.count == DISPLAY_DOCK_PIN_LIMIT ||
            !read_length(text, length, &at, ASTRA_BUNDLE_ID_MAX, &id) ||
            !read_length(text, length, &at, ASTRA_BUNDLE_NAME_MAX, &name) ||
            !read_length(text, length, &at, ASTRA_VFS_PATH_MAX, &path) ||
            id + name + path > length - at) goto invalid;
        memcpy(id_text, text + at, id); id_text[id] = '\0'; at += id;
        memcpy(name_text, text + at, name); name_text[name] = '\0'; at += name;
        memcpy(path_text, text + at, path); path_text[path] = '\0'; at += path;
        if (display_dock_pins_find(&decoded, id_text) >= 0 ||
            display_dock_pins_set(&decoded, id_text, name_text,
                                  path_text) != ASTRA_STATUS_OK) goto invalid;
    }
    display_dock_pins_clear(pins);
    *pins = decoded;
    return ASTRA_STATUS_OK;
invalid:
    display_dock_pins_clear(&decoded);
    return ASTRA_STATUS_INVALID;
}
