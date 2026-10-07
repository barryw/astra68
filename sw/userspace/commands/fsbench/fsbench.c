/*
 * fsbench -- the filesystem baseline (docs/FILESYSTEM_PRODUCTION_PRD.md,
 * "Acceptance and performance gates").
 *
 *   fsbench --label NAME [--dir DIR] [--cold-handshake] [--only W]...
 *
 * Runs each workload over DIR (default /work/fsbench-NAME) and writes one
 * JSON object to /work/.astra/fsbench-NAME.json, then prints
 * "fsbench: done NAME". With --cold-handshake it stops before each cold
 * workload, creates /work/.astra/fsbench-NAME.cold, prints
 * "fsbench: cold NAME", and waits (60 s at most) until a host driver has
 * dropped the Linux page cache and removed the file.
 *
 * held-miss gets its own handshake and its own marker,
 * /work/.astra/fsbench-NAME.heldmiss.cold: it needs `big2` cold and `big`
 * left warm, and the shared marker's host-side handler drops the whole
 * Linux page cache, which would evict `big` too. A host driver that only
 * understands the shared marker (drops everything) still unblocks this one
 * correctly -- it just contaminates the measurement again, which is why it
 * is a separate marker rather than a separate protocol.
 *
 * Times are CLOCK_MONOTONIC, which Astra derives from QEMU's guest timebase:
 * 12.5 MHz, so MC68040 cycles = ns / 80. "host_commands" and "max_inflight"
 * are the HostFS host.channel counters around the workload, when METRICS: is
 * granted; null otherwise.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/metrics.h>
#include <astra/program.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

ASTRA_PROGRAM("fsbench", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define FILES 200u
#define BIG_BYTES (4u * 1024u * 1024u)
#define CHUNK (64u * 1024u)
#define SMALL 4096u
#define RANDOM_OPS 1024u
#define APPENDS 1000u
#define APPEND_BYTES 100u
#define SAVES 20u
#define SAVE_BYTES (64u * 1024u)
#define SCANS 20u
#define SPAWNS 10u
#define HELD_READS 200u
#define SAMPLES_MAX 1024u
#define NS_PER_CYCLE 80u

typedef struct Result {
    const char *name;
    uint32_t count;
    uint64_t samples[SAMPLES_MAX];
    uint64_t total_ns;
    uint64_t bytes;
    uint64_t commands_before;
    uint64_t commands_after;
    uint64_t max_inflight;
    int metrics;
    int ran;
    int failed;
} Result;

enum {
    W_STAT_COLD, W_STAT_WARM, W_OPEN_COLD, W_OPEN_WARM, W_READDIR,
    W_SEQ_WRITE, W_SEQ_READ_COLD, W_SEQ_READ_WARM, W_RAND_READ,
    W_RAND_WRITE, W_APPEND, W_DURABLE, W_HELD_MISS, W_SPAWN, W_COUNT
};

static const char *const names[W_COUNT] = {
    "stat-cold", "stat-warm", "open-close-cold", "open-close-warm",
    "readdir", "seq-write", "seq-read-cold", "seq-read-warm", "rand-read",
    "rand-write", "append", "durable-save", "held-miss", "spawn"
};

static Result results[W_COUNT];
static uint8_t chunk[CHUNK];
static char directory[128];
static char label[48];
static int handshake;
static int only[W_COUNT];
static int any_only;

static uint64_t now_ns(void)
{
    struct timespec time;

    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) +
           (uint64_t)time.tv_nsec;
}

static int wanted(uint32_t workload)
{
    return !any_only || only[workload];
}

static void file_name(char *out, size_t capacity, const char *leaf,
                      uint32_t index)
{
    (void)snprintf(out, capacity, "%s/%s%03u", directory, leaf,
                   (unsigned)index);
}

/* The HostFS channel counters, or 0 when METRICS: is not granted. */
static int host_counter(const char *name, uint64_t *value)
{
    AstraMetricRecord record;
    int fd = open("/metrics/snapshot", O_RDONLY);
    int found = 0;

    if (fd < 0)
        return 0;
    while (read(fd, &record, sizeof(record)) == (ssize_t)sizeof(record)) {
        if (record.size == sizeof(record) &&
            strncmp(record.group, "host.channel", sizeof(record.group)) == 0 &&
            strncmp(record.name, name, sizeof(record.name)) == 0) {
            *value = astra_metric_record_value(&record);
            found = 1;
            break;
        }
    }
    (void)close(fd);
    return found;
}

