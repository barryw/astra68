#include <astraz/engine.h>
#include <astra/endian.h>

#include <limits.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "frotz.h"
#include "blorb.h"

typedef char AstraZFileContract[(FILE_RESTORE == ASTRAZ_FILE_RESTORE &&
    FILE_SAVE == ASTRAZ_FILE_SAVE && FILE_SCRIPT == ASTRAZ_FILE_TRANSCRIPT &&
    FILE_PLAYBACK == ASTRAZ_FILE_PLAYBACK && FILE_RECORD == ASTRAZ_FILE_RECORD &&
    FILE_LOAD_AUX == ASTRAZ_FILE_LOAD_AUX &&
    FILE_SAVE_AUX == ASTRAZ_FILE_SAVE_AUX &&
    FILE_NO_PROMPT == ASTRAZ_FILE_NO_PROMPT) ? 1 : -1];
typedef char AstraZDisplayContract[(REVERSE_STYLE == ASTRAZ_STYLE_REVERSE &&
    BOLDFACE_STYLE == ASTRAZ_STYLE_BOLD &&
    EMPHASIS_STYLE == ASTRAZ_STYLE_EMPHASIS &&
    FIXED_WIDTH_STYLE == ASTRAZ_STYLE_FIXED &&
    PICTURE_STYLE == ASTRAZ_STYLE_PICTURE &&
    TEXT_FONT == ASTRAZ_FONT_TEXT && FIXED_WIDTH_FONT == ASTRAZ_FONT_FIXED &&
    WHITE_COLOUR == ASTRAZ_COLOUR_WHITE &&
    TRANSPARENT_COLOUR == ASTRAZ_COLOUR_TRANSPARENT &&
    RESTART_WPROP_SET == ASTRAZ_RESTART_PROPERTIES) ? 1 : -1];
typedef char AstraZHotkeyContract[(ZC_HKEY_UNDO == ASTRAZ_HOTKEY_UNDO &&
    ZC_HKEY_RESTART == ASTRAZ_HOTKEY_RESTART &&
    ZC_HKEY_QUIT == ASTRAZ_HOTKEY_QUIT) ? 1 : -1];

extern int astraz_frotz_main(int argc, char **argv);
extern void reset_memory(void);
extern int mouse_x, mouse_y;

typedef struct Engine {
    const AstraZRun *request;
    AstraZStory story;
    FILE *resources;
    bb_map_t *map;
    char *companion_path;
    jmp_buf escape;
    int status;
    int text_style;
    char file_name[1024];
} Engine;

/* Frotz core owns process-global state. One interpreter thread per process. */
static Engine *active;

static void error_text(const char *message)
{
    if (active && active->request->error && active->request->error_capacity)
        snprintf(active->request->error, active->request->error_capacity,
                 "%s", message);
}

