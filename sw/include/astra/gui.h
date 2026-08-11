#ifndef ASTRA_GUI_H
#define ASTRA_GUI_H

#include <stdint.h>

#include <astra/syscall.h>
#include <astra/window.h>

#define ASTRA_CAPABILITY_GUI "GUI"

#define ASTRA_GUI_PROTOCOL UINT32_C(0x47554920) /* GUI  */
#define ASTRA_GUI_VERSION 6u

#define ASTRA_GUI_CONTENT_RGB565   1u
#define ASTRA_GUI_CONTENT_DRAW_LIST 2u

#define ASTRA_GUI_OPEN_WINDOW   1u
#define ASTRA_GUI_WINDOW_OPENED 2u
#define ASTRA_GUI_WINDOW_COMMAND 3u
#define ASTRA_GUI_WINDOW_STATE   4u
#define ASTRA_GUI_WINDOW_EVENT   5u

enum {
    ASTRA_GUI_WINDOW_QUERY = 1u,
    ASTRA_GUI_WINDOW_SET_FRAME = 2u,
    ASTRA_GUI_WINDOW_MOVE = 3u,
    ASTRA_GUI_WINDOW_RESIZE = 4u,
    ASTRA_GUI_WINDOW_RAISE = 5u,
    ASTRA_GUI_WINDOW_LOWER = 6u,
    ASTRA_GUI_WINDOW_ACTIVATE = 7u,
    ASTRA_GUI_WINDOW_DEACTIVATE = 8u,
    ASTRA_GUI_WINDOW_MINIMIZE = 9u,
    ASTRA_GUI_WINDOW_MAXIMIZE = 10u,
    ASTRA_GUI_WINDOW_RESTORE = 11u,
    ASTRA_GUI_WINDOW_SET_TITLE = 12u,
    ASTRA_GUI_WINDOW_CLOSE = 13u,
    ASTRA_GUI_WINDOW_SET_EVENT_MASK = 14u,
    ASTRA_GUI_WINDOW_PRESENT = 15u,
};

typedef struct AstraGuiOpenWindow {
    AstraMessageHeader header;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t pitch;
    uint32_t flags;
    uint32_t gadgets;
    uint8_t type;
    uint8_t close_state;
    uint8_t minimize_state;
    uint8_t maximize_state;
    uint16_t title_length;
    uint16_t content_format;
    char title[ASTRA_WINDOW_TITLE_MAX];
    uint32_t event_mask;
} AstraGuiOpenWindow;

typedef struct AstraGuiWindowOpened {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t window;
    uint32_t generation;
} AstraGuiWindowOpened;

typedef struct AstraGuiWindowCommand {
    AstraMessageHeader header;
    uint32_t window;
    uint32_t generation;
    uint32_t action;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t flags;
    uint16_t title_length;
    uint16_t reserved16;
    char title[ASTRA_WINDOW_TITLE_MAX];
    uint32_t reserved;
} AstraGuiWindowCommand;

typedef struct AstraGuiWindowState {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t window;
    uint32_t generation;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t flags;
    uint32_t state;
    uint32_t z_order;
    uint32_t reserved[2];
} AstraGuiWindowState;

typedef struct AstraGuiWindowEvent {
    AstraMessageHeader header;
    AstraWindowEvent event;
} AstraGuiWindowEvent;

#define ASTRA_GUI_OPEN_WINDOW_SIZE   104u
#define ASTRA_GUI_WINDOW_OPENED_SIZE 36u
#define ASTRA_GUI_WINDOW_COMMAND_SIZE 104u
#define ASTRA_GUI_WINDOW_STATE_SIZE   64u
#define ASTRA_GUI_WINDOW_EVENT_SIZE   72u

_Static_assert(sizeof(AstraGuiOpenWindow) == ASTRA_GUI_OPEN_WINDOW_SIZE,
               "GUI open-window message is an ABI");
_Static_assert(sizeof(AstraGuiWindowOpened) == ASTRA_GUI_WINDOW_OPENED_SIZE,
               "GUI window-opened message is an ABI");
_Static_assert(sizeof(AstraGuiWindowCommand) == ASTRA_GUI_WINDOW_COMMAND_SIZE,
               "GUI window-command message is an ABI");
_Static_assert(sizeof(AstraGuiWindowState) == ASTRA_GUI_WINDOW_STATE_SIZE,
               "GUI window-state message is an ABI");
_Static_assert(sizeof(AstraGuiWindowEvent) == ASTRA_GUI_WINDOW_EVENT_SIZE,
               "GUI window-event message is an ABI");

#endif
