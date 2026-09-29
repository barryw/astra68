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
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#ifndef ASTRA_DISPLAY_CAPTURE_LOCK_PATH
#define ASTRA_DISPLAY_CAPTURE_LOCK_PATH \
    "/run/lock/astra-display-capture.lock"
#endif

void astra_graphics_memory_barrier(void)
{
#if defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}

void astra_graphics_device_init(struct astra_graphics_device *device)
{
    device->memory_fd = -1;
    device->capture_lock_fd = -1;
    device->registers = MAP_FAILED;
    device->framebuffer = MAP_FAILED;
    device->arena = MAP_FAILED;
}

int astra_display_capture_claim(struct astra_graphics_device *device)
{
    if (device == NULL || device->memory_fd < 0) {
        errno = EBADF;
        return -1;
    }
    if (device->capture_lock_fd >= 0)
        return 0;
    device->capture_lock_fd = open(
        ASTRA_DISPLAY_CAPTURE_LOCK_PATH,
        O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (device->capture_lock_fd < 0) {
        perror("open display capture lock");
        return -1;
    }
    if (flock(device->capture_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK)
            fprintf(stderr, "Astra display capture is already owned\n");
        else
            perror("lock display capture");
        astra_display_capture_release(device);
        return -1;
    }
    return 0;
}

void astra_display_capture_release(struct astra_graphics_device *device)
{
    if (device != NULL && device->capture_lock_fd >= 0) {
        (void)close(device->capture_lock_fd);
        device->capture_lock_fd = -1;
    }
}

int astra_display_capture_device_open(
    struct astra_graphics_device *device)
{
    astra_graphics_device_init(device);
    device->memory_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (device->memory_fd < 0) {
        perror("open /dev/mem");
        return -1;
    }
    if (astra_display_capture_claim(device) != 0) {
        astra_graphics_device_close(device);
        return -1;
    }
    device->registers = mmap(NULL, ASTRA_CONTROL_BYTES,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             device->memory_fd,
                             (off_t)ASTRA_CONTROL_BASE);
    if (device->registers == MAP_FAILED) {
        perror("map display capture control");
        astra_graphics_device_close(device);
        return -1;
    }
    return 0;
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

#ifdef ASTRA_GRAPHICS_ARENA_DEVICE
    {
        /* /dev/mem maps memory outside System RAM as Device-nGnRnE, where
           every store waits for its bridge response. The arena driver maps
           it write-combining, from the media RAM base, which is the arena
           base. Arena stores are ordered before each doorbell by the DSB in
           astra_graphics_memory_barrier. */
        int arena_fd = open(ASTRA_GRAPHICS_ARENA_DEVICE, O_RDWR | O_CLOEXEC);

        _Static_assert(ASTRA_GRAPHICS_ARENA_BASE == 0x40000000u,
                       "the arena device maps from the media RAM base");
        if (arena_fd < 0) {
            perror("open " ASTRA_GRAPHICS_ARENA_DEVICE);
            astra_graphics_device_close(device);
            return -1;
        }
        device->arena = mmap(NULL, ASTRA_GRAPHICS_ARENA_BYTES,
                             PROT_READ | PROT_WRITE, MAP_SHARED, arena_fd, 0);
        (void)close(arena_fd);
    }
#else
    device->arena = mmap(NULL, ASTRA_GRAPHICS_ARENA_BYTES,
                         PROT_READ | PROT_WRITE, MAP_SHARED,
                         device->memory_fd, (off_t)ASTRA_GRAPHICS_ARENA_BASE);
#endif
    if (device->arena == MAP_FAILED) {
        perror("map graphics arena");
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
    if (device->arena != MAP_FAILED) {
        (void)munmap((void *)device->arena, ASTRA_GRAPHICS_ARENA_BYTES);
        device->arena = MAP_FAILED;
    }
    if (device->framebuffer != MAP_FAILED) {
        (void)munmap((void *)device->framebuffer, ASTRA_FRAMEBUFFER_BYTES);
        device->framebuffer = MAP_FAILED;
    }
    if (device->registers != MAP_FAILED) {
        (void)munmap((void *)device->registers, ASTRA_CONTROL_BYTES);
        device->registers = MAP_FAILED;
    }
    astra_display_capture_release(device);
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
    if (device->arena != MAP_FAILED && device->arena != NULL) {
        /* A view: nothing for close to unmap. */
        mapping->data = device->arena +
                        (physical_address - ASTRA_GRAPHICS_ARENA_BASE);
        mapping->data_bytes = bytes;
        return 0;
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

void astra_graphics_memory_fill(volatile void *destination, uint8_t value,
                                size_t bytes)
{
    volatile uint8_t *out = destination;
    uint64_t wide_value = UINT64_C(0x0101010101010101) * value;

    while (bytes != 0u && ((uintptr_t)out & 7u) != 0u) {
        *out++ = value;
        --bytes;
    }
    while (bytes >= sizeof(wide_value)) {
        *(volatile uint64_t *)(uintptr_t)out = wide_value;
        out += sizeof(wide_value);
        bytes -= sizeof(wide_value);
    }
    while (bytes-- != 0u)
        *out++ = value;
}

void astra_graphics_memory_copy_to(volatile void *destination,
                                   const void *source, size_t bytes)
{
    volatile uint8_t *out = destination;
    const uint8_t *in = source;

    while (bytes != 0u && ((uintptr_t)out & 7u) != 0u) {
        *out++ = *in++;
        --bytes;
    }
    while (bytes >= sizeof(uint64_t)) {
        uint64_t value;

        __builtin_memcpy(&value, in, sizeof(value));
        *(volatile uint64_t *)(uintptr_t)out = value;
        out += sizeof(value);
        in += sizeof(value);
        bytes -= sizeof(value);
    }
    while (bytes-- != 0u)
        *out++ = *in++;
}

void astra_graphics_memory_copy_from(void *destination,
                                     volatile const void *source,
                                     size_t bytes)
{
    uint8_t *out = destination;
    volatile const uint8_t *in = source;

    while (bytes != 0u && ((uintptr_t)in & 7u) != 0u) {
        *out++ = *in++;
        --bytes;
    }
    while (bytes >= sizeof(uint64_t)) {
        uint64_t value = *(volatile const uint64_t *)(uintptr_t)in;

        __builtin_memcpy(out, &value, sizeof(value));
        out += sizeof(value);
        in += sizeof(value);
        bytes -= sizeof(value);
    }
    while (bytes-- != 0u)
        *out++ = *in++;
}

int astra_graphics_capture_rgb(
    const struct astra_graphics_device *device, uint32_t physical_address,
    const char *path, struct astra_display_capture_result *result)
{
    const uint64_t timeout_ns = UINT64_C(2000000000);
    struct astra_graphics_memory_map frame;
    uint8_t *copy = NULL;
    FILE *output = NULL;
    uint64_t started;
    uint64_t deadline;
    int status = -1;

    astra_graphics_memory_map_init(&frame);
    if (path == NULL || device->capture_lock_fd < 0 ||
        (physical_address & 255u) != 0u ||
        astra_mmio_read(device, ASTRA_REG_CAPTURE_DEVICE_ID) !=
            ASTRA_CAPTURE_DEVICE_ID ||
        astra_mmio_read(device, ASTRA_REG_CAPTURE_VERSION) !=
            ASTRA_CAPTURE_VERSION ||
        astra_mmio_read(device, ASTRA_REG_CAPTURE_FRAME_BYTES) !=
            ASTRA_CAPTURE_FRAME_BYTES) {
        errno = ENODEV;
        goto done;
    }
    if (astra_graphics_memory_map_open(device, &frame, physical_address,
                                       ASTRA_CAPTURE_FRAME_BYTES) != 0)
        goto done;

    started = astra_monotonic_nanoseconds();
    deadline = started + timeout_ns;
    /* A capture given up on earlier finishes at its frame's end; commands
       before then are refused. The kernel driver does the same. */
    while ((astra_mmio_read(device, ASTRA_REG_CAPTURE_STATUS) &
            (ASTRA_CAPTURE_STATUS_ARMED | ASTRA_CAPTURE_STATUS_ACTIVE)) !=
           0u) {
        if (astra_monotonic_nanoseconds() >= deadline) {
            errno = ETIMEDOUT;
            goto disable;
        }
        astra_graphics_poll_pause(started);
    }
    astra_mmio_write(device, ASTRA_REG_CAPTURE_CONTROL,
                     ASTRA_CAPTURE_ENABLE | ASTRA_CAPTURE_ACKNOWLEDGE |
                         ASTRA_CAPTURE_CLEAR_COUNTERS);
    astra_mmio_write(device, ASTRA_REG_CAPTURE_BUFFER_BASE,
                     physical_address);
    astra_mmio_write(device, ASTRA_REG_CAPTURE_CONTROL,
                     ASTRA_CAPTURE_ENABLE | ASTRA_CAPTURE_ARM);
    /* Judged by this capture's own counters, never the sticky status bits
       (sw/include/astra/display_capture.h). */
    for (;;) {
        if ((astra_mmio_read(device, ASTRA_REG_CAPTURE_COMPLETED_COUNT) |
             astra_mmio_read(device, ASTRA_REG_CAPTURE_DROPPED_COUNT) |
             astra_mmio_read(device, ASTRA_REG_CAPTURE_AXI_ERROR_COUNT) |
             astra_mmio_read(device,
                             ASTRA_REG_CAPTURE_COMMAND_ERROR_COUNT)) != 0u)
            break;
        if (astra_monotonic_nanoseconds() >= deadline) {
            errno = ETIMEDOUT;
            goto disable;
        }
        astra_graphics_poll_pause(started);
    }
    if (astra_mmio_read(device, ASTRA_REG_CAPTURE_DROPPED_COUNT) != 0u &&
        astra_mmio_read(device, ASTRA_REG_CAPTURE_AXI_ERROR_COUNT) == 0u &&
        astra_mmio_read(device,
                        ASTRA_REG_CAPTURE_COMMAND_ERROR_COUNT) == 0u) {
        errno = EAGAIN;
        goto disable;
    }
    if (astra_mmio_read(device, ASTRA_REG_CAPTURE_COMPLETED_BASE) !=
            physical_address ||
        astra_mmio_read(device, ASTRA_REG_CAPTURE_COMPLETED_COUNT) != 1u ||
        astra_mmio_read(device, ASTRA_REG_CAPTURE_AXI_ERROR_COUNT) != 0u ||
        astra_mmio_read(device,
                        ASTRA_REG_CAPTURE_COMMAND_ERROR_COUNT) != 0u) {
        errno = EIO;
        goto disable;
    }

    copy = malloc(ASTRA_CAPTURE_FRAME_BYTES);
    if (copy == NULL)
        goto disable;
    astra_graphics_memory_copy_from(copy, frame.data,
                                    ASTRA_CAPTURE_FRAME_BYTES);
    output = fopen(path, "wb");
    if (output == NULL)
        goto disable;
    if (fwrite(copy, 1u, ASTRA_CAPTURE_FRAME_BYTES, output) !=
            ASTRA_CAPTURE_FRAME_BYTES) {
        (void)fclose(output);
        output = NULL;
        goto disable;
    }
    if (fclose(output) != 0) {
        output = NULL;
        goto disable;
    }
    output = NULL;
    if (result != NULL) {
        result->generation = astra_mmio_read(
            device, ASTRA_REG_CAPTURE_COMPLETED_GENERATION);
        result->cycles = astra_mmio_read(
            device, ASTRA_REG_CAPTURE_LAST_CYCLES);
        result->elapsed_ns = astra_monotonic_nanoseconds() - started;
    }
    status = 0;

disable:
    /* Enable stays set: turning it off during a capture is refused and
       counted as a command error. */
    astra_mmio_write(device, ASTRA_REG_CAPTURE_CONTROL,
                     ASTRA_CAPTURE_ENABLE | ASTRA_CAPTURE_ACKNOWLEDGE);
done:
    if (output != NULL)
        (void)fclose(output);
    free(copy);
    astra_graphics_memory_map_close(&frame);
    return status;
}

int astra_graphics_wait_register_mask(
    const struct astra_graphics_device *device, unsigned offset,
    uint32_t mask, uint32_t expected, uint64_t timeout_ns,
    uint32_t *value_out)
{
    uint64_t now = astra_monotonic_nanoseconds();
    uint64_t deadline = timeout_ns > UINT64_MAX - now ?
        UINT64_MAX : now + timeout_ns;

    for (;;) {
        uint32_t value = astra_mmio_read(device, offset);

        if ((value & mask) == expected) {
            if (value_out != NULL)
                *value_out = value;
            return 0;
        }
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "graphics register timeout offset=%04x value=%08x "
                    "mask=%08x expected=%08x\n",
                    offset, value, mask, expected);
            errno = ETIMEDOUT;
            return -1;
        }
        astra_graphics_poll_pause(now);
    }
}

int astra_graphics_render_stop(const struct astra_graphics_device *device,
                               uint64_t timeout_ns)
{
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL, 0u);
    return astra_graphics_wait_register_mask(
        device, ASTRA_REG_RENDER_STATUS,
        ASTRA_RENDER_ENGINE_BUSY | ASTRA_RENDER_ENGINE_ENABLED, 0u,
        timeout_ns, NULL);
}

bool astra_graphics_has_capability(
    const struct astra_graphics_device *device, uint32_t capability)
{
    return (astra_mmio_read(device, ASTRA_REG_CAPABILITIES) & capability) ==
           capability;
}

int astra_graphics_access_fault_take(
    const struct astra_graphics_device *device,
    struct astra_access_fault *fault)
{
    fault->count = 0u;
    fault->first = 0u;
    if (!astra_graphics_has_capability(device,
                                       ASTRA_CAP_ACCESS_FAULT_RECORD)) {
        errno = ENOTSUP;
        return -1;
    }
    fault->count = astra_mmio_read(device, ASTRA_REG_ACCESS_FAULT_COUNT);
    if (fault->count == 0u)
        return 0;
    fault->first = astra_mmio_read(device, ASTRA_REG_ACCESS_FAULT_FIRST);
    astra_mmio_write(device, ASTRA_REG_ACCESS_FAULT_COUNT, 0u);
    return 1;
}

int astra_graphics_render_host_aperture_set(
    const struct astra_graphics_device *device, uint32_t base,
    uint64_t timeout_ns)
{
    if (!astra_graphics_has_capability(device,
                                       ASTRA_CAP_RENDER_HOST_APERTURE)) {
        fprintf(stderr, "bitstream has no render host aperture\n");
        errno = ENOTSUP;
        return -1;
    }
    if (astra_graphics_render_stop(device, timeout_ns) != 0) {
        fprintf(stderr, "render engine did not stop: status=0x%08x\n",
                astra_mmio_read(device, ASTRA_REG_RENDER_STATUS));
        return -1;
    }
    astra_mmio_write(device, ASTRA_REG_RENDER_HOST_APERTURE_BASE, base);
    if (astra_mmio_read(device, ASTRA_REG_RENDER_HOST_APERTURE_BASE) !=
        base) {
        fprintf(stderr, "render host aperture did not take 0x%08x\n", base);
        errno = EIO;
        return -1;
    }
    return 0;
}

void astra_graphics_copper_write_instruction(
    const struct astra_graphics_device *device, unsigned index,
    uint32_t word0, uint32_t word1)
{
    unsigned offset = ASTRA_REG_COPPER_PROGRAM + index * 8u;

    astra_mmio_write(device, offset, word0);
    astra_mmio_write(device, offset + 4u, word1);
}

void astra_graphics_scene_prepare_empty(
    const struct astra_graphics_device *device)
{
    astra_mmio_write(device, ASTRA_REG_COPPER_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_COPPER_IRQ_PENDING, 1u);
    astra_mmio_write(device, ASTRA_REG_BACKDROP, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_WINDOW_SCENE_BYTES, 0u);
    astra_mmio_write(device, ASTRA_REG_SPRITE_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_GLOBAL_CONTROL, 1u);
}

void astra_graphics_scene_commit_begin(
    const struct astra_graphics_device *device,
    struct astra_graphics_commit *commit)
{
    commit->generation = astra_mmio_read(device, ASTRA_REG_GENERATION);
    commit->errors = astra_mmio_read(device, ASTRA_REG_COMMIT_ERRORS);
    astra_mmio_write(device, ASTRA_REG_COMMIT, 1u);
}

int astra_graphics_scene_commit_wait(
    const struct astra_graphics_device *device,
    const struct astra_graphics_commit *commit, uint64_t timeout_ns,
    uint32_t *generation_out)
{
    uint64_t now = astra_monotonic_nanoseconds();
    uint64_t deadline = timeout_ns > UINT64_MAX - now ?
        UINT64_MAX : now + timeout_ns;

    for (;;) {
        uint32_t status = astra_mmio_read(device, ASTRA_REG_COMMIT);
        uint32_t active_generation =
            astra_mmio_read(device, ASTRA_REG_GENERATION);
        uint32_t active_errors =
            astra_mmio_read(device, ASTRA_REG_COMMIT_ERRORS);

        if ((status & 1u) == 0u && active_generation != commit->generation) {
            if (generation_out != NULL)
                *generation_out = active_generation;
            return 0;
        }
        if (active_errors != commit->errors) {
            fprintf(stderr,
                    "graphics scene commit rejected: errors=%u->%u\n",
                    commit->errors, active_errors);
            return -1;
        }
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "graphics scene commit timed out: status=%08x "
                    "generation=%u\n",
                    status, active_generation);
            return -1;
        }
        astra_graphics_poll_pause(now);
    }
}

