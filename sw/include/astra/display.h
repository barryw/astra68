#ifndef ASTRA_DISPLAY_H
#define ASTRA_DISPLAY_H

/**
 * @file display.h
 * @brief The display device.
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

#include <astra/compiler.h>
#include <stdint.h>
#include <astra/pointer_shapes.h>

/** Native-big-endian `DISP` display device class, matched by device lookup. */
#define ASTRA_DEVICE_CLASS_DISPLAY UINT32_C(0x44495350) /* DISP */
/** Identifier of the one display device instance. */
#define ASTRA_DEVICE_ID_DISPLAY0   UINT32_C(0x44490001)

/** Capability name under which the display device lease is granted. */
#define ASTRA_CAPABILITY_DISPLAY_DEVICE "DISPLAY"

/** Capability reported through the device query: the character plane is available. */
#define ASTRA_DISPLAY_CAP_TEXT           (UINT32_C(1) << 0)
/** Capability reported through the device query: ASTRA_DISPLAY_FRAME_PRESENT_SOLID is supported. */
#define ASTRA_DISPLAY_CAP_SOLID_FRAME    (UINT32_C(1) << 1)
/** Capability reported through the device query: DISPLAY_SUBMIT/DISPLAY_COLLECT fences are honored. */
#define ASTRA_DISPLAY_CAP_FENCED_PRESENT (UINT32_C(1) << 2)
/** Capability reported through the device query: ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH is supported. */
#define ASTRA_DISPLAY_CAP_RENDER_BATCH   (UINT32_C(1) << 3)
/** Capability reported through the device query: the hardware cursor
 * (::ASTRA_SYSCALL_DISPLAY_CURSOR and ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE) is
 * supported.
 */
#define ASTRA_DISPLAY_CAP_HARDWARE_CURSOR (UINT32_C(1) << 4)
/** Capability reported through the device query: ASTRA_DISPLAY_FRAME_READ_SURFACE is supported. */
#define ASTRA_DISPLAY_CAP_READ_SURFACE   (UINT32_C(1) << 5)
/** Capability reported through the device query: a render batch may carry an
 * area attachment (AstraDisplayFrameRequest::attachment).
 */
#define ASTRA_DISPLAY_CAP_ATTACHMENT     (UINT32_C(1) << 6)

/** Capability name under which the display's completion interrupt is granted. */
#define ASTRA_CAPABILITY_DISPLAY_IRQ "DISPLAY_IRQ"
/** Capability name under which the display's vertical-blank interrupt is granted. */
#define ASTRA_CAPABILITY_DISPLAY_VBLANK_IRQ "VBLANK_IRQ"

/** Fixed display width, in pixels. */
#define ASTRA_DISPLAY_WIDTH  1920u
/** Fixed display height, in pixels. */
#define ASTRA_DISPLAY_HEIGHT 1080u

/** AstraDisplayFrameRequest::operation: present a solid RGB565 color. */
#define ASTRA_DISPLAY_FRAME_PRESENT_SOLID 1u
/** AstraDisplayFrameRequest::operation: present a full-screen RGB565 frame. */
#define ASTRA_DISPLAY_FRAME_PRESENT_RGB565 2u
/** AstraDisplayFrameRequest::operation: execute a render batch. */
#define ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH 3u
/* Operation 4 retired: the cursor position is a posted register write
   (::ASTRA_SYSCALL_DISPLAY_CURSOR), never a queued request. */
/** Device-side display operation reserved for the kernel's own panic/POST
 * text output; never submitted through a user DISPLAY_SUBMIT request.
 */
#define ASTRA_DISPLAY_PANIC_TEXT 5u
/** AstraDisplayFrameRequest::operation: upload a new hardware cursor image. */
#define ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE 6u
/** AstraDisplayFrameRequest::operation: copy a rectangle of one Media RAM
 * surface into the request's DMA buffer.
 */
#define ASTRA_DISPLAY_FRAME_READ_SURFACE 7u

