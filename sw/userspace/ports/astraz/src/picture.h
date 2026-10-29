#ifndef ASTRAZ_PICTURE_H
#define ASTRAZ_PICTURE_H

#include <astraz/engine.h>
#include <astra/graphics.h>

/* Decode only a story resource; the caller owns any resulting GPU surface. */
int astraz_picture_size(const AstraZResource *resource, int *height,
                        int *width);
AstraResult astraz_picture_upload(AstraDisplay *display,
    const AstraZResource *resource, AstraSurface *surface,
    int *height, int *width);

#endif
