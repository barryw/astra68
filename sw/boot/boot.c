// Astra 68 boot ROM: hardware inventory and destructive power-on self-test.
#include "vesta.h"
#include "vega.h"
#include "astraea.h"
#include "rom_build_info.h"

#define ROM_BANNER "ASTRA 68 SYSTEM ROM v" ASTRA_ROM_VERSION

#define MIB (1024u * 1024u)
#define SDRAM_READY_POLLS 5000000u
#define MEMTEST_TIMEOUT_POLLS 100000000u
#ifndef MEM_BENCH_BYTES
#define MEM_BENCH_BYTES (16u * 1024u)
#endif

#define MEM_BENCH_BYTE_SUM (0xa5u * MEM_BENCH_BYTES)
#define MEM_BENCH_WORD_SUM (0x5aa5u * (MEM_BENCH_BYTES / 2u))
#define MEM_BENCH_LONG_SUM (0x5aa5c33cu * (MEM_BENCH_BYTES / 4u))
#define LOCAL_BENCH_BYTES 256u
#define LOCAL_BENCH_BYTE_SUM (0xa5u * LOCAL_BENCH_BYTES)
#define LOCAL_BENCH_WORD_SUM (0x5aa5u * (LOCAL_BENCH_BYTES / 2u))
#define LOCAL_BENCH_LONG_SUM (0x5aa5c33cu * (LOCAL_BENCH_BYTES / 4u))
#ifndef DMA_BENCH_BYTES
#define DMA_BENCH_BYTES (64u * 1024u)
#endif
#define DMA_TIMEOUT_POLLS 10000000u
#define SCREEN_TOP_MARGIN 2u
#define SCREEN_LEFT_MARGIN 2u
#define SCREEN_RIGHT_MARGIN 2u
#define SCREEN_BOTTOM_MARGIN 2u

static uint32_t screen_row;
static uint32_t screen_col;
static int screen_enabled;
static const char *last_failure_phase;
static uint32_t last_failure_address;
static uint32_t last_failure_expected;
static uint32_t last_failure_actual;
static int last_failure_has_values;
static volatile uint32_t local_bench[LOCAL_BENCH_BYTES / sizeof(uint32_t)]
    __attribute__((aligned(16)));

static void screen_clear(void)
{
    if (!screen_enabled) return;
    for (uint32_t i = 0; i < VEGA_POST_COLS * VEGA_POST_ROWS; ++i)
        VEGA_POST_TEXT[i] = ' ';
    screen_row = SCREEN_TOP_MARGIN;
    screen_col = SCREEN_LEFT_MARGIN;
}

static void screen_scroll(void)
{
    const uint32_t last_row = VEGA_POST_ROWS - SCREEN_BOTTOM_MARGIN - 1u;
    const uint32_t last_col = VEGA_POST_COLS - SCREEN_RIGHT_MARGIN;

    for (uint32_t row = SCREEN_TOP_MARGIN; row < last_row; ++row) {
        for (uint32_t col = SCREEN_LEFT_MARGIN; col < last_col; ++col) {
            VEGA_POST_TEXT[row * VEGA_POST_COLS + col] =
                VEGA_POST_TEXT[(row + 1u) * VEGA_POST_COLS + col];
        }
    }
    for (uint32_t col = SCREEN_LEFT_MARGIN; col < last_col; ++col)
        VEGA_POST_TEXT[last_row * VEGA_POST_COLS + col] = ' ';
    screen_row = last_row;
    screen_col = SCREEN_LEFT_MARGIN;
}

static void screen_putc(char c)
{
    const uint32_t last_col = VEGA_POST_COLS - SCREEN_RIGHT_MARGIN;
    const uint32_t last_row = VEGA_POST_ROWS - SCREEN_BOTTOM_MARGIN;

    if (!screen_enabled || c == '\r') return;
    if (c == '\n') {
        screen_col = SCREEN_LEFT_MARGIN;
        ++screen_row;
    } else {
        VEGA_POST_TEXT[screen_row * VEGA_POST_COLS + screen_col] = (uint8_t)c;
        if (++screen_col == last_col) {
            screen_col = SCREEN_LEFT_MARGIN;
            ++screen_row;
        }
    }
    if (screen_row == last_row) screen_scroll();
}

static void screen_puts(const char *s)
{
    while (*s) screen_putc(*s++);
}

