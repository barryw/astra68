#ifndef ASTRA_USERSPACE_RENDER_BUILDER_H
#define ASTRA_USERSPACE_RENDER_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include <astra/draw_list.h>
#include <astra/render_batch.h>

#define ASTRA_RENDER_BUILDER_BYTES ASTRA_RENDER_BATCH_BUFFER_BYTES

typedef enum AstraRenderBuilderFailure {
    ASTRA_RENDER_BUILDER_FAILURE_NONE = 0u,
    ASTRA_RENDER_BUILDER_FAILURE_DATA,
    ASTRA_RENDER_BUILDER_FAILURE_DESCRIPTOR,
    ASTRA_RENDER_BUILDER_FAILURE_DESTINATION,
    ASTRA_RENDER_BUILDER_FAILURE_COMMAND_CAPACITY,
    ASTRA_RENDER_BUILDER_FAILURE_SURFACE,
    ASTRA_RENDER_BUILDER_FAILURE_SCENE,
} AstraRenderBuilderFailure;

typedef enum AstraRenderReplayResult {
    ASTRA_RENDER_REPLAY_INVALID = 0,
    ASTRA_RENDER_REPLAY_DONE = 1,
    /* The batch is full: flush it and resume at *next in a new batch. */
    ASTRA_RENDER_REPLAY_FULL = 2,
    /* *next is a TARGET command: replay what follows it into its surface,
       from *next + 1, in this batch or another. */
    ASTRA_RENDER_REPLAY_TARGET = 3,
    /* *next is an UPLOAD command, validated but not lowered: the caller
       writes its staging rows into its surface, then replays from
       *next + 1. */
    ASTRA_RENDER_REPLAY_UPLOAD = 4,
} AstraRenderReplayResult;

typedef struct AstraRenderBuilder AstraRenderBuilder;

/* Maps a draw-list source surface id to a descriptor in the current batch,
   or returns zero to reject it. Called once per BLIT; must not cache
   descriptors, because a full batch rolls its data back. */
typedef struct AstraRenderSourceResolver {
    uint32_t (*resolve)(void *context, AstraRenderBuilder *builder,
                        uint32_t surface_id);
    void *context;
} AstraRenderSourceResolver;

struct AstraRenderBuilder {
    uint8_t *bytes;
    uint32_t generation;
    uint32_t command_count;
    uint32_t glyph_count;
    uint32_t data_cursor;
    uint32_t surface_cursor;
    uint32_t scene_offset;
    uint32_t scene_layer_count;
    uint32_t scene_output_capacity;
    uint32_t failed; /* AstraRenderBuilderFailure */
};

int astra_render_builder_init(AstraRenderBuilder *builder, void *storage,
                              uint32_t bytes, uint32_t generation);
uint32_t astra_render_builder_frame(const AstraRenderBuilder *builder);
int astra_render_builder_window_scene(AstraRenderBuilder *builder,
                                      uint32_t output_offset,
                                      uint32_t output_capacity,
                                      uint16_t width, uint16_t height,
                                      uint16_t backdrop_rgb565);
int astra_render_builder_window_scene_layer(AstraRenderBuilder *builder,
                                            uint32_t surface,
                                            int32_t x, int32_t y,
                                            uint16_t radius, int visible);
uint32_t astra_render_builder_scanout(AstraRenderBuilder *builder,
                                      uint32_t scanout_offset);
uint32_t astra_render_builder_surface(AstraRenderBuilder *builder,
                                      uint16_t width, uint16_t height);
uint32_t astra_render_builder_surface_at(AstraRenderBuilder *builder,
                                         uint32_t data_offset,
                                         uint32_t data_capacity,
                                         uint16_t width, uint16_t height);
/* A persistent Media RAM surface in any render format. access is
   ASTRA_RENDER_SURFACE_READ and/or ASTRA_RENDER_SURFACE_WRITE. */
uint32_t astra_render_builder_surface_format_at(
    AstraRenderBuilder *builder, uint32_t data_offset,
    uint32_t data_capacity, uint16_t width, uint16_t height, uint32_t pitch,
    uint8_t format, uint8_t access);
