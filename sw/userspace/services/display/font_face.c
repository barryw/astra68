#include "font_face.h"

#include <astra/runtime.h>
#include <astra/result.h>
#include <string.h>

static int valid_id(const char *id, uint32_t length)
{
    return id != NULL && length != 0u && length < ASTRA_FONT_ID_MAX &&
        memchr(id, '\0', length) == NULL;
}

static int face_matches(const DisplayFontFace *face, const char *id,
                         uint32_t length)
{
    return face != NULL && strlen(face->id) == length &&
        memcmp(face->id, id, length) == 0;
}

static DisplayFontFace *find_face(DisplayFontRegistry *registry,
                                   const char *id, uint32_t length)
{
    for (DisplayFontFace *face = registry->faces; face != NULL; face = face->next)
        if (face_matches(face, id, length))
            return face;
    return NULL;
}

int display_font_retain(DisplayFontFace *face)
{
    if (face == NULL || face->references == 0u || face->references == UINT32_MAX)
        return 0;
    ++face->references;
    return 1;
}

void display_font_release(DisplayFontFace *face)
{
    if (face == NULL) return;
    if (--face->references != 0u) return;
    DisplayFontFace **link = &face->registry->faces;
    while (*link != face) link = &(*link)->next;
    *link = face->next;
    AstraResult result = astra_area_close(&face->area);
    if (result != ASTRA_OK)
        (void)astra_log_failure("display font release", (uint32_t)result);
    astra_runtime_deallocate(face);
}

AstraResult display_font_adopt(DisplayFontRegistry *registry,
    AstraArea *area, uint32_t bytes, const char *id, uint32_t id_length,
    DisplayFontFace **out)
{
    AstraRenderFont render;
    DisplayFontFace *face;
    if (registry == NULL || area == NULL || out == NULL || *out != NULL ||
        !valid_id(id, id_length) || area->handle == ASTRA_INVALID_HANDLE ||
        area->map_flags != ASTRA_AREA_MAP_READ || area->address == NULL ||
        bytes > ASTRA_FONT_IMPORT_MAX || area->size < bytes ||
        !astra_render_font_init(&render, area->address, bytes))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    face = find_face(registry, id, id_length);
    if (face != NULL) {
        if (!display_font_retain(face)) return ASTRA_ERROR_OUT_OF_MEMORY;
        AstraResult result = astra_area_close(area);
        if (result != ASTRA_OK) {
            display_font_release(face);
            return result;
        }
        *out = face;
        return ASTRA_OK;
    }
    face = astra_runtime_allocate(sizeof(*face));
    if (face == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    *face = (DisplayFontFace){.registry = registry, .next = registry->faces,
        .area = *area, .render = render, .bytes = bytes, .references = 1u};
    memcpy(face->id, id, id_length);
    face->id[id_length] = '\0';
    *area = (AstraArea)ASTRA_AREA_INIT;
    registry->faces = face;
    *out = face;
    return ASTRA_OK;
}

AstraResult display_font_acquire(DisplayFontRegistry *registry,
    const char *id, uint32_t id_length, DisplayFontFace **out)
{
    DisplayFontFace *face;
    AstraArea area = ASTRA_AREA_INIT;
    uint32_t bytes = 0u;
    AstraResult result;
    if (registry == NULL || out == NULL || *out != NULL || !valid_id(id, id_length))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    face = find_face(registry, id, id_length);
    if (face != NULL) {
        if (!display_font_retain(face)) return ASTRA_ERROR_OUT_OF_MEMORY;
        *out = face;
        return ASTRA_OK;
    }
    result = astra_fonts_face_open(registry->service, id, id_length, &area, &bytes);
    if (result == ASTRA_OK)
        result = display_font_adopt(registry, &area, bytes, id, id_length, out);
    if (area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult closed = astra_area_close(&area);
        if (result == ASTRA_OK) result = closed;
    }
    return result;
}

void display_font_context_confirm(DisplayFontContext *context,
    const char *id, uint32_t id_length)
{
    DisplayFontFace *adopted = NULL;
    uint8_t active = context->active;
    if (face_matches(context->pending, id, id_length)) {
        adopted = context->pending;
        context->pending = NULL;
    } else if (face_matches(context->confirmed, id, id_length)) {
        adopted = context->confirmed;
        context->confirmed = NULL;
    }
    display_font_context_close(context);
    context->confirmed = adopted;
    context->active = active;
}

void display_font_context_close(DisplayFontContext *context)
{
    display_font_release(context->confirmed);
    display_font_release(context->pending);
    *context = (DisplayFontContext){0};
}

struct DisplayFontUse {
    DisplayFontUse *next;
    DisplayFontFace *face;
    uint8_t used;
    uint8_t fresh;
};

void display_font_set_begin(DisplayFontSet *set)
{
    for (DisplayFontUse *use = set->uses; use != NULL; use = use->next) {
        use->used = 0u;
        use->fresh = 0u;
    }
}

const AstraRenderFont *display_font_set_resolve(void *context,
    const char *id, uint32_t id_length)
{
    DisplayFontSet *set = context;
    DisplayFontUse *use;
    if (set == NULL || set->registry == NULL || !valid_id(id, id_length))
        return NULL;
    for (use = set->uses; use != NULL; use = use->next)
        if (face_matches(use->face, id, id_length)) {
            use->used = 1u;
            return &use->face->render;
        }
    use = astra_runtime_allocate(sizeof(*use));
    if (use == NULL) return NULL;
    *use = (DisplayFontUse){.next = set->uses, .used = 1u, .fresh = 1u};
    if (display_font_acquire(set->registry, id, id_length, &use->face) != ASTRA_OK) {
        astra_runtime_deallocate(use);
        return NULL;
    }
    set->uses = use;
    return &use->face->render;
}

void display_font_set_end(DisplayFontSet *set, int success)
{
    DisplayFontUse **link = &set->uses;
    while (*link != NULL) {
        DisplayFontUse *use = *link;
        if (success ? !use->used : use->fresh) {
            *link = use->next;
            display_font_release(use->face);
            astra_runtime_deallocate(use);
        } else {
            use->fresh = 0u;
            link = &use->next;
        }
    }
}

void display_font_set_close(DisplayFontSet *set)
{
    display_font_set_begin(set);
    display_font_set_end(set, 1);
}