int astra_graphics_scene_commit_drain(
    const struct astra_graphics_device *device, uint64_t timeout_ns)
{
    uint64_t now = astra_monotonic_nanoseconds();
    uint64_t deadline = now + timeout_ns;

    /* A commit left by an earlier helper applies at the next safe frame
       boundary; wait for it rather than have the next commit rejected. */
    while ((astra_mmio_read(device, ASTRA_REG_COMMIT) & 1u) != 0u) {
        if (astra_monotonic_nanoseconds() >= deadline)
            return -1;
        astra_graphics_poll_pause(now);
    }
    return 0;
}

int astra_graphics_scene_commit(
    const struct astra_graphics_device *device, uint64_t timeout_ns,
    uint32_t *generation_out)
{
    struct astra_graphics_commit commit;

    astra_graphics_scene_commit_begin(device, &commit);
    return astra_graphics_scene_commit_wait(device, &commit, timeout_ns,
                                            generation_out);
}

/*
 * No interrupt reaches the HPS from the graphics block, so every wait polls.
 * A render batch or pointer commit usually finishes within this budget, and
 * spinning answers it within one register read; sleeping 50 us at a time
 * added up to a tenth of a millisecond to every batch. Past the budget the
 * wait is a vblank or a fault, and the core goes back to the guest. Timer
 * slack is cut to 1 ns so that sleep ends when asked, not 50 us later.
 */
#define POLL_SPIN_NS UINT64_C(2000000)

void astra_graphics_poll_pause(uint64_t started)
{
    static bool slack_set;
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000 };

    if (astra_monotonic_nanoseconds() - started < POLL_SPIN_NS) {
#if defined(__aarch64__)
        __asm__ volatile("yield" ::: "memory");
#endif
        return;
    }
    if (!slack_set) {
        (void)prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
        slack_set = true;
    }
    while (nanosleep(&delay, NULL) != 0 && errno == EINTR) {
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
