#include "volume_cli.h"
#include <astra/runtime.h>
#include <astra/status.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct Frame {
    uint64_t epoch;
    AstraResult result;
    uint32_t count;
    AstraVolumeRecord records[2];
} Frame;

static Frame frames[3];
static uint32_t frame_count, frame_at, waits;
static uint64_t clock_now;
static uint32_t removal_states[3], removal_status[3], removal_observations;

AstraResult astra_volume_snapshot_open(AstraHandle service, AstraVolumeSnapshot *snapshot)
{
    assert(service == 1u && frame_at < frame_count);
    Frame *frame = &frames[frame_at];
    if (frame->result != ASTRA_OK) return frame->result;
    *snapshot = (AstraVolumeSnapshot){.epoch = frame->epoch, .records = frame->records,
        .count = frame->count};
    return ASTRA_OK;
}

void astra_volume_snapshot_close(AstraVolumeSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
}

AstraResult astra_volume_watch_receive(const AstraVolumeWatch *watch, AstraVolumeChange *change)
{
    (void)watch;
    (void)change;
    return ASTRA_ERROR_WOULD_BLOCK;
}

AstraResult astra_volume_removal_advance(AstraHandle administrator,
    const AstraVolumeSnapshot *snapshot, AstraVolumeRemoval *removal)
{
    assert(administrator == 3u && snapshot->epoch == frames[frame_at].epoch);
    ++removal_observations;
    removal->state = removal_states[frame_at];
    removal->status = removal_status[frame_at];
    return ASTRA_OK;
}

uint64_t astra_clock_monotonic(void)
{
    return clock_now;
}

uint32_t astra_wait_one(uint32_t handle, uint64_t deadline_ns, uint32_t *detail)
{
    assert(handle == 2u && deadline_ns > clock_now);
    (void)detail;
    ++waits;
    clock_now = deadline_ns;
    if (frame_at + 1u < frame_count) ++frame_at;
    return ASTRA_SYSCALL_TIMED_OUT;
}

static void reset(uint32_t count)
{
    memset(frames, 0, sizeof(frames));
    frame_count = count;
    frame_at = waits = 0u;
    removal_observations = 0u;
    clock_now = 0u;
    for (uint32_t at = 0u; at < count; ++at) frames[at].epoch = 7u;
}

static void row(uint32_t frame, uint32_t state, uint64_t generation, uint32_t status)
{
    frames[frame].count = 1u;
    frames[frame].records[0] = (AstraVolumeRecord){.id = 9u, .generation = generation,
        .kind = ASTRA_VOLUME_MOUNT, .state = state, .status = status};
    strcpy(frames[frame].records[0].name, "/cd0");
}

static void selectors(void)
{
    uint64_t number;
    assert(volume_cli_number("18446744073709551615", &number) && number == UINT64_MAX);
    assert(!volume_cli_number("18446744073709551616", &number));
    assert(!volume_cli_number("0", &number) && !volume_cli_number("", &number));
    assert(!volume_cli_number("-1", &number) && !volume_cli_number("1/2", &number));
    reset(1u);
    row(0u, ASTRA_VOLUME_UNMOUNTED, 1u, 0u);
    frames[0].records[1] = frames[0].records[0];
    frames[0].records[1].id = 10u;
    frames[0].records[1].state = ASTRA_VOLUME_MOUNTED;
    frames[0].count = 2u;
    VolumeCli cli = {.metadata = 1u};
    AstraVolumeRecord record;
    AstraVolumeReference reference;
    assert(volume_cli_select(&cli, "CD0", 0u, &record, &reference, NULL));
    assert(reference.epoch == 7u && reference.id == 10u);
    assert(volume_cli_select(&cli, "/cd0", ASTRA_VOLUME_MOUNT, &record, &reference, NULL));
    assert(reference.id == 10u);
    assert(volume_cli_select(&cli, "9", ASTRA_VOLUME_MOUNT, &record, &reference, NULL));
    assert(reference.id == 9u);
    assert(!volume_cli_select(&cli, "/cd0/folder", 0u, &record, &reference, NULL));
}

static void lifecycle(void)
{
    VolumeCli cli = {.metadata = 1u, .watch = {.receive = 2u}};
    AstraVolumeReference reference = {7u, 9u, 1u};
    AstraVolumeRecord record;
    reset(2u);
    row(0u, ASTRA_VOLUME_MOUNTING, 1u, 0u);
    row(1u, ASTRA_VOLUME_MOUNTED, 1u, 0u);
    assert(volume_cli_wait(&cli, &reference, ASTRA_VOLUME_MOUNTED, 0, &record) && waits == 1u);
    reset(2u);
    row(0u, ASTRA_VOLUME_UNMOUNTED, 2u, 0u);
    assert(volume_cli_wait(&cli, &reference, ASTRA_VOLUME_UNMOUNTED, 1, &record) && waits == 1u);
    reset(2u);
    row(0u, ASTRA_VOLUME_LOST, 2u, ASTRA_STATUS_LOST);
    assert(volume_cli_wait(&cli, &reference, 0u, 1, &record) && waits == 1u);
    reset(1u);
    row(0u, ASTRA_VOLUME_UNMOUNTED, 1u, ASTRA_STATUS_IO);
    assert(!volume_cli_wait(&cli, &reference, 0u, 1, &record) && waits == 0u);
    reset(1u);
    row(0u, ASTRA_VOLUME_FAILED, 1u, ASTRA_STATUS_IO);
    assert(!volume_cli_wait(&cli, &reference, ASTRA_VOLUME_MOUNTED, 0, &record) && waits == 0u);
    reset(1u);
    row(0u, ASTRA_VOLUME_MOUNTED, 2u, 0u);
    assert(!volume_cli_wait(&cli, &reference, ASTRA_VOLUME_MOUNTED, 0, &record) && waits == 0u);
    reset(1u);
    frames[0].epoch = 8u;
    assert(!volume_cli_wait(&cli, &reference, 0u, 1, &record));
    reset(2u);
    frames[0].result = ASTRA_ERROR_BUSY;
    row(1u, ASTRA_VOLUME_MOUNTED, 1u, 0u);
    assert(volume_cli_wait(&cli, &reference, ASTRA_VOLUME_MOUNTED, 0, &record) && waits == 1u);
    reset(1u);
    row(0u, ASTRA_VOLUME_QUIESCING, 1u, 0u);
    assert(!volume_cli_wait(&cli, &reference, 0u, 1, &record));
    assert(clock_now == UINT64_C(60000000000) && waits == 600u);
    cli.administrator = 3u;
    AstraVolumeRemoval removal = {.mount = reference};
    reset(2u);
    removal_states[0] = ASTRA_VOLUME_REMOVAL_PENDING;
    removal_states[1] = ASTRA_VOLUME_REMOVAL_COMPLETE;
    assert(volume_cli_wait_removal(&cli, &removal) && waits == 1u && removal_observations == 2u);
    reset(1u);
    removal_states[0] = ASTRA_VOLUME_REMOVAL_REFUSED;
    removal_status[0] = ASTRA_STATUS_BUSY;
    assert(!volume_cli_wait_removal(&cli, &removal) && waits == 0u);
    reset(1u);
    removal_states[0] = ASTRA_VOLUME_REMOVAL_PROVIDER_RETIRED;
    assert(!volume_cli_wait_removal(&cli, &removal) && waits == 0u);
}

int main(void)
{
    selectors();
    lifecycle();
    puts("volume CLI exact selectors, admission, rollback, cleanup, epoch and timeout PASS");
    return 0;
}
