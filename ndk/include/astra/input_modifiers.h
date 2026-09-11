#ifndef ASTRA_INPUT_MODIFIERS_H
#define ASTRA_INPUT_MODIFIERS_H

/** @file input_modifiers.h @brief Modifier bits carried by input events. */

#include <stdint.h>

/** Left Control key. */
#define ASTRA_INPUT_MOD_LEFT_CTRL   (UINT32_C(1) << 0)
/** Left Shift key. */
#define ASTRA_INPUT_MOD_LEFT_SHIFT  (UINT32_C(1) << 1)
/** Left Alt key. */
#define ASTRA_INPUT_MOD_LEFT_ALT    (UINT32_C(1) << 2)
/** Left GUI/Command key. */
#define ASTRA_INPUT_MOD_LEFT_GUI    (UINT32_C(1) << 3)
/** Right Control key. */
#define ASTRA_INPUT_MOD_RIGHT_CTRL  (UINT32_C(1) << 4)
/** Right Shift key. */
#define ASTRA_INPUT_MOD_RIGHT_SHIFT (UINT32_C(1) << 5)
/** Right Alt key. */
#define ASTRA_INPUT_MOD_RIGHT_ALT   (UINT32_C(1) << 6)
/** Right GUI/Command key. */
#define ASTRA_INPUT_MOD_RIGHT_GUI   (UINT32_C(1) << 7)
/** Caps Lock state. */
#define ASTRA_INPUT_MOD_CAPS_LOCK   (UINT32_C(1) << 8)
/** Input-service-selected primary command modifier. */
#define ASTRA_INPUT_MOD_META        (UINT32_C(1) << 9)

/** Either Control key. */
#define ASTRA_INPUT_MOD_CTRL \
    (ASTRA_INPUT_MOD_LEFT_CTRL | ASTRA_INPUT_MOD_RIGHT_CTRL)
/** Either Shift key. */
#define ASTRA_INPUT_MOD_SHIFT \
    (ASTRA_INPUT_MOD_LEFT_SHIFT | ASTRA_INPUT_MOD_RIGHT_SHIFT)
/** Either Alt key. */
#define ASTRA_INPUT_MOD_ALT \
    (ASTRA_INPUT_MOD_LEFT_ALT | ASTRA_INPUT_MOD_RIGHT_ALT)
/** Either GUI/Command key. */
#define ASTRA_INPUT_MOD_GUI \
    (ASTRA_INPUT_MOD_LEFT_GUI | ASTRA_INPUT_MOD_RIGHT_GUI)

#endif
