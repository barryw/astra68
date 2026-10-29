#ifndef ASTRAZ_ENGINE_H
#define ASTRAZ_ENGINE_H

#include <stddef.h>
#include <stdint.h>

/* Application-private API. Callbacks execute on the run caller's thread.
 * Frotz has process-global state, so only one run may be active. */

enum {
    ASTRAZ_OK = 0,
    ASTRAZ_INVALID = 1,
    ASTRAZ_IO = 2,
    ASTRAZ_UNSUPPORTED_VM = 3,
    ASTRAZ_MISSING_CAPABILITY = 4,
    ASTRAZ_BUSY = 5,
    ASTRAZ_ENGINE_ERROR = 6,
    ASTRAZ_CANCELLED = 7
};

enum { ASTRAZ_VM_NONE = 0, ASTRAZ_VM_Z = 1, ASTRAZ_VM_GLULX = 2 };
enum { ASTRAZ_CONTAINER_BARE = 0, ASTRAZ_CONTAINER_BLORB = 1 };

enum {
    ASTRAZ_CAP_TIMED_INPUT = 1u << 0,
    ASTRAZ_CAP_COLOUR = 1u << 1,
    ASTRAZ_CAP_TRUE_COLOUR = 1u << 2,
    ASTRAZ_CAP_MOUSE = 1u << 3,
    ASTRAZ_CAP_PICTURES = 1u << 4,
    ASTRAZ_CAP_SOUND = 1u << 5
};

/* ZSCII input codes. Unicode text uses BMP code points in uint16_t buffers. */
enum {
    ASTRAZ_KEY_TIMEOUT = 0,
    ASTRAZ_KEY_BACKSPACE = 8,
    ASTRAZ_KEY_RETURN = 13,
    ASTRAZ_KEY_ESCAPE = 27,
    ASTRAZ_KEY_DELETE_WORD = 28,
    ASTRAZ_KEY_WORD_RIGHT = 29,
    ASTRAZ_KEY_WORD_LEFT = 30,
    ASTRAZ_KEY_DELETE_TO_START = 31,
    ASTRAZ_KEY_UP = 0x81,
    ASTRAZ_KEY_DOWN = 0x82,
    ASTRAZ_KEY_LEFT = 0x83,
    ASTRAZ_KEY_RIGHT = 0x84,
    ASTRAZ_KEY_F1 = 0x85,
    ASTRAZ_KEY_F12 = 0x90,
    ASTRAZ_KEY_NUMPAD_0 = 0x91,
    ASTRAZ_KEY_NUMPAD_9 = 0x9a,
    ASTRAZ_KEY_CLICK = 0x9b,
    ASTRAZ_KEY_DOUBLE_CLICK = 0x9c,
    ASTRAZ_KEY_MENU_CLICK = 0x9d
};

enum {
    ASTRAZ_HOTKEY_UNDO = 0x11,
    ASTRAZ_HOTKEY_RESTART = 0x12,
    ASTRAZ_HOTKEY_QUIT = 0x13
};

enum {
    ASTRAZ_FILE_RESTORE = 0,
    ASTRAZ_FILE_SAVE,
    ASTRAZ_FILE_TRANSCRIPT,
    ASTRAZ_FILE_PLAYBACK,
    ASTRAZ_FILE_RECORD,
    ASTRAZ_FILE_LOAD_AUX,
    ASTRAZ_FILE_SAVE_AUX,
    ASTRAZ_FILE_NO_PROMPT
};

enum {
    ASTRAZ_STYLE_NORMAL = 0,
    ASTRAZ_STYLE_REVERSE = 1,
    ASTRAZ_STYLE_BOLD = 2,
    ASTRAZ_STYLE_EMPHASIS = 4,
    ASTRAZ_STYLE_FIXED = 8,
    ASTRAZ_STYLE_PICTURE = 16
};

enum {
    ASTRAZ_FONT_TEXT = 1,
    ASTRAZ_FONT_PICTURE = 2,
    ASTRAZ_FONT_GRAPHICS = 3,
    ASTRAZ_FONT_FIXED = 4
};

enum {
    ASTRAZ_COLOUR_DEFAULT = 1,
    ASTRAZ_COLOUR_BLACK = 2,
    ASTRAZ_COLOUR_RED = 3,
    ASTRAZ_COLOUR_GREEN = 4,
    ASTRAZ_COLOUR_YELLOW = 5,
    ASTRAZ_COLOUR_BLUE = 6,
    ASTRAZ_COLOUR_MAGENTA = 7,
    ASTRAZ_COLOUR_CYAN = 8,
    ASTRAZ_COLOUR_WHITE = 9,
    ASTRAZ_COLOUR_GREY = 10,
    ASTRAZ_COLOUR_TRANSPARENT = 15
};

