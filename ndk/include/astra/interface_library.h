#ifndef ASTRA_INTERFACE_LIBRARY_H
#define ASTRA_INTERFACE_LIBRARY_H

#include <astra/control.h>
#include <astra/interface.h>
#include <astra/window.h>

#define ASTRA_INTERFACE_LIBRARY_ABI_MAJOR 2u
#define ASTRA_INTERFACE_LIBRARY_ABI_MINOR 0u

typedef struct AstraInterfaceLibraryV2 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;
    AstraResult (*show_alert)(AstraHandle gui, const AstraAlertInfo *info);
    AstraResult (*window_create)(uint32_t, uint32_t,
                                 const AstraWindowCreateInfo *, AstraWindow *);
    AstraResult (*window_get_info)(AstraWindow *, AstraWindowInfo *);
    AstraResult (*window_set_frame)(AstraWindow *, const AstraWindowFrame *);
    AstraResult (*window_move)(AstraWindow *, uint16_t, uint16_t);
    AstraResult (*window_resize)(AstraWindow *, uint16_t, uint16_t);
    AstraResult (*window_raise)(AstraWindow *);
    AstraResult (*window_lower)(AstraWindow *);
    AstraResult (*window_activate)(AstraWindow *);
    AstraResult (*window_deactivate)(AstraWindow *);
    AstraResult (*window_minimize)(AstraWindow *);
    AstraResult (*window_maximize)(AstraWindow *);
    AstraResult (*window_restore)(AstraWindow *);
    AstraResult (*window_set_title)(AstraWindow *, const char *, uint16_t);
    AstraResult (*window_set_event_mask)(AstraWindow *, uint32_t);
    AstraResult (*window_present)(AstraWindow *);
    AstraResult (*window_present_region)(AstraWindow *,
                                         const AstraWindowFrame *);
    AstraResult (*window_close)(AstraWindow *);
    AstraResult (*window_event_try)(AstraWindow *, AstraWindowEvent *);
    AstraResult (*window_event_wait)(AstraWindow *, AstraWindowEvent *,
                                     AstraMonotonicDeadline);
    AstraHandle (*window_event_wait_handle)(const AstraWindow *);
    AstraResult (*label_init)(AstraControl *, const AstraLabelInfo *);
    AstraResult (*button_init)(AstraControl *, const AstraButtonInfo *);
    AstraResult (*control_set_flex)(AstraControl *, const AstraFlexItem *);
    AstraResult (*ui_init)(AstraUIContext *, AstraControl *, uint32_t,
                           uint16_t, uint16_t);
    AstraResult (*ui_measure)(const AstraUIContext *, const AstraControl *,
                              AstraControlSize *);
    AstraResult (*ui_layout)(AstraUIContext *, const AstraFlexLayout *);
    AstraResult (*ui_set_state)(AstraUIContext *, AstraControl *, uint32_t);
    AstraResult (*ui_render)(const AstraUIContext *, AstraSurfaceView *);
    AstraResult (*ui_handle_event)(AstraUIContext *, const AstraWindowEvent *,
                                   AstraUIAction *);
    AstraResult (*ui_damage)(const AstraUIContext *, AstraControlFrame *);
    void (*ui_damage_clear)(AstraUIContext *);
    AstraResult (*checkbox_init)(AstraControl *, const AstraToggleInfo *);
    AstraResult (*radio_init)(AstraControl *, const AstraToggleInfo *);
    AstraResult (*switch_init)(AstraControl *, const AstraToggleInfo *);
    AstraResult (*slider_init)(AstraControl *, const AstraRangeInfo *);
    AstraResult (*control_set_value)(AstraUIContext *, AstraControl *,
                                     int32_t);
    AstraResult (*control_get_value)(const AstraControl *, int32_t *);
    AstraResult (*progress_init)(AstraControl *, const AstraProgressInfo *);
    AstraResult (*progress_set)(AstraUIContext *, AstraControl *, uint32_t,
                                uint32_t);
    AstraResult (*ui_tick)(AstraUIContext *, uint64_t);
    AstraResult (*control_set_text)(AstraUIContext *, AstraControl *,
                                    const char *, uint32_t);
    AstraResult (*container_init)(AstraControl *, const AstraContainerInfo *);
} AstraInterfaceLibraryV2;

#endif
