#ifndef ASTRAZ_CANVAS_H
#define ASTRAZ_CANVAS_H

#include <astraz/engine.h>
#include <astra/graphics.h>

#define ASTRAZ_CANVAS_WIDTH 320u
#define ASTRAZ_CANVAS_HEIGHT 200u
#define ASTRAZ_CANVAS_VIEW_WIDTH 640u
#define ASTRAZ_CANVAS_VIEW_HEIGHT 400u

typedef struct AstraZCanvas {
    AstraDisplay *display;
    AstraSurface surface;
    AstraDrawList list;
    uint32_t operations;
    int32_t row[8], column[8];
    uint32_t style[8];
    int32_t foreground[8], background[8];
} AstraZCanvas;

int astraz_canvas_open(AstraZCanvas *canvas, AstraDisplay *display);
int astraz_canvas_event(AstraZCanvas *canvas, const AstraZEvent *event);
AstraResult astraz_canvas_flush(AstraZCanvas *canvas);
void astraz_canvas_close(AstraZCanvas *canvas);

#endif
