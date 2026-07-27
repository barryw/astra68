#ifndef ASTRA_KERNEL_FAULT_H
#define ASTRA_KERNEL_FAULT_H

#include "exception.h"
#include "platform.h"
#include "vm.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum KernelFaultKind {
    KERNEL_FAULT_INVALID = 0,
    KERNEL_FAULT_PMMU_TRANSLATION,
    KERNEL_FAULT_PMMU_PROTECTION,
    KERNEL_FAULT_PHYSICAL_UNMAPPED,
    KERNEL_FAULT_PHYSICAL_TIMEOUT,
    KERNEL_FAULT_PHYSICAL_DEVICE,
    KERNEL_FAULT_PHYSICAL_EXTERNAL,
    KERNEL_FAULT_KERNEL_BUG
} KernelFaultKind;

#define KERNEL_FAULT_TRACE_KIND_MASK       0x00ffu
#define KERNEL_FAULT_TRACE_WRITE           0x0100u
#define KERNEL_FAULT_TRACE_RECORD_MATCHED  0x0200u
#define KERNEL_FAULT_TRACE_RECORD_STALE    0x0400u
#define KERNEL_FAULT_TRACE_RECORD_LOST     0x0800u
#define KERNEL_FAULT_TRACE_MAPPING_SHIFT   12u
#define KERNEL_FAULT_TRACE_MAPPING_MASK    0x3000u

typedef struct KernelFaultReport {
    uint32_t logical_address;
    uint32_t expected_physical;
    uint32_t bus_address;
    uint32_t bus_status;
    uint32_t bus_target;
    uint32_t bus_lost;
    uint32_t bus_cycles_high;
    uint32_t bus_cycles_low;
    uint32_t bus_timeout_cycles;
    uint8_t kind;
    uint8_t mapping;
    uint8_t write;
    uint8_t bus_record_present;
    uint8_t bus_record_matched;
    uint8_t reserved[3];
} KernelFaultReport;

_Static_assert(sizeof(KernelFaultReport) == 44u,
               "fault report layout changed");

bool kernel_fault_classify(const KernelExceptionFrame *frame,
                           KernelVmMapping mapping,
                           uint32_t expected_physical,
                           const KernelPlatformBusFault *bus_fault,
                           KernelFaultReport *report);
bool kernel_fault_capture(const KernelExceptionFrame *frame,
                          KernelFaultReport *report);

#endif