static void screen_init(void)
{
    screen_enabled = VEGA->ID == VEGA_ID_MAGIC && (VEGA->CAPS & VEGA_CAP_POST_TEXT);
    screen_clear();
}

static void serial_putc(char c)
{
    while (!(VESTA->UART_STATUS & UART_TX_READY)) {}
    VESTA->UART_DATA = (uint8_t)c;
}

static void uart_putc(char c)
{
    screen_putc(c);
    serial_putc(c);
}

static void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_hex32(uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfu;
        uart_putc((char)(digit < 10u ? '0' + digit : 'A' + digit - 10u));
    }
}

static void serial_hex32(uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfu;
        serial_putc((char)(digit < 10u ? '0' + digit : 'A' + digit - 10u));
    }
}

static void uart_dec32(uint32_t value)
{
    static const uint32_t divisors[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u
    };
    int started = 0;

    for (unsigned i = 0; i < sizeof(divisors) / sizeof(divisors[0]); ++i) {
        uint32_t digit = 0;
        while (value >= divisors[i]) {
            value -= divisors[i];
            ++digit;
        }
        if (digit != 0u || started || divisors[i] == 1u) {
            uart_putc((char)('0' + digit));
            started = 1;
        }
    }
}

static void serial_dec32(uint32_t value)
{
    static const uint32_t divisors[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u
    };
    int started = 0;

    for (unsigned i = 0; i < sizeof(divisors) / sizeof(divisors[0]); ++i) {
        uint32_t digit = 0;
        while (value >= divisors[i]) {
            value -= divisors[i];
            ++digit;
        }
        if (digit != 0u || started || divisors[i] == 1u) {
            serial_putc((char)('0' + digit));
            started = 1;
        }
    }
}

static void uart_fourcc(uint32_t id)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint8_t c = (uint8_t)(id >> shift);
        uart_putc((char)(c >= 0x20u && c <= 0x7eu ? c : '?'));
    }
}

static void uart_version(uint32_t version)
{
    uart_dec32(version >> 16);
    uart_putc('.');
    uart_dec32(version & 0xffffu);
}

static const char *cpu_name(uint32_t model, uint32_t implementation)
{
    if (implementation == CPU_IMPL_TGM2) return "TG68K.C 68030 MMU2";
    if (implementation == CPU_IMPL_TG30) return "TG68K.C 68030";
    if (implementation == CPU_IMPL_TG20) return "TG68K.C 68020";
    if (implementation == CPU_IMPL_WF30) return "WF68K30L 68030";
    if (model == CPU_MODEL_68030) return "68030-compatible";
    if (model == CPU_MODEL_68020) return "68020-compatible";
    return "unknown";
}

static void print_inventory(void)
{
    uart_puts("Vesta:  v");
    uart_version(VESTA->VERSION);
    uart_putc('\n');

    uart_puts("CPU:    ");
    uart_puts(cpu_name(VESTA->CPU_MODEL, VESTA->CPU_IMPL));
    uart_puts(" @ ");
    uart_dec32(VESTA->CPU_HZ);
    uart_puts(" Hz\n");

    uart_puts("MMU:    ");
    uart_puts((VESTA->CPU_FEATURES & CPU_FEAT_PMMU) ?
              "integrated 68030 PMMU\n" : "none\n");

    uart_puts("RAM:    ");
    uart_dec32(VESTA->RAM_SIZE >> 20);
    uart_puts(" MiB @ 0x");
    uart_hex32(VESTA->RAM_BASE);
    uart_putc('\n');

    uart_puts("ROM:    ");
    uart_dec32(VESTA->ROM_SIZE >> 10);
    uart_puts(" KiB @ 0x");
    uart_hex32(VESTA->ROM_BASE);
    uart_putc('\n');

    uart_puts("BUILD:  0x");
    uart_hex32(VESTA->BUILD_ID);
    uart_putc('\n');

    uart_puts("Chips:  ");
    uint32_t count = VESTA->PERSONALITY_COUNT;
    if (count > 8u) count = 8u;
    if (count == 0u) {
        uart_puts("none");
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            if (i != 0u) uart_puts(", ");
            uart_fourcc(VESTA->PERSONALITY[i].ID);
            uart_puts(" v");
            uart_version(VESTA->PERSONALITY[i].VERSION);
        }
    }
    uart_putc('\n');

    uart_puts("NVRAM:  ");
    uart_puts(VESTA->NVRAM_CAPS ? "available\n" : "not present; defaults active\n");
}

