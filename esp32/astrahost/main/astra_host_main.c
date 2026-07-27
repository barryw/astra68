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
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "astra_block_policy.h"
#include "astra_host_protocol.h"
#include "astra_input.h"
#include "astra_partition.h"
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
#define ASTRA_RUNTIME_RETRIES 3u
#define ASTRA_LINK_RETRY_MS 100
#define ASTRA_SD_RETRY_MS 1000
#define ASTRA_SERVICE_IDLE_MS 1

#define ASTRA_SPI_FRAME_MAX 512u
#define ASTRA_POLL_RESPONSE_BYTES 30u
#define ASTRA_FETCH_RESPONSE_BYTES (15u + ASTRA_BLOCK_CHUNK_BYTES)

static const char *TAG = "AstraHost";
static spi_device_handle_t fpga_device;
static sdmmc_card_t *sd_card;
static DMA_ATTR uint8_t spi_tx[ASTRA_SPI_FRAME_MAX];
static DMA_ATTR uint8_t spi_rx[ASTRA_SPI_FRAME_MAX];
static DMA_ATTR uint8_t runtime_payload[ASTRA_RUNTIME_MAX_PAYLOAD];
static DMA_ATTR uint8_t block_buffer[
    ASTRA_BLOCK_MAX_SECTORS * ASTRA_BLOCK_SECTOR_BYTES];
static DMA_ATTR uint8_t partition_sector[ASTRA_SECTOR_BYTES];
static astra_partition_t astra_partition;
static uint32_t host_generation;
static uint32_t media_generation = 1;
static uint32_t media_flags = ASTRA_STATE_LINK_UP;
static uint8_t fpga_capabilities;

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

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be64(uint8_t *bytes, uint64_t value)
{
    write_be32(bytes, (uint32_t)(value >> 32));
    write_be32(bytes + 4, (uint32_t)value);
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           bytes[3];
}

