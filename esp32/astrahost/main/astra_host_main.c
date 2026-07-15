#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "astra_host_protocol.h"
#include "astra_rom.h"

#define ASTRA_MOUNT_POINT "/sdcard"
#define ASTRA_ROM_PATH ASTRA_MOUNT_POINT "/ASTRA68.ROM"
#define ASTRA_ROM_STAGING_PATH ASTRA_MOUNT_POINT "/ASTRA68.NEW"
#define ASTRA_ROM_BACKUP_PATH ASTRA_MOUNT_POINT "/ASTRA68.OLD"

#define ASTRA_SPI_HOST SPI2_HOST
#define ASTRA_PIN_SCK GPIO_NUM_14
#define ASTRA_PIN_MISO GPIO_NUM_2
#define ASTRA_PIN_MOSI GPIO_NUM_15
#define ASTRA_PIN_SD_CS GPIO_NUM_13
#define ASTRA_PIN_FPGA_CS GPIO_NUM_4
#ifndef ASTRA_FPGA_SPI_HZ
#define ASTRA_FPGA_SPI_HZ 20000000
#endif
#define ASTRA_RESPONSE_TIMEOUT_US 250000
#define ASTRA_DRAIN_QUIET_PASSES 2
#define ASTRA_LINK_RETRY_MS 100
#define ASTRA_SD_RETRY_MS 1000

#define ASTRA_SPI_FRAME_MAX (3u + ASTRA_BOOT_CHUNK_BYTES)

static const char *TAG = "AstraHost";
static spi_device_handle_t fpga_device;
static sdmmc_card_t *sd_card;
static DMA_ATTR uint8_t spi_tx[ASTRA_SPI_FRAME_MAX];

#ifdef ASTRA_PROVISION_ROM
extern const uint8_t astra_provision_start[]
    asm("_binary_astra68_provision_rom_start");
extern const uint8_t astra_provision_end[]
    asm("_binary_astra68_provision_rom_end");
#endif

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static esp_err_t fpga_transfer(const uint8_t *data, size_t size)
{
    spi_transaction_t transaction = {
        .length = size * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(fpga_device, &transaction);
}

static esp_err_t fpga_send_command(uint8_t command, const uint8_t *payload,
                                   size_t payload_size)
{
    if (payload_size + 2 > sizeof(spi_tx))
        return ESP_ERR_INVALID_SIZE;
    spi_tx[0] = ASTRA_SPI_WRITE_OP;
    spi_tx[1] = command;
    if (payload_size != 0)
        memcpy(spi_tx + 2, payload, payload_size);
    return fpga_transfer(spi_tx, payload_size + 2);
}

static esp_err_t fpga_try_read_byte(uint8_t *value)
{
    spi_transaction_t transaction = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 24,
    };
    transaction.tx_data[0] = ASTRA_SPI_READ_OP;
    transaction.tx_data[1] = 0;
    transaction.tx_data[2] = 0;

    esp_err_t error = spi_device_polling_transmit(fpga_device, &transaction);
    if (error != ESP_OK)
        return error;
    if (transaction.rx_data[1] != ASTRA_SPI_TOKEN_DATA)
        return ESP_ERR_NOT_FOUND;
    *value = transaction.rx_data[2];
    return ESP_OK;
}

static esp_err_t fpga_read_bytes(uint8_t *data, size_t size)
{
    int64_t deadline = esp_timer_get_time() + ASTRA_RESPONSE_TIMEOUT_US;
    size_t received = 0;
    while (received != size) {
        esp_err_t error = fpga_try_read_byte(data + received);
        if (error == ESP_OK) {
            ++received;
            continue;
        }
        if (error != ESP_ERR_NOT_FOUND)
            return error;
        if (esp_timer_get_time() >= deadline)
            return ESP_ERR_TIMEOUT;
        taskYIELD();
    }
    return ESP_OK;
}