static int post_failure(const char *phase, uint32_t address,
                        uint32_t expected, uint32_t actual)
{
    last_failure_phase = phase;
    last_failure_address = address;
    last_failure_expected = expected;
    last_failure_actual = actual;
    last_failure_has_values = 1;

    uart_puts("\nPOST FAIL: ");
    uart_puts(phase);
    uart_puts(" @ 0x");
    uart_hex32(address);
    uart_puts(" expected=0x");
    uart_hex32(expected);
    uart_puts(" actual=0x");
    uart_hex32(actual);
    uart_putc('\n');
    return 0;
}

static int post_failure_text(const char *phase)
{
    last_failure_phase = phase;
    last_failure_has_values = 0;
    uart_puts("POST FAIL: ");
    uart_puts(phase);
    uart_putc('\n');
    return 0;
}

static void serial_repeat_failure(void)
{
    if (!last_failure_phase) return;
    serial_puts("POST FAIL: ");
    serial_puts(last_failure_phase);
    if (last_failure_has_values) {
        serial_puts(" @ 0x");
        serial_hex32(last_failure_address);
        serial_puts(" expected=0x");
        serial_hex32(last_failure_expected);
        serial_puts(" actual=0x");
        serial_hex32(last_failure_actual);
    }
    serial_putc('\n');
}

static int wait_for_sdram(void)
{
    if (!(VESTA->SYS_STATUS & SYS_SDRAM_PRESENT)) {
        return post_failure_text("SDRAM not present");
    }

    for (uint32_t i = 0; i < SDRAM_READY_POLLS; ++i) {
        if (VESTA->SYS_STATUS & SYS_SDRAM_READY) return 1;
    }
    return post_failure_text("SDRAM initialization timeout");
}

static int test_data_bus(volatile uint32_t *base)
{
    static const uint32_t fixed[] = {
        0x00000000u, 0xffffffffu, 0xaaaaaaaau, 0x55555555u,
        0xccccccccu, 0x33333333u, 0xf0f0f0f0u, 0x0f0f0f0fu
    };

    for (unsigned i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        base[0] = fixed[i];
        uint32_t actual = base[0];
        if (actual != fixed[i])
            return post_failure("SDRAM data bus", (uint32_t)base, fixed[i], actual);
    }

    for (uint32_t bit = 1u; bit != 0u; bit <<= 1) {
        base[0] = bit;
        uint32_t actual = base[0];
        if (actual != bit)
            return post_failure("SDRAM data bus", (uint32_t)base, bit, actual);
        base[0] = ~bit;
        actual = base[0];
        if (actual != ~bit)
            return post_failure("SDRAM data bus", (uint32_t)base, ~bit, actual);
    }
    return 1;
}

static int test_access_widths(uint32_t ram_base)
{
    static const uint8_t long_bytes[] = { 0x12u, 0x34u, 0xabu, 0xcdu };
    volatile uint8_t *bytes = (volatile uint8_t *)(ram_base + 0x100u);
    for (uint32_t i = 0; i < 16u; ++i) bytes[i] = (uint8_t)(0xa5u ^ (i * 0x11u));
    for (uint32_t i = 0; i < 16u; ++i) {
        uint32_t expected = (uint8_t)(0xa5u ^ (i * 0x11u));
        if (bytes[i] != expected)
            return post_failure("SDRAM byte lanes", (uint32_t)&bytes[i], expected, bytes[i]);
    }

    volatile uint16_t *odd_word = (volatile uint16_t *)(ram_base + 0x121u);
    *odd_word = 0x39c6u;
    if (*odd_word != 0x39c6u)
        return post_failure("SDRAM unaligned word", (uint32_t)odd_word, 0x39c6u, *odd_word);

    for (uint32_t offset = 0; offset < 4u; ++offset) {
        volatile uint8_t *slot = (volatile uint8_t *)(ram_base + 0x140u + offset * 8u);
        volatile uint32_t *long_ptr = (volatile uint32_t *)(slot + offset);

        for (uint32_t i = 0; i < 8u; ++i) slot[i] = (uint8_t)(0x80u + i);
        *long_ptr = 0x1234abcdu;
        if (*long_ptr != 0x1234abcdu)
            return post_failure("SDRAM long alignment", (uint32_t)long_ptr,
                                0x1234abcdu, *long_ptr);

        for (uint32_t i = 0; i < 8u; ++i) {
            uint32_t expected = (i >= offset && i < offset + 4u)
                              ? long_bytes[i - offset]
                              : (uint8_t)(0x80u + i);
            if (slot[i] != expected)
                return post_failure("SDRAM long byte preserve", (uint32_t)&slot[i],
                                    expected, slot[i]);
        }
    }
    return 1;
}