/** Cursor flag (::ASTRA_SYSCALL_DISPLAY_CURSOR): the cursor is shown. */
#define ASTRA_DISPLAY_CURSOR_VISIBLE (UINT32_C(1) << 0)
/** Bit position of the pointer shape within the cursor flags. */
#define ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT 1u
/** Mask of the pointer shape within the cursor flags. */
#define ASTRA_DISPLAY_CURSOR_SHAPE_MASK (UINT32_C(15) << ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT)
/** Encode a pointer shape into the cursor flags.
 * @param shape ASTRA_POINTER_SHAPE_* index to encode.
 * @return shape shifted into ::ASTRA_DISPLAY_CURSOR_SHAPE_MASK's position.
 */
#define ASTRA_DISPLAY_CURSOR_SHAPE(shape) \
    ((uint32_t)(shape) << ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT)
/** Every bit the cursor flags define. */
#define ASTRA_DISPLAY_CURSOR_FLAGS_MASK \
    (ASTRA_DISPLAY_CURSOR_VISIBLE | ASTRA_DISPLAY_CURSOR_SHAPE_MASK)

/** Native-big-endian `CURS` signature identifying a valid cursor image. */
#define ASTRA_DISPLAY_CURSOR_IMAGE_MAGIC UINT32_C(0x43555253) /* CURS */
/** Current cursor image ABI revision. */
#define ASTRA_DISPLAY_CURSOR_IMAGE_VERSION UINT32_C(1)
/** Fixed cursor image width, in pixels. */
#define ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH 32u
/** Fixed cursor image height, in pixels. */
#define ASTRA_DISPLAY_CURSOR_IMAGE_HEIGHT 32u
/** Pixel count of one cursor image. */
#define ASTRA_DISPLAY_CURSOR_IMAGE_PIXELS \
    (ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH * ASTRA_DISPLAY_CURSOR_IMAGE_HEIGHT)
/** Fixed byte size of ::AstraDisplayCursorImage. */
#define ASTRA_DISPLAY_CURSOR_IMAGE_BYTES \
    (16u + ASTRA_DISPLAY_CURSOR_IMAGE_PIXELS * 4u)

/** Immutable 32x32 ARGB image copied by one CURSOR_IMAGE_UPDATE submission. */
typedef struct AstraDisplayCursorImage {
    uint32_t magic;    /**< ::ASTRA_DISPLAY_CURSOR_IMAGE_MAGIC. */
    uint32_t version;  /**< ::ASTRA_DISPLAY_CURSOR_IMAGE_VERSION. */
    uint32_t hotspot;  /**< Cursor hotspot: x in bits 0-15, y in bits 16-31. */
    uint32_t reserved; /**< Reserved; zero. */
    uint32_t argb[ASTRA_DISPLAY_CURSOR_IMAGE_PIXELS]; /**< Row-major ARGB pixels. */
} AstraDisplayCursorImage;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraDisplayCursorImage) ==
                   ASTRA_DISPLAY_CURSOR_IMAGE_BYTES,
               "cursor image ABI size changed");
/** @endcond */

/*
 * READ_SURFACE buffer: this 64-byte header, written by the requester, then
 * read_height packed rows of the surface format, which the device writes.
 * The request's byte_size is the header plus those rows exactly. The device
 * rejects a header whose surface is not wholly inside Media RAM or whose
 * rectangle is not inside the surface, and then writes nothing.
 */
/** Native-big-endian `SREA` signature identifying a valid surface-read header. */
#define ASTRA_DISPLAY_SURFACE_READ_MAGIC UINT32_C(0x53524541) /* SREA */
/** Current surface-read header ABI revision. */
#define ASTRA_DISPLAY_SURFACE_READ_VERSION UINT32_C(1)
/** Fixed byte size of ::AstraDisplaySurfaceRead. */
#define ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES 64u

/** Header the requester writes at the start of a READ_SURFACE DMA buffer;
 * the device appends read_height packed rows of the surface format after it.
 */
