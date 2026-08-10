#ifndef ASTRA_DISPLAY_H
#define ASTRA_DISPLAY_H

#include <stdint.h>

/*
 * The display device.
 *
 * Leased like the block device and the keyboard, because the screen has one
 * owner and that ownership has to be grantable, rights-checked and revocable
 * rather than assumed. The initial image holds it today; a terminal service
 * holds it when there is one, and nothing in the kernel needs to change for
 * that to happen.
 *
 * Only the character plane is exposed so far. The kernel drives that plane for
 * POST and panic output whether userspace exists or not, so exposing it costs
 * no new hardware knowledge. Framebuffer mapping and glyph rendering are the
 * intended growth, and they belong to this device rather than beside it.
 */

#define ASTRA_DEVICE_CLASS_DISPLAY UINT32_C(0x44495350) /* DISP */
#define ASTRA_DEVICE_ID_DISPLAY0   UINT32_C(0x44490001)

#define ASTRA_CAPABILITY_DISPLAY_DEVICE "DISPLAY"

/* Capabilities reported through the device query. */
#define ASTRA_DISPLAY_CAP_TEXT (UINT32_C(1) << 0)

/* Cells accepted by one console write. */
#define ASTRA_CONSOLE_WRITE_MAX 256u

/*
 * The file-backed QEMU text page keeps renderer-only state at its end. The
 * sequence is odd while the guest updates the cursor and even when complete,
 * so the ARM renderer never has to accept a torn row/column pair.
 */
#define ASTRA_TEXT_PLANE_BYTES 4096u
#define ASTRA_TEXT_CURSOR_OFFSET (ASTRA_TEXT_PLANE_BYTES - 8u)
#define ASTRA_TEXT_CURSOR_MAGIC_0 ((uint8_t)'A')
#define ASTRA_TEXT_CURSOR_MAGIC_1 ((uint8_t)'C')
#define ASTRA_TEXT_CURSOR_MAGIC_2 ((uint8_t)'U')
#define ASTRA_TEXT_CURSOR_MAGIC_3 ((uint8_t)'R')
#define ASTRA_TEXT_CURSOR_ROW_OFFSET      (ASTRA_TEXT_CURSOR_OFFSET + 4u)
#define ASTRA_TEXT_CURSOR_COLUMN_OFFSET   (ASTRA_TEXT_CURSOR_OFFSET + 5u)
#define ASTRA_TEXT_CURSOR_FLAGS_OFFSET    (ASTRA_TEXT_CURSOR_OFFSET + 6u)
#define ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET (ASTRA_TEXT_CURSOR_OFFSET + 7u)
#define ASTRA_TEXT_CURSOR_VISIBLE (1u << 0)

#endif