static int test_cache_coherence(uint32_t ram_base)
{
    volatile uint32_t *code = (volatile uint32_t *)(ram_base + 0x200u);
    uint32_t (*function)(void) = (uint32_t (*)(void))(ram_base + 0x200u);

    code[0] = 0x70014e75u; // moveq #1,d0; rts
    uint32_t actual = function();
    if (actual != 1u)
        return post_failure("SDRAM cached code", (uint32_t)code, 1u, actual);

    code[0] = 0x70024e75u; // moveq #2,d0; rts
    actual = function();
    if (actual != 2u)
        return post_failure("SDRAM cache invalidate", (uint32_t)code, 2u, actual);
    return 1;
}

static int test_address_bus(volatile uint32_t *base, uint32_t words)
{
    const uint32_t pattern = 0xaaaaaaaau;
    const uint32_t antipattern = 0x55555555u;

    base[0] = antipattern;
    for (uint32_t offset = 1u; offset < words; offset <<= 1) base[offset] = pattern;
    if (base[0] != antipattern)
        return post_failure("SDRAM address bus", (uint32_t)base, antipattern, base[0]);

    for (uint32_t test = 1u; test < words; test <<= 1) {
        base[test] = antipattern;
        if (base[0] != antipattern)
            return post_failure("SDRAM address alias", (uint32_t)base, antipattern, base[0]);
        for (uint32_t offset = 1u; offset < words; offset <<= 1) {
            if (offset != test && base[offset] != pattern) {
                return post_failure("SDRAM address alias",
                                    (uint32_t)&base[offset], pattern, base[offset]);
            }
        }
        base[test] = pattern;
    }
    return 1;
}

static void print_cycle_pair(const char *width, uint32_t write_cycles,
                             uint32_t read_cycles)
{
    serial_puts("    ");
    serial_puts(width);
    serial_puts(" write=");
    serial_dec32(write_cycles);
    serial_puts(" read=");
    serial_dec32(read_cycles);
    serial_putc('\n');
}

static void print_hot_read(uint32_t cycles)
{
    serial_puts("      hot read=");
    serial_dec32(cycles);
    serial_putc('\n');
}

typedef struct {
    uint32_t dcache_misses;
    uint32_t sdram_reads;
    uint32_t sdram_writes;
    uint32_t sdram_wait;
    uint32_t line_hits;
    uint32_t line_misses;
    uint32_t posted_writes;
} MemoryPerf;

static void memory_perf_read(MemoryPerf *perf)
{
    perf->dcache_misses = VESTA->DCACHE_MISSES;
    perf->sdram_reads = VESTA->CPU_SDRAM_READS;
    perf->sdram_writes = VESTA->CPU_SDRAM_WRITES;
    perf->sdram_wait = VESTA->CPU_SDRAM_WAIT;
    perf->line_hits = VESTA->SDRAM_LINE_HITS;
    perf->line_misses = VESTA->SDRAM_LINE_MISSES;
    perf->posted_writes = VESTA->SDRAM_POSTED_WRITES;
}

static void print_memory_perf(const char *phase, const MemoryPerf *before,
                              const MemoryPerf *after)
{
    serial_puts("      ");
    serial_puts(phase);
    serial_puts(" dmiss=");
    serial_dec32(after->dcache_misses - before->dcache_misses);
    serial_puts(" rdreq=");
    serial_dec32(after->sdram_reads - before->sdram_reads);
    serial_puts(" wrreq=");
    serial_dec32(after->sdram_writes - before->sdram_writes);
    serial_puts(" wait=");
    serial_dec32(after->sdram_wait - before->sdram_wait);
    serial_puts(" lhit=");
    serial_dec32(after->line_hits - before->line_hits);
    serial_puts(" lmiss=");
    serial_dec32(after->line_misses - before->line_misses);
    serial_puts(" post=");
    serial_dec32(after->posted_writes - before->posted_writes);
    serial_putc('\n');
}