static void begin(Result *result, uint32_t workload)
{
    result->name = names[workload];
    result->count = 0u;
    result->total_ns = 0u;
    result->bytes = 0u;
    result->ran = 1;
    result->metrics = host_counter("commands", &result->commands_before);
}

static void sample(Result *result, uint64_t elapsed)
{
    if (result->count < SAMPLES_MAX)
        result->samples[result->count++] = elapsed;
    result->total_ns += elapsed;
}

static void end(Result *result)
{
    if (result->metrics)
        result->metrics =
            host_counter("commands", &result->commands_after) &&
            host_counter("max_inflight", &result->max_inflight);
}

static int compare(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;

    return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t percentile(Result *result, uint32_t percent)
{
    uint32_t index;

    if (result->count == 0u)
        return 0u;
    qsort(result->samples, result->count, sizeof(result->samples[0]),
          compare);
    index = (result->count * percent + 99u) / 100u;
    return result->samples[index == 0u ? 0u : index - 1u];
}

/* `suffix` distinguishes which marker a waiting host driver sees, so it can
 * tell the shared cold point (drop everything) from held-miss's own (drop
 * only big2) without guessing from call order. */
static int cold_point_named(const char *suffix)
{
    char path[160];
    int fd;

    if (!handshake)
        return 0;
    (void)snprintf(path, sizeof(path), "/work/.astra/fsbench-%s%s.cold",
                   label, suffix);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    (void)close(fd);
    printf("fsbench: cold %s%s\n", label, suffix);
    fflush(stdout);
    for (uint32_t waited = 0u; waited < 600u; ++waited) {
        struct stat st;
        struct timespec pause = {0, 100000000L};

        if (stat(path, &st) != 0 && errno == ENOENT)
            return 0;
        (void)nanosleep(&pause, NULL);
    }
    return -1;
}

static int cold_point(void)
{
    return cold_point_named("");
}

static int held_miss_cold_point(void)
{
    return cold_point_named(".heldmiss");
}

static int write_whole(const char *path, uint32_t bytes)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    uint32_t done = 0u;

    if (fd < 0)
        return -1;
    while (done < bytes) {
        uint32_t part = bytes - done < CHUNK ? bytes - done : CHUNK;
        ssize_t moved = write(fd, chunk, part);

        if (moved <= 0) {
            (void)close(fd);
            return -1;
        }
        done += (uint32_t)moved;
    }
    return close(fd);
}

static int prepare(void)
{
    char path[160];

    for (uint32_t index = 0u; index < CHUNK; ++index)
        chunk[index] = (uint8_t)(index * 131u + 7u);
    if (mkdir(directory, 0755) != 0) {
        fprintf(stderr, "fsbench: %s exists or cannot be made; "
                "use a new --label\n", directory);
        return -1;
    }
    for (uint32_t index = 0u; index < FILES; ++index) {
        file_name(path, sizeof(path), "f", index);
        if (write_whole(path, 64u) != 0)
            return -1;
    }
    (void)snprintf(path, sizeof(path), "%s/big", directory);
    if (write_whole(path, BIG_BYTES) != 0)
        return -1;
    (void)snprintf(path, sizeof(path), "%s/big2", directory);
    return write_whole(path, BIG_BYTES);
}

static void run_stat(Result *result)
{
    char path[160];

    for (uint32_t index = 0u; index < FILES; ++index) {
        struct stat st;
        uint64_t start;

        file_name(path, sizeof(path), "f", index);
        start = now_ns();
        if (stat(path, &st) != 0)
            result->failed = 1;
        sample(result, now_ns() - start);
    }
}

