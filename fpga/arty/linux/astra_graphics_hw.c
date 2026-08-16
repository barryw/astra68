// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "astra_graphics_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

void astra_graphics_memory_barrier(void)
{
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}

void astra_graphics_device_init(struct astra_graphics_device *device)
{
    device->memory_fd = -1;
    device->registers = MAP_FAILED;
    device->framebuffer = MAP_FAILED;
}

int astra_graphics_device_open(struct astra_graphics_device *device,
                               bool map_framebuffer)
{
    astra_graphics_device_init(device);
    device->memory_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (device->memory_fd < 0) {
        perror("open /dev/mem");
        return -1;
    }
    if (flock(device->memory_fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK)
            fprintf(stderr, "Astra graphics device is already owned\n");
        else
            perror("lock graphics device");
        astra_graphics_device_close(device);
        return -1;
    }

    device->registers = mmap(NULL, ASTRA_CONTROL_BYTES,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             device->memory_fd,
                             (off_t)ASTRA_CONTROL_BASE);
    if (device->registers == MAP_FAILED) {
        perror("map graphics control");
        astra_graphics_device_close(device);
        return -1;
    }

    if (map_framebuffer) {
        device->framebuffer = mmap(NULL, ASTRA_FRAMEBUFFER_BYTES,
                                   PROT_READ | PROT_WRITE, MAP_SHARED,
                                   device->memory_fd,
                                   (off_t)ASTRA_FRAMEBUFFER_BASE);
        if (device->framebuffer == MAP_FAILED) {
            perror("map graphics framebuffer");
            astra_graphics_device_close(device);
            return -1;
        }
    }
    return 0;
}

void astra_graphics_device_close(struct astra_graphics_device *device)
{
    if (device->framebuffer != MAP_FAILED) {
        (void)munmap((void *)device->framebuffer, ASTRA_FRAMEBUFFER_BYTES);
        device->framebuffer = MAP_FAILED;
    }
    if (device->registers != MAP_FAILED) {
        (void)munmap((void *)device->registers, ASTRA_CONTROL_BYTES);
        device->registers = MAP_FAILED;
    }
    if (device->memory_fd >= 0) {
        (void)close(device->memory_fd);
        device->memory_fd = -1;
    }
}

uint32_t astra_mmio_read(const struct astra_graphics_device *device,
                         unsigned offset)
{
    uint32_t value = device->registers[offset / sizeof(uint32_t)];

    astra_graphics_memory_barrier();
    return value;
}

void astra_mmio_write(const struct astra_graphics_device *device,
                      unsigned offset, uint32_t value)
{
    device->registers[offset / sizeof(uint32_t)] = value;
    astra_graphics_memory_barrier();
}

int astra_graphics_device_validate(const struct astra_graphics_device *device,
                                   bool require_boot_text)
{
    uint32_t capabilities;

    if (astra_mmio_read(device, ASTRA_REG_DEVICE_ID) !=
        ASTRA_GRAPHICS_DEVICE_ID) {
        fprintf(stderr, "Astra graphics device ID is not present\n");
        return -1;
    }
    if (astra_mmio_read(device, ASTRA_REG_VERSION) !=
        ASTRA_GRAPHICS_VERSION) {
        fprintf(stderr, "unsupported Astra graphics control version\n");
        return -1;
    }
    capabilities = astra_mmio_read(device, ASTRA_REG_CAPABILITIES);
    if (require_boot_text && (capabilities & ASTRA_CAP_BOOT_TEXT) == 0u) {
        fprintf(stderr, "Astra boot text plane is not present\n");
        return -1;
    }
    return 0;
}

void astra_graphics_memory_map_init(struct astra_graphics_memory_map *mapping)
{
    mapping->mapping = MAP_FAILED;
    mapping->mapping_bytes = 0;
    mapping->data = MAP_FAILED;
    mapping->data_bytes = 0;
}

int astra_graphics_memory_map_open(
    const struct astra_graphics_device *device,
    struct astra_graphics_memory_map *mapping,
    uint32_t physical_address, size_t bytes)
{
    uint64_t end = (uint64_t)physical_address + bytes;
    long page_size_long;
    size_t page_size;
    size_t page_offset;
    uint32_t mapping_address;
    size_t mapping_bytes;

    astra_graphics_memory_map_init(mapping);
    if (device->memory_fd < 0 || bytes == 0 ||
        physical_address < ASTRA_GRAPHICS_ARENA_BASE ||
        end > ASTRA_GRAPHICS_ARENA_LIMIT) {
        errno = EINVAL;
        return -1;
    }

    page_size_long = sysconf(_SC_PAGESIZE);
    if (page_size_long <= 0 ||
        (unsigned long)page_size_long > SIZE_MAX) {
        errno = EINVAL;
        return -1;
    }
    page_size = (size_t)page_size_long;
    if ((page_size & (page_size - 1u)) != 0u) {
        errno = EINVAL;
        return -1;
    }

    page_offset = physical_address & (page_size - 1u);
    if (bytes > SIZE_MAX - page_offset) {
        errno = EOVERFLOW;
        return -1;
    }
    mapping_address = physical_address - (uint32_t)page_offset;
    mapping_bytes = page_offset + bytes;
    mapping->mapping = mmap(NULL, mapping_bytes, PROT_READ | PROT_WRITE,
                            MAP_SHARED, device->memory_fd,
                            (off_t)mapping_address);
    if (mapping->mapping == MAP_FAILED)
        return -1;

    mapping->mapping_bytes = mapping_bytes;
    mapping->data = (volatile uint8_t *)mapping->mapping + page_offset;
    mapping->data_bytes = bytes;
    return 0;
}

void astra_graphics_memory_map_close(
    struct astra_graphics_memory_map *mapping)
{
    if (mapping->mapping != MAP_FAILED)
        (void)munmap(mapping->mapping, mapping->mapping_bytes);
    astra_graphics_memory_map_init(mapping);
}

int astra_graphics_scene_commit(
    const struct astra_graphics_device *device, uint64_t timeout_ns,
    uint32_t *generation_out)
{
    const struct timespec poll_delay = { .tv_sec = 0, .tv_nsec = 1000000 };
    uint32_t generation = astra_mmio_read(device, ASTRA_REG_GENERATION);
    uint32_t errors = astra_mmio_read(device, ASTRA_REG_COMMIT_ERRORS);
    uint64_t now = astra_monotonic_nanoseconds();
    uint64_t deadline = timeout_ns > UINT64_MAX - now ?
        UINT64_MAX : now + timeout_ns;

    astra_mmio_write(device, ASTRA_REG_COMMIT, 1u);
    for (;;) {
        uint32_t status = astra_mmio_read(device, ASTRA_REG_COMMIT);
        uint32_t active_generation =
            astra_mmio_read(device, ASTRA_REG_GENERATION);
        uint32_t active_errors =
            astra_mmio_read(device, ASTRA_REG_COMMIT_ERRORS);

        if ((status & 1u) == 0u && active_generation != generation) {
            if (generation_out != NULL)
                *generation_out = active_generation;
            return 0;
        }
        if (active_errors != errors) {
            fprintf(stderr,
                    "graphics scene commit rejected: errors=%u->%u\n",
                    errors, active_errors);
            return -1;
        }
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "graphics scene commit timed out: status=%08x "
                    "generation=%u\n",
                    status, active_generation);
            return -1;
        }
        while (nanosleep(&poll_delay, NULL) != 0 && errno == EINTR) {
        }
    }
}

uint64_t astra_monotonic_nanoseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}
