#ifndef ASTRA_INTERFACE_LIBRARY_H
#define ASTRA_INTERFACE_LIBRARY_H

#include <astra/interface.h>
#include <astra/window.h>

#define ASTRA_INTERFACE_LIBRARY_ABI_MAJOR 1u
#define ASTRA_INTERFACE_LIBRARY_ABI_MINOR 1u

typedef struct AstraInterfaceLibraryV1 {
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
} AstraInterfaceLibraryV1;

#endif