static int benchmark_local_access_widths(void)
{
    uint32_t start;
    uint32_t write_cycles;
    uint32_t read_cycles;
    uint32_t sum;
    volatile uint8_t *bytes = (volatile uint8_t *)local_bench;
    volatile uint16_t *words = (volatile uint16_t *)local_bench;
    volatile uint32_t *longs = local_bench;

    serial_puts("  CPU BRAM cycles (256 bytes payload)\n");

    start = VESTA->CPU_CYCLES_LO;
    for (uint32_t i = 0; i < LOCAL_BENCH_BYTES; ++i) bytes[i] = 0xa5u;
    write_cycles = VESTA->CPU_CYCLES_LO - start;
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < LOCAL_BENCH_BYTES; ++i) sum += bytes[i];
    read_cycles = VESTA->CPU_CYCLES_LO - start;
    if (sum != LOCAL_BENCH_BYTE_SUM)
        return post_failure("BRAM byte benchmark", (uint32_t)bytes,
                            LOCAL_BENCH_BYTE_SUM, sum);
    print_cycle_pair("8-bit ", write_cycles, read_cycles);

    start = VESTA->CPU_CYCLES_LO;
    for (uint32_t i = 0; i < LOCAL_BENCH_BYTES / 2u; ++i)
        words[i] = 0x5aa5u;
    write_cycles = VESTA->CPU_CYCLES_LO - start;
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < LOCAL_BENCH_BYTES / 2u; ++i) sum += words[i];
    read_cycles = VESTA->CPU_CYCLES_LO - start;
    if (sum != LOCAL_BENCH_WORD_SUM)
        return post_failure("BRAM word benchmark", (uint32_t)words,
                            LOCAL_BENCH_WORD_SUM, sum);
    print_cycle_pair("16-bit", write_cycles, read_cycles);

    start = VESTA->CPU_CYCLES_LO;
    for (uint32_t i = 0; i < LOCAL_BENCH_BYTES / 4u; ++i)
        longs[i] = 0x5aa5c33cu;
    write_cycles = VESTA->CPU_CYCLES_LO - start;
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < LOCAL_BENCH_BYTES / 4u; ++i) sum += longs[i];
    read_cycles = VESTA->CPU_CYCLES_LO - start;
    if (sum != LOCAL_BENCH_LONG_SUM)
        return post_failure("BRAM long benchmark", (uint32_t)longs,
                            LOCAL_BENCH_LONG_SUM, sum);
    print_cycle_pair("32-bit", write_cycles, read_cycles);
    return 1;
}

