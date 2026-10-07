/*
 * Measures what logging costs the program that logs.
 *   logburst COUNT [--threads N] [--fault] [--off] [--warm] [--settle MS]
 *   logburst --fork
 *   logburst --wrap N [--settle MS]
 * Prints "logburst: COUNT lines, N threads, ELAPSED ns" on stdout.
 * --off formats every line but does not log it: the baseline.
 * --warm logs one line "logburst: warm" before the clock starts, so the
 *   ring's one-time setup is not in the measurement (its page commits are).
 * --settle MS sleeps MS milliseconds after the warm line and before the
 *   burst: long enough for the log service to drain the warm line and sleep
 *   again, so only a doorbell (or its 1 s sweep) can bring the burst in.
 * --fault logs one line and then faults, for the crash gate.
 * --fork logs "logburst: parent" and then fork()s: the child logs
 *   "logburst: child" and exits 0, the parent waits for it. Prints
 *   "logburst: fork child PID status S"; a fork that fails exits 6.
 * --wrap N maps and unmaps one page N times: 2N kernel-only trace records,
 *   enough at N >= 1024 to wrap the kernel ring past every user record.
 *   It waits 3 s first, so the terminal's echo is drawn and quiet, then
 *   logs (not prints) "logburst: wrapped N", sleeps --settle MS so the ring
 *   stays that way while a gate watches, and only then prints the same line.
 *
 * Each thread writes COUNT / N lines "burst T I" (the first COUNT % N
 * threads one more), straight through astra_log_line so the result does not
 * depend on whether stdout is a terminal. docs/SYSTEM_LOG.md section 8.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/program.h>
#include <astra/event.h>
#include <astra/log_ring.h>
#include <astra/runtime.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

ASTRA_PROGRAM("logburst", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define LOGBURST_THREADS_MAX 16u

typedef struct Burst {
    pthread_t thread;
    uint32_t index;
    uint32_t count;
} Burst;

static Burst bursts[LOGBURST_THREADS_MAX];
static int logging = 1;

static void *
burst(void *argument)
{
    const Burst *self = argument;
    char text[32];

    for (uint32_t index = 0u; index < self->count; ++index) {
        int length = snprintf(text, sizeof(text), "burst %u %u",
                              (unsigned)self->index, (unsigned)index);

        if (logging)
            (void)astra_log_line(ASTRA_EVENT_LEVEL_NOTICE,
                                 ASTRA_LOG_STREAM_LOG, text,
                                 (uint32_t)length);
    }
    return NULL;
}

static void
say(const char *text)
{
    (void)astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG, text,
                         (uint32_t)strlen(text));
}

/* A process that has logged forks, and both sides log again. */
static int
fork_test(void)
{
    int status = 0;
    pid_t child;

    say("logburst: parent");
    child = fork();
    if (child < 0) {
        perror("logburst: fork");
        return 6;
    }
    if (child == 0) {
        say("logburst: child");
        _exit(0);
    }
    if (waitpid(child, &status, 0) != child)
        return 7;
    printf("logburst: fork child %08x status %d\n", (unsigned)child,
           WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 8;
}

static void
pause_ms(uint32_t milliseconds)
{
    struct timespec pause = {(time_t)(milliseconds / 1000u),
                             (long)(milliseconds % 1000u) * 1000000L};

    while (nanosleep(&pause, &pause) != 0 && errno == EINTR)
        ;
}

/* Kernel records only: AREA_MAP and AREA_UNMAP each write one. */
static int
wrap_test(uint32_t count, uint32_t settle)
{
    char text[40];
    uint32_t area = 0u;

    pause_ms(3000u);
    if (astra_rt_area_create_flagged(4096u, ASTRA_RIGHT_READ |
                                                ASTRA_RIGHT_WRITE |
                                                ASTRA_RIGHT_MAP,
                                     0u, &area) != ASTRA_SYSCALL_OK)
        return 9;
    for (uint32_t index = 0u; index < count; ++index) {
        void *address = NULL;
        uint32_t size = 0u;

        if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ, &address, &size) !=
                ASTRA_SYSCALL_OK ||
            astra_rt_area_unmap(address) != ASTRA_SYSCALL_OK)
            return 10;
    }
    (void)astra_close(area);
    (void)snprintf(text, sizeof(text), "logburst: wrapped %u",
                   (unsigned)count);
    say(text);
    pause_ms(settle);
    printf("%s\n", text);
    return 0;
}

