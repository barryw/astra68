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
#define ASTRA_DISPLAY_CAP_TEXT           (UINT32_C(1) << 0)
#define ASTRA_DISPLAY_CAP_SOLID_FRAME    (UINT32_C(1) << 1)
#define ASTRA_DISPLAY_CAP_FENCED_PRESENT (UINT32_C(1) << 2)
#define ASTRA_DISPLAY_CAP_RENDER_BATCH   (UINT32_C(1) << 3)
#define ASTRA_DISPLAY_CAP_HARDWARE_CURSOR (UINT32_C(1) << 4)

#define ASTRA_CAPABILITY_DISPLAY_IRQ "DISPLAY_IRQ"

#define ASTRA_DISPLAY_WIDTH  1280u
#define ASTRA_DISPLAY_HEIGHT 720u

#define ASTRA_DISPLAY_FRAME_PRESENT_SOLID 1u
#define ASTRA_DISPLAY_FRAME_PRESENT_RGB565 2u
#define ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH 3u
#define ASTRA_DISPLAY_CURSOR_UPDATE 4u

#define ASTRA_DISPLAY_CURSOR_VISIBLE (UINT32_C(1) << 0)
#define ASTRA_DISPLAY_CURSOR_DEFER_COMMIT (UINT32_C(1) << 1)

#define ASTRA_DISPLAY_COMPLETION_OK          0u
#define ASTRA_DISPLAY_COMPLETION_BAD_REQUEST 1u
#define ASTRA_DISPLAY_COMPLETION_IO_ERROR    2u
#define ASTRA_DISPLAY_COMPLETION_RESET       3u

#define ASTRA_DISPLAY_FRAME_REQUEST_SIZE    24u
#define ASTRA_DISPLAY_FRAME_COMPLETION_SIZE 20u

/* Supervisor-only AstraHost display transport. */
#define ASTRA_DISPLAY_HOST_ID_MAGIC       UINT32_C(0x44504c59) /* DPLY */
#define ASTRA_DISPLAY_HOST_VERSION_1_0    UINT32_C(0x00010000)
#define ASTRA_DISPLAY_HOST_CAP_SOLID_FRAME    (UINT32_C(1) << 0)
#define ASTRA_DISPLAY_HOST_CAP_FENCED_PRESENT (UINT32_C(1) << 1)
#define ASTRA_DISPLAY_HOST_CAP_RENDER_BATCH   (UINT32_C(1) << 2)
#define ASTRA_DISPLAY_HOST_CAP_HARDWARE_CURSOR (UINT32_C(1) << 3)
#define ASTRA_DISPLAY_HOST_CURSOR_X_MASK UINT32_C(0x0000ffff)
#define ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT 16u
#define ASTRA_DISPLAY_HOST_CURSOR_Y_MASK UINT32_C(0x7fff0000)
#define ASTRA_DISPLAY_HOST_CURSOR_VISIBLE UINT32_C(0x80000000)
#define ASTRA_DISPLAY_HOST_CURSOR_PACK(x, y, visible) \
    (((uint32_t)(x) & ASTRA_DISPLAY_HOST_CURSOR_X_MASK) | \
     (((uint32_t)(y) << ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT) & \
      ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) | \
     ((visible) ? ASTRA_DISPLAY_HOST_CURSOR_VISIBLE : 0u))
#define ASTRA_DISPLAY_HOST_OPERATION_MASK UINT32_C(0xff)
#define ASTRA_DISPLAY_HOST_BYTE_SIZE_SHIFT 8u
#define ASTRA_DISPLAY_HOST_BYTE_SIZE_MAX UINT32_C(0x00ffffff)
#define ASTRA_DISPLAY_HOST_QUEUE_BUSY             (UINT32_C(1) << 0)
#define ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY    (UINT32_C(1) << 8)
#define ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID (UINT32_C(1) << 20)
#define ASTRA_DISPLAY_HOST_SUBMIT UINT32_C(1)
#define ASTRA_DISPLAY_HOST_POP    UINT32_C(2)
#define ASTRA_DISPLAY_HOST_RESET  UINT32_C(4)

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

typedef struct AstraDisplayFrameRequest {
    _Alignas(4) uint32_t size;
    uint32_t operation;
    uint32_t fence;
    /* RGB565 color for SOLID; DMA handle for frames; cursor X for CURSOR. */
    uint32_t source;
    /* Frame pitch, or cursor Y for CURSOR. */
    uint32_t pitch;
    /* Frame byte size, or ASTRA_DISPLAY_CURSOR_* flags for CURSOR. */
    uint32_t byte_size;
} AstraDisplayFrameRequest;

typedef struct AstraDisplayFrameCompletion {
    _Alignas(4) uint32_t size;
    uint32_t fence;
    uint32_t status;
    uint32_t generation;
    uint32_t reserved;
} AstraDisplayFrameCompletion;

_Static_assert(sizeof(AstraDisplayFrameRequest) ==
                   ASTRA_DISPLAY_FRAME_REQUEST_SIZE,
               "display request ABI size changed");
_Static_assert(sizeof(AstraDisplayFrameCompletion) ==
                   ASTRA_DISPLAY_FRAME_COMPLETION_SIZE,
               "display completion ABI size changed");

#endif
