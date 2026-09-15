#ifndef ASTRA_KERNEL_CAPACITY_H
#define ASTRA_KERNEL_CAPACITY_H

#include "vm.h"

#include <astra/library.h>
#include <astra/process.h>

/* Every address space can consume its complete share of area objects. */
#define KERNEL_AREA_OWNER_MAX 8u
#define KERNEL_AREA_MAX \
    (KERNEL_VM_ADDRESS_SPACE_MAX * KERNEL_AREA_OWNER_MAX)

/*
 * The kernel, every process, every resident library, and every area can own
 * frames simultaneously.  This is their complete object-pool demand, not an
 * independently chosen memory-owner ceiling.
 */
#define KERNEL_MEMORY_OWNER_MAX \
    (1u + ASTRA_PROCESS_COUNT_MAX + ASTRA_LIBRARY_SLOT_COUNT + \
     KERNEL_AREA_MAX)

#endif
