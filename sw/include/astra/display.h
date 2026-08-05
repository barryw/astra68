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

#define ASTRA_CAPABILITY_DISPLAY_DEVICE UINT32_C(0x44535044) /* DSPD */

/* Capabilities reported through the device query. */
#define ASTRA_DISPLAY_CAP_TEXT (UINT32_C(1) << 0)

/* Cells accepted by one console write. */
#define ASTRA_CONSOLE_WRITE_MAX 256u

#endif
