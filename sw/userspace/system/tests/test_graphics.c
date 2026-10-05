#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <astra/draw_list.h>
#include <astra/graphics.h>
#include <astra/graphics_library.h>
#include <astra/gui.h>
#include <astra/port.h>
#include <astra/status.h>
#include <astra/window.h>

#include "syscall.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

_Static_assert(sizeof(AstraSurface) == 28, "AstraSurface ABI");
_Static_assert(sizeof(AstraBlitOptions) == 28, "AstraBlitOptions ABI");
_Static_assert(sizeof(AstraFence) == 4, "AstraFence ABI");
_Static_assert(sizeof(AstraPointI32) == 8, "AstraPointI32 ABI");
_Static_assert(sizeof(AstraRectI32) == 16, "AstraRectI32 ABI");
_Static_assert(sizeof(AstraDisplayMode) == 28, "AstraDisplayMode ABI");
_Static_assert(sizeof(AstraDisplayLayout) == 20, "AstraDisplayLayout ABI");
_Static_assert(sizeof(AstraSurfaceCreateInfo) == 40,
               "AstraSurfaceCreateInfo ABI");
_Static_assert(sizeof(AstraSurfaceInfo) == 40, "AstraSurfaceInfo ABI");
_Static_assert(sizeof(AstraDrawPaint) == 32, "AstraDrawPaint ABI");

/* A fake display service behind the NDK syscall seam. It records what the
   managed graphics API sends and answers like display/window_graphics.c. */
#define FAKE_CONTROL 0x303u
#define FAKE_WINDOW 7u
#define FAKE_AREAS 16u

typedef struct FakeArea {
    uint32_t handle;
    uint32_t alias;
    uint8_t *bytes;
    uint32_t size;
    int live;
} FakeArea;

static FakeArea fake_areas[FAKE_AREAS];
static uint32_t fake_area_copies;
static uint32_t fake_next_handle = 0x1000u;
static uint32_t fake_live_handles;
static uint32_t fake_next_object = 1u;
static AstraGuiGraphicsCommand fake_last;
static uint32_t fake_actions[16];
static uint32_t fake_list_area;
static uint32_t fake_staging_area;
static uint32_t fake_status = ASTRA_STATUS_OK;
static uint8_t fake_reply[128];
static uint32_t fake_reply_size;
static AstraDrawListHeader fake_submitted_header;
static AstraDrawListCommand fake_submitted[1200];
/* The whole submitted list area, payload included. */
static uint8_t fake_submitted_list[1u << 20];
static uint32_t fake_duplicate_rights;
/* Release events the NDK created, the service's signaling handles for
   them, and whether a posted frame left one signaled. */
#define FAKE_EVENTS 4u
static uint32_t fake_events[FAKE_EVENTS];
static uint32_t fake_event_aliases[FAKE_EVENTS];
static uint32_t fake_release_signaled;
static uint32_t fake_release_waits;
static uint32_t fake_list_release;
static AstraGuiGraphicsCommand fake_posted;
static uint32_t fake_posts;

static int fake_event(uint32_t handle)
{
    for (uint32_t index = 0u; index < FAKE_EVENTS; ++index)
        if (handle != 0u && fake_events[index] == handle)
            return 1;
    return 0;
}

static int fake_event_alias(uint32_t handle)
{
    for (uint32_t index = 0u; index < FAKE_EVENTS; ++index)
        if (handle != 0u && fake_event_aliases[index] == handle)
            return 1;
    return 0;
}


static FakeArea *fake_area(uint32_t handle)
{
    for (uint32_t index = 0u; index < FAKE_AREAS; ++index)
        if (fake_areas[index].live &&
            (fake_areas[index].handle == handle ||
             fake_areas[index].alias == handle))
            return &fake_areas[index];
    return 0;
}

/* The service's copy of the attached list, as it reads it. */
static void fake_take_list(void)
{
    const AstraDrawListHeader *header =
        (const AstraDrawListHeader *)fake_area(fake_list_area)->bytes;

    fake_submitted_header = *header;
    CHECK(header->command_count <= 1200u &&
          header->total_bytes <= sizeof(fake_submitted_list) &&
          header->total_bytes <= fake_area(fake_list_area)->size);
    memcpy(fake_submitted, header + 1,
           header->command_count * sizeof(AstraDrawListCommand));
    memcpy(fake_submitted_list, header, header->total_bytes);
}

static void fake_reply_graphics(uint32_t transaction, uint32_t object,
                                uint32_t pitch)
{
    AstraGuiGraphicsReply reply = {0};

    reply.header.total_size = sizeof(reply);
    reply.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    reply.header.protocol = ASTRA_GUI_PROTOCOL;
    reply.header.protocol_version = ASTRA_GUI_VERSION;
    reply.header.operation = ASTRA_GUI_GRAPHICS_REPLY;
    reply.header.transaction_id = transaction;
    reply.status = fake_status;
    reply.object = fake_status == ASTRA_STATUS_OK ? object : 0u;
    reply.pitch = fake_status == ASTRA_STATUS_OK ? pitch : 0u;
    memcpy(fake_reply, &reply, sizeof(reply));
    fake_reply_size = sizeof(reply);
}

static void fake_graphics(const AstraGuiGraphicsCommand *command,
                          const uint32_t *handles, uint32_t count)
{
    uint32_t object = 0u;
    uint32_t pitch = 0u;

    CHECK(command->header.operation == ASTRA_GUI_GRAPHICS_COMMAND &&
          command->header.total_size == sizeof(*command) &&
          command->window == FAKE_WINDOW && command->generation != 0u);
    fake_last = *command;
    ++fake_actions[command->action];
    switch (command->action) {
    case ASTRA_GUI_GRAPHICS_SURFACE_CREATE:
        CHECK(count == 1u);
        object = ++fake_next_object;
        pitch = command->width *
                (command->format == ASTRA_PIXEL_FORMAT_RGB565 ? 2u : 4u);
        break;
    case ASTRA_GUI_GRAPHICS_STAGING_SET:
        CHECK(count == 2u && fake_area(handles[1]) != 0);
        fake_staging_area = handles[1];
        break;
    case ASTRA_GUI_GRAPHICS_LIST_ATTACH:
        /* The list area, and the event to signal once a FRAME is read. */
        CHECK(count == 3u && fake_area(handles[1]) != 0 &&
              fake_event_alias(handles[2]));
        fake_list_area = handles[1];
        fake_list_release = handles[2];
        object = ++fake_next_object;
        break;
    case ASTRA_GUI_GRAPHICS_LIST_SUBMIT:
        fake_take_list();
        break;
    case ASTRA_GUI_GRAPHICS_SURFACE_READ: {
        /* Row r, byte b of the read comes back as r * 16 + b. */
        uint8_t *staging = fake_area(fake_staging_area)->bytes;
        uint32_t row = command->width *
                       (command->pitch / command->width);

        CHECK(count == 1u);
        for (uint32_t y = 0u; y < command->height; ++y)
            for (uint32_t at = 0u; at < row; ++at)
                staging[command->offset + y * command->pitch + at] =
                    (uint8_t)(y * 16u + at);
        break;
    }
    default:
        CHECK(count == 1u);
        break;
    }
    fake_reply_graphics(command->header.transaction_id, object, pitch);
}

