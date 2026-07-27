#include "fault.h"

#include "vesta.h"

#include <assert.h>
#include <stdio.h>

static KernelVmMapping simulated_mapping;
static uint32_t simulated_physical;
static bool simulated_bus_present;
static KernelPlatformBusFault simulated_bus;
static bool observed_supervisor_translation;

KernelVmMapping kernel_vm_probe_current(uint32_t virtual_address,
                                        bool supervisor,
                                        uint32_t *physical_address)
{
    assert(virtual_address == 0x40001234u);
    assert(physical_address != NULL);
    observed_supervisor_translation = supervisor;
    *physical_address = simulated_physical;
    return simulated_mapping;
}

bool kernel_platform_bus_fault_read(KernelPlatformBusFault *fault)
{
    assert(fault != NULL);
    if (!simulated_bus_present)
        return false;
    *fault = simulated_bus;
    return true;
}

static KernelExceptionFrame make_frame(uint16_t special_status)
{
    KernelExceptionFrame frame = {0};

    frame.fault_address = 0x40001234u;
    frame.vector_offset = 0x0008u;
    frame.special_status = special_status;
    frame.format = 0xbu;
    frame.access_fault = 1u;
    return frame;
}

static KernelPlatformBusFault make_bus(uint32_t status)
{
    KernelPlatformBusFault fault = {0};

    fault.status = BUS_FAULT_VALID | status;
    fault.address = 0x02010234u;
    fault.target = BUS_FAULT_TARGET_USB;
    fault.cycles_high = 0x01234567u;
    fault.cycles_low = 0x89abcdefu;
    fault.lost = 2u;
    fault.timeout_cycles = 2048u;
    return fault;
}

static void test_pmmu_and_invalid_classification(void)
{
    KernelExceptionFrame frame = make_frame(0x0141u);
    KernelFaultReport report;

    assert(!kernel_fault_classify(NULL, KERNEL_VM_MAPPING_UNMAPPED,
                                  0u, NULL, &report));
    assert(!kernel_fault_classify(&frame, KERNEL_VM_MAPPING_UNMAPPED,
                                  0u, NULL, NULL));
    frame.vector_offset = 0x000cu;
    assert(!kernel_fault_classify(&frame, KERNEL_VM_MAPPING_UNMAPPED,
                                  0u, NULL, &report));

    frame = make_frame(0x0141u);
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_UNMAPPED,
                                 0u, NULL, &report));
    assert(report.kind == KERNEL_FAULT_PMMU_TRANSLATION);
    assert(report.write == 0u && report.bus_record_present == 0u);

    frame = make_frame(0x0101u);
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_ONLY,
                                 0x02010234u, NULL, &report));
    assert(report.kind == KERNEL_FAULT_PMMU_PROTECTION);
    assert(report.write == 1u);

    frame = make_frame(0x0141u);
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_ONLY,
                                 0x02010234u, NULL, &report));
    assert(report.kind == KERNEL_FAULT_KERNEL_BUG);
}

static void test_matching_physical_classes(void)
{
    KernelExceptionFrame frame = make_frame(0x0105u);
    KernelPlatformBusFault bus;
    KernelFaultReport report;

    bus = make_bus(BUS_FAULT_TIMEOUT | BUS_FAULT_DEVICE |
                   BUS_FAULT_WRITE | (5u << BUS_FAULT_FC_SHIFT));
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_WRITE,
                                 bus.address, &bus, &report));
    assert(report.kind == KERNEL_FAULT_PHYSICAL_TIMEOUT);
    assert(report.bus_record_matched == 1u);
    assert(report.bus_address == bus.address);
    assert(report.bus_lost == 2u);
    assert(report.bus_cycles_high == 0x01234567u);
    assert(report.bus_cycles_low == 0x89abcdefu);
    assert(report.bus_timeout_cycles == 2048u);

    bus = make_bus(BUS_FAULT_UNMAPPED | BUS_FAULT_WRITE |
                   (5u << BUS_FAULT_FC_SHIFT));
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_WRITE,
                                 bus.address, &bus, &report));
    assert(report.kind == KERNEL_FAULT_PHYSICAL_UNMAPPED);

    bus = make_bus(BUS_FAULT_DEVICE | BUS_FAULT_WRITE |
                   (5u << BUS_FAULT_FC_SHIFT));
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_WRITE,
                                 bus.address, &bus, &report));
    assert(report.kind == KERNEL_FAULT_PHYSICAL_DEVICE);

    bus = make_bus(BUS_FAULT_WRITE | (5u << BUS_FAULT_FC_SHIFT));
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_WRITE,
                                 bus.address, &bus, &report));
    assert(report.kind == KERNEL_FAULT_PHYSICAL_EXTERNAL);
}

