#include <astra/limits.h>
#include <astra/runtime.h>

#include <errno.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void *
malloc(size_t size)
{
    return astra_runtime_allocate(size);
}

void
free(void *pointer)
{
    astra_runtime_deallocate(pointer);
}

void *
calloc(size_t count, size_t size)
{
    return astra_runtime_callocate(count, size);
}

void *
realloc(void *pointer, size_t size)
{
    return astra_runtime_reallocate(pointer, size);
}

size_t
malloc_usable_size(void *pointer)
{
    return astra_runtime_allocation_size(pointer);
}

void *
memalign(size_t alignment, size_t size)
{
    void *result;

    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        errno = EINVAL;
        return NULL;
    }
    result = astra_runtime_allocate_aligned(alignment, size);
    if (result == NULL)
        errno = ENOMEM;
    return result;
}

void *
aligned_alloc(size_t alignment, size_t size)
{
    if (alignment == 0u || size % alignment != 0u) {
        errno = EINVAL;
        return NULL;
    }
    return memalign(alignment, size);
}

int
posix_memalign(void **result, size_t alignment, size_t size)
{
    void *allocated;

    if (result == NULL || alignment < sizeof(void *) ||
        alignment % sizeof(void *) != 0u ||
        (alignment & (alignment - 1u)) != 0u)
        return EINVAL;
    allocated = astra_runtime_allocate_aligned(alignment, size);
    if (allocated == NULL)
        return ENOMEM;
    *result = allocated;
    return 0;
}

void
cfree(void *pointer)
{
    free(pointer);
}

void *
valloc(size_t size)
{
    return memalign(ASTRA_MEMORY_PAGE_SIZE, size);
}

void *
pvalloc(size_t size)
{
    size_t page = ASTRA_MEMORY_PAGE_SIZE;
    size_t rounded;

    if (size > SIZE_MAX - (page - 1u)) {
        errno = ENOMEM;
        return NULL;
    }
    rounded = (size + page - 1u) & ~(page - 1u);
    if (rounded == 0u)
        rounded = page;
    return memalign(page, rounded);
}
