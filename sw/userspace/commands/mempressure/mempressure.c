/* Disposable physical-memory pressure gate. Never installed in a release. */
#include <astra/program.h>
#include <astra/runtime.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHUNK_MIB 4u
#define CHUNK_BYTES (CHUNK_MIB * 1024u * 1024u)
#define MAX_CHUNKS 128u
#define TAIL_BYTES (64u * 1024u)
#define MAX_TAIL 64u

ASTRA_PROGRAM("mempressure", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    uint32_t handles[MAX_CHUNKS] = {0u};
    uint32_t tail[MAX_TAIL] = {0u};
    char *end;
    unsigned long requested;
    uint32_t held = 0u;
    uint32_t tail_held = 0u;
    uint32_t status = ASTRA_SYSCALL_OK;
    int result = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: mempressure MAX_MIB (4..512, multiple of 4)\n");
        return 2;
    }
    errno = 0;
    requested = strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' ||
        requested < CHUNK_MIB || requested > CHUNK_MIB * MAX_CHUNKS ||
        requested % CHUNK_MIB != 0u) {
        fprintf(stderr, "mempressure: invalid limit\n");
        return 2;
    }
    puts("mempressure: allocating committed 4 MiB areas");
    fflush(stdout);
    for (; held < requested / CHUNK_MIB; ++held) {
        status = astra_rt_area_create(CHUNK_BYTES, ASTRA_RIGHT_READ,
                                      &handles[held]);
        if (status != ASTRA_SYSCALL_OK)
            break;
    }
    printf("mempressure: held=%lu MiB first-stop=%lu\n",
           (unsigned long)(held * CHUNK_MIB), (unsigned long)status);
    fflush(stdout);
    if (status == ASTRA_SYSCALL_OUT_OF_MEMORY) {
        for (; tail_held < MAX_TAIL; ++tail_held) {
            status = astra_rt_area_create(TAIL_BYTES, ASTRA_RIGHT_READ,
                                          &tail[tail_held]);
            if (status != ASTRA_SYSCALL_OK)
                break;
        }
        printf("mempressure: tail=%lu KiB final-stop=%lu\n",
               (unsigned long)(tail_held * (TAIL_BYTES / 1024u)),
               (unsigned long)status);
        fflush(stdout);
    }
    if (status == ASTRA_SYSCALL_OUT_OF_MEMORY) {
        const struct timespec pause = {.tv_sec = 2, .tv_nsec = 0};

        (void)nanosleep(&pause, NULL);
    } else {
        result = 1;
    }
    while (tail_held != 0u)
        if (astra_close(tail[--tail_held]) != ASTRA_SYSCALL_OK)
            result = 1;
    while (held != 0u)
        if (astra_close(handles[--held]) != ASTRA_SYSCALL_OK)
            result = 1;
    if (astra_rt_area_create(CHUNK_BYTES, ASTRA_RIGHT_READ,
                             &handles[0]) != ASTRA_SYSCALL_OK)
        result = 1;
    else if (astra_close(handles[0]) != ASTRA_SYSCALL_OK)
        result = 1;
    printf("mempressure: recovery=%s\n", result == 0 ? "PASS" : "FAIL");
    return result;
}