/* Row bytes for width pixels of format, or zero for an unknown format. */
uint32_t astra_render_format_row_bytes(uint8_t format, uint32_t width);
/* Rows of source pixels copied into the batch; returns a READ descriptor. */
uint32_t astra_render_builder_upload(AstraRenderBuilder *builder,
                                     const void *pixels,
                                     uint32_t source_pitch, uint16_t width,
                                     uint16_t height, uint8_t format);
/*
 * A source surface whose pixels the device places in the batch: height rows
 * of source_pitch bytes, as they lie in the caller's staging area. Nothing is
 * copied. Returns a READ descriptor and, in *batch_offset, where the pixels
 * belong; the caller submits the batch with the staged bytes as its
 * attachment (AstraDisplayFrameRequest). The pixels are the last data the
 * batch allocates, so they must be reserved after every other descriptor.
 */
uint32_t astra_render_builder_upload_reserve(AstraRenderBuilder *builder,
                                             uint32_t source_pitch,
                                             uint16_t width, uint16_t height,
                                             uint8_t format,
                                             uint32_t *batch_offset);
/* An ARGB8888 draw-list color converted to a destination's native value. */
uint32_t astra_render_builder_destination_color(uint8_t format,
                                                uint32_t argb);
/* A render-only batch (v1.4): no scanout, scene, or cursor change. */
uint32_t astra_render_builder_finish_render_only(AstraRenderBuilder *builder);
uint32_t astra_render_builder_upload_rgb565(AstraRenderBuilder *builder,
                                             const void *pixels,
                                             uint32_t source_pitch,
                                             uint16_t width, uint16_t height);
int astra_render_builder_fill(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t color);
int astra_render_builder_rounded(AstraRenderBuilder *builder,
                                 uint32_t destination, int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 uint16_t radius, uint16_t color);
int astra_render_builder_text(AstraRenderBuilder *builder,
                              uint32_t destination, int32_t x, int32_t y,
                              const char *utf8, uint32_t length,
                              uint16_t pixel_height, uint16_t color);
int astra_render_builder_text_styled(AstraRenderBuilder *builder,
                                     uint32_t destination, int32_t x,
                                     int32_t y, const char *utf8,
                                     uint32_t length,
                                     uint16_t pixel_height, uint16_t color,
                                     uint32_t style_flags);
int astra_render_builder_mono_text(AstraRenderBuilder *builder,
                                   uint32_t destination, int32_t x,
                                   int32_t y, const char *utf8,
                                   uint32_t length, uint16_t pixel_height,
                                   uint16_t cell_width, uint16_t color);
int astra_draw_list_covers(const AstraDrawListHeader *header,
                           uint16_t width, uint16_t height);
int astra_render_builder_replay_range(
    AstraRenderBuilder *builder, uint32_t destination,
    const AstraDrawListHeader *draw_list, uint32_t area_bytes,
    const AstraRenderSourceResolver *resolver, uint32_t first,
    uint32_t *next);
int astra_render_builder_replay(AstraRenderBuilder *builder,
                                uint32_t destination,
                                const AstraDrawListHeader *draw_list);
int astra_render_builder_blit(AstraRenderBuilder *builder,
                              uint32_t destination, uint32_t source,
                              int32_t x, int32_t y, uint16_t width,
                              uint16_t height, uint16_t radius,
                              int round_top);
int astra_render_builder_blit_clipped(
    AstraRenderBuilder *builder, uint32_t destination, uint32_t source,
    int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t radius,
    int round_top, int32_t clip_left, int32_t clip_top,
    int32_t clip_right, int32_t clip_bottom);
int astra_render_builder_blit_region(
    AstraRenderBuilder *builder, uint32_t destination, uint32_t source,
    int32_t source_x, int32_t source_y, int32_t destination_x,
    int32_t destination_y, uint16_t width, uint16_t height);
uint32_t astra_render_builder_finish(AstraRenderBuilder *builder);

#endif
