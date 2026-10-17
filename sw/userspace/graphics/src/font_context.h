#ifndef ASTRA_FONT_CONTEXT_H
#define ASTRA_FONT_CONTEXT_H

#include <astra/fonts.h>

/* Private Font Kit state; use only between frames on the UI thread. */
AstraResult astra_font_context_id(char id[ASTRA_FONT_ID_MAX]);

#endif
