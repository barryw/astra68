#include "window_graphics.h"

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/draw_list.h>
#include <astra/graphics.h>
#include <astra/render_batch.h>
#include <astra/runtime.h>
#include <astra/status.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <astra_render_protocol.h>
#pragma GCC diagnostic pop

enum {
    /* Every batch starts with rings and descriptors; leave them room. */
    UPLOAD_BAND_BYTES = ASTRA_RENDER_BATCH_MAX_BYTES -
                        (ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
                         ASTRA_RENDER_BATCH_ARENA_OFFSET) - 4096u,
    SUPPORTED_SURFACE_FLAGS = ASTRA_SURFACE_DRAW_TARGET |
                              ASTRA_SURFACE_DRAW_SOURCE |
                              ASTRA_SURFACE_CPU_READ |
                              ASTRA_SURFACE_CPU_WRITE,
    /* A readback returns its rows after the request header, in the same
       buffer the batches use. */
    READ_BAND_BYTES = ASTRA_RENDER_BATCH_MAX_BYTES -
                      ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES,
};

static uint32_t status_from_syscall(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_OK: return ASTRA_STATUS_OK;
    case ASTRA_SYSCALL_INVALID_HANDLE: return ASTRA_STATUS_BAD_HANDLE;
    case ASTRA_SYSCALL_ACCESS_DENIED: return ASTRA_STATUS_ACCESS;
    case ASTRA_SYSCALL_RESOURCE_LIMIT:
    case ASTRA_SYSCALL_OUT_OF_MEMORY: return ASTRA_STATUS_LIMIT;
    default: return ASTRA_STATUS_INVALID;
    }
}

static uint8_t render_format(uint32_t pixel_format)
{
    switch (pixel_format) {
    case ASTRA_PIXEL_FORMAT_INDEX8: return ASTRA_RENDER_FORMAT_INDEX8;
    case ASTRA_PIXEL_FORMAT_RGB565: return ASTRA_RENDER_FORMAT_RGB565;
    case ASTRA_PIXEL_FORMAT_XRGB8888: return ASTRA_RENDER_FORMAT_XRGB8888;
    case ASTRA_PIXEL_FORMAT_ARGB8888: return ASTRA_RENDER_FORMAT_ARGB8888;
    default: return UINT8_MAX;
    }
}

static uint32_t area_map(uint32_t area, uint32_t flags, void **mapping,
                         uint32_t *bytes)
{
    void *address = NULL;
    uint32_t status = astra_rt_area_map(area, flags, &address, bytes);

    *mapping = address;
    return status_from_syscall(status);
}

uint32_t display_window_graphics_open(DisplayWindowGraphics **graphics,
                                      uint32_t staging_area)
{
    DisplayWindowGraphics *created = NULL;
    uint32_t self_area = 0u;
    uint32_t bytes = 0u;
    uint32_t status;

    if (graphics == NULL || *graphics != NULL || staging_area == 0u)
        return ASTRA_STATUS_INVALID;
    status = status_from_syscall(astra_rt_area_create(
        sizeof(*created),
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP, &self_area));
    if (status == ASTRA_STATUS_OK)
        status = status_from_syscall(astra_rt_area_map(
            self_area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
            (void **)&created, &bytes));
    if (status == ASTRA_STATUS_OK && bytes < sizeof(*created))
        status = ASTRA_STATUS_LIMIT;
    if (status == ASTRA_STATUS_OK) {
        *created = (DisplayWindowGraphics){ .self_area = self_area };
        status = area_map(staging_area,
                          ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          (void **)&created->staging,
                          &created->staging_bytes);
    }
    if (status != ASTRA_STATUS_OK) {
        if (created != NULL)
            (void)astra_rt_area_unmap(created);
        if (self_area != 0u)
            (void)astra_close(self_area);
        return status;
    }
    created->staging_area = staging_area;
    *graphics = created;
    return ASTRA_STATUS_OK;
}

static void list_release(DisplayGraphicsList *list)
{
    if (list->mapping != NULL)
        (void)astra_rt_area_unmap((void *)(uintptr_t)list->mapping);
    if (list->area != 0u)
        (void)astra_close(list->area);
    *list = (DisplayGraphicsList){0};
}

