/** @file interface_library.h @brief Interface Kit shared-library ABI. */
#ifndef ASTRA_INTERFACE_LIBRARY_H
#define ASTRA_INTERFACE_LIBRARY_H

#include <stddef.h>

#include <astra/clipboard.h>
#include <astra/control.h>
#include <astra/interface.h>
#include <astra/scroll.h>
#include <astra/text_model.h>
#include <astra/text_surface.h>
#include <astra/undo.h>
#include <astra/window.h>

/** Breaking Interface Kit ABI generation. */
#define ASTRA_INTERFACE_LIBRARY_ABI_MAJOR 5u
/** Latest append-only Interface Kit ABI revision. */
#define ASTRA_INTERFACE_LIBRARY_ABI_MINOR 1u

/** Interface Kit export table for the ABI declared by this header. */
typedef struct AstraInterfaceLibrary {
    /** Export-table ABI major. */
    uint16_t abi_major;
    /** Export-table ABI minor. */
    uint16_t abi_minor;
    /** Available bytes in this table. */
    uint32_t structure_size;
    /** Display a modal alert. */
    AstraResult (*show_alert)(AstraHandle gui, const AstraAlertInfo *info);
    /** Create a window. */
    AstraResult (*window_create)(uint32_t, uint32_t,
                                 const AstraWindowCreateInfo *, AstraWindow *);
    /** Read current window state. */
    AstraResult (*window_get_info)(AstraWindow *, AstraWindowInfo *);
    /** Atomically move and resize a window. */
    AstraResult (*window_set_frame)(AstraWindow *, const AstraWindowFrame *);
    /** Move a window. */
    AstraResult (*window_move)(AstraWindow *, uint16_t, uint16_t);
    /** Resize a window. */
    AstraResult (*window_resize)(AstraWindow *, uint16_t, uint16_t);
    /** Raise a window in z-order. */
    AstraResult (*window_raise)(AstraWindow *);
    /** Lower a window in z-order. */
    AstraResult (*window_lower)(AstraWindow *);
    /** Activate a window. */
    AstraResult (*window_activate)(AstraWindow *);
    /** Deactivate a window. */
    AstraResult (*window_deactivate)(AstraWindow *);
    /** Minimize a window. */
    AstraResult (*window_minimize)(AstraWindow *);
    /** Maximize a window. */
    AstraResult (*window_maximize)(AstraWindow *);
    /** Restore a minimized or maximized window. */
    AstraResult (*window_restore)(AstraWindow *);
    /** Replace a window's UTF-8 title. */
    AstraResult (*window_set_title)(AstraWindow *, const char *, uint16_t);
    /** Select delivered window-event kinds. */
    AstraResult (*window_set_event_mask)(AstraWindow *, uint32_t);
    /** Present all window content. */
    AstraResult (*window_present)(AstraWindow *);
    /** Present one damaged window region. */
    AstraResult (*window_present_region)(AstraWindow *,
                                         const AstraWindowFrame *);
    /** Close a window. */
    AstraResult (*window_close)(AstraWindow *);
    /** Poll for the next window event. */
    AstraResult (*window_event_try)(AstraWindow *, AstraWindowEvent *);
    /** Wait until a window event or deadline. */
    AstraResult (*window_event_wait)(AstraWindow *, AstraWindowEvent *,
                                     AstraMonotonicDeadline);
    /** Return the handle usable in a multi-object wait. */
    AstraHandle (*window_event_wait_handle)(const AstraWindow *);
    /** Return the window-owned coalescing display-vblank wait handle. */
    AstraHandle (*window_vblank_wait_handle)(const AstraWindow *);
    /** Initialize a retained label. */
    AstraResult (*label_init)(AstraControl *, const AstraLabelInfo *);
    /** Initialize a retained button. */
    AstraResult (*button_init)(AstraControl *, const AstraButtonInfo *);
    /** Replace one control's flex constraints. */
    AstraResult (*control_set_flex)(AstraControl *, const AstraFlexItem *);
    /** Initialize a UI context over caller-owned controls. */
    AstraResult (*ui_init)(AstraUIContext *, AstraControl *, uint32_t,
                           uint16_t, uint16_t);
    /** Measure one control's intrinsic extent. */
    AstraResult (*ui_measure)(const AstraUIContext *, const AstraControl *,
                              AstraControlSize *);
    /** Reflow the complete retained control hierarchy. */
    AstraResult (*ui_layout)(AstraUIContext *, const AstraFlexLayout *);
    /** Replace application-owned semantic control state. */
    AstraResult (*ui_set_state)(AstraUIContext *, AstraControl *, uint32_t);
    /** Render damaged retained controls. */
    AstraResult (*ui_render)(AstraUIContext *, AstraSurfaceView *);
    /** Dispatch one window event and return its semantic action. */
    AstraResult (*ui_handle_event)(AstraUIContext *, const AstraWindowEvent *,
                                   AstraUIAction *);
    /** Read the accumulated damaged rectangle. */
    AstraResult (*ui_damage)(const AstraUIContext *, AstraControlFrame *);
    /** Clear accumulated UI damage. */
    void (*ui_damage_clear)(AstraUIContext *);
    /** Initialize a retained checkbox. */
    AstraResult (*checkbox_init)(AstraControl *, const AstraToggleInfo *);
    /** Initialize a retained radio button. */
    AstraResult (*radio_init)(AstraControl *, const AstraToggleInfo *);
    /** Initialize a retained switch. */
    AstraResult (*switch_init)(AstraControl *, const AstraToggleInfo *);
    /** Initialize a retained slider. */
    AstraResult (*slider_init)(AstraControl *, const AstraRangeInfo *);
    /** Set a value control and emit immediate damage. */
    AstraResult (*control_set_value)(AstraUIContext *, AstraControl *,
                                     int64_t);
    /** Read a value control. */
    AstraResult (*control_get_value)(const AstraControl *, int64_t *,
                                     uint32_t *);
    /** Initialize a retained progress indicator. */
    AstraResult (*progress_init)(AstraControl *, const AstraProgressInfo *);
    /** Set progress and indeterminate state. */
    AstraResult (*progress_set)(AstraUIContext *, AstraControl *, uint32_t,
                                uint32_t);
    /** Replace one control's borrowed UTF-8 text span. */
    AstraResult (*control_set_text)(AstraUIContext *, AstraControl *,
                                    const char *, uint32_t);
    /** Initialize a nonvisual retained container. */
    AstraResult (*container_init)(AstraControl *, const AstraContainerInfo *);
    /** Initialize a caller-owned TextSurface. */
    AstraResult (*text_surface_init)(AstraTextSurface *,
                                     const AstraTextSurfaceInfo *);
    /** Render a fixed-grid cell run. */
    AstraResult (*text_surface_render_cells)(
        const AstraTextSurface *, AstraSurfaceView *, int32_t, int32_t,
        uint32_t, uint32_t, const AstraTextCell *, uint32_t);
    /** Draw a fixed-grid caret. */
    AstraResult (*text_surface_draw_caret)(
        const AstraTextSurface *, AstraSurfaceView *, int32_t, int32_t,
        uint32_t, uint32_t, uint32_t, uint16_t);
    /** Scroll fixed-grid content through the shared blit path. */
    AstraResult (*text_surface_scroll)(
        const AstraTextSurface *, AstraSurfaceView *, uint32_t, uint32_t,
        uint32_t, uint32_t, uint32_t, uint32_t);
    /** Set the visible blink phase. */
    AstraResult (*text_surface_set_blink)(AstraTextSurface *, uint32_t);
    /** Render a fixed-grid run with a selection overlay. */
    AstraResult (*text_surface_render_grid)(
        const AstraTextSurface *, AstraSurfaceView *, int32_t, int32_t,
        uint32_t, uint32_t, const AstraTextCell *, uint32_t,
        const AstraTextGridSelection *);
    /** Map a point to a fixed-grid cell boundary. */
    AstraResult (*text_surface_grid_hit_test)(
        const AstraTextSurface *, int32_t, int32_t, uint32_t, uint32_t,
        int32_t, int32_t, AstraTextGridPosition *);
    /** Extract one fixed-grid selection as UTF-8. */
    AstraResult (*text_surface_copy_grid_selection)(
        const AstraTextSurface *, const AstraTextCell *, uint32_t, uint32_t,
        uint32_t, const AstraTextGridSelection *, char *, uint32_t,
        uint32_t *);
    /** Atomically replace the typed system clipboard. */
    AstraResult (*clipboard_write)(
        AstraHandle, const AstraClipboardRepresentation *, uint32_t,
        uint32_t *);
    /** Acquire an immutable clipboard snapshot. */
    AstraResult (*clipboard_read)(AstraHandle, AstraClipboardItem *);
    /** Find a typed representation in a clipboard snapshot. */
    AstraResult (*clipboard_item_find)(
        const AstraClipboardItem *, const char *, uint32_t,
        const void **, uint32_t *);
    /** Close a clipboard snapshot. */
    AstraResult (*clipboard_item_close)(AstraClipboardItem *);
    /** Atomically clear the system clipboard. */
    AstraResult (*clipboard_clear)(AstraHandle, uint32_t *);
    /** Initialize one per-document undo manager. */
    AstraResult (*undo_init)(AstraUndoManager *,
                             const AstraUndoManagerInfo *);
    /** Measure one serialized undo group. */
    AstraResult (*undo_group_size)(const AstraUndoGroupInfo *, uint32_t *);
    /** Record an already-applied transaction. */
    AstraResult (*undo_record_group)(AstraUndoManager *,
                                     const AstraUndoGroupInfo *);
    /** Apply and record one transaction atomically. */
    AstraResult (*undo_perform_group)(AstraUndoManager *,
                                      const AstraUndoGroupInfo *);
    /** Undo the previous transaction. */
    AstraResult (*undo_undo)(AstraUndoManager *);
    /** Redo the next transaction. */
    AstraResult (*undo_redo)(AstraUndoManager *);
    /** Wipe all retained history. */
    AstraResult (*undo_clear)(AstraUndoManager *);
    /** Mark the current history position as saved. */
    AstraResult (*undo_mark_clean)(AstraUndoManager *);
    /** Read history availability, names, and dirty state. */
    AstraResult (*undo_get_state)(const AstraUndoManager *, AstraUndoState *);
    /** Move retained history into replacement caller-owned storage. */
    AstraResult (*undo_move_arena)(AstraUndoManager *, void *, uint32_t);
    /** Initialize a caller-owned UTF-8 piece-table document. */
    AstraResult (*text_model_init)(AstraTextModel *,
                                   const AstraTextModelInfo *);
    /** Perform complete text-model invariant validation. */
    AstraResult (*text_model_validate)(const AstraTextModel *);
    /** Read text-model state and arena accounting. */
    AstraResult (*text_model_get_state)(const AstraTextModel *,
                                        AstraTextModelState *);
    /** Replace the model's scalar-boundary selection. */
    AstraResult (*text_model_set_selection)(AstraTextModel *,
                                            const AstraTextSelection *);
    /** Calculate exact post-replacement arena requirements. */
    AstraResult (*text_model_replace_requirements)(
        const AstraTextModel *, uint32_t, uint32_t, const char *, uint32_t,
        AstraTextModelRequirements *);
    /** Atomically replace one scalar-boundary UTF-8 range. */
    AstraResult (*text_model_replace)(AstraTextModel *, uint32_t, uint32_t,
                                      const char *, uint32_t);
    /** Atomically copy one scalar-boundary UTF-8 range. */
    AstraResult (*text_model_copy)(const AstraTextModel *, uint32_t,
                                   uint32_t, char *, uint32_t, uint32_t *);
    /** Borrow the contiguous piece at one document byte offset. */
    AstraResult (*text_model_read)(const AstraTextModel *, uint32_t,
                                   const char **, uint32_t *);
    /** Advance one model position by a Unicode scalar. */
    AstraResult (*text_model_scalar_advance)(const AstraTextModel *,
                                             uint32_t *);
    /** Retreat one model position by a Unicode scalar. */
    AstraResult (*text_model_scalar_retreat)(const AstraTextModel *,
                                             uint32_t *);
    /** Resolve one indexed logical line. */
    AstraResult (*text_model_get_line)(const AstraTextModel *, uint32_t,
                                       AstraTextLine *);
    /** Compact live text into replacement caller-owned arenas. */
    AstraResult (*text_model_move_arenas)(AstraTextModel *, void *, uint32_t,
                                          void *, uint32_t);
    /** Wipe occupied arenas and dispose a caller-owned text model. */
    void (*text_model_dispose)(AstraTextModel *);
    /** Initialize a retained single-line UTF-8 field. */
    AstraResult (*field_init)(AstraControl *, const AstraFieldInfo *);
    /** Revalidate and damage a field after an external model mutation. */
    AstraResult (*field_refresh)(AstraUIContext *, AstraControl *);
    /** Replace a field selection while preserving single-line invariants. */
    AstraResult (*field_replace_selection)(AstraUIContext *, AstraControl *,
                                           const char *, uint32_t);
    /** Advance every active control exactly once for one display vblank. */
    AstraResult (*ui_vblank)(AstraUIContext *, uint64_t, AstraUIAction *);
    /** Return nonzero while at least one control owns an animation. */
    int (*ui_animations_active)(const AstraUIContext *);
    /** Initialize a mutually exclusive segmented selector. */
    AstraResult (*segmented_init)(AstraControl *,
                                  const AstraChoiceInfo *);
    /** Initialize a retained numeric stepper. */
    AstraResult (*stepper_init)(AstraControl *, const AstraStepperInfo *);
    /** Initialize an underline-style peer-navigation tab strip. */
    AstraResult (*tab_init)(AstraControl *, const AstraChoiceInfo *);
    /** Initialize a caller-owned shared scroll model. */
    AstraResult (*scroll_init)(AstraScrollModel *,
                               const AstraScrollModelInfo *);
    /** Read one scroll-model snapshot. */
    AstraResult (*scroll_get_state)(const AstraScrollModel *,
                                    AstraScrollState *);
    /** Replace content and viewport extents. */
    AstraResult (*scroll_set_extents)(AstraScrollModel *, uint32_t, uint32_t,
                                      uint32_t, uint32_t);
    /** Set absolute scroll offsets. */
    AstraResult (*scroll_set_offset)(AstraScrollModel *, uint32_t, uint32_t);
    /** Add signed scroll deltas. */
    AstraResult (*scroll_by)(AstraScrollModel *, int32_t, int32_t);
    /** Replace the borrowed scrollbar-marker array. */
    AstraResult (*scroll_set_marks)(AstraScrollModel *,
                                    const AstraScrollMark *, uint32_t);
    /** Initialize a viewport that clips and translates one direct child. */
    AstraResult (*scroll_view_init)(AstraControl *,
                                    const AstraScrollViewInfo *);
    /** Initialize a scrollbar bound to a shared scroll model. */
    AstraResult (*scrollbar_init)(AstraControl *,
                                  const AstraScrollbarInfo *);
    /** Initialize the divider in a flex-composed split view. */
    AstraResult (*splitter_init)(AstraControl *,
                                 const AstraSplitterInfo *);
    /** Select a server-supplied pointer image for window content. */
    AstraResult (*window_set_pointer_shape)(AstraWindow *,
                                            AstraPointerShape);
    /** Copy, install, and select a custom pointer image. */
    AstraResult (*window_set_pointer_image)(
        AstraWindow *, const AstraHardwarePointerImage *);
    /** Synchronize an override or the image implied by UI hover/capture. */
    AstraResult (*ui_update_pointer)(const AstraUIContext *, AstraWindow *,
                                     uint32_t, uint32_t *);
    /** Initialize a retained vertically-dragged dial. */
    AstraResult (*dial_init)(AstraControl *, const AstraDialInfo *);
    /** Initialize a disclosure header controlling a sibling container. */
    AstraResult (*disclosure_init)(AstraControl *,
                                   const AstraDisclosureInfo *);
} AstraInterfaceLibrary;

