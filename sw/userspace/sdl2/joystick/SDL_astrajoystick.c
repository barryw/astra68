#include "../../SDL_internal.h"

#ifdef SDL_JOYSTICK_ASTRA

#include "SDL_joystick.h"
#include "SDL_events.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include <astra/gamepad.h>
#include <astra/input.h>
#include <astra/input_service.h>
#include <astra/posix.h>
#include <astra/runtime.h>

typedef struct AstraPad {
    SDL_JoystickID instance;
    SDL_JoystickGUID guid;
    SDL_Joystick *joystick;
    char *name;
    Sint16 axes[8];
    Uint32 buttons;
    Uint8 hat;
    Uint8 naxes;
    Uint8 nbuttons;
    Uint8 nhats;
    SDL_bool connected;
} AstraPad;

struct joystick_hwdata {
    AstraPad *pad;
    SDL_bool snapshot_pending;
};

static AstraGamepadObserver observer = ASTRA_GAMEPAD_OBSERVER_INIT;
static AstraPad *pads[ASTRA_INPUT_GAMEPAD_LIMIT];

static void ASTRA_FreePad(AstraPad *pad)
{
    if (pad != NULL) {
        SDL_free(pad->name);
        SDL_free(pad);
    }
}

static void ASTRA_RemovePad(unsigned slot)
{
    AstraPad *pad = pads[slot];

    if (pad == NULL)
        return;
    pads[slot] = NULL;
    pad->connected = SDL_FALSE;
    SDL_PrivateJoystickRemoved(pad->instance);
    if (pad->joystick == NULL)
        ASTRA_FreePad(pad);
}

static AstraPad *ASTRA_PadAt(int index)
{
    for (unsigned slot = 0; slot < SDL_arraysize(pads); ++slot) {
        if (pads[slot] != NULL && index-- == 0)
            return pads[slot];
    }
    return NULL;
}

static void ASTRA_JoystickDetect(void)
{
    AstraGamepadEvent event;
    AstraResult result;

    if (observer._private_events == ASTRA_INVALID_HANDLE)
        return;
    while ((result = astra_gamepad_event_try(&observer, &event)) == ASTRA_OK) {
        AstraPad *pad;

        if (event.type == ASTRA_GAMEPAD_EVENT_RESET) {
            for (unsigned slot = 0; slot < SDL_arraysize(pads); ++slot)
                ASTRA_RemovePad(slot);
            continue;
        }
        if (event.slot >= SDL_arraysize(pads))
            continue;
        if (event.type == ASTRA_GAMEPAD_EVENT_DEVICE) {
            ASTRA_RemovePad(event.slot);
            if ((event.flags & ASTRA_GAMEPAD_EVENT_DOWN) == 0u ||
                event.axes > 8u || event.buttons > 32u || event.hats > 1u)
                continue;
            pad = SDL_calloc(1u, sizeof(*pad));
            if (pad == NULL)
                continue;
            pad->name = SDL_CreateJoystickName(event.vendor, event.product,
                                                NULL, NULL);
            if (pad->name == NULL)
                pad->name = SDL_strdup("USB Joystick");
            if (pad->name == NULL) {
                ASTRA_FreePad(pad);
                continue;
            }
            pad->guid = SDL_CreateJoystickGUID(event.bus, event.vendor,
                event.product, event.product_version, NULL, NULL, 0, 0);
            pad->instance = SDL_GetNextJoystickInstanceID();
            pad->naxes = event.axes;
            pad->nbuttons = event.buttons;
            pad->nhats = event.hats;
            pad->connected = SDL_TRUE;
            pads[event.slot] = pad;
            SDL_PrivateJoystickAdded(pad->instance);
            continue;
        }
        pad = pads[event.slot];
        if (pad == NULL)
            continue;
        if (event.type == ASTRA_GAMEPAD_EVENT_AXIS &&
            event.index < pad->naxes) {
            pad->axes[event.index] = (Sint16)event.value;
            if (pad->joystick != NULL)
                SDL_PrivateJoystickAxis(pad->joystick, event.index,
                                         pad->axes[event.index]);
        } else if (event.type == ASTRA_GAMEPAD_EVENT_BUTTON &&
                   event.index < pad->nbuttons) {
            Uint32 mask = 1u << event.index;

            if ((event.flags & ASTRA_GAMEPAD_EVENT_DOWN) != 0u)
                pad->buttons |= mask;
            else
                pad->buttons &= ~mask;
            if (pad->joystick != NULL)
                SDL_PrivateJoystickButton(pad->joystick, event.index,
                    (event.flags & ASTRA_GAMEPAD_EVENT_DOWN) != 0u ?
                        SDL_PRESSED : SDL_RELEASED);
        } else if (event.type == ASTRA_GAMEPAD_EVENT_HAT &&
                   pad->nhats != 0u) {
            pad->hat = (Uint8)event.value;
            if (pad->joystick != NULL)
                SDL_PrivateJoystickHat(pad->joystick, 0u, pad->hat);
        }
    }
    if (result != ASTRA_ERROR_WOULD_BLOCK)
        SDL_SetError("Astra gamepad input failed (%d)", result);
}

static int ASTRA_JoystickInit(void)
{
    const AstraStartupCapability *service = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_INPUT_SERVICE);
    AstraResult result;

    if (service == NULL)
        return 0;
    result = astra_gamepad_observer_open(service->handle, &observer);
    if (result != ASTRA_OK)
        return SDL_SetError("Astra gamepad service unavailable (%d)", result);
    ASTRA_JoystickDetect();
    return 0;
}

static int ASTRA_JoystickGetCount(void)
{
    int count = 0;

    for (unsigned slot = 0; slot < SDL_arraysize(pads); ++slot)
        count += pads[slot] != NULL;
    return count;
}

