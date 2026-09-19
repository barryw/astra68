#define _GNU_SOURCE 1

#include <astra/limits.h>
#include <astra/runtime.h>

#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static _Alignas(256) unsigned char storage[512];
static volatile size_t seen_alignment;
static volatile size_t seen_size;
static int fail_allocation;

void cfree(void *pointer);

void *
astra_runtime_allocate_aligned(size_t alignment, size_t size)
{
    seen_alignment = alignment;
    seen_size = size;
    return fail_allocation ? NULL : storage;
}

void *astra_runtime_allocate(size_t size) { seen_size = size; return storage; }
void astra_runtime_deallocate(void *pointer) { assert(pointer == storage); }
void *astra_runtime_callocate(size_t count, size_t size)
{
    seen_size = count * size;
    return storage;
}
void *astra_runtime_reallocate(void *pointer, size_t size)
{
    assert(pointer == storage);
    seen_size = size;
    return storage;
}
size_t astra_runtime_allocation_size(void *pointer)
{
    assert(pointer == storage);
    return sizeof(storage);
}

int
main(void)
{
    void *result = (void *)(uintptr_t)1u;

    errno = 0;
    assert(memalign(64u, 17u) != NULL);
    assert(seen_alignment == 64u);
    assert(seen_size == 17u);
    assert(memalign(3u, 17u) == NULL && errno == EINVAL);

    errno = 0;
    assert(aligned_alloc(64u, 128u) != NULL);
    assert(seen_alignment == 64u);
    assert(seen_size == 128u);
    assert(aligned_alloc(64u, 127u) == NULL && errno == EINVAL);

    assert(posix_memalign(&result, 64u, 31u) == 0);
    assert(result != NULL && result != (void *)(uintptr_t)1u);
    result = (void *)(uintptr_t)1u;
    assert(posix_memalign(&result, 3u, 31u) == EINVAL);
    assert(result == (void *)(uintptr_t)1u);
    fail_allocation = 1;
    assert(posix_memalign(&result, 64u, 31u) == ENOMEM);
    assert(result == (void *)(uintptr_t)1u);
    fail_allocation = 0;

    assert(valloc(17u) != NULL);
    assert(seen_alignment == ASTRA_MEMORY_PAGE_SIZE && seen_size == 17u);
    assert(pvalloc(ASTRA_MEMORY_PAGE_SIZE + 1u) != NULL);
    assert(seen_alignment == ASTRA_MEMORY_PAGE_SIZE &&
           seen_size == ASTRA_MEMORY_PAGE_SIZE * 2u);
    assert(pvalloc(0u) != NULL && seen_size == ASTRA_MEMORY_PAGE_SIZE);
    errno = 0;
    assert(pvalloc(SIZE_MAX) == NULL && errno == ENOMEM);

    cfree(storage);

    puts("ASTRA POSIX ALLOCATION PASS");
    return 0;
}