static void run_open_close(Result *result)
{
    char path[160];

    for (uint32_t index = 0u; index < FILES; ++index) {
        uint64_t start;
        int fd;

        file_name(path, sizeof(path), "f", index);
        start = now_ns();
        fd = open(path, O_RDONLY);
        if (fd < 0 || close(fd) != 0)
            result->failed = 1;
        sample(result, now_ns() - start);
    }
}

static void run_readdir(Result *result)
{
    for (uint32_t scan = 0u; scan < SCANS; ++scan) {
        uint64_t start = now_ns();
        DIR *listing = opendir(directory);
        uint32_t entries = 0u;

        if (listing == NULL) {
            result->failed = 1;
            return;
        }
        while (readdir(listing) != NULL)
            ++entries;
        (void)closedir(listing);
        sample(result, now_ns() - start);
        if (entries < FILES)
            result->failed = 1;
    }
}

static void run_sequential(Result *result, int writing)
{
    char path[160];
    int fd;

    (void)snprintf(path, sizeof(path), "%s/%s", directory,
                   writing ? "written" : "big");
    fd = writing ? open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644) :
                   open(path, O_RDONLY);
    if (fd < 0) {
        result->failed = 1;
        return;
    }
    for (uint32_t done = 0u; done < BIG_BYTES; done += CHUNK) {
        uint64_t start = now_ns();
        ssize_t moved = writing ? write(fd, chunk, CHUNK) :
                                  read(fd, chunk, CHUNK);

        sample(result, now_ns() - start);
        if (moved != (ssize_t)CHUNK) {
            result->failed = 1;
            break;
        }
        result->bytes += CHUNK;
    }
    (void)close(fd);
}

static void run_random(Result *result, int writing)
{
    char path[160];
    uint32_t state = 0x68040u;
    int fd;

    (void)snprintf(path, sizeof(path), "%s/big", directory);
    fd = open(path, writing ? O_RDWR : O_RDONLY);
    if (fd < 0) {
        result->failed = 1;
        return;
    }
    for (uint32_t op = 0u; op < RANDOM_OPS; ++op) {
        off_t offset;
        uint64_t start;
        ssize_t moved;

        state = state * 1664525u + 1013904223u;
        offset = (off_t)((state >> 8) % (BIG_BYTES / SMALL)) * SMALL;
        start = now_ns();
        moved = writing ? pwrite(fd, chunk, SMALL, offset) :
                          pread(fd, chunk, SMALL, offset);
        sample(result, now_ns() - start);
        if (moved != (ssize_t)SMALL) {
            result->failed = 1;
            break;
        }
        result->bytes += SMALL;
    }
    (void)close(fd);
}

static void run_append(Result *result)
{
    char path[160];
    int fd;

    (void)snprintf(path, sizeof(path), "%s/appended", directory);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (fd < 0) {
        result->failed = 1;
        return;
    }
    for (uint32_t op = 0u; op < APPENDS; ++op) {
        uint64_t start = now_ns();
        ssize_t moved = write(fd, chunk, APPEND_BYTES);

        sample(result, now_ns() - start);
        if (moved != (ssize_t)APPEND_BYTES) {
            result->failed = 1;
            break;
        }
        result->bytes += APPEND_BYTES;
    }
    (void)close(fd);
}

/* write, fsync, rename over the target, fsync the directory. */
static void run_durable(Result *result)
{
    char temporary[160];
    char target[160];

    (void)snprintf(temporary, sizeof(temporary), "%s/save.tmp", directory);
    (void)snprintf(target, sizeof(target), "%s/save", directory);
    for (uint32_t op = 0u; op < SAVES; ++op) {
        uint64_t start = now_ns();
        int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int ok = fd >= 0;
        int parent;

        for (uint32_t done = 0u; ok && done < SAVE_BYTES; done += CHUNK)
            ok = write(fd, chunk, CHUNK) == (ssize_t)CHUNK;
        ok = ok && fsync(fd) == 0;
        if (fd >= 0)
            ok = close(fd) == 0 && ok;
        ok = ok && rename(temporary, target) == 0;
        parent = open(directory, O_RDONLY | O_DIRECTORY);
        ok = ok && parent >= 0 && fsync(parent) == 0;
        if (parent >= 0)
            (void)close(parent);
        sample(result, now_ns() - start);
        if (!ok) {
            result->failed = 1;
            break;
        }
        result->bytes += SAVE_BYTES;
    }
}