void display_window_graphics_close(DisplayWindowGraphics *graphics)
{
    uint32_t self_area;

    if (graphics == NULL)
        return;
    for (uint32_t index = 0u; index < DISPLAY_GRAPHICS_LIST_MAX; ++index)
        list_release(&graphics->lists[index]);
    if (graphics->staging != NULL)
        (void)astra_rt_area_unmap(graphics->staging);
    if (graphics->staging_area != 0u)
        (void)astra_close(graphics->staging_area);
    if (graphics->surfaces != NULL)
        (void)astra_rt_area_unmap(graphics->surfaces);
    if (graphics->surfaces_area != 0u)
        (void)astra_close(graphics->surfaces_area);
    self_area = graphics->self_area;
    (void)astra_rt_area_unmap(graphics);
    (void)astra_close(self_area);
}

int display_window_graphics_command_valid(
    const AstraGuiGraphicsCommand *command, uint32_t size,
    uint32_t handle_count, uint32_t window)
{
    uint32_t attachment =
        command->action == ASTRA_GUI_GRAPHICS_STAGING_SET ||
        command->action == ASTRA_GUI_GRAPHICS_LIST_ATTACH;

    return size == sizeof(*command) &&
           handle_count == 1u + attachment &&
           command->header.total_size == sizeof(*command) &&
           command->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           command->header.flags == 0u &&
           command->header.protocol == ASTRA_GUI_PROTOCOL &&
           command->header.protocol_version == ASTRA_GUI_VERSION &&
           command->header.reserved == 0u &&
           command->header.operation == ASTRA_GUI_GRAPHICS_COMMAND &&
           command->header.transaction_id != 0u &&
           command->window == window && command->generation != 0u &&
           command->action >= ASTRA_GUI_GRAPHICS_SURFACE_CREATE &&
           command->action <= ASTRA_GUI_GRAPHICS_SURFACE_READ &&
           astra_words_zero(command->reserved, 2u);
}

static DisplayGraphicsSurface *find_surface(DisplayWindowGraphics *graphics,
                                            uint32_t id)
{
    for (uint32_t index = 0u; index < graphics->surface_count; ++index)
        if (graphics->surfaces[index].id == id)
            return &graphics->surfaces[index];
    return NULL;
}

/* The window's own content is surface 1: an RGB565 draw target and source
   the CPU may write and read back. */
static int content_surface(const DisplayGraphicsHost *host,
                           DisplayGraphicsSurface *surface)
{
    if (host->content_offset == 0u)
        return 0;
    *surface = (DisplayGraphicsSurface){
        .id = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID,
        .offset = host->content_offset,
        .bytes = host->content_bytes,
        .pitch = host->content_pitch,
        .width = host->content_width,
        .height = host->content_height,
        .format = ASTRA_RENDER_FORMAT_RGB565,
        .flags = ASTRA_SURFACE_DRAW_TARGET | ASTRA_SURFACE_DRAW_SOURCE |
                 ASTRA_SURFACE_CPU_READ | ASTRA_SURFACE_CPU_WRITE,
    };
    return 1;
}

static int lookup_surface(DisplayWindowGraphics *graphics,
                          const DisplayGraphicsHost *host, uint32_t id,
                          DisplayGraphicsSurface *surface)
{
    const DisplayGraphicsSurface *found;

    if (id == ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID)
        return content_surface(host, surface);
    found = find_surface(graphics, id);
    if (found == NULL)
        return 0;
    *surface = *found;
    return 1;
}

static uint32_t descriptor(AstraRenderBuilder *builder,
                           const DisplayGraphicsSurface *surface,
                           uint8_t format, uint8_t access)
{
    return astra_render_builder_surface_format_at(
        builder, surface->offset, surface->bytes, surface->width,
        surface->height, surface->pitch, format, access);
}

static int builder_begin(AstraRenderBuilder *builder,
                         const DisplayGraphicsHost *host)
{
    static uint32_t generation;

    /* Render-only batches use their own nonzero generation space, apart
       from the compositor's alternating frame fences. */
    if (++generation == 0u)
        generation = 1u;
    return astra_render_builder_init(builder, host->batch_storage,
                                     ASTRA_RENDER_BUILDER_BYTES, generation);
}

