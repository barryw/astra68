/*
 * Physical output gate for decoded media. RGB565 pixels and 48 kHz stereo
 * signed-16-le PCM stay in separate files so the decoder can be measured
 * independently from Astra's HDMI presentation path.
 *
 * usage: SDLMediaPlayback WIDTH HEIGHT FPS_NUM FPS_DEN FRAMES VIDEO AUDIO
 */
#include <SDL.h>
#include <astra/runtime.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AUDIO_RATE = 48000, AUDIO_CHANNELS = 2, AUDIO_FRAME_BYTES = 4,
       AUDIO_BUFFER_BYTES = 8192,
       MAX_FRAME_BYTES = 1920 * 1080 * 2, VIDEO_BUFFER_BYTES = 144 * 1024 * 1024 };

typedef struct VideoQueue {
    FILE *input;
    uint8_t *frames;
    size_t frame_bytes;
    uint32_t capacity;
    uint32_t produced;
    uint32_t consumed;
    uint64_t clock_frequency;
    uint64_t max_read_us;
    int stopped;
    int finished;
    SDL_mutex *mutex;
    SDL_cond *ready;
    SDL_cond *room;
    SDL_Thread *thread;
} VideoQueue;

static int fail(const char *marker)
{
    char line[192];
    (void)snprintf(line, sizeof(line), "%s %s", marker, SDL_GetError());
    (void)astra_log(line);
    puts(line);
    return 1;
}

static int number(const char *text, uint32_t *result)
{
    char *end;
    unsigned long value;

    if (text[0] == '\0' || text[0] == '-') return 0;
    value = strtoul(text, &end, 10);
    if (*end != '\0' || value == 0ul || value > UINT32_MAX) return 0;
    *result = (uint32_t)value;
    return 1;
}

static int SDLCALL read_video(void *opaque)
{
    VideoQueue *queue = opaque;

    for (;;) {
        uint32_t slot;
        uint64_t start, elapsed;
        size_t count;

        SDL_LockMutex(queue->mutex);
        while (!queue->stopped &&
               queue->produced - queue->consumed == queue->capacity)
            SDL_CondWait(queue->room, queue->mutex);
        if (queue->stopped) {
            SDL_UnlockMutex(queue->mutex);
            break;
        }
        slot = queue->produced % queue->capacity;
        SDL_UnlockMutex(queue->mutex);
        start = SDL_GetPerformanceCounter();
        count = fread(queue->frames + (size_t)slot * queue->frame_bytes,
                      1u, queue->frame_bytes, queue->input);
        elapsed = (SDL_GetPerformanceCounter() - start) * 1000000u /
                  queue->clock_frequency;
        SDL_LockMutex(queue->mutex);
        if (elapsed > queue->max_read_us) queue->max_read_us = elapsed;
        if (count != queue->frame_bytes) {
            queue->finished = 1;
            SDL_CondBroadcast(queue->ready);
            SDL_UnlockMutex(queue->mutex);
            break;
        }
        ++queue->produced;
        SDL_CondSignal(queue->ready);
        SDL_UnlockMutex(queue->mutex);
    }
    return 0;
}

static int open_video_queue(VideoQueue *queue, FILE *input,
                            size_t frame_bytes, uint32_t frames,
                            uint64_t frequency)
{
    queue->input = input;
    queue->frame_bytes = frame_bytes;
    queue->clock_frequency = frequency;
    queue->capacity = VIDEO_BUFFER_BYTES / frame_bytes;
    if (queue->capacity > frames) queue->capacity = frames;
    if (queue->capacity == 0u) return 0;
    queue->frames = malloc((size_t)queue->capacity * frame_bytes);
    queue->mutex = SDL_CreateMutex();
    queue->ready = SDL_CreateCond();
    queue->room = SDL_CreateCond();
    if (queue->frames == NULL || queue->mutex == NULL ||
        queue->ready == NULL || queue->room == NULL)
        return 0;
    queue->thread = SDL_CreateThread(read_video, "media-read", queue);
    return queue->thread != NULL;
}

