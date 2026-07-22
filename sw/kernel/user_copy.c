#include "user_copy.h"

#include "exception.h"
#include "vm.h"

#include <stddef.h>

#define M68K_SSW_DATA_FAULT 0x0100u
#define M68K_SSW_READ       0x0040u
#define M68K_SSW_FC_MASK    0x0007u
#define M68K_FC_USER_DATA   0x0001u
#define M68K_BUS_ERROR_VECTOR_OFFSET 0x0008u

#if !KERNEL_USER_COPY_HOST_TEST
extern const KernelUserCopyFaultSite _kernel_user_copy_sites_start[];
extern const KernelUserCopyFaultSite _kernel_user_copy_sites_end[];
#endif

static KernelUserCopyFaultScope active_scope;

static bool checked_user_range(uint32_t address, uint32_t size,
                               uint32_t *end)
{
    if (size == 0u) {
        *end = address;
        return true;
    }
    if (address < KERNEL_VM_USER_MIN ||
        address > KERNEL_VM_USER_MAX - (size - 1u))
        return false;
    *end = address + size;
    return true;
}

static int begin_copy(uint32_t address, uint32_t size, uint32_t direction,
                      uint32_t *end)
{
    if (size > KERNEL_USER_COPY_MAX_BYTES)
        return KERNEL_USER_COPY_TOO_LARGE;
    if (!checked_user_range(address, size, end))
        return KERNEL_USER_COPY_BAD_ADDRESS;
    if (active_scope.active != 0u)
        return KERNEL_USER_COPY_BUSY;
    active_scope.start = address;
    active_scope.end = *end;
    active_scope.direction = direction;
    active_scope.active = 1u;
    return KERNEL_USER_COPY_OK;
}

static void end_copy(void)
{
    active_scope.active = 0u;
    active_scope.start = 0u;
    active_scope.end = 0u;
    active_scope.direction = 0u;
}

int kernel_copy_from_user(void *kernel_destination, uint32_t user_source,
                          uint32_t size)
{
    uint32_t end;
    int status;

    if (size == 0u)
        return KERNEL_USER_COPY_OK;
    if (kernel_destination == NULL)
        return KERNEL_USER_COPY_INVALID_ARGUMENT;
    status = begin_copy(user_source, size, KERNEL_USER_COPY_FROM_USER, &end);
    if (status != KERNEL_USER_COPY_OK)
        return status;
    status = kernel_user_copy_from_asm(kernel_destination, user_source, size);
    end_copy();
    return status;
}

int kernel_copy_to_user(uint32_t user_destination, const void *kernel_source,
                        uint32_t size)
{
    uint32_t end;
    int status;

    if (size == 0u)
        return KERNEL_USER_COPY_OK;
    if (kernel_source == NULL)
        return KERNEL_USER_COPY_INVALID_ARGUMENT;
    status = begin_copy(user_destination, size, KERNEL_USER_COPY_TO_USER, &end);
    if (status != KERNEL_USER_COPY_OK)
        return status;
    status = kernel_user_copy_to_asm(user_destination, kernel_source, size);
    end_copy();
    return status;
}

bool kernel_user_copy_recover_frame(
    void *raw_frame, uint32_t available,
    const KernelUserCopyFaultScope *scope,
    const KernelUserCopyFaultSite *sites, uint32_t site_count)
{
    KernelExceptionFrame frame;
    const KernelUserCopyFaultSite *match = NULL;

    if (scope == NULL || sites == NULL || scope->active == 0u ||
        scope->start >= scope->end ||
        (scope->direction != KERNEL_USER_COPY_FROM_USER &&
         scope->direction != KERNEL_USER_COPY_TO_USER))
        return false;
    if (kernel_exception_decode(raw_frame, available, &frame) !=
        KERNEL_EXCEPTION_OK)
        return false;
    if (frame.vector_offset != M68K_BUS_ERROR_VECTOR_OFFSET ||
        frame.from_user != 0u || frame.access_fault == 0u ||
        (frame.special_status & M68K_SSW_DATA_FAULT) == 0u ||
        (frame.special_status & M68K_SSW_FC_MASK) != M68K_FC_USER_DATA ||
        frame.fault_address < scope->start ||
        frame.fault_address >= scope->end)
        return false;
    if (scope->direction == KERNEL_USER_COPY_FROM_USER &&
        (frame.special_status & M68K_SSW_READ) == 0u)
        return false;
    if (scope->direction == KERNEL_USER_COPY_TO_USER &&
        (frame.special_status & M68K_SSW_READ) != 0u)
        return false;

    for (uint32_t index = 0u; index < site_count; ++index) {
        if (sites[index].direction == scope->direction &&
            frame.program_counter >= sites[index].start_pc &&
            frame.program_counter <= sites[index].end_pc) {
            match = &sites[index];
            break;
        }
    }
    if (match == NULL)
        return false;
    return kernel_exception_set_program_counter(
               raw_frame, available, match->fixup_pc) == KERNEL_EXCEPTION_OK;
}

bool kernel_user_copy_handle_fault(void *raw_frame)
{
#if KERNEL_USER_COPY_HOST_TEST
    (void)raw_frame;
    return false;
#else
    uint32_t site_count = (uint32_t)(_kernel_user_copy_sites_end -
                                     _kernel_user_copy_sites_start);

    return kernel_user_copy_recover_frame(
        raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE, &active_scope,
        _kernel_user_copy_sites_start, site_count);
#endif
}
