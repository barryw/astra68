#include <astra/graphics.h>

#include <astra/area.h>
#include <astra/bytes.h>
#include <astra/draw_list.h>
#include <astra/gui.h>
#include <astra/port.h>
#include <astra/text_style.h>
#include <astra/utf8.h>
#include <astra/window.h>

#include <string.h>

#include "internal/status.h"
#include "internal/syscall.h"

/* The direct-MMIO NDK deliberately does not expose unsafe graphics MMIO.
   Every operation is a validated request to the display service; see
   docs/MANAGED_GRAPHICS.md. */

#define SURFACE_FLAGS (ASTRA_SURFACE_SCANOUT | ASTRA_SURFACE_DRAW_TARGET | \
                       ASTRA_SURFACE_DRAW_SOURCE | \
                       ASTRA_SURFACE_CPU_READ | ASTRA_SURFACE_CPU_WRITE)
#define DRAW_FLAGS ASTRA_DRAW_OPAQUE_BACKGROUND
#define BLIT_FLAGS (ASTRA_BLIT_FLIP_X | ASTRA_BLIT_FLIP_Y | \
                    ASTRA_BLIT_FILTER_LINEAR)
#define SPRITE_FLAGS (ASTRA_SPRITE_VISIBLE | ASTRA_SPRITE_FLIP_X | \
                      ASTRA_SPRITE_FLIP_Y | \
                      ASTRA_SPRITE_BEHIND_FRAMEBUFFER | \
                      ASTRA_SPRITE_COLLISION_ENABLE)
#define LIST_INITIAL_BYTES UINT32_C(65536)
#define STAGING_GRANULE UINT32_C(65536)
#define AREA_RIGHTS (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | \
                     ASTRA_RIGHT_TRANSFER)
#define SHARE_RIGHTS (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER)
/* The service writes readback rows into staging. */
#define STAGING_SHARE_RIGHTS (SHARE_RIGHTS | ASTRA_RIGHT_WRITE)
/* Vertex coordinates stay inside the engine's +-32768 pixels. */
#define VERTEX_COORD_LIMIT (INT32_C(1) << 23)

static int pixel_format_valid(uint16_t format)
{
    return format >= ASTRA_PIXEL_FORMAT_INDEX4 &&
           format <= ASTRA_PIXEL_FORMAT_ARGB8888;
}

static uint32_t row_bytes(uint16_t format, uint32_t width)
{
    switch (format) {
    case ASTRA_PIXEL_FORMAT_INDEX8: return width;
    case ASTRA_PIXEL_FORMAT_RGB565: return width * 2u;
    case ASTRA_PIXEL_FORMAT_XRGB8888:
    case ASTRA_PIXEL_FORMAT_ARGB8888: return width * 4u;
    default: return 0u;
    }
}

static int surface_usage_valid(const AstraSurfaceCreateInfo *info)
{
    uint32_t direct_only = ASTRA_SURFACE_SCANOUT |
                           ASTRA_SURFACE_DRAW_TARGET;

    if ((info->flags & direct_only) != 0u &&
        info->format != ASTRA_PIXEL_FORMAT_INDEX8 &&
        info->format != ASTRA_PIXEL_FORMAT_RGB565 &&
        info->format != ASTRA_PIXEL_FORMAT_XRGB8888 &&
        info->format != ASTRA_PIXEL_FORMAT_ARGB8888)
        return 0;
    return 1;
}

static int rect_valid(const AstraRectI32 *rectangle)
{
    return rectangle != 0 && rectangle->width != 0 &&
           rectangle->height != 0 && rectangle->width <= 32767u &&
           rectangle->height <= 32767u;
}

static int int16_value(int64_t value)
{
    return value >= INT16_MIN && value <= INT16_MAX;
}

static int sprite_source_rect_valid(const AstraRectI32 *rectangle)
{
    return rect_valid(rectangle) && rectangle->x >= 0 && rectangle->y >= 0 &&
           rectangle->width <= ASTRA_SPRITE_SOURCE_WIDTH_MAX &&
           rectangle->height <= ASTRA_SPRITE_SOURCE_HEIGHT_MAX;
}

static int paint_valid(const AstraDrawPaint *paint)
{
    return paint != 0 && paint->size >= sizeof(*paint) &&
           (paint->flags & ~DRAW_FLAGS) == 0 &&
           paint->blend <= ASTRA_BLEND_MULTIPLY &&
           astra_words_zero(paint->reserved, 3);
}

static int text_paint_valid(const AstraTextPaint *paint)
{
    return paint != 0 && paint->size >= sizeof(*paint) &&
           (paint->flags & ~(ASTRA_TEXT_PAINT_OPAQUE_BACKGROUND |
                             ASTRA_TEXT_PAINT_UNDERLINE |
                             ASTRA_TEXT_PAINT_STRIKETHROUGH)) == 0 &&
           paint->embedded_color_policy <= ASTRA_TEXT_EMBEDDED_COLOR_REJECT &&
           paint->reserved16 == 0 && astra_words_zero(paint->reserved, 5);
}

static int empty_handle(AstraHandle handle)
{
    return handle == ASTRA_INVALID_HANDLE;
}

static uint32_t argb(AstraColorRGBA8 color)
{
    return ((uint32_t)color.alpha << 24) | ((uint32_t)color.red << 16) |
           ((uint32_t)color.green << 8) | color.blue;
}

static int display_live(const AstraDisplay *display)
{
    return display != 0 && !empty_handle(display->_private_handle) &&
           display->_private_window != 0u;
}

static int surface_live(const AstraSurface *surface)
{
    return surface != 0 && !empty_handle(surface->_private_handle) &&
           surface->_private_window != 0u && surface->_private_id != 0u;
}

static int list_live(const AstraDrawList *list)
{
    return list != 0 && !empty_handle(list->_private_handle) &&
           list->_private_commands != 0 && list->_private_list != 0u;
}

static AstraResult graphics_command(AstraHandle control, uint32_t window,
                                    AstraGuiGraphicsCommand *request,
                                    AstraHandle *attachment,
                                    AstraGuiGraphicsReply *reply)
{
    static uint32_t transaction;
    AstraCall call = {0};
    AstraResult result;

    if (++transaction == 0u)
        transaction = 1u;
    result = astra_message_header_init(
        &request->header, sizeof(*request), ASTRA_GUI_PROTOCOL,
        ASTRA_GUI_VERSION, ASTRA_GUI_GRAPHICS_COMMAND, transaction);
    request->window = window;
    request->generation = 1u;
    call.request = request;
    call.request_size = sizeof(*request);
    call.handles = attachment;
    call.handle_count = attachment != 0 ? 1u : 0u;
    call.reply = reply;
    call.reply_capacity = sizeof(*reply);
    if (result == ASTRA_OK)
        result = astra_port_call(control, &call, ASTRA_DEADLINE_INFINITE);
    if (result == ASTRA_OK &&
        (call.reply_size != sizeof(*reply) || call.reply_handle_count != 0u ||
         reply->header.total_size != sizeof(*reply) ||
         reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
         reply->header.flags != 0u ||
         reply->header.protocol != ASTRA_GUI_PROTOCOL ||
         reply->header.protocol_version != ASTRA_GUI_VERSION ||
         reply->header.reserved != 0u ||
         reply->header.operation != ASTRA_GUI_GRAPHICS_REPLY ||
         reply->header.transaction_id != transaction ||
         reply->reserved != 0u))
        result = ASTRA_ERROR_IO;
    if (result == ASTRA_OK)
        result = astra_internal_service_result(reply->status);
    return result;
}

