#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <astra/front_panel.h>

#include "memory_map.h"

static uint32_t panel_id;
static uint32_t panel_version;
static uint32_t panel_caps;
static uint32_t panel_input;
static uint32_t panel_raw_input;
static uint32_t panel_change;
static uint32_t panel_led_data;
static uint32_t panel_led_ownership;
static uint32_t last_write_address;
static uint32_t last_write_value;
static unsigned write_count;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void reset_fake_panel(void)
{
    panel_id = ASTRA_HW_PANEL_ID_MAGIC;
    panel_version = ASTRA_HW_PANEL_VERSION_1_0;
    panel_caps = UINT32_C(0x0F040608);
    panel_input = UINT32_C(0x00000A35);
    panel_raw_input = UINT32_C(0x0000052A);
    panel_change = UINT32_C(0x00000921);
    panel_led_data = UINT32_C(0x5A);
    panel_led_ownership = 0;
    last_write_address = 0;
    last_write_value = 0;
    write_count = 0;
}

uint32_t astra_ndk_test_mmio_read32(uint32_t address)
{
    switch (address - ASTRA_HW_FRONT_PANEL_BASE) {
    case ASTRA_HW_PANEL_ID: return panel_id;
    case ASTRA_HW_PANEL_VERSION: return panel_version;
    case ASTRA_HW_PANEL_CAPS: return panel_caps;
    case ASTRA_HW_PANEL_INPUT: return panel_input;
    case ASTRA_HW_PANEL_RAW_INPUT: return panel_raw_input;
    case ASTRA_HW_PANEL_CHANGE: return panel_change;
    case ASTRA_HW_PANEL_LED_DATA: return panel_led_data;
    case ASTRA_HW_PANEL_LED_OWNERSHIP: return panel_led_ownership;
    default: return 0;
    }
}

void astra_ndk_test_mmio_write32(uint32_t address, uint32_t value)
{
    uint32_t offset = address - ASTRA_HW_FRONT_PANEL_BASE;

    last_write_address = address;
    last_write_value = value;
    write_count++;
    switch (offset) {
    case ASTRA_HW_PANEL_CHANGE:
        panel_change &= ~value;
        break;
    case ASTRA_HW_PANEL_LED_DATA:
        panel_led_data = value & 0xffu;
        break;
    case ASTRA_HW_PANEL_LED_OWNERSHIP:
        panel_led_ownership = value & 0xffu;
        break;
    case ASTRA_HW_PANEL_LED_SET:
        panel_led_data |= value & 0xffu;
        break;
    case ASTRA_HW_PANEL_LED_CLEAR:
        panel_led_data &= ~(value & 0xffu);
        break;
    case ASTRA_HW_PANEL_LED_TOGGLE:
        panel_led_data ^= value & 0xffu;
        break;
    default:
        break;
    }
}

static void test_discovery(void)
{
    AstraFrontPanelInfo info;

    reset_fake_panel();
    CHECK(astra_front_panel_present());
    CHECK(astra_front_panel_get_info(&info) == ASTRA_OK);
    CHECK(info.led_count == 8);
    CHECK(info.button_count == 6);
    CHECK(info.switch_count == 4);
    CHECK(info.features == 0x0f);
    CHECK(info.hardware_version == ASTRA_HW_PANEL_VERSION_1_0);
    CHECK(info.reserved[0] == 0 && info.reserved[1] == 0 && info.reserved[2] == 0);
    CHECK(astra_front_panel_get_info(0) == ASTRA_ERROR_INVALID_ARGUMENT);

    panel_id = 0;
    CHECK(!astra_front_panel_present());
    CHECK(astra_front_panel_get_info(&info) == ASTRA_ERROR_NOT_PRESENT);

    panel_id = ASTRA_HW_PANEL_ID_MAGIC;
    panel_version = UINT32_C(0x00020000);
    CHECK(!astra_front_panel_present());
}