static int inspect(FILE *file, AstraZStory *story, bb_map_t **map)
{
    uint8_t head[64] = {0};
    long size;
    memset(story, 0, sizeof(*story));
    *map = NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0)
        return ASTRAZ_IO;
    size_t count = fread(head, 1u, sizeof(head), file);
    if (ferror(file)) return ASTRAZ_IO;
    if (count >= 24u && memcmp(head, "FORM", 4u) == 0 &&
        memcmp(head + 8u, "IFRS", 4u) == 0) {
        story->container = ASTRAZ_CONTAINER_BLORB;
        if ((uint64_t)astra_load_be32(head + 4u) + 8u != (uint64_t)size ||
            fseek(file, 0, SEEK_SET) != 0 ||
            bb_create_map(file, map) != bb_err_None)
            return ASTRAZ_INVALID;
        bb_result_t result;
        if (bb_load_resource(*map, bb_method_FilePos, &result,
                             bb_ID_Exec, 0) != bb_err_None ||
            result.data.startpos < 8u || result.data.startpos > LONG_MAX ||
            (uint64_t)result.data.startpos + result.length > (uint64_t)size ||
            fseek(file, (long)result.data.startpos - 8L, SEEK_SET) != 0 ||
            fread(head, 1u, 8u, file) != 8u)
            return ASTRAZ_INVALID;
        if (memcmp(head, "ZCOD", 4u) == 0)
            story->vm = ASTRAZ_VM_Z;
        else if (memcmp(head, "GLUL", 4u) == 0)
            story->vm = ASTRAZ_VM_GLULX;
        else
            return ASTRAZ_UNSUPPORTED_VM;
        story->payload_offset = result.data.startpos;
        story->payload_length = result.length;
        if (story->payload_length < 64u ||
            fseek(file, (long)story->payload_offset, SEEK_SET) != 0 ||
            fread(head, 1u, 64u, file) != 64u)
            return ASTRAZ_INVALID;
    } else {
        story->payload_length = (uint64_t)size;
        if (count < 64u) return ASTRAZ_INVALID;
        if (head[0] >= 1u && head[0] <= 8u)
            story->vm = ASTRAZ_VM_Z;
        else if (memcmp(head, "Glul", 4u) == 0)
            story->vm = ASTRAZ_VM_GLULX;
        else
            return ASTRAZ_UNSUPPORTED_VM;
    }
    if (story->vm == ASTRAZ_VM_Z) {
        uint64_t length = story->payload_length;
        if (head[0] < 1u || head[0] > 8u ||
            astra_load_be16(head + 0x0eu) < 64u ||
            astra_load_be16(head + 0x0eu) > length ||
            astra_load_be16(head + 0x08u) < 64u ||
            astra_load_be16(head + 0x08u) >= length ||
            astra_load_be16(head + 0x0au) < 64u ||
            astra_load_be16(head + 0x0au) >= length ||
            astra_load_be16(head + 0x0cu) < 64u ||
            astra_load_be16(head + 0x0cu) >= length)
            return ASTRAZ_INVALID;
        story->version = head[0];
    } else {
        uint32_t end_file = astra_load_be32(head + 12u);
        if (story->payload_length < 256u ||
            astra_load_be32(head + 4u) == 0u ||
            astra_load_be32(head + 8u) < 256u ||
            (astra_load_be32(head + 8u) & 255u) != 0u ||
            astra_load_be32(head + 8u) > end_file ||
            end_file != story->payload_length ||
            astra_load_be32(head + 16u) < end_file ||
            (astra_load_be32(head + 16u) & 255u) != 0u ||
            astra_load_be32(head + 20u) == 0u ||
            (astra_load_be32(head + 20u) & 255u) != 0u ||
            astra_load_be32(head + 24u) < 36u ||
            astra_load_be32(head + 24u) >= end_file)
            return ASTRAZ_INVALID;
        story->version = astra_load_be32(head + 4u);
    }
    if (story->payload_offset > LONG_MAX || story->payload_length > LONG_MAX)
        return ASTRAZ_INVALID;
    return ASTRAZ_OK;
}

int astraz_engine_probe(const char *path, AstraZStory *story)
{
    if (!path || !story) return ASTRAZ_INVALID;
    FILE *file = fopen(path, "rb");
    if (!file) return ASTRAZ_IO;
    bb_map_t *map;
    int status = inspect(file, story, &map);
    if (map) bb_destroy_map(map);
    fclose(file);
    return status;
}

static int open_resources(Engine *engine)
{
    const char *path = engine->request->resources_path;
    if (engine->story.container == ASTRAZ_CONTAINER_BLORB)
        path = engine->request->story_path;
    if (!path) {
        const char *story = engine->request->story_path;
        const char *last_slash = strrchr(story, '/');
        const char *dot = strrchr(story, '.');
        size_t stem = dot && (!last_slash || dot > last_slash) ?
            (size_t)(dot - story) : strlen(story);
        if (stem > SIZE_MAX - 8u) return ASTRAZ_INVALID;
        engine->companion_path = malloc(stem + 8u);
        if (!engine->companion_path) return ASTRAZ_IO;
        memcpy(engine->companion_path, story, stem);
        memcpy(engine->companion_path + stem, ".blb", 5u);
        engine->resources = fopen(engine->companion_path, "rb");
        if (!engine->resources) {
            if (errno != ENOENT) return ASTRAZ_IO;
            memcpy(engine->companion_path + stem, ".blorb", 7u);
            engine->resources = fopen(engine->companion_path, "rb");
        }
        if (!engine->resources) {
            if (errno != ENOENT) return ASTRAZ_IO;
            free(engine->companion_path);
            engine->companion_path = NULL;
            return ASTRAZ_OK;
        }
    }
    if (!path && !engine->resources) return ASTRAZ_OK;
    if (!engine->resources) engine->resources = fopen(path, "rb");
    if (!engine->resources) return ASTRAZ_IO;
    if (bb_create_map(engine->resources, &engine->map) != bb_err_None)
        return ASTRAZ_INVALID;
    return ASTRAZ_OK;
}