enum {
    ASTRAZ_RESTART_BEGIN = 0,
    ASTRAZ_RESTART_PROPERTIES = 1,
    ASTRAZ_RESTART_END = 2
};

enum {
    ASTRAZ_EVENT_SCREEN_BEGIN = 1,
    ASTRAZ_EVENT_SCREEN_END,
    ASTRAZ_EVENT_CHARACTER,
    ASTRAZ_EVENT_CURSOR,
    ASTRAZ_EVENT_ERASE,
    ASTRAZ_EVENT_SCROLL,
    ASTRAZ_EVENT_STYLE,
    ASTRAZ_EVENT_COLOUR,
    ASTRAZ_EVENT_FONT,
    ASTRAZ_EVENT_PICTURE,
    ASTRAZ_EVENT_SOUND_PREPARE,
    ASTRAZ_EVENT_SOUND_PLAY,
    ASTRAZ_EVENT_SOUND_STOP,
    ASTRAZ_EVENT_SOUND_RELEASE,
    ASTRAZ_EVENT_BEEP,
    ASTRAZ_EVENT_RESTART,
    ASTRAZ_EVENT_MORE,
    ASTRAZ_EVENT_REPAINT,
    ASTRAZ_EVENT_WARNING
};

typedef struct AstraZResource {
    uint32_t format; /* Blorb four-character chunk code, big-endian. */
    const uint8_t *bytes; /* Valid only during callback. */
    uint32_t length;
} AstraZResource;

typedef struct AstraZEvent {
    uint32_t kind;
    union {
        struct { uint32_t version; uint16_t width, height; } screen;
        struct { uint16_t character; int32_t window; } text;
        struct { int32_t row, column, window; } cursor;
        struct { int32_t top, left, bottom, right, window; } erase;
        struct { int32_t top, left, bottom, right, units; } scroll;
        struct { int32_t style, window; } style;
        struct { int32_t foreground, background, window; } colour;
        struct { int32_t font; } font;
        struct { int32_t number, row, column; AstraZResource resource; } picture;
        struct {
            int32_t number, volume, repeats;
            uint16_t end_routine;
            AstraZResource resource;
        } sound;
        struct { int32_t pitch; } beep;
        struct { int32_t stage; } restart;
        struct {
            int32_t window, old_y, new_y, x, height, width;
        } repaint;
        struct { const char *text; } warning; /* Borrowed during callback. */
    } data; /* Coordinates retain Frotz's 1-based units. */
} AstraZEvent;

typedef struct AstraZStory {
    uint32_t vm;
    uint32_t container;
    uint32_t version; /* Z version 1..8, or full Glulx version. */
    uint64_t payload_offset;
    uint64_t payload_length;
} AstraZStory;

typedef struct AstraZHost {
    void *context;
    uint32_t capabilities;
    uint16_t width, height; /* Pixels in V6, character cells in V1..V5/V7/V8. */
    uint8_t cell_width, cell_height;
    int (*event)(void *context, const AstraZEvent *event); /* Negative: fatal; repaint uses positive for success. */
    uint16_t (*read_key)(void *context, uint32_t timeout_tenths,
                         int show_cursor);
    uint16_t (*read_line)(void *context, uint16_t *buffer, uint32_t max,
                          uint32_t timeout_tenths, uint32_t width,
                          int continued);
    int (*choose_file)(void *context, int purpose, const char *suggested,
                       char *path, size_t path_capacity);
    int (*font_metrics)(void *context, int font, int *height, int *width);
    int (*character_width)(void *context, uint16_t character);
    int (*glyph_available)(void *context, int font, uint16_t character);
    int (*picture_info)(void *context, int picture,
                        const AstraZResource *resource, int *height,
                        int *width);
    int (*mouse)(void *context, int *x, int *y);
    int (*peek_colour)(void *context);
    int (*colour_from_true)(void *context, uint16_t rgb5);
    uint16_t (*colour_to_true)(void *context, int index);
    uint32_t (*random_seed)(void *context);
    int (*sound_finished)(void *context); /* Drain completed sounds when input returns or tick runs. */
    int (*tick)(void *context); /* Return zero to cancel execution. */
} AstraZHost;

typedef struct AstraZRun {
    const char *story_path;
    const char *resources_path; /* Optional companion Blorb. */
    const AstraZHost *host;
    char *error;
    size_t error_capacity;
} AstraZRun;

int astraz_engine_probe(const char *path, AstraZStory *story);
int astraz_engine_run(const AstraZRun *request);
/* Pump a completed sound while a GUI host is waiting for user input. */
void astraz_engine_poll_sound(void);

#endif
