// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif

enum {
    EDID_BLOCK_BYTES = 128u,
    EDID_DDC_ADDRESS = 0x50u,
    HDMI_VIC_720P60 = 4u,
};

static int checksum_valid(const uint8_t block[EDID_BLOCK_BYTES])
{
    unsigned sum = 0u;
    unsigned byte;

    for (byte = 0; byte < EDID_BLOCK_BYTES; ++byte)
        sum += block[byte];
    return (sum & 0xffu) == 0u;
}

static int edid_supports_hdmi_720p(
    const uint8_t base[EDID_BLOCK_BYTES],
    const uint8_t cta[EDID_BLOCK_BYTES])
{
    static const uint8_t header[8] = {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u,
    };
    unsigned end;
    unsigned offset;
    int hdmi = 0;
    int vic4 = 0;

    if (memcmp(base, header, sizeof(header)) != 0 ||
        !checksum_valid(base) || base[18] != 1u || base[19] < 3u ||
        base[126] == 0u || cta[0] != 0x02u || cta[1] < 3u ||
        !checksum_valid(cta))
        return 0;

    end = cta[2] == 0u ? 127u : cta[2];
    if (end < 4u || end > 127u)
        return 0;
    for (offset = 4u; offset < end;) {
        unsigned tag = cta[offset] >> 5;
        unsigned length = cta[offset] & 0x1fu;
        unsigned byte;

        ++offset;
        if (length > end - offset)
            return 0;
        if (tag == 2u) {
            for (byte = 0u; byte < length; ++byte) {
                if ((cta[offset + byte] & 0x7fu) == HDMI_VIC_720P60)
                    vic4 = 1;
            }
        } else if (tag == 3u && length >= 5u &&
                   cta[offset] == 0x03u && cta[offset + 1u] == 0x0cu &&
                   cta[offset + 2u] == 0x00u) {
            hdmi = 1;
        }
        offset += length;
    }
    return hdmi && vic4;
}

static void finish_checksum(uint8_t block[EDID_BLOCK_BYTES])
{
    unsigned sum = 0u;
    unsigned byte;

    block[127] = 0u;
    for (byte = 0; byte < EDID_BLOCK_BYTES; ++byte)
        sum += block[byte];
    block[127] = (uint8_t)(0u - sum);
}

static int self_test(void)
{
    uint8_t base[EDID_BLOCK_BYTES] = {0};
    uint8_t cta[EDID_BLOCK_BYTES] = {0};
    static const uint8_t header[8] = {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u,
    };

    memcpy(base, header, sizeof(header));
    base[18] = 1u;
    base[19] = 4u;
    base[126] = 1u;
    finish_checksum(base);
    cta[0] = 0x02u;
    cta[1] = 0x03u;
    cta[2] = 12u;
    cta[4] = 0x41u;
    cta[5] = HDMI_VIC_720P60;
    cta[6] = 0x65u;
    cta[7] = 0x03u;
    cta[8] = 0x0cu;
    cta[9] = 0x00u;
    cta[10] = 0x10u;
    cta[11] = 0x00u;
    finish_checksum(cta);
    if (!edid_supports_hdmi_720p(base, cta))
        return EXIT_FAILURE;

    base[127] ^= 1u;
    if (edid_supports_hdmi_720p(base, cta))
        return EXIT_FAILURE;
    base[127] ^= 1u;
    cta[9] = 1u;
    finish_checksum(cta);
    if (edid_supports_hdmi_720p(base, cta))
        return EXIT_FAILURE;
    cta[9] = 0u;
    cta[5] = 16u;
    finish_checksum(cta);
    if (edid_supports_hdmi_720p(base, cta))
        return EXIT_FAILURE;
    cta[5] = HDMI_VIC_720P60;
    cta[2] = 7u;
    finish_checksum(cta);
    return edid_supports_hdmi_720p(base, cta) ?
           EXIT_FAILURE : EXIT_SUCCESS;
}

#if defined(__linux__)
enum {
    LINK_BASE = 0x43c06000u,
    LINK_BYTES = 0x1000u,
    REG_LINK_CONTROL = 0x2cu,
    ZYNQ_EMIO_GPIO0 = 54u,
};

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t revalidate = 1;

