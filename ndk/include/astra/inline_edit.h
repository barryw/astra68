#ifndef ASTRA_INLINE_EDIT_H
#define ASTRA_INLINE_EDIT_H

/** @file inline_edit.h @brief Single-line edit overlay composed from Field and TextModel. */

#include <astra/control.h>
#include <astra/graphics.h>

ASTRA_EXTERN_C_BEGIN

/** Inline editor width in pixels. */
#define ASTRA_INLINE_EDIT_WIDTH 124u
/** Inline editor height in pixels. */
#define ASTRA_INLINE_EDIT_HEIGHT 32u

/** Caller-owned single-line edit overlay. */
typedef struct AstraInlineEdit {
    /** @cond ASTRA_INTERNAL */
    AstraTextModel model;
    uint8_t content[512];
    _Alignas(4) uint8_t metadata[1024];
    AstraControl field;
    AstraUIContext ui;
    AstraSurface surface;
    uint32_t active;
    /** @endcond */
} AstraInlineEdit;

/** Empty inline editor initializer. */
#define ASTRA_INLINE_EDIT_INIT { \
    .model = ASTRA_TEXT_MODEL_INIT, .field = ASTRA_CONTROL_INIT, \
    .ui = ASTRA_UI_CONTEXT_INIT, .surface = ASTRA_SURFACE_INIT \
}

/** Start editing selected initial text. Reuses its hardware draw surface.
 * @param edit Caller-owned editor.
 * @param display Owning display.
 * @param initial NUL-terminated initial text.
 * @return ASTRA_OK or resource/validation error.
 */
AstraResult astra_interface_inline_edit_start(AstraInlineEdit *edit, AstraDisplay *display,
    const char *initial);
/** Forward text/key input to Field; Enter activates and Escape dismisses.
 * @param edit Active editor.
 * @param event Window event to handle.
 * @param action Receives edit action.
 * @return ASTRA_OK or validation error.
 */
AstraResult astra_interface_inline_edit_event(AstraInlineEdit *edit,
    const AstraWindowEvent *event, AstraUIAction *action);
/** Render Field to the overlay surface for hardware blitting.
 * @param edit Active editor.
 * @param commands Caller-owned draw command storage.
 * @param command_bytes Capacity of @p commands in bytes.
 * @return ASTRA_OK or draw capacity/validation error.
 */
AstraResult astra_interface_inline_edit_render(AstraInlineEdit *edit, uint32_t *commands,
    uint32_t command_bytes);
/** Copy current text; application owns validation and persistence.
 * @param edit Active editor.
 * @param text Receives NUL-terminated text.
 * @param capacity Capacity of @p text in bytes.
 * @return ASTRA_OK or capacity/validation error.
 */
AstraResult astra_interface_inline_edit_text(const AstraInlineEdit *edit,
    char *text, uint32_t capacity);
/** End edit without changing application data.
 * @param edit Active editor.
 */
void astra_interface_inline_edit_stop(AstraInlineEdit *edit);

ASTRA_EXTERN_C_END

#endif
