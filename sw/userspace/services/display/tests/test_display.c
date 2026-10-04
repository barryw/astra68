#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/gui.h>
#include <astra/input_service.h>
#include <astra/display_mailbox.h>
#include <astra/render_batch.h>
#include <astra/runtime.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <astra_render_protocol.h>
#pragma GCC diagnostic pop

static AstraGuiWindowEvent delivered;
static AstraGuiWindowEvent previous_delivered;
static uint32_t delivered_count;
static uint32_t send_would_block;
static uint32_t wait_count;
static uint32_t wait_poll_status = ASTRA_SYSCALL_OK;
static AstraInputEventMessage input_messages[4];
static uint32_t input_message_count;
static uint32_t input_message_index;
static uint32_t signal_count;
static uint32_t last_signal;

#define TEST_AREA_COUNT 8u
#define TEST_AREA_BYTES UINT32_C(65536)

static _Alignas(8) uint8_t test_areas[TEST_AREA_COUNT][TEST_AREA_BYTES];
static uint32_t test_area_sizes[TEST_AREA_COUNT];
static uint32_t test_area_live[TEST_AREA_COUNT];
static uint32_t test_area_creates;
static uint32_t test_area_closes;

uint32_t astra_rt_area_create(uint32_t byte_size, uint32_t rights,
                              uint32_t *handle)
{
    uint32_t rounded = (byte_size + UINT32_C(4095)) & ~UINT32_C(4095);

    assert(byte_size != 0u && rounded <= TEST_AREA_BYTES &&
           rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                      ASTRA_RIGHT_MAP) && handle != NULL);
    for (uint32_t index = 0u; index < TEST_AREA_COUNT; ++index)
        if (test_area_live[index] == 0u) {
            memset(test_areas[index], 0, rounded);
            test_area_sizes[index] = rounded;
            test_area_live[index] = 1u;
            *handle = 0x800u + index;
            ++test_area_creates;
            return ASTRA_SYSCALL_OK;
        }
    return ASTRA_SYSCALL_OUT_OF_MEMORY;
}

uint32_t astra_rt_area_map(uint32_t handle, uint32_t permissions,
                           void **address, uint32_t *byte_size)
{
    uint32_t index = handle - 0x800u;

    assert(index < TEST_AREA_COUNT && test_area_live[index] != 0u &&
           (permissions == ASTRA_AREA_MAP_READ ||
            permissions == (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE)) &&
           address != NULL && byte_size != NULL);
    *address = test_areas[index];
    *byte_size = test_area_sizes[index];
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_unmap(void *address)
{
    for (uint32_t index = 0u; index < TEST_AREA_COUNT; ++index)
        if (address == test_areas[index])
            return ASTRA_SYSCALL_OK;
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}

uint32_t astra_close(uint32_t handle)
{
    uint32_t index = handle - 0x800u;

    assert(index < TEST_AREA_COUNT && test_area_live[index] != 0u);
    test_area_live[index] = 0u;
    ++test_area_closes;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_signal(uint32_t handle, uint32_t count, uint32_t *woken)
{
    assert(handle >= 0x700u && count == 1u && woken == NULL);
    last_signal = handle;
    ++signal_count;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                            uint32_t *handles, uint32_t handle_capacity,
                            uint32_t *size, uint32_t *handle_count)
{
    assert(handle == 0x600u && message != NULL &&
           capacity == sizeof(AstraInputEventMessage) && handles == NULL &&
           handle_capacity == 0u && size != NULL && handle_count != NULL);
    if (input_message_index == input_message_count)
        return ASTRA_SYSCALL_WOULD_BLOCK;
    memcpy(message, &input_messages[input_message_index++],
           sizeof(AstraInputEventMessage));
    *size = sizeof(AstraInputEventMessage);
    *handle_count = 0u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
                         const uint32_t *handles, uint32_t handle_count)
{
    (void)handles;
    assert(handle == 0x500u && size == sizeof(delivered) &&
           handle_count == 0u);
    if (send_would_block != 0u) {
        --send_would_block;
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    previous_delivered = delivered;
    memcpy(&delivered, message, sizeof(delivered));
    ++delivered_count;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_wait_one(uint32_t handle, uint64_t deadline_ns,
                        uint32_t *flags)
{
    assert(handle == 0x500u && flags == NULL);
    if (deadline_ns == 0u)
        return wait_poll_status;
    assert(deadline_ns == ASTRA_DEADLINE_FOREVER);
    ++wait_count;
    return ASTRA_SYSCALL_OK;
}

/* The device's answer to the request in flight (collect_request). */
static AstraDisplayFrameCompletion collected;
static uint32_t refusals_logged;

uint32_t astra_display_collect(uint32_t device,
                               AstraDisplayFrameCompletion *completion)
{
    assert(device == 0x600u);
    *completion = collected;
    return ASTRA_SYSCALL_OK;
}

/* Posted cursor writes (update_cursor): never a request. */
static uint32_t cursor_posts;
static uint32_t cursor_post_x;
static uint32_t cursor_post_y;
static uint32_t cursor_post_flags;

uint32_t astra_display_cursor(uint32_t device, uint32_t x, uint32_t y,
                              uint32_t flags)
{
    assert(device == 0x600u);
    cursor_post_x = x;
    cursor_post_y = y;
    cursor_post_flags = flags;
    ++cursor_posts;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_irq_read(uint32_t handle, AstraIrqRecord *record,
                        uint32_t *events)
{
    (void)handle;
    (void)record;
    (void)events;
    return ASTRA_SYSCALL_WOULD_BLOCK;
}

uint32_t astra_irq_ack(uint32_t handle, uint32_t sequence)
{
    (void)handle;
    (void)sequence;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_log(const char *text)
{
    (void)text;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation;
    (void)status;
    ++refusals_logged;
    return ASTRA_SYSCALL_OK;
}

#define astra_main astra_display_service_main
#include "../main.c"
#undef astra_main

#define TEST_WINDOW_COUNT 16u

static uint8_t lists[TEST_WINDOW_COUNT][ASTRA_DRAW_LIST_AREA_BYTES];
static uint8_t batch[ASTRA_RENDER_BUILDER_BYTES];

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint32_t valid_batch(uint32_t bytes)
{
    assert(bytes >= ASTRA_RENDER_BATCH_MIN_BYTES);
    assert(bytes <= ASTRA_RENDER_BUILDER_BYTES);
    assert(read_be32(batch + 8u) == bytes);
    return bytes;
}

static const uint8_t *batch_scene(void)
{
    uint32_t offset = read_be32(batch + 48u);

    assert(read_be32(batch + 4u) == ASTRA_RENDER_BATCH_VERSION_1_3);
    assert(offset >= ASTRA_RENDER_BATCH_ARENA_OFFSET);
    return batch + offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
}

static uint32_t batch_scene_layer_source(uint32_t index)
{
    const uint8_t *scene = batch_scene();
    const uint8_t *layer;

    assert(index < read_be32(scene + 20u));
    layer = scene + read_be32(scene + 24u) +
            index * ASTRA_WINDOW_SCENE_LAYER_BYTES;
    return read_be32(layer);
}

static int batch_has_surface_fill(uint16_t width, uint16_t height,
                                  uint16_t value)
{
    uint32_t count = read_be32(batch + 12u);
    uint32_t commands = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;

    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *command = batch + commands +
                                 index * ASTRA_RENDER_COMMAND_BYTES;
        uint32_t descriptor = read_be32(command + 32u);
        uint32_t descriptor_offset;

        if ((read_be32(command + 4u) >> 16) != ASTRA_RENDER_OP_FILL ||
            descriptor < ASTRA_RENDER_BATCH_ARENA_OFFSET)
            continue;
        descriptor_offset = descriptor - ASTRA_RENDER_BATCH_ARENA_OFFSET;
        if (descriptor_offset + ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES >
            sizeof(batch))
            continue;
        if (read_be32(batch + descriptor_offset + 20u) ==
                ((uint32_t)width << 16 | height) &&
            read_be32(command + 48u) == 0u &&
            read_be32(command + 56u) ==
                ((uint32_t)width << 16 | height) &&
            (uint16_t)read_be32(command + 60u) == value)
            return 1;
    }
    return 0;
}

static int batch_has_fill(int16_t x, int16_t y, uint16_t width,
                          uint16_t height, uint16_t value)
{
    uint32_t count = read_be32(batch + 12u);
    uint32_t commands = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;

    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *command = batch + commands +
                                 index * ASTRA_RENDER_COMMAND_BYTES;

        if ((read_be32(command + 4u) >> 16) == ASTRA_RENDER_OP_FILL &&
            read_be32(command + 48u) ==
                ((uint32_t)(uint16_t)x << 16 | (uint16_t)y) &&
            read_be32(command + 56u) ==
                ((uint32_t)width << 16 | height) &&
            (uint16_t)read_be32(command + 60u) == value)
            return 1;
    }
    return 0;
}

static int batch_has_colored_fill_at(int16_t x, int16_t y, uint16_t value)
{
    uint32_t count = read_be32(batch + 12u);
    uint32_t commands = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;

    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *command = batch + commands +
                                 index * ASTRA_RENDER_COMMAND_BYTES;
        uint32_t origin = read_be32(command + 48u);
        uint32_t extent = read_be32(command + 56u);
        int32_t left = (int16_t)(origin >> 16);
        int32_t top = (int16_t)origin;

        if ((read_be32(command + 4u) >> 16) == ASTRA_RENDER_OP_FILL &&
            (uint16_t)read_be32(command + 60u) == value &&
            x >= left && y >= top &&
            (uint32_t)(x - left) < (extent >> 16) &&
            (uint32_t)(y - top) < (extent & UINT16_MAX))
            return 1;
    }
    return 0;
}

static uint32_t batch_blit_count(void)
{
    uint32_t count = read_be32(batch + 12u);
    uint32_t commands = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;
    uint32_t found = 0u;

    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *command = batch + commands +
                                 index * ASTRA_RENDER_COMMAND_BYTES;

        if ((read_be32(command + 4u) >> 16) == ASTRA_RENDER_OP_BLIT)
            ++found;
    }
    return found;
}

static uint32_t batch_blit_from_surface(uint32_t data_offset)
{
    uint32_t count = read_be32(batch + 12u);
    uint32_t commands = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;
    uint32_t found = 0u;

    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *command = batch + commands +
                                 index * ASTRA_RENDER_COMMAND_BYTES;

        uint32_t source = read_be32(command + 36u);
        uint32_t descriptor_offset;

        if ((read_be32(command + 4u) >> 16) != ASTRA_RENDER_OP_BLIT ||
            source < ASTRA_RENDER_BATCH_ARENA_OFFSET)
            continue;
        descriptor_offset = source - ASTRA_RENDER_BATCH_ARENA_OFFSET;
        if (descriptor_offset + ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES <=
                sizeof(batch) &&
            read_be32(batch + descriptor_offset + 8u) == data_offset)
            ++found;
    }
    return found;
}

static void label(AstraSurfaceView *surface, int32_t x, int32_t y,
                  const char *text, uint32_t length, uint16_t color)
{
    astra_surface_ui_text(surface, x, y, text, length,
                          ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT, color);
}

static void paint_gallery(AstraSurfaceView *surface, uint8_t type,
                          const AstraTheme *theme)
{
    uint16_t client = color(theme->client);
    uint16_t primary = color(theme->title_active);
    uint16_t muted = color(theme->text_muted);
    uint16_t accent = color(theme->accent);

    astra_surface_clear(surface, client);
    label(surface, 18, 18, "GALLERY STATE", 13u, primary);
    astra_surface_fill(surface, 18, 36, surface->width - 36u, 2u, accent);
    if (type == ASTRA_WINDOW_STANDARD) {
        astra_surface_fill_round(surface, 18, 58, 238u, 92u,
                                 theme->card_radius, 0xffffu);
        astra_surface_fill_round(surface, 276, 58, 256u, 92u,
                                 theme->card_radius, 0xffffu);
        label(surface, 34, 76, "NORMAL", 6u, muted);
        label(surface, 292, 76, "HOVER / PRESSED", 15u, muted);
        astra_surface_fill_round(surface, 18, 174, 118u, 34u,
                                 theme->control_radius, accent);
        astra_surface_fill_round(surface, 148, 174, 118u, 34u,
                                 theme->control_radius,
                                 color(theme->control));
        label(surface, 36, 186, "PRIMARY", 7u, 0xffffu);
        label(surface, 164, 186, "SECONDARY", 9u, primary);
    } else if (type == ASTRA_WINDOW_UTILITY) {
        astra_surface_fill_round(surface, 18, 58, surface->width - 36u, 48u,
                                 theme->control_radius, 0xffffu);
        label(surface, 34, 77, "COMPACT TOOL WINDOW", 19u, muted);
    } else if (type == ASTRA_WINDOW_DIALOG) {
        label(surface, 18, 62, "CHANGES ARE READY TO SAVE.", 26u, muted);
        astra_surface_fill_round(surface, 128, 116, 112u, 34u,
                                 theme->control_radius, accent);
        astra_surface_fill_round(surface, 252, 116, 112u, 34u,
                                 theme->control_radius,
                                 color(theme->control));
        label(surface, 164, 128, "SAVE", 4u, 0xffffu);
        label(surface, 280, 128, "CANCEL", 6u, primary);
    } else {
        label(surface, 18, 58, "NO TITLEBAR", 11u, primary);
        label(surface, 18, 78, "CONTEXTUAL CHROME", 17u, muted);
    }
}

static void add_window(DisplayState *state, uint32_t index, uint8_t type,
                       uint16_t x, uint16_t y, uint16_t width,
                       uint16_t height, uint32_t flags, uint32_t gadgets)
{
    DisplayWindow *window = &state->windows[index];

    assert(astra_draw_list_view_init(&window->surface.view, lists[index],
                                     sizeof(lists[index]), width, height));
    {
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

        paint_gallery(&window->surface.view, type, &theme);
    }
    window->request.x = x;
    window->request.y = y;
    window->request.width = width;
    window->request.height = height;
    window->request.flags = flags;
    window->request.gadgets = gadgets;
    window->request.event_mask = ASTRA_WINDOW_SUBSCRIBE_ALL;
    window->request.type = type;
    window->request.title_length = 4u;
    window->request.title[0] = 'T';
    window->request.title[1] = 'E';
    window->request.title[2] = 'S';
    window->request.title[3] = 'T';
    window->id = index + 1u;
    window->generation = 1u;
    assert(display_window_media_prepare(state, window) == ASTRA_STATUS_OK);
    init_content_banks(window);
    dirty_cache(window);
    reset_content(window);
    window->event_send = 0x500u;
    window->vblank_signal = 0x700u + index;
    ++state->count;
}

static void test_dynamic_window_resources(void)
{
    DisplayState state = {0};
    DisplayWindow *before;
    uint32_t first_capacity;
    uint32_t first_content;
    uint32_t first_cache[2];

    assert(display_windows_reserve(&state, 1u) == ASTRA_STATUS_OK);
    assert(state.windows != NULL && state.capacity > 4u &&
           state.windows_area != 0u);
    first_capacity = state.capacity;
    before = state.windows;
    state.count = first_capacity;
    state.windows[0].id = 0x1234u;
    assert(display_windows_reserve(&state, first_capacity + 1u) ==
           ASTRA_STATUS_OK);
    assert(state.capacity > first_capacity && state.windows != before &&
           state.windows[0].id == 0x1234u && test_area_closes == 1u);
    state.count = 0u;

    for (uint32_t index = 0u; index < 5u; ++index) {
        DisplayWindow candidate = {0};

        candidate.request.type = ASTRA_WINDOW_STANDARD;
        candidate.request.width = 100u + (uint16_t)index;
        candidate.request.height = 80u + (uint16_t)index;
        assert(display_window_media_prepare(&state, &candidate) ==
               ASTRA_STATUS_OK);
        state.windows[state.count++] = candidate;
    }
    assert(state.count == 5u);
    /* Every content bank and cache bank of every window is disjoint. */
    for (uint32_t left = 0u; left < state.count * 5u; ++left)
        for (uint32_t right = left + 1u; right < state.count * 5u;
             ++right) {
            const DisplayWindow *a = &state.windows[left / 5u];
            const DisplayWindow *b = &state.windows[right / 5u];
            uint32_t ab = left % 5u;
            uint32_t bb = right % 5u;

            assert(!media_extents_overlap(
                ab < 3u ? a->content_offset[ab] : a->cache_offset[ab - 3u],
                ab < 3u ? a->content_bytes : a->cache_bytes,
                bb < 3u ? b->content_offset[bb] : b->cache_offset[bb - 3u],
                bb < 3u ? b->content_bytes : b->cache_bytes));
        }
    first_content = state.windows[0].content_offset[0];
    first_cache[0] = state.windows[0].cache_offset[0];
    first_cache[1] = state.windows[0].cache_offset[1];
    for (uint32_t index = 0u; index + 1u < state.count; ++index)
        state.windows[index] = state.windows[index + 1u];
    --state.count;
    {
        DisplayWindow candidate = {0};

        candidate.request.type = ASTRA_WINDOW_STANDARD;
        candidate.request.width = 100u;
        candidate.request.height = 80u;
        assert(display_window_media_prepare(&state, &candidate) ==
               ASTRA_STATUS_OK);
        assert(candidate.content_offset[0] == first_content &&
               candidate.cache_offset[0] == first_cache[0] &&
               candidate.cache_offset[1] == first_cache[1]);
    }
    {
        DisplayWindow occupied = {
            .content_offset = {DISPLAY_MEDIA_BASE},
            .content_bytes = DISPLAY_MEDIA_LIMIT - DISPLAY_MEDIA_BASE,
        };
        DisplayWindow candidate = {0};
        DisplayState full = {
            .windows = &occupied,
            .capacity = 1u,
            .count = 1u,
        };

        candidate.request.type = ASTRA_WINDOW_POPOVER;
        candidate.request.width = 1u;
        candidate.request.height = 1u;
        assert(display_window_media_prepare(&full, &candidate) ==
               ASTRA_STATUS_LIMIT);
    }
    assert(astra_rt_area_unmap(state.windows) == ASTRA_SYSCALL_OK);
    assert(astra_close(state.windows_area) == ASTRA_SYSCALL_OK);
    state = (DisplayState){0};
    assert(state.windows == NULL && state.capacity == 0u &&
           state.windows_area == 0u && test_area_creates == 2u &&
           test_area_closes == 2u);
}


/* Window graphics: a recording host stands in for the display device. */
#define GRAPHICS_BATCH_MAX 8u
static uint8_t graphics_batches[GRAPHICS_BATCH_MAX][262144];
static uint32_t graphics_batch_bytes[GRAPHICS_BATCH_MAX];
static uint32_t graphics_batch_count;
static uint32_t graphics_submit_status = ASTRA_STATUS_OK;
static uint8_t big_staging[4096u * 1100u * 4u];

static uint32_t record_submit(void *context, uint32_t bytes,
                              const DisplayGraphicsAttachment *attachment)
{
    const DisplayState *state = context;

    /* What the device does with an attachment: the staged bytes land at
       their target, the batch's last defined bytes. */
    if (attachment != NULL) {
        const DisplayWindowGraphics *graphics = state->windows[0].graphics;

        assert(attachment->area == graphics->staging_area &&
               attachment->bytes != 0u &&
               attachment->target >= ASTRA_RENDER_BATCH_MIN_BYTES &&
               attachment->target <= bytes &&
               attachment->bytes <= bytes - attachment->target &&
               bytes - attachment->target - attachment->bytes < 64u * 1024u &&
               attachment->offset <= graphics->staging_bytes &&
               attachment->bytes <=
                   graphics->staging_bytes - attachment->offset);
        memcpy(batch + attachment->target,
               graphics->staging + attachment->offset, attachment->bytes);
    }
    assert(read_be32(batch + 4u) == ASTRA_RENDER_BATCH_VERSION_1_4 &&
           read_be32(batch + 8u) == bytes && read_be32(batch + 32u) == 0u &&
           read_be32(batch + 48u) == 0u);
    if (graphics_batch_count < GRAPHICS_BATCH_MAX) {
        uint32_t kept = bytes < sizeof(graphics_batches[0]) ?
                        bytes : (uint32_t)sizeof(graphics_batches[0]);

        memcpy(graphics_batches[graphics_batch_count], batch, kept);
        graphics_batch_bytes[graphics_batch_count] = bytes;
    }
    ++graphics_batch_count;
    return graphics_submit_status;
}

/* The recording device answers a READ_SURFACE by writing, after the
   header, byte (row * 31 + column) of each read row, where row and column
   count from the surface origin in bytes. */
static AstraDisplaySurfaceRead read_requests[4];
static uint32_t read_request_bytes[4];
static uint32_t read_count;
static uint32_t read_status = ASTRA_STATUS_OK;

static uint32_t record_read(void *context, uint32_t bytes)
{
    AstraDisplaySurfaceRead request;
    uint32_t row;

    (void)context;
    memcpy(&request, batch, sizeof(request));
    row = astra_render_format_row_bytes((uint8_t)request.format,
                                        request.read_width);
    assert(request.magic == ASTRA_DISPLAY_SURFACE_READ_MAGIC &&
           request.version == ASTRA_DISPLAY_SURFACE_READ_VERSION &&
           bytes == ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES +
                        row * request.read_height &&
           bytes <= ASTRA_RENDER_BUILDER_BYTES);
    for (uint32_t line = 0u; line < request.read_height; ++line)
        for (uint32_t at = 0u; at < row; ++at)
            batch[ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES + line * row +
                  at] = (uint8_t)((request.y + line) * 31u +
                                  request.x * (row / request.read_width) +
                                  at);
    if (read_count < 4u) {
        read_requests[read_count] = request;
        read_request_bytes[read_count] = bytes;
    }
    ++read_count;
    return read_status;
}

/* A failure the storage hook reports: a render-only batch the service left
   running failed, and the next command that needs the buffer returns it. */
static uint32_t storage_failure;

static uint32_t record_storage(void *context, void **storage)
{
    (void)context;
    *storage = batch;
    return storage_failure;
}

static uint32_t record_allocate(void *context, uint32_t bytes)
{
    return display_media_allocate(context, NULL, UINT32_MAX, bytes);
}

static const uint8_t *recorded_command(uint32_t batch_index, uint32_t index)
{
    return graphics_batches[batch_index] +
           ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
           ASTRA_RENDER_BATCH_ARENA_OFFSET +
           index * ASTRA_RENDER_COMMAND_BYTES;
}

static const uint8_t *recorded_record(uint32_t batch_index, uint32_t offset)
{
    assert(offset >= ASTRA_RENDER_BATCH_ARENA_OFFSET &&
           offset - ASTRA_RENDER_BATCH_ARENA_OFFSET + 32u <=
               sizeof(graphics_batches[0]));
    return graphics_batches[batch_index] + offset -
           ASTRA_RENDER_BATCH_ARENA_OFFSET;
}

static uint32_t graphics_run(DisplayWindowGraphics *graphics,
                             const DisplayGraphicsHost *host,
                             AstraGuiGraphicsCommand command,
                             uint32_t attachment,
                             AstraGuiGraphicsReply *reply)
{
    uint32_t handles[2] = {0x900u, attachment};

    command.header = (AstraMessageHeader){0};
    astra_message_header_set(&command.header, sizeof(command),
                             ASTRA_GUI_PROTOCOL, ASTRA_GUI_VERSION,
                             ASTRA_GUI_GRAPHICS_COMMAND, 1u);
    command.window = 42u;
    command.generation = 1u;
    assert(display_window_graphics_command_valid(
        &command, sizeof(command), attachment != 0u ? 2u : 1u, 42u));
    *reply = (AstraGuiGraphicsReply){0};
    graphics_batch_count = 0u;
    return display_window_graphics_command(graphics, host, &command, handles,
                                           reply);
}

/* ARGB8888 targets start transparent black and keep a fill's alpha. */
static void test_window_graphics_argb_target(DisplayWindowGraphics *graphics,
                                             const DisplayGraphicsHost *host)
{
    AstraGuiGraphicsReply reply;
    uint32_t target;
    uint32_t list_area = 0u;
    uint32_t mapped = 0u;
    uint32_t list_id;
    AstraDrawListHeader *list;
    const uint8_t *command;

    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 16u, .height = 8u,
               .format = ASTRA_PIXEL_FORMAT_ARGB8888,
               .flags = ASTRA_SURFACE_DRAW_TARGET |
                        ASTRA_SURFACE_DRAW_SOURCE}, 0u, &reply) ==
           ASTRA_STATUS_OK && graphics_batch_count == 1u);
    target = reply.object;
    command = recorded_command(0u, 0u);
    assert(read_be32(command + 4u) >> 16 == ASTRA_RENDER_OP_FILL &&
           read_be32(command + 60u) == 0u &&
           read_be32(recorded_record(0u, read_be32(command + 32u)) + 24u) >>
                   24 == ASTRA_RENDER_FORMAT_ARGB8888);
    assert(astra_rt_area_create(8192u, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                ASTRA_RIGHT_MAP, &list_area) ==
           ASTRA_SYSCALL_OK);
    assert(astra_rt_area_map(list_area, ASTRA_AREA_MAP_READ |
                             ASTRA_AREA_MAP_WRITE, (void **)&list,
                             &mapped) == ASTRA_SYSCALL_OK);
    *list = (AstraDrawListHeader){
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_5,
        .total_bytes = 8192u,
        .command_count = 1u,
        .width = 16u,
        .height = 8u,
        .command_capacity = 16u,
    };
    *(AstraDrawListCommand *)(void *)(list + 1) = (AstraDrawListCommand){
        .operation = ASTRA_DRAW_LIST_FILL,
        .width = 4u, .height = 4u, .color = 0x40a0b0c0u,
        .clip_right = 16u, .clip_bottom = 8u,
    };
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_LIST_ATTACH}, list_area,
               &reply) == ASTRA_STATUS_OK);
    list_id = reply.object;
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
               .object = list_id, .target = target}, 0u, &reply) ==
           ASTRA_STATUS_OK && graphics_batch_count == 1u);
    command = recorded_command(0u, 0u);
    assert(read_be32(command + 4u) >> 16 == ASTRA_RENDER_OP_FILL &&
           read_be32(command + 60u) == 0x40a0b0c0u);
    /* A rectangle list is one hardware command whatever its length; an
       empty rectangle costs nothing. */
    {
        AstraDrawListCommand *rects =
            (AstraDrawListCommand *)(void *)(list + 1);
        AstraDrawListRect *records = (AstraDrawListRect *)(void *)
            ((uint8_t *)list +
             astra_draw_list_payload_offset(list->command_capacity));
        const uint8_t *record;

        records[0] = (AstraDrawListRect){ 1, 2, 3u, 4u };
        records[1] = (AstraDrawListRect){ 5, 6, 0u, 1u };
        records[2] = (AstraDrawListRect){ 7, 3, 2u, 2u };
        list->payload_bytes = 3u * sizeof(*records);
        *rects = (AstraDrawListCommand){
            .operation = ASTRA_DRAW_LIST_FILL_RECTS,
            .color = 0x40a0b0c0u,
            .payload_offset =
                astra_draw_list_payload_offset(list->command_capacity),
            .payload_bytes = 3u * sizeof(*records),
            .clip_right = 16u, .clip_bottom = 8u,
        };
        assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
                   .object = list_id, .target = target}, 0u,
                   &reply) == ASTRA_STATUS_OK &&
               graphics_batch_count == 1u &&
               read_be32(graphics_batches[0] + 12u) == 1u);
        command = recorded_command(0u, 0u);
        assert(read_be32(command + 4u) ==
                   (uint32_t)ASTRA_RENDER_OP_FILL_RECTS << 16 &&
               read_be32(command + 44u) == 2u &&
               read_be32(command + 48u) == 0u &&
               read_be32(command + 60u) == 0x40a0b0c0u);
        record = recorded_record(0u, read_be32(command + 40u));
        assert(read_be32(record + 0u) == ((uint32_t)1u << 16 | 2u) &&
               read_be32(record + 4u) == ((uint32_t)3u << 16 | 4u) &&
               read_be32(record + 16u) == ((uint32_t)7u << 16 | 3u));
        /* Translucent BLEND is the same list, blended by the hardware. */
        rects->flags =
            ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND);
        assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
                   .object = list_id, .target = target}, 0u,
                   &reply) == ASTRA_STATUS_OK &&
               graphics_batch_count == 1u &&
               read_be32(graphics_batches[0] + 12u) == 1u);
        command = recorded_command(0u, 0u);
        assert(read_be32(command + 4u) ==
                   (uint32_t)ASTRA_RENDER_OP_FILL_RECTS << 16 &&
               read_be32(command + 44u) == 2u &&
               read_be32(command + 48u) ==
                   ASTRA_RENDER_FILL_RECTS_OPTION_BLEND &&
               read_be32(command + 60u) == 0x40a0b0c0u);
    }
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_DESTROY,
               .object = target}, 0u, &reply) == ASTRA_STATUS_OK);
}

