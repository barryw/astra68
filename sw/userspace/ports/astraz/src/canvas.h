#ifndef ASTRAZ_CANVAS_H
#define ASTRAZ_CANVAS_H

#include <astraz/engine.h>
#include <astra/graphics.h>

#define ASTRAZ_STORY_WIDTH 320u
#define ASTRAZ_STORY_HEIGHT 200u
#define ASTRAZ_CANVAS_WIDTH ASTRAZ_STORY_WIDTH
#define ASTRAZ_CANVAS_HEIGHT ASTRAZ_STORY_HEIGHT
#define ASTRAZ_CANVAS_VIEW_WIDTH 640u
#define ASTRAZ_CANVAS_VIEW_HEIGHT 400u
#define ASTRAZ_PIXEL_FACE_REGULAR "system:astra-pixel-mono"
#define ASTRAZ_PIXEL_FACE_BOLD "system:astra-pixel-mono-bold"
#define ASTRAZ_PIXEL_FACE_ITALIC "system:astra-pixel-mono-italic"
#define ASTRAZ_PIXEL_FACE_BOLD_ITALIC "system:astra-pixel-mono-bold-italic"
#define ASTRAZ_GRAPHICS_FACE "system:astra-zmachine-graphics"

int astraz_canvas_view_fit(uint32_t available_width,
    uint32_t available_height, uint32_t *width, uint32_t *height);

typedef struct AstraZCanvas {
    AstraDisplay *display;
    AstraSurface surface;
    AstraDrawList list;
    uint32_t operations;
    int32_t row[8], column[8];
    uint32_t style[8];
    int32_t font[8];
    int32_t foreground[8], background[8];
    int32_t input_x, input_y;
    uint32_t input_cells;
    uint8_t input_active;
} AstraZCanvas;

int astraz_canvas_open(AstraZCanvas *canvas, AstraDisplay *display);
int astraz_canvas_event(AstraZCanvas *canvas, const AstraZEvent *event);
AstraResult astraz_canvas_flush(AstraZCanvas *canvas);
AstraResult astraz_canvas_more_overlay(AstraDrawList *list,
    const AstraRectI32 *frame, const AstraRectI32 *clip);
int astraz_canvas_input_begin(AstraZCanvas *canvas, uint32_t width);
int astraz_canvas_input_render(AstraZCanvas *canvas, const char *text,
    uint32_t bytes, uint32_t focus, int show_caret);
void astraz_canvas_input_end(AstraZCanvas *canvas);
void astraz_canvas_close(AstraZCanvas *canvas);

#endif
