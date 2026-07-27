#include "fault.h"

#include "vesta.h"

#include <stddef.h>

#define M68K_BUS_ERROR_VECTOR_OFFSET 0x0008u
#define M68K_SSW_READ 0x0040u
#define M68K_SSW_SIZE_SHIFT 4u
#define M68K_SSW_SIZE_MASK 0x0003u
#define M68K_SSW_FC_MASK 0x0007u

static void clear_report(KernelFaultReport *report)
{
    uint8_t *bytes = (uint8_t *)report;

    for (uint32_t index = 0u; index < sizeof(*report); ++index)
        bytes[index] = 0u;
}

static bool mapping_has_physical(KernelVmMapping mapping)
{
    return mapping == KERNEL_VM_MAPPING_READ_ONLY ||
           mapping == KERNEL_VM_MAPPING_READ_WRITE;
}

static bool bus_record_matches(const KernelExceptionFrame *frame,
                               bool write,
                               KernelVmMapping mapping,
                               uint32_t expected_physical,
                               const KernelPlatformBusFault *bus_fault)
{
    bool recorded_write;

    if (bus_fault == NULL ||
        (bus_fault->status & BUS_FAULT_VALID) == 0u ||
        !mapping_has_physical(mapping) ||
        bus_fault->address != expected_physical ||
        BUS_FAULT_FC(bus_fault->status) !=
            (frame->special_status & M68K_SSW_FC_MASK) ||
        BUS_FAULT_SIZE(bus_fault->status) !=
            ((frame->special_status >> M68K_SSW_SIZE_SHIFT) &
             M68K_SSW_SIZE_MASK))
        return false;
    recorded_write = (bus_fault->status & BUS_FAULT_WRITE) != 0u;
    return recorded_write == write;
}

bool kernel_fault_classify(const KernelExceptionFrame *frame,
                           KernelVmMapping mapping,
                           uint32_t expected_physical,
                           const KernelPlatformBusFault *bus_fault,
                           KernelFaultReport *report)
{
    bool write;
    bool matched;

    if (frame == NULL || report == NULL || frame->access_fault == 0u ||
        frame->vector_offset != M68K_BUS_ERROR_VECTOR_OFFSET ||
        (frame->format != 0xau && frame->format != 0xbu) ||
        mapping > KERNEL_VM_MAPPING_READ_WRITE)
        return false;

    clear_report(report);
    write = (frame->special_status & M68K_SSW_READ) == 0u;
    report->logical_address = frame->fault_address;
    report->expected_physical = expected_physical;
    report->mapping = (uint8_t)mapping;
    report->write = write ? 1u : 0u;
    if (bus_fault != NULL &&
        (bus_fault->status & BUS_FAULT_VALID) != 0u) {
        report->bus_record_present = 1u;
        report->bus_address = bus_fault->address;
        report->bus_status = bus_fault->status;
        report->bus_target = bus_fault->target;
        report->bus_lost = bus_fault->lost;
        report->bus_cycles_high = bus_fault->cycles_high;
        report->bus_cycles_low = bus_fault->cycles_low;
        report->bus_timeout_cycles = bus_fault->timeout_cycles;
    }

    if (mapping == KERNEL_VM_MAPPING_UNMAPPED) {
        report->kind = KERNEL_FAULT_PMMU_TRANSLATION;
        return true;
    }
    if (write && mapping == KERNEL_VM_MAPPING_READ_ONLY) {
        report->kind = KERNEL_FAULT_PMMU_PROTECTION;
        return true;
    }

    matched = bus_record_matches(frame, write, mapping,
                                 expected_physical, bus_fault);
    report->bus_record_matched = matched ? 1u : 0u;
    if (matched) {
        if ((bus_fault->status & BUS_FAULT_TIMEOUT) != 0u)
            report->kind = KERNEL_FAULT_PHYSICAL_TIMEOUT;
        else if ((bus_fault->status & BUS_FAULT_UNMAPPED) != 0u)
            report->kind = KERNEL_FAULT_PHYSICAL_UNMAPPED;
        else if ((bus_fault->status & BUS_FAULT_DEVICE) != 0u)
            report->kind = KERNEL_FAULT_PHYSICAL_DEVICE;
        else
            report->kind = KERNEL_FAULT_PHYSICAL_EXTERNAL;
    } else {
        report->kind = KERNEL_FAULT_KERNEL_BUG;
    }
    return true;
}

bool kernel_fault_capture(const KernelExceptionFrame *frame,
                          KernelFaultReport *report)
{
    KernelPlatformBusFault bus_fault;
    KernelVmMapping mapping;
    uint32_t physical_address = 0u;
    uint32_t function_code;
    bool supervisor_translation;
    bool bus_present;

    if (frame == NULL || report == NULL)
        return false;
    function_code = frame->special_status & M68K_SSW_FC_MASK;
    supervisor_translation = (function_code & 4u) != 0u;
    mapping = kernel_vm_probe_current(frame->fault_address,
                                      supervisor_translation,
                                      &physical_address);
    bus_present = kernel_platform_bus_fault_read(&bus_fault);
    return kernel_fault_classify(frame, mapping, physical_address,
                                 bus_present ? &bus_fault : NULL, report);
}
