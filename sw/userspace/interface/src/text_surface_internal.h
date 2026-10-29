#ifndef ASTRA_TEXT_SURFACE_INTERNAL_H
#define ASTRA_TEXT_SURFACE_INTERNAL_H

#include <astra/text_surface.h>

/* Use the renderer's run splitting and UTF-8 encoding for atomic UI preflight. */
AstraResult astra_text_surface_cells_requirements(
    const AstraTextSurface *text, const AstraTextCell *cells, uint32_t count,
    uint64_t *commands, uint64_t *payload);
AstraResult astra_text_surface_flow_run_requirements(
    const AstraTextSurface *text, const char *utf8, uint32_t bytes,
    uint32_t advance, uint32_t foreground, uint32_t background,
    uint16_t attributes, uint64_t *commands, uint64_t *payload);

#endif