static int benchmark_access_widths(uint32_t ram_base)
{
    uint32_t start;
    uint32_t write_cycles;
    uint32_t read_cycles;
    uint32_t hot_read_cycles;
    uint32_t sum;
    MemoryPerf before;
    MemoryPerf after_write;
    MemoryPerf after_read;
    volatile uint8_t *bytes = (volatile uint8_t *)(ram_base + 0x10000u);
    volatile uint16_t *words = (volatile uint16_t *)bytes;
    volatile uint32_t *longs = (volatile uint32_t *)bytes;

    serial_puts("  CPU memory cycles (");
    serial_dec32(MEM_BENCH_BYTES);
    serial_puts(" bytes payload)\n");

    memory_perf_read(&before);
    start = VESTA->CPU_CYCLES_LO;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES; ++i) bytes[i] = 0xa5u;
    write_cycles = VESTA->CPU_CYCLES_LO - start;
    memory_perf_read(&after_write);
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES; ++i) sum += bytes[i];
    read_cycles = VESTA->CPU_CYCLES_LO - start;
    memory_perf_read(&after_read);
    if (sum != MEM_BENCH_BYTE_SUM)
        return post_failure("SDRAM byte benchmark", (uint32_t)bytes,
                            MEM_BENCH_BYTE_SUM, sum);
    print_cycle_pair("8-bit ", write_cycles, read_cycles);
    print_memory_perf("write", &before, &after_write);
    print_memory_perf("read ", &after_write, &after_read);
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES; ++i) sum += bytes[i];
    hot_read_cycles = VESTA->CPU_CYCLES_LO - start;
    if (sum != MEM_BENCH_BYTE_SUM)
        return post_failure("SDRAM hot byte benchmark", (uint32_t)bytes,
                            MEM_BENCH_BYTE_SUM, sum);
    print_hot_read(hot_read_cycles);

    memory_perf_read(&before);
    start = VESTA->CPU_CYCLES_LO;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES / 2u; ++i) words[i] = 0x5aa5u;
    write_cycles = VESTA->CPU_CYCLES_LO - start;
    memory_perf_read(&after_write);
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES / 2u; ++i) sum += words[i];
    read_cycles = VESTA->CPU_CYCLES_LO - start;
    memory_perf_read(&after_read);
    if (sum != MEM_BENCH_WORD_SUM)
        return post_failure("SDRAM word benchmark", (uint32_t)words,
                            MEM_BENCH_WORD_SUM, sum);
    print_cycle_pair("16-bit", write_cycles, read_cycles);
    print_memory_perf("write", &before, &after_write);
    print_memory_perf("read ", &after_write, &after_read);
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES / 2u; ++i) sum += words[i];
    hot_read_cycles = VESTA->CPU_CYCLES_LO - start;
    if (sum != MEM_BENCH_WORD_SUM)
        return post_failure("SDRAM hot word benchmark", (uint32_t)words,
                            MEM_BENCH_WORD_SUM, sum);
    print_hot_read(hot_read_cycles);

    memory_perf_read(&before);
    start = VESTA->CPU_CYCLES_LO;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES / 4u; ++i)
        longs[i] = 0x5aa5c33cu;
    write_cycles = VESTA->CPU_CYCLES_LO - start;
    memory_perf_read(&after_write);
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES / 4u; ++i) sum += longs[i];
    read_cycles = VESTA->CPU_CYCLES_LO - start;
    memory_perf_read(&after_read);
    if (sum != MEM_BENCH_LONG_SUM)
        return post_failure("SDRAM long benchmark", (uint32_t)longs,
                            MEM_BENCH_LONG_SUM, sum);
    print_cycle_pair("32-bit", write_cycles, read_cycles);
    print_memory_perf("write", &before, &after_write);
    print_memory_perf("read ", &after_write, &after_read);
    start = VESTA->CPU_CYCLES_LO;
    sum = 0u;
    for (uint32_t i = 0; i < MEM_BENCH_BYTES / 4u; ++i) sum += longs[i];
    hot_read_cycles = VESTA->CPU_CYCLES_LO - start;
    if (sum != MEM_BENCH_LONG_SUM)
        return post_failure("SDRAM hot long benchmark", (uint32_t)longs,
                            MEM_BENCH_LONG_SUM, sum);
    print_hot_read(hot_read_cycles);
    return 1;
}

static int wait_for_blitter(const char *phase, uint32_t start,
                            uint32_t *elapsed)
{
    for (uint32_t polls = 0; polls < DMA_TIMEOUT_POLLS; ++polls) {
        uint32_t status = ASTRAEA->BLIT_STATUS;
        if (!(status & BLIT_BUSY) && (status & BLIT_DONE)) {
            *elapsed = VESTA->CPU_CYCLES_LO - start;
            if (status & BLIT_ERROR)
                return post_failure(phase, ASTRAEA_BASE + 0x6cu,
                                    0u, status);
            return 1;
        }
    }
    return post_failure_text("Astraea blitter timeout");
}

