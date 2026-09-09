#define _XOPEN_SOURCE 700

#include "resource_internal.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define ASTRA_POSIX_STACK_BYTES 4096u

enum {
    POSIX_RESOURCE_CPU,
    POSIX_RESOURCE_FSIZE,
    POSIX_RESOURCE_DATA,
    POSIX_RESOURCE_STACK,
    POSIX_RESOURCE_CORE,
    POSIX_RESOURCE_NOFILE,
    POSIX_RESOURCE_AS
};

static const AstraPosixResourceState defaults = {
    .current = {
        RLIM_INFINITY, RLIM_INFINITY, RLIM_INFINITY,
        ASTRA_POSIX_STACK_BYTES, 0u, RLIM_INFINITY, RLIM_INFINITY
    },
    .maximum = {
        RLIM_INFINITY, RLIM_INFINITY, RLIM_INFINITY,
        ASTRA_POSIX_STACK_BYTES, 0u, RLIM_INFINITY, RLIM_INFINITY
    }
};
static AstraPosixResourceState limits;

static int
resource_slot(int resource)
{
    switch (resource) {
    case RLIMIT_CPU: return POSIX_RESOURCE_CPU;
    case RLIMIT_FSIZE: return POSIX_RESOURCE_FSIZE;
    case RLIMIT_DATA: return POSIX_RESOURCE_DATA;
    case RLIMIT_STACK: return POSIX_RESOURCE_STACK;
    case RLIMIT_CORE: return POSIX_RESOURCE_CORE;
    case RLIMIT_NOFILE: return POSIX_RESOURCE_NOFILE;
    case RLIMIT_AS: return POSIX_RESOURCE_AS;
    default: return -1;
    }
}

void
astra_posix_resource_reset(void)
{
    limits = defaults;
}

void
astra_posix_resource_export(AstraPosixResourceState *state)
{
    if (state != NULL)
        *state = limits;
}

int
astra_posix_resource_validate(const AstraPosixResourceState *state)
{
    if (state == NULL)
        return 0;
    for (uint32_t resource = 0u; resource < ASTRA_POSIX_RESOURCE_COUNT;
         ++resource)
        if (state->current[resource] > state->maximum[resource])
            return 0;
    return state->current[POSIX_RESOURCE_CPU] == RLIM_INFINITY &&
           state->maximum[POSIX_RESOURCE_CPU] == RLIM_INFINITY &&
           state->current[POSIX_RESOURCE_STACK] == ASTRA_POSIX_STACK_BYTES &&
           state->maximum[POSIX_RESOURCE_STACK] == ASTRA_POSIX_STACK_BYTES &&
           state->current[POSIX_RESOURCE_CORE] == 0u &&
           state->maximum[POSIX_RESOURCE_CORE] == 0u &&
           state->current[POSIX_RESOURCE_AS] == RLIM_INFINITY &&
           state->maximum[POSIX_RESOURCE_AS] == RLIM_INFINITY;
}

void
astra_posix_resource_import(const AstraPosixResourceState *state)
{
    limits = *state;
}

#if defined(__GNUC__) && !defined(__clang__)
/* glibc marks these pointers nonnull; Astra returns EFAULT at the ABI edge. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
int
getrlimit(int resource, struct rlimit *value)
{
    int slot;

    if (value == NULL) {
        errno = EFAULT;
        return -1;
    }
    slot = resource_slot(resource);
    if (slot < 0) {
        errno = EINVAL;
        return -1;
    }
    value->rlim_cur = (rlim_t)limits.current[slot];
    value->rlim_max = (rlim_t)limits.maximum[slot];
    return 0;
}

int
setrlimit(int resource, const struct rlimit *value)
{
    int slot;

    if (value == NULL) {
        errno = EFAULT;
        return -1;
    }
    slot = resource_slot(resource);
    if (slot < 0 || value->rlim_cur > value->rlim_max) {
        errno = EINVAL;
        return -1;
    }
    if (value->rlim_max < limits.maximum[slot]) {
        if (resource == RLIMIT_CPU || resource == RLIMIT_STACK ||
            resource == RLIMIT_CORE || resource == RLIMIT_AS) {
            errno = ENOTSUP;
            return -1;
        }
    } else if (value->rlim_max > limits.maximum[slot]) {
        errno = EPERM;
        return -1;
    }
    if ((resource == RLIMIT_CPU || resource == RLIMIT_STACK ||
         resource == RLIMIT_CORE || resource == RLIMIT_AS) &&
        (value->rlim_cur != limits.current[slot] ||
         value->rlim_max != limits.maximum[slot])) {
        errno = ENOTSUP;
        return -1;
    }
    limits.current[slot] = value->rlim_cur;
    limits.maximum[slot] = value->rlim_max;
    return 0;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

uint32_t
astra_posix_resource_nofile(void)
{
    uint64_t value = limits.current[POSIX_RESOURCE_NOFILE];
    uint64_t representable = (uint64_t)INT_MAX + 1u;

    return (uint32_t)(value < representable ? value : representable);
}

int
astra_posix_resource_heap_allows(uint64_t static_bytes,
                                 uint32_t used, uint32_t growth)
{
    uint64_t limit = limits.current[POSIX_RESOURCE_DATA];
    uint64_t occupied = static_bytes + used;

    return limit == RLIM_INFINITY ||
           (occupied <= limit && growth <= limit - occupied);
}

rlim_t
astra_posix_resource_file_size(void)
{
    return (rlim_t)limits.current[POSIX_RESOURCE_FSIZE];
}