typedef struct Miss {
    pthread_t thread;
    int fd;
    int failed;
} Miss;

static void *read_miss(void *argument)
{
    static uint8_t buffer[CHUNK];
    Miss *miss = argument;

    for (uint32_t done = 0u; done < BIG_BYTES; done += CHUNK)
        if (read(miss->fd, buffer, CHUNK) != (ssize_t)CHUNK) {
            miss->failed = 1;
            break;
        }
    return NULL;
}

/* Cached 4 KiB reads of `big` while another thread reads cold `big2`. */
static void run_held_miss(Result *result)
{
    char cold[160];
    char warm[160];
    Miss miss = {0};
    int fd;

    (void)snprintf(cold, sizeof(cold), "%s/big2", directory);
    (void)snprintf(warm, sizeof(warm), "%s/big", directory);
    miss.fd = open(cold, O_RDONLY);
    fd = open(warm, O_RDONLY);
    if (miss.fd < 0 || fd < 0 ||
        pthread_create(&miss.thread, NULL, read_miss, &miss) != 0) {
        result->failed = 1;
        if (miss.fd >= 0)
            (void)close(miss.fd);
        if (fd >= 0)
            (void)close(fd);
        return;
    }
    for (uint32_t op = 0u; op < HELD_READS; ++op) {
        uint64_t start = now_ns();
        ssize_t moved = pread(fd, chunk, SMALL, (off_t)(op % 64u) * SMALL);

        sample(result, now_ns() - start);
        if (moved != (ssize_t)SMALL)
            result->failed = 1;
        result->bytes += SMALL;
    }
    (void)pthread_join(miss.thread, NULL);
    result->failed |= miss.failed;
    (void)close(fd);
    (void)close(miss.fd);
}