static void test_inputs(void)
{
    AstraFrontPanelState state;
    uint16_t changes;

    reset_fake_panel();
    CHECK(astra_front_panel_read(&state) == ASTRA_OK);
    CHECK(state.buttons == 0x35);
    CHECK(state.switches == 0x0a);
    CHECK(state.raw_buttons == 0x2a);
    CHECK(state.raw_switches == 0x05);
    CHECK(astra_front_panel_read(0) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_front_panel_get_changes(&changes) == ASTRA_OK);
    CHECK(changes == 0x0921);
    CHECK(astra_front_panel_get_changes(0) == ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_front_panel_clear_changes(0xffff) == ASTRA_OK);
    CHECK(last_write_address == ASTRA_HW_FRONT_PANEL_BASE + ASTRA_HW_PANEL_CHANGE);
    CHECK(last_write_value == ASTRA_PANEL_CHANGE_ALL);
    CHECK(panel_change == 0);
}

static AstraResult exercise_auto_cleanup(void)
{
    ASTRA_AUTO_FRONT_PANEL_LED_LEASE(lease);

    return astra_front_panel_acquire_leds(0x03, 0, &lease);
}

static void test_leds(void)
{
    AstraFrontPanelLedLease low = ASTRA_FRONT_PANEL_LED_LEASE_INIT;
    AstraFrontPanelLedLease high = ASTRA_FRONT_PANEL_LED_LEASE_INIT;
    AstraFrontPanelLedLease overlap = ASTRA_FRONT_PANEL_LED_LEASE_INIT;
    AstraFrontPanelLedLease stale;
    AstraAcquireOptions shared = ASTRA_ACQUIRE_OPTIONS_INIT;
    uint8_t value;

    reset_fake_panel();
    CHECK(astra_front_panel_get_leds(&value) == ASTRA_OK);
    CHECK(value == 0x5a);
    CHECK(astra_front_panel_get_leds(0) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_front_panel_acquire_leds(0, 0, &low) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    shared.flags = ASTRA_ACQUIRE_SHARED;
    CHECK(astra_front_panel_acquire_leds(0x01, &shared, &low) ==
          ASTRA_ERROR_UNSUPPORTED);

    CHECK(astra_front_panel_acquire_leds(0x0f, 0, &low) == ASTRA_OK);
    CHECK(panel_led_ownership == 0x0f);
    CHECK(astra_front_panel_acquire_leds(0x01, 0, &overlap) ==
          ASTRA_ERROR_BUSY);
    CHECK(astra_front_panel_acquire_leds(0xf0, 0, &high) == ASTRA_OK);
    CHECK(panel_led_ownership == 0xff);

    CHECK(astra_front_panel_set_leds(&low, 0x05) == ASTRA_OK);
    CHECK(panel_led_data == 0x55);
    CHECK(astra_front_panel_set_leds(&high, 0xa0) == ASTRA_OK);
    CHECK(panel_led_data == 0xa5);
    CHECK(astra_front_panel_set_led_bits(&low, 0x0a) == ASTRA_OK);
    CHECK(panel_led_data == 0xaf);
    CHECK(astra_front_panel_clear_led_bits(&low, 0x03) == ASTRA_OK);
    CHECK(panel_led_data == 0xac);
    CHECK(astra_front_panel_toggle_led_bits(&low, 0x0c) == ASTRA_OK);
    CHECK(panel_led_data == 0xa0);

    stale = low;
    CHECK(astra_front_panel_release_leds(&low) == ASTRA_OK);
    CHECK(panel_led_ownership == 0xf0);
    CHECK(astra_front_panel_set_leds(&stale, 0) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_front_panel_release_leds(&high) == ASTRA_OK);
    CHECK(panel_led_ownership == 0);
    CHECK(astra_front_panel_release_leds(&high) == ASTRA_ERROR_INVALID_HANDLE);

    CHECK(exercise_auto_cleanup() == ASTRA_OK);
    CHECK(panel_led_ownership == 0);
}

int main(void)
{
    test_discovery();
    test_inputs();
    test_leds();
    puts("PASS Astra NDK front-panel API");
    return 0;
}