static void fake_window_query(const AstraGuiWindowCommand *command)
{
    AstraGuiWindowState reply = {0};

    CHECK(command->action == ASTRA_GUI_WINDOW_QUERY);
    reply.header.total_size = sizeof(reply);
    reply.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    reply.header.protocol = ASTRA_GUI_PROTOCOL;
    reply.header.protocol_version = ASTRA_GUI_VERSION;
    reply.header.operation = ASTRA_GUI_WINDOW_STATE;
    reply.header.transaction_id = command->header.transaction_id;
    reply.window = FAKE_WINDOW;
    reply.generation = 2u;
    reply.width = 320u;
    reply.height = 200u;
    memcpy(fake_reply, &reply, sizeof(reply));
    fake_reply_size = sizeof(reply);
}

uint32_t astra_system_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    (void)d3;
    (void)d4;
    (void)d5;
    *out_d1 = 0u;
    *out_d2 = 0u;
    switch (number) {
    case ASTRA_SYSCALL_AREA_CREATE:
        for (uint32_t index = 0u; index < FAKE_AREAS; ++index)
            if (!fake_areas[index].live) {
                void *bytes = mmap(0, d1, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                                   -1, 0);

                CHECK(bytes != MAP_FAILED &&
                      (uintptr_t)bytes <= UINT32_MAX);
                fake_areas[index] = (FakeArea){
                    fake_next_handle++, 0u, bytes, (uint32_t)d1, 1
                };
                ++fake_live_handles;
                *out_d1 = fake_areas[index].handle;
                return ASTRA_SYSCALL_OK;
            }
        return ASTRA_SYSCALL_OUT_OF_MEMORY;
    case ASTRA_SYSCALL_AREA_MAP: {
        FakeArea *area = fake_area((uint32_t)d1);

        CHECK(area != 0);
        *out_d1 = (uint32_t)(uintptr_t)area->bytes;
        *out_d2 = area->size;
        return ASTRA_SYSCALL_OK;
    }
    case ASTRA_SYSCALL_AREA_UNMAP:
        return ASTRA_SYSCALL_OK;
    case ASTRA_SYSCALL_AREA_COPY_IN: {
        /* The copy engine, over the fake area's bytes. */
        const AstraAreaCopy *copy = (const AstraAreaCopy *)d1;
        FakeArea *area = fake_area(copy->area);

        CHECK(copy->size == ASTRA_AREA_COPY_SIZE && area != 0 &&
              copy->rows != 0u && copy->row_bytes != 0u &&
              (uint64_t)copy->area_offset +
                      (uint64_t)copy->area_pitch * (copy->rows - 1u) +
                      copy->row_bytes <= area->size);
        for (uint32_t row = 0u; row < copy->rows; ++row)
            memcpy(area->bytes + copy->area_offset + copy->area_pitch * row,
                   (const uint8_t *)(uintptr_t)copy->source +
                       (uint64_t)copy->source_pitch * row,
                   copy->row_bytes);
        ++fake_area_copies;
        return ASTRA_SYSCALL_OK;
    }
    case ASTRA_SYSCALL_EVENT_CREATE:
        CHECK(d1 == 0u && d2 == (ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                                 ASTRA_RIGHT_TRANSFER));
        for (uint32_t index = 0u; index < FAKE_EVENTS; ++index)
            if (fake_events[index] == 0u) {
                fake_events[index] = fake_next_handle++;
                ++fake_live_handles;
                *out_d1 = fake_events[index];
                return ASTRA_SYSCALL_OK;
            }
        return ASTRA_SYSCALL_OUT_OF_MEMORY;
    case ASTRA_SYSCALL_PORT_SEND_TRY: {
        const AstraGuiGraphicsCommand *command =
            (const AstraGuiGraphicsCommand *)d2;

        /* Only a posted frame is sent without a reply. */
        CHECK(d1 == FAKE_CONTROL && d3 == sizeof(*command) && d5 == 0u &&
              command->header.operation == ASTRA_GUI_GRAPHICS_COMMAND &&
              command->header.transaction_id != 0u &&
              command->action == ASTRA_GUI_GRAPHICS_FRAME &&
              command->window == FAKE_WINDOW);
        fake_posted = *command;
        ++fake_posts;
        fake_take_list();
        fake_release_signaled = 1u;
        return ASTRA_SYSCALL_OK;
    }
    case ASTRA_SYSCALL_HANDLE_DUPLICATE: {
        FakeArea *area = fake_area((uint32_t)d1);

        if (fake_event((uint32_t)d1)) {
            CHECK(d2 == (ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_TRANSFER));
            for (uint32_t index = 0u; index < FAKE_EVENTS; ++index)
                if (fake_events[index] == d1)
                    fake_event_aliases[index] = fake_next_handle;
            ++fake_live_handles;
            *out_d1 = fake_next_handle++;
            return ASTRA_SYSCALL_OK;
        }
        CHECK(area != 0 && area->alias == 0u &&
              (d2 & ~(uint32_t)ASTRA_RIGHT_WRITE) ==
                  (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP |
                   ASTRA_RIGHT_TRANSFER));
        fake_duplicate_rights = (uint32_t)d2;
        area->alias = fake_next_handle++;
        ++fake_live_handles;
        *out_d1 = area->alias;
        return ASTRA_SYSCALL_OK;
    }
    case ASTRA_SYSCALL_CLOSE: {
        FakeArea *area = fake_area((uint32_t)d1);

        CHECK(fake_live_handles != 0u);
        --fake_live_handles;
        for (uint32_t index = 0u; index < FAKE_EVENTS; ++index)
            if (fake_events[index] == d1)
                fake_events[index] = 0u;
        if (area != 0 && area->handle == d1) {
            munmap(area->bytes, area->size);
            *area = (FakeArea){0};
        }
        return ASTRA_SYSCALL_OK;
    }
    case ASTRA_SYSCALL_PORT_CREATE:
        *out_d1 = fake_next_handle;
        *out_d2 = fake_next_handle + 1u;
        fake_next_handle += 2u;
        fake_live_handles += 2u;
        return ASTRA_SYSCALL_OK;
    case ASTRA_SYSCALL_PORT_CALL: {
        const AstraCall *call = astra_system_test_call;
        const AstraMessageHeader *header = call->request;
        uint32_t handles[3] = {0u, 0u, 0u};
        uint32_t count = call->handle_count + 1u;

        CHECK(((const AstraPortCall *)d1)->port == FAKE_CONTROL &&
              call->handle_count < 3u && call->reply_index == 0u);
        /* The service's view: the reply capability, then the attachment,
           which it now owns. */
        handles[0] = 0xcafeu;
        for (uint32_t index = 0u; index < call->handle_count; ++index)
            handles[index + 1u] = call->handles[index];
        fake_live_handles -= call->handle_count;
        if (header->operation == ASTRA_GUI_GRAPHICS_COMMAND)
            fake_graphics(call->request, handles, count);
        else
            fake_window_query(call->request);
        CHECK(fake_reply_size != 0u &&
              call->reply_capacity >= fake_reply_size);
        memcpy(call->reply, fake_reply, fake_reply_size);
        *out_d1 = fake_reply_size;
        fake_reply_size = 0u;
        return ASTRA_SYSCALL_OK;
    }
    case ASTRA_SYSCALL_WAIT_ONE:
        /* A posted list is waited for only after it was posted, and its
           event taken once. */
        if (fake_event((uint32_t)d1)) {
            CHECK(fake_release_signaled != 0u);
            fake_release_signaled = 0u;
            ++fake_release_waits;
        }
        return ASTRA_SYSCALL_OK;
    default:
        fprintf(stderr, "unexpected syscall %u\n", number);
        exit(1);
    }
}