static const char *ASTRA_JoystickGetDeviceName(int index)
{
    AstraPad *pad = ASTRA_PadAt(index);

    return pad != NULL ? pad->name : NULL;
}

static const char *ASTRA_JoystickGetDevicePath(int index)
{
    return NULL;
}

static int ASTRA_JoystickGetDeviceSteamVirtualGamepadSlot(int index)
{
    return -1;
}

static int ASTRA_JoystickGetDevicePlayerIndex(int index)
{
    return -1;
}

static void ASTRA_JoystickSetDevicePlayerIndex(int index, int player_index)
{
}

static SDL_JoystickGUID ASTRA_JoystickGetDeviceGUID(int index)
{
    AstraPad *pad = ASTRA_PadAt(index);
    SDL_JoystickGUID guid;

    SDL_zero(guid);
    return pad != NULL ? pad->guid : guid;
}

static SDL_JoystickID ASTRA_JoystickGetDeviceInstanceID(int index)
{
    AstraPad *pad = ASTRA_PadAt(index);

    return pad != NULL ? pad->instance : -1;
}

static int ASTRA_JoystickOpen(SDL_Joystick *joystick, int index)
{
    AstraPad *pad = ASTRA_PadAt(index);
    struct joystick_hwdata *hwdata;

    if (pad == NULL || pad->joystick != NULL)
        return SDL_SetError("Astra gamepad unavailable");
    hwdata = SDL_calloc(1u, sizeof(*hwdata));
    if (hwdata == NULL)
        return SDL_OutOfMemory();
    hwdata->pad = pad;
    hwdata->snapshot_pending = SDL_TRUE;
    joystick->hwdata = hwdata;
    joystick->naxes = pad->naxes;
    joystick->nbuttons = pad->nbuttons;
    joystick->nhats = pad->nhats;
    pad->joystick = joystick;
    return 0;
}

static int ASTRA_JoystickUnsupported(SDL_Joystick *joystick,
                                      Uint16 left, Uint16 right)
{
    return SDL_Unsupported();
}

static int ASTRA_JoystickRumbleTriggers(SDL_Joystick *joystick,
                                        Uint16 left, Uint16 right)
{
    return SDL_Unsupported();
}

static Uint32 ASTRA_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    return 0u;
}

static int ASTRA_JoystickSetLED(SDL_Joystick *joystick, Uint8 red,
                                Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static int ASTRA_JoystickSendEffect(SDL_Joystick *joystick,
                                    const void *data, int size)
{
    return SDL_Unsupported();
}

static int ASTRA_JoystickSetSensorsEnabled(SDL_Joystick *joystick,
                                           SDL_bool enabled)
{
    return SDL_Unsupported();
}

static void ASTRA_JoystickUpdate(SDL_Joystick *joystick)
{
    struct joystick_hwdata *hwdata = joystick->hwdata;
    AstraPad *pad;

    if (hwdata == NULL || !hwdata->snapshot_pending)
        return;
    pad = hwdata->pad;
    hwdata->snapshot_pending = SDL_FALSE;
    for (unsigned axis = 0; axis < pad->naxes; ++axis)
        SDL_PrivateJoystickAxis(joystick, axis, pad->axes[axis]);
    for (unsigned button = 0; button < pad->nbuttons; ++button)
        if ((pad->buttons & (1u << button)) != 0u)
            SDL_PrivateJoystickButton(joystick, button, SDL_PRESSED);
    if (pad->nhats != 0u)
        SDL_PrivateJoystickHat(joystick, 0u, pad->hat);
}

static void ASTRA_JoystickClose(SDL_Joystick *joystick)
{
    struct joystick_hwdata *hwdata = joystick->hwdata;
    AstraPad *pad;

    if (hwdata == NULL)
        return;
    pad = hwdata->pad;
    pad->joystick = NULL;
    joystick->hwdata = NULL;
    SDL_free(hwdata);
    if (!pad->connected)
        ASTRA_FreePad(pad);
}

static void ASTRA_JoystickQuit(void)
{
    for (unsigned slot = 0; slot < SDL_arraysize(pads); ++slot)
        ASTRA_RemovePad(slot);
    if (observer._private_events != ASTRA_INVALID_HANDLE) {
        AstraResult result = astra_gamepad_observer_close(&observer);

        (void)result;
        observer = (AstraGamepadObserver)ASTRA_GAMEPAD_OBSERVER_INIT;
    }
}

static SDL_bool ASTRA_JoystickGetGamepadMapping(int index,
                                                SDL_GamepadMapping *out)
{
    return SDL_FALSE;
}

SDL_JoystickDriver SDL_ASTRA_JoystickDriver = {
    ASTRA_JoystickInit,
    ASTRA_JoystickGetCount,
    ASTRA_JoystickDetect,
    ASTRA_JoystickGetDeviceName,
    ASTRA_JoystickGetDevicePath,
    ASTRA_JoystickGetDeviceSteamVirtualGamepadSlot,
    ASTRA_JoystickGetDevicePlayerIndex,
    ASTRA_JoystickSetDevicePlayerIndex,
    ASTRA_JoystickGetDeviceGUID,
    ASTRA_JoystickGetDeviceInstanceID,
    ASTRA_JoystickOpen,
    ASTRA_JoystickUnsupported,
    ASTRA_JoystickRumbleTriggers,
    ASTRA_JoystickGetCapabilities,
    ASTRA_JoystickSetLED,
    ASTRA_JoystickSendEffect,
    ASTRA_JoystickSetSensorsEnabled,
    ASTRA_JoystickUpdate,
    ASTRA_JoystickClose,
    ASTRA_JoystickQuit,
    ASTRA_JoystickGetGamepadMapping,
};

#endif
