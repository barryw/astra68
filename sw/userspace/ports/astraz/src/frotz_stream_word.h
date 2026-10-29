#ifndef ASTRAZ_FROTZ_STREAM_WORD_H
#define ASTRAZ_FROTZ_STREAM_WORD_H

#include <stdint.h>

/* Frotz places an argument after each style/font marker. The argument may
 * itself be zero, so scanning for the first zero loses the following text. */
enum {
    ASTRAZ_FROTZ_STYLE_MARKER = 1u,
    ASTRAZ_FROTZ_FONT_MARKER = 2u
};

static inline uint32_t astraz_frotz_stream_word_length(const uint16_t *text)
{
    if (!text) return 0u;
    uint32_t length = 0u;
    while (length < UINT32_MAX && text[length] != 0u) {
        if (text[length] == ASTRAZ_FROTZ_STYLE_MARKER ||
            text[length] == ASTRAZ_FROTZ_FONT_MARKER) {
            if (length >= UINT32_MAX - 1u) return 0u;
            length += 2u;
        } else {
            ++length;
        }
    }
    return length;
}

#endif