static uint32_t builder_submit(AstraRenderBuilder *builder,
                               const DisplayGraphicsHost *host,
                               const DisplayGraphicsAttachment *attachment)
{
    uint32_t bytes = astra_render_builder_finish_render_only(builder);

    return bytes == 0u ? ASTRA_STATUS_LIMIT :
                         host->submit(host->context, bytes, attachment);
}

static uint32_t surface_create(DisplayWindowGraphics *graphics,
                               const DisplayGraphicsHost *host,
                               const AstraGuiGraphicsCommand *command,
                               AstraGuiGraphicsReply *reply)
{
    uint8_t format = render_format(command->format);
    uint32_t row = astra_render_format_row_bytes(format, command->width);
    DisplayGraphicsSurface surface;
    AstraRenderBuilder builder;
    uint32_t target;
    uint32_t bytes;
    uint32_t status;

    if (format == UINT8_MAX || command->width == 0u ||
        command->height == 0u ||
        command->width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        command->height > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        command->x != 0 || command->y != 0 || command->object != 0u ||
        command->target != 0u || command->offset != 0u ||
        command->pitch != 0u || command->flags == 0u)
        return ASTRA_STATUS_INVALID;
    if ((command->flags & ~SUPPORTED_SURFACE_FLAGS) != 0u)
        return ASTRA_STATUS_UNSUPPORTED;
    bytes = row * command->height;
    if (graphics->surface_count >= DISPLAY_GRAPHICS_SURFACE_MAX ||
        bytes > DISPLAY_GRAPHICS_BYTES_MAX - graphics->surface_bytes)
        return ASTRA_STATUS_LIMIT;
    if (graphics->surface_count == graphics->surface_capacity) {
        uint32_t capacity = graphics->surface_capacity == 0u ? 64u :
                            graphics->surface_capacity * 2u;
        uint32_t area = 0u;
        uint32_t mapped = 0u;
        DisplayGraphicsSurface *grown = NULL;

        status = status_from_syscall(astra_rt_area_create(
            capacity * (uint32_t)sizeof(*grown),
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP, &area));
        if (status == ASTRA_STATUS_OK)
            status = status_from_syscall(astra_rt_area_map(
                area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                (void **)&grown, &mapped));
        if (status != ASTRA_STATUS_OK) {
            if (area != 0u)
                (void)astra_close(area);
            return status;
        }
        for (uint32_t index = 0u; index < graphics->surface_count; ++index)
            grown[index] = graphics->surfaces[index];
        if (graphics->surfaces != NULL)
            (void)astra_rt_area_unmap(graphics->surfaces);
        if (graphics->surfaces_area != 0u)
            (void)astra_close(graphics->surfaces_area);
        graphics->surfaces = grown;
        graphics->surfaces_area = area;
        graphics->surface_capacity = mapped / (uint32_t)sizeof(*grown);
    }
    do {
        if (++graphics->next_surface <= ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID)
            graphics->next_surface = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID + 1u;
    } while (find_surface(graphics, graphics->next_surface) != NULL);
    surface = (DisplayGraphicsSurface){
        .id = graphics->next_surface,
        .offset = host->allocate(host->context, bytes),
        .bytes = bytes,
        .pitch = row,
        .width = (uint16_t)command->width,
        .height = (uint16_t)command->height,
        .format = format,
        .flags = (uint16_t)command->flags,
    };
    if (surface.offset == 0u)
        return ASTRA_STATUS_LIMIT;
    /* Media RAM is reused across clients: clear it before anyone can read
       it, ARGB8888 to transparent black. */
    if (!builder_begin(&builder, host))
        return ASTRA_STATUS_INVALID;
    target = descriptor(&builder, &surface, format,
                        ASTRA_RENDER_SURFACE_READ |
                            ASTRA_RENDER_SURFACE_WRITE);
    if (target == 0u ||
        !astra_render_builder_fill(&builder, target, 0, 0, surface.width,
                                   surface.height, 0u))
        return ASTRA_STATUS_LIMIT;
    status = builder_submit(&builder, host, NULL);
    if (status != ASTRA_STATUS_OK)
        return status;
    graphics->surfaces[graphics->surface_count++] = surface;
    graphics->surface_bytes += bytes;
    reply->object = surface.id;
    reply->pitch = surface.pitch;
    return ASTRA_STATUS_OK;
}

