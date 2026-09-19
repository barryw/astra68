#ifndef ASTRA_LOADER_SELF_RELOCATE_H
#define ASTRA_LOADER_SELF_RELOCATE_H

#include <stdint.h>

int astra_loader_self_relocate(uintptr_t base, const void *dynamic,
                               const void *dynamic_end,
                               uintptr_t writable_start,
                               uintptr_t writable_end,
                               uintptr_t image_end);

#endif