/* Process start: fork, exec /commands/lua with an empty chunk, wait. */
static void run_spawn(Result *result)
{
    for (uint32_t op = 0u; op < SPAWNS; ++op) {
        uint64_t start = now_ns();
        int status = 0;
        pid_t child = fork();

        if (child == 0) {
            char *const argv[] = {"lua", "-e", "", NULL};

            execv("/commands/lua", argv);
            _exit(127);
        }
        if (child < 0 || waitpid(child, &status, 0) != child ||
            !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            result->failed = 1;
        sample(result, now_ns() - start);
    }
}

static int report(void)
{
    char temporary[128];
    char path[128];
    FILE *out;

    (void)snprintf(temporary, sizeof(temporary),
                   "/work/.astra/fsbench-%s.json.tmp", label);
    (void)snprintf(path, sizeof(path), "/work/.astra/fsbench-%s.json", label);
    out = fopen(temporary, "w");
    if (out == NULL)
        return -1;
    fprintf(out, "{\"label\":\"%s\",\"ns_per_cycle\":%u,\"workloads\":{",
            label, NS_PER_CYCLE);
    for (uint32_t index = 0u, first = 1u; index < W_COUNT; ++index) {
        Result *result = &results[index];
        uint64_t median;
        uint64_t p95;

        if (!result->ran)
            continue;
        median = percentile(result, 50u);
        p95 = percentile(result, 95u);
        fprintf(out, "%s\"%s\":{\"ops\":%u,\"failed\":%s,"
                "\"median_ns\":%llu,\"p95_ns\":%llu,\"total_ns\":%llu,"
                "\"cycles_per_op\":%llu,\"bytes\":%llu,",
                first ? "" : ",", result->name, (unsigned)result->count,
                result->failed ? "true" : "false",
                (unsigned long long)median, (unsigned long long)p95,
                (unsigned long long)result->total_ns,
                (unsigned long long)(median / NS_PER_CYCLE),
                (unsigned long long)result->bytes);
        if (result->bytes != 0u && result->total_ns != 0u)
            fprintf(out, "\"throughput_bps\":%llu,",
                    (unsigned long long)(result->bytes *
                                         UINT64_C(1000000000) /
                                         result->total_ns));
        else
            fprintf(out, "\"throughput_bps\":null,");
        if (result->metrics)
            fprintf(out, "\"host_commands\":%llu,\"max_inflight\":%llu}",
                    (unsigned long long)(result->commands_after -
                                         result->commands_before),
                    (unsigned long long)result->max_inflight);
        else
            fprintf(out, "\"host_commands\":null,\"max_inflight\":null}");
        first = 0u;
    }
    fprintf(out, "}}\n");
    if (fclose(out) != 0 || rename(temporary, path) != 0)
        return -1;
    printf("fsbench: done %s\n", label);
    return 0;
}

int main(int argc, char **argv)
{
    int failed = 0;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--label") == 0 && index + 1 < argc)
            (void)snprintf(label, sizeof(label), "%s", argv[++index]);
        else if (strcmp(argv[index], "--dir") == 0 && index + 1 < argc)
            (void)snprintf(directory, sizeof(directory), "%s",
                           argv[++index]);
        else if (strcmp(argv[index], "--cold-handshake") == 0)
            handshake = 1;
        else if (strcmp(argv[index], "--only") == 0 && index + 1 < argc) {
            const char *name = argv[++index];
            uint32_t at = 0u;

            while (at < W_COUNT && strcmp(names[at], name) != 0)
                ++at;
            if (at == W_COUNT) {
                fprintf(stderr, "fsbench: no workload %s\n", name);
                return 2;
            }
            only[at] = 1;
            any_only = 1;
        } else {
            fprintf(stderr, "usage: fsbench --label NAME [--dir DIR] "
                    "[--cold-handshake] [--only WORKLOAD]...\n");
            return 2;
        }
    }
    if (label[0] == '\0')
        return 2;
    if (directory[0] == '\0')
        (void)snprintf(directory, sizeof(directory), "/work/fsbench-%s",
                       label);
    (void)mkdir("/work/.astra", 0755);
    if (prepare() != 0) {
        fprintf(stderr, "fsbench: prepare failed: %s\n", strerror(errno));
        return 1;
    }
    for (uint32_t workload = 0u; workload < W_COUNT; ++workload) {
        Result *result = &results[workload];
        int cold = workload == W_STAT_COLD || workload == W_OPEN_COLD ||
                   workload == W_SEQ_READ_COLD;

        if (!wanted(workload))
            continue;
        if (cold && cold_point() != 0) {
            fprintf(stderr, "fsbench: no cold handshake\n");
            return 1;
        }
        if (workload == W_HELD_MISS && held_miss_cold_point() != 0) {
            fprintf(stderr, "fsbench: no held-miss cold handshake\n");
            return 1;
        }
        begin(result, workload);
        switch (workload) {
        case W_STAT_COLD: case W_STAT_WARM: run_stat(result); break;
        case W_OPEN_COLD: case W_OPEN_WARM: run_open_close(result); break;
        case W_READDIR: run_readdir(result); break;
        case W_SEQ_WRITE: run_sequential(result, 1); break;
        case W_SEQ_READ_COLD: case W_SEQ_READ_WARM:
            run_sequential(result, 0);
            break;
        case W_RAND_READ: run_random(result, 0); break;
        case W_RAND_WRITE: run_random(result, 1); break;
        case W_APPEND: run_append(result); break;
        case W_DURABLE: run_durable(result); break;
        case W_HELD_MISS: run_held_miss(result); break;
        default: run_spawn(result); break;
        }
        end(result);
        failed |= result->failed;
    }
    if (report() != 0) {
        fprintf(stderr, "fsbench: cannot write the result\n");
        return 1;
    }
    return failed ? 1 : 0;
}
