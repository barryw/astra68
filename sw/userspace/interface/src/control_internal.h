#ifndef ASTRA_INTERFACE_CONTROL_INTERNAL_H
#define ASTRA_INTERFACE_CONTROL_INTERNAL_H

#include <astra/control.h>

AstraResult astra_interface_label_init(AstraControl *control,
                                       const AstraLabelInfo *info);
AstraResult astra_interface_button_init(AstraControl *control,
                                        const AstraButtonInfo *info);
AstraResult astra_interface_container_init(AstraControl *control,
                                           const AstraContainerInfo *info);
AstraResult astra_interface_checkbox_init(AstraControl *control,
                                          const AstraToggleInfo *info);
AstraResult astra_interface_radio_init(AstraControl *control,
                                       const AstraToggleInfo *info);
AstraResult astra_interface_switch_init(AstraControl *control,
                                        const AstraToggleInfo *info);
AstraResult astra_interface_slider_init(AstraControl *control,
                                        const AstraRangeInfo *info);
AstraResult astra_interface_control_set_value(AstraUIContext *context,
                                              AstraControl *control,
                                              int32_t value);
AstraResult astra_interface_control_get_value(const AstraControl *control,
                                              int32_t *value);
AstraResult astra_interface_progress_init(AstraControl *control,
                                          const AstraProgressInfo *info);
AstraResult astra_interface_progress_set(AstraUIContext *context,
                                         AstraControl *control,
                                         uint32_t value, uint32_t maximum);
AstraResult astra_interface_ui_tick(AstraUIContext *context, uint64_t now_ns);
AstraResult astra_interface_control_set_text(AstraUIContext *context,
                                             AstraControl *control,
                                             const char *text,
                                             uint32_t text_length);
AstraResult astra_interface_control_set_flex(AstraControl *control,
                                             const AstraFlexItem *item);
AstraResult astra_interface_ui_init(AstraUIContext *context,
                                    AstraControl *controls,
                                    uint32_t control_count,
                                    uint16_t width, uint16_t height);
AstraResult astra_interface_ui_measure(const AstraUIContext *context,
                                       const AstraControl *control,
                                       AstraControlSize *size);
AstraResult astra_interface_ui_layout(AstraUIContext *context,
                                      const AstraFlexLayout *layout);
AstraResult astra_interface_ui_set_state(AstraUIContext *context,
                                         AstraControl *control,
                                         uint32_t state);
AstraResult astra_interface_ui_render(const AstraUIContext *context,
                                      AstraSurfaceView *surface);
AstraResult astra_interface_ui_handle_event(AstraUIContext *context,
                                            const AstraWindowEvent *event,
                                            AstraUIAction *action);
AstraResult astra_interface_ui_damage(const AstraUIContext *context,
                                      AstraControlFrame *damage);
void astra_interface_ui_damage_clear(AstraUIContext *context);

#endif
