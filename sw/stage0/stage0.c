#include "fat_loader.h"
#include "vega.h"
#include "vesta.h"

#define STAGE0_VERSION "0.2"
#define SDRAM_READY_POLLS 10000000u
#define BIST_POLLS 200000000u
#define HOST_BOOT_POLLS 200000000u
#ifndef ASTRA_HOST_BOOT
#define SD_COMMAND_POLLS 16u
#define SD_INIT_POLLS 10000u
#define SD_TOKEN_POLLS 100000u
#define STAGE2_LOAD_ADDRESS ((uint8_t *)0x03e00000u)
#endif

#ifndef ASTRA_HOST_BOOT
static uint8_t sector[FAT_BOOT_SECTOR_SIZE] __attribute__((aligned(4)));
#endif
static uint32_t screen_row;
static uint32_t screen_col;
static int screen_enabled;
#ifndef ASTRA_HOST_BOOT
static uint8_t spi_divider;
static int sd_high_capacity;
#endif

extern void stage0_handoff(uint32_t initial_sp, uint32_t initial_pc)
    __attribute__((noreturn));

static void screen_init(void)
{
    screen_enabled = VEGA->ID == VEGA_ID_MAGIC &&
                     (VEGA->CAPS & VEGA_CAP_POST_TEXT) != 0u;
    if (!screen_enabled) return;
    for (uint32_t index = 0; index < VEGA_POST_COLS * VEGA_POST_ROWS; ++index)
        VEGA_POST_TEXT[index] = ' ';
    screen_row = 2u;
    screen_col = 2u;
}

static void screen_putc(char value)
{
    if (!screen_enabled || value == '\r') return;
    if (value == '\n') {
        ++screen_row;
        screen_col = 2u;
        return;
    }
    if (screen_row >= VEGA_POST_ROWS - 2u) return;
    VEGA_POST_TEXT[screen_row * VEGA_POST_COLS + screen_col] = (uint8_t)value;
    if (++screen_col >= VEGA_POST_COLS - 2u) {
        ++screen_row;
        screen_col = 2u;
    }
}

static void serial_putc(char value)
{
    while ((VESTA->UART_STATUS & 1u) == 0u) {}
    VESTA->UART_DATA = (uint8_t)value;
}

static void putc(char value)
{
    screen_putc(value);
    if (value == '\n') serial_putc('\r');
    serial_putc(value);
}

static void puts(const char *text)
{
    while (*text != '\0') putc(*text++);
}

static void put_hex(uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfu;
        putc((char)(digit < 10u ? '0' + digit : 'A' + digit - 10u));
    }
}

static void halt(int error)
{
    puts("FAILED E");
    putc((char)('0' + error));
    puts("\nRESET AFTER REPAIR\n");
    for (;;) __asm__ volatile ("stop #0x2700");
}

#ifndef ASTRA_HOST_BOOT
static uint8_t spi_transfer(uint8_t value)
{
    while ((VESTA->SPI_STATUS & SPI_STATUS_BUSY) != 0u) {}
    VESTA->SPI_DATA = value;
    while ((VESTA->SPI_STATUS & SPI_STATUS_BUSY) != 0u) {}
    return (uint8_t)VESTA->SPI_DATA;
}

static void spi_select(int selected)
{
    while ((VESTA->SPI_STATUS & SPI_STATUS_BUSY) != 0u) {}
    VESTA->SPI_CTRL = SPI_CTRL_CLKDIV(spi_divider) |
                      (selected ? 0u : SPI_CTRL_CS_N);
}

static void sd_end_command(void)
{
    (void)spi_transfer(0xffu);
    spi_select(0);
    (void)spi_transfer(0xffu);
}

