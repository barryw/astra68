#ifndef ASTRA_INTERFACE_PANEL_INTERNAL_H
#define ASTRA_INTERFACE_PANEL_INTERNAL_H

#include <astra/interface_library.h>

/* Shared flex attachment for composed system panels. */
static inline AstraResult astra_panel_child(AstraControl *control,
    uint32_t parent, uint32_t grow)
{
    AstraFlexItem item = ASTRA_FLEX_ITEM_INIT;
    item.parent_id = parent;
    item.grow = grow;
    if (grow != 0u) item.align_self = ASTRA_FLEX_ALIGN_STRETCH;
    return astra_interface_control_set_flex(control, &item);
}

#endif