static void finish(int status)
{
    active->status = status;
    longjmp(active->escape, 1);
}

static int event(const AstraZEvent *item)
{
    int result = active->request->host->event(active->request->host->context,
                                               item);
    if (result < 0) {
        error_text("Display or audio callback failed");
        finish(ASTRAZ_ENGINE_ERROR);
    }
    return result;
}

int astraz_engine_run(const AstraZRun *request)
{
    if (!request || !request->story_path || !request->host ||
        !request->host->event || !request->host->read_key ||
        !request->host->read_line || !request->host->choose_file ||
        !request->host->width || !request->host->height ||
        !request->host->cell_width || !request->host->cell_height)
        return ASTRAZ_INVALID;
    if (active) return ASTRAZ_BUSY;
    Engine engine = {.request = request};
    if (request->error && request->error_capacity) request->error[0] = '\0';
    int status = astraz_engine_probe(request->story_path, &engine.story);
    if (status != ASTRAZ_OK) return status;
    if (engine.story.vm != ASTRAZ_VM_Z) return ASTRAZ_UNSUPPORTED_VM;
    if ((request->host->capabilities & ASTRAZ_CAP_PICTURES) &&
        !request->host->picture_info)
        return ASTRAZ_MISSING_CAPABILITY;
    if ((request->host->capabilities & ASTRAZ_CAP_MOUSE) &&
        !request->host->mouse)
        return ASTRAZ_MISSING_CAPABILITY;
    if ((request->host->capabilities & ASTRAZ_CAP_SOUND) &&
        !request->host->sound_finished)
        return ASTRAZ_MISSING_CAPABILITY;
    if ((request->host->capabilities & ASTRAZ_CAP_TRUE_COLOUR) &&
        (!(request->host->capabilities & ASTRAZ_CAP_COLOUR) ||
         !request->host->colour_from_true || !request->host->colour_to_true))
        return ASTRAZ_MISSING_CAPABILITY;
    unsigned columns = engine.story.version == V6 ?
        request->host->width / request->host->cell_width : request->host->width;
    unsigned rows = engine.story.version == V6 ?
        request->host->height / request->host->cell_height : request->host->height;
    if (!columns || columns > UINT8_MAX || !rows || rows > UINT8_MAX)
        return ASTRAZ_INVALID;
    status = open_resources(&engine);
    if (status != ASTRAZ_OK) {
        if (engine.map) bb_destroy_map(engine.map);
        if (engine.resources) fclose(engine.resources);
        free(engine.companion_path);
        return status;
    }
    active = &engine;
    if (setjmp(engine.escape) == 0) {
        char *argv[] = {"astraz", (char *)request->story_path, NULL};
        astraz_frotz_main(2, argv);
        engine.status = ASTRAZ_OK;
    } else {
        reset_memory();
    }
    if (engine.map) bb_destroy_map(engine.map);
    if (engine.resources) fclose(engine.resources);
    free(engine.companion_path);
    free(f_setup.save_name);
    free(f_setup.script_name);
    free(f_setup.command_name);
    free(f_setup.aux_name);
    active = NULL;
    return engine.status;
}

void os_init_setup(void) {}

