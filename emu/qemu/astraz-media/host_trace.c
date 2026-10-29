/* Host-only diagnostic: trace the private engine's first V6 screen events. */
#include <astraz/engine.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                         ((uint32_t)(c) << 8) | (uint32_t)(d))

typedef struct Trace {
    unsigned events;
    unsigned pictures;
    unsigned characters;
    unsigned reads;
    unsigned picture_oob;
    unsigned cursor_oob;
    unsigned font3;
    unsigned bold;
    unsigned emphasis;
    unsigned reverse;
    unsigned fixed;
    unsigned stop_after;
    unsigned width;
    unsigned height;
} Trace;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int event(void *context, const AstraZEvent *item)
{
    Trace *trace = context;
    unsigned at = ++trace->events;
    if (item->kind == ASTRAZ_EVENT_CHARACTER) trace->characters++;
    if (item->kind == ASTRAZ_EVENT_PICTURE) {
        const AstraZResource *resource = &item->data.picture.resource;
        trace->pictures++;
        if (resource->format == TAG('P', 'N', 'G', ' ') &&
            resource->length >= 24) {
            int64_t right = item->data.picture.column +
                            (int64_t)be32(resource->bytes + 16) - 1;
            int64_t bottom = item->data.picture.row +
                             (int64_t)be32(resource->bytes + 20) - 1;
            if (item->data.picture.column < 1 || item->data.picture.row < 1 ||
                right > trace->width || bottom > trace->height)
                trace->picture_oob++;
        }
    }
    if (item->kind == ASTRAZ_EVENT_CURSOR &&
        (item->data.cursor.column < 1 ||
         item->data.cursor.column > trace->width ||
         item->data.cursor.row < 1 ||
         item->data.cursor.row > trace->height))
        trace->cursor_oob++;
    if (item->kind == ASTRAZ_EVENT_FONT && item->data.font.font == 3)
        trace->font3++;
    if (item->kind == ASTRAZ_EVENT_STYLE) {
        unsigned style = (unsigned)item->data.style.style;
        if (style & ASTRAZ_STYLE_BOLD) trace->bold++;
        if (style & ASTRAZ_STYLE_EMPHASIS) trace->emphasis++;
        if (style & ASTRAZ_STYLE_REVERSE) trace->reverse++;
        if (style & ASTRAZ_STYLE_FIXED) trace->fixed++;
    }
    if (at > 250 && item->kind == ASTRAZ_EVENT_CHARACTER)
        return 0;
    if (at > 1000) return item->kind == ASTRAZ_EVENT_REPAINT ? 1 : 0;
    printf("%03u ", at);
    switch (item->kind) {
    case ASTRAZ_EVENT_SCREEN_BEGIN:
    case ASTRAZ_EVENT_SCREEN_END:
        printf("screen kind=%u version=%u size=%ux%u", item->kind,
               item->data.screen.version, item->data.screen.width,
               item->data.screen.height);
        break;
    case ASTRAZ_EVENT_CHARACTER:
        printf("char window=%d U+%04X", item->data.text.window,
               item->data.text.character);
        break;
    case ASTRAZ_EVENT_CURSOR:
        printf("cursor window=%d row=%d column=%d", item->data.cursor.window,
               item->data.cursor.row, item->data.cursor.column);
        break;
    case ASTRAZ_EVENT_ERASE:
        printf("erase window=%d rect=%d,%d..%d,%d", item->data.erase.window,
               item->data.erase.top, item->data.erase.left,
               item->data.erase.bottom, item->data.erase.right);
        break;
    case ASTRAZ_EVENT_SCROLL:
        printf("scroll rect=%d,%d..%d,%d units=%d", item->data.scroll.top,
               item->data.scroll.left, item->data.scroll.bottom,
               item->data.scroll.right, item->data.scroll.units);
        break;
    case ASTRAZ_EVENT_PICTURE:
        printf("picture #%d row=%d column=%d format=%08X bytes=%u",
               item->data.picture.number, item->data.picture.row,
               item->data.picture.column, item->data.picture.resource.format,
               item->data.picture.resource.length);
        break;
    case ASTRAZ_EVENT_SOUND_PREPARE:
    case ASTRAZ_EVENT_SOUND_PLAY:
        printf("sound kind=%u #%d format=%08X bytes=%u", item->kind,
               item->data.sound.number, item->data.sound.resource.format,
               item->data.sound.resource.length);
        break;
    case ASTRAZ_EVENT_REPAINT:
        printf("repaint window=%d old_y=%d new_y=%d x=%d size=%dx%d",
               item->data.repaint.window, item->data.repaint.old_y,
               item->data.repaint.new_y, item->data.repaint.x,
               item->data.repaint.width, item->data.repaint.height);
        break;
    case ASTRAZ_EVENT_FONT:
        printf("font=%d", item->data.font.font);
        break;
    case ASTRAZ_EVENT_COLOUR:
        printf("colour window=%d fg=%d bg=%d", item->data.colour.window,
               item->data.colour.foreground, item->data.colour.background);
        break;
    case ASTRAZ_EVENT_STYLE:
        printf("style window=%d flags=%d", item->data.style.window,
               item->data.style.style);
        break;
    case ASTRAZ_EVENT_WARNING:
        printf("warning %s", item->data.warning.text);
        break;
    default:
        printf("kind=%u", item->kind);
        break;
    }
    putchar('\n');
    return item->kind == ASTRAZ_EVENT_REPAINT ? 1 : 0;
}

