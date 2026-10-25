#ifndef ASTRA_NAME_EDIT_H
#define ASTRA_NAME_EDIT_H

#include <astra/interface_kit.h>
#include <astra/graphics.h>

#define ASTRA_NAME_EDIT_WIDTH 124u
#define ASTRA_NAME_EDIT_HEIGHT 32u

typedef struct AstraNameEdit {
    AstraTextModel model;
    uint8_t content[512];
    _Alignas(4) uint8_t metadata[1024];
    AstraControl field;
    AstraUIContext ui;
    AstraSurface surface;
    uint32_t active;
} AstraNameEdit;

#define ASTRA_NAME_EDIT_INIT { \
    .model = ASTRA_TEXT_MODEL_INIT, .field = ASTRA_CONTROL_INIT, \
    .ui = ASTRA_UI_CONTEXT_INIT, .surface = ASTRA_SURFACE_INIT \
}

AstraResult astra_name_edit_start(AstraNameEdit *edit, AstraDisplay *display,
    const char *initial);
AstraResult astra_name_edit_event(AstraNameEdit *edit,
    const AstraWindowEvent *event, AstraUIAction *action);
AstraResult astra_name_edit_render(AstraNameEdit *edit, uint32_t *commands,
    uint32_t command_bytes);
AstraResult astra_name_edit_text(const AstraNameEdit *edit,
    char *text, uint32_t capacity);

#endif