void os_process_arguments(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    f_setup.story_file = (char *)active->request->story_path;
    f_setup.blorb_file = active->map ?
        (char *)(active->story.container == ASTRAZ_CONTAINER_BLORB ?
                 active->request->story_path : active->companion_path ?
                 active->companion_path : active->request->resources_path) : NULL;
    f_setup.save_name = strdup("story.qzl");
    f_setup.script_name = strdup("story.scr");
    f_setup.command_name = strdup("story.rec");
    f_setup.aux_name = strdup("story.aux");
    if (!f_setup.save_name || !f_setup.script_name ||
        !f_setup.command_name || !f_setup.aux_name)
        os_fatal("Out of memory");
}

void os_init_screen(void)
{
    const AstraZHost *host = active->request->host;
    unsigned caps = host->capabilities;
    z_header.screen_width = host->width;
    z_header.screen_height = host->height;
    z_header.font_width = host->cell_width;
    z_header.font_height = host->cell_height;
    z_header.screen_cols = z_header.version == V6 ?
        host->width / host->cell_width : host->width;
    z_header.screen_rows = z_header.version == V6 ?
        host->height / host->cell_height : host->height;
    if (z_header.screen_cols == 0 || z_header.screen_rows == 0)
        os_fatal("Screen too small");
    z_header.interpreter_number = z_header.version == V6 ?
        INTERP_MSDOS : INTERP_DEC_20;
    z_header.interpreter_version = 'A';
    z_header.default_foreground = 1;
    z_header.default_background = 1;
    if (z_header.version == V3)
        z_header.config |= CONFIG_SPLITSCREEN;
    if (z_header.version >= V4) {
        z_header.config |= CONFIG_BOLDFACE | CONFIG_EMPHASIS | CONFIG_FIXED;
        if (caps & ASTRAZ_CAP_TIMED_INPUT)
            z_header.config |= CONFIG_TIMEDINPUT;
        else
            z_header.config &= ~CONFIG_TIMEDINPUT;
    }
    if (z_header.version >= V5) {
        if (caps & ASTRAZ_CAP_COLOUR)
            z_header.config |= CONFIG_COLOUR;
        else
            z_header.config &= ~CONFIG_COLOUR;
        if (!(caps & ASTRAZ_CAP_MOUSE)) z_header.flags &= ~MOUSE_FLAG;
        z_header.flags &= ~MENU_FLAG; /* Upstream Frotz make_menu always fails. */
    }
    if (z_header.version == V6) {
        if (active->map && (caps & ASTRAZ_CAP_PICTURES))
            z_header.config |= CONFIG_PICTURES;
        else
            z_header.config &= ~CONFIG_PICTURES;
        if (active->map && (caps & ASTRAZ_CAP_SOUND))
            z_header.config |= CONFIG_SOUND;
        else
            z_header.config &= ~CONFIG_SOUND;
    }
    f_setup.sound = active->map && (caps & ASTRAZ_CAP_SOUND);
    if (!f_setup.sound)
        z_header.flags &= ~(OLD_SOUND_FLAG | SOUND_FLAG);
    AstraZEvent item = {.kind = ASTRAZ_EVENT_SCREEN_BEGIN};
    item.data.screen.version = z_header.version;
    item.data.screen.width = host->width;
    item.data.screen.height = host->height;
    event(&item);
}

void os_reset_screen(void)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_SCREEN_END};
    event(&item);
}

