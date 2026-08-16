#ifndef ASTRA_USERSPACE_FONT_LIBRARY_H
#define ASTRA_USERSPACE_FONT_LIBRARY_H

#include <stdint.h>

#include <astra/surface.h>

#define ASTRA_FONT_LIBRARY_ABI_MAJOR 1u
#define ASTRA_FONT_LIBRARY_ABI_MINOR 0u

typedef struct AstraFontLibraryV1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;
    uint32_t (*ui_text_width)(const char *, uint32_t, uint16_t);
    uint32_t (*ui_text_fit)(const char *, uint32_t, uint16_t, uint32_t);
    uint16_t (*mono_cell_width)(uint16_t);
    void (*draw_list_mono_text)(AstraSurfaceView *, int32_t, int32_t,
                                const char *, uint32_t, uint16_t, uint16_t,
                                uint16_t);
} AstraFontLibraryV1;

#endif