/* SURFACE_READ: rows come back through the device into staging. */
static void test_window_graphics_read(DisplayWindowGraphics *graphics,
                                      const DisplayGraphicsHost *host,
                                      const DisplayWindow *window,
                                      uint8_t *staging, uint32_t mapped)
{
    AstraGuiGraphicsReply reply;
    uint32_t readable;
    uint32_t hidden;

    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 32u, .height = 16u,
               .format = ASTRA_PIXEL_FORMAT_XRGB8888,
               .flags = ASTRA_SURFACE_DRAW_TARGET |
                        ASTRA_SURFACE_CPU_READ}, 0u, &reply) ==
           ASTRA_STATUS_OK);
    readable = reply.object;
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 32u, .height = 16u,
               .format = ASTRA_PIXEL_FORMAT_XRGB8888,
               .flags = ASTRA_SURFACE_DRAW_TARGET}, 0u, &reply) ==
           ASTRA_STATUS_OK);
    hidden = reply.object;
    read_count = 0u;
    memset(staging, 0xee, mapped);
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_READ, .object = readable,
               .x = 3, .y = 5, .width = 4u, .height = 2u, .offset = 100u,
               .pitch = 40u}, 0u, &reply) == ASTRA_STATUS_OK);
    assert(read_count == 1u && graphics_batch_count == 0u &&
           read_request_bytes[0] ==
               ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES + 2u * 16u);
    assert(read_requests[0].data_bytes == 32u * 16u * 4u &&
           read_requests[0].pitch == 128u && read_requests[0].width == 32u &&
           read_requests[0].height == 16u &&
           read_requests[0].format == ASTRA_RENDER_FORMAT_XRGB8888 &&
           read_requests[0].x == 3u && read_requests[0].y == 5u &&
           read_requests[0].read_width == 4u &&
           read_requests[0].read_height == 2u &&
           read_requests[0].data_offset >= ASTRA_RENDER_BATCH_WORKSPACE_LIMIT);
    for (uint32_t line = 0u; line < 2u; ++line)
        for (uint32_t at = 0u; at < 16u; ++at)
            assert(staging[100u + line * 40u + at] ==
                   (uint8_t)((5u + line) * 31u + 12u + at));
    /* Bytes between the staged rows are the client's, untouched. */
    assert(staging[99] == 0xee && staging[116] == 0xee &&
           staging[139] == 0xee && staging[156] == 0xee);
    /* The window content is readable too. */
    read_count = 0u;
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_READ,
               .object = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID,
               .width = 320u, .height = 2u, .pitch = 640u}, 0u, &reply) ==
           ASTRA_STATUS_OK && read_count == 1u &&
           read_requests[0].data_offset ==
               window->content_offset[window->content_back] &&
           read_requests[0].format == ASTRA_RENDER_FORMAT_RGB565);
    /* Readback needs CPU_READ, a rectangle inside the surface, and rows
       inside the staging area; a device failure reaches the client. */
    read_count = 0u;
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_READ, .object = hidden,
               .width = 1u, .height = 1u, .pitch = 4u}, 0u, &reply) ==
           ASTRA_STATUS_ACCESS);
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_READ, .object = readable,
               .x = 29, .width = 4u, .height = 1u, .pitch = 16u}, 0u,
               &reply) == ASTRA_STATUS_INVALID);
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_READ, .object = readable,
               .width = 32u, .height = 16u, .offset = mapped - 128u * 15u,
               .pitch = 128u}, 0u, &reply) == ASTRA_STATUS_INVALID);
    assert(read_count == 0u);
    read_status = ASTRA_STATUS_IO;
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_READ, .object = readable,
               .width = 1u, .height = 1u, .pitch = 4u}, 0u, &reply) ==
           ASTRA_STATUS_IO);
    read_status = ASTRA_STATUS_OK;
    /* A read larger than one buffer is split into bands. */
    graphics->staging = big_staging;
    graphics->staging_bytes = sizeof(big_staging);
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 4096u, .height = 1100u,
               .format = ASTRA_PIXEL_FORMAT_XRGB8888,
               .flags = ASTRA_SURFACE_CPU_READ}, 0u, &reply) ==
           ASTRA_STATUS_OK);
    read_count = 0u;
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_READ,
               .object = reply.object, .width = 4096u, .height = 1100u,
               .pitch = 4096u * 4u}, 0u, &reply) == ASTRA_STATUS_OK);
    assert(read_count == 3u &&
           read_requests[0].y == 0u && read_requests[1].y ==
               read_requests[0].read_height &&
           read_requests[0].read_height + read_requests[1].read_height +
                   read_requests[2].read_height == 1100u);
    assert(big_staging[(uint64_t)4096u * 4u * 1099u + 5u] ==
           (uint8_t)(1099u * 31u + 5u));
    graphics->staging = staging;
    graphics->staging_bytes = mapped;
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_DESTROY,
               .object = readable}, 0u, &reply) == ASTRA_STATUS_OK);
    assert(graphics_run(graphics, host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_DESTROY,
               .object = hidden}, 0u, &reply) == ASTRA_STATUS_OK);
}