static uint16_t read_key(void *context, uint32_t timeout, int cursor)
{
    Trace *trace = context;
    trace->reads++;
    printf("INPUT key timeout=%u cursor=%d\n", timeout, cursor);
    return ASTRAZ_KEY_RETURN;
}

static uint16_t read_line(void *context, uint16_t *buffer, uint32_t max,
                          uint32_t timeout, uint32_t width, int continued)
{
    Trace *trace = context;
    trace->reads++;
    if (max) buffer[0] = 0;
    printf("INPUT line max=%u timeout=%u width=%u continued=%d\n",
           max, timeout, width, continued);
    return ASTRAZ_KEY_RETURN;
}

static int choose_file(void *context, int purpose, const char *suggested,
                       char *path, size_t capacity)
{
    (void)context; (void)purpose; (void)suggested; (void)path;
    (void)capacity;
    return 0;
}

static int picture_info(void *context, int number,
                        const AstraZResource *resource, int *height,
                        int *width)
{
    (void)context;
    if (resource->format == TAG('P', 'N', 'G', ' ') &&
        resource->length >= 24 &&
        memcmp(resource->bytes, "\x89PNG\r\n\x1a\n", 8) == 0) {
        *width = (int)be32(resource->bytes + 16);
        *height = (int)be32(resource->bytes + 20);
    } else if (resource->format == TAG('R', 'e', 'c', 't') &&
               resource->length == 8) {
        *width = (int)be32(resource->bytes);
        *height = (int)be32(resource->bytes + 4);
    } else {
        return 0;
    }
    printf("SIZE picture #%d %dx%d format=%08X\n", number, *width,
           *height, resource->format);
    return *width > 0 && *height > 0;
}

static int font_metrics(void *context, int font, int *height, int *width)
{
    Trace *trace = context;
    if (trace->width != 320u || font != ASTRAZ_FONT_GRAPHICS ||
        height == NULL || width == NULL) return 0;
    *height = *width = 8;
    return 1;
}

static int tick(void *context)
{
    Trace *trace = context;
    return trace->reads < trace->stop_after;
}

static int sound_finished(void *context)
{
    (void)context;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s STORY [BLORB [STOP_AFTER_INPUTS]]\n",
                argv[0]);
        return 2;
    }
    AstraZStory story = {0};
    if (astraz_engine_probe(argv[1], &story) != ASTRAZ_OK) return 1;
    Trace trace = {.stop_after = 1,
                   .width = story.version == 6 ? 320u : 80u,
                   .height = story.version == 6 ? 200u : 24u};
    if (argc == 4) {
        char *end;
        unsigned long value = strtoul(argv[3], &end, 10);
        if (!*argv[3] || *end || value < 1 || value > 10) return 2;
        trace.stop_after = (unsigned)value;
    }
    AstraZHost host = {
        .context = &trace, .capabilities = ASTRAZ_CAP_PICTURES | ASTRAZ_CAP_SOUND,
        .width = trace.width, .height = trace.height,
        .cell_width = story.version == 6 ? 8u : 1u,
        .cell_height = story.version == 6 ? 8u : 1u,
        .event = event, .read_key = read_key, .read_line = read_line,
        .choose_file = choose_file, .picture_info = picture_info,
        .font_metrics = font_metrics,
        .tick = tick, .sound_finished = sound_finished
    };
    char error[256] = {0};
    AstraZRun run = {.story_path = argv[1],
                     .resources_path = argc >= 3 ? argv[2] : NULL,
                     .host = &host, .error = error,
                     .error_capacity = sizeof(error)};
    int result = astraz_engine_run(&run);
    printf("RESULT status=%d events=%u pictures=%u characters=%u reads=%u picture_oob=%u cursor_oob=%u font3=%u bold=%u emphasis=%u reverse=%u fixed=%u error=%s\n",
           result, trace.events, trace.pictures, trace.characters,
           trace.reads, trace.picture_oob, trace.cursor_oob, trace.font3,
           trace.bold, trace.emphasis, trace.reverse, trace.fixed, error);
    return result == ASTRAZ_OK ||
        (result == ASTRAZ_CANCELLED && trace.reads == trace.stop_after) ? 0 : 1;
}
