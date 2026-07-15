#include <astra/front_panel.h>

typedef struct FrontPanelDemo {
    AstraFrontPanelLedLease leds;
} FrontPanelDemo;

AstraResult front_panel_demo_init(FrontPanelDemo *demo)
{
    AstraFrontPanelLedLease empty = ASTRA_FRONT_PANEL_LED_LEASE_INIT;

    if (demo == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    demo->leds = empty;
    return astra_front_panel_acquire_leds(ASTRA_PANEL_LED_ALL, 0,
                                          &demo->leds);
}

/* Mirror buttons and DIP switches onto the LEDs owned by this application. */
AstraResult front_panel_demo_step(FrontPanelDemo *demo)
{
    AstraFrontPanelState state;
    AstraResult result;

    if (demo == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_front_panel_read(&state);

    if (result != ASTRA_OK)
        return result;

    return astra_front_panel_set_leds(
        &demo->leds,
        (uint8_t)(state.buttons | (uint8_t)(state.switches << 4)));
}

AstraResult front_panel_demo_shutdown(FrontPanelDemo *demo)
{
    if (demo == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_front_panel_release_leds(&demo->leds);
}