static int
parse_count(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);

    if (end == text || *end != '\0' || parsed == 0ul || parsed > 1000000ul)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

int
main(int argc, char **argv)
{
    struct timespec start;
    struct timespec end;
    uint32_t count = 0u;
    uint32_t threads = 1u;
    int fault = 0;
    int warm = 0;
    uint32_t settle = 0u;
    uint64_t elapsed;

    if (argc == 2 && strcmp(argv[1], "--fork") == 0)
        return fork_test();
    if (argc >= 3 && strcmp(argv[1], "--wrap") == 0 &&
        parse_count(argv[2], &count)) {
        if (argc == 5 && strcmp(argv[3], "--settle") == 0 &&
            parse_count(argv[4], &settle))
            return wrap_test(count, settle);
        return argc == 3 ? wrap_test(count, 0u) : 2;
    }
    if (argc < 2 || !parse_count(argv[1], &count)) {
        (void)fputs("usage: logburst COUNT [--threads N] [--fault] [--off] "
                    "[--warm] [--settle MS]\n"
                    "       logburst --fork | --wrap N [--settle MS]\n",
                    stderr);
        return 2;
    }
    for (int index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--off") == 0) {
            logging = 0;
        } else if (strcmp(argv[index], "--warm") == 0) {
            warm = 1;
        } else if (strcmp(argv[index], "--settle") == 0 && index + 1 < argc &&
                   parse_count(argv[index + 1], &settle)) {
            ++index;
        } else if (strcmp(argv[index], "--fault") == 0) {
            fault = 1;
        } else if (strcmp(argv[index], "--threads") == 0 && index + 1 < argc &&
                   parse_count(argv[index + 1], &threads) &&
                   threads <= LOGBURST_THREADS_MAX) {
            ++index;
        } else {
            (void)fputs("logburst: bad option\n", stderr);
            return 2;
        }
    }
    if (fault) {
        static const char last[] = "logburst: about to fault";

        (void)astra_log_line(ASTRA_EVENT_LEVEL_ERROR, ASTRA_LOG_STREAM_LOG,
                             last, (uint32_t)(sizeof(last) - 1u));
        *(volatile int *)0 = 1;
        return 3;
    }
    for (uint32_t index = 0u; index < threads; ++index) {
        bursts[index].index = index;
        bursts[index].count = count / threads +
                              (index < count % threads ? 1u : 0u);
    }
    if (warm && logging) {
        static const char line[] = "logburst: warm";

        (void)astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG,
                             line, (uint32_t)(sizeof(line) - 1u));
    }
    if (settle != 0u)
        pause_ms(settle);
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
        return 4;
    if (threads == 1u) {
        (void)burst(&bursts[0]);
    } else {
        for (uint32_t index = 0u; index < threads; ++index) {
            if (pthread_create(&bursts[index].thread, NULL, burst,
                               &bursts[index]) != 0)
                return 5;
        }
        for (uint32_t index = 0u; index < threads; ++index)
            (void)pthread_join(bursts[index].thread, NULL);
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
        return 4;
    elapsed = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000u +
              (uint64_t)end.tv_nsec - (uint64_t)start.tv_nsec;
    printf("logburst: %u lines, %u threads, %llu ns\n", (unsigned)count,
           (unsigned)threads, (unsigned long long)elapsed);
    return 0;
}
