#include <astra/front_panel.h>

#include "internal/memory_map.h"
#include "internal/mmio.h"

#define PANEL_LEASE_TAG UINT32_C(0x504c0000)
#define PANEL_LEASE_TAG_MASK UINT32_C(0xffff0000)
#define PANEL_LEASE_SLOT_MASK UINT32_C(0x000000ff)
#define PANEL_LEASE_GENERATION_SHIFT 8
#define PANEL_LEASE_SLOT_COUNT 8

typedef struct PanelLeaseSlot {
    uint8_t mask;
    uint8_t generation;
} PanelLeaseSlot;

static PanelLeaseSlot panel_leases[PANEL_LEASE_SLOT_COUNT];
static uint8_t panel_claimed_leds;

static uint32_t panel_read(uint32_t offset)
{
    return astra_mmio_read32(ASTRA_HW_FRONT_PANEL_BASE + offset);
}

static void panel_write(uint32_t offset, uint32_t value)
{
    astra_mmio_write32(ASTRA_HW_FRONT_PANEL_BASE + offset, value);
}

int astra_front_panel_present(void)
{
    uint32_t version;

    if (panel_read(ASTRA_HW_PANEL_ID) != ASTRA_HW_PANEL_ID_MAGIC)
        return 0;

    version = panel_read(ASTRA_HW_PANEL_VERSION);
    return (version >> 16) == (ASTRA_HW_PANEL_VERSION_1_0 >> 16);
}

AstraResult astra_front_panel_get_info(AstraFrontPanelInfo *info)
{
    uint32_t caps;

    if (info == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_front_panel_present())
        return ASTRA_ERROR_NOT_PRESENT;

    caps = panel_read(ASTRA_HW_PANEL_CAPS);
    info->led_count = (uint8_t)caps;
    info->button_count = (uint8_t)(caps >> 8);
    info->switch_count = (uint8_t)(caps >> 16);
    info->features = (uint8_t)(caps >> 24);
    info->hardware_version = panel_read(ASTRA_HW_PANEL_VERSION);
    info->reserved[0] = 0;
    info->reserved[1] = 0;
    info->reserved[2] = 0;
    return ASTRA_OK;
}

