#ifndef ASTRA_KEYMAP_H
#define ASTRA_KEYMAP_H

#include <stdint.h>

/*
 * HID usage to character, US layout.
 *
 * Kept apart from the terminal because it is the same question wherever a key
 * is turned into text, and kept to the signature the input service already
 * expects for a translation hook (usage, modifiers) so the service can call it
 * unchanged once one exists.
 *
 * Keys that are not text -- arrows, home, end -- report themselves through the
 * editing codes rather than a character, because that is what a line editor
 * needs to hear.
 */

#define ASTRA_KEYMAP_MOD_SHIFT   (1u << 0)
#define ASTRA_KEYMAP_MOD_CONTROL (1u << 1)
#define ASTRA_KEYMAP_MOD_CAPS    (1u << 2)

/* Returned instead of a character for keys that move rather than type. */
#define ASTRA_KEYMAP_NONE      0u
#define ASTRA_KEYMAP_ENTER     0x100u
#define ASTRA_KEYMAP_BACKSPACE 0x101u
#define ASTRA_KEYMAP_DELETE    0x102u
#define ASTRA_KEYMAP_LEFT      0x103u
#define ASTRA_KEYMAP_RIGHT     0x104u
#define ASTRA_KEYMAP_UP        0x105u
#define ASTRA_KEYMAP_DOWN      0x106u
#define ASTRA_KEYMAP_HOME      0x107u
#define ASTRA_KEYMAP_END       0x108u
#define ASTRA_KEYMAP_TAB       0x109u
#define ASTRA_KEYMAP_ESCAPE    0x10au

/* HID usages for the modifier keys, so a caller can track them. */
#define ASTRA_KEYMAP_USAGE_CAPS_LOCK    0x39u
#define ASTRA_KEYMAP_USAGE_LEFT_CONTROL 0xe0u
#define ASTRA_KEYMAP_USAGE_LEFT_SHIFT   0xe1u
#define ASTRA_KEYMAP_USAGE_RIGHT_SHIFT  0xe5u
#define ASTRA_KEYMAP_USAGE_RIGHT_CONTROL 0xe4u

/*
 * Returns a character in 0x01..0x7e, one of the codes above, or
 * ASTRA_KEYMAP_NONE for a usage that produces nothing. Control collapses a
 * letter to its control code, so ^C arrives as 0x03.
 */
uint32_t astra_keymap_translate(uint32_t usage, uint32_t modifiers);

/* Non-zero when the usage is a modifier the caller should track. */
int astra_keymap_is_modifier(uint32_t usage);

/* Folds a modifier key press or release into a modifier mask. */
uint32_t astra_keymap_apply_modifier(uint32_t modifiers, uint32_t usage,
                                     int pressed);

#endif