static void test_window_graphics(void)
{
    DisplayWindow window = {0};
    DisplayState state = {
        .windows = &window,
        .capacity = 1u,
        .count = 1u,
    };
    DisplayGraphicsHost host = {
        .context = &state,
        .allocate = record_allocate,
        .submit = record_submit,
        .read = record_read,
        .storage = record_storage,
        .content_width = 320u,
        .content_height = 200u,
    };
    AstraGuiGraphicsReply reply;
    uint32_t staging_area = 0u;
    uint32_t list_area = 0u;
    uint32_t sprite;
    uint32_t sprite_offset;
    uint8_t *staging;
    AstraDrawListHeader *list;
    AstraDrawListCommand *draw;
    const uint8_t *command;
    const uint8_t *record;
    uint32_t mapped = 0u;
    uint32_t creates = test_area_creates;
    uint32_t closes = test_area_closes;

    window.request.type = ASTRA_WINDOW_STANDARD;
    window.request.width = 320u;
    window.request.height = 200u;
    assert(display_window_media_prepare(&state, &window) == ASTRA_STATUS_OK);
    host.content_offset = window.content_offset[window.content_back];
    host.content_pitch = window.content_pitch;
    host.content_bytes = window.content_bytes;
    assert(astra_rt_area_create(8192u, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                ASTRA_RIGHT_MAP, &staging_area) ==
           ASTRA_SYSCALL_OK);
    assert(astra_rt_area_map(staging_area, ASTRA_AREA_MAP_READ |
                             ASTRA_AREA_MAP_WRITE, (void **)&staging,
                             &mapped) == ASTRA_SYSCALL_OK);
    assert(display_window_graphics_open(&window.graphics, staging_area) ==
           ASTRA_STATUS_OK);

    /* A failed batch the service left running is the next command's
       failure, not a silent one; nothing is submitted after it. */
    storage_failure = ASTRA_STATUS_IO;
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 16u, .height = 8u,
               .format = ASTRA_PIXEL_FORMAT_ARGB8888,
               .flags = ASTRA_SURFACE_DRAW_SOURCE |
                        ASTRA_SURFACE_CPU_WRITE}, 0u, &reply) ==
           ASTRA_STATUS_IO && graphics_batch_count == 0u);
    storage_failure = ASTRA_STATUS_OK;

    /* Creation clears the Media RAM an earlier client may have used. */
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 16u, .height = 8u,
               .format = ASTRA_PIXEL_FORMAT_ARGB8888,
               .flags = ASTRA_SURFACE_DRAW_SOURCE |
                        ASTRA_SURFACE_CPU_WRITE}, 0u, &reply) ==
           ASTRA_STATUS_OK);
    sprite = reply.object;
    assert(sprite > ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID &&
           reply.pitch == 64u && graphics_batch_count == 1u);
    command = recorded_command(0u, 0u);
    record = recorded_record(0u, read_be32(command + 32u));
    sprite_offset = read_be32(record + 8u);
    assert(read_be32(command + 4u) >> 16 == ASTRA_RENDER_OP_FILL &&
           read_be32(command + 60u) == 0u &&
           read_be32(record + 24u) >> 24 == ASTRA_RENDER_FORMAT_ARGB8888);
    /* The allocator now steps over the surface. */
    assert(!media_extents_overlap(
        record_allocate(&state, 4096u), 4096u, sprite_offset, 16u * 8u * 4u));
    assert(!media_extents_overlap(window.content_offset[window.content_back],
                                  window.content_bytes,
                                  sprite_offset, 16u * 8u * 4u));

    /* Unsupported rights are reported, not dropped. */
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 16u, .height = 8u,
               .format = ASTRA_PIXEL_FORMAT_RGB565,
               .flags = ASTRA_SURFACE_SCANOUT}, 0u, &reply) ==
           ASTRA_STATUS_UNSUPPORTED && graphics_batch_count == 0u);
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 4097u, .height = 8u,
               .format = ASTRA_PIXEL_FORMAT_RGB565,
               .flags = ASTRA_SURFACE_DRAW_SOURCE}, 0u, &reply) ==
           ASTRA_STATUS_INVALID);

    /* A staging write uploads the rows and blits them into place. */
    for (uint32_t at = 0u; at < 64u * 8u; ++at)
        staging[256u + at] = (uint8_t)(at * 7u);
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_WRITE, .object = sprite,
               .x = 4, .y = 2, .width = 8u, .height = 4u,
               .offset = 256u, .pitch = 64u}, 0u, &reply) ==
           ASTRA_STATUS_OK && graphics_batch_count == 1u);
    command = recorded_command(0u, 0u);
    assert(read_be32(command + 4u) >> 16 == ASTRA_RENDER_OP_BLIT &&
           read_be32(recorded_record(0u, read_be32(command + 32u)) + 8u) ==
               sprite_offset &&
           read_be32(command + 48u) == ((uint32_t)4u << 16 | 2u) &&
           read_be32(command + 52u) == ((uint32_t)8u << 16 | 4u));
    record = recorded_record(0u, read_be32(command + 36u));
    /* The rows keep the staging pitch: the device placed them as staged. */
    assert(read_be32(record + 16u) == 64u &&
           memcmp(recorded_record(0u, read_be32(record + 8u)),
                  staging + 256u, 32u) == 0 &&
           memcmp(recorded_record(0u, read_be32(record + 8u)) + 64u * 3u,
                  staging + 256u + 64u * 3u, 32u) == 0);
    /* Writes stay inside the surface and the staging area. */
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_WRITE, .object = sprite,
               .x = 12, .width = 8u, .height = 1u, .pitch = 32u}, 0u,
               &reply) == ASTRA_STATUS_INVALID);
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_WRITE, .object = sprite,
               .width = 16u, .height = 8u, .offset = 8192u - 64u * 7u,
               .pitch = 64u}, 0u, &reply) == ASTRA_STATUS_INVALID);

    /* A large write is split into batches that each fit the mailbox. */
    graphics_batch_count = 0u;
    window.graphics->staging = big_staging;
    window.graphics->staging_bytes = sizeof(big_staging);
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_SURFACE_CREATE,
               .width = 4096u, .height = 1100u,
               .format = ASTRA_PIXEL_FORMAT_XRGB8888,
               .flags = ASTRA_SURFACE_DRAW_SOURCE |
                        ASTRA_SURFACE_CPU_WRITE}, 0u, &reply) ==
           ASTRA_STATUS_OK);
    {
        uint32_t large = reply.object;
        uint32_t rows = 0u;

        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_SURFACE_WRITE,
                   .object = large, .width = 4096u, .height = 1100u,
                   .pitch = 4096u * 4u}, 0u, &reply) == ASTRA_STATUS_OK);
        assert(graphics_batch_count == 3u);
        for (uint32_t index = 0u; index < graphics_batch_count; ++index) {
            const uint8_t *blit = recorded_command(index, 0u);

            assert(graphics_batch_bytes[index] <=
                   ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES);
            assert(read_be32(blit + 48u) == rows);
            rows += read_be32(blit + 52u) & 0xffffu;
        }
        assert(rows == 1100u);
        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_SURFACE_DESTROY,
                   .object = large}, 0u, &reply) == ASTRA_STATUS_OK);
    }
    window.graphics->staging = staging;
    window.graphics->staging_bytes = mapped;

    /* A submitted list blits the sprite into the window content. */
    assert(astra_rt_area_create(8192u, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                ASTRA_RIGHT_MAP, &list_area) ==
           ASTRA_SYSCALL_OK);
    assert(astra_rt_area_map(list_area, ASTRA_AREA_MAP_READ |
                             ASTRA_AREA_MAP_WRITE, (void **)&list,
                             &mapped) == ASTRA_SYSCALL_OK);
    *list = (AstraDrawListHeader){
        .magic = ASTRA_DRAW_LIST_MAGIC,
        .version = ASTRA_DRAW_LIST_VERSION_1_5,
        .total_bytes = 8192u,
        .command_count = 1u,
        .width = 320u,
        .height = 200u,
        .command_capacity = 16u,
    };
    draw = (AstraDrawListCommand *)(void *)(list + 1);
    *draw = (AstraDrawListCommand){
        .operation = ASTRA_DRAW_LIST_BLIT,
        .flags = ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND),
        .x = 100, .y = 50, .width = 32u, .height = 16u,
        .color = ASTRA_DRAW_LIST_COLOR_IDENTITY,
        .source = sprite,
        .source_width = 16u, .source_height = 8u,
        .clip_right = 320u, .clip_bottom = 200u,
    };
    assert(graphics_run(window.graphics, &host, (AstraGuiGraphicsCommand){
               .action = ASTRA_GUI_GRAPHICS_LIST_ATTACH}, list_area,
               &reply) == ASTRA_STATUS_OK && reply.object != 0u);
    {
        uint32_t list_id = reply.object;

        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
                   .object = list_id,
                   .target = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID}, 0u,
                   &reply) == ASTRA_STATUS_OK && graphics_batch_count == 1u);
        command = recorded_command(0u, 0u);
        assert(read_be32(command + 4u) ==
               ((uint32_t)ASTRA_RENDER_OP_BLIT << 16 |
                ASTRA_RENDER_FLAG_BLIT_ALPHA));
        assert(read_be32(recorded_record(0u, read_be32(command + 32u)) +
                         8u) == window.content_offset[window.content_back]);
        assert(read_be32(recorded_record(0u, read_be32(command + 36u)) +
                         8u) == sprite_offset);
        /* The sprite is not a draw target; a surface without SOURCE is
           not a source. */
        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
                   .object = list_id, .target = sprite}, 0u, &reply) ==
               ASTRA_STATUS_ACCESS);
        /* A destroyed source makes the list invalid; nothing is drawn. */
        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_SURFACE_DESTROY,
                   .object = sprite}, 0u, &reply) == ASTRA_STATUS_OK);
        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
                   .object = list_id,
                   .target = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID}, 0u,
                   &reply) == ASTRA_STATUS_INVALID &&
               graphics_batch_count == 0u);
        /* A failed hardware submission is reported to the client. */
        draw->source = ASTRA_DRAW_LIST_SOURCE_DESTINATION;
        draw->flags = 0u;
        draw->source_width = 32u;
        draw->source_height = 16u;
        graphics_submit_status = ASTRA_STATUS_IO;
        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
                   .object = list_id,
                   .target = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID}, 0u,
                   &reply) == ASTRA_STATUS_IO);
        graphics_submit_status = ASTRA_STATUS_OK;
        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_DETACH,
                   .object = list_id}, 0u, &reply) == ASTRA_STATUS_OK);
        assert(graphics_run(window.graphics, &host,
                            (AstraGuiGraphicsCommand){
                   .action = ASTRA_GUI_GRAPHICS_LIST_SUBMIT,
                   .object = list_id,
                   .target = ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID}, 0u,
                   &reply) == ASTRA_STATUS_NOT_FOUND);
    }
    test_window_graphics_argb_target(window.graphics, &host);
    test_window_graphics_read(window.graphics, &host, &window, staging,
                              mapped);
    /* Closing releases every area the graphics state owned. */
    display_window_graphics_close(window.graphics);
    window.graphics = NULL;
    assert(test_area_creates - creates == test_area_closes - closes);
}

static const uint8_t *batch_command(uint32_t index)
{
    assert(index < read_be32(batch + 12u));
    return batch + ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
           ASTRA_RENDER_BATCH_ARENA_OFFSET +
           index * ASTRA_RENDER_COMMAND_BYTES;
}

static uint32_t batch_descriptor_offset(uint32_t descriptor)
{
    return read_be32(batch + descriptor - ASTRA_RENDER_BATCH_ARENA_OFFSET +
                     8u);
}

static const uint8_t *batch_scene_layer(uint32_t index)
{
    const uint8_t *scene = batch_scene();

    assert(index < read_be32(scene + 20u));
    return scene + read_be32(scene + 24u) +
           index * ASTRA_WINDOW_SCENE_LAYER_BYTES;
}

static uint32_t present_content(DisplayState *state, uint32_t index,
                                uint32_t flags, uint16_t x, uint16_t y,
                                uint16_t width, uint16_t height)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraGuiWindowCommand present = {
        .header = {
            .total_size = sizeof(AstraGuiWindowCommand),
            .header_size = ASTRA_MESSAGE_HEADER_SIZE,
            .protocol = ASTRA_GUI_PROTOCOL,
            .protocol_version = ASTRA_GUI_VERSION,
            .operation = ASTRA_GUI_WINDOW_COMMAND,
            .transaction_id = 1u,
        },
        .window = state->windows[index].id,
        .generation = state->windows[index].generation,
        .action = ASTRA_GUI_WINDOW_PRESENT,
        .flags = flags,
        .x = x, .y = y, .width = width, .height = height,
    };
    DisplayWindow closed = {0};
    int changed = 0;

    assert(valid_command(&present, sizeof(present), 1u,
                         state->windows[index].id,
                         state->windows[index].request.width,
                         state->windows[index].request.height));
    return apply_command(state, &theme, &present, &closed, &changed);
}

/* Compose, check the batch, and commit it as a completed present would. */
static void compose_commit(DisplayState *state, uint32_t fence)
{
    uint32_t error = ASTRA_STATUS_OK;

    assert(valid_batch(compose(batch, fence, state, &error, NULL)) != 0u);
    assert(error == ASTRA_STATUS_OK);
    commit_render_state(state);
}

static void test_content_banks(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    DisplayWindow windows[TEST_WINDOW_COUNT] = {0};
    DisplayState state = {
        .windows = windows,
        .capacity = TEST_WINDOW_COUNT,
    };
    DisplayWindow *window = &windows[0];
    uint32_t round = theme.window_radius - theme.frame_width;
    int32_t client_x = 100 + theme.frame_width;
    int32_t client_y = 90 + theme.frame_width + theme.titlebar_height +
                       theme.signal_height;
    const uint8_t *command;
    const uint8_t *layer;
    uint8_t shown;

    add_window(&state, 0u, ASTRA_WINDOW_STANDARD, 100u, 90u, 300u, 200u,
               ASTRA_WINDOW_ACTIVE, 0u);
    window->request.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
    /* The pitch lets a layer begin at any row. */
    assert(window->content_pitch == 640u &&
           window->content_bytes == 640u * 200u);
    damage_window(&state, &theme, window);
    compose_commit(&state, 2u);
    assert(window->content_front == 1u && window->content_back == 0u &&
           window->content_shown == 1u);
    /* The service's own first clear is carried forward like any frame. */
    assert(window->content_stale[0].valid == 0u &&
           window->content_stale[2].valid != 0u);

    /* A discarding present of a changed window costs no GPU command: the
       scene shows the bank the client drew, in three layers. */
    assert(present_content(&state, 0u, ASTRA_GUI_PRESENT_DISCARD,
                           0u, 0u, 0u, 0u) == ASTRA_STATUS_OK);
    assert(valid_batch(compose(batch, 3u, &state, &(uint32_t){0}, NULL))
           != 0u);
    assert(read_be32(batch + 12u) == 0u);
    assert(read_be32(batch_scene() + 20u) == 5u);
    layer = batch_scene_layer(2u);
    assert(read_be32(layer) == window->cache_offset[window->cache_pending] &&
           (read_be32(layer + 20u) & 0xffffu) == theme.window_radius);
    /* The rounded bottom strip ... */
    layer = batch_scene_layer(3u);
    assert(read_be32(layer) == window->content_offset[0] +
                                   (200u - round * 2u) * 640u &&
           read_be32(layer + 8u) == 640u &&
           read_be32(layer + 12u) == (300u << 16 | round * 2u) &&
           read_be32(layer + 16u) ==
               ((uint32_t)client_x << 16 |
                (uint32_t)(client_y + 200 - (int32_t)round * 2)) &&
           read_be32(layer + 20u) ==
               (round | ASTRA_WINDOW_SCENE_LAYER_VISIBLE));
    /* ... under the square rows that hide its top corners. */
    layer = batch_scene_layer(4u);
    assert(read_be32(layer) == window->content_offset[0] &&
           read_be32(layer + 12u) == (300u << 16 | (200u - round)) &&
           read_be32(layer + 16u) ==
               ((uint32_t)client_x << 16 | (uint32_t)client_y) &&
           read_be32(layer + 20u) == ASTRA_WINDOW_SCENE_LAYER_VISIBLE);
    commit_render_state(&state);
    assert(window->content_front == 0u && window->content_shown == 0u &&
           window->content_back == 2u && window->content_discard == 0u);

    /* A plain present carries the frame forward: one blit from the drawn
       bank into the bank the client draws next -- never one this scene or
       the one before it shows. That bank last held the first frame, so it
       missed the whole discarded one. */
    shown = window->content_shown;
    assert(present_content(&state, 0u, 0u, 10u, 20u, 30u, 40u) ==
           ASTRA_STATUS_OK);
    assert(valid_batch(compose(batch, 4u, &state, &(uint32_t){0}, NULL))
           != 0u);
    assert(read_be32(batch + 12u) == 1u);
    command = batch_command(0u);
    assert(read_be32(command + 4u) >> 16 == ASTRA_RENDER_OP_BLIT &&
           batch_descriptor_offset(read_be32(command + 36u)) ==
               window->content_offset[2] &&
           batch_descriptor_offset(read_be32(command + 32u)) ==
               window->content_offset[window->content_pending_back] &&
           window->content_pending_back != 2u &&
           window->content_pending_back != shown &&
           read_be32(command + 44u) == 0u && read_be32(command + 48u) == 0u &&
           read_be32(command + 52u) == (300u << 16 | 200u));
    commit_render_state(&state);
    assert(window->content_front == 2u && window->content_back == 1u);

    /* Bank 0 missed the last frame and this one: it gets both damages. */
    assert(present_content(&state, 0u, 0u, 200u, 150u, 10u, 10u) ==
           ASTRA_STATUS_OK);
    assert(valid_batch(compose(batch, 5u, &state, &(uint32_t){0}, NULL))
           != 0u);
    command = batch_command(0u);
    assert(read_be32(batch + 12u) == 1u &&
           window->content_pending_back == 0u &&
           read_be32(command + 44u) == (10u << 16 | 20u) &&
           read_be32(command + 48u) == (10u << 16 | 20u) &&
           read_be32(command + 52u) == (200u << 16 | 140u));
    commit_render_state(&state);
    /* An unchanged window costs nothing and keeps showing its front. */
    damage_both(&state, (DamageRect){0, 0, 10, 10, 1u});
    assert(valid_batch(compose(batch, 6u, &state, &(uint32_t){0}, NULL))
           != 0u);
    assert(read_be32(batch + 12u) == 0u &&
           read_be32(batch_scene_layer(4u)) ==
               window->content_offset[window->content_front]);
    commit_render_state(&state);

    /* Only a CONTENT_SURFACE client can promise to redraw everything. */
    window->request.content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST;
    assert(present_content(&state, 0u, ASTRA_GUI_PRESENT_DISCARD,
                           0u, 0u, 0u, 0u) == ASTRA_STATUS_INVALID);
    window->request.content_format = ASTRA_WINDOW_CONTENT_SURFACE;

    /* An open menu is drawn once and then only when its hover moves. */
    set_overlay(&state, DISPLAY_OVERLAY_MENU);
    compose_commit(&state, 7u);
    assert(state.overlay_drawn == DISPLAY_OVERLAY_MENU);
    damage_both(&state, overlay_bounds(DISPLAY_OVERLAY_MENU));
    state.system_initialized = 1u;
    assert(valid_batch(compose(batch, 8u, &state, &(uint32_t){0}, NULL))
           != 0u);
    assert(read_be32(batch + 12u) == 0u);
    state.overlay_hover = 2u;
    assert(valid_batch(compose(batch, 8u, &state, &(uint32_t){0}, NULL))
           != 0u);
    assert(read_be32(batch + 12u) != 0u && state.overlay_pending != 0u);
}

