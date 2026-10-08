/*
 * fsprobe -- per-operation cost of one warm stat and one 4 KiB pread, with
 * the kernel's own accounting of where it went (switches, per-process CPU
 * and syscalls). A test command: the lane's structural gate (Tasks 3b, 13).
 *
 *   fsprobe LABEL N
 *
 * Phases null, stat, read. Each phase: snapshot PROC, print
 * "fsprobe: begin PHASE", sleep 400 ms (the host starts a profile interval),
 * run N ops (null: none), snapshot PROC, print "fsprobe: end PHASE ..." with
 * the per-op latency distribution and the per-process runtime / syscall
 * deltas and scheduler deltas, then sleep 400 ms (the host stops).
 */
#define _POSIX_C_SOURCE 200809L

#include <astra/proc.h>
#include <astra/program.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

ASTRA_PROGRAM("fsprobe", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define MAX_OPS 40000u
#define MAX_PROCS 64u
#define BIG (4u * 1024u * 1024u)

static uint64_t samples[MAX_OPS];
static AstraProcSnapshot before[MAX_PROCS], after[MAX_PROCS];
static AstraSchedulerStats sched_before, sched_after;
static uint8_t buffer[65536];

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int read_file(const char *path, void *out, size_t capacity)
{
    int fd = open(path, O_RDONLY);
    size_t used = 0;
    if (fd < 0)
        return -1;
    for (;;) {
        ssize_t got = read(fd, (uint8_t *)out + used, capacity - used);
        if (got <= 0)
            break;
        used += (size_t)got;
        if (used == capacity)
            break;
    }
    close(fd);
    return (int)used;
}

static int snap(AstraProcSnapshot *procs, AstraSchedulerStats *sched)
{
    int bytes = read_file("/proc/snapshot", procs,
                          MAX_PROCS * sizeof(*procs));
    if (read_file("/proc/scheduler", sched, sizeof(*sched)) !=
        (int)sizeof(*sched))
        memset(sched, 0, sizeof(*sched));
    return bytes < 0 ? 0 : bytes / (int)sizeof(*procs);
}

static int compare(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void pause_ms(long ms)
{
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

int main(int argc, char **argv)
{
    static const char *const phases[] = {"null", "stat", "read"};
    char dir[96], small[128], big[128];
    uint32_t n;
    int fd;

    long pause = 600;

    if (argc != 3 && argc != 4)
        return 2;
    if (argc == 4)
        pause = strtol(argv[3], NULL, 10);
    n = (uint32_t)strtoul(argv[2], NULL, 10);
    if (n == 0 || n > MAX_OPS)
        return 2;
    snprintf(dir, sizeof dir, "/work/fsprobe-%s", argv[1]);
    snprintf(small, sizeof small, "%s/f", dir);
    snprintf(big, sizeof big, "%s/big", dir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
        return 3;
    fd = open(small, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, buffer, 64) != 64 || close(fd) != 0)
        return 4;
    fd = open(big, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return 5;
    for (uint32_t done = 0; done < BIG; done += sizeof buffer)
        if (write(fd, buffer, sizeof buffer) != (ssize_t)sizeof buffer)
            return 5;
    close(fd);
    fd = open(big, O_RDONLY);
    if (fd < 0)
        return 6;
    for (uint32_t i = 0; i < 500; ++i) { /* warm both paths */
        struct stat st;
        if (stat(small, &st) != 0 || pread(fd, buffer, 4096, (off_t)(i % 1024) * 4096) != 4096)
            return 7;
    }
    for (uint32_t p = 0; p < 3; ++p) {
        uint32_t state = 0x68040u, ops = p == 0 ? 0 : n;
        int nb, na;
        uint64_t start, total;

        nb = snap(before, &sched_before);
        printf("fsprobe: begin %s\n", phases[p]);
        fflush(stdout);
        pause_ms(pause);
        start = now_ns();
        for (uint32_t i = 0; i < ops; ++i) {
            uint64_t t0 = now_ns();
            if (p == 1) {
                struct stat st;
                if (stat(small, &st) != 0)
                    return 8;
            } else {
                state = state * 1664525u + 1013904223u;
                if (pread(fd, buffer, 4096,
                          (off_t)((state >> 8) % (BIG / 4096)) * 4096) != 4096)
                    return 9;
            }
            samples[i] = now_ns() - t0;
        }
        total = now_ns() - start;
        na = snap(after, &sched_after);
        printf("fsprobe: end %s\n", phases[p]);
        fflush(stdout);
        pause_ms(600);
        if (ops) {
            qsort(samples, ops, sizeof samples[0], compare);
            printf("fsprobe: lat %s n=%u total=%llu med=%llu\n", phases[p],
                   ops, (unsigned long long)total,
                   (unsigned long long)samples[ops / 2]);
            printf("fsprobe: tail %s p95=%llu p99=%llu min=%llu\n", phases[p],
                   (unsigned long long)samples[(ops * 95) / 100],
                   (unsigned long long)samples[(ops * 99) / 100],
                   (unsigned long long)samples[0]);
        }
        printf("fsprobe: sw %s win=%llu all=%u same=%u cross=%u\n", phases[p],
               (unsigned long long)(sched_after.now_ns - sched_before.now_ns),
               sched_after.context_switches - sched_before.context_switches,
               sched_after.same_address_space_switches -
                   sched_before.same_address_space_switches,
               sched_after.cross_address_space_switches -
                   sched_before.cross_address_space_switches);
        printf("fsprobe: why %s blk=%u pre=%u idle=%u wait=%u sys=%u\n",
               phases[p],
               sched_after.block_switches - sched_before.block_switches,
               sched_after.preempt_switches - sched_before.preempt_switches,
               sched_after.idle_switches - sched_before.idle_switches,
               sched_after.wait_blocks - sched_before.wait_blocks,
               sched_after.syscalls_low - sched_before.syscalls_low);
        for (int a = 0; a < na; ++a)
            for (int b = 0; b < nb; ++b)
                if (after[a].process.id == before[b].process.id &&
                    after[a].process.generation ==
                        before[b].process.generation) {
                    uint64_t rt = after[a].process.runtime_ns -
                                  before[b].process.runtime_ns;
                    uint32_t sc = after[a].process.syscall_count -
                                  before[b].process.syscall_count;
                    if (rt > 0 || sc > 0)
                        printf("fsprobe: proc %s %.20s rt=%llu sc=%u\n",
                               phases[p], after[a].name,
                               (unsigned long long)rt, sc);
                }
        fflush(stdout);
    }
    close(fd);
    printf("fsprobe: done %s\n", argv[1]);
    return 0;
}