AstraResult astra_front_panel_read(AstraFrontPanelState *state)
{
    uint32_t input;
    uint32_t raw_input;

    if (state == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;

    input = panel_read(ASTRA_HW_PANEL_INPUT);
    raw_input = panel_read(ASTRA_HW_PANEL_RAW_INPUT);
    state->buttons = (uint8_t)(input & ASTRA_PANEL_BUTTON_ALL);
    state->switches = (uint8_t)((input >> 8) & ASTRA_PANEL_SWITCH_ALL);
    state->raw_buttons = (uint8_t)(raw_input & ASTRA_PANEL_BUTTON_ALL);
    state->raw_switches = (uint8_t)((raw_input >> 8) & ASTRA_PANEL_SWITCH_ALL);
    return ASTRA_OK;
}

AstraResult astra_front_panel_get_changes(uint16_t *changes)
{
    if (changes == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *changes = (uint16_t)(panel_read(ASTRA_HW_PANEL_CHANGE) &
                          ASTRA_PANEL_CHANGE_ALL);
    return ASTRA_OK;
}

AstraResult astra_front_panel_clear_changes(uint16_t changes)
{
    panel_write(ASTRA_HW_PANEL_CHANGE,
                (uint32_t)changes & ASTRA_PANEL_CHANGE_ALL);
    return ASTRA_OK;
}

static AstraResult panel_lease_mask(const AstraFrontPanelLedLease *lease,
                                    uint8_t *mask)
{
    uint32_t handle;
    uint32_t slot_number;
    PanelLeaseSlot *slot;

    if (lease == 0 || mask == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;

    handle = lease->_private_handle;
    slot_number = handle & PANEL_LEASE_SLOT_MASK;
    if ((handle & PANEL_LEASE_TAG_MASK) != PANEL_LEASE_TAG ||
        slot_number == 0 || slot_number > PANEL_LEASE_SLOT_COUNT)
        return ASTRA_ERROR_INVALID_HANDLE;

    slot = &panel_leases[slot_number - 1u];
    if (slot->mask == 0 ||
        slot->generation != (uint8_t)(handle >> PANEL_LEASE_GENERATION_SHIFT))
        return ASTRA_ERROR_INVALID_HANDLE;

    *mask = slot->mask;
    return ASTRA_OK;
}

AstraResult astra_front_panel_get_leds(uint8_t *value)
{
    if (value == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *value = (uint8_t)panel_read(ASTRA_HW_PANEL_LED_DATA);
    return ASTRA_OK;
}

AstraResult astra_front_panel_acquire_leds(
    uint8_t mask,
    const AstraAcquireOptions *options,
    AstraFrontPanelLedLease *lease)
{
    uint8_t hardware_owned;
    unsigned slot_number;
    PanelLeaseSlot *slot;

    if (mask == 0 || lease == 0 ||
        lease->_private_handle != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (options != 0) {
        if (options->size < sizeof(*options))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if ((options->flags & ASTRA_ACQUIRE_SHARED) != 0)
            return ASTRA_ERROR_UNSUPPORTED;
    }
    if (!astra_front_panel_present())
        return ASTRA_ERROR_NOT_PRESENT;

    hardware_owned = (uint8_t)panel_read(ASTRA_HW_PANEL_LED_OWNERSHIP);
    if (((panel_claimed_leds | hardware_owned) & mask) != 0)
        return ASTRA_ERROR_BUSY;

    for (slot_number = 0; slot_number < PANEL_LEASE_SLOT_COUNT; ++slot_number) {
        if (panel_leases[slot_number].mask == 0)
            break;
    }
    if (slot_number == PANEL_LEASE_SLOT_COUNT)
        return ASTRA_ERROR_NO_RESOURCES;

    slot = &panel_leases[slot_number];
    slot->generation++;
    if (slot->generation == 0)
        slot->generation++;
    slot->mask = mask;
    panel_claimed_leds |= mask;
    panel_write(ASTRA_HW_PANEL_LED_OWNERSHIP, hardware_owned | mask);
    lease->_private_handle = PANEL_LEASE_TAG |
        ((uint32_t)slot->generation << PANEL_LEASE_GENERATION_SHIFT) |
        (uint32_t)(slot_number + 1u);
    return ASTRA_OK;
}

AstraResult astra_front_panel_release_leds(AstraFrontPanelLedLease *lease)
{
    uint8_t mask;
    uint32_t slot_number;
    AstraResult result = panel_lease_mask(lease, &mask);

    if (result != ASTRA_OK)
        return result;

    slot_number = (lease->_private_handle & PANEL_LEASE_SLOT_MASK) - 1u;
    panel_write(ASTRA_HW_PANEL_LED_OWNERSHIP,
                panel_read(ASTRA_HW_PANEL_LED_OWNERSHIP) & ~(uint32_t)mask);
    panel_claimed_leds &= (uint8_t)~mask;
    panel_leases[slot_number].mask = 0;
    lease->_private_handle = ASTRA_INVALID_HANDLE;
    return ASTRA_OK;
}

void astra_front_panel_led_lease_cleanup(AstraFrontPanelLedLease *lease)
{
    if (lease != 0 && lease->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_front_panel_release_leds(lease);
        (void)ignored;
    }
}

AstraResult astra_front_panel_set_leds(const AstraFrontPanelLedLease *lease,
                                       uint8_t value)
{
    uint8_t owned;
    uint8_t current;
    AstraResult result = panel_lease_mask(lease, &owned);

    if (result != ASTRA_OK)
        return result;
    current = (uint8_t)panel_read(ASTRA_HW_PANEL_LED_DATA);
    panel_write(ASTRA_HW_PANEL_LED_DATA,
                (current & (uint8_t)~owned) | (value & owned));
    return ASTRA_OK;
}

static AstraResult panel_led_atomic(const AstraFrontPanelLedLease *lease,
                                    uint8_t mask, uint32_t operation)
{
    uint8_t owned;
    AstraResult result = panel_lease_mask(lease, &owned);

    if (result != ASTRA_OK)
        return result;
    panel_write(operation, mask & owned);
    return ASTRA_OK;
}

AstraResult astra_front_panel_set_led_bits(
    const AstraFrontPanelLedLease *lease, uint8_t mask)
{
    return panel_led_atomic(lease, mask, ASTRA_HW_PANEL_LED_SET);
}

AstraResult astra_front_panel_clear_led_bits(
    const AstraFrontPanelLedLease *lease, uint8_t mask)
{
    return panel_led_atomic(lease, mask, ASTRA_HW_PANEL_LED_CLEAR);
}

AstraResult astra_front_panel_toggle_led_bits(
    const AstraFrontPanelLedLease *lease, uint8_t mask)
{
    return panel_led_atomic(lease, mask, ASTRA_HW_PANEL_LED_TOGGLE);
}