/** Compute the export-table extent through one named member. */
#define ASTRA_INTERFACE_LIBRARY_SIZE_THROUGH(member)                       \
    ((uint32_t)(offsetof(AstraInterfaceLibrary, member) +                   \
                sizeof(((AstraInterfaceLibrary *)0)->member)))

/** Interface Kit 5.0 export-table extent. */
#define ASTRA_INTERFACE_LIBRARY_5_0_SIZE \
    ASTRA_INTERFACE_LIBRARY_SIZE_THROUGH(dial_init)
/** Interface Kit 5.1 export-table extent. */
#define ASTRA_INTERFACE_LIBRARY_5_1_SIZE \
    ASTRA_INTERFACE_LIBRARY_SIZE_THROUGH(disclosure_init)

/**
 * Verify one consumer's minimum compatible minor and table extent.
 * @param library Open Interface Kit export table.
 * @param minimum_minor Oldest compatible 5.x minor required by the caller.
 * @param minimum_structure_size Required append-only table extent.
 * @return Nonzero when the library satisfies both requirements.
 */
static inline int astra_interface_library_supports(
    const AstraInterfaceLibrary *library, uint16_t minimum_minor,
    uint32_t minimum_structure_size)
{
    return library != NULL &&
           library->abi_major == ASTRA_INTERFACE_LIBRARY_ABI_MAJOR &&
           library->abi_minor >= minimum_minor &&
           library->structure_size >= minimum_structure_size;
}

#endif