static void test_stale_record_never_overrides_pmmu(void)
{
    KernelExceptionFrame frame = make_frame(0x0105u);
    KernelPlatformBusFault bus = make_bus(
        BUS_FAULT_DEVICE | BUS_FAULT_WRITE |
        (5u << BUS_FAULT_FC_SHIFT));
    KernelFaultReport report;

    bus.address += 4u;
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_UNMAPPED,
                                 0u, &bus, &report));
    assert(report.kind == KERNEL_FAULT_PMMU_TRANSLATION);
    assert(report.bus_record_present == 1u);
    assert(report.bus_record_matched == 0u);
    assert(report.bus_address == bus.address);

    bus.address = 0x02010234u;
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_ONLY,
                                 bus.address, &bus, &report));
    assert(report.kind == KERNEL_FAULT_PMMU_PROTECTION);
    assert(report.bus_record_present == 1u);
    assert(report.bus_record_matched == 0u);

    bus.status = BUS_FAULT_VALID | BUS_FAULT_DEVICE |
                 BUS_FAULT_WRITE | (1u << BUS_FAULT_FC_SHIFT);
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_WRITE,
                                 bus.address, &bus, &report));
    assert(report.kind == KERNEL_FAULT_KERNEL_BUG);
    assert(report.bus_record_present == 1u);
    assert(report.bus_record_matched == 0u);

    bus.status = BUS_FAULT_VALID | BUS_FAULT_DEVICE |
                 BUS_FAULT_WRITE | (2u << BUS_FAULT_SIZE_SHIFT) |
                 (5u << BUS_FAULT_FC_SHIFT);
    assert(kernel_fault_classify(&frame, KERNEL_VM_MAPPING_READ_WRITE,
                                 bus.address, &bus, &report));
    assert(report.kind == KERNEL_FAULT_KERNEL_BUG);
    assert(report.bus_record_present == 1u);
    assert(report.bus_record_matched == 0u);
}

static void test_runtime_capture_uses_ssw_function_code(void)
{
    KernelExceptionFrame frame = make_frame(0x0105u);
    KernelFaultReport report;

    simulated_mapping = KERNEL_VM_MAPPING_READ_WRITE;
    simulated_physical = 0x02010234u;
    simulated_bus_present = true;
    simulated_bus = make_bus(BUS_FAULT_DEVICE | BUS_FAULT_WRITE |
                             (5u << BUS_FAULT_FC_SHIFT));
    observed_supervisor_translation = false;
    assert(kernel_fault_capture(&frame, &report));
    assert(observed_supervisor_translation);
    assert(report.kind == KERNEL_FAULT_PHYSICAL_DEVICE);

    frame.special_status = 0x0101u;
    simulated_bus.status = BUS_FAULT_VALID | BUS_FAULT_DEVICE |
                           BUS_FAULT_WRITE |
                           (1u << BUS_FAULT_FC_SHIFT);
    observed_supervisor_translation = true;
    assert(kernel_fault_capture(&frame, &report));
    assert(!observed_supervisor_translation);
    assert(report.kind == KERNEL_FAULT_PHYSICAL_DEVICE);

    simulated_mapping = KERNEL_VM_MAPPING_UNMAPPED;
    simulated_bus_present = false;
    assert(kernel_fault_capture(&frame, &report));
    assert(report.kind == KERNEL_FAULT_PMMU_TRANSLATION);
}

int main(void)
{
    test_pmmu_and_invalid_classification();
    test_matching_physical_classes();
    test_stale_record_never_overrides_pmmu();
    test_runtime_capture_uses_ssw_function_code();
    puts("fault classification tests passed");
    return 0;
}
