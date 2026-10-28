#include <astraz/engine.h>

#include <stdio.h>
#include <string.h>

static int display(void *context, const AstraZEvent *event)
{
    (void)context;
    if (event->kind == ASTRAZ_EVENT_CHARACTER) {
        uint16_t character = event->data.text.character;
        putchar(character < 128u ? (int)character : '?');
    } else if (event->kind == ASTRAZ_EVENT_MORE) {
        fputs("\n[More] ", stdout);
        fflush(stdout);
    } else if (event->kind == ASTRAZ_EVENT_WARNING) {
        fprintf(stderr, "AstraZ warning: %s\n", event->data.warning.text);
    }
    return 0;
}

static uint16_t read_key(void *context, uint32_t timeout, int cursor)
{
    (void)context;
    (void)timeout;
    (void)cursor;
    fflush(stdout);
    int value = getchar();
    return value == EOF ? ASTRAZ_KEY_ESCAPE : (uint16_t)value;
}

static uint16_t read_line(void *context, uint16_t *buffer, uint32_t max,
                          uint32_t timeout, uint32_t width, int continued)
{
    char text[256];
    (void)context;
    (void)timeout;
    (void)width;
    (void)continued;
    fflush(stdout);
    if (!fgets(text, sizeof(text), stdin)) return ASTRAZ_KEY_ESCAPE;
    uint32_t length = 0u;
    while (text[length] && text[length] != '\n' && length + 1u < max) {
        buffer[length] = (uint8_t)text[length];
        ++length;
    }
    buffer[length] = 0u;
    return ASTRAZ_KEY_RETURN;
}

static int choose_file(void *context, int purpose, const char *suggested,
                       char *path, size_t capacity)
{
    (void)context;
    (void)purpose;
    (void)suggested;
    if (capacity < sizeof("/work/astraz-smoke.qzl")) return 0;
    strcpy(path, "/work/astraz-smoke.qzl");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: astraz-session-smoke STORY\n");
        return 2;
    }
    AstraZStory story;
    int status = astraz_engine_probe(argv[1], &story);
    if (status != ASTRAZ_OK) {
        fprintf(stderr, "AstraZ probe failed: %d\n", status);
        return 1;
    }
    AstraZHost host = {
        .width = 80,
        .height = 24,
        .cell_width = 1,
        .cell_height = 1,
        .event = display,
        .read_key = read_key,
        .read_line = read_line,
        .choose_file = choose_file
    };
    char error[256];
    AstraZRun request = {
        .story_path = argv[1], .host = &host,
        .error = error, .error_capacity = sizeof(error)
    };
    status = astraz_engine_run(&request);
    if (status != ASTRAZ_OK)
        fprintf(stderr, "AstraZ failed: %d %s\n", status, error);
    return status == ASTRAZ_OK ? 0 : 1;
}
