#ifndef ASTRA_DISPLAY_WIDGETS_INTERNAL_H
#define ASTRA_DISPLAY_WIDGETS_INTERNAL_H

#include <astra/control.h>
#include <astra/theme.h>

int astra_interface_display_model_valid(const AstraControl *control);
AstraResult astra_interface_display_measure(const AstraControl *control,
                                            AstraControlSize *size);
AstraResult astra_interface_display_render(const AstraControl *control,
    const AstraTheme *theme, AstraSurfaceView *surface, uint64_t *commands);

#endif