static uint32_t surface_destroy(DisplayWindowGraphics *graphics,
                                const AstraGuiGraphicsCommand *command)
{
    DisplayGraphicsSurface *surface = find_surface(graphics, command->object);

    if (surface == NULL)
        return ASTRA_STATUS_NOT_FOUND;
    graphics->surface_bytes -= surface->bytes;
    *surface = graphics->surfaces[--graphics->surface_count];
    graphics->surfaces[graphics->surface_count] =
        (DisplayGraphicsSurface){0};
    return ASTRA_STATUS_OK;
}

/* The surface rectangle and staging rows a SURFACE_WRITE or SURFACE_READ
   names; returns the row bytes, or zero when either is out of range. */
static uint32_t transfer_rows(const DisplayWindowGraphics *graphics,
                              const DisplayGraphicsSurface *surface,
                              const AstraGuiGraphicsCommand *command)
{
    uint32_t row = astra_render_format_row_bytes(surface->format,
                                                 command->width);
    uint64_t end;

    if (command->x < 0 || command->y < 0 || command->width == 0u ||
        command->height == 0u || command->target != 0u ||
        command->format != 0u || command->flags != 0u ||
        (uint32_t)command->x > surface->width ||
        command->width > surface->width - (uint32_t)command->x ||
        (uint32_t)command->y > surface->height ||
        command->height > surface->height - (uint32_t)command->y ||
        command->pitch < row || row == 0u)
        return 0u;
    end = (uint64_t)command->offset +
          (uint64_t)command->pitch * (command->height - 1u) + row;
    return end > graphics->staging_bytes ? 0u : row;
}

static uint32_t surface_write(DisplayWindowGraphics *graphics,
                              const DisplayGraphicsHost *host,
                              const AstraGuiGraphicsCommand *command)
{
    DisplayGraphicsSurface surface;
    uint32_t row;
    uint32_t band;

    if (!lookup_surface(graphics, host, command->object, &surface))
        return ASTRA_STATUS_NOT_FOUND;
    /* The content surface is CPU-writable: it is how a CPU-drawn
       framebuffer reaches the window. */
    if ((surface.flags & ASTRA_SURFACE_CPU_WRITE) == 0u)
        return ASTRA_STATUS_ACCESS;
    row = transfer_rows(graphics, &surface, command);
    if (row == 0u)
        return ASTRA_STATUS_INVALID;
    /* The device reads the rows straight from the staging area: each band
       is its batch's attachment, and neither this service nor the MC68040
       copies a pixel. */
    band = UPLOAD_BAND_BYTES / command->pitch;
    if (band == 0u)
        return ASTRA_STATUS_INVALID;
    for (uint32_t done = 0u; done < command->height;) {
        uint32_t rows = command->height - done < band ?
                        command->height - done : band;
        AstraRenderBuilder builder;
        DisplayGraphicsAttachment attachment = {
            .area = graphics->staging_area,
            .offset = command->offset + command->pitch * done,
            .bytes = command->pitch * (rows - 1u) + row,
        };
        uint32_t target;
        uint32_t source;
        uint32_t status;

        if (!builder_begin(&builder, host))
            return ASTRA_STATUS_INVALID;
        target = descriptor(&builder, &surface, surface.format,
                            ASTRA_RENDER_SURFACE_READ |
                                ASTRA_RENDER_SURFACE_WRITE);
        source = astra_render_builder_upload_reserve(
            &builder, command->pitch, (uint16_t)command->width,
            (uint16_t)rows, surface.format, &attachment.target);
        if (target == 0u || source == 0u ||
            !astra_render_builder_blit_region(
                &builder, target, source, 0, 0, command->x,
                command->y + (int32_t)done, (uint16_t)command->width,
                (uint16_t)rows))
            return ASTRA_STATUS_LIMIT;
        status = builder_submit(&builder, host, &attachment);
        if (status != ASTRA_STATUS_OK)
            return status;
        done += rows;
    }
    return ASTRA_STATUS_OK;
}