typedef struct AstraDisplaySurfaceRead {
    uint32_t magic;       /**< ::ASTRA_DISPLAY_SURFACE_READ_MAGIC. */
    uint32_t version;     /**< ::ASTRA_DISPLAY_SURFACE_READ_VERSION. */
    /** Graphics-arena byte offset and extent of the surface allocation. */
    uint32_t data_offset; /**< Graphics-arena byte offset of the surface. */
    uint32_t data_bytes;  /**< Byte extent of the surface allocation. */
    uint32_t pitch;       /**< Surface row pitch, in bytes. */
    uint16_t width;       /**< Surface width, in pixels. */
    uint16_t height;      /**< Surface height, in pixels. */
    uint32_t format;      /**< ASTRA_RENDER_FORMAT_* pixel format. */
    uint16_t x;           /**< Left edge of the rectangle to read, in pixels. */
    uint16_t y;           /**< Top edge of the rectangle to read, in pixels. */
    uint16_t read_width;  /**< Width of the rectangle to read, in pixels. */
    uint16_t read_height; /**< Height of the rectangle to read, in pixels; row count written. */
    uint32_t reserved[7]; /**< Reserved; zero. */
} AstraDisplaySurfaceRead;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraDisplaySurfaceRead) ==
                   ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES,
               "surface read header ABI size changed");
/** @endcond */

/** AstraDisplayFrameCompletion::status: the named request completed normally. */
#define ASTRA_DISPLAY_COMPLETION_OK          0u
/** AstraDisplayFrameCompletion::status: the device rejected the request's own contents. */
#define ASTRA_DISPLAY_COMPLETION_BAD_REQUEST 1u
/** AstraDisplayFrameCompletion::status: the device could not complete the request. */
#define ASTRA_DISPLAY_COMPLETION_IO_ERROR    2u
/** AstraDisplayFrameCompletion::status: the device reset before or while processing the request. */
#define ASTRA_DISPLAY_COMPLETION_RESET       3u

/** Fixed byte size of ::AstraDisplayFrameRequest. */
#define ASTRA_DISPLAY_FRAME_REQUEST_SIZE    40u
/** Fixed byte size of ::AstraDisplayFrameCompletion. */
#define ASTRA_DISPLAY_FRAME_COMPLETION_SIZE 20u

/*
 * Supervisor-only AstraHost display transport: the Vesta MMIO register
 * fields the kernel's platform layer uses to submit and collect display
 * requests. Never constructed by user code.
 */
/** @cond ASTRA_INTERNAL */
#define ASTRA_DISPLAY_HOST_ID_MAGIC       UINT32_C(0x44504c59) /* DPLY */
/* 1.1: the device holds ASTRA_DISPLAY_HOST_QUEUE_DEPTH requests; DISPLAY_QUEUE
   counts them and the submits it accepted, and CPL_* are the oldest of a
   FIFO of completions that POP advances. */
#define ASTRA_DISPLAY_HOST_VERSION_1_1    UINT32_C(0x00010001)
#define ASTRA_DISPLAY_HOST_CAP_SOLID_FRAME    (UINT32_C(1) << 0)
#define ASTRA_DISPLAY_HOST_CAP_FENCED_PRESENT (UINT32_C(1) << 1)
#define ASTRA_DISPLAY_HOST_CAP_RENDER_BATCH   (UINT32_C(1) << 2)
#define ASTRA_DISPLAY_HOST_CAP_HARDWARE_CURSOR (UINT32_C(1) << 3)
#define ASTRA_DISPLAY_HOST_CAP_READ_SURFACE    (UINT32_C(1) << 4)
/* DISPLAY_REQ_ATTACH names an AstraRenderAttachment for a render batch. */
#define ASTRA_DISPLAY_HOST_CAP_ATTACHMENT      (UINT32_C(1) << 5)
/*
 * DISPLAY_CURSOR (Vesta 0x738): the cursor as one posted, latest-value word,
 * x in bits 0-11, y in 12-23 and the cursor flags in 24-27. A write replaces
 * the cursor; it takes no request slot, completes nothing and raises no
 * interrupt, so it never waits behind rendering. One word, so the device can
 * never see a position with another write's flags.
 */