static void area_release(AstraArea *area)
{
    AstraResult ignored = astra_area_close(area);

    (void)ignored;
}

/* Create a mapped area and a duplicate with @p rights for the service. */
static AstraResult shared_area(uint32_t bytes, uint32_t rights,
                               AstraArea *area, AstraHandle *shared)
{
    AstraResult result = astra_area_create(bytes, AREA_RIGHTS, area);

    if (result == ASTRA_OK)
        result = astra_area_map(area, ASTRA_AREA_MAP_READ |
                                          ASTRA_AREA_MAP_WRITE);
    if (result == ASTRA_OK)
        result = astra_handle_duplicate(area->handle, rights, shared);
    if (result != ASTRA_OK)
        area_release(area);
    return result;
}

static void close_quietly(AstraHandle handle)
{
    if (!empty_handle(handle)) {
        AstraResult ignored = astra_handle_close(&handle);

        (void)ignored;
    }
}

int astra_graphics_present(void)
{
    return 0;
}

AstraResult astra_graphics_get_info(AstraGraphicsInfo *info)
{
    if (info == 0 || info->size < sizeof(*info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_NOT_PRESENT;
}

AstraResult astra_display_open(AstraDisplay *display)
{
    /* Exclusive fullscreen scene ownership is not implemented; windows
       reach the hardware through astra_window_display(). */
    if (display == 0 || !empty_handle(display->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_NOT_PRESENT;
}

AstraResult astra_window_display(const AstraWindow *window,
                                 AstraDisplay *display)
{
    if (window == 0 || window->_private_control == ASTRA_INVALID_HANDLE ||
        window->_private_id == 0u || display == 0 ||
        !empty_handle(display->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *display = (AstraDisplay)ASTRA_DISPLAY_INIT;
    display->_private_handle = window->_private_control;
    display->_private_window = window->_private_id;
    return ASTRA_OK;
}

AstraResult astra_display_close(AstraDisplay *display)
{
    AstraArea staging = ASTRA_AREA_INIT;
    AstraResult result = ASTRA_OK;

    if (display == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!display_live(display))
        return ASTRA_ERROR_INVALID_HANDLE;
    if (!empty_handle(display->_private_staging)) {
        staging.handle = display->_private_staging;
        staging.address = display->_private_staging_pixels;
        staging.size = display->_private_staging_bytes;
        staging.map_flags = ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE;
        result = astra_area_close(&staging);
    }
    *display = (AstraDisplay)ASTRA_DISPLAY_INIT;
    return result;
}

AstraResult astra_display_staging(AstraDisplay *display,
                                  uint32_t minimum_bytes, void **pixels,
                                  uint32_t *bytes)
{
    AstraArea replacement = ASTRA_AREA_INIT;
    AstraArea previous = ASTRA_AREA_INIT;
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};
    AstraHandle shared = ASTRA_INVALID_HANDLE;
    uint32_t size;
    AstraResult result;

    if (!display_live(display) || pixels == 0 || bytes == 0 ||
        minimum_bytes == 0u || minimum_bytes > ASTRA_AREA_SIZE_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (display->_private_staging_bytes < minimum_bytes) {
        size = minimum_bytes > ASTRA_AREA_SIZE_MAX - (STAGING_GRANULE - 1u) ?
               ASTRA_AREA_SIZE_MAX :
               (minimum_bytes + STAGING_GRANULE - 1u) & ~(STAGING_GRANULE - 1u);
        result = shared_area(size, STAGING_SHARE_RIGHTS, &replacement,
                             &shared);
        if (result != ASTRA_OK)
            return result;
        request.action = ASTRA_GUI_GRAPHICS_STAGING_SET;
        result = graphics_command(display->_private_handle,
                                  display->_private_window, &request,
                                  &shared, &reply);
        close_quietly(shared);
        if (result != ASTRA_OK) {
            area_release(&replacement);
            return result;
        }
        previous.handle = display->_private_staging;
        previous.address = display->_private_staging_pixels;
        previous.size = display->_private_staging_bytes;
        previous.map_flags = ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE;
        if (!empty_handle(previous.handle))
            area_release(&previous);
        display->_private_staging = replacement.handle;
        display->_private_staging_pixels = replacement.address;
        display->_private_staging_bytes = replacement.size;
    }
    *pixels = display->_private_staging_pixels;
    *bytes = display->_private_staging_bytes;
    return ASTRA_OK;
}

AstraResult astra_display_set_mode(const AstraDisplay *display,
                                   const AstraDisplayMode *mode,
                                   AstraFence *fence)
{
    AstraDisplayLayout layout;
    AstraResult result;

    if (display == 0 || fence == 0 || !empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_display_layout_calculate(
        mode, ASTRA_GRAPHICS_OUTPUT_WIDTH, ASTRA_GRAPHICS_OUTPUT_HEIGHT,
        &layout);
    return result == ASTRA_OK ? ASTRA_ERROR_INVALID_HANDLE : result;
}

AstraResult astra_display_set_pointer_image(
    const AstraDisplay *display, const AstraHardwarePointerImage *image,
    AstraFence *fence)
{
    if (display == 0 || image == 0 || image->size < sizeof(*image) ||
        image->pixels == 0 || image->width == 0 ||
        image->width > ASTRA_HARDWARE_POINTER_WIDTH ||
        image->height == 0 ||
        image->height > ASTRA_HARDWARE_POINTER_HEIGHT ||
        image->pitch < (uint32_t)image->width * sizeof(*image->pixels) ||
        image->hotspot.x < 0 || image->hotspot.y < 0 ||
        (uint32_t)image->hotspot.x >= image->width ||
        (uint32_t)image->hotspot.y >= image->height ||
        !astra_words_zero(image->reserved, 4) || fence == 0 ||
        !empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_display_set_pointer_state(
    const AstraDisplay *display, AstraPointI32 position, int enabled,
    AstraFence *fence)
{
    if (display == 0 || position.x < 0 || position.y < 0 ||
        position.x >= ASTRA_GRAPHICS_OUTPUT_WIDTH ||
        position.y >= ASTRA_GRAPHICS_OUTPUT_HEIGHT ||
        (enabled != 0 && enabled != 1) || fence == 0 ||
        !empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_surface_create(const AstraDisplay *display,
                                 const AstraSurfaceCreateInfo *create_info,
                                 AstraSurface *surface)
{
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};
    uint32_t row;
    AstraResult result;

    if (display == 0 || create_info == 0 ||
        create_info->size < sizeof(*create_info) ||
        create_info->width == 0 || create_info->height == 0 ||
        create_info->width > 32767u || create_info->height > 32767u ||
        !pixel_format_valid(create_info->format) ||
        !surface_usage_valid(create_info) ||
        (create_info->flags & ~SURFACE_FLAGS) != 0 ||
        create_info->flags == 0 || create_info->reserved16 != 0 ||
        !astra_words_zero(create_info->reserved, 5) || surface == 0 ||
        !empty_handle(surface->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!display_live(display))
        return ASTRA_ERROR_INVALID_HANDLE;
    row = row_bytes(create_info->format, create_info->width);
    if (row == 0u || (create_info->flags & ASTRA_SURFACE_SCANOUT) != 0u ||
        (create_info->preferred_pitch != 0u &&
         create_info->preferred_pitch != row))
        return ASTRA_ERROR_UNSUPPORTED;
    request.action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE;
    request.width = create_info->width;
    request.height = create_info->height;
    request.format = create_info->format;
    request.flags = create_info->flags;
    result = graphics_command(display->_private_handle,
                              display->_private_window, &request, 0,
                              &reply);
    if (result != ASTRA_OK)
        return result;
    if (reply.object <= ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID ||
        reply.pitch != row)
        return ASTRA_ERROR_IO;
    *surface = (AstraSurface)ASTRA_SURFACE_INIT;
    surface->_private_handle = display->_private_handle;
    surface->_private_window = display->_private_window;
    surface->_private_id = reply.object;
    surface->_private_flags = create_info->flags;
    surface->_private_pitch = row;
    surface->_private_width = create_info->width;
    surface->_private_height = create_info->height;
    surface->_private_format = create_info->format;
    return ASTRA_OK;
}

AstraResult astra_window_surface(AstraWindow *window, AstraSurface *surface)
{
    AstraWindowInfo info = ASTRA_WINDOW_INFO_INIT;
    AstraResult result;

    if (window == 0 || surface == 0 ||
        !empty_handle(surface->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_window_get_info(window, &info);
    if (result != ASTRA_OK)
        return result;
    *surface = (AstraSurface)ASTRA_SURFACE_INIT;
    surface->_private_handle = window->_private_control;
    surface->_private_window = window->_private_id;
    surface->_private_id = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID;
    surface->_private_flags = ASTRA_SURFACE_DRAW_TARGET |
                              ASTRA_SURFACE_DRAW_SOURCE |
                              ASTRA_SURFACE_CPU_READ |
                              ASTRA_SURFACE_CPU_WRITE;
    surface->_private_pitch = (uint32_t)info.frame.width * 2u;
    surface->_private_width = info.frame.width;
    surface->_private_height = info.frame.height;
    surface->_private_format = ASTRA_PIXEL_FORMAT_RGB565;
    return ASTRA_OK;
}

AstraResult astra_surface_get_info(const AstraSurface *surface,
                                   AstraSurfaceInfo *info)
{
    if (surface == 0 || info == 0 || info->size < sizeof(*info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!surface_live(surface))
        return ASTRA_ERROR_INVALID_HANDLE;
    info->flags = surface->_private_flags;
    info->width = surface->_private_width;
    info->height = surface->_private_height;
    info->format = surface->_private_format;
    info->reserved16 = 0u;
    info->pitch = surface->_private_pitch;
    for (uint32_t index = 0u; index < 5u; ++index)
        info->reserved[index] = 0u;
    return ASTRA_OK;
}

AstraResult astra_surface_close(AstraSurface *surface)
{
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};
    AstraResult result = ASTRA_OK;

    if (surface == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!surface_live(surface))
        return ASTRA_ERROR_INVALID_HANDLE;
    /* A window's content surface is borrowed; it lives with the window. */
    if (surface->_private_id != ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID) {
        request.action = ASTRA_GUI_GRAPHICS_SURFACE_DESTROY;
        request.object = surface->_private_id;
        result = graphics_command(surface->_private_handle,
                                  surface->_private_window, &request, 0,
                                  &reply);
    }
    *surface = (AstraSurface)ASTRA_SURFACE_INIT;
    return result;
}

static int surface_rect_valid(const AstraSurface *surface,
                              const AstraRectI32 *rectangle)
{
    return rect_valid(rectangle) && rectangle->x >= 0 &&
           rectangle->y >= 0 &&
           (uint32_t)rectangle->x <= surface->_private_width &&
           rectangle->width <= surface->_private_width -
                                   (uint32_t)rectangle->x &&
           (uint32_t)rectangle->y <= surface->_private_height &&
           rectangle->height <= surface->_private_height -
                                    (uint32_t)rectangle->y;
}

AstraResult astra_surface_write_staged(const AstraSurface *surface,
                                       const AstraRectI32 *rectangle,
                                       uint32_t offset, uint32_t pitch)
{
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};

    if (!surface_live(surface) || !surface_rect_valid(surface, rectangle) ||
        pitch < row_bytes(surface->_private_format, rectangle->width))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((surface->_private_flags & ASTRA_SURFACE_CPU_WRITE) == 0u)
        return ASTRA_ERROR_PERMISSION;
    request.action = ASTRA_GUI_GRAPHICS_SURFACE_WRITE;
    request.object = surface->_private_id;
    request.x = rectangle->x;
    request.y = rectangle->y;
    request.width = rectangle->width;
    request.height = rectangle->height;
    request.offset = offset;
    request.pitch = pitch;
    return graphics_command(surface->_private_handle,
                            surface->_private_window, &request, 0, &reply);
}

AstraResult astra_surface_write(AstraDisplay *display,
                                const AstraSurface *surface,
                                const AstraRectI32 *rectangle,
                                const void *pixels, uint32_t pitch)
{
    const uint8_t *source = pixels;
    uint8_t *staging = 0;
    uint32_t staging_bytes = 0u;
    uint32_t row;
    uint32_t ignored;
    AstraAreaCopy copy;
    AstraResult result;

    if (!display_live(display) || !surface_live(surface) ||
        surface->_private_window != display->_private_window ||
        !surface_rect_valid(surface, rectangle) || pixels == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    row = row_bytes(surface->_private_format, rectangle->width);
    if (pitch < row || row > ASTRA_AREA_SIZE_MAX / rectangle->height)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_display_staging(display, row * rectangle->height,
                                   (void **)&staging, &staging_bytes);
    if (result != ASTRA_OK)
        return result;
    /* The machine's copy engine packs the rows into staging; the MC68040
       copies nothing. */
    copy = (AstraAreaCopy){
        .size = ASTRA_AREA_COPY_SIZE,
        .area = display->_private_staging,
        .area_offset = 0u,
        .area_pitch = row,
        .source = (uint32_t)(uintptr_t)source,
        .source_pitch = pitch,
        .row_bytes = row,
        .rows = rectangle->height,
    };
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_AREA_COPY_IN, (uintptr_t)&copy, 0, 0, 0, 0, &ignored,
        &ignored));
    if (result != ASTRA_OK)
        return result;
    return astra_surface_write_staged(surface, rectangle, 0u, row);
}

AstraResult astra_surface_read(AstraDisplay *display,
                               const AstraSurface *surface,
                               const AstraRectI32 *rectangle, void *pixels,
                               uint32_t pitch)
{
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};
    uint8_t *target = pixels;
    uint8_t *staging = 0;
    uint32_t staging_bytes = 0u;
    uint32_t row;
    AstraResult result;

    if (!display_live(display) || !surface_live(surface) ||
        surface->_private_window != display->_private_window ||
        !surface_rect_valid(surface, rectangle) || pixels == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    row = row_bytes(surface->_private_format, rectangle->width);
    if (pitch < row || row > ASTRA_AREA_SIZE_MAX / rectangle->height)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if ((surface->_private_flags & ASTRA_SURFACE_CPU_READ) == 0u)
        return ASTRA_ERROR_PERMISSION;
    result = astra_display_staging(display, row * rectangle->height,
                                   (void **)&staging, &staging_bytes);
    if (result != ASTRA_OK)
        return result;
    request.action = ASTRA_GUI_GRAPHICS_SURFACE_READ;
    request.object = surface->_private_id;
    request.x = rectangle->x;
    request.y = rectangle->y;
    request.width = rectangle->width;
    request.height = rectangle->height;
    request.pitch = row;
    result = graphics_command(surface->_private_handle,
                              surface->_private_window, &request, 0, &reply);
    if (result != ASTRA_OK)
        return result;
    for (uint32_t y = 0u; y < rectangle->height; ++y)
        memcpy(target + (uint64_t)y * pitch, staging + y * row, row);
    return ASTRA_OK;
}

static AstraDrawListHeader *list_header(const AstraDrawList *list)
{
    return (AstraDrawListHeader *)list->_private_commands;
}

/* Payload bytes the list's area holds after its command slots. */
static uint32_t payload_capacity(const AstraDrawListHeader *header)
{
    return header->total_bytes -
           astra_draw_list_payload_offset(header->command_capacity);
}

/*
 * Attach a fresh list area with @p capacity command slots and at least
 * @p payload payload bytes, carrying over the commands and the payload.
 * Commands name payload by list offset, so theirs move with it.
 */
static AstraResult list_attach(AstraDrawList *list, uint32_t capacity,
                               uint32_t payload, uint16_t width,
                               uint16_t height)
{
    AstraArea area = ASTRA_AREA_INIT;
    AstraHandle shared = ASTRA_INVALID_HANDLE;
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};
    AstraDrawListHeader *header;
    const AstraDrawListHeader *old = list_header(list);
    uint32_t count = old != 0 ? old->command_count : 0u;
    uint32_t used = old != 0 ? old->payload_bytes : 0u;
    uint32_t offset = astra_draw_list_payload_offset(capacity);
    uint32_t attached;
    AstraResult result;

    if (capacity > (ASTRA_DRAW_LIST_SESSION_BYTES_MAX -
                    ASTRA_DRAW_LIST_HEADER_BYTES) /
                       ASTRA_DRAW_LIST_COMMAND_BYTES ||
        payload > ASTRA_DRAW_LIST_SESSION_BYTES_MAX - offset)
        return ASTRA_ERROR_NO_RESOURCES;
    result = shared_area(offset + payload, SHARE_RIGHTS, &area, &shared);
    if (result != ASTRA_OK)
        return result;
    header = (AstraDrawListHeader *)area.address;
    *header = (AstraDrawListHeader){
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_5,
        .total_bytes = area.size < ASTRA_DRAW_LIST_SESSION_BYTES_MAX ?
                       area.size : ASTRA_DRAW_LIST_SESSION_BYTES_MAX,
        .command_count = count,
        .payload_bytes = used,
        .width = width,
        .height = height,
        .command_capacity = capacity,
    };
    for (uint32_t index = 0u; index < count; ++index) {
        AstraDrawListCommand *moved =
            &((AstraDrawListCommand *)(header + 1))[index];

        *moved = ((const AstraDrawListCommand *)(old + 1))[index];
        if (moved->payload_bytes != 0u)
            moved->payload_offset = moved->payload_offset -
                astra_draw_list_payload_offset(old->command_capacity) +
                offset;
    }
    if (used != 0u)
        memcpy((uint8_t *)header + offset,
               (const uint8_t *)old +
                   astra_draw_list_payload_offset(old->command_capacity),
               used);
    request.action = ASTRA_GUI_GRAPHICS_LIST_ATTACH;
    result = graphics_command(list->_private_port, list->_private_window,
                              &request, &shared, &reply);
    close_quietly(shared);
    if (result == ASTRA_OK && reply.object == 0u)
        result = ASTRA_ERROR_IO;
    if (result != ASTRA_OK) {
        area_release(&area);
        return result;
    }
    attached = reply.object;
    if (old != 0) {
        AstraArea previous = { list->_private_handle,
                               list->_private_commands,
                               list->_private_bytes,
                               ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE };

        request = (AstraGuiGraphicsCommand){0};
        request.action = ASTRA_GUI_GRAPHICS_LIST_DETACH;
        request.object = list->_private_list;
        (void)graphics_command(list->_private_port, list->_private_window,
                               &request, 0, &reply);
        area_release(&previous);
    }
    list->_private_handle = area.handle;
    list->_private_commands = area.address;
    list->_private_bytes = area.size;
    list->_private_list = attached;
    return ASTRA_OK;
}

static int clip_to(const AstraRectI32 *clip, uint16_t width,
                   uint16_t height, AstraRectI32 *out)
{
    int64_t left = clip->x < 0 ? 0 : clip->x;
    int64_t top = clip->y < 0 ? 0 : clip->y;
    int64_t right = (int64_t)clip->x + clip->width;
    int64_t bottom = (int64_t)clip->y + clip->height;

    if (right > width)
        right = width;
    if (bottom > height)
        bottom = height;
    if (left >= right || top >= bottom)
        return 0;
    *out = (AstraRectI32){ (int32_t)left, (int32_t)top,
                           (uint32_t)(right - left),
                           (uint32_t)(bottom - top) };
    return 1;
}

AstraResult astra_draw_list_create(const AstraSurface *destination,
                                   const AstraRectI32 *clip,
                                   AstraDrawList *draw_list)
{
    AstraDrawList created = ASTRA_DRAW_LIST_INIT;
    AstraResult result;

    if (destination == 0 || !rect_valid(clip) || draw_list == 0 ||
        !empty_handle(draw_list->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!surface_live(destination))
        return ASTRA_ERROR_INVALID_HANDLE;
    if ((destination->_private_flags & ASTRA_SURFACE_DRAW_TARGET) == 0u)
        return ASTRA_ERROR_PERMISSION;
    created._private_port = destination->_private_handle;
    created._private_window = destination->_private_window;
    created._private_destination = destination->_private_id;
    if (!clip_to(clip, destination->_private_width,
                 destination->_private_height, &created._private_clip))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = list_attach(&created,
                         (LIST_INITIAL_BYTES - ASTRA_DRAW_LIST_HEADER_BYTES) /
                             ASTRA_DRAW_LIST_COMMAND_BYTES,
                         0u, destination->_private_width,
                         destination->_private_height);
    if (result == ASTRA_OK)
        *draw_list = created;
    return result;
}

AstraResult astra_draw_list_set_clip(AstraDrawList *draw_list,
                                     const AstraRectI32 *clip)
{
    AstraDrawListHeader *header;

    if (draw_list == 0 || clip == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!list_live(draw_list))
        return ASTRA_ERROR_INVALID_HANDLE;
    header = list_header(draw_list);
    if (!clip_to(clip, header->width, header->height,
                 &draw_list->_private_clip))
        /* An empty clip suppresses later commands until it is replaced. */
        draw_list->_private_clip = (AstraRectI32){ 0, 0, 0, 0 };
    return ASTRA_OK;
}

AstraResult astra_draw_list_reset(AstraDrawList *draw_list)
{
    if (draw_list == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!list_live(draw_list))
        return ASTRA_ERROR_INVALID_HANDLE;
    list_header(draw_list)->command_count = 0u;
    list_header(draw_list)->payload_bytes = 0u;
    draw_list->_private_sealed = 0u;
    return ASTRA_OK;
}

AstraResult astra_draw_list_close(AstraDrawList *draw_list)
{
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};
    AstraArea area = ASTRA_AREA_INIT;
    AstraResult result;
    AstraResult close_result;

    if (draw_list == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!list_live(draw_list))
        return ASTRA_ERROR_INVALID_HANDLE;
    request.action = ASTRA_GUI_GRAPHICS_LIST_DETACH;
    request.object = draw_list->_private_list;
    result = graphics_command(draw_list->_private_port,
                              draw_list->_private_window, &request, 0,
                              &reply);
    area.handle = draw_list->_private_handle;
    area.address = draw_list->_private_commands;
    area.size = draw_list->_private_bytes;
    area.map_flags = ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE;
    close_result = astra_area_close(&area);
    *draw_list = (AstraDrawList)ASTRA_DRAW_LIST_INIT;
    return result == ASTRA_OK ? close_result : result;
}

/* A zeroed command carrying the current clip, or NULL with *result set.
   An empty clip yields NULL with ASTRA_OK: the command draws nothing. */
static AstraDrawListCommand *append(AstraDrawList *list, uint32_t operation,
                                    AstraResult *result)
{
    AstraDrawListHeader *header;
    AstraDrawListCommand *command;
    const AstraRectI32 *clip;

    *result = ASTRA_OK;
    if (!list_live(list)) {
        *result = ASTRA_ERROR_INVALID_HANDLE;
        return 0;
    }
    if (list->_private_sealed != 0u) {
        *result = ASTRA_ERROR_BUSY;
        return 0;
    }
    clip = &list->_private_clip;
    if (clip->width == 0u || clip->height == 0u)
        return 0;
    header = list_header(list);
    if (header->command_count == header->command_capacity) {
        /* Double the command bytes; the payload keeps its capacity. */
        *result = list_attach(list, header->command_capacity * 2u + 1u,
                              payload_capacity(header), header->width,
                              header->height);
        if (*result != ASTRA_OK)
            return 0;
        header = list_header(list);
    }
    command = &((AstraDrawListCommand *)(header + 1))
                  [header->command_count++];
    *command = (AstraDrawListCommand){
        .operation = operation,
        .clip_left = (uint16_t)clip->x,
        .clip_top = (uint16_t)clip->y,
        .clip_right = (uint16_t)(clip->x + (int32_t)clip->width),
        .clip_bottom = (uint16_t)(clip->y + (int32_t)clip->height),
    };
    return command;
}

static AstraResult append_fill(AstraDrawList *list, int32_t x, int32_t y,
                               uint32_t width, uint32_t height,
                               const AstraDrawPaint *paint)
{
    AstraResult result;
    AstraDrawListCommand *command;

    if (!int16_value(x) || !int16_value(y) || width > UINT16_MAX ||
        height > UINT16_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    command = append(list, ASTRA_DRAW_LIST_FILL, &result);
    if (command != 0) {
        command->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(paint->blend);
        command->x = x;
        command->y = y;
        command->width = width;
        command->height = height;
        command->color = argb(paint->foreground);
    }
    return result;
}

AstraResult astra_draw_line(AstraDrawList *draw_list,
                            AstraPointI32 p0,
                            AstraPointI32 p1,
                            const AstraDrawPaint *paint)
{
    AstraDrawListCommand *command;
    AstraResult result;

    if (draw_list == 0 || !paint_valid(paint) || !int16_value(p0.x) ||
        !int16_value(p0.y) || !int16_value(p1.x) || !int16_value(p1.y))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    /* Lines are the line engine's: replace, or opaque source-over. SDL
       draws translucent lines as spans of blended fills. */
    if (paint->blend > ASTRA_BLEND_ALPHA ||
        (paint->blend == ASTRA_BLEND_ALPHA &&
         paint->foreground.alpha != 255u))
        return ASTRA_ERROR_UNSUPPORTED;
    command = append(draw_list, ASTRA_DRAW_LIST_LINE, &result);
    if (command != 0) {
        command->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(paint->blend);
        command->x = p0.x;
        command->y = p0.y;
        command->width = (uint32_t)p1.x;
        command->height = (uint32_t)p1.y;
        command->color = argb(paint->foreground);
    }
    return result;
}

AstraResult astra_draw_rectangle(AstraDrawList *draw_list,
                                 const AstraRectI32 *rectangle,
                                 int filled,
                                 const AstraDrawPaint *paint)
{
    AstraResult result;
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;

    if (draw_list == 0 || !rect_valid(rectangle) ||
        (filled != 0 && filled != 1) || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    x = rectangle->x;
    y = rectangle->y;
    w = rectangle->width;
    h = rectangle->height;
    if (filled || w <= 2u || h <= 2u)
        return append_fill(draw_list, x, y, w, h, paint);
    /* Four non-overlapping edges, so a blended outline blends once. */
    result = append_fill(draw_list, x, y, w, 1u, paint);
    if (result == ASTRA_OK)
        result = append_fill(draw_list, x, y + (int32_t)h - 1, w, 1u, paint);
    if (result == ASTRA_OK)
        result = append_fill(draw_list, x, y + 1, 1u, h - 2u, paint);
    if (result == ASTRA_OK)
        result = append_fill(draw_list, x + (int32_t)w - 1, y + 1, 1u,
                             h - 2u, paint);
    return result;
}

AstraResult astra_draw_blit(AstraDrawList *draw_list,
                            const AstraSurface *source,
                            const AstraRectI32 *source_rect,
                            const AstraRectI32 *destination_rect,
                            const AstraBlitOptions *options)
{
    AstraBlitOptions plain = ASTRA_BLIT_OPTIONS_INIT;
    AstraDrawListCommand *command;
    AstraResult result;

    if (options == 0)
        options = &plain;
    if (draw_list == 0 || source == 0 || !rect_valid(source_rect) ||
        !rect_valid(destination_rect) || options->size < sizeof(*options) ||
        (options->flags & ~BLIT_FLAGS) != 0u ||
        options->blend > ASTRA_BLEND_MULTIPLY ||
        !astra_words_zero(options->reserved, 3) ||
        !int16_value(destination_rect->x) ||
        !int16_value(destination_rect->y))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!surface_live(source) ||
        source->_private_window != draw_list->_private_window)
        return ASTRA_ERROR_INVALID_HANDLE;
    if (!surface_rect_valid(source, source_rect))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    command = append(draw_list, ASTRA_DRAW_LIST_BLIT, &result);
    if (command != 0) {
        command->flags =
            ((options->flags & ASTRA_BLIT_FLIP_X) != 0u ?
                 ASTRA_DRAW_LIST_FLIP_X : 0u) |
            ((options->flags & ASTRA_BLIT_FLIP_Y) != 0u ?
                 ASTRA_DRAW_LIST_FLIP_Y : 0u) |
            ((options->flags & ASTRA_BLIT_FILTER_LINEAR) != 0u ?
                 ASTRA_DRAW_LIST_FILTER_LINEAR : 0u) |
            ASTRA_DRAW_LIST_BLEND_FLAGS(options->blend);
        command->x = destination_rect->x;
        command->y = destination_rect->y;
        command->width = destination_rect->width;
        command->height = destination_rect->height;
        command->color = argb(options->modulate);
        command->source =
            source->_private_id == draw_list->_private_destination ?
                ASTRA_DRAW_LIST_SOURCE_DESTINATION : source->_private_id;
        command->source_x = (int16_t)source_rect->x;
        command->source_y = (int16_t)source_rect->y;
        command->source_width = (uint16_t)source_rect->width;
        command->source_height = (uint16_t)source_rect->height;
    }
    return result;
}

/* Reserve @p bytes of list payload, growing the area when needed; returns
   the payload's list offset, or zero with *result set. */
static uint32_t append_payload(AstraDrawList *list, uint32_t bytes,
                               AstraResult *result)
{
    AstraDrawListHeader *header = list_header(list);
    uint32_t capacity = payload_capacity(header);
    uint32_t offset;

    *result = ASTRA_OK;
    /* Every payload starts word aligned, whatever came before it. */
    header->payload_bytes = (header->payload_bytes + 3u) & ~3u;
    if (bytes > ASTRA_DRAW_LIST_SESSION_BYTES_MAX - header->payload_bytes) {
        *result = ASTRA_ERROR_NO_RESOURCES;
        return 0u;
    }
    if (header->payload_bytes + bytes > capacity) {
        uint32_t grown = capacity * 2u;

        if (grown < header->payload_bytes + bytes)
            grown = header->payload_bytes + bytes;
        *result = list_attach(list, header->command_capacity, grown,
                              header->width, header->height);
        if (*result != ASTRA_OK)
            return 0u;
        header = list_header(list);
    }
    offset = astra_draw_list_payload_offset(header->command_capacity) +
             header->payload_bytes;
    header->payload_bytes += bytes;
    return offset;
}

AstraResult astra_draw_triangles(AstraDrawList *draw_list,
                                 const AstraSurface *texture,
                                 const AstraVertex *vertices,
                                 uint32_t vertex_count, uint32_t blend,
                                 uint32_t flags)
{
    AstraDrawListCommand *command;
    AstraDrawListVertex *out;
    uint32_t index;
    uint32_t offset;
    AstraResult result;

    if (draw_list == 0 || vertices == 0 || vertex_count == 0u ||
        vertex_count % 3u != 0u ||
        vertex_count > ASTRA_DRAW_LIST_SESSION_BYTES_MAX /
                           sizeof(AstraDrawListVertex) ||
        blend > ASTRA_BLEND_MULTIPLY ||
        (flags & ~(uint32_t)ASTRA_BLIT_FILTER_LINEAR) != 0u ||
        (texture == 0 && flags != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!list_live(draw_list))
        return ASTRA_ERROR_INVALID_HANDLE;
    if (texture != 0) {
        if (!surface_live(texture) ||
            texture->_private_window != draw_list->_private_window)
            return ASTRA_ERROR_INVALID_HANDLE;
        if (texture->_private_id == draw_list->_private_destination)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (texture->_private_format != ASTRA_PIXEL_FORMAT_RGB565 &&
            texture->_private_format != ASTRA_PIXEL_FORMAT_XRGB8888 &&
            texture->_private_format != ASTRA_PIXEL_FORMAT_ARGB8888)
            return ASTRA_ERROR_UNSUPPORTED;
    }
    for (index = 0u; index < vertex_count; ++index)
        if (vertices[index].x < -VERTEX_COORD_LIMIT ||
            vertices[index].x >= VERTEX_COORD_LIMIT ||
            vertices[index].y < -VERTEX_COORD_LIMIT ||
            vertices[index].y >= VERTEX_COORD_LIMIT ||
            (texture == 0 && (vertices[index].u != 0 ||
                              vertices[index].v != 0)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    command = append(draw_list, ASTRA_DRAW_LIST_TRIANGLES, &result);
    if (command == 0)
        return result;
    /* The payload may move the list; find the command again by index. */
    index = list_header(draw_list)->command_count - 1u;
    offset = append_payload(draw_list,
                            vertex_count * (uint32_t)sizeof(*out), &result);
    if (result != ASTRA_OK) {
        --list_header(draw_list)->command_count;
        return result;
    }
    command = &((AstraDrawListCommand *)(list_header(draw_list) + 1))[index];
    command->flags = ASTRA_DRAW_LIST_BLEND_FLAGS(blend) |
                     ((flags & ASTRA_BLIT_FILTER_LINEAR) != 0u ?
                          ASTRA_DRAW_LIST_FILTER_LINEAR : 0u);
    command->source = texture != 0 ? texture->_private_id : 0u;
    command->payload_offset = offset;
    command->payload_bytes = vertex_count * (uint32_t)sizeof(*out);
    out = (AstraDrawListVertex *)(void *)
              ((uint8_t *)list_header(draw_list) + offset);
    for (index = 0u; index < vertex_count; ++index)
        out[index] = (AstraDrawListVertex){
            vertices[index].x, vertices[index].y, vertices[index].u,
            vertices[index].v, argb(vertices[index].color) };
    return ASTRA_OK;
}

/*
 * Append a command of @p operation followed by @p bytes of payload and
 * return the payload, or NULL: with *result set when that failed, with
 * ASTRA_OK when the clip is empty and nothing draws. The payload may move
 * the list, so the command is named by *index.
 */
static uint8_t *append_array(AstraDrawList *list, uint32_t operation,
                             uint32_t bytes, uint32_t *index,
                             uint32_t *offset, AstraResult *result)
{
    if (append(list, operation, result) == 0)
        return 0;
    *index = list_header(list)->command_count - 1u;
    *offset = append_payload(list, bytes, result);
    if (*result != ASTRA_OK) {
        --list_header(list)->command_count;
        return 0;
    }
    return (uint8_t *)list_header(list) + *offset;
}

/* Keep the first @p used payload bytes of the array command at @p index,
   the last in the list, or drop it with its payload when @p used is 0. */
static void finish_array(AstraDrawList *list, uint32_t index,
                         uint32_t offset, uint32_t used, uint32_t flags,
                         uint32_t color)
{
    AstraDrawListHeader *header = list_header(list);
    AstraDrawListCommand *command =
        &((AstraDrawListCommand *)(header + 1))[index];

    header->payload_bytes = offset + used -
        astra_draw_list_payload_offset(header->command_capacity);
    if (used == 0u) {
        --header->command_count;
        return;
    }
    command->flags = flags;
    command->color = color;
    command->payload_offset = offset;
    command->payload_bytes = used;
}

static int int16_point(AstraPointI32 point)
{
    return (uint32_t)point.x + 32768u <= UINT16_MAX &&
           (uint32_t)point.y + 32768u <= UINT16_MAX;
}

AstraResult astra_draw_rectangles(AstraDrawList *draw_list,
                                  const AstraRectI32 *rectangles,
                                  uint32_t count,
                                  const AstraDrawPaint *paint)
{
    AstraDrawListRect *out;
    uint32_t index = 0u;
    uint32_t offset = 0u;
    uint32_t kept = 0u;
    AstraResult result;

    if (draw_list == 0 || (rectangles == 0 && count != 0u) ||
        count > ASTRA_DRAW_LIST_SESSION_BYTES_MAX / sizeof(*out) ||
        !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (count == 0u)
        return list_live(draw_list) ? ASTRA_OK : ASTRA_ERROR_INVALID_HANDLE;
    /* With an empty clip out is NULL and the rectangles are only checked. */
    out = (AstraDrawListRect *)(void *)append_array(
        draw_list, ASTRA_DRAW_LIST_FILL_RECTS, count * (uint32_t)sizeof(*out),
        &index, &offset, &result);
    if (result != ASTRA_OK)
        return result;
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraRectI32 *rectangle = &rectangles[at];

        /* ADLT carries a 16-bit origin and extent; zero draws nothing. */
        if (!int16_point((AstraPointI32){ rectangle->x, rectangle->y }) ||
            (rectangle->width | rectangle->height) > UINT16_MAX) {
            if (out != 0)
                finish_array(draw_list, index, offset, 0u, 0u, 0u);
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        if (out == 0 || rectangle->width == 0u || rectangle->height == 0u)
            continue;
        out[kept++] = (AstraDrawListRect){
            (int16_t)rectangle->x, (int16_t)rectangle->y,
            (uint16_t)rectangle->width, (uint16_t)rectangle->height };
    }
    if (out != 0)
        finish_array(draw_list, index, offset,
                     kept * (uint32_t)sizeof(*out),
                     ASTRA_DRAW_LIST_BLEND_FLAGS(paint->blend),
                     argb(paint->foreground));
    return ASTRA_OK;
}

AstraResult astra_draw_lines(AstraDrawList *draw_list,
                             const AstraPointI32 *endpoints,
                             uint32_t segment_count,
                             const AstraDrawPaint *paint)
{
    AstraDrawListSegment *out;
    uint32_t index = 0u;
    uint32_t offset = 0u;
    AstraResult result;

    if (draw_list == 0 || (endpoints == 0 && segment_count != 0u) ||
        segment_count > ASTRA_DRAW_LIST_SESSION_BYTES_MAX / sizeof(*out) ||
        !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    /* The line engine replaces, or blends only as opaque, as for
       astra_draw_line. */
    if (paint->blend > ASTRA_BLEND_ALPHA ||
        (paint->blend == ASTRA_BLEND_ALPHA &&
         paint->foreground.alpha != 255u))
        return ASTRA_ERROR_UNSUPPORTED;
    if (segment_count == 0u)
        return list_live(draw_list) ? ASTRA_OK : ASTRA_ERROR_INVALID_HANDLE;
    out = (AstraDrawListSegment *)(void *)append_array(
        draw_list, ASTRA_DRAW_LIST_LINES,
        segment_count * (uint32_t)sizeof(*out), &index, &offset, &result);
    if (result != ASTRA_OK)
        return result;
    for (uint32_t at = 0u; at < segment_count; ++at) {
        AstraPointI32 p0 = endpoints[at * 2u];
        AstraPointI32 p1 = endpoints[at * 2u + 1u];

        if (!int16_point(p0) || !int16_point(p1)) {
            if (out != 0)
                finish_array(draw_list, index, offset, 0u, 0u, 0u);
            return ASTRA_ERROR_INVALID_ARGUMENT;
        }
        if (out != 0)
            out[at] = (AstraDrawListSegment){ (int16_t)p0.x, (int16_t)p0.y,
                                              (int16_t)p1.x, (int16_t)p1.y };
    }
    if (out != 0)
        finish_array(draw_list, index, offset,
                     segment_count * (uint32_t)sizeof(*out),
                     ASTRA_DRAW_LIST_BLEND_FLAGS(paint->blend),
                     argb(paint->foreground));
    return ASTRA_OK;
}

AstraResult astra_draw_circle(AstraDrawList *draw_list,
                              AstraPointI32 center,
                              uint32_t radius,
                              int filled,
                              const AstraDrawPaint *paint)
{
    (void)center;
    if (draw_list == 0 || radius > 32767u ||
        (filled != 0 && filled != 1) || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    /* The hardware has CIRCLE; ADLT does not carry it yet. */
    return list_live(draw_list) ? ASTRA_ERROR_UNSUPPORTED :
                                  ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_ellipse(AstraDrawList *draw_list,
                               AstraPointI32 center,
                               uint32_t radius_x,
                               uint32_t radius_y,
                               int filled,
                               const AstraDrawPaint *paint)
{
    (void)center;
    if (draw_list == 0 || radius_x > 32767u || radius_y > 32767u ||
        (filled != 0 && filled != 1) || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return list_live(draw_list) ? ASTRA_ERROR_UNSUPPORTED :
                                  ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_pattern_fill(AstraDrawList *draw_list,
                                    const AstraRectI32 *rectangle,
                                    const AstraPattern8 *pattern,
                                    const AstraDrawPaint *paint)
{
    if (draw_list == 0 || !rect_valid(rectangle) || pattern == 0 ||
        !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return list_live(draw_list) ? ASTRA_ERROR_UNSUPPORTED :
                                  ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_flood_fill(AstraDrawList *draw_list,
                                  AstraPointI32 seed,
                                  const AstraDrawPaint *paint)
{
    (void)seed;
    if (draw_list == 0 || !paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return list_live(draw_list) ? ASTRA_ERROR_UNSUPPORTED :
                                  ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_text_layout(AstraDrawList *draw_list,
                                   const AstraTextLayout *layout,
                                   AstraPointI32 origin,
                                   const AstraTextPaint *paint)
{
    (void)origin;
    if (draw_list == 0 || layout == 0 || !text_paint_valid(paint))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return list_live(draw_list) ? ASTRA_ERROR_UNSUPPORTED :
                                  ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_draw_ui_text(AstraDrawList *draw_list, AstraPointI32 origin,
                               const char *utf8, uint32_t utf8_bytes,
                               uint32_t pixel_height, uint32_t style_flags,
                               AstraColorRGBA8 color)
{
    AstraDrawListCommand *command;
    uint32_t index;
    uint32_t offset;
    AstraResult result;

    if (draw_list == 0 || utf8 == 0 || utf8_bytes == 0u ||
        utf8_bytes > 4096u || pixel_height == 0u ||
        pixel_height > UINT16_MAX ||
        (style_flags & ~(uint32_t)ASTRA_TEXT_RENDER_STYLE_MASK) != 0u ||
        color.alpha != 255u || !int16_value(origin.x) ||
        !int16_value(origin.y) ||
        !astra_utf8_validate(utf8, utf8_bytes, 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    command = append(draw_list, ASTRA_DRAW_LIST_TEXT, &result);
    if (command == 0)
        return result;
    /* The payload may move the list; find the command again by index. */
    index = list_header(draw_list)->command_count - 1u;
    offset = append_payload(draw_list, utf8_bytes, &result);
    if (result != ASTRA_OK) {
        --list_header(draw_list)->command_count;
        return result;
    }
    command = &((AstraDrawListCommand *)(list_header(draw_list) + 1))[index];
    command->flags = style_flags;
    command->x = origin.x;
    command->y = origin.y;
    command->color = argb(color);
    command->font_height = (uint16_t)pixel_height;
    command->payload_offset = offset;
    command->payload_bytes = utf8_bytes;
    memcpy((uint8_t *)list_header(draw_list) + offset, utf8, utf8_bytes);
    return ASTRA_OK;
}

AstraResult astra_draw_submit(AstraDrawList *draw_list, AstraFence *fence)
{
    AstraGuiGraphicsCommand request = {0};
    AstraGuiGraphicsReply reply = {0};
    uint32_t event = ASTRA_INVALID_HANDLE;
    uint32_t ignored;
    AstraResult result;

    if (draw_list == 0 || fence == 0 ||
        !empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!list_live(draw_list))
        return ASTRA_ERROR_INVALID_HANDLE;
    if (draw_list->_private_sealed != 0u)
        return ASTRA_ERROR_BUSY;
    request.action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT;
    request.object = draw_list->_private_list;
    request.target = draw_list->_private_destination;
    result = graphics_command(draw_list->_private_port,
                              draw_list->_private_window, &request, 0,
                              &reply);
    if (result != ASTRA_OK)
        return result;
    /* ponytail: the service replies after the hardware completes, so the
       fence is born signaled; make it pending when submission pipelines. */
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_EVENT_CREATE,
        ASTRA_EVENT_MANUAL_RESET | ASTRA_EVENT_INITIALLY_SIGNALED,
        ASTRA_RIGHT_WAIT, 0, 0, 0, &event, &ignored));
    if (result != ASTRA_OK)
        return result;
    draw_list->_private_sealed = 1u;
    fence->_private_handle = event;
    return ASTRA_OK;
}

AstraResult astra_palette_create(const AstraDisplay *display,
                                 const AstraColorRGBA8 *entries,
                                 uint32_t entry_count,
                                 AstraPalette *palette)
{
    uint32_t index;

    if (display == 0 || entries == 0 || entry_count == 0 ||
        entry_count > 256u || palette == 0 ||
        !empty_handle(palette->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (index = 0; index < entry_count; ++index) {
        if (entries[index].alpha != 255u)
            return ASTRA_ERROR_UNSUPPORTED;
    }
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_palette_update(AstraPalette *palette,
                                 uint32_t first_entry,
                                 const AstraColorRGBA8 *entries,
                                 uint32_t entry_count)
{
    uint32_t index;

    if (palette == 0 || entries == 0 || entry_count == 0 ||
        first_entry >= 256u || entry_count > 256u - first_entry)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (index = 0; index < entry_count; ++index) {
        if (entries[index].alpha != 255u)
            return ASTRA_ERROR_UNSUPPORTED;
    }
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_palette_close(AstraPalette *palette)
{
    if (palette == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_sprite_set_create(const AstraDisplay *display,
                                    AstraSpriteSet *sprite_set)
{
    if (display == 0 || sprite_set == 0 ||
        !empty_handle(sprite_set->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_sprite_set_update(AstraSpriteSet *sprite_set,
                                    uint32_t index,
                                    const AstraSpriteUpdate *update)
{
    if (sprite_set == 0 || index >= ASTRA_GRAPHICS_SPRITE_COUNT)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (update != 0 &&
        (update->size < sizeof(*update) || update->source == 0 ||
         !sprite_source_rect_valid(&update->source_rect) ||
         update->destination.x < -32768 ||
         update->destination.x > 32767 ||
         update->destination.y < -32768 ||
         update->destination.y > 32767 ||
         update->destination_width == 0u ||
         update->destination_width > ASTRA_SPRITE_DESTINATION_EXTENT_MAX ||
         update->destination_height == 0u ||
         update->destination_height > ASTRA_SPRITE_DESTINATION_EXTENT_MAX ||
         (update->flags & ~SPRITE_FLAGS) != 0 ||
         update->palette_bank >= ASTRA_SPRITE_PALETTE_BANK_COUNT ||
         !astra_words_zero(update->reserved, 2)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_sprite_set_close(AstraSpriteSet *sprite_set)
{
    if (sprite_set == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_raster_program_create(const AstraDisplay *display,
                                        const AstraRasterChange *changes,
                                        uint32_t change_count,
                                        AstraRasterProgram *program)
{
    uint32_t index;

    if (display == 0 || changes == 0 || change_count == 0 ||
        change_count > 2047u || program == 0 ||
        !empty_handle(program->_private_handle))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (index = 0; index < change_count; ++index) {
        if (changes[index].target < ASTRA_RASTER_TARGET_BACKDROP ||
            changes[index].target > ASTRA_RASTER_TARGET_FRAMEBUFFER_BASE ||
            changes[index].beam_x >= ASTRA_GRAPHICS_OUTPUT_WIDTH ||
            changes[index].beam_y >= ASTRA_GRAPHICS_OUTPUT_HEIGHT)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (index != 0 &&
            (changes[index].beam_y < changes[index - 1].beam_y ||
             (changes[index].beam_y == changes[index - 1].beam_y &&
              changes[index].beam_x < changes[index - 1].beam_x)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_raster_program_close(AstraRasterProgram *program)
{
    if (program == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_display_present_surface(const AstraDisplay *display,
                                          const AstraSurface *surface,
                                          const AstraPresentOptions *options,
                                          AstraFence *fence)
{
    if (display == 0 || surface == 0 || fence == 0 ||
        !empty_handle(fence->_private_handle) ||
        (options != 0 &&
         (options->size < sizeof(*options) || options->flags != 0 ||
          !astra_words_zero(options->reserved, 4))))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_display_get_status(const AstraDisplay *display,
                                     AstraDisplayStatus *status)
{
    if (display == 0 || status == 0 || status->size < sizeof(*status))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_fence_poll(const AstraFence *fence,
                             int *signaled,
                             AstraResult *completion_result)
{
    uint32_t ignored_d1;
    uint32_t ignored_d2;
    AstraResult result;

    if (fence == 0 || signaled == 0 || completion_result == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_HANDLE;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_WAIT_ONE, fence->_private_handle, 0, 0, 0, 0,
        &ignored_d1, &ignored_d2));
    *signaled = result == ASTRA_OK;
    *completion_result = ASTRA_OK;
    return result == ASTRA_OK || result == ASTRA_ERROR_TIMEOUT ||
           result == ASTRA_ERROR_WOULD_BLOCK ? ASTRA_OK : result;
}

AstraResult astra_fence_wait(const AstraFence *fence,
                             uint32_t timeout_ms,
                             AstraResult *completion_result)
{
    uint64_t deadline = timeout_ms == 0u ? 0u :
                        (uint64_t)ASTRA_DEADLINE_INFINITE;
    uint32_t ignored_d1;
    uint32_t ignored_d2;
    AstraResult result;

    if (fence == 0 || completion_result == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_HANDLE;
    /* Fences are signaled when astra_draw_submit returns, so any nonzero
       timeout is satisfied at once. */
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_WAIT_ONE, fence->_private_handle,
        (uint32_t)(deadline >> 32), (uint32_t)deadline, 0, 0,
        &ignored_d1, &ignored_d2));
    if (result == ASTRA_ERROR_WOULD_BLOCK)
        result = ASTRA_ERROR_TIMEOUT;
    if (result == ASTRA_OK)
        *completion_result = ASTRA_OK;
    return result;
}

AstraResult astra_fence_close(AstraFence *fence)
{
    if (fence == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (empty_handle(fence->_private_handle))
        return ASTRA_ERROR_INVALID_HANDLE;
    return astra_handle_close(&fence->_private_handle);
}

#define DEFINE_CLEANUP(function_name, type_name, close_function) \
    void function_name(type_name *value) \
    { \
        if (value != 0 && value->_private_handle != ASTRA_INVALID_HANDLE) { \
            AstraResult ignored = close_function(value); \
            (void)ignored; \
        } \
    }

DEFINE_CLEANUP(astra_display_cleanup, AstraDisplay, astra_display_close)
DEFINE_CLEANUP(astra_surface_cleanup, AstraSurface, astra_surface_close)
DEFINE_CLEANUP(astra_draw_list_cleanup, AstraDrawList, astra_draw_list_close)
DEFINE_CLEANUP(astra_palette_cleanup, AstraPalette, astra_palette_close)
DEFINE_CLEANUP(astra_sprite_set_cleanup, AstraSpriteSet, astra_sprite_set_close)
DEFINE_CLEANUP(astra_raster_program_cleanup, AstraRasterProgram,
               astra_raster_program_close)
DEFINE_CLEANUP(astra_fence_cleanup, AstraFence, astra_fence_close)