static int prime_video(VideoQueue *queue, uint32_t count)
{
    int ready;

    SDL_LockMutex(queue->mutex);
    while (!queue->finished && queue->produced - queue->consumed < count)
        SDL_CondWait(queue->ready, queue->mutex);
    ready = queue->produced - queue->consumed >= count;
    SDL_UnlockMutex(queue->mutex);
    return ready;
}

static uint8_t *next_video(VideoQueue *queue, uint64_t *wait_us)
{
    uint64_t start = SDL_GetPerformanceCounter();
    uint8_t *frame = NULL;

    SDL_LockMutex(queue->mutex);
    while (!queue->finished && queue->produced == queue->consumed)
        SDL_CondWait(queue->ready, queue->mutex);
    if (queue->produced != queue->consumed)
        frame = queue->frames +
                (size_t)(queue->consumed % queue->capacity) *
                queue->frame_bytes;
    SDL_UnlockMutex(queue->mutex);
    *wait_us = (SDL_GetPerformanceCounter() - start) * 1000000u /
               queue->clock_frequency;
    return frame;
}

static void release_video(VideoQueue *queue)
{
    SDL_LockMutex(queue->mutex);
    ++queue->consumed;
    SDL_CondSignal(queue->room);
    SDL_UnlockMutex(queue->mutex);
}

static void close_video_queue(VideoQueue *queue)
{
    if (queue->mutex != NULL) {
        SDL_LockMutex(queue->mutex);
        queue->stopped = 1;
        if (queue->room != NULL) SDL_CondBroadcast(queue->room);
        SDL_UnlockMutex(queue->mutex);
    }
    if (queue->thread != NULL) SDL_WaitThread(queue->thread, NULL);
    if (queue->room != NULL) SDL_DestroyCond(queue->room);
    if (queue->ready != NULL) SDL_DestroyCond(queue->ready);
    if (queue->mutex != NULL) SDL_DestroyMutex(queue->mutex);
    free(queue->frames);
}

