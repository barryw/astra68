/* Native, application-private AstraZ media gate. No story assets are shipped. */
#include <astraz/engine.h>
#include <astra/runtime.h>
#include "audio.h"
#include "picture.h"

#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <string.h>

#define TAG(a, b, c, d) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | \
                         ((unsigned)(c) << 8) | (unsigned)(d))

typedef struct Probe {
    AstraZAudio *audio;
    int expect_mod;
    int pictures;
    int sounds;
    int invalid;
} Probe;

static int picture(void *context, int number, const AstraZResource *resource,
                   int *height, int *width)
{
    Probe *probe = context;
    if (number != 7 || resource->format != TAG('P', 'N', 'G', ' ') ||
        resource->length < 24 || memcmp(resource->bytes, "\x89PNG\r\n\x1a\n", 8)) {
        probe->invalid = 1;
        return 0;
    }
    if (!astraz_picture_size(resource, height, width) ||
        *height != 64 || *width != 64) {
        probe->invalid = 1;
        return 0;
    }
    return 1;
}

static int event(void *context, const AstraZEvent *item)
{
    Probe *probe = context;
    if (item->kind == ASTRAZ_EVENT_PICTURE) {
        const AstraZResource *resource = &item->data.picture.resource;
        if (item->data.picture.number != 7 ||
            item->data.picture.row != 1 || item->data.picture.column != 1 ||
            resource->format != TAG('P', 'N', 'G', ' ') ||
            resource->length < 24 ||
            memcmp(resource->bytes, "\x89PNG\r\n\x1a\n", 8)) {
            probe->invalid = 1;
            return -1;
        }
        int height, width;
        if (!astraz_picture_size(resource, &height, &width) ||
            height != 64 || width != 64) {
            probe->invalid = 1;
            return -1;
        }
        probe->pictures++;
    } else if (item->kind == ASTRAZ_EVENT_SOUND_PLAY) {
        const AstraZResource *resource = &item->data.sound.resource;
        unsigned format = probe->expect_mod ?
            TAG('M', 'O', 'D', ' ') : TAG('F', 'O', 'R', 'M');
        if (item->data.sound.number != 3 || resource->format != format ||
            resource->length < 12 ||
            (!probe->expect_mod && memcmp(resource->bytes, "AIFF", 4))) {
            probe->invalid = 1;
            return -1;
        }
        probe->sounds++;
        if (!astraz_audio_event(probe->audio, item)) {
            const char *error = SDL_GetError();
            if (error && *error) {
                (void)astra_log(error);
                fprintf(stderr, "AstraZ media SDL error: %s\n", error);
            }
            probe->invalid = 1;
            return -1;
        }
        if (probe->expect_mod ? !Mix_PlayingMusic() : !Mix_Playing(0)) {
            (void)astra_log("ASTRAZ_MEDIA_FAIL playback did not start");
            probe->invalid = 1;
            return -1;
        }
        SDL_Delay(probe->expect_mod ? 1200u : 200u);
        if (probe->expect_mod ? !Mix_PlayingMusic() : !Mix_Playing(0)) {
            (void)astra_log("ASTRAZ_MEDIA_FAIL playback stopped early");
            probe->invalid = 1;
            return -1;
        }
    } else if (item->kind == ASTRAZ_EVENT_SOUND_STOP ||
               item->kind == ASTRAZ_EVENT_SOUND_RELEASE ||
               item->kind == ASTRAZ_EVENT_SOUND_PREPARE) {
        if (!astraz_audio_event(probe->audio, item)) {
            probe->invalid = 1;
            return -1;
        }
    }
    return 0;
}

static uint16_t read_key(void *context, uint32_t timeout, int cursor)
{
    (void)context; (void)timeout; (void)cursor;
    return ASTRAZ_KEY_RETURN;
}

static uint16_t read_line(void *context, uint16_t *buffer, uint32_t max,
                          uint32_t timeout, uint32_t width, int continued)
{
    (void)context; (void)timeout; (void)width; (void)continued;
    if (max) buffer[0] = 0;
    return ASTRAZ_KEY_RETURN;
}

static int choose_file(void *context, int purpose, const char *suggested,
                       char *path, size_t capacity)
{
    (void)context; (void)purpose; (void)suggested; (void)path; (void)capacity;
    return 0;
}

static int sound_finished(void *context)
{
    Probe *probe = context;
    return astraz_audio_finished(probe->audio);
}

int main(int argc, char **argv)
{
    const char *story_path;
    const char *resource_path;
    int expect_mod;
    if (argc == 1) {
        expect_mod = strstr(argv[0], "ModProbe.app") != NULL;
        story_path = expect_mod ?
            "/apps/AstraZMediaModProbe.app/media.z6" :
            "/apps/AstraZMediaProbe.app/media.z6";
        resource_path = NULL;
    } else if (argc >= 3 && argc <= 4 &&
               (!strcmp(argv[2], "aiff") || !strcmp(argv[2], "mod"))) {
        story_path = argv[1];
        resource_path = argc == 4 ? argv[3] : NULL;
        expect_mod = !strcmp(argv[2], "mod");
    } else {
        fprintf(stderr, "usage: astraz-media-probe STORY aiff|mod [BLORB]\n");
        return 2;
    }
    Probe probe = {.expect_mod = expect_mod};
    probe.audio = astraz_audio_open();
    if (!probe.audio) {
        (void)astra_log("ASTRAZ_MEDIA_FAIL audio device unavailable");
        fprintf(stderr, "ASTRAZ_MEDIA_FAIL audio device unavailable\n");
        return 1;
    }
    AstraZHost host = {
        .context = &probe,
        .capabilities = ASTRAZ_CAP_PICTURES | ASTRAZ_CAP_SOUND,
        .width = 320, .height = 200, .cell_width = 8, .cell_height = 8,
        .event = event, .read_key = read_key, .read_line = read_line,
        .choose_file = choose_file, .picture_info = picture,
        .sound_finished = sound_finished
    };
    char error[256];
    AstraZRun request = {.story_path = story_path, .host = &host,
        .resources_path = resource_path,
        .error = error, .error_capacity = sizeof(error)};
    int status = astraz_engine_run(&request);
    astraz_audio_close(probe.audio);
    if (status != ASTRAZ_OK || probe.invalid || probe.pictures != 1 ||
        probe.sounds != 1) {
        (void)astra_log(expect_mod ? "ASTRAZ_MEDIA_MOD_FAIL" :
                        "ASTRAZ_MEDIA_AIFF_FAIL");
        fprintf(stderr, "ASTRAZ_MEDIA_FAIL status=%d pictures=%d sounds=%d "
                "invalid=%d error=%s\n", status, probe.pictures,
                probe.sounds, probe.invalid, error);
        return 1;
    }
    (void)astra_log(expect_mod ? "ASTRAZ_MEDIA_MOD_PASS" :
                    "ASTRAZ_MEDIA_AIFF_PASS");
    puts("ASTRAZ_MEDIA_PASS picture=PNG:64x64@1,1 sound=played");
    return 0;
}