/* Triangles carry their vertices in the list payload, which moves with
   the list as either the commands or the payload grow. */
static void test_triangles(const AstraSurface *sprite,
                           const AstraSurface *content)
{
    AstraDrawList fresh = ASTRA_DRAW_LIST_INIT;
    AstraDrawList *list = &fresh;
    enum { MANY = 6000u };
    static AstraVertex many[MANY * 3u];
    AstraVertex quad[6];
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    const AstraDrawListCommand *command;
    const AstraDrawListVertex *stored;
    uint32_t attaches;

    CHECK(astra_draw_list_create(content,
                                 &(AstraRectI32){ 0, 0, 320, 200 }, list) ==
          ASTRA_OK);
    for (uint32_t index = 0u; index < 6u; ++index)
        quad[index] = (AstraVertex){
            ASTRA_VERTEX_PIXELS(index * 10u), ASTRA_VERTEX_PIXELS(index),
            ASTRA_VERTEX_TEXELS(index), 32768 * (int32_t)index,
            { (uint8_t)index, 0x20, 0x30, 0xff } };
    CHECK(astra_draw_triangles(list, sprite, quad, 6u, ASTRA_BLEND_ALPHA,
                               ASTRA_BLIT_FILTER_LINEAR) == ASTRA_OK);
    /* Untextured triangles carry no texel coordinates. */
    CHECK(astra_draw_triangles(list, 0, quad, 3u, ASTRA_BLEND_NONE, 0u) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    for (uint32_t index = 0u; index < 6u; ++index)
        quad[index].u = quad[index].v = 0;
    CHECK(astra_draw_triangles(list, 0, quad, 3u, ASTRA_BLEND_MODULATE,
                               0u) == ASTRA_OK);
    /* Whole triangles, known modes, filtering only with a texture, a
       texture other than the destination, coordinates in range. */
    CHECK(astra_draw_triangles(list, 0, quad, 4u, ASTRA_BLEND_NONE, 0u) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_triangles(list, 0, quad, 3u, ASTRA_BLEND_MULTIPLY + 1u,
                               0u) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_triangles(list, 0, quad, 3u, ASTRA_BLEND_NONE,
                               ASTRA_BLIT_FILTER_LINEAR) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_triangles(list, content, quad, 3u, ASTRA_BLEND_NONE,
                               0u) == ASTRA_ERROR_INVALID_ARGUMENT);
    quad[1].x = INT32_C(1) << 23;
    CHECK(astra_draw_triangles(list, 0, quad, 3u, ASTRA_BLEND_NONE, 0u) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    /* A payload larger than the list area grows it; so do commands. */
    attaches = fake_actions[ASTRA_GUI_GRAPHICS_LIST_ATTACH];
    for (uint32_t index = 0u; index < MANY * 3u; ++index)
        many[index] = (AstraVertex){ (int32_t)index, -(int32_t)index, 0, 0,
                                     { 1, 2, 3, 4 } };
    CHECK(astra_draw_triangles(list, 0, many, MANY * 3u, ASTRA_BLEND_ADD,
                               0u) == ASTRA_OK);
    for (uint32_t index = 0u; index < 1100u; ++index)
        CHECK(astra_draw_line(list, (AstraPointI32){ 0, 0 },
                              (AstraPointI32){ 1, 1 }, &paint) == ASTRA_OK);
    CHECK(fake_actions[ASTRA_GUI_GRAPHICS_LIST_ATTACH] >= attaches + 2u);
    CHECK(astra_draw_submit(list, &fence) == ASTRA_OK);
    CHECK(fake_submitted_header.command_count == 1103u &&
          fake_submitted_header.payload_bytes ==
              (6u + 3u + MANY * 3u) * sizeof(AstraDrawListVertex));
    command = &fake_submitted[0];
    CHECK(command->operation == ASTRA_DRAW_LIST_TRIANGLES &&
          command->source == sprite->_private_id &&
          command->flags ==
              (ASTRA_DRAW_LIST_FILTER_LINEAR |
               ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND)) &&
          command->payload_offset ==
              astra_draw_list_payload_offset(
                  fake_submitted_header.command_capacity) &&
          command->payload_bytes == 6u * sizeof(AstraDrawListVertex) &&
          command->x == 0 && command->width == 0u && command->color == 0u);
    stored = (const AstraDrawListVertex *)(const void *)
                 (fake_submitted_list + command->payload_offset);
    CHECK(stored[5].x == 50 * 256 && stored[5].y == 5 * 256 &&
          stored[5].u == 5 * 65536 && stored[5].v == 5 * 32768 &&
          stored[5].color == 0xff052030u);
    command = &fake_submitted[1];
    CHECK(command->source == 0u &&
          command->flags ==
              ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_MOD) &&
          command->payload_offset ==
              fake_submitted[0].payload_offset + 6u * 20u);
    command = &fake_submitted[2];
    stored = (const AstraDrawListVertex *)(const void *)
                 (fake_submitted_list + command->payload_offset);
    CHECK(command->payload_bytes == MANY * 3u * 20u &&
          stored[MANY * 3u - 1u].x == (int32_t)(MANY * 3u - 1u) &&
          stored[MANY * 3u - 1u].color == 0x04010203u);
    CHECK(astra_fence_close(&fence) == ASTRA_OK);
    CHECK(astra_draw_list_close(list) == ASTRA_OK);
}