FILE *os_load_story(void)
{
    FILE *file = fopen(active->request->story_path, "rb");
    if (file && fseek(file, (long)active->story.payload_offset, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    return file;
}

int os_storyfile_seek(FILE *file, long offset, int whence)
{
    int64_t target;
    if (whence == SEEK_SET)
        target = offset;
    else if (whence == SEEK_END)
        target = (int64_t)active->story.payload_length + offset;
    else if (whence == SEEK_CUR) {
        long here = ftell(file);
        if (here < 0) return -1;
        target = (int64_t)here - (int64_t)active->story.payload_offset + offset;
    } else return -1;
    if (target < 0 || target > (int64_t)active->story.payload_length ||
        target + (int64_t)active->story.payload_offset > LONG_MAX)
        return -1;
    return fseek(file, (long)((int64_t)active->story.payload_offset + target),
                 SEEK_SET);
}

int os_storyfile_tell(FILE *file)
{
    long here = ftell(file);
    return here < 0 ? -1 : (int)(here - (long)active->story.payload_offset);
}

char *os_read_file_name(const char *suggested, int purpose)
{
    active->file_name[0] = '\0';
    if (!active->request->host->choose_file(active->request->host->context,
                                             purpose, suggested,
                                             active->file_name,
                                             sizeof(active->file_name)) ||
        active->file_name[0] == '\0' ||
        !memchr(active->file_name, '\0', sizeof(active->file_name)))
        return NULL;
    return active->file_name;
}

static void drain_sound(void)
{
    const AstraZHost *host = active->request->host;
    if (host->sound_finished && host->sound_finished(host->context))
        end_of_sound();
}

void astraz_engine_poll_sound(void)
{
    if (active) drain_sound();
}

zchar os_read_key(int timeout, bool show_cursor)
{
    zchar key = active->request->host->read_key(active->request->host->context,
        timeout > 0 ? (uint32_t)timeout : 0u, show_cursor);
    if (active->request->host->tick &&
        !active->request->host->tick(active->request->host->context))
        finish(ASTRAZ_CANCELLED);
    drain_sound();
    return key;
}

zchar os_read_line(int max, zchar *buffer, int timeout, int width,
                   int continued)
{
    if (max <= 0 || !buffer) os_fatal("Invalid input buffer");
    zchar key = active->request->host->read_line(active->request->host->context,
        buffer, (uint32_t)max,
        timeout > 0 ? (uint32_t)timeout : 0u,
        width > 0 ? (uint32_t)width : 0u, continued);
    if (active->request->host->tick &&
        !active->request->host->tick(active->request->host->context))
        finish(ASTRAZ_CANCELLED);
    drain_sound();
    return key;
}

void os_more_prompt(void)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_MORE};
    event(&item);
    (void)os_read_key(0, TRUE);
}

int os_random_seed(void)
{
    const AstraZHost *host = active->request->host;
    return (int)(host->random_seed ? host->random_seed(host->context) :
                 (uint32_t)time(NULL));
}

void os_quit(int status)
{
    finish(status == EXIT_SUCCESS ? ASTRAZ_OK : ASTRAZ_ENGINE_ERROR);
}

void os_fatal(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    error_text(message);
    finish(ASTRAZ_ENGINE_ERROR);
}

void os_warn(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    AstraZEvent item = {.kind = ASTRAZ_EVENT_WARNING};
    item.data.warning.text = message;
    event(&item);
}

void os_tick(void)
{
    const AstraZHost *host = active->request->host;
    drain_sound();
    if (host->tick && !host->tick(host->context)) finish(ASTRAZ_CANCELLED);
}

void os_restart_game(int stage)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_RESTART};
    item.data.restart.stage = stage;
    event(&item);
}

void os_display_char(zchar character)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_CHARACTER};
    item.data.text.character = character;
    item.data.text.window = cwin;
    event(&item);
}

void os_display_string(const zchar *string)
{
    zchar character;
    while ((character = *string++) != 0u) {
        if (character == ZC_NEW_STYLE)
            os_set_text_style(*string++);
        else if (character == ZC_NEW_FONT)
            os_set_font(*string++);
        else
            os_display_char(character);
    }
}

void os_set_cursor(int row, int column)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_CURSOR};
    item.data.cursor.row = row;
    item.data.cursor.column = column;
    item.data.cursor.window = cwin;
    event(&item);
}

void os_erase_area(int top, int left, int bottom, int right, int window)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_ERASE};
    item.data.erase.top = top;
    item.data.erase.left = left;
    item.data.erase.bottom = bottom;
    item.data.erase.right = right;
    item.data.erase.window = window;
    event(&item);
}

void os_scroll_area(int top, int left, int bottom, int right, int units)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_SCROLL};
    item.data.scroll.top = top;
    item.data.scroll.left = left;
    item.data.scroll.bottom = bottom;
    item.data.scroll.right = right;
    item.data.scroll.units = units;
    event(&item);
}