#define ASTRA_DISPLAY_HOST_CURSOR_X_MASK UINT32_C(0x00000fff)
#define ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT 12u
#define ASTRA_DISPLAY_HOST_CURSOR_Y_MASK UINT32_C(0x00fff000)
#define ASTRA_DISPLAY_HOST_CURSOR_FLAGS_SHIFT 24u
#define ASTRA_DISPLAY_HOST_CURSOR_PACK(x, y, flags) \
    (((uint32_t)(x) & ASTRA_DISPLAY_HOST_CURSOR_X_MASK) | \
     (((uint32_t)(y) << ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT) & \
      ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) | \
     (((uint32_t)(flags) & ASTRA_DISPLAY_CURSOR_FLAGS_MASK) << \
      ASTRA_DISPLAY_HOST_CURSOR_FLAGS_SHIFT))
#define ASTRA_DISPLAY_HOST_CURSOR_X(word) \
    ((uint32_t)(word) & ASTRA_DISPLAY_HOST_CURSOR_X_MASK)
#define ASTRA_DISPLAY_HOST_CURSOR_Y(word) \
    (((uint32_t)(word) & ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) >> \
     ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT)
#define ASTRA_DISPLAY_HOST_CURSOR_FLAGS(word) \
    ((uint32_t)(word) >> ASTRA_DISPLAY_HOST_CURSOR_FLAGS_SHIFT)
/* True when @p word names an on-screen position and defined flags. */
#define ASTRA_DISPLAY_HOST_CURSOR_VALID(word) \
    (ASTRA_DISPLAY_HOST_CURSOR_X(word) < ASTRA_DISPLAY_WIDTH && \
     ASTRA_DISPLAY_HOST_CURSOR_Y(word) < ASTRA_DISPLAY_HEIGHT && \
     (ASTRA_DISPLAY_HOST_CURSOR_FLAGS(word) & \
      ~ASTRA_DISPLAY_CURSOR_FLAGS_MASK) == 0u && \
     ((ASTRA_DISPLAY_HOST_CURSOR_FLAGS(word) & \
       ASTRA_DISPLAY_CURSOR_SHAPE_MASK) >> \
      ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT) < ASTRA_POINTER_SHAPE_COUNT)
#define ASTRA_DISPLAY_HOST_OPERATION_MASK UINT32_C(0xff)
#define ASTRA_DISPLAY_HOST_BYTE_SIZE_SHIFT 8u
#define ASTRA_DISPLAY_HOST_BYTE_SIZE_MAX UINT32_C(0x00ffffff)
/*
 * DISPLAY_QUEUE. A request holds one of the device's DEPTH slots from the
 * submit that is accepted until its completion is popped: BUSY while one
 * runs, HELD the slots in use, REQUEST_READY while one is free,
 * COMPLETION_VALID while a completion waits in CPL_*, and ACCEPTED counts
 * accepted submits modulo 256, so a submitter tells its own acceptance from
 * an earlier request's progress. The device raises Astraea DRAW_DONE while
 * any completion waits.
 */
#define ASTRA_DISPLAY_HOST_QUEUE_DEPTH            2u
#define ASTRA_DISPLAY_HOST_QUEUE_BUSY             (UINT32_C(1) << 0)
#define ASTRA_DISPLAY_HOST_QUEUE_HELD_SHIFT       4u
#define ASTRA_DISPLAY_HOST_QUEUE_HELD_MASK        UINT32_C(0x000000f0)
#define ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY    (UINT32_C(1) << 8)
#define ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID (UINT32_C(1) << 20)
#define ASTRA_DISPLAY_HOST_QUEUE_ACCEPTED_SHIFT   24u
#define ASTRA_DISPLAY_HOST_QUEUE_HELD(queue) \
    (((uint32_t)(queue) & ASTRA_DISPLAY_HOST_QUEUE_HELD_MASK) >> \
     ASTRA_DISPLAY_HOST_QUEUE_HELD_SHIFT)
