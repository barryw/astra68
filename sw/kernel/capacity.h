#ifndef ASTRA_KERNEL_CAPACITY_H
#define ASTRA_KERNEL_CAPACITY_H

#include "vm.h"

#include <astra/library.h>
#include <astra/process.h>

/* Every address space can consume its complete share of area objects. */
#define KERNEL_AREA_OWNER_MAX 8u
#define KERNEL_AREA_MAX \
    (KERNEL_VM_ADDRESS_SPACE_MAX * KERNEL_AREA_OWNER_MAX)

#endif
