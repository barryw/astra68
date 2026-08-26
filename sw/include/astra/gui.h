#ifndef ASTRA_GUI_H
#define ASTRA_GUI_H

#include <stdint.h>

#include <astra/syscall.h>

#define ASTRA_CAPABILITY_GUI "GUI"

#define ASTRA_GUI_PROTOCOL UINT32_C(0x47554920) /* GUI  */
#define ASTRA_GUI_VERSION 7u

#define ASTRA_WINDOW_TITLE_MAX UINT32_C(48)
#define ASTRA_WINDOW_TITLE_ICON_BYTES_MAX UINT32_C(8192)

enum {
    ASTRA_WINDOW_STANDARD = 1,
    ASTRA_WINDOW_UTILITY = 2,
    ASTRA_WINDOW_DIALOG = 3,
    ASTRA_WINDOW_POPOVER = 4,
    ASTRA_WINDOW_FULLSCREEN = 5,
    ASTRA_WINDOW_DESKTOP = 6
};

enum {
    ASTRA_WINDOW_STATE_NORMAL = 0,
    ASTRA_WINDOW_STATE_MINIMIZED = 1,
    ASTRA_WINDOW_STATE_MAXIMIZED = 2
};

enum {
    ASTRA_WINDOW_CONTENT_RGB565 = 1,
    ASTRA_WINDOW_CONTENT_DRAW_LIST = 2
};

enum {
    ASTRA_WINDOW_RESIZABLE = 1u << 0,
    ASTRA_WINDOW_MODAL = 1u << 1,
    ASTRA_WINDOW_ACTIVE = 1u << 2
};

enum {
    ASTRA_WINDOW_GADGET_CLOSE = 1u << 0,
    ASTRA_WINDOW_GADGET_MINIMIZE = 1u << 1,
    ASTRA_WINDOW_GADGET_MAXIMIZE = 1u << 2
};

enum {
    ASTRA_GADGET_NORMAL = 0,
    ASTRA_GADGET_HOVER = 1,
    ASTRA_GADGET_PRESSED = 2,
    ASTRA_GADGET_FOCUSED = 3,
    ASTRA_GADGET_DISABLED = 4
};

typedef struct AstraWindowFrame {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} AstraWindowFrame;

#define ASTRA_WINDOW_EVENT_VERSION 3u

enum {
    ASTRA_WINDOW_EVENT_POINTER_MOTION = 1,
    ASTRA_WINDOW_EVENT_POINTER_BUTTON = 2,
    ASTRA_WINDOW_EVENT_POINTER_WHEEL = 3,
    ASTRA_WINDOW_EVENT_FOCUS = 4,
    ASTRA_WINDOW_EVENT_FRAME = 5,
    ASTRA_WINDOW_EVENT_CLOSE_REQUEST = 6,
    ASTRA_WINDOW_EVENT_STATE_RESET = 7,
    ASTRA_WINDOW_EVENT_KEY = 8,
    ASTRA_WINDOW_EVENT_TEXT = 9
};

enum {
    ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION = 1u << 0,
    ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON = 1u << 1,
    ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL = 1u << 2,
    ASTRA_WINDOW_SUBSCRIBE_FOCUS = 1u << 3,
    ASTRA_WINDOW_SUBSCRIBE_FRAME = 1u << 4,
    ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST = 1u << 5,
    ASTRA_WINDOW_SUBSCRIBE_KEY = 1u << 6,
    ASTRA_WINDOW_SUBSCRIBE_TEXT = 1u << 7
};

#define ASTRA_WINDOW_SUBSCRIBE_DEFAULT \
    (ASTRA_WINDOW_SUBSCRIBE_FOCUS | ASTRA_WINDOW_SUBSCRIBE_FRAME | \
     ASTRA_WINDOW_SUBSCRIBE_CLOSE_REQUEST)
#define ASTRA_WINDOW_SUBSCRIBE_ALL \
    (ASTRA_WINDOW_SUBSCRIBE_POINTER_MOTION | \
     ASTRA_WINDOW_SUBSCRIBE_POINTER_BUTTON | \
     ASTRA_WINDOW_SUBSCRIBE_POINTER_WHEEL | ASTRA_WINDOW_SUBSCRIBE_DEFAULT | \
     ASTRA_WINDOW_SUBSCRIBE_KEY | ASTRA_WINDOW_SUBSCRIBE_TEXT)

enum {
    ASTRA_WINDOW_EVENT_DOWN = 1u << 0,
    ASTRA_WINDOW_EVENT_FOCUSED = 1u << 1,
    ASTRA_WINDOW_EVENT_CAPTURED = 1u << 2,
    ASTRA_WINDOW_EVENT_LOSS = 1u << 3,
    ASTRA_WINDOW_EVENT_REPEAT = 1u << 4,
    ASTRA_WINDOW_EVENT_SYNTHETIC = 1u << 5
};

typedef struct AstraWindowEvent {
    uint32_t size;
    uint16_t version;
    uint16_t type;
    uint32_t flags;
    uint32_t timestamp_ms;
    uint32_t sequence;
    uint32_t generation;
    union {
        struct {
            int32_t x;
            int32_t y;
            int32_t screen_x;
            int32_t screen_y;
            uint32_t button;
            uint32_t click_count;
        } pointer;
        struct {
            int32_t x;
            int32_t y;
            int32_t screen_x;
            int32_t screen_y;
            int32_t delta_x;
            int32_t delta_y;
        } wheel;
        struct {
            AstraWindowFrame frame;
            uint32_t state;
            uint32_t z_order;
            uint32_t reserved[2];
        } frame;
        struct {
            uint32_t usage;
            uint32_t modifiers;
            uint32_t reserved[4];
        } key;
        struct {
            uint32_t codepoint;
            uint32_t modifiers;
            uint32_t reserved[4];
        } text;
        uint32_t reserved[6];
    } data;
} AstraWindowEvent;

_Static_assert(sizeof(AstraWindowEvent) == 48u,
               "window event ABI changed");

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
    uint32_t title_icon_length;
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

#define ASTRA_GUI_OPEN_WINDOW_SIZE   108u
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