static void test_ui_text(const AstraSurface *content)
{
    static const char label[] = "Caf\xc3\xa9";
    static char long_text[4097];
    AstraDrawList fresh = ASTRA_DRAW_LIST_INIT;
    AstraDrawList *list = &fresh;
    AstraFence fence = ASTRA_FENCE_INIT;
    const AstraColorRGBA8 ink = { 0x10, 0x20, 0x30, 0xff };
    const AstraDrawListCommand *command;

    CHECK(astra_draw_list_create(content,
                                 &(AstraRectI32){ 0, 0, 320, 200 }, list) ==
          ASTRA_OK);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 12, 34 }, label,
                             sizeof(label) - 1u, 13u,
                             ASTRA_TEXT_STYLE_BOLD, ink) == ASTRA_OK);
    /* Valid UTF-8 without NUL, bounded length, a height, render styles
       only, opaque ink, 16-bit coordinates. */
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, "\xc3", 1u, 13u,
                             0u, ink) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, "a\0b", 3u, 13u,
                             0u, ink) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, "a", 0u, 13u, 0u,
                             ink) == ASTRA_ERROR_INVALID_ARGUMENT);
    memset(long_text, 'x', sizeof(long_text));
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, long_text,
                             sizeof(long_text), 13u, 0u, ink) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, long_text,
                             sizeof(long_text) - 1u, 13u, 0u, ink) ==
          ASTRA_OK);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, "a", 1u, 0u, 0u,
                             ink) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, "a", 1u, 13u,
                             ASTRA_TEXT_STYLE_BLINK, ink) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, "a", 1u, 13u, 0u,
                             (AstraColorRGBA8){ 1, 2, 3, 254 }) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 40000, 0 }, "a", 1u, 13u,
                             0u, ink) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_ui_text(0, (AstraPointI32){ 0, 0 }, "a", 1u, 13u, 0u,
                             ink) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_submit(list, &fence) == ASTRA_OK);
    /* Each payload starts word aligned. */
    CHECK(fake_submitted_header.command_count == 2u &&
          fake_submitted_header.payload_bytes == 8u + 4096u);
    command = &fake_submitted[0];
    CHECK(command->operation == ASTRA_DRAW_LIST_TEXT &&
          command->flags == ASTRA_TEXT_STYLE_BOLD && command->x == 12 &&
          command->y == 34 && command->width == 0u &&
          command->height == 0u && command->font_height == 13u &&
          command->color == 0xff102030u && command->source == 0u &&
          command->payload_bytes == sizeof(label) - 1u &&
          memcmp(fake_submitted_list + command->payload_offset, label,
                 sizeof(label) - 1u) == 0);
    CHECK(fake_submitted[1].payload_offset == command->payload_offset + 8u);
    CHECK(astra_fence_close(&fence) == ASTRA_OK);
    CHECK(astra_draw_list_close(list) == ASTRA_OK);
}