#define ASTRA_DISPLAY_HOST_QUEUE_ACCEPTED(queue) \
    ((uint32_t)(queue) >> ASTRA_DISPLAY_HOST_QUEUE_ACCEPTED_SHIFT)
#define ASTRA_DISPLAY_HOST_SUBMIT UINT32_C(1)
#define ASTRA_DISPLAY_HOST_POP    UINT32_C(2)
#define ASTRA_DISPLAY_HOST_RESET  UINT32_C(4)
/** @endcond */

/*
 * The file-backed QEMU text page keeps renderer-only state at its end. The
 * sequence is odd while the guest updates a multi-cell text operation or the
 * cursor and even when complete, so the host renderer never accepts a torn
 * scroll, write, or row/column pair. Kernel/platform-only: never constructed
 * by user code.
 */
/** @cond ASTRA_INTERNAL */
#define ASTRA_TEXT_PLANE_BYTES 4096u
#define ASTRA_TEXT_COLUMNS 90u
#define ASTRA_TEXT_ROWS 30u
#define ASTRA_TEXT_TOP_MARGIN 2u
#define ASTRA_TEXT_LEFT_MARGIN 2u
#define ASTRA_TEXT_RIGHT_MARGIN 2u
#define ASTRA_TEXT_BOTTOM_MARGIN 2u
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
/** @endcond */

/** One DISPLAY_SUBMIT request. AstraDisplayFrameRequest::operation selects
 * which other fields are meaningful, as noted per field below.
 */
typedef struct AstraDisplayFrameRequest {
    _Alignas(4) uint32_t size; /**< ::ASTRA_DISPLAY_FRAME_REQUEST_SIZE. */
    uint32_t operation; /**< ASTRA_DISPLAY_FRAME_PRESENT_* or ASTRA_DISPLAY_CURSOR_* code. */
    uint32_t fence;     /**< Nonzero caller-chosen id echoed in the matching completion. */
    /** RGB565 color for SOLID; DMA handle for frames, batches, and
       READ_SURFACE; cursor X for CURSOR. */
    uint32_t source;
    /** Frame pitch, or cursor Y for CURSOR. */
    uint32_t pitch;
    /** Frame byte size, or ASTRA_DISPLAY_CURSOR_* flags for CURSOR. */
    uint32_t byte_size;
    /**
     * RENDER_BATCH only, otherwise zero: an area handle whose bytes
     * [attachment_offset, attachment_offset + attachment_bytes) the device
     * places at batch offset attachment_target before executing the batch.
     * The attachment ends the batch's defined bytes; what follows it, up to
     * byte_size, is padding (a staged rectangle's last row is shorter than
     * its pitch) that the device need not fill. Pixels a client staged reach
     * Media RAM without the display service or the MC68040 copying them.
     */
    uint32_t attachment;
    uint32_t attachment_offset; /**< Byte offset into attachment of the staged bytes. */
    uint32_t attachment_bytes;  /**< Byte count of the staged bytes. */
    uint32_t attachment_target; /**< Batch offset the staged bytes are placed at. */
} AstraDisplayFrameRequest;

/** One DISPLAY_COLLECT completion, reporting the outcome of the request
 * named by fence.
 */
typedef struct AstraDisplayFrameCompletion {
    _Alignas(4) uint32_t size; /**< ::ASTRA_DISPLAY_FRAME_COMPLETION_SIZE. */
    uint32_t fence;      /**< The completed request's AstraDisplayFrameRequest::fence. */
    uint32_t status;     /**< ASTRA_DISPLAY_COMPLETION_* result. */
    uint32_t generation; /**< Device generation at completion time. */
    uint32_t reserved;   /**< Reserved; zero. */
} AstraDisplayFrameCompletion;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraDisplayFrameRequest) ==
                   ASTRA_DISPLAY_FRAME_REQUEST_SIZE,
               "display request ABI size changed");
_Static_assert(sizeof(AstraDisplayFrameCompletion) ==
                   ASTRA_DISPLAY_FRAME_COMPLETION_SIZE,
               "display completion ABI size changed");
/** @endcond */

#endif
