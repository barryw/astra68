/** @file keymap.h @brief Keyboard-usage to UTF-8 translation. */
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

/** Either Shift key is active. */
#define ASTRA_KEYMAP_MOD_SHIFT   (1u << 0)
/** Either Control key is active. */
#define ASTRA_KEYMAP_MOD_CONTROL (1u << 1)
/** Caps Lock is active. */
#define ASTRA_KEYMAP_MOD_CAPS    (1u << 2)

/** Usage produces no text or editing action. */
#define ASTRA_KEYMAP_NONE      0u
/** Enter editing action. */
#define ASTRA_KEYMAP_ENTER     0x110000u
/** Backspace editing action. */
#define ASTRA_KEYMAP_BACKSPACE 0x110001u
/** Forward-delete editing action. */
#define ASTRA_KEYMAP_DELETE    0x110002u
/** Move left editing action. */
#define ASTRA_KEYMAP_LEFT      0x110003u
/** Move right editing action. */
#define ASTRA_KEYMAP_RIGHT     0x110004u
/** Move up editing action. */
#define ASTRA_KEYMAP_UP        0x110005u
/** Move down editing action. */
#define ASTRA_KEYMAP_DOWN      0x110006u
/** Move to beginning editing action. */
#define ASTRA_KEYMAP_HOME      0x110007u
/** Move to end editing action. */
#define ASTRA_KEYMAP_END       0x110008u
/** Tab traversal or insertion action. */
#define ASTRA_KEYMAP_TAB       0x110009u
/** Escape editing action. */
#define ASTRA_KEYMAP_ESCAPE    0x11000au

/** USB HID Caps Lock usage. */
#define ASTRA_KEYMAP_USAGE_CAPS_LOCK    0x39u
/** USB HID left Control usage. */
#define ASTRA_KEYMAP_USAGE_LEFT_CONTROL 0xe0u
/** USB HID left Shift usage. */
#define ASTRA_KEYMAP_USAGE_LEFT_SHIFT   0xe1u
/** USB HID right Shift usage. */
#define ASTRA_KEYMAP_USAGE_RIGHT_SHIFT  0xe5u
/** USB HID right Control usage. */
#define ASTRA_KEYMAP_USAGE_RIGHT_CONTROL 0xe4u

/**
 * Returns a Unicode scalar, one of the out-of-Unicode codes above, or
 * ASTRA_KEYMAP_NONE for a usage that produces nothing. Control collapses a
 * letter to its control code, so ^C arrives as 0x03.
 * @param usage USB HID keyboard usage.
 * @param modifiers ASTRA_KEYMAP_MOD_* bit mask.
 * @return Unicode scalar, editing-action code, or ASTRA_KEYMAP_NONE.
 */
uint32_t astra_keymap_translate(uint32_t usage, uint32_t modifiers);

/**
 * Test whether a USB HID usage changes the tracked modifier mask.
 * @param usage USB HID keyboard usage.
 * @return Nonzero for a supported modifier usage.
 */
int astra_keymap_is_modifier(uint32_t usage);

/**
 * Fold a modifier-key transition into a modifier mask.
 * @param modifiers Current ASTRA_KEYMAP_MOD_* mask.
 * @param usage USB HID keyboard usage.
 * @param pressed Nonzero for key-down, zero for key-up.
 * @return Updated modifier mask.
 */
uint32_t astra_keymap_apply_modifier(uint32_t modifiers, uint32_t usage,
                                     int pressed);

#endif