static esp_err_t fpga_drain_responses(void)
{
    int64_t deadline = esp_timer_get_time() + ASTRA_RESPONSE_TIMEOUT_US;
    unsigned quiet_passes = 0;

    while (esp_timer_get_time() < deadline) {
        uint8_t ignored;
        esp_err_t error = fpga_try_read_byte(&ignored);
        if (error == ESP_OK) {
            quiet_passes = 0;
            continue;
        }
        if (error != ESP_ERR_NOT_FOUND)
            return error;
        if (++quiet_passes == ASTRA_DRAIN_QUIET_PASSES)
            return ESP_OK;

        // Let the FPGA finish decoding any bytes already queued by an older
        // SPI client before declaring the response stream quiescent.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t fpga_expect_status(const char *operation)
{
    uint8_t status;
    esp_err_t error = fpga_read_bytes(&status, 1);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "%s response failed: %s", operation,
                 esp_err_to_name(error));
        return error;
    }
    if (status != ASTRA_STATUS_OK) {
        ESP_LOGE(TAG, "%s rejected by FPGA: status 0x%02x", operation,
                 status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t fpga_identify(void)
{
    uint8_t response[15];
    esp_err_t error = fpga_drain_responses();
    if (error != ESP_OK)
        return error;
    error = fpga_send_command(ASTRA_CMD_IDENTIFY, NULL, 0);
    if (error != ESP_OK)
        return error;
    error = fpga_read_bytes(response, sizeof(response));
    if (error != ESP_OK)
        return error;
    if (response[0] != ASTRA_STATUS_OK ||
        memcmp(response + 1, "A68H", 4) != 0 ||
        response[5] != ASTRA_HOST_PROTOCOL_MAJOR ||
        (response[8] & ASTRA_CAP_BOOT_STREAM) == 0)
        return ESP_ERR_INVALID_RESPONSE;

    ESP_LOGI(TAG, "FPGA link: AstraHost protocol %u.%u flags=0x%02x",
             response[5], response[6], response[9]);
    return ESP_OK;
}

static esp_err_t fpga_boot_status(uint8_t *flags, uint8_t *error_code,
                                  uint32_t *bytes_received)
{
    uint8_t response[7];
    esp_err_t error = fpga_send_command(ASTRA_CMD_BOOT_STATUS, NULL, 0);
    if (error != ESP_OK)
        return error;
    error = fpga_read_bytes(response, sizeof(response));
    if (error != ESP_OK)
        return error;
    if (response[0] != ASTRA_STATUS_OK)
        return ESP_ERR_INVALID_RESPONSE;
    *flags = response[1];
    *error_code = response[2];
    *bytes_received = ((uint32_t)response[3] << 24) |
                      ((uint32_t)response[4] << 16) |
                      ((uint32_t)response[5] << 8) |
                      response[6];
    return ESP_OK;
}

static esp_err_t fpga_abort_boot(void)
{
    esp_err_t error = fpga_send_command(ASTRA_CMD_BOOT_ABORT, NULL, 0);
    if (error != ESP_OK)
        return error;
    return fpga_expect_status("BOOT_ABORT");
}

static esp_err_t fpga_stream_rom(FILE *file, const astra_rom_info_t *rom)
{
    uint8_t begin[12];
    write_be32(begin, rom->payload_size);
    write_be32(begin + 4, rom->payload_crc32);
    write_be32(begin + 8, ASTRA_BOOT_LOAD_OFFSET);

    esp_err_t error = fpga_send_command(ASTRA_CMD_BOOT_BEGIN,
                                        begin, sizeof(begin));
    if (error != ESP_OK)
        return error;
    error = fpga_expect_status("BOOT_BEGIN");
    if (error != ESP_OK)
        return error;

    if (fseek(file, ASTRA_ROM_HEADER_SIZE, SEEK_SET) != 0)
        return ESP_FAIL;
    uint32_t remaining = rom->payload_size;
    uint32_t sent = 0;
    while (remaining != 0) {
        size_t chunk = remaining < ASTRA_BOOT_CHUNK_BYTES ?
                       remaining : ASTRA_BOOT_CHUNK_BYTES;
        spi_tx[0] = ASTRA_SPI_WRITE_OP;
        spi_tx[1] = ASTRA_CMD_BOOT_DATA;
        spi_tx[2] = chunk == ASTRA_BOOT_CHUNK_BYTES ? 0 : (uint8_t)chunk;
        if (fread(spi_tx + 3, 1, chunk, file) != chunk)
            return ESP_FAIL;

        error = fpga_transfer(spi_tx, chunk + 3);
        if (error != ESP_OK)
            return error;
        error = fpga_expect_status("BOOT_DATA");
        if (error != ESP_OK)
            return error;

        sent += chunk;
        remaining -= chunk;
    }

    error = fpga_send_command(ASTRA_CMD_BOOT_COMMIT, NULL, 0);
    if (error != ESP_OK)
        return error;
    error = fpga_expect_status("BOOT_COMMIT");
    if (error == ESP_OK)
        ESP_LOGI(TAG, "ROM committed over SPI: %lu bytes CRC32=%08lx",
                 (unsigned long)sent, (unsigned long)rom->payload_crc32);
    return error;
}

static esp_err_t initialize_shared_spi(void)
{
    gpio_config_t chip_selects = {
        .pin_bit_mask = (1ULL << ASTRA_PIN_SD_CS) |
                        (1ULL << ASTRA_PIN_FPGA_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&chip_selects));
    ESP_ERROR_CHECK(gpio_set_level(ASTRA_PIN_SD_CS, 1));
    ESP_ERROR_CHECK(gpio_set_level(ASTRA_PIN_FPGA_CS, 1));

    spi_bus_config_t bus = {
        .mosi_io_num = ASTRA_PIN_MOSI,
        .miso_io_num = ASTRA_PIN_MISO,
        .sclk_io_num = ASTRA_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512,
    };
    return spi_bus_initialize(ASTRA_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
}

static esp_err_t mount_boot_partition(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = ASTRA_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = ASTRA_SPI_HOST;
    slot.gpio_cs = ASTRA_PIN_SD_CS;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };
    return esp_vfs_fat_sdspi_mount(ASTRA_MOUNT_POINT, &host, &slot,
                                   &mount, &sd_card);
}

#ifdef ASTRA_PROVISION_ROM
static esp_err_t provision_boot_rom(void)
{
    FILE *existing = fopen(ASTRA_ROM_PATH, "rb");
    if (existing == NULL) {
        FILE *backup = fopen(ASTRA_ROM_BACKUP_PATH, "rb");
        if (backup != NULL) {
            astra_rom_info_t backup_rom;
            char backup_error[96];
            bool backup_valid = astra_rom_validate(
                backup, &backup_rom, backup_error, sizeof(backup_error));
            fclose(backup);
            if (!backup_valid) {
                ESP_LOGE(TAG, "interrupted-update backup is invalid: %s",
                         backup_error);
                return ESP_ERR_INVALID_CRC;
            }
            remove(ASTRA_ROM_STAGING_PATH);
            if (rename(ASTRA_ROM_BACKUP_PATH, ASTRA_ROM_PATH) != 0) {
                ESP_LOGE(TAG, "cannot restore interrupted-update backup");
                return ESP_FAIL;
            }
            ESP_LOGW(TAG, "restored %s after interrupted update",
                     ASTRA_ROM_PATH);
            existing = fopen(ASTRA_ROM_PATH, "rb");
            if (existing == NULL)
                return ESP_FAIL;
        }
    }

    bool existing_present = existing != NULL;
    astra_rom_info_t existing_rom = {0};
    if (existing != NULL) {
        char existing_error[96];
        bool existing_valid = astra_rom_validate(
            existing, &existing_rom, existing_error, sizeof(existing_error));
        fclose(existing);
        if (!existing_valid) {
            ESP_LOGE(TAG, "%s exists but is invalid: %s",
                     ASTRA_ROM_PATH, existing_error);
            return ESP_ERR_INVALID_CRC;
        }
#ifndef ASTRA_PROVISION_REPLACE
        ESP_LOGI(TAG, "%s already exists; provisioning skipped",
                 ASTRA_ROM_PATH);
        return ESP_OK;
#endif
    }

    size_t package_size = (size_t)(astra_provision_end -
                                   astra_provision_start);
    if (package_size < ASTRA_ROM_HEADER_SIZE ||
        package_size > ASTRA_ROM_HEADER_SIZE + ASTRA_ROM_MAX_PAYLOAD)
        return ESP_ERR_INVALID_SIZE;

    FILE *file = fopen(ASTRA_ROM_STAGING_PATH, "wb");
    if (file == NULL)
        return ESP_FAIL;
    bool written = fwrite(astra_provision_start, 1, package_size, file) ==
                   package_size;
    bool flushed = written && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0)
        flushed = false;
    if (!flushed) {
        remove(ASTRA_ROM_STAGING_PATH);
        return ESP_FAIL;
    }

    astra_rom_info_t rom;
    char validation_error[96];
    file = fopen(ASTRA_ROM_STAGING_PATH, "rb");
    bool valid = file != NULL &&
                 astra_rom_validate(file, &rom, validation_error,
                                    sizeof(validation_error));
    if (file != NULL)
        fclose(file);
    if (!valid) {
        ESP_LOGE(TAG, "embedded ROM validation failed: %s",
                 file == NULL ? "cannot reopen staging file" :
                                validation_error);
        remove(ASTRA_ROM_STAGING_PATH);
        return ESP_ERR_INVALID_CRC;
    }

    if (existing_present &&
        existing_rom.payload_size == rom.payload_size &&
        existing_rom.payload_crc32 == rom.payload_crc32) {
        remove(ASTRA_ROM_STAGING_PATH);
        remove(ASTRA_ROM_BACKUP_PATH);
        ESP_LOGI(TAG, "%s already matches embedded ROM",
                 ASTRA_ROM_PATH);
        return ESP_OK;
    }

#ifdef ASTRA_PROVISION_REPLACE
    if (existing_present) {
        remove(ASTRA_ROM_BACKUP_PATH);
        if (rename(ASTRA_ROM_PATH, ASTRA_ROM_BACKUP_PATH) != 0) {
            remove(ASTRA_ROM_STAGING_PATH);
            return ESP_FAIL;
        }
    }
#endif

    if (rename(ASTRA_ROM_STAGING_PATH, ASTRA_ROM_PATH) != 0) {
#ifdef ASTRA_PROVISION_REPLACE
        if (existing_present &&
            rename(ASTRA_ROM_BACKUP_PATH, ASTRA_ROM_PATH) != 0)
            ESP_LOGE(TAG, "ROM update failed and backup restore failed");
#endif
        remove(ASTRA_ROM_STAGING_PATH);
        return ESP_FAIL;
    }
    remove(ASTRA_ROM_BACKUP_PATH);
    ESP_LOGI(TAG, "%s %s: %lu payload bytes CRC32=%08lx",
             existing_present ? "updated" : "provisioned",
             ASTRA_ROM_PATH, (unsigned long)rom.payload_size,
             (unsigned long)rom.payload_crc32);
    return ESP_OK;
}
#endif

static esp_err_t add_fpga_device(void)
{
    spi_device_interface_config_t device = {
        .clock_speed_hz = ASTRA_FPGA_SPI_HZ,
        .mode = 0,
        .spics_io_num = ASTRA_PIN_FPGA_CS,
        .queue_size = 1,
    };
    return spi_bus_add_device(ASTRA_SPI_HOST, &device, &fpga_device);
}

static FILE *open_valid_rom(astra_rom_info_t *rom)
{
    FILE *file = fopen(ASTRA_ROM_PATH, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot open %s: errno=%d", ASTRA_ROM_PATH, errno);
        return NULL;
    }

    char error[96];
    if (!astra_rom_validate(file, rom, error, sizeof(error))) {
        ESP_LOGE(TAG, "%s is invalid: %s", ASTRA_ROM_PATH, error);
        fclose(file);
        return NULL;
    }
    ESP_LOGI(TAG, "ROM ready: %lu bytes CRC32=%08lx SP=%08lx PC=%08lx",
             (unsigned long)rom->payload_size,
             (unsigned long)rom->payload_crc32,
             (unsigned long)rom->initial_sp,
             (unsigned long)rom->initial_pc);
    return file;
}

static esp_err_t wait_for_fpga(void)
{
    for (;;) {
        esp_err_t error = fpga_identify();
        if (error == ESP_OK)
            return ESP_OK;
        ESP_LOGW(TAG, "waiting for FPGA SPI link: %s", esp_err_to_name(error));
        vTaskDelay(pdMS_TO_TICKS(ASTRA_LINK_RETRY_MS));
    }
}

static esp_err_t wait_for_boot_request(void)
{
    for (;;) {
        uint8_t flags;
        uint8_t error_code;
        uint32_t received;
        esp_err_t error = fpga_boot_status(&flags, &error_code, &received);
        if (error != ESP_OK)
            return error;
        if ((flags & ASTRA_BOOT_ERROR) != 0) {
            ESP_LOGE(TAG, "FPGA boot engine error 0x%02x after %lu bytes",
                     error_code, (unsigned long)received);
            return ESP_FAIL;
        }
        if ((flags & ASTRA_BOOT_DONE) != 0) {
            vTaskDelay(pdMS_TO_TICKS(ASTRA_LINK_RETRY_MS));
            continue;
        }
        if ((flags & ASTRA_BOOT_REQUESTED) != 0)
            return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "AstraHost 0.1 starting");
    ESP_LOGI(TAG, "FPGA communication: SPI only, mode 0 at %d Hz",
             ASTRA_FPGA_SPI_HZ);

    ESP_ERROR_CHECK(initialize_shared_spi());

    esp_err_t error;
    while ((error = mount_boot_partition()) != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed without formatting: %s",
                 esp_err_to_name(error));
        vTaskDelay(pdMS_TO_TICKS(ASTRA_SD_RETRY_MS));
    }
    ESP_LOGI(TAG, "SD boot partition mounted");
    sdmmc_card_print_info(stdout, sd_card);

#ifdef ASTRA_PROVISION_ROM
    ESP_ERROR_CHECK(provision_boot_rom());
#endif

    ESP_ERROR_CHECK(add_fpga_device());
    for (;;) {
        ESP_ERROR_CHECK(wait_for_fpga());

        astra_rom_info_t rom;
        FILE *file = open_valid_rom(&rom);
        if (file == NULL) {
            vTaskDelay(pdMS_TO_TICKS(ASTRA_SD_RETRY_MS));
            continue;
        }

        error = wait_for_boot_request();
        if (error == ESP_OK)
            error = fpga_stream_rom(file, &rom);
        fclose(file);

        if (error == ESP_OK) {
            ESP_LOGI(TAG, "Astra boot handoff complete");
            vTaskDelay(pdMS_TO_TICKS(ASTRA_LINK_RETRY_MS));
            continue;
        }

        ESP_LOGE(TAG, "boot attempt failed: %s", esp_err_to_name(error));
        esp_err_t abort_error = fpga_abort_boot();
        if (abort_error != ESP_OK)
            ESP_LOGW(TAG, "BOOT_ABORT failed: %s",
                     esp_err_to_name(abort_error));
        vTaskDelay(pdMS_TO_TICKS(ASTRA_LINK_RETRY_MS));
    }
}
