/*
 * Persistent Musashi target for the backend-neutral Astra68 conformance runner.
 *
 * The worker deliberately knows nothing about JSON or expected results. It
 * accepts a normalized initial CPU/memory state, executes it, and returns only
 * observations. The same fixture can therefore be consumed later by an RTL
 * adapter without copying the oracle into either implementation.
 */

#include "m68k.h"
#include "m68kconf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_VERSION 1u
#define CASE_SCHEMA 1u

#define REQUEST_MAGIC UINT32_C(0x41363851)  /* A68Q */
#define RESPONSE_MAGIC UINT32_C(0x41363852) /* A68R */

#define COMMAND_HELLO 1u
#define COMMAND_RUN 2u

#define STATUS_OK 0u
#define STATUS_BAD_REQUEST 1u
#define STATUS_UNSUPPORTED 2u
#define STATUS_INTERNAL 3u

#define MODE_INSTRUCTION 1u
#define MODE_CYCLES 2u
#define MODE_MEMORY 3u

#define TERMINAL_INSTRUCTION 1u
#define TERMINAL_CYCLE_LIMIT 2u
#define TERMINAL_MEMORY 3u

#define FLAG_RESET 1u

#define CAP_SINGLE_INSTRUCTION (UINT32_C(1) << 0)
#define CAP_SPARSE_MEMORY      (UINT32_C(1) << 1)
#define CAP_MEMORY_STOP        (UINT32_C(1) << 2)
#define CAP_MEMORY_OBSERVE     (UINT32_C(1) << 3)
#define CAP_NO_FPU             (UINT32_C(1) << 4)
#define CAP_TABLE_BUS_FAULT    (UINT32_C(1) << 5)

#define PAGE_SHIFT 12u
#define PAGE_BYTES (UINT32_C(1) << PAGE_SHIFT)
#define PAGE_COUNT (UINT32_C(1) << (32u - PAGE_SHIFT))
#define PAGE_MASK (PAGE_BYTES - 1u)

#define CPU_FIELD_COUNT 26u
#define MAX_MESSAGE_BYTES (64u * 1024u * 1024u)
#define MAX_OBSERVATIONS 4096u

static uint8_t *memory_pages[PAGE_COUNT];
static uint32_t *allocated_pages;
static size_t allocated_count;
static size_t allocated_capacity;

static int stop_enabled;
static int stop_matched;
static uint32_t stop_address;
static uint32_t stop_mask;
static uint32_t stop_value;
static unsigned int selected_cpu_type = M68K_CPU_TYPE_68030;
static const char *selected_cpu_name = "vendored-musashi-68030-pmmu-no-fpu";

static const m68k_register_t cpu_fields[CPU_FIELD_COUNT] = {
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3,
    M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3,
    M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
    M68K_REG_PC, M68K_REG_SR, M68K_REG_USP, M68K_REG_ISP,
    M68K_REG_MSP, M68K_REG_SFC, M68K_REG_DFC, M68K_REG_VBR,
    M68K_REG_CACR, M68K_REG_CAAR
};

struct reader {
    const uint8_t *data;
    size_t length;
    size_t offset;
};

struct buffer {
    uint8_t *data;
    size_t length;
    size_t capacity;
};

struct observation {
    uint32_t address;
    uint32_t length;
};

static uint16_t load_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t load_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void store_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void store_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static int read_exact(void *destination, size_t length)
{
    uint8_t *output = destination;
    size_t offset = 0;

    while (offset < length) {
        size_t received = fread(output + offset, 1, length - offset, stdin);
        if (received == 0) {
            if (feof(stdin))
                return offset == 0 ? 0 : -1;
            if (ferror(stdin))
                return -1;
        }
        offset += received;
    }
    return 1;
}

static int write_exact(const void *source, size_t length)
{
    return fwrite(source, 1, length, stdout) == length && fflush(stdout) == 0;
}