bool os_repaint_window(int window, int old_y, int new_y, int x, int height,
                       int width)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_REPAINT};
    item.data.repaint.window = window;
    item.data.repaint.old_y = old_y;
    item.data.repaint.new_y = new_y;
    item.data.repaint.x = x;
    item.data.repaint.height = height;
    item.data.repaint.width = width;
    return event(&item) != 0;
}

void os_set_text_style(int style)
{
    active->text_style = style;
    AstraZEvent item = {.kind = ASTRAZ_EVENT_STYLE};
    item.data.style.style = style;
    item.data.style.window = cwin;
    event(&item);
}

int os_get_text_style(void)
{
    return active->text_style;
}

void os_set_font(int font)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_FONT};
    item.data.font.font = font;
    event(&item);
}

int os_font_data(int font, int *height, int *width)
{
    const AstraZHost *host = active->request->host;
    if (host->font_metrics &&
        host->font_metrics(host->context, font, height, width) &&
        *height > 0 && *width > 0)
        return 1;
    if (font != TEXT_FONT && font != FIXED_WIDTH_FONT) return 0;
    *height = host->cell_height;
    *width = host->cell_width;
    return 1;
}

int os_char_width(zchar character)
{
    const AstraZHost *host = active->request->host;
    int width = host->character_width ?
        host->character_width(host->context, character) : host->cell_width;
    return width > 0 ? width : host->cell_width;
}

int os_string_width(const zchar *string)
{
    int width = 0;
    zchar character;
    while ((character = *string++) != 0u) {
        if (character == ZC_NEW_STYLE || character == ZC_NEW_FONT)
            ++string;
        else
            width += os_char_width(character);
    }
    return width;
}

int os_check_unicode(int font, zchar character)
{
    const AstraZHost *host = active->request->host;
    return host->glyph_available ?
        host->glyph_available(host->context, font, character) :
        character >= 32u && character <= 126u;
}

void os_set_colour(int foreground, int background)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_COLOUR};
    item.data.colour.foreground = foreground;
    item.data.colour.background = background;
    item.data.colour.window = cwin;
    event(&item);
}

int os_peek_colour(void)
{
    const AstraZHost *host = active->request->host;
    return host->peek_colour ? host->peek_colour(host->context) : 0;
}

int os_from_true_colour(zword rgb5)
{
    const AstraZHost *host = active->request->host;
    return host->colour_from_true ?
        host->colour_from_true(host->context, rgb5) : 0;
}

zword os_to_true_colour(int index)
{
    const AstraZHost *host = active->request->host;
    return host->colour_to_true ?
        host->colour_to_true(host->context, index) : 0;
}

void os_beep(int pitch)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_BEEP};
    item.data.beep.pitch = pitch;
    event(&item);
}

zword os_read_mouse(void)
{
    const AstraZHost *host = active->request->host;
    if (!(host->capabilities & ASTRAZ_CAP_MOUSE) || !host->mouse) return 0;
    return (zword)host->mouse(host->context, &mouse_x, &mouse_y);
}

static int load_resource(uint32_t usage, int number, AstraZResource *resource,
                         uint8_t **owned)
{
    bb_result_t result;
    uint8_t tag[8];
    long file_size;
    uint64_t start, payload_start;
    uint32_t payload_length;
    *owned = NULL;
    memset(resource, 0, sizeof(*resource));
    if (!active->map || number < 0 ||
        bb_load_resource(active->map, bb_method_FilePos, &result,
                         usage, number) != bb_err_None ||
        result.data.startpos > LONG_MAX ||
        result.length > 32u * 1024u * 1024u + 8u ||
        fseek(active->resources, 0, SEEK_END) != 0 ||
        (file_size = ftell(active->resources)) < 0 ||
        (uint64_t)result.data.startpos + result.length > (uint64_t)file_size)
        return 0;
    start = (uint64_t)result.data.startpos;
    if (start >= 8u &&
        fseek(active->resources, (long)(start - 8u), SEEK_SET) == 0 &&
        fread(tag, 1u, sizeof(tag), active->resources) == sizeof(tag) &&
        astra_load_be32(tag + 4u) == result.length) {
        payload_start = start;
        payload_length = result.length;
    } else if (result.length >= 8u &&
        fseek(active->resources, (long)start, SEEK_SET) == 0 &&
        fread(tag, 1u, sizeof(tag), active->resources) == sizeof(tag) &&
        astra_load_be32(tag) == UINT32_C(0x464f524d) &&
        (uint64_t)astra_load_be32(tag + 4u) + 8u == result.length) {
        /* Blorblib reports FORM's chunk header as its FilePos and includes
         * the header in length. All other chunks report payload FilePos. */
        payload_start = start + 8u;
        payload_length = result.length - 8u;
    } else return 0;
    if (payload_length > 32u * 1024u * 1024u ||
        fseek(active->resources, (long)payload_start, SEEK_SET) != 0)
        return 0;
    uint8_t *bytes = malloc(payload_length ? payload_length : 1u);
    if (!bytes) return 0;
    if (fread(bytes, 1u, payload_length, active->resources) != payload_length) {
        free(bytes);
        return 0;
    }
    resource->format = astra_load_be32(tag);
    resource->bytes = bytes;
    resource->length = payload_length;
    *owned = bytes;
    return 1;
}

