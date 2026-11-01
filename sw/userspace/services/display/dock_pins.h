#ifndef ASTRA_DISPLAY_DOCK_PINS_H
#define ASTRA_DISPLAY_DOCK_PINS_H

#include <astra/bundle.h>
#include <astra/status.h>
#include <astra/vfs_service.h>

#include <stdint.h>

/* One stable slot per app, bounded by the fixed-width 1920-pixel Dock. */
#define DISPLAY_DOCK_PIN_LIMIT 36u

typedef struct DisplayDockPin {
    char *path;
    void *icon_bytes;
    uint32_t icon_length;
    char id[ASTRA_BUNDLE_ID_MAX];
    char name[ASTRA_BUNDLE_NAME_MAX];
    uint8_t online;
} DisplayDockPin;

typedef struct DisplayDockPins {
    DisplayDockPin entries[DISPLAY_DOCK_PIN_LIMIT];
    uint32_t count;
} DisplayDockPins;

void display_dock_pins_clear(DisplayDockPins *pins);
int32_t display_dock_pins_find(const DisplayDockPins *pins, const char *id);
uint32_t display_dock_pins_set(DisplayDockPins *pins, const char *id,
                               const char *name, const char *path);
uint32_t display_dock_pins_remove(DisplayDockPins *pins, uint32_t index);
uint32_t display_dock_pins_move(DisplayDockPins *pins, uint32_t from,
                                uint32_t to);
/* One length-prefixed string lets Config Kit commit complete order atomically. */
uint32_t display_dock_pins_encode(const DisplayDockPins *pins, char *out,
                                  uint32_t capacity, uint32_t *required);
uint32_t display_dock_pins_decode(DisplayDockPins *pins, const char *text,
                                  uint32_t length);

#endif