static uint8_t sd_command(uint8_t command, uint32_t argument, uint8_t crc)
{
    spi_select(0);
    (void)spi_transfer(0xffu);
    spi_select(1);
    (void)spi_transfer(0xffu);
    (void)spi_transfer((uint8_t)(0x40u | command));
    (void)spi_transfer((uint8_t)(argument >> 24));
    (void)spi_transfer((uint8_t)(argument >> 16));
    (void)spi_transfer((uint8_t)(argument >> 8));
    (void)spi_transfer((uint8_t)argument);
    (void)spi_transfer(crc);

    for (uint32_t poll = 0; poll < SD_COMMAND_POLLS; ++poll) {
        uint8_t response = spi_transfer(0xffu);
        if ((response & 0x80u) == 0u) return response;
    }
    return 0xffu;
}

static int sd_initialize(void)
{
    spi_divider = 15u;
    spi_select(0);
    for (unsigned index = 0; index < 10; ++index) (void)spi_transfer(0xffu);

    uint8_t response = 0xffu;
    for (unsigned retry = 0; retry < 32; ++retry) {
        response = sd_command(0u, 0u, 0x95u);
        sd_end_command();
        if (response == 0x01u) break;
    }
    if (response != 0x01u) return 0;

    int version2 = 0;
    response = sd_command(8u, 0x000001aau, 0x87u);
    if (response == 0x01u) {
        uint8_t r7[4];
        for (unsigned index = 0; index < 4; ++index) r7[index] = spi_transfer(0xffu);
        sd_end_command();
        if (r7[2] != 0x01u || r7[3] != 0xaau) return 0;
        version2 = 1;
    } else if ((response & 0x04u) != 0u) {
        sd_end_command();
    } else {
        sd_end_command();
        return 0;
    }

    for (uint32_t poll = 0; poll < SD_INIT_POLLS; ++poll) {
        response = sd_command(55u, 0u, 0x01u);
        sd_end_command();
        if (response > 0x01u) return 0;
        response = sd_command(41u, version2 ? 0x40000000u : 0u, 0x01u);
        sd_end_command();
        if (response == 0x00u) break;
    }
    if (response != 0x00u) return 0;

    response = sd_command(58u, 0u, 0x01u);
    if (response != 0x00u) {
        sd_end_command();
        return 0;
    }
    uint8_t ocr[4];
    for (unsigned index = 0; index < 4; ++index) ocr[index] = spi_transfer(0xffu);
    sd_end_command();
    sd_high_capacity = version2 && (ocr[0] & 0x40u) != 0u;

    if (!sd_high_capacity) {
        response = sd_command(16u, FAT_BOOT_SECTOR_SIZE, 0x01u);
        sd_end_command();
        if (response != 0x00u) return 0;
    }

    spi_divider = 0u;
    spi_select(0);
    return 1;
}

static int sd_read_sector(void *context, uint32_t lba, uint8_t *data)
{
    (void)context;
    uint32_t argument = sd_high_capacity ? lba : lba << 9;
    uint8_t response = sd_command(17u, argument, 0x01u);
    if (response != 0x00u) {
        sd_end_command();
        return -1;
    }

    uint8_t token = 0xffu;
    for (uint32_t poll = 0; poll < SD_TOKEN_POLLS; ++poll) {
        token = spi_transfer(0xffu);
        if (token != 0xffu) break;
    }
    if (token != 0xfeu) {
        sd_end_command();
        return -1;
    }
    for (uint32_t index = 0; index < FAT_BOOT_SECTOR_SIZE; ++index)
        data[index] = spi_transfer(0xffu);
    (void)spi_transfer(0xffu);
    (void)spi_transfer(0xffu);
    sd_end_command();
    return 0;
}
#endif

