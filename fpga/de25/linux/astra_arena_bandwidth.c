/*
 * CPU bandwidth into and out of the DE25 media RAM through the
 * write-combining arena device, by store and load width. The display helper
 * copies every render batch across this bridge, so its best width is the one
 * the helper should use.
 *
 * usage: astra-arena-bandwidth [OFFSET [BYTES [ROUNDS]]]
 * Writes the arena: run it with Astra stopped.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "astra_de25_media.h"

static uint64_t now_ns(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void store64(volatile uint8_t *out, const uint8_t *in, size_t bytes)
{
    for (size_t at = 0; at < bytes; at += 8) {
        uint64_t value;

        memcpy(&value, in + at, 8);
        *(volatile uint64_t *)(out + at) = value;
    }
}

#if defined(__aarch64__)
/* 64 bytes per iteration: two 32-byte STP Q pairs. */
static void store_q64(volatile uint8_t *out, const uint8_t *in, size_t bytes)
{
    for (size_t at = 0; at < bytes; at += 64)
        __asm__ volatile(
            "ldp q0, q1, [%1]\n\t"
            "ldp q2, q3, [%1, #32]\n\t"
            "stp q0, q1, [%0]\n\t"
            "stp q2, q3, [%0, #32]\n\t"
            :: "r"(out + at), "r"(in + at)
            : "v0", "v1", "v2", "v3", "memory");
}

static void load_q64(uint8_t *out, volatile const uint8_t *in, size_t bytes)
{
    for (size_t at = 0; at < bytes; at += 64)
        __asm__ volatile(
            "ldp q0, q1, [%1]\n\t"
            "ldp q2, q3, [%1, #32]\n\t"
            "stp q0, q1, [%0]\n\t"
            "stp q2, q3, [%0, #32]\n\t"
            :: "r"(out + at), "r"(in + at)
            : "v0", "v1", "v2", "v3", "memory");
}
#endif

static void load64(uint8_t *out, volatile const uint8_t *in, size_t bytes)
{
    for (size_t at = 0; at < bytes; at += 8) {
        uint64_t value = *(volatile const uint64_t *)(in + at);

        memcpy(out + at, &value, 8);
    }
}

static void report(const char *name, size_t bytes, unsigned rounds,
                   uint64_t elapsed)
{
    printf("ASTRA_ARENA_BANDWIDTH %-10s %8.1f MB/s\n", name,
           (double)bytes * rounds * 1000.0 / (double)elapsed);
}

int main(int argc, char **argv)
{
    uint64_t offset = argc > 1 ? strtoull(argv[1], NULL, 0) : 0x10000000u;
    size_t bytes = argc > 2 ? strtoull(argv[2], NULL, 0) : 1331200u;
    unsigned rounds = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0) : 20u;
    uint8_t *host;
    uint8_t *check;
    volatile uint8_t *arena;
    uint64_t start;
    int fd;

    bytes &= ~(size_t)63;
    if (bytes == 0 || (offset & 4095u) != 0 ||
        offset + bytes > ASTRA_DE25_MEDIA_BYTES) {
        fprintf(stderr, "bad offset or size\n");
        return 2;
    }
    fd = open("/dev/astra-graphics-arena", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open /dev/astra-graphics-arena");
        return 1;
    }
    arena = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                 (off_t)offset);
    host = aligned_alloc(64, bytes);
    check = aligned_alloc(64, bytes);
    if (arena == MAP_FAILED || host == NULL || check == NULL) {
        perror("map");
        return 1;
    }
    for (size_t at = 0; at < bytes; ++at)
        host[at] = (uint8_t)(at * 131u + 7u);

    start = now_ns();
    for (unsigned round = 0; round < rounds; ++round)
        store64(arena, host, bytes);
    __sync_synchronize();
    report("store64", bytes, rounds, now_ns() - start);

    start = now_ns();
    for (unsigned round = 0; round < rounds; ++round)
        memcpy((void *)arena, host, bytes);
    __sync_synchronize();
    report("memcpy", bytes, rounds, now_ns() - start);

#if defined(__aarch64__)
    start = now_ns();
    for (unsigned round = 0; round < rounds; ++round)
        store_q64(arena, host, bytes);
    __sync_synchronize();
    report("store_q64", bytes, rounds, now_ns() - start);
#endif

    start = now_ns();
    for (unsigned round = 0; round < rounds; ++round)
        load64(check, arena, bytes);
    report("load64", bytes, rounds, now_ns() - start);

#if defined(__aarch64__)
    start = now_ns();
    for (unsigned round = 0; round < rounds; ++round)
        load_q64(check, arena, bytes);
    report("load_q64", bytes, rounds, now_ns() - start);
#endif

    if (memcmp(check, host, bytes) != 0) {
        printf("ASTRA_ARENA_BANDWIDTH FAIL read back differs\n");
        return 1;
    }
    printf("ASTRA_ARENA_BANDWIDTH PASS bytes=%zu rounds=%u\n", bytes, rounds);
    return 0;
}