/* Rectangles are one command whose payload holds the nonempty ones. */
static void test_rectangles(const AstraSurface *content)
{
    AstraDrawList fresh = ASTRA_DRAW_LIST_INIT;
    AstraDrawList *list = &fresh;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    const AstraRectI32 rects[4] = {
        { -32768, 5, 65535u, 1u }, { 3, 4, 0u, 9u }, { 7, 32767, 2u, 3u },
        { 1, 1, 1u, 0u },
    };
    AstraRectI32 bad[2] = { { 0, 0, 1u, 1u }, { 0, 32768, 1u, 1u } };
    enum { MANY = 70000u };
    static AstraRectI32 many[MANY];
    const AstraDrawListCommand *command;
    const AstraDrawListRect *stored;
    uint32_t count;

    CHECK(astra_draw_list_create(content,
                                 &(AstraRectI32){ 0, 0, 320, 200 }, list) ==
          ASTRA_OK);
    paint.foreground = (AstraColorRGBA8){ 0x10, 0x20, 0x30, 0x80 };
    paint.blend = ASTRA_BLEND_ALPHA;
    CHECK(astra_draw_ui_text(list, (AstraPointI32){ 0, 0 }, "abc", 3u, 13u,
                             0u, (AstraColorRGBA8){ 1, 2, 3, 255 }) ==
          ASTRA_OK);
    CHECK(astra_draw_rectangles(list, rects, 4u, &paint) == ASTRA_OK);
    /* Nothing, only empties, and an invalid entry all append nothing. */
    CHECK(astra_draw_rectangles(list, 0, 0u, &paint) == ASTRA_OK);
    CHECK(astra_draw_rectangles(list, &rects[1], 1u, &paint) == ASTRA_OK);
    CHECK(astra_draw_rectangles(list, bad, 2u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    bad[1] = (AstraRectI32){ 0, 0, 65536u, 1u };
    CHECK(astra_draw_rectangles(list, bad, 2u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_rectangles(list, 0, 1u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_rectangles(0, rects, 1u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_rectangles(list, rects, 1u, 0) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    /* An empty clip draws nothing but still checks the rectangles. */
    CHECK(astra_draw_list_set_clip(list, &(AstraRectI32){ 400, 0, 1, 1 }) ==
          ASTRA_OK);
    CHECK(astra_draw_rectangles(list, rects, 4u, &paint) == ASTRA_OK);
    CHECK(astra_draw_rectangles(list, bad, 2u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_list_set_clip(list, &(AstraRectI32){ 2, 3, 100, 50 }) ==
          ASTRA_OK);
    /* A payload larger than the list grows it. */
    for (uint32_t index = 0u; index < MANY; ++index)
        many[index] = (AstraRectI32){ (int32_t)(index % 300u),
                                      (int32_t)(index % 190u), 1u, 1u };
    paint.blend = ASTRA_BLEND_NONE;
    CHECK(astra_draw_rectangles(list, many, MANY, &paint) == ASTRA_OK);
    CHECK(astra_draw_submit(list, &fence) == ASTRA_OK);
    CHECK(fake_submitted_header.command_count == 3u &&
          fake_submitted_header.payload_bytes ==
              4u + 2u * sizeof(AstraDrawListRect) +
                  MANY * sizeof(AstraDrawListRect));
    command = &fake_submitted[1];
    CHECK(command->operation == ASTRA_DRAW_LIST_FILL_RECTS &&
          command->flags ==
              ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND) &&
          command->color == 0x80102030u && command->x == 0 &&
          command->y == 0 && command->width == 0u && command->height == 0u &&
          command->source == 0u && command->clip_right == 320u &&
          command->payload_offset == fake_submitted[0].payload_offset + 4u &&
          command->payload_bytes == 2u * sizeof(AstraDrawListRect));
    stored = (const AstraDrawListRect *)(const void *)
                 (fake_submitted_list + command->payload_offset);
    CHECK(stored[0].x == -32768 && stored[0].y == 5 &&
          stored[0].width == 65535u && stored[0].height == 1u &&
          stored[1].x == 7 && stored[1].y == 32767 &&
          stored[1].width == 2u && stored[1].height == 3u);
    command = &fake_submitted[2];
    count = command->payload_bytes / (uint32_t)sizeof(AstraDrawListRect);
    stored = (const AstraDrawListRect *)(const void *)
                 (fake_submitted_list + command->payload_offset);
    CHECK(command->operation == ASTRA_DRAW_LIST_FILL_RECTS &&
          command->flags == 0u && command->clip_left == 2u &&
          command->clip_bottom == 53u && count == MANY &&
          stored[MANY - 1u].x == (int16_t)((MANY - 1u) % 300u) &&
          stored[MANY - 1u].y == (int16_t)((MANY - 1u) % 190u));
    CHECK(astra_fence_close(&fence) == ASTRA_OK);
    CHECK(astra_draw_list_close(list) == ASTRA_OK);
}

/* Segments are one command, both endpoints of each carried; opaque only. */
static void test_lines(const AstraSurface *content)
{
    AstraDrawList fresh = ASTRA_DRAW_LIST_INIT;
    AstraDrawList *list = &fresh;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    const AstraPointI32 points[4] = {
        { -32768, 5 }, { 32767, -1 }, { 7, 8 }, { 7, 8 } };
    AstraPointI32 bad[2] = { { 0, 0 }, { 0, 32768 } };
    const AstraDrawListCommand *command;
    const AstraDrawListSegment *stored;

    CHECK(astra_draw_list_create(content,
                                 &(AstraRectI32){ 0, 0, 320, 200 }, list) ==
          ASTRA_OK);
    paint.foreground = (AstraColorRGBA8){ 0x10, 0x20, 0x30, 0xff };
    paint.blend = ASTRA_BLEND_ALPHA;
    CHECK(astra_draw_lines(list, points, 2u, &paint) == ASTRA_OK);
    CHECK(astra_draw_lines(list, 0, 0u, &paint) == ASTRA_OK);
    CHECK(astra_draw_lines(list, bad, 1u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_lines(list, 0, 1u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    paint.foreground.alpha = 0xfe;
    CHECK(astra_draw_lines(list, points, 2u, &paint) ==
          ASTRA_ERROR_UNSUPPORTED);
    paint.foreground.alpha = 0xff;
    paint.blend = ASTRA_BLEND_ADD;
    CHECK(astra_draw_lines(list, points, 2u, &paint) ==
          ASTRA_ERROR_UNSUPPORTED);
    paint.blend = ASTRA_BLEND_NONE;
    /* An empty clip draws nothing but still checks the endpoints. */
    CHECK(astra_draw_list_set_clip(list, &(AstraRectI32){ 400, 0, 1, 1 }) ==
          ASTRA_OK);
    CHECK(astra_draw_lines(list, points, 2u, &paint) == ASTRA_OK);
    CHECK(astra_draw_lines(list, bad, 1u, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_list_set_clip(list, &(AstraRectI32){ 0, 0, 320, 200 }) ==
          ASTRA_OK);
    CHECK(astra_draw_lines(list, &points[2], 1u, &paint) == ASTRA_OK);
    CHECK(astra_draw_submit(list, &fence) == ASTRA_OK);
    CHECK(fake_submitted_header.command_count == 2u &&
          fake_submitted_header.payload_bytes ==
              3u * sizeof(AstraDrawListSegment));
    command = &fake_submitted[0];
    stored = (const AstraDrawListSegment *)(const void *)
                 (fake_submitted_list + command->payload_offset);
    CHECK(command->operation == ASTRA_DRAW_LIST_LINES &&
          command->flags ==
              ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND) &&
          command->color == 0xff102030u && command->x == 0 &&
          command->width == 0u && command->source == 0u &&
          command->payload_bytes == 2u * sizeof(AstraDrawListSegment));
    CHECK(stored[0].x0 == -32768 && stored[0].y0 == 5 &&
          stored[0].x1 == 32767 && stored[0].y1 == -1 &&
          stored[1].x0 == 7 && stored[1].y0 == 8 && stored[1].x1 == 7 &&
          stored[1].y1 == 8);
    CHECK(fake_submitted[1].operation == ASTRA_DRAW_LIST_LINES &&
          fake_submitted[1].flags == 0u &&
          fake_submitted[1].payload_bytes == sizeof(AstraDrawListSegment));
    CHECK(astra_fence_close(&fence) == ASTRA_OK);
    CHECK(astra_draw_list_close(list) == ASTRA_OK);
}

/* Readback: the service writes rows into staging; the NDK unpacks them. */
static void test_surface_read(AstraDisplay *display,
                              const AstraSurface *sprite,
                              const AstraSurface *content)
{
    AstraSurface readable = ASTRA_SURFACE_INIT;
    AstraSurfaceCreateInfo create = ASTRA_SURFACE_CREATE_INFO_INIT;
    uint8_t pixels[3][20];

    memset(pixels, 0xee, sizeof(pixels));
    create.width = 8;
    create.height = 8;
    create.format = ASTRA_PIXEL_FORMAT_XRGB8888;
    create.flags = ASTRA_SURFACE_DRAW_TARGET | ASTRA_SURFACE_CPU_READ;
    CHECK(astra_surface_create(display, &create, &readable) == ASTRA_OK);
    CHECK(astra_surface_read(display, &readable,
                             &(AstraRectI32){ 1, 2, 4, 3 }, pixels, 20u) ==
          ASTRA_OK);
    CHECK(fake_last.action == ASTRA_GUI_GRAPHICS_SURFACE_READ &&
          fake_last.object == readable._private_id && fake_last.x == 1 &&
          fake_last.y == 2 && fake_last.width == 4u &&
          fake_last.height == 3u && fake_last.offset == 0u &&
          fake_last.pitch == 16u);
    CHECK(pixels[0][0] == 0u && pixels[0][15] == 15u &&
          pixels[2][0] == 32u && pixels[2][15] == 47u &&
          pixels[0][16] == 0xee && pixels[2][19] == 0xee);
    /* CPU_READ is required; the window content has it. */
    CHECK(astra_surface_read(display, sprite, &(AstraRectI32){ 0, 0, 1, 1 },
                             pixels, 20u) == ASTRA_ERROR_PERMISSION);
    CHECK(astra_surface_read(display, content,
                             &(AstraRectI32){ 0, 0, 10, 1 }, pixels, 20u) ==
          ASTRA_OK);
    CHECK(astra_surface_read(display, &readable,
                             &(AstraRectI32){ 6, 0, 4, 1 }, pixels, 20u) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_surface_read(display, &readable,
                             &(AstraRectI32){ 0, 0, 8, 1 }, pixels, 20u) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    fake_status = ASTRA_STATUS_IO;
    CHECK(astra_surface_read(display, &readable,
                             &(AstraRectI32){ 0, 0, 1, 1 }, pixels, 20u) !=
          ASTRA_OK);
    fake_status = ASTRA_STATUS_OK;
    CHECK(astra_surface_close(&readable) == ASTRA_OK);
}

static uint32_t list_command_count(const AstraDrawList *list)
{
    return ((const AstraDrawListHeader *)list->_private_commands)
        ->command_count;
}

/* One list carries a frame for two destinations and is posted: the
   service is not waited for until the list changes again. */
static void test_posted_frame(AstraDisplay *display,
                              const AstraSurface *content)
{
    AstraSurfaceCreateInfo create = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraSurface target = ASTRA_SURFACE_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    AstraRectI32 whole = { 0, 0, 320, 200 };
    AstraRectI32 small = { 0, 0, 64, 32 };
    AstraRectI32 huge = { 0, 0, 300, 100 };
    uint32_t posts = fake_posts;

    create.width = 64;
    create.height = 32;
    create.format = ASTRA_PIXEL_FORMAT_ARGB8888;
    create.flags = ASTRA_SURFACE_DRAW_TARGET | ASTRA_SURFACE_DRAW_SOURCE;
    CHECK(astra_surface_create(display, &create, &target) == ASTRA_OK);
    CHECK(astra_draw_list_create(content, &whole, &list) == ASTRA_OK);
    /* Retargeting an empty list just moves where it starts. */
    CHECK(astra_draw_list_set_target(&list, &target) == ASTRA_OK);
    CHECK(list._private_destination == target._private_id);
    CHECK(astra_draw_rectangle(&list, &small, 1, &paint) == ASTRA_OK);
    /* Clips follow the current destination. */
    CHECK(astra_draw_list_set_clip(&list, &huge) == ASTRA_OK &&
          list._private_clip.width == 64u && list._private_clip.height == 32u);
    CHECK(astra_draw_list_set_target(&list, content) == ASTRA_OK);
    CHECK(astra_draw_blit(&list, &target, &small, &whole, 0) == ASTRA_OK);
    /* The same destination again adds nothing. */
    CHECK(astra_draw_list_set_target(&list, content) == ASTRA_OK);
    CHECK(astra_draw_post(&list, ASTRA_DRAW_POST_DISCARD) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_post(&list, 4u) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_post(&list, ASTRA_DRAW_POST_PRESENT |
                                     ASTRA_DRAW_POST_DISCARD) == ASTRA_OK);
    CHECK(fake_posts == posts + 1u && fake_release_waits == 0u &&
          fake_posted.object == list._private_list &&
          fake_posted.target == target._private_id &&
          fake_posted.flags ==
              (ASTRA_GUI_FRAME_PRESENT | ASTRA_GUI_FRAME_DISCARD));
    CHECK(fake_submitted_header.version == ASTRA_DRAW_LIST_VERSION_1_6 &&
          fake_submitted_header.command_count == 3u &&
          fake_submitted[0].operation == ASTRA_DRAW_LIST_FILL &&
          fake_submitted[0].clip_right == 64u &&
          fake_submitted[1].operation == ASTRA_DRAW_LIST_TARGET &&
          fake_submitted[1].source == ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID &&
          fake_submitted[1].width == 320u &&
          fake_submitted[1].clip_right == 320u &&
          fake_submitted[1].clip_bottom == 200u &&
          fake_submitted[2].operation == ASTRA_DRAW_LIST_BLIT &&
          fake_submitted[2].source == target._private_id);
    /* A blit from the current destination names it as such. */
    CHECK(astra_draw_blit(&list, content, &small, &small, 0) == ASTRA_OK);
    CHECK(fake_release_waits == 1u &&
          list_command_count(&list) == 1u &&
          list._private_destination == ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID);
    CHECK(((const AstraDrawListCommand *)
               ((const AstraDrawListHeader *)list._private_commands + 1))[0]
              .source == ASTRA_DRAW_LIST_SOURCE_DESTINATION);
    /* Posting twice in a row presents an empty frame. */
    CHECK(astra_draw_post(&list, ASTRA_DRAW_POST_PRESENT) == ASTRA_OK);
    CHECK(astra_draw_post(&list, ASTRA_DRAW_POST_PRESENT) == ASTRA_OK);
    CHECK(fake_release_waits == 2u &&
          fake_submitted_header.command_count == 0u &&
          fake_posted.flags == ASTRA_GUI_FRAME_PRESENT);
    CHECK(astra_draw_list_close(&list) == ASTRA_OK);
    fake_release_signaled = 0u;
    CHECK(astra_surface_close(&target) == ASTRA_OK);
}

static void test_window_graphics_session(void)
{
    AstraWindow window = { FAKE_CONTROL, 0x305u, FAKE_WINDOW, 1u, 0x307u };
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface sprite = ASTRA_SURFACE_INIT;
    AstraSurface content = ASTRA_SURFACE_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraSurfaceCreateInfo create = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraSurfaceInfo info = ASTRA_SURFACE_INFO_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    AstraBlitOptions options = ASTRA_BLIT_OPTIONS_INIT;
    AstraRectI32 whole = { 0, 0, 320, 200 };
    AstraRectI32 rect = { 2, 1, 4, 3 };
    /* Below 4 GiB: the copy request carries 32-bit Astra addresses. */
    uint8_t (*pixels)[40] = mmap(0, 3u * 40u, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                                 -1, 0);
    const uint8_t *staging;
    const AstraDrawListCommand *command;
    AstraResult completion = ASTRA_ERROR_IO;
    int signaled = 0;
    uint32_t perimeter = 0u;

    CHECK(astra_window_display(&window, &display) == ASTRA_OK);
    create.width = 16;
    create.height = 8;
    create.format = ASTRA_PIXEL_FORMAT_ARGB8888;
    create.flags = ASTRA_SURFACE_DRAW_SOURCE | ASTRA_SURFACE_CPU_WRITE;
    CHECK(astra_surface_create(&display, &create, &sprite) == ASTRA_OK);
    CHECK(fake_last.action == ASTRA_GUI_GRAPHICS_SURFACE_CREATE &&
          fake_last.format == ASTRA_PIXEL_FORMAT_ARGB8888 &&
          fake_last.flags == create.flags);
    CHECK(astra_surface_get_info(&sprite, &info) == ASTRA_OK &&
          info.pitch == 64u && info.width == 16u &&
          info.format == ASTRA_PIXEL_FORMAT_ARGB8888);
    create.flags = ASTRA_SURFACE_SCANOUT;
    {
        AstraSurface scanout = ASTRA_SURFACE_INIT;

        CHECK(astra_surface_create(&display, &create, &scanout) ==
              ASTRA_ERROR_UNSUPPORTED);
    }

    /* The copy engine packs the rows into staging, then the service writes
       them. */
    CHECK((void *)pixels != MAP_FAILED && (uintptr_t)pixels <= UINT32_MAX);
    for (uint32_t y = 0u; y < 3u; ++y)
        for (uint32_t x = 0u; x < 40u; ++x)
            pixels[y][x] = (uint8_t)(y * 40u + x);
    CHECK(astra_surface_write(&display, &sprite, &rect, pixels, 40u) ==
          ASTRA_OK);
    CHECK(fake_actions[ASTRA_GUI_GRAPHICS_STAGING_SET] == 1u);
    /* The service may write staging: readback returns through it. */
    CHECK(fake_duplicate_rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                    ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
    CHECK(fake_last.action == ASTRA_GUI_GRAPHICS_SURFACE_WRITE &&
          fake_last.object == sprite._private_id && fake_last.x == 2 &&
          fake_last.y == 1 && fake_last.width == 4u &&
          fake_last.height == 3u && fake_last.offset == 0u &&
          fake_last.pitch == 16u);
    staging = fake_area(fake_staging_area)->bytes;
    CHECK(memcmp(staging, pixels[0], 16u) == 0 &&
          memcmp(staging + 32u, pixels[2], 16u) == 0 &&
          fake_area_copies == 1u);
    /* A second small write reuses the staging area. */
    CHECK(astra_surface_write(&display, &sprite, &rect, pixels, 40u) ==
          ASTRA_OK && fake_actions[ASTRA_GUI_GRAPHICS_STAGING_SET] == 1u);
    rect.x = 13;
    CHECK(astra_surface_write(&display, &sprite, &rect, pixels, 40u) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_window_surface(&window, &content) == ASTRA_OK);
    CHECK(content._private_id == ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID &&
          content._private_width == 320u && content._private_height == 200u);
    CHECK(astra_draw_list_create(&content, &whole, &list) == ASTRA_OK);
    CHECK(fake_actions[ASTRA_GUI_GRAPHICS_LIST_ATTACH] == 1u &&
          fake_duplicate_rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP |
                                    ASTRA_RIGHT_TRANSFER));
    CHECK(astra_draw_list_create(&sprite, &whole, &list) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    /* A blended fill under a narrower clip. */
    CHECK(astra_draw_list_set_clip(&list, &(AstraRectI32){ 10, 10, 100, 50 })
          == ASTRA_OK);
    paint.blend = ASTRA_BLEND_ALPHA;
    paint.foreground = (AstraColorRGBA8){ 0x12, 0x34, 0x56, 0x80 };
    CHECK(astra_draw_rectangle(&list, &(AstraRectI32){ 0, 0, 40, 30 }, 1,
                               &paint) == ASTRA_OK);
    /* An outline is four edges that never overlap. */
    CHECK(astra_draw_rectangle(&list, &(AstraRectI32){ 20, 20, 10, 10 }, 0,
                               &paint) == ASTRA_OK);
    /* A flipped, blended, half-opaque 2x blit of the sprite. */
    options.flags = ASTRA_BLIT_FLIP_X;
    options.blend = ASTRA_BLEND_ALPHA;
    options.modulate.alpha = 200;
    CHECK(astra_draw_blit(&list, &sprite, &(AstraRectI32){ 0, 0, 16, 8 },
                          &(AstraRectI32){ -5, 20, 32, 16 }, &options) ==
          ASTRA_OK);
    /* Color modulation, ADD, and filtering reach the list. */
    options.flags = ASTRA_BLIT_FILTER_LINEAR;
    options.blend = ASTRA_BLEND_ADD;
    options.modulate.red = 0;
    CHECK(astra_draw_blit(&list, &sprite, &(AstraRectI32){ 0, 0, 16, 8 },
                          &(AstraRectI32){ 0, 0, 16, 8 }, &options) ==
          ASTRA_OK);
    options.blend = ASTRA_BLEND_MULTIPLY + 1u;
    CHECK(astra_draw_blit(&list, &sprite, &(AstraRectI32){ 0, 0, 16, 8 },
                          &(AstraRectI32){ 0, 0, 16, 8 }, &options) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_draw_blit(&list, &sprite, &(AstraRectI32){ 8, 0, 16, 8 },
                          &(AstraRectI32){ 0, 0, 16, 8 }, 0) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    /* Lines take opaque blending only; SDL spans translucent ones. */
    CHECK(astra_draw_line(&list, (AstraPointI32){ 0, 0 },
                          (AstraPointI32){ 5, 5 }, &paint) ==
          ASTRA_ERROR_UNSUPPORTED);
    paint.blend = ASTRA_BLEND_ADD;
    paint.foreground.alpha = 255;
    CHECK(astra_draw_line(&list, (AstraPointI32){ 0, 0 },
                          (AstraPointI32){ 5, 5 }, &paint) ==
          ASTRA_ERROR_UNSUPPORTED);
    paint.blend = ASTRA_BLEND_ALPHA;
    paint.foreground.alpha = 0x80;
    /* An empty clip draws nothing and is not an error. */
    CHECK(astra_draw_list_set_clip(&list, &(AstraRectI32){ 400, 0, 10, 10 })
          == ASTRA_OK);
    CHECK(astra_draw_rectangle(&list, &(AstraRectI32){ 0, 0, 40, 30 }, 1,
                               &paint) == ASTRA_OK);
    CHECK(astra_draw_list_set_clip(&list, &whole) == ASTRA_OK);
    /* Fill past the first area so the list grows and keeps its commands. */
    paint.blend = ASTRA_BLEND_NONE;
    for (uint32_t index = 0u; index < 1100u; ++index)
        CHECK(astra_draw_line(&list,
                              (AstraPointI32){ 0, (int32_t)(index % 200u) },
                              (AstraPointI32){ 319, 0 }, &paint) ==
              ASTRA_OK);
    CHECK(fake_actions[ASTRA_GUI_GRAPHICS_LIST_ATTACH] == 2u &&
          fake_actions[ASTRA_GUI_GRAPHICS_LIST_DETACH] == 1u);

    CHECK(astra_draw_submit(&list, &fence) == ASTRA_OK);
    CHECK(fake_last.action == ASTRA_GUI_GRAPHICS_LIST_SUBMIT &&
          fake_last.object == list._private_list &&
          fake_last.target == ASTRA_GUI_WINDOW_CONTENT_SURFACE_ID);
    CHECK(fake_submitted_header.magic == ASTRA_DRAW_LIST_MAGIC &&
          fake_submitted_header.version == ASTRA_DRAW_LIST_VERSION_1_6 &&
          fake_submitted_header.width == 320u &&
          fake_submitted_header.command_count == 1107u &&
          fake_submitted_header.command_capacity >= 1107u);
    command = &fake_submitted[0];
    CHECK(command->operation == ASTRA_DRAW_LIST_FILL &&
          command->flags ==
              ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND) &&
          command->color == 0x80123456u && command->width == 40u &&
          command->clip_left == 10u && command->clip_top == 10u &&
          command->clip_right == 110u && command->clip_bottom == 60u);
    for (uint32_t index = 1u; index <= 4u; ++index)
        perimeter += fake_submitted[index].width *
                     fake_submitted[index].height;
    CHECK(perimeter == 36u);
    command = &fake_submitted[5];
    CHECK(command->operation == ASTRA_DRAW_LIST_BLIT &&
          command->flags ==
              (ASTRA_DRAW_LIST_FLIP_X |
               ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_BLEND)) &&
          command->color == 0xc8ffffffu &&
          command->source == sprite._private_id && command->x == -5 &&
          command->width == 32u && command->source_width == 16u &&
          command->source_height == 8u);
    command = &fake_submitted[6];
    CHECK(command->operation == ASTRA_DRAW_LIST_BLIT &&
          command->flags ==
              (ASTRA_DRAW_LIST_FILTER_LINEAR |
               ASTRA_DRAW_LIST_BLEND_FLAGS(ASTRA_DRAW_LIST_BLEND_ADD)) &&
          command->color == 0xc800ffffu);
    CHECK(fake_submitted[7].operation == ASTRA_DRAW_LIST_LINE &&
          fake_submitted[7].clip_right == 320u &&
          fake_submitted[7].color == 0x80123456u &&
          fake_submitted[7].flags == 0u);

    CHECK(astra_fence_poll(&fence, &signaled, &completion) == ASTRA_OK &&
          signaled == 1 && completion == ASTRA_OK);
    /* A submitted list is sealed until it is reset. */
    CHECK(astra_draw_line(&list, (AstraPointI32){ 0, 0 },
                          (AstraPointI32){ 1, 1 }, &paint) ==
          ASTRA_ERROR_BUSY);
    CHECK(astra_draw_list_reset(&list) == ASTRA_OK);
    CHECK(astra_draw_line(&list, (AstraPointI32){ 0, 0 },
                          (AstraPointI32){ 1, 1 }, &paint) == ASTRA_OK);

    /* Service failures reach the caller. */
    fake_status = ASTRA_STATUS_INVALID;
    {
        AstraFence failed = ASTRA_FENCE_INIT;

        CHECK(astra_draw_submit(&list, &failed) ==
              ASTRA_ERROR_INVALID_ARGUMENT &&
              failed._private_handle == ASTRA_INVALID_HANDLE);
    }
    fake_status = ASTRA_STATUS_OK;

    CHECK(astra_fence_close(&fence) == ASTRA_OK);
    test_triangles(&sprite, &content);
    test_ui_text(&content);
    test_rectangles(&content);
    test_lines(&content);
    test_surface_read(&display, &sprite, &content);
    test_posted_frame(&display, &content);
    CHECK(astra_draw_list_close(&list) == ASTRA_OK);
    CHECK(astra_surface_close(&content) == ASTRA_OK);
    CHECK(astra_surface_close(&sprite) == ASTRA_OK);
    CHECK(fake_last.action == ASTRA_GUI_GRAPHICS_SURFACE_DESTROY);
    CHECK(astra_display_close(&display) == ASTRA_OK);
    /* Every handle the NDK created is closed or owned by the service. */
    CHECK(fake_live_handles == 0u);
    munmap(pixels, 3u * 40u);
}

static void test_initializers(void)
{
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface surface = ASTRA_SURFACE_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraSurfaceCreateInfo create_info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraSurfaceInfo surface_info = ASTRA_SURFACE_INFO_INIT;
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    AstraDisplayMode mode = ASTRA_DISPLAY_MODE_INIT;
    AstraHardwarePointerImage pointer = ASTRA_HARDWARE_POINTER_IMAGE_INIT;

    CHECK(display._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(surface._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(list._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(fence._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(create_info.size == sizeof(create_info));
    CHECK(surface_info.size == sizeof(surface_info));
    CHECK(paint.size == sizeof(paint) && paint.foreground.alpha == 255);
    CHECK(mode.size == sizeof(mode) &&
          mode.scaling == ASTRA_DISPLAY_SCALE_AUTO);
    CHECK(pointer.size == sizeof(pointer) && pointer.pixels == 0);
}

static void test_display_layouts(void)
{
    AstraDisplayMode mode = ASTRA_DISPLAY_MODE_INIT;
    AstraDisplayLayout layout;

    mode.width = 1920;
    mode.height = 1080;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_OK);
    CHECK(layout.viewport_x == 0 && layout.viewport_y == 0 &&
          layout.viewport_width == 1920 && layout.viewport_height == 1080);

    mode.width = 320;
    mode.height = 200;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_OK);
    CHECK(layout.crop_width == 320 && layout.crop_height == 200 &&
          layout.viewport_x == 160 && layout.viewport_y == 40 &&
          layout.viewport_width == 1600 && layout.viewport_height == 1000);

    mode.width = 1280;
    mode.height = 720;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_OK);
    CHECK(layout.viewport_x == 0 && layout.viewport_y == 0 &&
          layout.viewport_width == 1920 && layout.viewport_height == 1080);

    mode.width = 640;
    mode.height = 480;
    mode.scaling = ASTRA_DISPLAY_SCALE_INTEGER;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_OK);
    CHECK(layout.viewport_x == 320 && layout.viewport_y == 60 &&
          layout.viewport_width == 1280 && layout.viewport_height == 960);

    mode.width = 320;
    mode.height = 200;
    mode.scaling = ASTRA_DISPLAY_SCALE_FIT;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_OK);
    CHECK(layout.viewport_x == 96 && layout.viewport_y == 0 &&
          layout.viewport_width == 1728 && layout.viewport_height == 1080);

    mode.scaling = ASTRA_DISPLAY_SCALE_FILL;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_OK);
    CHECK(layout.crop_x == 0 && layout.crop_y == 10 &&
          layout.crop_width == 320 && layout.crop_height == 180 &&
          layout.viewport_width == 1920 && layout.viewport_height == 1080);

    mode.width = 0;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    mode.width = 2000;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_ERROR_UNSUPPORTED);
}

static void test_invalid_handles(void)
{
    AstraDisplay display = ASTRA_DISPLAY_INIT;
    AstraSurface surface = ASTRA_SURFACE_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraSurfaceCreateInfo create_info = ASTRA_SURFACE_CREATE_INFO_INIT;
    AstraSurfaceInfo surface_info = ASTRA_SURFACE_INFO_INIT;
    AstraRectI32 clip = { 0, 0, 320, 200 };
    AstraRectI32 rectangle = { 3, 4, 20, 10 };
    AstraDrawPaint paint = ASTRA_DRAW_PAINT_INIT;
    AstraPointI32 p0 = { 0, 0 };
    AstraPointI32 p1 = { 10, 7 };
    AstraDisplayMode mode = ASTRA_DISPLAY_MODE_INIT;
    AstraDisplayLayout layout;
    int signaled = 0;
    AstraResult completion = ASTRA_OK;

    mode.width = 320;
    mode.height = 200;
    CHECK(astra_display_layout_calculate(&mode, 1920, 1080, &layout) ==
          ASTRA_OK);

    create_info.flags = ASTRA_SURFACE_DRAW_TARGET;
    create_info.width = 320;
    create_info.height = 200;
    create_info.format = ASTRA_PIXEL_FORMAT_INDEX8;
    CHECK(astra_surface_create(&display, &create_info, &surface) ==
          ASTRA_ERROR_INVALID_HANDLE);
    create_info.format = 0;
    CHECK(astra_surface_create(&display, &create_info, &surface) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    create_info.format = ASTRA_PIXEL_FORMAT_ARGB8888 + 1;
    CHECK(astra_surface_create(&display, &create_info, &surface) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_surface_get_info(&surface, &surface_info) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_list_create(&surface, &clip, &list) ==
          ASTRA_ERROR_INVALID_HANDLE);

    CHECK(astra_draw_line(&list, p0, p1, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_rectangle(&list, &rectangle, 1, &paint) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_draw_rectangle(&list, &rectangle, 2, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    paint.reserved[0] = 1;
    CHECK(astra_draw_line(&list, p0, p1, &paint) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_draw_submit(&list, &fence) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_fence_poll(&fence, &signaled, &completion) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_fence_wait(&fence, 0, &completion) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_fence_poll(0, &signaled, &completion) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_empty_cleanup(void)
{
    ASTRA_AUTO_DISPLAY(display);
    ASTRA_AUTO_SURFACE(surface);
    ASTRA_AUTO_DRAW_LIST(list);
    ASTRA_AUTO_FENCE(fence);

    CHECK(display._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(surface._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(list._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(fence._private_handle == ASTRA_INVALID_HANDLE);
}

int main(void)
{
    test_initializers();
    test_display_layouts();
    test_invalid_handles();
    test_empty_cleanup();
    test_window_graphics_session();
    puts("PASS Astra NDK graphics contract and window graphics");
    return 0;
}