static int benchmark_astraea(uint32_t ram_base)
{
    const uint32_t src_offset = 0x00100000u;
    const uint32_t dst_offset = 0x00200000u;
    const uint32_t color = 0x5aa5c33cu;
    volatile uint32_t *src = (volatile uint32_t *)(ram_base + src_offset);
    volatile uint32_t *dst = (volatile uint32_t *)(ram_base + dst_offset);
    uint32_t fill_cycles;
    uint32_t copy_cycles;
    uint32_t start;

    serial_puts("  Astraea DMA (");
    serial_dec32(DMA_BENCH_BYTES / 1024u);
    serial_puts(" KiB)\n");
    if (ASTRAEA->ID != ASTRAEA_ID_MAGIC)
        return post_failure("Astraea identity", ASTRAEA_BASE,
                            ASTRAEA_ID_MAGIC, ASTRAEA->ID);

    ASTRAEA->BLIT_DST = src_offset;
    ASTRAEA->BLIT_DST_PITCH = DMA_BENCH_BYTES;
    ASTRAEA->BLIT_DIM = BLIT_DIM_(DMA_BENCH_BYTES / 4u, 1u);
    ASTRAEA->BLIT_OP = BLIT_MODE_FILL | BLIT_ELEM32;
    ASTRAEA->BLIT_COLOR = color;
    start = VESTA->CPU_CYCLES_LO;
    ASTRAEA->BLIT_CTRL = BLIT_START;
    if (!wait_for_blitter("Astraea fill", start, &fill_cycles)) return 0;

    for (uint32_t i = 0; i < DMA_BENCH_BYTES / 4u; i += 1024u) {
        if (src[i] != color)
            return post_failure("Astraea fill data", (uint32_t)&src[i],
                                color, src[i]);
    }
    if (src[DMA_BENCH_BYTES / 4u - 1u] != color)
        return post_failure("Astraea fill tail",
                            (uint32_t)&src[DMA_BENCH_BYTES / 4u - 1u],
                            color, src[DMA_BENCH_BYTES / 4u - 1u]);

    ASTRAEA->BLIT_SRC = src_offset;
    ASTRAEA->BLIT_DST = dst_offset;
    ASTRAEA->BLIT_SRC_PITCH = DMA_BENCH_BYTES;
    ASTRAEA->BLIT_DST_PITCH = DMA_BENCH_BYTES;
    ASTRAEA->BLIT_DIM = BLIT_DIM_(DMA_BENCH_BYTES / 4u, 1u);
    ASTRAEA->BLIT_OP = BLIT_MODE_COPY | BLIT_ELEM32;
    start = VESTA->CPU_CYCLES_LO;
    ASTRAEA->BLIT_CTRL = BLIT_START;
    if (!wait_for_blitter("Astraea copy", start, &copy_cycles)) return 0;

    for (uint32_t i = 0; i < DMA_BENCH_BYTES / 4u; i += 1024u) {
        if (dst[i] != color)
            return post_failure("Astraea copy data", (uint32_t)&dst[i],
                                color, dst[i]);
    }
    if (dst[DMA_BENCH_BYTES / 4u - 1u] != color)
        return post_failure("Astraea copy tail",
                            (uint32_t)&dst[DMA_BENCH_BYTES / 4u - 1u],
                            color, dst[DMA_BENCH_BYTES / 4u - 1u]);

    serial_puts("    fill=");
    serial_dec32(fill_cycles);
    serial_puts(" copy=");
    serial_dec32(copy_cycles);
    serial_putc('\n');
    return 1;
}

static int test_full_range(uint32_t ram_base, uint32_t ram_size)
{
    uint32_t total_mib = ram_size >> 20;
    uint32_t initial_status = VESTA->MEMTEST_STATUS;

    // SD stage-0 runs the destructive sweep while all executable code is still
    // in bootstrap BRAM. Re-running it after this ROM has moved into SDRAM
    // would erase the code currently executing.
    if ((initial_status & MEMTEST_DONE) != 0u &&
        (initial_status & MEMTEST_BUSY) == 0u) {
        serial_puts("  Full-range BIST ... [stage0] ");
        if (VESTA->MEMTEST_ERRORS != 0u) {
            return post_failure("SDRAM full range",
                                ram_base + VESTA->MEMTEST_FIRST_FAIL,
                                VESTA->MEMTEST_EXPECTED,
                                VESTA->MEMTEST_ACTUAL);
        }
        serial_puts("OK\n");
        return 1;
    }

    serial_puts("  Full-range BIST ... [");
    VESTA->MEMTEST_CTRL = MEMTEST_START;

    uint32_t seen_busy = 0u;
    uint32_t last_phase = 0u;
    uint32_t displayed_mib = 0u;
    for (uint32_t polls = 0; polls < MEMTEST_TIMEOUT_POLLS; ++polls) {
        uint32_t status = VESTA->MEMTEST_STATUS;
        uint32_t phase = MEMTEST_PHASE(status);
        if (status & MEMTEST_BUSY) seen_busy = 1u;

        if (phase != last_phase) {
            if (last_phase == MEMTEST_PHASE_WRITE || last_phase == MEMTEST_PHASE_READ) {
                while (displayed_mib < total_mib) {
                    serial_putc('.');
                    ++displayed_mib;
                }
            }
            if (phase == MEMTEST_PHASE_WRITE || phase == MEMTEST_PHASE_READ) {
                serial_putc(phase == MEMTEST_PHASE_WRITE ? 'W' : 'R');
                displayed_mib = 0u;
            }
            last_phase = phase;
        }

        uint32_t progress_mib = VESTA->MEMTEST_PROGRESS >> 20;
        while (displayed_mib < progress_mib && displayed_mib < total_mib) {
            serial_putc('.');
            ++displayed_mib;
        }

        if (seen_busy && (status & MEMTEST_DONE) && !(status & MEMTEST_BUSY)) {
            while (displayed_mib < total_mib) {
                serial_putc('.');
                ++displayed_mib;
            }
            serial_puts("] ");
            if (VESTA->MEMTEST_ERRORS != 0u) {
                return post_failure("SDRAM full range",
                                    ram_base + VESTA->MEMTEST_FIRST_FAIL,
                                    VESTA->MEMTEST_EXPECTED,
                                    VESTA->MEMTEST_ACTUAL);
            }
            serial_puts("OK\n");
            return 1;
        }
    }
    serial_puts("]\n");
    return post_failure_text("SDRAM BIST timeout");
}