/* Every BLIT reads inside its source surface: the FPGA blitter refuses a
   source rectangle past the surface (BAD_RANGE) where QEMU's clips it. */
static int batch_blits_within_sources(void)
{
    uint32_t count = read_be32(batch + 12u);
    uint32_t commands = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;

    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *command = batch + commands +
                                 index * ASTRA_RENDER_COMMAND_BYTES;
        uint32_t source = read_be32(command + 36u);
        uint32_t extent;
        uint32_t origin;
        uint32_t size;

        if ((read_be32(command + 4u) >> 16) != ASTRA_RENDER_OP_BLIT)
            continue;
        assert(source >= ASTRA_RENDER_BATCH_ARENA_OFFSET);
        extent = read_be32(batch + source - ASTRA_RENDER_BATCH_ARENA_OFFSET +
                           20u);
        origin = read_be32(command + 44u);
        size = read_be32(command + 52u);
        if ((origin >> 16) + (size >> 16) > (extent >> 16) ||
            (origin & 0xffffu) + (size & 0xffffu) > (extent & 0xffffu))
            return 0;
    }
    return 1;
}

#define TEST_AICON_BYTES (88u + 16u * 16u + 32u * 32u + 64u * 64u)

static void store_be16(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

static void store_be32(uint8_t *at, uint32_t value)
{
    store_be16(at, (uint16_t)(value >> 16));
    store_be16(at + 2u, (uint16_t)value);
}

/* A two-color AICON: palette 0 transparent, 1 opaque green; the 64 strike
   green in its left half. */
static void build_aicon(uint8_t *bytes)
{
    static const uint16_t sizes[3] = {16u, 32u, 64u};
    uint32_t data = 88u;

    memset(bytes, 0, TEST_AICON_BYTES);
    store_be32(bytes, ASTRA_AICON_MAGIC);
    store_be16(bytes + 4u, ASTRA_AICON_VERSION);
    store_be16(bytes + 6u, ASTRA_AICON_HEADER_SIZE);
    store_be32(bytes + 8u, TEST_AICON_BYTES);
    store_be16(bytes + 12u, ASTRA_AICON_REQUIRED_STRIKES);
    store_be16(bytes + 14u, 2u);
    store_be32(bytes + 16u, 32u);
    store_be32(bytes + 20u, 40u);
    store_be32(bytes + 24u, 88u);
    bytes[36] = 0x20u;
    bytes[37] = 0xc0u;
    bytes[38] = 0x40u;
    bytes[39] = 0xffu;
    for (uint32_t strike = 0u; strike < 3u; ++strike) {
        uint8_t *record = bytes + 40u + strike * ASTRA_AICON_STRIKE_SIZE;
        uint32_t count = (uint32_t)sizes[strike] * sizes[strike];

        store_be16(record, sizes[strike]);
        store_be16(record + 2u, sizes[strike]);
        store_be32(record + 4u, data);
        store_be32(record + 8u, count);
        for (uint32_t at = 0u; at < count; ++at)
            bytes[data + at] = at % sizes[strike] < sizes[strike] / 2u;
        data += count;
    }
}

/* A named session shows its launch panel over the windows until a window
   of that session presents, or the session ends. */
static void test_launch_panel(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    DisplayWindow windows[TEST_WINDOW_COUNT] = {0};
    DisplayState state = {
        .windows = windows,
        .capacity = TEST_WINDOW_COUNT,
        .damage = {
            {0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u},
            {0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u}
        }
    };
    AstraGuiOpenSession named = {
        .header = {
            .total_size = sizeof(AstraGuiOpenSession),
            .header_size = ASTRA_MESSAGE_HEADER_SIZE,
            .protocol = ASTRA_GUI_PROTOCOL,
            .protocol_version = ASTRA_GUI_VERSION,
            .operation = ASTRA_GUI_OPEN_SESSION,
            .transaction_id = 1u,
        },
        .name_length = 4u,
        .name = "Doom",
    };
    const uint32_t rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP;
    uint32_t icon = 0u;
    uint32_t closes;
    uint32_t none = 0u;

    assert(valid_open_session(&named, sizeof(named), 1u));
    assert(!valid_open_session(&named, sizeof(named), 2u));
    assert(!valid_open_session(&named, sizeof(AstraGuiSessionRequest), 1u));
    named.name[0] = (char)0xff;
    assert(!valid_open_session(&named, sizeof(named), 1u));
    named.name[0] = 'D';
    named.name_length = ASTRA_WINDOW_TITLE_MAX + 1u;
    assert(!valid_open_session(&named, sizeof(named), 1u));
    named.name_length = 4u;
    named.icon_length = TEST_AICON_BYTES;
    assert(valid_open_session(&named, sizeof(named), 2u));
    assert(!valid_open_session(&named, sizeof(named), 1u));

    assert(astra_rt_area_create(TEST_AICON_BYTES, rights, &icon) ==
           ASTRA_SYSCALL_OK);
    build_aicon(test_areas[icon - 0x800u]);
    begin_launch(&state, &named, 5u, &icon);
    assert(icon == 0u && state.launch.session == 5u &&
           state.launch.icon_bytes != NULL &&
           state.launch.strike.width == DISPLAY_LAUNCH_ICON);
    compose_commit(&state, 1u);
    /* The bars, then the panel; its icon blended over the panel. */
    assert(read_be32(batch_scene() + 20u) == 3u);
    assert(batch_scene_layer_source(2u) == state.launch_offset);
    assert(state.launch_drawn == 5u);
    assert(launch_icon_pixels[0] == astra_surface_rgb565(0x20, 0xc0, 0x40));
    assert(launch_icon_pixels[DISPLAY_LAUNCH_ICON - 1u] ==
           color(theme.title_inactive));

    /* Its window opens under the panel: no frame yet, the panel stays. */
    add_window(&state, 0u, ASTRA_WINDOW_STANDARD, 100u, 100u, 200u, 150u,
               ASTRA_WINDOW_ACTIVE, 0u);
    state.windows[0].session = 5u;
    for (uint32_t bank = 0u; bank < DISPLAY_CONTENT_BANKS; ++bank)
        assert(!media_extents_overlap(state.windows[0].content_offset[bank],
                                      state.windows[0].content_bytes,
                                      state.launch_offset,
                                      state.launch_capacity));
    damage_both(&state, (DamageRect){0, 0, 10, 10, 1u});
    compose_commit(&state, 2u);
    assert(state.launch.session == 5u);
    assert(batch_scene_layer_source(read_be32(batch_scene() + 20u) - 1u) ==
           state.launch_offset);

    /* A window of another session drawing leaves it. */
    state.windows[0].session = 6u;
    assert(present_content(&state, 0u, 0u, 0u, 0u, 0u, 0u) ==
           ASTRA_STATUS_OK);
    assert(state.launch.session == 5u);

    /* Its own first frame ends it, and gives the icon back. */
    state.windows[0].session = 5u;
    closes = test_area_closes;
    assert(present_content(&state, 0u, 0u, 0u, 0u, 0u, 0u) ==
           ASTRA_STATUS_OK);
    assert(state.launch.session == 0u && state.launch.icon_bytes == NULL &&
           test_area_closes == closes + 1u);
    compose_commit(&state, 3u);
    assert(batch_scene_layer_source(read_be32(batch_scene() + 20u) - 1u) !=
           state.launch_offset);

    /* An unnamed session shows nothing; a named one without an icon shows
       its name alone, until its session ends. */
    named.icon_length = 0u;
    begin_launch(&state, &named, 9u, &none);
    assert(state.launch.session == 9u && state.launch.icon_bytes == NULL);
    compose_commit(&state, 4u);
    assert(state.launch_drawn == 9u);
    assert(astra_rt_area_create(4096u, rights, &state.session_receive[0]) ==
           ASTRA_SYSCALL_OK);
    state.session_id[0] = 9u;
    state.session_count = 1u;
    close_session(&state, 0u);
    assert(state.session_count == 0u && state.launch.session == 0u);
    compose_commit(&state, 5u);
}

/* A window that leaves full screen keeps no damage from its larger self:
   the next frame's carry-forward copies only what the window now has. */
static void test_shrunk_window_copy_forward(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    DisplayWindow windows[TEST_WINDOW_COUNT] = {0};
    DisplayState state = {
        .windows = windows,
        .capacity = TEST_WINDOW_COUNT,
        .damage = {
            {0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u},
            {0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u}
        }
    };
    AstraGuiWindowCommand fullscreen = {
        .window = 1u, .action = ASTRA_GUI_WINDOW_FULLSCREEN,
    };
    AstraGuiWindowCommand restore = {
        .window = 1u, .action = ASTRA_GUI_WINDOW_RESTORE,
    };
    DisplayWindow closed = {0};
    int changed = 0;

    add_window(&state, 0u, ASTRA_WINDOW_STANDARD, 100u, 100u, 800u, 600u,
               ASTRA_WINDOW_ACTIVE, 0u);
    state.windows[0].request.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
    compose_commit(&state, 1u);
    assert(apply_command(&state, &theme, &fullscreen, &closed, &changed) ==
           ASTRA_STATUS_OK && changed);
    compose_commit(&state, 2u);
    assert(present_content(&state, 0u, 0u, 0u, 0u, 0u, 0u) ==
           ASTRA_STATUS_OK);
    compose_commit(&state, 3u);
    assert(apply_command(&state, &theme, &restore, &closed, &changed) ==
           ASTRA_STATUS_OK && changed);
    assert(state.windows[0].request.width == 800u &&
           state.windows[0].request.height == 600u);
    compose_commit(&state, 4u);
    assert(batch_blit_count() != 0u);
    assert(batch_blits_within_sources());
}

int main(void)
{
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

    {
        /* A request the device answers with a failure is that request's:
           a client's batch in flight is dropped and the display goes on;
           only a malformed answer means the device itself is lost. */
        collected = (AstraDisplayFrameCompletion){
            .size = ASTRA_DISPLAY_FRAME_COMPLETION_SIZE, .fence = 7u,
            .status = ASTRA_DISPLAY_COMPLETION_IO_ERROR, .generation = 3u,
        };
        in_flight.active = 1u;
        in_flight.fence = 7u;
        assert(settle(0x600u, 0x500u) == ASTRA_STATUS_OK &&
               in_flight.active == 0u && refusals_logged == 1u);
        in_flight.active = 1u;
        assert(collect_request(0x600u, 0x500u) == DISPLAY_REQUEST_REFUSED);
        collected.status = ASTRA_DISPLAY_COMPLETION_OK;
        in_flight.active = 1u;
        assert(collect_request(0x600u, 0x500u) == ASTRA_STATUS_OK);
        collected.fence = 8u;
        in_flight.active = 1u;
        assert(collect_request(0x600u, 0x500u) == DISPLAY_FAIL_COMPLETION);
        in_flight.active = 0u;
    }
    test_dynamic_window_resources();
    test_window_graphics();
    test_content_banks();

    {
        DisplayWindow fullscreen = {0};
        DisplayWindow ordinary = {0};

        fullscreen.request.type = ASTRA_WINDOW_FULLSCREEN;
        assert(frame_valid(&theme, &fullscreen, 0u, 0u, 320u, 200u));
        assert(frame_valid(&theme, &fullscreen, 0u, 0u,
                           ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT));
        assert(!frame_valid(&theme, &fullscreen, 1u, 0u, 320u, 200u));
        assert(!frame_valid(&theme, &fullscreen, 0u, 1u, 320u, 200u));
        assert(!frame_valid(&theme, &fullscreen, 0u, 0u,
                            ASTRA_DISPLAY_WIDTH + 1u, 200u));
        ordinary.request.type = ASTRA_WINDOW_STANDARD;
        assert(!frame_valid(&theme, &ordinary, 0u, 0u, 320u, 200u));
    }

    {
        DisplayWindow animated_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState animated = {
            .windows = animated_windows,
            .capacity = TEST_WINDOW_COUNT,
        };

        add_window(&animated, 0u, ASTRA_WINDOW_STANDARD,
                   0u, 0u, 100u, 100u, 0u, 0u);
        add_window(&animated, 1u, ASTRA_WINDOW_STANDARD,
                   100u, 0u, 100u, 100u, 0u, 0u);
        animated.windows[1].request.event_mask &=
            ~ASTRA_WINDOW_SUBSCRIBE_VBLANK;
        assert(signal_vblank(&animated) == ASTRA_STATUS_OK);
        assert(signal_count == 1u && last_signal == 0x700u);
        animated.windows[0].request.event_mask &=
            ~ASTRA_WINDOW_SUBSCRIBE_VBLANK;
        animated.windows[1].request.event_mask |=
            ASTRA_WINDOW_SUBSCRIBE_VBLANK;
        assert(signal_vblank(&animated) == ASTRA_STATUS_OK);
        assert(signal_count == 2u && last_signal == 0x701u);
        {
            AstraGuiWindowCommand subscription = {
                .window = animated.windows[1].id,
                .action = ASTRA_GUI_WINDOW_SET_EVENT_MASK,
                .flags = ASTRA_WINDOW_SUBSCRIBE_VBLANK};
            DisplayWindow closed;
            int changed;

            assert(apply_command(&animated, &theme, &subscription,
                                 &closed, &changed) == ASTRA_STATUS_OK);
        }
    }

    assert(service_status(ASTRA_SYSCALL_INVALID_ARGUMENT) ==
           ASTRA_STATUS_INVALID);
    assert(service_status(ASTRA_SYSCALL_RESOURCE_LIMIT) ==
           ASTRA_STATUS_LIMIT);
    assert(service_status(ASTRA_SYSCALL_PEER_DEAD) ==
           ASTRA_STATUS_PEER_DEAD);

    {
        DisplayWindow pointer_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState pointer_state = {
            .windows = pointer_windows,
            .capacity = TEST_WINDOW_COUNT,
        };
        AstraGuiWindowCommand shape = {
            .header = {
                .total_size = sizeof(AstraGuiWindowCommand),
                .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                .protocol = ASTRA_GUI_PROTOCOL,
                .protocol_version = ASTRA_GUI_VERSION,
                .operation = ASTRA_GUI_WINDOW_COMMAND,
                .transaction_id = 1u,
            },
            .window = 1u,
            .generation = 1u,
            .action = ASTRA_GUI_WINDOW_SET_POINTER_SHAPE,
            .flags = ASTRA_POINTER_SHAPE_TEXT,
        };
        AstraGuiWindowCommand image = shape;
        DisplayWindow ignored;
        int pointer_changed;

        add_window(&pointer_state, 0u, ASTRA_WINDOW_STANDARD,
                   100u, 100u, 300u, 200u, ASTRA_WINDOW_RESIZABLE, 0u);
        assert(valid_command(&shape, sizeof(shape), 1u, 1u, 300u, 200u));
        assert(apply_command(&pointer_state, &theme, &shape, &ignored,
                             &pointer_changed) == ASTRA_STATUS_OK);
        pointer_state.pointer_x = 150;
        pointer_state.pointer_y = 180;
        assert(display_pointer_shape(&pointer_state, &theme) ==
               ASTRA_POINTER_SHAPE_TEXT);
        image.action = ASTRA_GUI_WINDOW_SET_POINTER_IMAGE;
        image.x = 3u;
        image.y = 4u;
        image.width = 16u;
        image.height = 20u;
        image.flags = ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH *
                      sizeof(AstraColorRGBA8);
        assert(valid_command(&image, sizeof(image), 2u, 1u, 300u, 200u));
        image.x = image.width;
        assert(!valid_command(&image, sizeof(image), 2u, 1u, 300u, 200u));
    }

    {
        DisplayWindow cursor_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState cursor = {
            .windows = cursor_windows,
            .capacity = TEST_WINDOW_COUNT,
            .damage = {
                { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u },
                { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u },
            },
        };
        AstraGuiWindowCommand close = {
            .window = 1u,
            .action = ASTRA_GUI_WINDOW_CLOSE,
        };
        DisplayWindow closed = {0};
        AstraLogicalInputEvent reset = {
            .type = ASTRA_INPUT_EVENT_STATE_RESET,
        };
        uint32_t effects = 0u;
        uint32_t frame_window = 0u;
        uint32_t frame_timestamp = 0u;
        int changed = 0;

        add_window(&cursor, 0u, ASTRA_WINDOW_STANDARD,
                   100u, 100u, 300u, 200u, ASTRA_WINDOW_RESIZABLE, 0u);
        cursor.count = 1u;
        cursor.pointer_x = 10;
        cursor.pointer_y = 10;
        cursor.capture_window = 1u;
        cursor.capture_region = HIT_RESIZE_E;
        assert(display_pointer_shape(&cursor, &theme) ==
               ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL);
        assert(handle_pointer(&cursor, &reset, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert((effects & DISPLAY_POINTER_CURSOR) != 0u &&
               cursor.capture_window == 0u &&
               display_pointer_shape(&cursor, &theme) ==
                   ASTRA_POINTER_SHAPE_DEFAULT);
        cursor.pointer_x = 100 + (int32_t)outer_width(
            &theme, &cursor.windows[0]) - 1;
        cursor.pointer_y = 180;
        assert(display_pointer_shape(&cursor, &theme) ==
               ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL);
        assert(update_cursor(0x600u, &cursor,
                             display_pointer_shape(&cursor, &theme)) ==
               ASTRA_STATUS_OK);
        assert(cursor_post_x == (uint32_t)cursor.pointer_x &&
               cursor_post_y == 180u &&
               cursor_post_flags ==
                   (ASTRA_DISPLAY_CURSOR_VISIBLE |
                    ASTRA_DISPLAY_CURSOR_SHAPE(
                        ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL)));
        assert(apply_command(&cursor, &theme, &close, &closed, &changed) ==
               ASTRA_STATUS_OK && changed && cursor.count == 0u);
        assert(display_pointer_shape(&cursor, &theme) ==
               ASTRA_POINTER_SHAPE_DEFAULT);
        assert(update_cursor(0x600u, &cursor,
                             display_pointer_shape(&cursor, &theme)) ==
               ASTRA_STATUS_OK);
        assert(cursor_post_flags == ASTRA_DISPLAY_CURSOR_VISIBLE);
    }

    DisplayWindow state_windows[TEST_WINDOW_COUNT] = {0};
    DisplayState state = {
        .windows = state_windows,
        .capacity = TEST_WINDOW_COUNT,
        .damage = {
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u },
            { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u }
        }
    };
    AstraGuiWindowCommand move = {
        .window = 4u,
        .action = ASTRA_GUI_WINDOW_MOVE,
        .x = 120u,
        .y = 110u
    };
    AstraGuiWindowCommand resize = {
        .window = 4u,
        .action = ASTRA_GUI_WINDOW_RESIZE,
        .width = 580u,
        .height = 300u
    };
    AstraLogicalInputEvent motion = {
        .type = ASTRA_INPUT_EVENT_POINTER_MOTION,
        .timestamp_ms = 99u,
        .value_x = 210,
        .value_y = 170,
    };
    DisplayWindow closed;
    uint32_t effects = 0u;
    uint32_t frame_window = 0u;
    uint32_t frame_timestamp = 0u;
    uint32_t error;
    int changed;

    assert((uint64_t)(DISPLAY_MEDIA_LIMIT - DISPLAY_MEDIA_BASE) >=
           (uint64_t)ASTRA_DISPLAY_WIDTH *
               (DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP) * sizeof(uint16_t));

    {
        AstraRenderBuilder builder;
        DisplayWindow icon_window = {0};
        uint8_t palette[8] = {0, 0, 0, 0, 0x20, 0xc0, 0x40, 0xff};
        uint8_t pixels[16u * 16u];
        uint16_t icon_color = astra_surface_rgb565(0x20, 0xc0, 0x40);
        uint32_t destination;

        memset(pixels, 1, sizeof(pixels));
        icon_window.title_icon = (AstraAicon){
            palette, sizeof(palette), 2u, 0u, 0u, 0u, 0u};
        icon_window.title_icon_strike = (AstraAiconStrike){
            16u, 16u, pixels, sizeof(pixels)};
        assert(astra_render_builder_init(&builder, batch, sizeof(batch), 1u));
        destination = astra_render_builder_surface_at(
            &builder, DISPLAY_MEDIA_BASE, 64u * 32u * 2u, 64u, 32u);
        assert(destination != 0u);
        assert(draw_title_icon(&builder, destination, &icon_window, 13, 7));
        assert(astra_render_builder_finish(&builder) != 0u);
        assert(batch_has_fill(13, 7, 16u, 16u, icon_color));

        {
            uint8_t maximum[ASTRA_AICON_STRIKE_WIDTH_MAX *
                            ASTRA_AICON_STRIKE_WIDTH_MAX];

            for (uint32_t at = 0u; at < sizeof(maximum); ++at)
                maximum[at] = (uint8_t)((at %
                    ASTRA_AICON_STRIKE_WIDTH_MAX) % 2u == 0u);
            icon_window.title_icon_strike = (AstraAiconStrike){
                ASTRA_AICON_STRIKE_WIDTH_MAX,
                ASTRA_AICON_STRIKE_WIDTH_MAX, maximum, sizeof(maximum)};
            assert(astra_render_builder_init(
                &builder, batch, sizeof(batch), 2u));
            destination = astra_render_builder_surface_at(
                &builder, DISPLAY_MEDIA_BASE, 64u * 64u * 2u, 64u, 64u);
            assert(destination != 0u);
            assert(draw_title_icon(
                &builder, destination, &icon_window, 13, 7));
            assert(astra_render_builder_finish(&builder) != 0u);
            assert(batch_has_fill(13, 7, 1u,
                                  ASTRA_AICON_STRIKE_WIDTH_MAX, icon_color));
        }

        {
            uint8_t too_wide[ASTRA_AICON_STRIKE_WIDTH_MAX + 1u] = {0};

            icon_window.title_icon_strike = (AstraAiconStrike){
                ASTRA_AICON_STRIKE_WIDTH_MAX + 1u, 1u,
                too_wide, sizeof(too_wide)};
            assert(astra_render_builder_init(
                &builder, batch, sizeof(batch), 3u));
            destination = astra_render_builder_surface_at(
                &builder, DISPLAY_MEDIA_BASE, 64u * 64u * 2u, 64u, 64u);
            assert(destination != 0u);
            assert(!draw_title_icon(
                &builder, destination, &icon_window, 13, 7));
        }
    }

    {
        AstraRenderBuilder builder;
        AstraTheme bar_theme = ASTRA_THEME_SYSTEM_INIT;
        DisplayState bar = {
            .system_offset = {
                DISPLAY_MEDIA_BASE,
                DISPLAY_MEDIA_BASE +
                    ASTRA_DISPLAY_WIDTH * DISPLAY_WORK_TOP * 2u
            },
            .system_capacity = {
                ASTRA_DISPLAY_WIDTH * DISPLAY_WORK_TOP * 2u,
                ASTRA_DISPLAY_WIDTH *
                    (ASTRA_DISPLAY_HEIGHT - DISPLAY_WORK_BOTTOM) * 2u
            }
        };

        assert(astra_render_builder_init(&builder, batch, sizeof(batch), 1u));
        assert(build_system_surfaces(&builder, &bar, &bar_theme));
        assert(astra_render_builder_finish(&builder) != 0u);
        assert(builder.glyph_count == 26u); /* ASTRA, Workspace, File/Edit/View */
        assert(!batch_has_colored_fill_at(16, 17, color(bar_theme.accent)));
        assert(!batch_has_colored_fill_at(
            100, 14, color(bar_theme.title_inactive)));
        assert(!batch_has_colored_fill_at(
            20, DISPLAY_WORK_TOP - 1u, color(bar_theme.accent)));
        assert(batch_has_colored_fill_at(
            20, DISPLAY_WORK_TOP - 1u, color(bar_theme.frame)));

        bar.system_initialized = 1u;
        assert(astra_render_builder_init(&builder, batch, sizeof(batch), 2u));
        assert(build_system_surfaces(&builder, &bar, &bar_theme));
        assert(builder.command_count == 0u && builder.glyph_count == 0u);

        bar.overlay = DISPLAY_OVERLAY_MENU;
        bar.system_initialized = 0u;
        assert(astra_render_builder_init(&builder, batch, sizeof(batch), 3u));
        assert(build_system_surfaces(&builder, &bar, &bar_theme));
        assert(astra_render_builder_finish(&builder) != 0u);
        assert(!batch_has_colored_fill_at(16, 17, color(bar_theme.accent)));
        assert(!batch_has_colored_fill_at(
            20, DISPLAY_WORK_TOP - 1u, color(bar_theme.accent)));
    }

    {
        AstraRenderBuilder builder;
        AstraTheme menu_theme = ASTRA_THEME_SYSTEM_INIT;
        DisplayWindow active = {.id = 1u, .event_send = 0x500u,
            .request = {
            .type = ASTRA_WINDOW_DESKTOP,
            .event_mask = ASTRA_WINDOW_SUBSCRIBE_SYSTEM_ACTION,
            .flags = ASTRA_WINDOW_ACTIVE, .x = 700u, .y = 300u,
            .width = 200u, .height = 200u}};
        DisplayState menu = {.windows = &active, .count = 1u};
        AstraLogicalInputEvent down = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .code = ASTRA_INPUT_BUTTON_LEFT};
        AstraLogicalInputEvent up = down;
        AstraLogicalInputEvent motion = {
            .type = ASTRA_INPUT_EVENT_POINTER_MOTION};
        uint32_t effects = 0u;
        uint32_t frame_window = 0u;
        uint32_t frame_timestamp = 0u;

        up.flags = 0u;
        menu.pointer_x = 40;
        menu.pointer_y = 15;
        assert(handle_pointer(&menu, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(menu.overlay == DISPLAY_OVERLAY_MENU &&
               menu.swallow_pointer_up != 0u &&
               (effects & (DISPLAY_POINTER_RENDER | DISPLAY_POINTER_CURSOR)) ==
                   (DISPLAY_POINTER_RENDER | DISPLAY_POINTER_CURSOR));
        assert((active.request.flags & ASTRA_WINDOW_ACTIVE) != 0u &&
               menu.capture_window == 0u);
        assert(handle_pointer(&menu, &up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(menu.overlay == DISPLAY_OVERLAY_MENU &&
               menu.swallow_pointer_up == 0u);

        menu.overlay_offset = DISPLAY_MEDIA_BASE;
        menu.overlay_capacity = align_media_bytes(
            DISPLAY_MENU_WIDTH * DISPLAY_MENU_HEIGHT * 2u);
        assert(astra_render_builder_init(&builder, batch, sizeof(batch), 4u));
        assert(build_overlay_surface(&builder, &menu, &menu_theme));
        assert(astra_render_builder_finish(&builder) != 0u);
        assert(builder.glyph_count == 32u);
        assert(DISPLAY_MENU_X == 12);
        assert(batch_has_fill(0, 0, DISPLAY_MENU_WIDTH,
                              menu_theme.window_radius,
                              color(menu_theme.frame)));
        assert(batch_has_fill(12, 44, DISPLAY_MENU_WIDTH - 24u,
                              1u, color(menu_theme.border_soft)));
        assert(!batch_has_fill(12, 84, DISPLAY_MENU_WIDTH - 24u,
                               1u, color(menu_theme.border_soft)));
        assert(menu_item_at(DISPLAY_MENU_X + 8,
                            DISPLAY_MENU_Y + 8) == 1u);
        assert(menu_item_at(DISPLAY_MENU_X + 7,
                            DISPLAY_MENU_Y + 8) == 0u);
        assert(menu_item_at(DISPLAY_MENU_X + 24,
                            DISPLAY_MENU_Y + 44) == 0u);
        assert(menu_item_at(DISPLAY_MENU_X + 24,
                            DISPLAY_MENU_Y + 84) == 0u);

        motion.value_x = DISPLAY_MENU_X + 24;
        motion.value_y = DISPLAY_MENU_Y + 24;
        assert(handle_pointer(&menu, &motion, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(menu.overlay_hover != 0u);
        motion.value_x = DISPLAY_MENU_X + DISPLAY_MENU_WIDTH + 10;
        assert(handle_pointer(&menu, &motion, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(menu.overlay_hover == 0u);
        motion.value_x = DISPLAY_MENU_X + 24;
        assert(handle_pointer(&menu, &motion, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(menu.overlay_hover != 0u);
        delivered_count = 0u;
        assert(handle_pointer(&menu, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(menu.overlay == DISPLAY_OVERLAY_NONE);
        assert(delivered_count == 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_SYSTEM_ACTION &&
               delivered.event.data.system_action.action ==
                   ASTRA_SYSTEM_ACTION_ABOUT);
        assert(handle_pointer(&menu, &up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == 1u);
        menu.pointer_x = DISPLAY_MENU_X + 24;
        menu.pointer_y = DISPLAY_MENU_Y + 72;
        assert(menu_item_at(menu.pointer_x, menu.pointer_y) == 2u);
        assert(menu_item_at(DISPLAY_MENU_X - 1, menu.pointer_y) == 0u);
        menu.pointer_x = 40;
        menu.pointer_y = 15;
        assert(handle_pointer(&menu, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(handle_pointer(&menu, &up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        menu.pointer_x = DISPLAY_MENU_X + 24;
        menu.pointer_y = DISPLAY_MENU_Y + 72;
        delivered_count = 0u;
        assert(handle_pointer(&menu, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_SYSTEM_ACTION &&
               delivered.event.data.system_action.action ==
                   ASTRA_SYSTEM_ACTION_RESTART);
        assert(handle_pointer(&menu, &up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        menu.pointer_x = 40;
        menu.pointer_y = 15;
        assert(handle_pointer(&menu, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(handle_pointer(&menu, &up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        menu.pointer_x = DISPLAY_MENU_X + 24;
        menu.pointer_y = DISPLAY_MENU_Y + 116;
        assert(menu_item_at(menu.pointer_x, menu.pointer_y) == 3u);
        delivered_count = 0u;
        assert(handle_pointer(&menu, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_SYSTEM_ACTION &&
               delivered.event.data.system_action.action ==
                   ASTRA_SYSTEM_ACTION_SHUTDOWN);
        menu.pointer_x = 240;
        menu.pointer_y = 15;
        delivered_count = 0u;
        assert(handle_pointer(&menu, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(menu.overlay == DISPLAY_OVERLAY_NONE && delivered_count == 0u);
    }

    {
        DisplayWindow dialog_window = {.id = 2u, .generation = 1u,
            .request = {.type = ASTRA_WINDOW_DIALOG,
                        .flags = ASTRA_WINDOW_ACTIVE,
                        .x = 400u, .y = 250u,
                        .width = 420u, .height = 150u,
                        .gadgets = ASTRA_WINDOW_GADGET_CLOSE}};
        DisplayState dialog = {.windows = &dialog_window, .count = 1u,
                               .capacity = 1u, .pointer_x = 450,
                               .pointer_y = 330};
        AstraLogicalInputEvent down = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .code = ASTRA_INPUT_BUTTON_LEFT};
        AstraLogicalInputEvent up = down;
        uint32_t effects = 0u;
        uint32_t frame_window = 0u;
        uint32_t frame_timestamp = 0u;

        up.flags = 0u;
        assert(handle_pointer(&dialog, &down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(handle_pointer(&dialog, &up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert((effects & DISPLAY_POINTER_RENDER) == 0u &&
               dialog.damage[0].valid == 0u &&
               dialog.damage[1].valid == 0u);
    }

    {
        DisplayWindow desktop_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState desktop = {
            .windows = desktop_windows,
            .capacity = TEST_WINDOW_COUNT,
            .damage = {
                { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u },
                { 0, 0, ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT, 1u }
            }
        };
        AstraGuiOpenWindow open = {
            .header = {
                .total_size = sizeof(AstraGuiOpenWindow),
                .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                .protocol = ASTRA_GUI_PROTOCOL,
                .protocol_version = ASTRA_GUI_VERSION,
                .operation = ASTRA_GUI_OPEN_WINDOW,
                .transaction_id = 1u,
            },
            .y = DISPLAY_WORK_TOP,
            .width = ASTRA_DISPLAY_WIDTH,
            .height = DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP,
            .type = ASTRA_WINDOW_DESKTOP,
            .content_format = ASTRA_WINDOW_CONTENT_DRAW_LIST,
        };

        assert(valid_open(&open, sizeof(open), 3u));
        assert(window_gadgets(ASTRA_WINDOW_STANDARD) ==
               (ASTRA_WINDOW_GADGET_CLOSE |
                ASTRA_WINDOW_GADGET_MINIMIZE |
                ASTRA_WINDOW_GADGET_MAXIMIZE));
        assert(window_gadgets(ASTRA_WINDOW_DIALOG) ==
               ASTRA_WINDOW_GADGET_CLOSE);
        assert(window_gadgets(ASTRA_WINDOW_POPOVER) == 0u);
        {
            AstraGuiOpenWindow dialog_open = open;

            dialog_open.type = ASTRA_WINDOW_DIALOG;
            dialog_open.x = 430u;
            dialog_open.y = 250u;
            dialog_open.width = 420u;
            dialog_open.height = 150u;
            dialog_open.gadgets = ASTRA_WINDOW_GADGET_AUTO;
            assert(valid_open(&dialog_open, sizeof(dialog_open), 3u));
            dialog_open.gadgets = ASTRA_WINDOW_GADGET_MINIMIZE;
            assert(!valid_open(&dialog_open, sizeof(dialog_open), 3u));
        }
        {
            AstraGuiOpenWindow rgb_open = open;
            DisplayWindow rgb_window = {0};
            uint16_t pixel = 0u;

            rgb_open.type = ASTRA_WINDOW_DIALOG;
            rgb_open.x = 430u;
            rgb_open.y = 250u;
            rgb_open.width = 200u;
            rgb_open.height = 120u;
            rgb_open.pitch = 400u;
            rgb_open.content_format = ASTRA_WINDOW_CONTENT_RGB565;
            rgb_open.gadgets = ASTRA_WINDOW_GADGET_AUTO;
            assert(valid_open(&rgb_open, sizeof(rgb_open), 3u));
            rgb_open.pitch = 399u;
            assert(!valid_open(&rgb_open, sizeof(rgb_open), 3u));
            rgb_open.pitch = 398u;
            assert(!valid_open(&rgb_open, sizeof(rgb_open), 3u));
            rgb_open.pitch = 400u;
            rgb_window.request = rgb_open;
            rgb_window.surface.view.pixels = &pixel;
            rgb_window.surface.view.pitch = rgb_open.pitch;
            rgb_window.surface.view.byte_size = rgb_open.pitch *
                                                rgb_open.height;
            assert(frame_valid(&theme, &rgb_window, rgb_open.x, rgb_open.y,
                               180u, 100u));
            assert(!frame_valid(&theme, &rgb_window, rgb_open.x, rgb_open.y,
                                210u, 100u));
            assert(!frame_valid(&theme, &rgb_window, rgb_open.x, rgb_open.y,
                                180u, 121u));
        }
        {
            AstraGuiOpenWindow malformed = open;

            malformed.type = ASTRA_WINDOW_STANDARD;
            malformed.x = 100u;
            malformed.y = 100u;
            malformed.width = 200u;
            malformed.height = 120u;
            malformed.title_length = 2u;
            malformed.title[0] = (char)0xc0;
            malformed.title[1] = (char)0x80;
            assert(!valid_open(&malformed, sizeof(malformed), 3u));
        }
        {
            AstraGuiWindowCommand malformed = {
                .header = {
                    .total_size = sizeof(AstraGuiWindowCommand),
                    .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                    .protocol = ASTRA_GUI_PROTOCOL,
                    .protocol_version = ASTRA_GUI_VERSION,
                    .operation = ASTRA_GUI_WINDOW_COMMAND,
                    .transaction_id = 1u,
                },
                .window = 1u,
                .generation = 1u,
                .action = ASTRA_GUI_WINDOW_SET_TITLE,
                .title_length = 3u,
                .title = {(char)0xed, (char)0xa0, (char)0x80},
            };

            assert(!valid_command(&malformed, sizeof(malformed), 1u, 1u,
                                  200u, 120u));
            malformed.action = ASTRA_GUI_WINDOW_SET_APPLICATION_NAME;
            assert(!valid_command(&malformed, sizeof(malformed), 1u, 1u,
                                  200u, 120u));
            malformed.title_length = 0u;
            assert(!valid_command(&malformed, sizeof(malformed), 1u, 1u,
                                  200u, 120u));
            malformed.title_length = 7u;
            memcpy(malformed.title, "Gallery", 7u);
            assert(valid_command(&malformed, sizeof(malformed), 1u, 1u,
                                 200u, 120u));
            malformed.header.protocol_version = 12u;
            assert(!valid_command(&malformed, sizeof(malformed), 1u, 1u,
                                  200u, 120u));
        }
        add_window(&desktop, 0u, ASTRA_WINDOW_DESKTOP, 0u,
                   DISPLAY_WORK_TOP, ASTRA_DISPLAY_WIDTH,
                   DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP, 0u, 0u);
        desktop.windows[0].request.title_length = 0u;
        desktop.windows[0].request.event_mask = 0u;
        assert(astra_draw_list_view_init(
            &desktop.windows[0].surface.view, lists[0], sizeof(lists[0]),
            ASTRA_DISPLAY_WIDTH, DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP));
        astra_surface_clear(&desktop.windows[0].surface.view,
                            color(theme.canvas));
        for (uint32_t index = 0u; index < 126u; ++index)
            astra_surface_fill(&desktop.windows[0].surface.view,
                               20 + (int32_t)(index % 32u),
                               20 + (int32_t)(index / 32u), 1u, 1u,
                               color(theme.accent));
        label(&desktop.windows[0].surface.view, 40, 104,
              "Terminal", 8u, color(theme.text_primary));
        assert(valid_batch(compose(batch, 1u, &desktop, &error, NULL)) != 0u);
        assert(error == ASTRA_STATUS_OK);
        commit_render_state(&desktop);
        set_overlay(&desktop, DISPLAY_OVERLAY_MENU);
        {
            uint32_t failure = 0u;
            uint32_t bytes = compose(batch, 2u, &desktop, &error, &failure);

            assert(valid_batch(bytes) != 0u);
            assert(error == ASTRA_STATUS_OK &&
                   failure == ASTRA_RENDER_BUILDER_FAILURE_NONE);
        }
        assert(read_be32(batch_scene() + 20u) == 4u);
        assert(batch_scene_layer_source(3u) == desktop.overlay_offset);
        assert(!media_extents_overlap(
            desktop.scene_offset[desktop.scene_pending],
            desktop.scene_capacity[desktop.scene_pending],
            desktop.overlay_offset, desktop.overlay_capacity));
        commit_render_state(&desktop);
        set_overlay(&desktop, DISPLAY_OVERLAY_NONE);
        assert(valid_batch(compose(batch, 3u, &desktop, &error, NULL)) != 0u);
        assert(read_be32(batch_scene() + 20u) == 3u);
    }

    {
        DisplayWindow desktop_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState desktop = {
            .windows = desktop_windows,
            .capacity = TEST_WINDOW_COUNT,
        };
        AstraLogicalInputEvent down = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .timestamp_ms = 1u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        uint32_t desktop_effects = 0u;
        uint32_t desktop_frame = 0u;
        uint32_t desktop_timestamp = 0u;

        add_window(&desktop, 0u, ASTRA_WINDOW_DESKTOP, 0u,
                   DISPLAY_WORK_TOP, ASTRA_DISPLAY_WIDTH,
                   DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP, 0u, 0u);
        desktop.damage[0] = (DamageRect){0};
        desktop.damage[1] = (DamageRect){0};
        desktop.pointer_x = 70;
        desktop.pointer_y = 90;
        assert(handle_pointer(&desktop, &down, &desktop_effects,
                              &desktop_frame, &desktop_timestamp) ==
               ASTRA_STATUS_OK);
        assert((desktop_effects & DISPLAY_POINTER_RENDER) == 0u);
    }

    {
        DisplayWindow fair_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState fair = {
            .windows = fair_windows,
            .capacity = TEST_WINDOW_COUNT,
            .count = 2u,
        };
        uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t sources[ASTRA_WAIT_MULTIPLE_MAX];

        fair.windows[0].control_receive = 0x30u;
        fair.windows[1].control_receive = 0x40u;
        assert(display_wait_handles(&fair, 0x10u, 0x20u, 0x25u, 3u,
                                    waits, sources) == 5u);
        assert(waits[0] == 0x30u && sources[0] == 3u);
        assert(waits[1] == 0x40u && sources[1] == 4u);
        assert(waits[2] == 0x10u && sources[2] == 0u);
        assert(waits[3] == 0x20u && sources[3] == 1u);
        assert(waits[4] == 0x25u && sources[4] == 2u);
        assert(display_window_wait_index(0u, fair.count) == fair.count);
        assert(display_window_wait_index(1u, fair.count) == fair.count);
        assert(display_window_wait_index(2u, fair.count) == fair.count);
        assert(display_window_wait_index(3u, fair.count) == 0u);
        assert(display_window_wait_index(4u, fair.count) == 1u);
        assert(display_window_wait_index(5u, fair.count) == fair.count);
        /* A client's dead port closes its window; the display's own
         * sources ending is still fatal. */
        assert(display_wait_client_ended(&fair, 3u, ASTRA_SYSCALL_PEER_DEAD));
        assert(display_wait_client_ended(&fair, 4u, ASTRA_SYSCALL_PEER_DEAD));
        assert(!display_wait_client_ended(&fair, 4u, ASTRA_SYSCALL_CLOSED));
        assert(!display_wait_client_ended(&fair, 3u,
                                          ASTRA_SYSCALL_INVALID_HANDLE));
        assert(!display_wait_client_ended(&fair, 0u, ASTRA_SYSCALL_PEER_DEAD));
        assert(!display_wait_client_ended(&fair, 1u, ASTRA_SYSCALL_PEER_DEAD));
        assert(!display_wait_client_ended(&fair, 2u, ASTRA_SYSCALL_PEER_DEAD));
        fair.pending_input_valid = 1u;
        assert(display_wait_client_ended(&fair, 1u, ASTRA_SYSCALL_PEER_DEAD));
        fair.pending_input_valid = 0u;
        /* GUI sessions wait after the windows; a session's dead port is
           its client's, like a window's. */
        const uint32_t rights =
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP;

        assert(astra_rt_area_create(4096u, rights,
                                    &fair.session_receive[0]) ==
                   ASTRA_SYSCALL_OK &&
               astra_rt_area_create(4096u, rights,
                                    &fair.session_receive[1]) ==
                   ASTRA_SYSCALL_OK);
        fair.session_id[0] = 7u;
        fair.session_id[1] = 9u;
        fair.session_count = 2u;
        assert(display_wait_handles(&fair, 0x10u, 0x20u, 0x25u, 0u,
                                    waits, sources) == 7u);
        assert(waits[3] == 0x30u && waits[4] == 0x40u &&
               waits[5] == fair.session_receive[0] && sources[5] == 5u &&
               waits[6] == fair.session_receive[1] && sources[6] == 6u);
        assert(display_window_wait_index(5u, fair.count) == fair.count);
        assert(display_wait_client_ended(&fair, 6u, ASTRA_SYSCALL_PEER_DEAD));
        {
            uint32_t second = fair.session_receive[1];

            close_session(&fair, 0u);
            assert(fair.session_count == 1u &&
                   fair.session_receive[0] == second &&
                   fair.session_id[0] == 9u);
            close_session(&fair, 0u);
            assert(fair.session_count == 0u);
        }
        fair.session_count = ASTRA_WAIT_MULTIPLE_MAX - 4u;
        assert(display_wait_handles(&fair, 0x10u, 0x20u, 0x25u, 0u,
                                    waits, sources) == 0u);
        fair.session_count = 0u;
        fair.count = ASTRA_WAIT_MULTIPLE_MAX - 2u;
        assert(display_wait_handles(&fair, 0x10u, 0x20u, 0x25u, 0u,
                                    waits, sources) == 0u);
    }

    add_window(&state, 0u, ASTRA_WINDOW_POPOVER, 300u, 380u,
               250u, 125u, 0u, 0u);
    assert(valid_batch(compose(batch, 1u, &state, &error, NULL)) != 0u);
    commit_render_state(&state);
    state.damage[1] = (DamageRect){0};
    add_window(&state, 1u, ASTRA_WINDOW_UTILITY, 600u, 80u,
               360u, 145u, 0u, ASTRA_WINDOW_GADGET_CLOSE);
    damage_window(&state, &theme, &state.windows[1]);
    assert(valid_batch(compose(batch, 2u, &state, &error, NULL)) != 0u);
    commit_render_state(&state);
    state.damage[0] = (DamageRect){0};
    add_window(&state, 2u, ASTRA_WINDOW_DIALOG, 520u, 300u,
               400u, 190u, ASTRA_WINDOW_MODAL, ASTRA_WINDOW_GADGET_CLOSE);
    damage_window(&state, &theme, &state.windows[2]);
    assert(valid_batch(compose(batch, 3u, &state, &error, NULL)) != 0u);
    commit_render_state(&state);
    state.damage[1] = (DamageRect){0};
    add_window(&state, 3u, ASTRA_WINDOW_STANDARD, 100u, 100u,
               550u, 280u, ASTRA_WINDOW_ACTIVE | ASTRA_WINDOW_RESIZABLE,
               ASTRA_WINDOW_GADGET_CLOSE | ASTRA_WINDOW_GADGET_MINIMIZE |
                   ASTRA_WINDOW_GADGET_MAXIMIZE);
    damage_window(&state, &theme, &state.windows[3]);
    assert(valid_batch(compose(batch, 4u, &state, &error, NULL)) != 0u);
    commit_render_state(&state);
    state.damage[0] = (DamageRect){0};
    assert(apply_command(&state, &theme, &resize, &closed, &changed) ==
           ASTRA_STATUS_OK && changed);
    assert(valid_batch(compose(batch, 5u, &state, &error, NULL)) != 0u);
    assert(batch_has_surface_fill(580u, 300u, color(theme.client)));
    commit_render_state(&state);
    assert(apply_command(&state, &theme, &move, &closed, &changed) ==
           ASTRA_STATUS_OK && changed);
    state.pointer_x = 321;
    state.pointer_y = 654;
    assert(valid_batch(compose(batch, 6u, &state, &error, NULL)) != 0u);
    assert(read_be32(batch + 12u) == 0u);
    assert(state.scene_pending == (state.scene_active ^ 1u));
    assert(!media_extents_overlap(
        state.scene_offset[state.scene_pending],
        state.scene_capacity[state.scene_pending],
        state.scene_offset[state.scene_active],
        state.scene_capacity[state.scene_active]));
    /* The cursor is never in a frame: it is posted on its own. */
    for (uint32_t offset = 32u; offset < 48u; offset += 4u)
        assert(read_be32(batch + offset) == 0u);
    state.capture_window = 4u;
    state.capture_region = HIT_TITLE;
    state.capture_dx = 10;
    state.capture_dy = 10;
    motion.size = sizeof(motion);
    motion.version = ASTRA_INPUT_SERVICE_VERSION;
    astra_message_header_set(
        &input_messages[0].header, sizeof(AstraInputEventMessage),
        ASTRA_INPUT_SERVICE_PROTOCOL, ASTRA_INPUT_SERVICE_VERSION,
        ASTRA_INPUT_OPERATION_EVENT, 1u);
    input_messages[0].event = motion;
    motion.timestamp_ms = 100u;
    motion.value_x = 220;
    motion.value_y = 175;
    astra_message_header_set(
        &input_messages[1].header, sizeof(AstraInputEventMessage),
        ASTRA_INPUT_SERVICE_PROTOCOL, ASTRA_INPUT_SERVICE_VERSION,
        ASTRA_INPUT_OPERATION_EVENT, 2u);
    input_messages[1].event = motion;
    input_message_count = 2u;
    input_message_index = 0u;
    assert(drain_input(0x600u, &state, &effects, &frame_window,
                       &frame_timestamp) == ASTRA_STATUS_OK);
    assert(input_message_index == input_message_count);
    assert(state.windows[3].request.x == 210u &&
           state.windows[3].request.y == 165u);
    assert((effects & (DISPLAY_POINTER_CURSOR | DISPLAY_POINTER_RENDER |
                       DISPLAY_POINTER_FRAME)) ==
           (DISPLAY_POINTER_CURSOR | DISPLAY_POINTER_RENDER |
            DISPLAY_POINTER_FRAME));
    assert((effects & DISPLAY_POINTER_RESIZE) == 0u);
    assert(frame_window == 4u && frame_timestamp == 100u);
    state.capture_window = 0u;
    state.capture_region = HIT_NONE;
    pointer_event(&state.windows[3], &theme,
                  ASTRA_WINDOW_EVENT_POINTER_MOTION, 0u, 77u,
                  180, 190, 0u, 2u,
                  ASTRA_INPUT_MOD_LEFT_SHIFT | ASTRA_INPUT_MOD_META);
    assert(delivered.event.type == ASTRA_WINDOW_EVENT_POINTER_MOTION);
    assert(delivered.event.data.pointer.screen_x == 180 &&
           delivered.event.data.pointer.screen_y == 190);
    assert(delivered.event.data.pointer.x ==
           180 - state.windows[3].request.x -
                 frame_width(&theme, state.windows[3].request.type));
    assert(delivered.event.data.pointer.y ==
           190 - state.windows[3].request.y -
                 frame_width(&theme, state.windows[3].request.type) -
                 title_height(&theme, state.windows[3].request.type) -
                 theme.signal_height);
    assert(delivered.event.data.pointer.click_count == 2u);
    assert(delivered.event.data.pointer.modifiers ==
           (ASTRA_INPUT_MOD_LEFT_SHIFT | ASTRA_INPUT_MOD_META));
    state.pointer_x = 100;
    state.pointer_y = 100;
    assert(register_click(&state, 4u, ASTRA_INPUT_BUTTON_LEFT, 1000u) == 1u);
    state.pointer_x = 104;
    state.pointer_y = 97;
    assert(register_click(&state, 4u, ASTRA_INPUT_BUTTON_LEFT, 1499u) == 2u);
    assert(register_click(&state, 4u, ASTRA_INPUT_BUTTON_LEFT, 2000u) == 1u);

    {
        DisplayWindow *window = &state.windows[3];
        AstraLogicalInputEvent resize_down = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .timestamp_ms = 101u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        AstraLogicalInputEvent resize_motion = {
            .type = ASTRA_INPUT_EVENT_POINTER_MOTION,
            .timestamp_ms = 102u,
        };
        AstraLogicalInputEvent resize_up = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .timestamp_ms = 103u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        uint16_t old_width = window->request.width;
        uint16_t old_height = window->request.height;

        effects = 0u;
        state.pointer_x = window->request.x +
                          (int32_t)outer_width(&theme, window) -
                          theme.resize_hit;
        state.pointer_y = window->request.y +
                          (int32_t)outer_height(&theme, window) -
                          theme.resize_hit;
        assert(hit_region(&theme, window, state.pointer_x,
                          state.pointer_y) == HIT_RESIZE_SE);
        assert(handle_pointer(&state, &resize_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(state.capture_window == window->id &&
               state.capture_region == HIT_RESIZE_SE);
        resize_motion.value_x = state.pointer_x + 12;
        resize_motion.value_y = state.pointer_y + 7;
        assert(handle_pointer(&state, &resize_motion, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->request.width == old_width + 12u &&
               window->request.height == old_height + 7u);
        assert((effects & (DISPLAY_POINTER_FRAME |
                           DISPLAY_POINTER_RESIZE)) == 0u);
        assert(handle_pointer(&state, &resize_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(state.capture_window == 0u &&
               (effects & (DISPLAY_POINTER_FRAME |
                           DISPLAY_POINTER_RESIZE)) ==
                   (DISPLAY_POINTER_FRAME | DISPLAY_POINTER_RESIZE) &&
               frame_window == window->id && frame_timestamp == 103u);

        effects = 0u;
        state.pointer_x = window->request.x +
                          (int32_t)outer_width(&theme, window) -
                          theme.resize_hit;
        state.pointer_y = window->request.y +
                          (int32_t)outer_height(&theme, window) -
                          theme.resize_hit;
        assert(hit_region(&theme, window, state.pointer_x,
                          state.pointer_y) == HIT_RESIZE_SE);
        assert(handle_pointer(&state, &resize_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        effects = 0u;
        assert(handle_pointer(&state, &resize_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert((effects & (DISPLAY_POINTER_FRAME |
                           DISPLAY_POINTER_RESIZE)) == 0u);
    }

    {
        DisplayWindow *window = &state.windows[3];
        uint32_t width = outer_width(&theme, window);
        uint32_t height = outer_height(&theme, window);
        const struct {
            int32_t x;
            int32_t y;
            uint32_t region;
        } edges[] = {
            { (int32_t)width / 2, 1, HIT_RESIZE_N },
            { (int32_t)width - theme.resize_hit, theme.resize_hit - 1,
              HIT_RESIZE_NE },
            { (int32_t)width - 1, (int32_t)height / 2, HIT_RESIZE_E },
            { (int32_t)width - theme.resize_hit,
              (int32_t)height - theme.resize_hit, HIT_RESIZE_SE },
            { (int32_t)width / 2, (int32_t)height - 1, HIT_RESIZE_S },
            { theme.resize_hit - 1, (int32_t)height - theme.resize_hit,
              HIT_RESIZE_SW },
            { 1, (int32_t)height / 2, HIT_RESIZE_W },
            { theme.resize_hit - 1, theme.resize_hit - 1, HIT_RESIZE_NW },
        };

        for (uint32_t index = 0u;
             index < sizeof(edges) / sizeof(edges[0]); ++index) {
            assert(hit_region(&theme, window,
                              window->request.x + edges[index].x,
                              window->request.y + edges[index].y) ==
                   edges[index].region);
            state.pointer_x = window->request.x + edges[index].x;
            state.pointer_y = window->request.y + edges[index].y;
            assert(display_pointer_shape(&state, &theme) ==
                   (edges[index].region == HIT_RESIZE_E ||
                    edges[index].region == HIT_RESIZE_W ?
                        ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL :
                    edges[index].region == HIT_RESIZE_N ||
                    edges[index].region == HIT_RESIZE_S ?
                        ASTRA_POINTER_SHAPE_RESIZE_VERTICAL :
                    edges[index].region == HIT_RESIZE_NW ||
                    edges[index].region == HIT_RESIZE_SE ?
                        ASTRA_POINTER_SHAPE_RESIZE_NW_SE :
                        ASTRA_POINTER_SHAPE_RESIZE_NE_SW));
        }

        window->request.flags &= ~ASTRA_WINDOW_RESIZABLE;
        state.pointer_x = window->request.x + (int32_t)width - 1;
        state.pointer_y = window->request.y + (int32_t)height / 2;
        assert(display_pointer_shape(&state, &theme) ==
               ASTRA_POINTER_SHAPE_DEFAULT);
        window->request.flags |= ASTRA_WINDOW_RESIZABLE;
    }

    {
        DisplayWindow *window = &state.windows[3];
        AstraLogicalInputEvent wheel = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .timestamp_ms = 103u,
            .code = ASTRA_INPUT_BUTTON_WHEEL_UP,
            .modifiers = ASTRA_INPUT_MOD_LEFT_ALT | ASTRA_INPUT_MOD_META,
        };
        uint32_t before = delivered_count;

        state.pointer_x = window->request.x + 40;
        state.pointer_y = window->request.y + 80;
        assert(handle_pointer(&state, &wheel, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == before + 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_POINTER_WHEEL &&
               delivered.event.data.wheel.delta_x == 0 &&
               delivered.event.data.wheel.delta_y == 1 &&
               delivered.event.data.wheel.modifiers ==
                   (ASTRA_INPUT_MOD_LEFT_ALT | ASTRA_INPUT_MOD_META) &&
               delivered.event.data.wheel.screen_x == state.pointer_x &&
               delivered.event.data.wheel.screen_y == state.pointer_y);
    }

    {
        AstraLogicalInputEvent key = {
            .type = ASTRA_INPUT_EVENT_KEY,
            .flags = ASTRA_INPUT_LOGICAL_DOWN | ASTRA_INPUT_LOGICAL_REPEAT,
            .timestamp_ms = 104u,
            .code = 0x04u,
            .modifiers = ASTRA_INPUT_MOD_LEFT_SHIFT,
        };
        AstraLogicalInputEvent text_event = {
            .type = ASTRA_INPUT_EVENT_TEXT,
            .timestamp_ms = 105u,
            .code = 'A',
            .modifiers = ASTRA_INPUT_MOD_LEFT_SHIFT,
        };
        uint32_t before = delivered_count;

        assert(handle_pointer(&state, &key, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == before + 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_KEY &&
               delivered.event.data.key.usage == 0x04u &&
               delivered.event.data.key.modifiers ==
                   ASTRA_INPUT_MOD_LEFT_SHIFT &&
               (delivered.event.flags & (ASTRA_WINDOW_EVENT_DOWN |
                                          ASTRA_WINDOW_EVENT_REPEAT)) ==
                   (ASTRA_WINDOW_EVENT_DOWN | ASTRA_WINDOW_EVENT_REPEAT));
        assert(handle_pointer(&state, &text_event, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == before + 2u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_TEXT &&
               delivered.event.data.text.codepoint == 'A' &&
               delivered.event.data.text.modifiers ==
                   ASTRA_INPUT_MOD_LEFT_SHIFT);
        text_event.code = 0xd800u;
        assert(handle_pointer(&state, &text_event, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_INVALID);
        assert(delivered_count == before + 2u);

        send_would_block = 1u;
        wait_count = 0u;
        assert(handle_pointer(&state, &key, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(delivered_count == before + 2u && wait_count == 0u &&
               state.pending_input_valid != 0u &&
               state.pending_input_window == state.windows[3].id &&
               state.windows[3].event_lost == 0u);
        retry_pending_input(&state);
        assert(delivered_count == before + 3u &&
               state.pending_input_valid == 0u &&
               state.windows[3].event_lost == 0u);
    }
    {
        DisplayWindow *window = &state.windows[3];
        AstraLogicalInputEvent blocked_motion = {
            .type = ASTRA_INPUT_EVENT_POINTER_MOTION,
            .timestamp_ms = 106u,
            .value_x = window->request.x + frame_width(&theme,
                window->request.type) + 40,
            .value_y = window->request.y + frame_width(&theme,
                window->request.type) + title_height(
                    &theme, window->request.type) + theme.signal_height + 40,
        };
        uint32_t before = delivered_count;

        effects = 0u;
        send_would_block = 1u;
        wait_count = 0u;
        assert(handle_pointer(&state, &blocked_motion, &effects,
                              &frame_window, &frame_timestamp) ==
               ASTRA_STATUS_OK);
        assert(state.pointer_x == blocked_motion.value_x &&
               state.pointer_y == blocked_motion.value_y &&
               (effects & DISPLAY_POINTER_CURSOR) != 0u &&
               delivered_count == before && window->event_lost != 0u &&
               wait_count == 0u);
        ++blocked_motion.timestamp_ms;
        ++blocked_motion.value_x;
        assert(handle_pointer(&state, &blocked_motion, &effects,
                              &frame_window, &frame_timestamp) ==
               ASTRA_STATUS_OK);
        assert(delivered_count == before + 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_POINTER_MOTION &&
               (delivered.event.flags & ASTRA_WINDOW_EVENT_LOSS) != 0u &&
               window->event_lost == 0u && wait_count == 0u);
    }
    {
        AstraGuiWindowCommand present = {
            .header = {
                .total_size = sizeof(AstraGuiWindowCommand),
                .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                .protocol = ASTRA_GUI_PROTOCOL,
                .protocol_version = ASTRA_GUI_VERSION,
                .operation = ASTRA_GUI_WINDOW_COMMAND,
                .transaction_id = 1u,
            },
            .window = state.windows[3].id,
            .generation = state.windows[3].generation,
            .action = ASTRA_GUI_WINDOW_PRESENT,
        };
        uint32_t generation = state.windows[3].generation;

        state.windows[3].cache_dirty[0] = 0u;
        state.windows[3].cache_dirty[1] = 0u;
        assert(apply_command(&state, &theme, &present, &closed, &changed) ==
               ASTRA_STATUS_OK && changed);
        assert(state.windows[3].cache_dirty[0] == 0u &&
               state.windows[3].cache_dirty[1] == 0u &&
               state.windows[3].content_dirty != 0u &&
               state.windows[3].content_damage.left == 0 &&
               state.windows[3].content_damage.top == 0 &&
               state.windows[3].content_damage.right ==
                   state.windows[3].request.width &&
               state.windows[3].content_damage.bottom ==
                   state.windows[3].request.height &&
               state.windows[3].generation == generation + 1u);
    }
    {
        AstraGuiWindowCommand present = {
            .header = {
                .total_size = sizeof(AstraGuiWindowCommand),
                .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                .protocol = ASTRA_GUI_PROTOCOL,
                .protocol_version = ASTRA_GUI_VERSION,
                .operation = ASTRA_GUI_WINDOW_COMMAND,
                .transaction_id = 1u,
            },
            .window = state.windows[3].id,
            .generation = state.windows[3].generation,
            .action = ASTRA_GUI_WINDOW_PRESENT,
            .x = 12u,
            .y = 18u,
            .width = 20u,
            .height = 14u,
        };

        state.windows[3].content_dirty = 0u;
        state.windows[3].content_damage = (DamageRect){0};
        assert(valid_command(&present, sizeof(present), 1u,
                             state.windows[3].id,
                             state.windows[3].request.width,
                             state.windows[3].request.height));
        assert(apply_command(&state, &theme, &present, &closed, &changed) ==
               ASTRA_STATUS_OK && changed);
        assert(state.windows[3].content_damage.left == 12 &&
               state.windows[3].content_damage.top == 18 &&
               state.windows[3].content_damage.right == 32 &&
               state.windows[3].content_damage.bottom == 32);
        present.x = state.windows[3].request.width - 10u;
        assert(!valid_command(&present, sizeof(present), 1u,
                              state.windows[3].id,
                              state.windows[3].request.width,
                              state.windows[3].request.height));
    }
    {
        AstraGuiWindowCommand close = {
            .header = {
                .total_size = sizeof(AstraGuiWindowCommand),
                .header_size = ASTRA_MESSAGE_HEADER_SIZE,
                .protocol = ASTRA_GUI_PROTOCOL,
                .protocol_version = ASTRA_GUI_VERSION,
                .operation = ASTRA_GUI_WINDOW_COMMAND,
                .transaction_id = 1u,
            },
            .window = state.windows[3].id,
            .generation = state.windows[3].generation,
            .action = ASTRA_GUI_WINDOW_CLOSE,
        };

        assert(valid_command(&close, sizeof(close), 0u,
                             state.windows[3].id,
                             state.windows[3].request.width,
                             state.windows[3].request.height));
        assert(!valid_command(&close, sizeof(close), 1u,
                              state.windows[3].id,
                              state.windows[3].request.width,
                              state.windows[3].request.height));
    }
    {
        DisplayWindow *window = &state.windows[3];
        AstraLogicalInputEvent button_down = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .flags = ASTRA_INPUT_LOGICAL_DOWN,
            .timestamp_ms = 106u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        AstraLogicalInputEvent button_up = {
            .type = ASTRA_INPUT_EVENT_POINTER_BUTTON,
            .timestamp_ms = 107u,
            .code = ASTRA_INPUT_BUTTON_LEFT,
        };
        uint16_t restore_x = window->request.x;
        uint16_t restore_y = window->request.y;
        uint16_t restore_width = window->request.width;
        uint16_t restore_height = window->request.height;

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent * 3u +
            theme.gadget_extent + theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        assert(hit_region(&theme, window, state.pointer_x,
                          state.pointer_y) == HIT_MAXIMIZE);
        assert(handle_pointer(&state, &button_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->request.maximize_state == ASTRA_GADGET_PRESSED);
        assert(handle_pointer(&state, &button_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->state == ASTRA_WINDOW_STATE_MAXIMIZED &&
               window->request.maximize_state == ASTRA_GADGET_NORMAL);

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent * 3u +
            theme.gadget_extent + theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        assert(handle_pointer(&state, &button_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(handle_pointer(&state, &button_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->state == ASTRA_WINDOW_STATE_NORMAL &&
               window->request.x == restore_x &&
               window->request.y == restore_y &&
               window->request.width == restore_width &&
               window->request.height == restore_height);

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent * 3u +
            theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        assert(handle_pointer(&state, &button_down, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(handle_pointer(&state, &button_up, &effects, &frame_window,
                              &frame_timestamp) == ASTRA_STATUS_OK);
        assert(window->state == ASTRA_WINDOW_STATE_MINIMIZED);
        {
            AstraGuiWindowCommand activate = {
                .window = window->id,
                .generation = window->generation,
                .action = ASTRA_GUI_WINDOW_ACTIVATE,
            };

            assert(apply_command(&state, &theme, &activate, &closed,
                                 &changed) == ASTRA_STATUS_OK && changed);
            assert(window->state == ASTRA_WINDOW_STATE_NORMAL &&
                   (window->request.flags & ASTRA_WINDOW_ACTIVE) != 0u);
        }
        {
            /* Fullscreen takes the whole display without chrome and
               RESTORE returns the window's own type, state and frame,
               from normal and from maximized. */
            uint32_t id = window->id;
            uint8_t type = window->request.type;
            AstraGuiWindowCommand fullscreen = {
                .window = id, .action = ASTRA_GUI_WINDOW_FULLSCREEN,
            };
            AstraGuiWindowCommand restore = {
                .window = id, .action = ASTRA_GUI_WINDOW_RESTORE,
            };
            AstraGuiWindowCommand maximize = {
                .window = id, .action = ASTRA_GUI_WINDOW_MAXIMIZE,
            };
            AstraGuiWindowCommand frame = {
                .window = id, .action = ASTRA_GUI_WINDOW_SET_FRAME,
                .x = 0u, .y = 0u, .width = 640u, .height = 480u,
            };
            DisplayWindow *current;

            assert(type == ASTRA_WINDOW_STANDARD);
            assert(apply_command(&state, &theme, &fullscreen, &closed,
                                 &changed) == ASTRA_STATUS_OK && changed);
            current = &state.windows[find_id(&state, id)];
            assert(find_id(&state, id) == state.count - 1u);
            assert(current->state == ASTRA_WINDOW_STATE_FULLSCREEN &&
                   current->request.type == ASTRA_WINDOW_FULLSCREEN &&
                   current->request.x == 0u && current->request.y == 0u &&
                   current->request.width == ASTRA_DISPLAY_WIDTH &&
                   current->request.height == ASTRA_DISPLAY_HEIGHT &&
                   outer_width(&theme, current) == ASTRA_DISPLAY_WIDTH &&
                   outer_height(&theme, current) == ASTRA_DISPLAY_HEIGHT);
            assert(hit_region(&theme, current, 5, 5) == HIT_CONTENT);
            assert(apply_command(&state, &theme, &fullscreen, &closed,
                                 &changed) == ASTRA_STATUS_OK && !changed);
            assert(apply_command(&state, &theme, &maximize, &closed,
                                 &changed) == ASTRA_STATUS_INVALID);
            assert(apply_command(&state, &theme, &frame, &closed,
                                 &changed) == ASTRA_STATUS_INVALID);
            assert(apply_command(&state, &theme, &restore, &closed,
                                 &changed) == ASTRA_STATUS_OK && changed);
            current = &state.windows[find_id(&state, id)];
            assert(current->state == ASTRA_WINDOW_STATE_NORMAL &&
                   current->request.type == type &&
                   current->request.x == restore_x &&
                   current->request.y == restore_y &&
                   current->request.width == restore_width &&
                   current->request.height == restore_height);

            assert(apply_command(&state, &theme, &maximize, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            assert(apply_command(&state, &theme, &fullscreen, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            assert(apply_command(&state, &theme, &restore, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            current = &state.windows[find_id(&state, id)];
            assert(current->state == ASTRA_WINDOW_STATE_MAXIMIZED &&
                   current->request.type == type &&
                   current->request.y == DISPLAY_WORK_TOP);
            assert(apply_command(&state, &theme, &restore, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            current = &state.windows[find_id(&state, id)];
            assert(current->state == ASTRA_WINDOW_STATE_NORMAL &&
                   current->request.x == restore_x &&
                   current->request.width == restore_width);
            {
                /* Ctrl+GUI+F takes the active window full screen and back,
                   whatever its program offers; the key goes no further. */
                AstraLogicalInputEvent chord = {
                    .type = ASTRA_INPUT_EVENT_KEY,
                    .flags = ASTRA_INPUT_LOGICAL_DOWN,
                    .code = DISPLAY_FULLSCREEN_KEY,
                    .modifiers = ASTRA_INPUT_MOD_LEFT_CTRL |
                                 ASTRA_INPUT_MOD_RIGHT_GUI,
                };
                uint32_t chord_effects = 0u;
                uint32_t chord_window = 0u;
                uint32_t chord_time = 0u;

                assert((current->request.flags & ASTRA_WINDOW_ACTIVE) != 0u);
                assert(handle_pointer(&state, &chord, &chord_effects,
                                      &chord_window, &chord_time) ==
                       ASTRA_STATUS_OK);
                current = &state.windows[find_id(&state, id)];
                assert(current->state == ASTRA_WINDOW_STATE_FULLSCREEN &&
                       chord_window == id &&
                       (chord_effects & DISPLAY_POINTER_RESIZE) != 0u);
                chord.flags = 0u; /* release: nothing */
                assert(handle_pointer(&state, &chord, &chord_effects,
                                      &chord_window, &chord_time) ==
                       ASTRA_STATUS_OK);
                current = &state.windows[find_id(&state, id)];
                assert(current->state == ASTRA_WINDOW_STATE_FULLSCREEN);
                chord.flags = ASTRA_INPUT_LOGICAL_DOWN;
                assert(handle_pointer(&state, &chord, &chord_effects,
                                      &chord_window, &chord_time) ==
                       ASTRA_STATUS_OK);
                current = &state.windows[find_id(&state, id)];
                assert(current->state == ASTRA_WINDOW_STATE_NORMAL &&
                       current->request.x == restore_x &&
                       current->request.width == restore_width);
                /* Without GUI it is the program's own key. */
                chord.modifiers = ASTRA_INPUT_MOD_LEFT_CTRL;
                assert(!fullscreen_chord(&chord));
            }
            window = current;
        }

        state.pointer_x = window->request.x + frame_width(&theme,
            window->request.type) + window->request.width -
            theme.spacing_unit - theme.gadget_extent / 2u;
        state.pointer_y = window->request.y + frame_width(&theme,
            window->request.type) +
            (title_height(&theme, window->request.type) -
             theme.gadget_extent) / 2u + theme.gadget_extent / 2u;
        {
            uint32_t before = delivered_count;

            send_would_block = 1u;
            wait_count = 0u;
            assert(handle_pointer(&state, &button_down, &effects,
                                  &frame_window, &frame_timestamp) ==
                   ASTRA_STATUS_OK);
            assert(handle_pointer(&state, &button_up, &effects,
                                  &frame_window, &frame_timestamp) ==
                   ASTRA_STATUS_OK);
            assert(delivered_count == before && wait_count == 0u &&
                   window->pending_close != 0u);
            assert(signal_vblank(&state) == ASTRA_STATUS_OK);
            assert(delivered_count == before + 1u &&
                   delivered.event.type ==
                       ASTRA_WINDOW_EVENT_CLOSE_REQUEST &&
                   (delivered.event.flags & ASTRA_WINDOW_EVENT_LOSS) != 0u &&
                   window->pending_close == 0u && wait_count == 0u);
        }
    }
    {
        DisplayWindow focus_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState focus = {
            .windows = focus_windows,
            .capacity = TEST_WINDOW_COUNT,
        };
        uint32_t before = delivered_count;

        add_window(&focus, 0u, ASTRA_WINDOW_STANDARD, 100u, 100u,
                   200u, 120u, ASTRA_WINDOW_ACTIVE, 0u);
        add_window(&focus, 1u, ASTRA_WINDOW_STANDARD, 160u, 140u,
                   200u, 120u, 0u, 0u);
        {
            const char *bar_title;
            uint32_t bar_length;

            focus.windows[0].request.flags = 0u;
            system_bar_title(&focus, &bar_title, &bar_length);
            assert(bar_length == 9u &&
                   memcmp(bar_title, "Workspace", bar_length) == 0);
            focus.windows[0].request.flags = ASTRA_WINDOW_ACTIVE;
            memcpy(focus.windows[1].request.title, "Gallery", 7u);
            focus.windows[1].request.title_length = 7u;
            focus.system_initialized = 1u;
            {
                AstraGuiWindowCommand name = {
                    .window = focus.windows[1].id,
                    .action = ASTRA_GUI_WINDOW_SET_APPLICATION_NAME,
                    .title_length = 7u,
                    .title = "Gallery"
                };
                DisplayWindow closed;
                int changed;

                assert(apply_command(&focus, &theme, &name, &closed,
                                     &changed) == ASTRA_STATUS_OK);
                assert(changed == 0 && focus.system_initialized == 1u);
            }
        }
        activate(&focus, &theme, focus.windows[1].id, 1, 108u);
        {
            const char *bar_title;
            uint32_t bar_length;

            system_bar_title(&focus, &bar_title, &bar_length);
            assert(bar_length == 7u &&
                   memcmp(bar_title, "Gallery", bar_length) == 0);
            assert(focus.system_initialized == 0u &&
                   focus.damage[0].top == 0 &&
                   focus.damage[1].top == 0);
            focus.system_initialized = 1u;
            assert(activate(&focus, &theme, focus.windows[1].id,
                            0, 108u) == 0);
            assert(focus.system_initialized == 1u);
            focus.windows[1].state = ASTRA_WINDOW_STATE_MINIMIZED;
            system_bar_title(&focus, &bar_title, &bar_length);
            assert(bar_length == 9u &&
                   memcmp(bar_title, "Workspace", bar_length) == 0);
            focus.windows[1].state = ASTRA_WINDOW_STATE_NORMAL;
        }
        assert(delivered_count == before + 2u &&
               previous_delivered.event.type == ASTRA_WINDOW_EVENT_STATE &&
               (previous_delivered.event.data.state.flags &
                ASTRA_WINDOW_ACTIVE) == 0u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_STATE &&
               (delivered.event.data.state.flags &
                ASTRA_WINDOW_ACTIVE) != 0u &&
               delivered.event.data.state.state ==
                   ASTRA_WINDOW_STATE_NORMAL &&
               (focus.windows[1].request.flags & ASTRA_WINDOW_ACTIVE) != 0u);
        {
            AstraGuiWindowCommand rename = {
                .window = focus.windows[1].id,
                .action = ASTRA_GUI_WINDOW_SET_TITLE,
                .title_length = 9u,
                .title = "Inspector"
            };
            DisplayWindow closed;
            int changed;
            const char *bar_title;
            uint32_t bar_length;

            focus.system_initialized = 1u;
            assert(apply_command(&focus, &theme, &rename, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            assert(changed != 0 && focus.system_initialized == 1u);
            system_bar_title(&focus, &bar_title, &bar_length);
            assert(bar_length == 7u &&
                   memcmp(bar_title, "Gallery", bar_length) == 0);
            assert(apply_command(&focus, &theme, &rename, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            assert(changed == 0 && focus.system_initialized == 1u);
            rename.action = ASTRA_GUI_WINDOW_SET_APPLICATION_NAME;
            assert(apply_command(&focus, &theme, &rename, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            assert(changed != 0 && focus.system_initialized == 0u);
            system_bar_title(&focus, &bar_title, &bar_length);
            assert(bar_length == 9u &&
                   memcmp(bar_title, "Inspector", bar_length) == 0);
            focus.system_initialized = 1u;
            assert(apply_command(&focus, &theme, &rename, &closed,
                                 &changed) == ASTRA_STATUS_OK);
            assert(changed == 0 && focus.system_initialized == 1u);
        }
        before = delivered_count;
        resize_event(&focus.windows[1], 109u);
        assert(delivered_count == before + 1u &&
               delivered.event.type == ASTRA_WINDOW_EVENT_RESIZE &&
               delivered.event.data.resize.width == 200u &&
               delivered.event.data.resize.height == 120u);
        focus.windows[1].request.event_mask &=
            ~ASTRA_WINDOW_SUBSCRIBE_RESIZE;
        resize_event(&focus.windows[1], 110u);
        assert(delivered_count == before + 1u);
    }
    {
        DisplayWindow sole_window = {
            .id = 11u,
            .request = {
                .flags = ASTRA_WINDOW_ACTIVE,
                .type = ASTRA_WINDOW_STANDARD,
                .width = 80u,
                .height = 40u,
                .title_length = 4u,
                .title = "Solo"
            }
        };
        DisplayWindow windows[1] = {sole_window};
        DisplayState solo = {
            .windows = windows,
            .count = 1u,
            .capacity = 1u,
            .system_initialized = 1u
        };
        AstraGuiWindowCommand command = {
            .window = 11u,
            .action = ASTRA_GUI_WINDOW_MINIMIZE
        };
        DisplayWindow closed;
        const char *bar_title;
        uint32_t bar_length;
        int changed;

        assert(apply_command(&solo, &theme, &command, &closed,
                             &changed) == ASTRA_STATUS_OK);
        system_bar_title(&solo, &bar_title, &bar_length);
        assert(changed != 0 && solo.system_initialized == 0u &&
               bar_length == 9u &&
               memcmp(bar_title, "Workspace", bar_length) == 0);

        windows[0] = sole_window;
        solo.system_initialized = 1u;
        solo.damage[0] = (DamageRect){0};
        solo.damage[1] = (DamageRect){0};
        command.action = ASTRA_GUI_WINDOW_CLOSE;
        assert(apply_command(&solo, &theme, &command, &closed,
                             &changed) == ASTRA_STATUS_OK);
        assert(changed != 0 && solo.count == 0u &&
               solo.system_initialized == 0u &&
               solo.damage[0].valid != 0u &&
               solo.damage[1].valid != 0u);

        windows[0] = sole_window;
        windows[0].state = ASTRA_WINDOW_STATE_MINIMIZED;
        windows[0].request.flags = 0u;
        solo.count = 1u;
        solo.damage[0] = (DamageRect){0};
        solo.damage[1] = (DamageRect){0};
        assert(apply_command(&solo, &theme, &command, &closed,
                             &changed) == ASTRA_STATUS_OK);
        assert(changed != 0 && solo.count == 0u &&
               solo.damage[0].valid == 0u &&
               solo.damage[1].valid == 0u);

        windows[0] = sole_window;
        windows[0].request.flags = 0u;
        solo.count = 1u;
        solo.system_initialized = 1u;
        assert(apply_command(&solo, &theme, &command, &closed,
                             &changed) == ASTRA_STATUS_OK);
        assert(changed != 0 && solo.system_initialized == 1u);
    }
    {
        /* The retained scene preserves bottom-to-top order; the host compiler
           resolves overlap into scanline spans without MC68040 pixel work. */
        DisplayWindow stack_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState stack = {
            .windows = stack_windows,
            .capacity = TEST_WINDOW_COUNT,
            .damage = {
                { 100, 100, 300, 300, 1u },
                { 100, 100, 300, 300, 1u }
            }
        };
        uint32_t bytes;

        add_window(&stack, 0u, ASTRA_WINDOW_DESKTOP, 0u, DISPLAY_WORK_TOP,
                   ASTRA_DISPLAY_WIDTH,
                   DISPLAY_WORK_BOTTOM - DISPLAY_WORK_TOP, 0u, 0u);
        add_window(&stack, 1u, ASTRA_WINDOW_FULLSCREEN, 100u, 100u,
                   200u, 200u, ASTRA_WINDOW_ACTIVE, 0u);

        bytes = compose(batch, 2u, &stack, &error, NULL);
        assert(bytes != 0u && error == ASTRA_STATUS_OK);
        assert(read_be32(batch_scene() + 20u) == 4u);
        assert((uint16_t)read_be32(batch_scene() + 36u) ==
               color(theme.canvas));
        assert(batch_scene_layer_source(0u) == stack.system_offset[0]);
        assert(batch_scene_layer_source(1u) == stack.system_offset[1]);
        /* An undecorated window is its content bank, with no cache. */
        assert(batch_scene_layer_source(2u) ==
               stack.windows[0].content_offset[
                   stack.windows[0].content_back]);
        assert(batch_scene_layer_source(3u) ==
               stack.windows[1].content_offset[
                   stack.windows[1].content_back]);
        /* The only blits carry each first frame into the next back bank. */
        assert(batch_blit_count() == 2u);
    }
    {
        DisplayWindow many_windows[TEST_WINDOW_COUNT] = {0};
        DisplayState many = {
            .windows = many_windows,
            .capacity = TEST_WINDOW_COUNT,
            .damage = {
                {0, DISPLAY_WORK_TOP, ASTRA_DISPLAY_WIDTH,
                 DISPLAY_WORK_BOTTOM, 1u},
                {0, DISPLAY_WORK_TOP, ASTRA_DISPLAY_WIDTH,
                 DISPLAY_WORK_BOTTOM, 1u},
            },
        };
        uint32_t waits[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t sources[ASTRA_WAIT_MULTIPLE_MAX];

        for (uint32_t index = 0u; index < 5u; ++index)
            add_window(&many, index, ASTRA_WINDOW_POPOVER,
                       (uint16_t)(20u + index * 120u), 80u,
                       100u, 80u, 0u, 0u);
        assert(valid_batch(compose(batch, 9u, &many, &error, NULL)) != 0u);
        assert(error == ASTRA_STATUS_OK);
        for (uint32_t index = 0u; index < 5u; ++index)
            assert(batch_blit_from_surface(
                       many.windows[index].content_offset[
                           many.windows[index].content_back]) != 0u);
        assert(display_wait_handles(&many, 0x10u, 0x20u, 0x25u, 0u,
                                    waits, sources) == 8u);
    }
    {
        static uint16_t pixels[100u * 80u];
        DisplayWindow rgb_window = {0};
        DisplayState rgb = {
            .windows = &rgb_window,
            .capacity = 1u,
            .damage = {
                {100, 100, 200, 180, 1u},
                {100, 100, 200, 180, 1u},
            },
        };

        pixels[0] = 0xf800u;
        add_window(&rgb, 0u, ASTRA_WINDOW_POPOVER, 100u, 100u,
                   100u, 80u, 0u, 0u);
        assert(astra_surface_view_init(&rgb_window.surface.view, pixels,
                                       sizeof(pixels), 100u, 80u, 200u));
        rgb_window.request.content_format = ASTRA_WINDOW_CONTENT_RGB565;
        rgb_window.request.pitch = 200u;
        assert(valid_batch(compose(batch, 1u, &rgb, &error, NULL)) != 0u);
        assert(error == ASTRA_STATUS_OK);
        rgb_window.content_damage.right = 101;
        assert(compose(batch, 2u, &rgb, &error, NULL) == 0u);
        assert(error == ASTRA_STATUS_PROTOCOL);
    }
    test_launch_panel();
    test_shrunk_window_copy_forward();
    puts("display compositor tests passed");
    return 0;
}