bool os_picture_data(int picture, int *height, int *width)
{
    const AstraZHost *host = active->request->host;
    *height = *width = 0;
    if (!(host->capabilities & ASTRAZ_CAP_PICTURES) || !active->map)
        return FALSE;
    if (picture == 0) {
        int count, first, last;
        if (bb_count_resources(active->map, bb_ID_Pict,
                               &count, &first, &last) != bb_err_None ||
            count == 0)
            return FALSE;
        *height = last;
        *width = bb_get_release_num(active->map);
        return TRUE;
    }
    AstraZResource resource;
    uint8_t *owned;
    if (!load_resource(bb_ID_Pict, picture, &resource, &owned)) return FALSE;
    int available = host->picture_info(host->context, picture, &resource,
                                       height, width);
    free(owned);
    return available != 0;
}

void os_draw_picture(int picture, int row, int column)
{
    const AstraZHost *host = active->request->host;
    AstraZResource resource;
    uint8_t *owned;
    if (!(host->capabilities & ASTRAZ_CAP_PICTURES) ||
        !load_resource(bb_ID_Pict, picture, &resource, &owned))
        os_fatal("Picture resource unavailable");
    AstraZEvent item = {.kind = ASTRAZ_EVENT_PICTURE};
    item.data.picture.number = picture;
    item.data.picture.row = row;
    item.data.picture.column = column;
    item.data.picture.resource = resource;
    int accepted = event(&item);
    free(owned);
    if (accepted < 0) os_fatal("Picture display failed");
}

void os_init_sound(void) {}

void os_prepare_sample(int number)
{
    AstraZResource resource;
    uint8_t *owned;
    if (!load_resource(bb_ID_Snd, number, &resource, &owned))
        os_fatal("Sound resource unavailable");
    AstraZEvent item = {.kind = ASTRAZ_EVENT_SOUND_PREPARE};
    item.data.sound.number = number;
    item.data.sound.resource = resource;
    int accepted = event(&item);
    free(owned);
    if (accepted < 0) os_fatal("Sound preparation failed");
}

void os_start_sample(int number, int volume, int repeats, zword end_routine)
{
    AstraZResource resource;
    uint8_t *owned;
    if (!load_resource(bb_ID_Snd, number, &resource, &owned))
        os_fatal("Sound resource unavailable");
    AstraZEvent item = {.kind = ASTRAZ_EVENT_SOUND_PLAY};
    item.data.sound.number = number;
    item.data.sound.volume = volume;
    item.data.sound.repeats = repeats;
    item.data.sound.end_routine = end_routine;
    item.data.sound.resource = resource;
    int accepted = event(&item);
    free(owned);
    if (accepted < 0) os_fatal("Sound playback failed");
}

void os_stop_sample(int number)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_SOUND_STOP};
    item.data.sound.number = number;
    event(&item);
}

void os_finish_with_sample(int number)
{
    AstraZEvent item = {.kind = ASTRAZ_EVENT_SOUND_RELEASE};
    item.data.sound.number = number;
    event(&item);
}