static int run_post(void)
{
    uint32_t ram_base = VESTA->RAM_BASE;
    uint32_t ram_size = VESTA->RAM_SIZE;

    uart_puts("\nPOST\n");
    uart_puts("  SDRAM init ........ ");
    if (!wait_for_sdram()) return 0;
    uart_puts("OK\n");

    if (ram_size < MIB || (ram_size & 3u) != 0u) {
        return post_failure_text("invalid hardware RAM map");
    }

    volatile uint32_t *base = (volatile uint32_t *)ram_base;
    uint32_t words = ram_size / sizeof(uint32_t);

    uart_puts("  Data/byte lanes .... ");
    if (!test_data_bus(base) || !test_access_widths(ram_base)) return 0;
    uart_puts("OK\n");

    uart_puts("  Address lines ...... ");
    if (!test_address_bus(base, words)) return 0;
    uart_puts("OK\n");

    uart_puts("  Cache coherence .... ");
    if (!test_cache_coherence(ram_base)) return 0;
    uart_puts("OK\n");

    screen_puts("  CPU BRAM access .... ");
    if (!benchmark_local_access_widths()) return 0;
    screen_puts("OK\n");

    screen_puts("  CPU SDRAM access ... ");
    if (!benchmark_access_widths(ram_base)) return 0;
    screen_puts("OK\n");

    screen_puts("  Astraea DMA ........ ");
    if (!benchmark_astraea(ram_base)) return 0;
    screen_puts("OK\n");

    screen_puts("  Full-range BIST .... ");
    if (!test_full_range(ram_base, ram_size)) return 0;
    screen_puts("OK\n");
    return 1;
}

static void idle_forever(const char *screen_message, const char *serial_message)
{
    uart_puts(screen_message);
    for (;;) {
        serial_puts(serial_message);
        serial_puts("ROM: v" ASTRA_ROM_VERSION "  Built: " ASTRA_ROM_BUILD_UTC
                    "  Git: " ASTRA_ROM_GIT_REVISION "\n");
        serial_puts("BUILD: 0x");
        serial_hex32(VESTA->BUILD_ID);
        serial_putc('\n');
        serial_repeat_failure();
        for (volatile uint32_t delay = 0; delay < 1000000u; ++delay) {}
    }
}

void kmain(void)
{
    screen_init();
    uart_puts(ROM_BANNER "\n");
    uart_puts("Built: " ASTRA_ROM_BUILD_UTC
              "  Git: " ASTRA_ROM_GIT_REVISION "\n\n");
    if (VESTA->ID != VESTA_ID_MAGIC) {
        post_failure_text("Vesta identity block unavailable");
        idle_forever("HALTED: POST FAILURE\n",
                     "\n" ROM_BANNER " - POST FAILURE\n");
    }

    print_inventory();
    if (!run_post())
        idle_forever("HALTED: POST FAILURE\n",
                     "\n" ROM_BANNER " - POST FAILURE\n");

    uart_puts("\nPOST PASS\n");
    idle_forever("READY FOR OS LOADER\n",
                 "\n" ROM_BANNER " - POST PASS - READY FOR OS LOADER\n");
}