/* Rows of a surface, read back from Media RAM by the display helper,
   written into the client's staging area (docs/TEXTURE_ENGINE.md 8). */
static uint32_t surface_read(DisplayWindowGraphics *graphics,
                             const DisplayGraphicsHost *host,
                             const AstraGuiGraphicsCommand *command)
{
    DisplayGraphicsSurface surface;
    uint32_t row;
    uint32_t band;

    if (!lookup_surface(graphics, host, command->object, &surface))
        return ASTRA_STATUS_NOT_FOUND;
    if ((surface.flags & ASTRA_SURFACE_CPU_READ) == 0u)
        return ASTRA_STATUS_ACCESS;
    row = transfer_rows(graphics, &surface, command);
    if (row == 0u)
        return ASTRA_STATUS_INVALID;
    band = READ_BAND_BYTES / row;
    for (uint32_t done = 0u; done < command->height;) {
        uint32_t rows = command->height - done < band ?
                        command->height - done : band;
        AstraDisplaySurfaceRead *request = host->batch_storage;
        const uint8_t *pixels = (const uint8_t *)host->batch_storage +
                                ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES;
        uint32_t status;

        *request = (AstraDisplaySurfaceRead){
            .magic = ASTRA_DISPLAY_SURFACE_READ_MAGIC,
            .version = ASTRA_DISPLAY_SURFACE_READ_VERSION,
            .data_offset = surface.offset,
            .data_bytes = surface.bytes,
            .pitch = surface.pitch,
            .width = surface.width,
            .height = surface.height,
            .format = surface.format,
            .x = (uint16_t)command->x,
            .y = (uint16_t)((uint32_t)command->y + done),
            .read_width = (uint16_t)command->width,
            .read_height = (uint16_t)rows,
        };
        status = host->read(host->context,
                            ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES +
                                rows * row);
        if (status != ASTRA_STATUS_OK)
            return status;
        for (uint32_t line = 0u; line < rows; ++line)
            memcpy(graphics->staging + command->offset +
                       (uint64_t)command->pitch * (done + line),
                   pixels + line * row, row);
        done += rows;
    }
    return ASTRA_STATUS_OK;
}

static DisplayGraphicsList *find_list(DisplayWindowGraphics *graphics,
                                      uint32_t id)
{
    for (uint32_t index = 0u; index < DISPLAY_GRAPHICS_LIST_MAX; ++index)
        if (id != 0u && graphics->lists[index].id == id)
            return &graphics->lists[index];
    return NULL;
}

static uint32_t list_attach(DisplayWindowGraphics *graphics,
                            uint32_t *handles,
                            AstraGuiGraphicsReply *reply)
{
    DisplayGraphicsList *slot = NULL;
    uint32_t status;

    for (uint32_t index = 0u; slot == NULL &&
                              index < DISPLAY_GRAPHICS_LIST_MAX; ++index)
        if (graphics->lists[index].id == 0u)
            slot = &graphics->lists[index];
    if (slot == NULL)
        return ASTRA_STATUS_LIMIT;
    status = area_map(handles[1], ASTRA_AREA_MAP_READ,
                      (void **)(uintptr_t)&slot->mapping, &slot->bytes);
    if (status != ASTRA_STATUS_OK) {
        *slot = (DisplayGraphicsList){0};
        return status;
    }
    if (slot->bytes < ASTRA_DRAW_LIST_HEADER_BYTES) {
        list_release(slot);
        return ASTRA_STATUS_INVALID;
    }
    do {
        if (++graphics->next_list == 0u)
            graphics->next_list = 1u;
    } while (find_list(graphics, graphics->next_list) != NULL);
    slot->id = graphics->next_list;
    slot->area = handles[1];
    handles[1] = 0u;
    reply->object = slot->id;
    return ASTRA_STATUS_OK;
}

typedef struct ResolveContext {
    DisplayWindowGraphics *graphics;
    const DisplayGraphicsHost *host;
} ResolveContext;

static uint32_t resolve_source(void *context, AstraRenderBuilder *builder,
                               uint32_t id)
{
    ResolveContext *resolve = context;
    DisplayGraphicsSurface surface;

    if (!lookup_surface(resolve->graphics, resolve->host, id, &surface) ||
        (surface.flags & ASTRA_SURFACE_DRAW_SOURCE) == 0u)
        return 0u;
    return descriptor(builder, &surface, surface.format,
                      ASTRA_RENDER_SURFACE_READ);
}

