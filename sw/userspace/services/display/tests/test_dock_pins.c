#include "../dock_pins.h"

#include <astra/runtime.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void *astra_runtime_allocate(size_t size) { return malloc(size); }
void astra_runtime_deallocate(void *pointer) { free(pointer); }

int main(void)
{
    DisplayDockPins pins = {0}, restored = {0};
    char *encoded;
    uint32_t required = 0u;

    assert(display_dock_pins_set(&pins, "org.astra.atlas", "Atlas",
        "/apps/Atlas.app") == ASTRA_STATUS_OK);
    assert(display_dock_pins_set(&pins, "org.astra.doom", "Doom",
        "/usb0/Some Folder/Doom.app") == ASTRA_STATUS_OK);
    assert(display_dock_pins_set(&pins, "org.astra.atlas", "Atlas New",
        "/apps/Atlas.app") == ASTRA_STATUS_OK && pins.count == 2u);
    assert(display_dock_pins_move(&pins, 1u, 0u) == ASTRA_STATUS_OK);
    assert(strcmp(pins.entries[0].name, "Doom") == 0);
    assert(display_dock_pins_encode(&pins, NULL, 0u, &required) ==
        ASTRA_STATUS_LIMIT && required > 1u);
    encoded = malloc(required);
    assert(encoded != NULL);
    assert(display_dock_pins_encode(&pins, encoded, required, &required) ==
        ASTRA_STATUS_OK);
    assert(display_dock_pins_decode(&restored, encoded,
        required - 1u) == ASTRA_STATUS_OK && restored.count == 2u);
    assert(strcmp(restored.entries[0].path,
        "/usb0/Some Folder/Doom.app") == 0);
    assert(strcmp(restored.entries[1].name, "Atlas New") == 0);
    assert(display_dock_pins_decode(&restored, "2:1:2:abx.a",
        11u) == ASTRA_STATUS_INVALID && restored.count == 2u);
    assert(display_dock_pins_decode(&restored,
        "1:1:2:aA/a1:1:2:aB/b", 20u) == ASTRA_STATUS_INVALID &&
        restored.count == 2u);
    assert(display_dock_pins_remove(&restored, 0u) == ASTRA_STATUS_OK &&
        restored.count == 1u &&
        strcmp(restored.entries[0].id, "org.astra.atlas") == 0);
    assert(display_dock_pins_set(&restored, "bad", "", "/apps/X.app") ==
        ASTRA_STATUS_INVALID);
    assert(display_dock_pins_move(&restored, 1u, 0u) == ASTRA_STATUS_INVALID);
    display_dock_pins_clear(&restored);
    display_dock_pins_clear(&pins);
    free(encoded);
    return 0;
}