static int queue_audio(SDL_AudioDeviceID device, FILE *input,
                       uint64_t *queued_total)
{
    uint8_t buffer[AUDIO_BUFFER_BYTES];

    for (;;) {
        size_t count = fread(buffer, 1u, sizeof(buffer), input);

        if (count == 0u) {
            if (ferror(input)) return -1;
            break;
        }
        if (count % AUDIO_FRAME_BYTES != 0u ||
            SDL_QueueAudio(device, buffer, (uint32_t)count) != 0)
            return -1;
        *queued_total += count;
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t width, height, fps_num, fps_den, frames;
    size_t frame_bytes;
    FILE *video = NULL, *audio = NULL;
    VideoQueue queue = {0};
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec wanted = {0}, obtained = {0};
    uint64_t start, frequency, last_present = 0u, queued_total = 0u;
    uint64_t max_gap_us = 0u, max_late_us = 0u;
    uint64_t max_wait_us = 0u, audio_load_ms = 0u, prebuffer_ms = 0u;
    uint64_t read_us;
    uint64_t max_upload_us = 0u, max_copy_us = 0u, max_present_us = 0u;
    uint32_t gaps_over_50ms = 0u, late_frames = 0u, low_audio = 0u;
    uint32_t presented = 0u;
    int status = 1;
    char line[256];

    if (argc != 8 || !number(argv[1], &width) ||
        !number(argv[2], &height) || !number(argv[3], &fps_num) ||
        !number(argv[4], &fps_den) || !number(argv[5], &frames) ||
        width > 1920u || height > 1080u ||
        (uint64_t)width * height * 2u > MAX_FRAME_BYTES ||
        fps_den > 100000u || fps_num < fps_den ||
        (uint64_t)fps_num > 120u * (uint64_t)fps_den ||
        (uint64_t)frames * fps_den > 120u * (uint64_t)fps_num) {
        puts("usage: SDLMediaPlayback WIDTH HEIGHT FPS_NUM FPS_DEN FRAMES VIDEO AUDIO");
        return 1;
    }
    frame_bytes = (size_t)width * height * 2u;
    video = fopen(argv[6], "rb");
    audio = fopen(argv[7], "rb");
    if (video == NULL || audio == NULL) {
        puts("SDL_MEDIA_INPUT_FAIL");
        goto done;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        (void)fail("SDL_MEDIA_INIT_FAIL");
        goto done;
    }
    wanted.freq = AUDIO_RATE;
    wanted.format = AUDIO_S16LSB;
    wanted.channels = AUDIO_CHANNELS;
    wanted.samples = 1024u;
    device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if (device == 0 || obtained.freq != AUDIO_RATE ||
        obtained.format != AUDIO_S16LSB ||
        obtained.channels != AUDIO_CHANNELS) {
        (void)fail("SDL_MEDIA_AUDIO_FAIL");
        goto done;
    }
    window = SDL_CreateWindow("Media Playback Gate", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, (int)width,
                              (int)height, SDL_WINDOW_SHOWN);
    renderer = window != NULL ? SDL_CreateRenderer(window, -1, 0) : NULL;
    texture = renderer != NULL ? SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        (int)width, (int)height) : NULL;
    if (texture == NULL || SDL_SetTextureBlendMode(texture,
        SDL_BLENDMODE_NONE) != 0) {
        (void)fail("SDL_MEDIA_OUTPUT_FAIL");
        goto done;
    }

    frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0u) {
        (void)fail("SDL_MEDIA_CLOCK_FAIL");
        goto done;
    }
    {
        uint64_t loading = SDL_GetPerformanceCounter();
        uint64_t required_audio = (uint64_t)frames * fps_den *
            AUDIO_RATE * AUDIO_FRAME_BYTES / fps_num;

        if (queue_audio(device, audio, &queued_total) != 0 ||
            queued_total < required_audio) {
            (void)fail("SDL_MEDIA_SHORT_AUDIO");
            goto done;
        }
        audio_load_ms = (SDL_GetPerformanceCounter() - loading) * 1000u /
                        frequency;
    }
    {
        uint32_t wanted_frames;
        uint64_t buffering = SDL_GetPerformanceCounter();

        if (!open_video_queue(&queue, video, frame_bytes, frames, frequency)) {
            (void)fail("SDL_MEDIA_BUFFER_FAIL");
            goto done;
        }
        wanted_frames = queue.capacity;
        if (!prime_video(&queue, wanted_frames)) {
            (void)fail("SDL_MEDIA_SHORT_VIDEO");
            goto done;
        }
        prebuffer_ms = (SDL_GetPerformanceCounter() - buffering) * 1000u /
                       frequency;
    }
    SDL_PauseAudioDevice(device, 0);
    start = SDL_GetPerformanceCounter();
    (void)astra_log("SDL_MEDIA_PLAYBACK_START");
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        uint64_t deadline = start +
            ((uint64_t)frame * fps_den * frequency) / fps_num;
        uint64_t now, gap_us, late_us, wait_us;
        uint64_t render_start, upload_end, copy_end;
        uint8_t *pixels;
        SDL_Event event;

        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT) goto done;
        pixels = next_video(&queue, &wait_us);
        if (pixels == NULL) {
            (void)fail("SDL_MEDIA_READ_FAIL");
            goto done;
        }
        if (wait_us > max_wait_us) max_wait_us = wait_us;
        if (SDL_GetQueuedAudioSize(device) < 4096u)
            ++low_audio;
        while ((now = SDL_GetPerformanceCounter()) < deadline)
            SDL_Delay(1u);
        late_us = (now - deadline) * 1000000u / frequency;
        if (late_us > max_late_us) max_late_us = late_us;
        if (late_us > 16683u) ++late_frames;
        render_start = SDL_GetPerformanceCounter();
        if (SDL_UpdateTexture(texture, NULL, pixels, (int)(width * 2u)) != 0) {
            (void)fail("SDL_MEDIA_RENDER_FAIL");
            goto done;
        }
        release_video(&queue);
        upload_end = SDL_GetPerformanceCounter();
        if (SDL_RenderCopy(renderer, texture, NULL, NULL) != 0) {
            (void)fail("SDL_MEDIA_COPY_FAIL");
            goto done;
        }
        copy_end = SDL_GetPerformanceCounter();
        SDL_RenderPresent(renderer);
        now = SDL_GetPerformanceCounter();
        if ((upload_end - render_start) * 1000000u / frequency > max_upload_us)
            max_upload_us = (upload_end - render_start) * 1000000u / frequency;
        if ((copy_end - upload_end) * 1000000u / frequency > max_copy_us)
            max_copy_us = (copy_end - upload_end) * 1000000u / frequency;
        if ((now - copy_end) * 1000000u / frequency > max_present_us)
            max_present_us = (now - copy_end) * 1000000u / frequency;
        if (last_present != 0u) {
            gap_us = (now - last_present) * 1000000u / frequency;
            if (gap_us > max_gap_us) max_gap_us = gap_us;
            if (gap_us > 50000u) ++gaps_over_50ms;
        }
        last_present = now;
        ++presented;
        if ((frame + 1u) % 30u == 0u) {
            (void)snprintf(line, sizeof(line),
                "SDL_MEDIA_PROGRESS frame=%" PRIu32 " elapsed_ms=%" PRIu64
                " audio_queued_bytes=%" PRIu32,
                presented, (now - start) * 1000u / frequency,
                SDL_GetQueuedAudioSize(device));
            (void)astra_log(line);
        }
    }
    for (uint32_t wait = 0u; wait < 500u &&
         SDL_GetQueuedAudioSize(device) != 0u; ++wait)
        SDL_Delay(10u);
    (void)snprintf(line, sizeof(line),
        "SDL_MEDIA_PLAYBACK frames=%" PRIu32 " elapsed_ms=%" PRIu64
        " max_gap_us=%" PRIu64 " gaps_over_50ms=%" PRIu32
        " late_frames=%" PRIu32 " max_late_us=%" PRIu64
        " low_audio=%" PRIu32 " audio_bytes=%" PRIu64,
        presented, (last_present - start) * 1000u / frequency,
        max_gap_us, gaps_over_50ms, late_frames, max_late_us,
        low_audio, queued_total);
    (void)astra_log(line);
    puts(line);
    SDL_LockMutex(queue.mutex);
    read_us = queue.max_read_us;
    SDL_UnlockMutex(queue.mutex);
    (void)snprintf(line, sizeof(line),
        "SDL_MEDIA_STAGES read_us=%" PRIu64 " wait_us=%" PRIu64
        " prebuffer_ms=%" PRIu64 " ring_frames=%" PRIu32
        " audio_load_ms=%" PRIu64
        " upload_us=%" PRIu64 " copy_us=%" PRIu64
        " present_us=%" PRIu64,
        read_us, max_wait_us, prebuffer_ms, queue.capacity,
        audio_load_ms, max_upload_us, max_copy_us,
        max_present_us);
    (void)astra_log(line);
    puts(line);
    status = presented == frames && gaps_over_50ms == 0u &&
             late_frames == 0u && low_audio == 0u ? 0 : 2;
done:
    if (device != 0u) SDL_CloseAudioDevice(device);
    close_video_queue(&queue);
    if (texture != NULL) SDL_DestroyTexture(texture);
    if (renderer != NULL) SDL_DestroyRenderer(renderer);
    if (window != NULL) SDL_DestroyWindow(window);
    SDL_Quit();
    if (audio != NULL) fclose(audio);
    if (video != NULL) fclose(video);
    return status;
}