static uint32_t list_submit(DisplayWindowGraphics *graphics,
                            const DisplayGraphicsHost *host,
                            const AstraGuiGraphicsCommand *command)
{
    DisplayGraphicsList *list = find_list(graphics, command->object);
    ResolveContext context = { graphics, host };
    AstraRenderSourceResolver resolver = { resolve_source, &context };
    DisplayGraphicsSurface target;
    uint32_t next = 0u;
    int result = ASTRA_RENDER_REPLAY_FULL;

    if (list == NULL)
        return ASTRA_STATUS_NOT_FOUND;
    if (!lookup_surface(graphics, host, command->target, &target))
        return ASTRA_STATUS_NOT_FOUND;
    if ((target.flags & ASTRA_SURFACE_DRAW_TARGET) == 0u)
        return ASTRA_STATUS_ACCESS;
    while (result == ASTRA_RENDER_REPLAY_FULL) {
        AstraRenderBuilder builder;
        uint32_t destination;
        uint32_t status;

        if (!builder_begin(&builder, host))
            return ASTRA_STATUS_INVALID;
        destination = descriptor(&builder, &target, target.format,
                                 ASTRA_RENDER_SURFACE_READ |
                                     ASTRA_RENDER_SURFACE_WRITE);
        if (destination == 0u)
            return ASTRA_STATUS_LIMIT;
        result = astra_render_builder_replay_range(
            &builder, destination, list->mapping, list->bytes, &resolver,
            next, &next);
        if (result == ASTRA_RENDER_REPLAY_INVALID)
            return ASTRA_STATUS_INVALID;
        if (builder.command_count == 0u)
            continue;
        status = builder_submit(&builder, host, NULL);
        if (status != ASTRA_STATUS_OK)
            return status;
    }
    return ASTRA_STATUS_OK;
}

uint32_t display_window_graphics_command(
    DisplayWindowGraphics *graphics, const DisplayGraphicsHost *host,
    const AstraGuiGraphicsCommand *command, uint32_t *handles,
    AstraGuiGraphicsReply *reply)
{
    if (graphics == NULL || host == NULL || command == NULL ||
        handles == NULL || reply == NULL)
        return ASTRA_STATUS_INVALID;
    switch (command->action) {
    case ASTRA_GUI_GRAPHICS_SURFACE_CREATE:
        return surface_create(graphics, host, command, reply);
    case ASTRA_GUI_GRAPHICS_SURFACE_DESTROY:
        return surface_destroy(graphics, command);
    case ASTRA_GUI_GRAPHICS_SURFACE_WRITE:
        return surface_write(graphics, host, command);
    case ASTRA_GUI_GRAPHICS_STAGING_SET: {
        uint8_t *staging = NULL;
        uint32_t bytes = 0u;
        uint32_t status = area_map(handles[1],
                                   ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                                   (void **)&staging, &bytes);

        if (status != ASTRA_STATUS_OK)
            return status;
        (void)astra_rt_area_unmap(graphics->staging);
        (void)astra_close(graphics->staging_area);
        graphics->staging = staging;
        graphics->staging_bytes = bytes;
        graphics->staging_area = handles[1];
        handles[1] = 0u;
        return ASTRA_STATUS_OK;
    }
    case ASTRA_GUI_GRAPHICS_LIST_ATTACH:
        return list_attach(graphics, handles, reply);
    case ASTRA_GUI_GRAPHICS_LIST_DETACH: {
        DisplayGraphicsList *list = find_list(graphics, command->object);

        if (list == NULL)
            return ASTRA_STATUS_NOT_FOUND;
        list_release(list);
        return ASTRA_STATUS_OK;
    }
    case ASTRA_GUI_GRAPHICS_LIST_SUBMIT:
        return list_submit(graphics, host, command);
    case ASTRA_GUI_GRAPHICS_SURFACE_READ:
        return surface_read(graphics, host, command);
    default:
        return ASTRA_STATUS_INVALID;
    }
}