static void stop(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static void reload(int signal_number)
{
    (void)signal_number;
    revalidate = 1;
}

static void write_reg(volatile uint32_t *registers, unsigned offset,
                      uint32_t value)
{
    registers[offset / 4u] = value;
    __sync_synchronize();
}

static int read_edid_block(int fd, uint8_t offset,
                           uint8_t block[EDID_BLOCK_BYTES])
{
    struct i2c_msg messages[2] = {
        {
            .addr = EDID_DDC_ADDRESS,
            .flags = 0,
            .len = 1,
            .buf = &offset,
        },
        {
            .addr = EDID_DDC_ADDRESS,
            .flags = I2C_M_RD,
            .len = EDID_BLOCK_BYTES,
            .buf = block,
        },
    };
    struct i2c_rdwr_ioctl_data transfer = {
        .msgs = messages,
        .nmsgs = 2,
    };

    return ioctl(fd, I2C_RDWR, &transfer) == 2 ? 0 : -1;
}

static int hpd_connected(int event_fd)
{
    struct gpiohandle_data values = {0};

    if (ioctl(event_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &values) != 0) {
        perror("read HDMI HPD GPIO");
        return -1;
    }
    return values.values[0] != 0u;
}

static int configure_link(volatile uint32_t *registers, int event_fd)
{
    const struct timespec settle = { .tv_sec = 0, .tv_nsec = 20000000 };
    uint8_t base[EDID_BLOCK_BYTES];
    uint8_t cta[EDID_BLOCK_BYTES];
    unsigned long functions = 0;
    int connected;
    int fd;

    write_reg(registers, REG_LINK_CONTROL, 0u);
    connected = hpd_connected(event_fd);

    if (connected < 0)
        return -1;
    if (!connected) {
        puts("ASTRA HDMI LINK DVI no-sink");
        return 0;
    }
    while (nanosleep(&settle, NULL) != 0 && errno == EINTR) {
        if (!running)
            return -1;
    }
    fd = open("/dev/i2c-0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open HDMI DDC");
        return -1;
    }
    if (ioctl(fd, I2C_FUNCS, &functions) != 0) {
        perror("query HDMI DDC capabilities");
        (void)close(fd);
        return -1;
    }
    if ((functions & I2C_FUNC_I2C) == 0u) {
        fputs("HDMI DDC adapter lacks I2C_RDWR support\n", stderr);
        (void)close(fd);
        return -1;
    }
    if (read_edid_block(fd, 0u, base) != 0 ||
        read_edid_block(fd, 128u, cta) != 0) {
        perror("read HDMI E-EDID");
        (void)close(fd);
        return -1;
    }
    (void)close(fd);
    if (!edid_supports_hdmi_720p(base, cta)) {
        puts("ASTRA HDMI LINK DVI sink lacks HDMI VSDB or VIC 4");
        return 0;
    }
    connected = hpd_connected(event_fd);
    if (connected < 0)
        return -1;
    if (!connected)
        return 0;
    write_reg(registers, REG_LINK_CONTROL, 1u);
    puts("ASTRA HDMI LINK HDMI 720p60 audio=2ch-LPCM-48k-24bit");
    return 0;
}

static int open_hpd_event(void)
{
    struct gpioevent_request request = {
        .lineoffset = ZYNQ_EMIO_GPIO0,
        .handleflags = GPIOHANDLE_REQUEST_INPUT |
                       GPIOHANDLE_REQUEST_ACTIVE_LOW,
        .eventflags = GPIOEVENT_REQUEST_BOTH_EDGES,
        .consumer_label = "astra-hdmi-link",
    };
    int chip = open("/dev/gpiochip0", O_RDONLY | O_CLOEXEC);

    if (chip < 0)
        return -1;
    if (ioctl(chip, GPIO_GET_LINEEVENT_IOCTL, &request) != 0) {
        (void)close(chip);
        return -1;
    }
    (void)close(chip);
    return request.fd;
}

static int run_manager(void)
{
    struct sigaction action = {0};
    volatile uint32_t *registers;
    int memory_fd = -1;
    int event_fd = -1;
    int pid_fd = -1;
    int result = EXIT_FAILURE;

    pid_fd = open("/run/astra-hdmi-link.pid",
                  O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (pid_fd < 0 || flock(pid_fd, LOCK_EX | LOCK_NB) != 0) {
        fputs("Astra HDMI link manager is already running\n", stderr);
        goto done;
    }
    if (ftruncate(pid_fd, 0) != 0 || dprintf(pid_fd, "%ld\n", (long)getpid()) < 0)
        goto done;

    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGINT, &action, NULL) != 0)
        goto done;
    action.sa_handler = reload;
    if (sigaction(SIGHUP, &action, NULL) != 0)
        goto done;

    memory_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (memory_fd < 0)
        goto done;
    registers = mmap(NULL, LINK_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED,
                     memory_fd, LINK_BASE);
    if (registers == MAP_FAILED)
        goto done;
    event_fd = open_hpd_event();
    if (event_fd < 0) {
        perror("open HDMI HPD GPIO event");
        (void)munmap((void *)registers, LINK_BYTES);
        goto done;
    }

    while (running) {
        struct gpioevent_data event;
        struct pollfd hpd_event = {
            .fd = event_fd,
            .events = POLLIN,
        };
        int ready;
        ssize_t received;

        if (revalidate) {
            revalidate = 0;
            (void)configure_link(registers, event_fd);
        }
        ready = poll(&hpd_event, 1, -1);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0) {
            perror("wait for HDMI HPD GPIO event");
            break;
        }
        if ((hpd_event.revents & POLLIN) == 0) {
            fputs("invalid HDMI HPD GPIO poll event\n", stderr);
            break;
        }
        received = read(event_fd, &event, sizeof(event));
        if (received == (ssize_t)sizeof(event)) {
            /* ACTIVE_LOW makes falling mean logically disconnected. */
            if (event.id == GPIOEVENT_EVENT_FALLING_EDGE)
                write_reg(registers, REG_LINK_CONTROL, 0u);
            revalidate = 1;
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            perror("read HDMI HPD GPIO event");
            break;
        }
    }
    write_reg(registers, REG_LINK_CONTROL, 0u);
    result = running ? EXIT_FAILURE : EXIT_SUCCESS;
    (void)close(event_fd);
    (void)munmap((void *)registers, LINK_BYTES);

done:
    if (memory_fd >= 0)
        (void)close(memory_fd);
    if (pid_fd >= 0) {
        (void)unlink("/run/astra-hdmi-link.pid");
        (void)close(pid_fd);
    }
    return result;
}
#endif

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test]\n", argv[0]);
        return EXIT_FAILURE;
    }
#if defined(__linux__)
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0)
        return EXIT_FAILURE;
    return run_manager();
#else
    fputs("Astra HDMI link manager requires Linux\n", stderr);
    return EXIT_FAILURE;
#endif
}