static int run_sdram_bist(void)
{
    if ((VESTA->SYS_STATUS & SYS_SDRAM_PRESENT) == 0u) return 0;
    for (uint32_t poll = 0; poll < SDRAM_READY_POLLS; ++poll)
        if ((VESTA->SYS_STATUS & SYS_SDRAM_READY) != 0u) goto ready;
    return 0;

ready:
    uint32_t status = VESTA->MEMTEST_STATUS;
    if ((status & MEMTEST_DONE) == 0u) VESTA->MEMTEST_CTRL = MEMTEST_START;
    for (uint32_t poll = 0; poll < BIST_POLLS; ++poll) {
        status = VESTA->MEMTEST_STATUS;
        if ((status & MEMTEST_DONE) != 0u && (status & MEMTEST_BUSY) == 0u)
            return VESTA->MEMTEST_ERRORS == 0u;
    }
    return 0;
}

#ifdef ASTRA_HOST_BOOT
static int astra_host_load(uint32_t *initial_sp, uint32_t *initial_pc,
                           uint32_t *payload_crc)
{
    if ((VESTA->SYS_STATUS & SYS_ASTRA_HOST) == 0u) return 0;
    VESTA->SYS_CTRL = SYS_HOST_BOOT_REQUEST;

    for (uint32_t poll = 0; poll < HOST_BOOT_POLLS; ++poll) {
        uint32_t status = VESTA->HOST_STATUS;
        if ((status & HOST_BOOT_ERROR) != 0u) return 0;
        if ((status & HOST_BOOT_DONE) != 0u) {
            uint32_t size = VESTA->HOST_ROM_SIZE;
            uint32_t received = VESTA->HOST_BYTES_RECEIVED;
            uint32_t sp = VESTA->HOST_INITIAL_SP;
            uint32_t pc = VESTA->HOST_INITIAL_PC;
            if (size < 8u || size > 0x00040000u || received != size ||
                sp <= 0x01ff8000u || sp > 0x02000000u ||
                pc < 0xffe00000u || pc >= 0xffe40000u)
                return 0;
            *initial_sp = sp;
            *initial_pc = pc;
            *payload_crc = VESTA->HOST_ROM_CRC32;
            return 1;
        }
    }
    return 0;
}
#endif

void stage0_main(void)
{
    screen_init();
    puts("ASTRA 68 STAGE 0 v" STAGE0_VERSION "\n\n");
    if (VESTA->ID != VESTA_ID_MAGIC)
        halt(FAT_BOOT_ERR_FILESYSTEM);

    puts("SDRAM full BIST ... ");
    if (!run_sdram_bist()) halt(FAT_BOOT_ERR_IO);
    puts("OK\n");

#ifdef ASTRA_HOST_BOOT
    uint32_t initial_sp;
    uint32_t initial_pc;
    uint32_t payload_crc;
    puts("AstraHost ROM ..... ");
    if (!astra_host_load(&initial_sp, &initial_pc, &payload_crc)) halt(9);
    puts("OK\nROM CRC32 ........ 0x");
    put_hex(payload_crc);
#else
    if ((VESTA->SYS_STATUS & SYS_SD_CONTROLLER) == 0u)
        halt(FAT_BOOT_ERR_FILESYSTEM);
    puts("SD card init ...... ");
    if (!sd_initialize()) halt(FAT_BOOT_ERR_IO);
    puts("OK\n");

    puts("FAT /ASTRA68.ROM .. ");
    FatBootIo io = {sd_read_sector, 0};
    FatBootResult result;
    int status = fat_boot_load(&io, sector, STAGE2_LOAD_ADDRESS, &result);
    if (status != FAT_BOOT_OK) halt(status);
    puts("OK\nROM CRC32 ........ 0x");
    put_hex(result.payload_crc32);
#endif
    puts("\nStarting system ROM\n");

    VESTA->SYS_CTRL = SYS_BOOT_SDRAM;
    while ((VESTA->SYS_STATUS & SYS_BOOT_OVERLAY) != 0u) {}
#ifdef ASTRA_HOST_BOOT
    stage0_handoff(initial_sp, initial_pc);
#else
    stage0_handoff(result.initial_sp, result.initial_pc);
#endif
}