static int reader_u16(struct reader *reader, uint16_t *value)
{
    if (reader->length - reader->offset < 2u)
        return 0;
    *value = load_be16(reader->data + reader->offset);
    reader->offset += 2u;
    return 1;
}

static int reader_u32(struct reader *reader, uint32_t *value)
{
    if (reader->length - reader->offset < 4u)
        return 0;
    *value = load_be32(reader->data + reader->offset);
    reader->offset += 4u;
    return 1;
}

static const uint8_t *reader_bytes(struct reader *reader, size_t length)
{
    const uint8_t *result;
    if (length > reader->length - reader->offset)
        return NULL;
    result = reader->data + reader->offset;
    reader->offset += length;
    return result;
}

static int buffer_reserve(struct buffer *buffer, size_t additional)
{
    size_t required;
    size_t capacity;
    uint8_t *replacement;

    if (additional > SIZE_MAX - buffer->length)
        return 0;
    required = buffer->length + additional;
    if (required <= buffer->capacity)
        return 1;
    capacity = buffer->capacity ? buffer->capacity : 256u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    replacement = realloc(buffer->data, capacity);
    if (replacement == NULL)
        return 0;
    buffer->data = replacement;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_bytes(struct buffer *buffer, const void *source, size_t length)
{
    if (!buffer_reserve(buffer, length))
        return 0;
    memcpy(buffer->data + buffer->length, source, length);
    buffer->length += length;
    return 1;
}

static int buffer_u16(struct buffer *buffer, uint16_t value)
{
    uint8_t encoded[2];
    store_be16(encoded, value);
    return buffer_bytes(buffer, encoded, sizeof(encoded));
}

static int buffer_u32(struct buffer *buffer, uint32_t value)
{
    uint8_t encoded[4];
    store_be32(encoded, value);
    return buffer_bytes(buffer, encoded, sizeof(encoded));
}

static int write_response(uint16_t status, const uint8_t *payload, size_t length)
{
    uint8_t header[12];
    if (length > UINT32_MAX)
        return 0;
    store_be32(header, RESPONSE_MAGIC);
    store_be16(header + 4, PROTOCOL_VERSION);
    store_be16(header + 6, status);
    store_be32(header + 8, (uint32_t)length);
    return write_exact(header, sizeof(header)) && write_exact(payload, length);
}

static int write_error(uint16_t status, const char *message)
{
    return write_response(status, (const uint8_t *)message, strlen(message));
}

static void memory_clear(void)
{
    size_t index;
    for (index = 0; index < allocated_count; ++index) {
        uint32_t page = allocated_pages[index];
        free(memory_pages[page]);
        memory_pages[page] = NULL;
    }
    allocated_count = 0;
}

static uint8_t *memory_page(uint32_t address, int create)
{
    uint32_t page_number = address >> PAGE_SHIFT;
    uint8_t *page = memory_pages[page_number];

    if (page != NULL || !create)
        return page;
    page = calloc(PAGE_BYTES, 1u);
    if (page == NULL)
        return NULL;
    if (allocated_count == allocated_capacity) {
        size_t new_capacity = allocated_capacity ? allocated_capacity * 2u : 64u;
        uint32_t *replacement = realloc(
            allocated_pages, new_capacity * sizeof(*allocated_pages));
        if (replacement == NULL) {
            free(page);
            return NULL;
        }
        allocated_pages = replacement;
        allocated_capacity = new_capacity;
    }
    memory_pages[page_number] = page;
    allocated_pages[allocated_count++] = page_number;
    return page;
}

static uint8_t memory_read8(uint32_t address)
{
    uint8_t *page = memory_page(address, 0);
    return page == NULL ? 0u : page[address & PAGE_MASK];
}

static uint16_t memory_read16(uint32_t address)
{
    return (uint16_t)(((uint16_t)memory_read8(address) << 8) |
                      memory_read8(address + 1u));
}

static uint32_t memory_read32(uint32_t address)
{
    return ((uint32_t)memory_read16(address) << 16) |
           memory_read16(address + 2u);
}

static int memory_write8(uint32_t address, uint8_t value)
{
    uint8_t *page = memory_page(address, 1);
    if (page == NULL)
        return 0;
    page[address & PAGE_MASK] = value;
    return 1;
}

static int memory_write16(uint32_t address, uint16_t value)
{
    return memory_write8(address, (uint8_t)(value >> 8)) &&
           memory_write8(address + 1u, (uint8_t)value);
}

static int memory_write32(uint32_t address, uint32_t value)
{
    return memory_write16(address, (uint16_t)(value >> 16)) &&
           memory_write16(address + 2u, (uint16_t)value);
}

static int memory_range_mapped(uint32_t address, uint32_t length)
{
    uint32_t offset;

    if (length == 0u || address > UINT32_MAX - (length - 1u))
        return 0;
    for (offset = 0u; offset < length; ++offset) {
        if (memory_page(address + offset, 0) == NULL)
            return 0;
    }
    return 1;
}

static void check_stop(void);

static int pmmu_memory_read32(unsigned int address, unsigned int *value)
{
    if (value == NULL || !memory_range_mapped(address, 4u))
        return 0;
    *value = memory_read32(address);
    return 1;
}

static int pmmu_memory_write32(unsigned int address, unsigned int value)
{
    if (!memory_range_mapped(address, 4u))
        return 0;
    if (!memory_write32(address, value))
        return 0;
    check_stop();
    return 1;
}

static void check_stop(void)
{
    if (stop_enabled &&
        (memory_read32(stop_address) & stop_mask) == (stop_value & stop_mask)) {
        stop_matched = 1;
        m68k_end_timeslice();
    }
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    return memory_read8(address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    return memory_read16(address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return memory_read32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    if (!memory_write8(address, (uint8_t)value))
        abort();
    check_stop();
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    if (!memory_write16(address, (uint16_t)value))
        abort();
    check_stop();
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    if (!memory_write32(address, value))
        abort();
    check_stop();
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return memory_read8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return memory_read16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return memory_read32(address);
}

static int apply_cpu_state(uint32_t mask, const uint32_t values[CPU_FIELD_COUNT])
{
    uint32_t index;
    uint32_t valid_mask = (UINT32_C(1) << CPU_FIELD_COUNT) - 1u;
    /* m68k_set_reg(M68K_REG_SR) deliberately uses Musashi's no-stack-swap
     * helper.  Select the privilege/master mode before assigning the banked
     * stack pointers so USP/ISP/MSP land in the intended active/hidden bank. */
    static const uint8_t order[CPU_FIELD_COUNT] = {
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14,
        17, 18, 19, 20, 15, 16,
        21, 22, 23, 24, 25
    };

    if (mask & ~valid_mask)
        return 0;
    for (index = 0; index < CPU_FIELD_COUNT; ++index) {
        uint32_t field = order[index];
        if (mask & (UINT32_C(1) << field))
            m68k_set_reg(cpu_fields[field], values[field]);
    }
    return 1;
}

static int handle_hello(void)
{
    struct buffer response = {0};
    size_t name_length = strlen(selected_cpu_name);
    uint32_t capabilities = CAP_SINGLE_INSTRUCTION | CAP_SPARSE_MEMORY |
                            CAP_MEMORY_STOP | CAP_MEMORY_OBSERVE |
                            CAP_TABLE_BUS_FAULT;
#if M68K_EMULATE_FPU == M68K_OPT_OFF
    capabilities |= CAP_NO_FPU;
#endif
    int ok = buffer_u16(&response, CASE_SCHEMA) &&
             buffer_u32(&response, capabilities) &&
             buffer_u16(&response, (uint16_t)name_length) &&
             buffer_bytes(&response, selected_cpu_name, name_length) &&
             write_response(STATUS_OK, response.data, response.length);
    free(response.data);
    return ok;
}

static int handle_run(const uint8_t *payload, size_t payload_length)
{
    struct reader reader = {payload, payload_length, 0};
    struct observation *observations = NULL;
    struct buffer response = {0};
    uint32_t cpu_values[CPU_FIELD_COUNT];
    uint16_t schema;
    uint16_t flags;
    uint32_t mode;
    uint32_t max_cycles;
    uint32_t cpu_mask;
    uint32_t segment_count;
    uint32_t observation_count;
    uint32_t index;
    uint32_t terminal;
    int cycles;
    int ok = 0;

    if (!reader_u16(&reader, &schema) || !reader_u16(&reader, &flags) ||
        !reader_u32(&reader, &mode) || !reader_u32(&reader, &max_cycles) ||
        !reader_u32(&reader, &stop_address) || !reader_u32(&reader, &stop_mask) ||
        !reader_u32(&reader, &stop_value) || !reader_u32(&reader, &cpu_mask))
        return write_error(STATUS_BAD_REQUEST, "truncated run prefix");
    if (schema != CASE_SCHEMA)
        return write_error(STATUS_UNSUPPORTED, "unsupported case schema");
    if (!(flags & FLAG_RESET))
        return write_error(STATUS_UNSUPPORTED, "non-reset cases are unsupported");
    if (flags & ~FLAG_RESET)
        return write_error(STATUS_BAD_REQUEST, "unknown run flags");
    if (mode < MODE_INSTRUCTION || mode > MODE_MEMORY || max_cycles == 0u ||
        max_cycles > INT32_MAX)
        return write_error(STATUS_BAD_REQUEST, "invalid run mode or cycle limit");

    for (index = 0; index < CPU_FIELD_COUNT; ++index) {
        if (!reader_u32(&reader, &cpu_values[index]))
            return write_error(STATUS_BAD_REQUEST, "truncated CPU state");
    }
    if (!reader_u32(&reader, &segment_count) ||
        !reader_u32(&reader, &observation_count))
        return write_error(STATUS_BAD_REQUEST, "truncated run counts");
    if (observation_count > MAX_OBSERVATIONS)
        return write_error(STATUS_BAD_REQUEST, "too many observations");

    memory_clear();
    stop_enabled = 0;
    stop_matched = 0;

    for (index = 0; index < segment_count; ++index) {
        uint32_t address;
        uint32_t length;
        uint32_t offset;
        const uint8_t *data;
        if (!reader_u32(&reader, &address) || !reader_u32(&reader, &length))
            goto bad_request;
        data = reader_bytes(&reader, length);
        if (data == NULL || (uint64_t)address + length > UINT64_C(0x100000000))
            goto bad_request;
        for (offset = 0; offset < length; ++offset) {
            if (!memory_write8(address + offset, data[offset]))
                goto internal_error;
        }
    }

    if (observation_count != 0u) {
        observations = calloc(observation_count, sizeof(*observations));
        if (observations == NULL)
            goto internal_error;
    }
    for (index = 0; index < observation_count; ++index) {
        if (!reader_u32(&reader, &observations[index].address) ||
            !reader_u32(&reader, &observations[index].length) ||
            observations[index].length == 0u ||
            (uint64_t)observations[index].address + observations[index].length >
                UINT64_C(0x100000000))
            goto bad_request;
    }
    if (reader.offset != reader.length)
        goto bad_request;

    m68k_set_cpu_type(selected_cpu_type);
    m68k_pulse_reset();
    /* A one-cycle slice consumes Musashi's pending reset accounting without
     * fetching an instruction. Case cycle counts therefore start at the first
     * architectural instruction, and MODE_INSTRUCTION really executes one. */
    (void)m68k_execute(1);
    if (!apply_cpu_state(cpu_mask, cpu_values))
        goto bad_request;

    stop_enabled = mode == MODE_MEMORY;
    if (stop_enabled)
        check_stop();
    if (stop_matched) {
        cycles = 0;
    } else if (mode == MODE_INSTRUCTION) {
        cycles = m68k_execute(1);
    } else {
        cycles = m68k_execute((int)max_cycles);
    }
    if (cycles < 0)
        cycles = 0;
    terminal = mode == MODE_INSTRUCTION ? TERMINAL_INSTRUCTION :
               stop_matched ? TERMINAL_MEMORY : TERMINAL_CYCLE_LIMIT;

    if (!buffer_u16(&response, CASE_SCHEMA) ||
        !buffer_u16(&response, (uint16_t)terminal) ||
        !buffer_u32(&response, (uint32_t)cycles))
        goto internal_error;
    for (index = 0; index < CPU_FIELD_COUNT; ++index) {
        if (!buffer_u32(&response, m68k_get_reg(NULL, cpu_fields[index])))
            goto internal_error;
    }
    if (!buffer_u32(&response, observation_count))
        goto internal_error;
    for (index = 0; index < observation_count; ++index) {
        uint32_t offset;
        if (!buffer_u32(&response, observations[index].address) ||
            !buffer_u32(&response, observations[index].length))
            goto internal_error;
        if (!buffer_reserve(&response, observations[index].length))
            goto internal_error;
        for (offset = 0; offset < observations[index].length; ++offset)
            response.data[response.length++] =
                memory_read8(observations[index].address + offset);
    }
    ok = write_response(STATUS_OK, response.data, response.length);
    goto done;

bad_request:
    ok = write_error(STATUS_BAD_REQUEST, "malformed run request");
    goto done;
internal_error:
    ok = write_error(STATUS_INTERNAL, "worker allocation failure");
done:
    stop_enabled = 0;
    free(observations);
    free(response.data);
    memory_clear();
    return ok;
}

static int select_cpu(int argc, char **argv)
{
    if (argc == 1)
        return 1;
    if (argc != 3 || strcmp(argv[1], "--cpu") != 0)
        return 0;
    if (strcmp(argv[2], "68000") == 0) {
        selected_cpu_type = M68K_CPU_TYPE_68000;
        selected_cpu_name = "vendored-musashi-68000";
        return 1;
    }
    if (strcmp(argv[2], "68030") == 0) {
        selected_cpu_type = M68K_CPU_TYPE_68030;
        selected_cpu_name = "vendored-musashi-68030-pmmu-no-fpu";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint8_t header[12];

    if (!select_cpu(argc, argv)) {
        fprintf(stderr, "usage: %s [--cpu 68000|68030]\n", argv[0]);
        return 2;
    }
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    m68k_init();
    m68k_set_pmmu_bus_callbacks(pmmu_memory_read32, pmmu_memory_write32);

    for (;;) {
        uint32_t magic;
        uint16_t version;
        uint16_t command;
        uint32_t length;
        uint8_t *payload = NULL;
        int received = read_exact(header, sizeof(header));

        if (received == 0)
            break;
        if (received < 0)
            return 2;
        magic = load_be32(header);
        version = load_be16(header + 4);
        command = load_be16(header + 6);
        length = load_be32(header + 8);
        if (magic != REQUEST_MAGIC || version != PROTOCOL_VERSION ||
            length > MAX_MESSAGE_BYTES) {
            write_error(STATUS_BAD_REQUEST, "invalid request header");
            return 2;
        }
        if (length != 0u) {
            payload = malloc(length);
            if (payload == NULL) {
                write_error(STATUS_INTERNAL, "worker allocation failure");
                return 2;
            }
            if (read_exact(payload, length) <= 0) {
                free(payload);
                return 2;
            }
        }

        if (command == COMMAND_HELLO) {
            if (length != 0u) {
                write_error(STATUS_BAD_REQUEST, "HELLO payload must be empty");
            } else if (!handle_hello()) {
                free(payload);
                return 2;
            }
        } else if (command == COMMAND_RUN) {
            if (!handle_run(payload, length)) {
                free(payload);
                return 2;
            }
        } else {
            write_error(STATUS_UNSUPPORTED, "unsupported command");
        }
        free(payload);
    }

    memory_clear();
    free(allocated_pages);
    return 0;
}
