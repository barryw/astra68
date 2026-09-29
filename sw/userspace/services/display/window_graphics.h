#ifndef DISPLAY_WINDOW_GRAPHICS_H
#define DISPLAY_WINDOW_GRAPHICS_H

/*
 * GPU objects owned by one CONTENT_SURFACE window: Media RAM surfaces, the
 * CPU staging area used to fill them, and attached ADLT draw lists. The
 * window's control port is the session: when the owner dies the window
 * closes and display_window_graphics_close() releases everything.
 */

#include <stdint.h>

#include <astra/gui.h>
#include <astra/render_builder.h>

#define DISPLAY_GRAPHICS_LIST_MAX 8u
#define DISPLAY_GRAPHICS_SURFACE_MAX 4096u
/* ponytail: one fixed per-window share of Media RAM; replace with a
   process-wide graphics budget when several large clients compete. */
#define DISPLAY_GRAPHICS_BYTES_MAX UINT32_C(0x10000000)

typedef struct DisplayGraphicsSurface {
    uint32_t id;
    uint32_t offset;
    uint32_t bytes;
    uint32_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t format; /* ASTRA_RENDER_FORMAT_* */
    uint8_t reserved;
    uint16_t flags; /* ASTRA_SURFACE_* */
} DisplayGraphicsSurface;

typedef struct DisplayGraphicsList {
    uint32_t id;
    uint32_t area;
    const AstraDrawListHeader *mapping;
    uint32_t bytes;
} DisplayGraphicsList;

typedef struct DisplayWindowGraphics {
    uint32_t self_area;
    uint32_t surfaces_area;
    DisplayGraphicsSurface *surfaces;
    uint32_t surface_count;
    uint32_t surface_capacity;
    uint32_t surface_bytes;
    uint32_t next_surface;
    DisplayGraphicsList lists[DISPLAY_GRAPHICS_LIST_MAX];
    uint32_t next_list;
    uint32_t staging_area;
    /* Mapped read-write: SURFACE_READ returns pixels through it. */
    uint8_t *staging;
    uint32_t staging_bytes;
} DisplayWindowGraphics;

/* Staged bytes the device places at batch offset `target` before running
   the batch (AstraDisplayFrameRequest's attachment). */
typedef struct DisplayGraphicsAttachment {
    uint32_t area;
    uint32_t offset;
    uint32_t bytes;
    uint32_t target;
} DisplayGraphicsAttachment;

/* What a graphics command needs from the display service. */
typedef struct DisplayGraphicsHost {
    void *context;
    /* A free, 64-byte-aligned Media RAM extent, or zero. */
    uint32_t (*allocate)(void *context, uint32_t bytes);
    /* Submit one finished render-only batch, with an optional attachment,
       and wait for it. */
    uint32_t (*submit)(void *context, uint32_t bytes,
                       const DisplayGraphicsAttachment *attachment);
    /* Submit the AstraDisplaySurfaceRead request of @p bytes at the start
       of batch_storage and wait until its rows are there. */
    uint32_t (*read)(void *context, uint32_t bytes);
    void *batch_storage;
    /* The content bank the client draws into now. */
    uint32_t content_offset;
    uint32_t content_bytes;
    uint32_t content_pitch;
    uint16_t content_width;
    uint16_t content_height;
} DisplayGraphicsHost;

/* Create the graphics state and adopt @p staging_area (consumed on success). */
uint32_t display_window_graphics_open(DisplayWindowGraphics **graphics,
                                      uint32_t staging_area);
void display_window_graphics_close(DisplayWindowGraphics *graphics);
int display_window_graphics_command_valid(
    const AstraGuiGraphicsCommand *command, uint32_t size,
    uint32_t handle_count, uint32_t window);
/* Execute one validated command. handles[1] is consumed (zeroed) when the
   command takes ownership of it. */
uint32_t display_window_graphics_command(
    DisplayWindowGraphics *graphics, const DisplayGraphicsHost *host,
    const AstraGuiGraphicsCommand *command, uint32_t *handles,
    AstraGuiGraphicsReply *reply);

#endif
