/** @file font_library.h @brief Font Kit shared-library ABI. */
#ifndef ASTRA_FONT_LIBRARY_H
#define ASTRA_FONT_LIBRARY_H

#include <stdint.h>

#include <astra/surface.h>

/** Font Kit export-table ABI major version. */
#define ASTRA_FONT_LIBRARY_ABI_MAJOR 2u
/** Font Kit export-table ABI minor version. */
#define ASTRA_FONT_LIBRARY_ABI_MINOR 1u

/** Font Kit 2.x immutable export table. */
typedef struct AstraFontLibraryV2 {
    uint16_t abi_major; /**< ASTRA_FONT_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_FONT_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */
    /** Measure proportional UTF-8 text. */
    uint32_t (*ui_text_width)(const char *, uint32_t, uint16_t);
    /** Return the UTF-8 prefix fitting a pixel width. */
    uint32_t (*ui_text_fit)(const char *, uint32_t, uint16_t, uint32_t);
    /** Return the canonical monospace cell width for a font height. */
    uint16_t (*mono_cell_width)(uint16_t);
    /** Append monospace text to a hardware draw list. */
    void (*draw_list_mono_text)(AstraSurfaceView *, int32_t, int32_t,
                                const char *, uint32_t, uint16_t, uint16_t,
                                uint16_t);
    /** Append styled monospace text to a hardware draw list. */
    int (*draw_list_mono_text_styled)(AstraSurfaceView *, int32_t, int32_t,
                                      const char *, uint32_t, uint16_t,
                                      uint16_t, uint16_t, uint32_t);
} AstraFontLibraryV2;

#endif
