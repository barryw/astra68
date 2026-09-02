#include <astra/host.h>
#include <astra/status.h>
#include <vesta.h>

#include <stdint.h>

#ifndef RAW_COMMANDS
#error RAW_COMMANDS must be zero or one
#endif

#ifndef RAW_ITERATIONS
#define RAW_ITERATIONS UINT32_C(100000)
#endif

#define RAW_CONFIG_ADDRESS UINT32_C(0x02000000)
#define RAW_CHANNEL_ADDRESS UINT32_C(0x02001000)
#define RAW_DOORBELL_ADDRESS UINT32_C(0xffd00000)
#define RAW_OWNER UINT32_C(0x52415730)
#define RAW_CHANNEL_GENERATION UINT32_C(1)
#ifndef RAW_COMMAND_CAPACITY
#define RAW_COMMAND_CAPACITY UINT32_C(64)
#endif
#ifndef RAW_OPERATION
#define RAW_OPERATION 0u
#endif
#ifndef RAW_EXPECTED_STATUS
#define RAW_EXPECTED_STATUS ASTRA_STATUS_UNSUPPORTED
#endif
#define RAW_CHANNEL_BYTES \
    (ASTRA_HOST_CHANNEL_HEADER_SIZE + \
     RAW_COMMAND_CAPACITY * ASTRA_HOST_COMMAND_SIZE + 64u)

static void zero_words(volatile void *address, uint32_t bytes)
{
    volatile uint32_t *words = address;

    for (uint32_t index = 0; index < bytes / sizeof(*words); ++index)
        words[index] = 0u;
}

static void uart_text(const char *text)
{
    while (*text != '\0')
        VESTA->UART_DATA = (uint8_t)*text++;
}

static void uart_hex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    for (int shift = 28; shift >= 0; shift -= 4)
        VESTA->UART_DATA = (uint8_t)digits[(value >> shift) & 0xfu];
}

static void finish_failure(uint32_t code)
{
    uart_text("ASTRA RAW HOST FAIL code=");
    uart_hex32(code);
    uart_text("\n");
    for (;;)
        __asm__ volatile("stop #0x2700");
}

void kmain(void)
{
    volatile AstraHostChannelConfig *config =
        (volatile AstraHostChannelConfig *)(uintptr_t)RAW_CONFIG_ADDRESS;
    volatile AstraHostChannelHeader *header =
        (volatile AstraHostChannelHeader *)(uintptr_t)RAW_CHANNEL_ADDRESS;
#if RAW_COMMANDS
    volatile AstraHostCommand *commands = (volatile AstraHostCommand *)(
        uintptr_t)(RAW_CHANNEL_ADDRESS + ASTRA_HOST_CHANNEL_HEADER_SIZE);
#endif
    volatile uint32_t *doorbell =
        (volatile uint32_t *)(uintptr_t)RAW_DOORBELL_ADDRESS;
    uint32_t host_generation;
    uint32_t started;
    uint32_t elapsed;

    if (RAW_ITERATIONS == 0u || RAW_COMMAND_CAPACITY == 0u ||
        (RAW_COMMAND_CAPACITY & (RAW_COMMAND_CAPACITY - 1u)) != 0u ||
        VESTA->HOST_ACCEL_ID !=
            ASTRA_DEVICE_CLASS_HOST ||
        (VESTA->HOST_ACCEL_CAPS & ASTRA_HOST_CAP_CHANNEL) == 0u)
        finish_failure(1u);
    host_generation = VESTA->HOST_ACCEL_GENERATION;
    if (host_generation == 0u)
        finish_failure(2u);

    zero_words(config, sizeof(*config));
    zero_words(header, RAW_CHANNEL_BYTES);
    config->size = sizeof(*config);
    config->version = ASTRA_HOST_CHANNEL_CONFIG_VERSION;
    config->operation = ASTRA_HOST_CHANNEL_CONFIG_OPEN;
    config->slot = 0u;
    config->owner = RAW_OWNER;
    config->host_generation = host_generation;
    config->channel_generation = RAW_CHANNEL_GENERATION;
    config->physical_buffer = RAW_CHANNEL_ADDRESS;
    config->byte_size = RAW_CHANNEL_BYTES;
    config->command_capacity = RAW_COMMANDS ? RAW_COMMAND_CAPACITY : 1u;
    __asm__ volatile("" ::: "memory");
    VESTA->HOST_ACCEL_CHANNEL_CONFIG = RAW_CONFIG_ADDRESS;
    if (VESTA->HOST_ACCEL_CHANNEL_RESULT != ASTRA_STATUS_OK ||
        header->magic != ASTRA_HOST_CHANNEL_MAGIC ||
        header->channel_generation != RAW_CHANNEL_GENERATION)
        finish_failure(3u);

#if RAW_COMMANDS
    for (uint32_t index = 0u; index < RAW_COMMAND_CAPACITY; ++index) {
        commands[index].size = sizeof(commands[index]);
        commands[index].version = ASTRA_HOST_COMMAND_VERSION;
        commands[index].service = ASTRA_HOST_SERVICE_FILESYSTEM;
        commands[index].operation = RAW_OPERATION;
        commands[index].generation = host_generation;
        if (RAW_OPERATION == ASTRA_HOST_FS_STAT)
            commands[index].path[0] = '/';
    }
#endif

    started = VESTA->HOST_TIME_US;
#if RAW_COMMANDS
    {
        uint32_t consumer = 0u;
        uint32_t producer = 0u;

        while (consumer != RAW_ITERATIONS) {
            uint32_t observed = header->consumer_position;

            if (header->transport_status != ASTRA_STATUS_OK ||
                observed - consumer > producer - consumer)
                finish_failure(4u);
            while (consumer != observed) {
                volatile AstraHostCommand *command =
                    &commands[consumer & (RAW_COMMAND_CAPACITY - 1u)];

                if (command->status != RAW_EXPECTED_STATUS)
                    finish_failure(5u);
                ++consumer;
            }
            if (producer != RAW_ITERATIONS &&
                producer - consumer < RAW_COMMAND_CAPACITY) {
                uint32_t available = RAW_COMMAND_CAPACITY -
                                     (producer - consumer);
                uint32_t remaining = RAW_ITERATIONS - producer;

                producer += remaining < available ? remaining : available;
                header->producer_position = producer;
                __asm__ volatile("" ::: "memory");
                doorbell[8] = producer;
            }
        }
    }
#else
    for (uint32_t index = 1u; index <= RAW_ITERATIONS; ++index) {
        doorbell[8] = 0u;
    }
#endif
    elapsed = VESTA->HOST_TIME_US - started;

#if RAW_COMMANDS
    uart_text("RAW command depth=");
    uart_hex32(RAW_COMMAND_CAPACITY);
    uart_text(" ");
#else
    if (header->consumer_position != 0u ||
        header->transport_status != ASTRA_STATUS_OK)
        finish_failure(6u);
    uart_text("RAW empty depth=00000000 ");
#endif
    uart_text("iterations=");
    uart_hex32(RAW_ITERATIONS);
    uart_text(" elapsed-us=");
    uart_hex32(elapsed);
    uart_text("\nASTRA RAW HOST PASS\n");
    for (;;)
        __asm__ volatile("stop #0x2700");
}
