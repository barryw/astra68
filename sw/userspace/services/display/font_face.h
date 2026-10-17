#ifndef DISPLAY_FONT_FACE_H
#define DISPLAY_FONT_FACE_H

#include <astra/fonts.h>
#include <astra/render_builder.h>

typedef struct DisplayFontFace DisplayFontFace;

/* Dispatch-thread registry. Membership is weak; owners retain each face. */
typedef struct DisplayFontRegistry {
    AstraHandle service;
    DisplayFontFace *faces;
} DisplayFontRegistry;

struct DisplayFontFace {
    DisplayFontRegistry *registry;
    DisplayFontFace *next;
    AstraArea area;
    AstraRenderFont render;
    uint32_t bytes;
    uint32_t references;
    char id[ASTRA_FONT_ID_MAX];
};

/* Adopt a mapped immutable snapshot, or retain the same already-open ID.
   Success consumes area. Failure leaves it with the caller. */
AstraResult display_font_adopt(DisplayFontRegistry *registry,
    AstraArea *area, uint32_t bytes, const char *id, uint32_t id_length,
    DisplayFontFace **face);
AstraResult display_font_acquire(DisplayFontRegistry *registry,
    const char *id, uint32_t id_length, DisplayFontFace **face);
int display_font_retain(DisplayFontFace *face);
void display_font_release(DisplayFontFace *face);

typedef struct DisplayFontContext {
    DisplayFontFace *confirmed;
    DisplayFontFace *pending;
    uint8_t active;
} DisplayFontContext;

void display_font_context_confirm(DisplayFontContext *context,
    const char *id, uint32_t id_length);
void display_font_context_close(DisplayFontContext *context);

typedef struct DisplayFontUse DisplayFontUse;
typedef struct DisplayFontSet {
    DisplayFontRegistry *registry;
    DisplayFontUse *uses;
} DisplayFontSet;

/* Replay records the named faces its retained document uses. Failed replay
   preserves the previous set; successful replacement retires unused faces. */
void display_font_set_begin(DisplayFontSet *set);
const AstraRenderFont *display_font_set_resolve(void *context,
    const char *id, uint32_t id_length);
void display_font_set_end(DisplayFontSet *set, int success);
void display_font_set_close(DisplayFontSet *set);

#endif
