#ifndef ASTRA_GUI_H
#define ASTRA_GUI_H

#include <stdint.h>

#include <astra/syscall.h>

#define ASTRA_CAPABILITY_GUI "GUI"

#define ASTRA_GUI_PROTOCOL UINT32_C(0x47554920) /* GUI  */
#define ASTRA_GUI_VERSION 1u

#define ASTRA_GUI_OPEN_WINDOW   1u
#define ASTRA_GUI_WINDOW_OPENED 2u

typedef struct AstraGuiOpenWindow {
    AstraMessageHeader header;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint32_t reserved;
} AstraGuiOpenWindow;

typedef struct AstraGuiWindowOpened {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t window;
    uint32_t generation;
} AstraGuiWindowOpened;

#define ASTRA_GUI_OPEN_WINDOW_SIZE   40u
#define ASTRA_GUI_WINDOW_OPENED_SIZE 36u

_Static_assert(sizeof(AstraGuiOpenWindow) == ASTRA_GUI_OPEN_WINDOW_SIZE,
               "GUI open-window message is an ABI");
_Static_assert(sizeof(AstraGuiWindowOpened) == ASTRA_GUI_WINDOW_OPENED_SIZE,
               "GUI window-opened message is an ABI");

#endif
