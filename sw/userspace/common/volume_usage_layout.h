#ifndef ASTRA_VOLUME_USAGE_LAYOUT_H
#define ASTRA_VOLUME_USAGE_LAYOUT_H

#include <astra/surface.h>
#include <astra/theme.h>
#include <string.h>

#define ASTRA_VOLUME_USAGE_VIEW_WIDTH 688u

/* Measurement and drawing use the same supported system body font. Consume
 * an explicit newline separately from the fitted UTF-8 text. */
static inline uint32_t volume_usage_dialog_line(const char *text, uint32_t length, uint32_t *advance)
{
    const char *newline = memchr(text, '\n', length);
    uint32_t available = newline == NULL ? length : (uint32_t)(newline - text);
    uint32_t fit = astra_surface_ui_text_fit(text, available,
        ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, ASTRA_VOLUME_USAGE_VIEW_WIDTH - 12u);
    *advance = fit + (fit == available && newline != NULL ? 1u : 0u);
    return fit;
}

#endif