static uint64_t read_be64(const uint8_t *bytes)
{
    return ((uint64_t)read_be32(bytes) << 32) | read_be32(bytes + 4);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
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

static esp_err_t fpga_send_runtime_command(uint8_t command,
                                           const uint8_t *payload,
                                           size_t payload_size)
{
    if (payload_size > ASTRA_RUNTIME_MAX_PAYLOAD ||
        payload_size + 4 > sizeof(spi_tx))
        return ESP_ERR_INVALID_SIZE;
    spi_tx[0] = ASTRA_SPI_WRITE_OP;
    spi_tx[1] = command;
    write_be16(spi_tx + 2, (uint16_t)payload_size);
    if (payload_size != 0)
        memcpy(spi_tx + 4, payload, payload_size);
    return fpga_transfer(spi_tx, payload_size + 4);
}

static esp_err_t fpga_read_available(uint8_t *data, size_t capacity,
                                     size_t *received)
{
    memset(spi_tx, 0, sizeof(spi_tx));
    memset(spi_rx, 0, sizeof(spi_rx));
    spi_tx[0] = ASTRA_SPI_READ_OP;
    spi_transaction_t transaction = {
        .length = sizeof(spi_tx) * 8,
        .tx_buffer = spi_tx,
        .rx_buffer = spi_rx,
    };
    esp_err_t error = spi_device_polling_transmit(fpga_device, &transaction);
    if (error != ESP_OK)
        return error;

    *received = 0;
    size_t index = 1;
    while (index < sizeof(spi_rx)) {
        uint8_t token = spi_rx[index++];
        if (token == ASTRA_SPI_TOKEN_EMPTY)
            continue;
        if (token != ASTRA_SPI_TOKEN_DATA)
            return ESP_ERR_INVALID_RESPONSE;
        if (index == sizeof(spi_rx))
            break;
        if (data != NULL) {
            if (*received == capacity)
                return ESP_ERR_INVALID_SIZE;
            data[*received] = spi_rx[index];
        }
        ++*received;
        ++index;
    }
    return ESP_OK;
}

static esp_err_t fpga_read_bytes(uint8_t *data, size_t size)
{
    int64_t deadline = esp_timer_get_time() + ASTRA_RESPONSE_TIMEOUT_US;
    size_t received = 0;
    while (received != size) {
        size_t batch = 0;
        esp_err_t error = fpga_read_available(
            data + received, size - received, &batch);
        if (error != ESP_OK)
            return error;
        received += batch;
        if (esp_timer_get_time() >= deadline)
            return ESP_ERR_TIMEOUT;
        if (batch == 0)
            taskYIELD();
    }
    return ESP_OK;
}

static esp_err_t fpga_drain_responses(void)
{
    int64_t deadline = esp_timer_get_time() + ASTRA_RESPONSE_TIMEOUT_US;
    unsigned quiet_passes = 0;

    while (esp_timer_get_time() < deadline) {
        size_t received = 0;
        esp_err_t error = fpga_read_available(NULL, 0, &received);
        if (error != ESP_OK)
            return error;
        if (received != 0) {
            quiet_passes = 0;
            continue;
        }
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

    fpga_capabilities = response[8];

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

static bool read_partition_sector(void *context, uint64_t lba,
                                  uint8_t sector[ASTRA_SECTOR_BYTES])
{
    (void)context;
    if (lba > UINT32_MAX)
        return false;
    esp_err_t error = sdmmc_read_sectors(
        sd_card, partition_sector, (uint32_t)lba, 1);
    if (error != ESP_OK)
        return false;
    memcpy(sector, partition_sector, ASTRA_SECTOR_BYTES);
    return true;
}

static void discover_astra_partition(void)
{
    media_flags = ASTRA_STATE_LINK_UP;
    astra_partition.first_lba = 0;
    astra_partition.sector_count = 0;
    if (sd_card->csd.sector_size != ASTRA_BLOCK_SECTOR_BYTES) {
        ESP_LOGE(TAG, "unsupported SD sector size: %lu",
                 (unsigned long)sd_card->csd.sector_size);
        return;
    }

    astra_partition_result_t result = astra_partition_find(
        read_partition_sector, NULL, sd_card->csd.capacity,
        &astra_partition);
    if (result != ASTRA_PARTITION_OK) {
        ESP_LOGW(TAG, "native Astra volume unavailable: %s",
                 astra_partition_result_string(result));
        return;
    }
    if (!astra_partition_u32_addressable(&astra_partition)) {
        ESP_LOGE(TAG, "native Astra partition exceeds SD backend LBA range");
        astra_partition.first_lba = 0;
        astra_partition.sector_count = 0;
        return;
    }

    media_flags |= ASTRA_STATE_MEDIA_PRESENT | ASTRA_STATE_WRITE_ENABLE;
    ESP_LOGI(TAG,
             "native Astra partition: LBA %llu, %llu sectors (%llu MiB)",
             (unsigned long long)astra_partition.first_lba,
             (unsigned long long)astra_partition.sector_count,
             (unsigned long long)(astra_partition.sector_count / 2048));
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

static esp_err_t fpga_runtime_status(uint8_t command,
                                     const uint8_t *payload,
                                     size_t payload_size,
                                     const char *operation)
{
    esp_err_t error = fpga_send_runtime_command(
        command, payload, payload_size);
    if (error != ESP_OK)
        return error;
    return fpga_expect_status(operation);
}

static esp_err_t fpga_runtime_status_retry(uint8_t command,
                                           const uint8_t *payload,
                                           size_t payload_size,
                                           const char *operation)
{
    esp_err_t last_error = ESP_ERR_TIMEOUT;

    for (unsigned attempt = 0; attempt < ASTRA_RUNTIME_RETRIES; ++attempt) {
        if (attempt != 0) {
            last_error = fpga_drain_responses();
            if (last_error != ESP_OK)
                return last_error;
        }

        last_error = fpga_runtime_status(command, payload, payload_size,
                                         operation);
        if (last_error == ESP_OK)
            return ESP_OK;
        ESP_LOGW(TAG, "%s retry %u/%u: %s", operation, attempt + 1,
                 ASTRA_RUNTIME_RETRIES, esp_err_to_name(last_error));
    }

    return last_error;
}

static esp_err_t fpga_service_hello(void)
{
    write_be32(runtime_payload, host_generation);
    write_be32(runtime_payload + 4, media_generation);
    write_be32(runtime_payload + 8, media_flags);
    write_be64(runtime_payload + 12, astra_partition.sector_count);
    write_be16(runtime_payload + 20, ASTRA_BLOCK_MAX_SECTORS);
    return fpga_runtime_status(ASTRA_CMD_SERVICE_HELLO,
                               runtime_payload, 22, "SERVICE_HELLO");
}

static esp_err_t fpga_poll_request(astra_block_request_t *request)
{
    uint8_t response[ASTRA_POLL_RESPONSE_BYTES];
    esp_err_t error = fpga_send_runtime_command(
        ASTRA_CMD_BLOCK_POLL, NULL, 0);
    if (error != ESP_OK)
        return error;
    error = fpga_read_bytes(response, sizeof(response));
    if (error != ESP_OK)
        return error;
    if (response[0] != ASTRA_STATUS_OK)
        return ESP_ERR_INVALID_RESPONSE;

    request->valid = (response[1] & 1u) != 0;
    request->id = read_be32(response + 2);
    request->operation = response[6];
    request->flags = response[7];
    request->sectors = read_be16(response + 8);
    request->lba = read_be64(response + 10);
    request->buffer = read_be32(response + 18);
    request->media_generation = read_be32(response + 22);
    request->host_generation = read_be32(response + 26);
    return ESP_OK;
}

static esp_err_t fpga_push_chunk(const astra_block_request_t *request,
                                 uint32_t offset, const uint8_t *data)
{
    write_be32(runtime_payload, request->id);
    write_be32(runtime_payload + 4, offset);
    write_be16(runtime_payload + 8, ASTRA_BLOCK_CHUNK_BYTES);
    write_be32(runtime_payload + 10,
               crc32_bytes(data, ASTRA_BLOCK_CHUNK_BYTES));
    memcpy(runtime_payload + 14, data, ASTRA_BLOCK_CHUNK_BYTES);

    for (unsigned attempt = 0; attempt < ASTRA_RUNTIME_RETRIES; ++attempt) {
        if (attempt != 0) {
            esp_err_t drain_error = fpga_drain_responses();
            if (drain_error != ESP_OK)
                return drain_error;
        }
        esp_err_t error = fpga_runtime_status(
            ASTRA_CMD_BLOCK_PUSH, runtime_payload,
            14 + ASTRA_BLOCK_CHUNK_BYTES, "BLOCK_PUSH");
        if (error == ESP_OK)
            return ESP_OK;
        ESP_LOGW(TAG, "BLOCK_PUSH retry %u: %s", attempt + 1,
                 esp_err_to_name(error));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t fpga_fetch_chunk(const astra_block_request_t *request,
                                  uint32_t offset, uint8_t *data)
{
    write_be32(runtime_payload, request->id);
    write_be32(runtime_payload + 4, offset);
    write_be16(runtime_payload + 8, ASTRA_BLOCK_CHUNK_BYTES);

    uint8_t response[ASTRA_FETCH_RESPONSE_BYTES];
    for (unsigned attempt = 0; attempt < ASTRA_RUNTIME_RETRIES; ++attempt) {
        if (attempt != 0) {
            esp_err_t drain_error = fpga_drain_responses();
            if (drain_error != ESP_OK)
                return drain_error;
        }
        esp_err_t error = fpga_send_runtime_command(
            ASTRA_CMD_BLOCK_FETCH, runtime_payload, 10);
        if (error == ESP_OK)
            error = fpga_read_bytes(response, sizeof(response));
        if (error == ESP_OK && response[0] == ASTRA_STATUS_OK &&
            read_be32(response + 1) == request->id &&
            read_be32(response + 5) == offset &&
            read_be16(response + 9) == ASTRA_BLOCK_CHUNK_BYTES &&
            read_be32(response + 11 + ASTRA_BLOCK_CHUNK_BYTES) ==
                crc32_bytes(response + 11, ASTRA_BLOCK_CHUNK_BYTES)) {
            memcpy(data, response + 11, ASTRA_BLOCK_CHUNK_BYTES);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "BLOCK_FETCH retry %u: %s", attempt + 1,
                 error == ESP_OK ? "invalid response" :
                                   esp_err_to_name(error));
    }
    return ESP_ERR_INVALID_CRC;
}

static esp_err_t fpga_complete_request(
    const astra_block_request_t *request, uint16_t status,
    uint16_t sectors, uint32_t detail)
{
    write_be32(runtime_payload, request->id);
    write_be16(runtime_payload + 4, status);
    write_be16(runtime_payload + 6, sectors);
    write_be32(runtime_payload + 8, detail);
    return fpga_runtime_status_retry(ASTRA_CMD_BLOCK_COMPLETE,
                                     runtime_payload, 12,
                                     "BLOCK_COMPLETE");
}

static esp_err_t service_block_request(
    const astra_block_request_t *request)
{
    uint32_t absolute_lba = 0;
    astra_block_policy_result_t policy = astra_block_policy_classify(
        request, host_generation, media_generation, media_flags,
        &astra_partition, &absolute_lba);

    if (policy == ASTRA_BLOCK_POLICY_INVALID)
        return fpga_complete_request(
            request, ASTRA_COMPLETION_BAD_REQUEST, 0, 0);
    if (policy == ASTRA_BLOCK_POLICY_FLUSH)
        return fpga_complete_request(request, ASTRA_COMPLETION_OK, 0, 0);

    size_t bytes = request->sectors * ASTRA_BLOCK_SECTOR_BYTES;
    esp_err_t error;
    if (request->operation == ASTRA_BLOCK_READ) {
        error = sdmmc_read_sectors(sd_card, block_buffer, absolute_lba,
                                   request->sectors);
        if (error == ESP_OK) {
            for (size_t offset = 0; offset < bytes;
                 offset += ASTRA_BLOCK_CHUNK_BYTES) {
                error = fpga_push_chunk(
                    request, (uint32_t)offset, block_buffer + offset);
                if (error != ESP_OK)
                    return error;
            }
        }
    } else {
        error = ESP_OK;
        for (size_t offset = 0; offset < bytes;
             offset += ASTRA_BLOCK_CHUNK_BYTES) {
            error = fpga_fetch_chunk(
                request, (uint32_t)offset, block_buffer + offset);
            if (error != ESP_OK)
                return error;
        }
        error = sdmmc_write_sectors(sd_card, block_buffer, absolute_lba,
                                    request->sectors);
    }

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SD request %08lx failed: %s",
                 (unsigned long)request->id, esp_err_to_name(error));
        esp_err_t completion_error = fpga_complete_request(
            request, ASTRA_COMPLETION_IO_ERROR, 0, (uint32_t)error);
        if (completion_error != ESP_OK)
            return completion_error;
        ++media_generation;
        if (media_generation == 0)
            ++media_generation;
        media_flags = ASTRA_STATE_LINK_UP;
        astra_partition.first_lba = 0;
        astra_partition.sector_count = 0;
        return fpga_service_hello();
    }

    return fpga_complete_request(request, ASTRA_COMPLETION_OK,
                                 request->sectors, 0);
}

static esp_err_t service_input_event(const astra_input_event_t *event)
{
    write_be32(runtime_payload, host_generation);
    write_be32(runtime_payload + 4, event->header);
    write_be32(runtime_payload + 8, event->value);
    write_be32(runtime_payload + 12, event->timestamp_ms);
    write_be32(runtime_payload + 16, event->device_sequence);
    return fpga_runtime_status_retry(ASTRA_CMD_INPUT_EVENT,
                                     runtime_payload, 20,
                                     "INPUT_EVENT");
}

static esp_err_t run_runtime_service(void)
{
    const uint8_t required = ASTRA_CAP_RAW_BLOCK | ASTRA_CAP_INPUT_EVENTS;
    if ((fpga_capabilities & required) != required)
        return ESP_ERR_NOT_SUPPORTED;

    // A new link session gets a new generation. If a prior session died after
    // partially transferring a request, HELLO completes that request as stale
    // instead of trying to restart it without a resumable byte offset.
    if (++host_generation == 0)
        ++host_generation;
    esp_err_t error = fpga_service_hello();
    if (error != ESP_OK)
        return error;
    astra_input_event_t link_event = {
        .header = ((uint32_t)ASTRA_INPUT_SYSTEM << 24) |
                  ((uint32_t)ASTRA_INPUT_SYSTEM_LINK_KIND << 16) |
                  ASTRA_INPUT_SYSTEM_LINK_FLAGS,
        .value = ASTRA_INPUT_SYSTEM_LINK_VALUE,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .device_sequence =
            ((uint32_t)ASTRA_INPUT_SYSTEM_DEVICE_ID << 16) |
            ((uint16_t)host_generation != 0u ?
                 (uint16_t)host_generation : 1u),
    };
    error = service_input_event(&link_event);
    if (error != ESP_OK)
        return error;

    for (;;) {
        astra_input_event_t event;
        if (astra_input_receive(&event, 0)) {
            error = service_input_event(&event);
            if (error != ESP_OK)
                return error;
        }

        astra_block_request_t request;
        error = fpga_poll_request(&request);
        if (error != ESP_OK)
            return error;
        if (request.valid) {
            error = service_block_request(&request);
            if (error != ESP_OK)
                return error;
        } else {
            vTaskDelay(pdMS_TO_TICKS(ASTRA_SERVICE_IDLE_MS));
        }

    }
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

static esp_err_t wait_for_boot_state(bool *boot_requested)
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
            *boot_requested = false;
            return ESP_OK;
        }
        if ((flags & ASTRA_BOOT_REQUESTED) != 0) {
            *boot_requested = true;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "AstraHost 0.1 starting");
    ESP_LOGI(TAG, "FPGA communication: SPI only, mode 0 at %d Hz",
             ASTRA_FPGA_SPI_HZ);

    host_generation = esp_random();
    if (host_generation == 0)
        host_generation = 1;
    ESP_ERROR_CHECK(astra_input_initialize() ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(initialize_shared_spi());

    esp_err_t error;
    while ((error = mount_boot_partition()) != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed without formatting: %s",
                 esp_err_to_name(error));
        vTaskDelay(pdMS_TO_TICKS(ASTRA_SD_RETRY_MS));
    }
    ESP_LOGI(TAG, "SD boot partition mounted");
    sdmmc_card_print_info(stdout, sd_card);
    discover_astra_partition();

#ifdef ASTRA_PROVISION_ROM
    ESP_ERROR_CHECK(provision_boot_rom());
#endif

    ESP_ERROR_CHECK(add_fpga_device());
    for (;;) {
        ESP_ERROR_CHECK(wait_for_fpga());

        bool boot_requested = false;
        error = wait_for_boot_state(&boot_requested);
        if (error == ESP_OK && boot_requested) {
            astra_rom_info_t rom;
            FILE *file = open_valid_rom(&rom);
            if (file == NULL) {
                vTaskDelay(pdMS_TO_TICKS(ASTRA_SD_RETRY_MS));
                continue;
            }
            error = fpga_stream_rom(file, &rom);
            fclose(file);
            if (error == ESP_OK)
                ESP_LOGI(TAG, "Astra boot handoff complete");
        }

        if (error != ESP_OK) {
            ESP_LOGE(TAG, "boot attempt failed: %s", esp_err_to_name(error));
            esp_err_t abort_error = fpga_abort_boot();
            if (abort_error != ESP_OK)
                ESP_LOGW(TAG, "BOOT_ABORT failed: %s",
                         esp_err_to_name(abort_error));
            vTaskDelay(pdMS_TO_TICKS(ASTRA_LINK_RETRY_MS));
            continue;
        }

        ESP_LOGI(TAG, "entering Astra runtime storage/input service");
        error = run_runtime_service();
        ESP_LOGW(TAG, "runtime service link reset: %s",
                 esp_err_to_name(error));
        vTaskDelay(pdMS_TO_TICKS(ASTRA_LINK_RETRY_MS));
    }
}
