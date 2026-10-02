#ifndef ASTRA_RUNTIME_ALLOCATOR_INTERNAL_H
#define ASTRA_RUNTIME_ALLOCATOR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define ASTRA_ALLOCATOR_CONTROL_MAGIC 0x41485031u /* "AHP1" */
#define ASTRA_ALLOCATOR_CONTROL_VERSION 3u
#define ASTRA_ALLOCATOR_CLASS_COUNT 20u
/* Freed pages kept committed for reuse before frames go back. */
#define ASTRA_ALLOCATOR_RETAIN_PAGES 256u

typedef struct AstraAllocatorLayout {
    uint8_t *base;
    uint32_t page_count;
    uint32_t *page_run;
    uint32_t *extent_next;
    uint32_t *extent_pages;
} AstraAllocatorLayout;

/*
 * Process-owned allocator state.  This cannot live in a library's .bss: the
 * bootstrap loader and runtime.library are different images in the same
 * process, and both must observe exactly the same heap.  On target this
 * record lives at ASTRA_PROCESS_HEAP_CONTROL_START, whose root slot the
 * kernel reserves when it creates the address space.  Host tests use one
 * ordinary static record.
 */
typedef struct AstraAllocatorControl {
    _Alignas(4) volatile uint32_t lock;
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    AstraAllocatorLayout layout;
    uint32_t heap_span;
    uint32_t heap_used;
    uint32_t heap_pages_taken;
    uint32_t free_extent_head;
    /* Freed extents still committed, newest first, and their pages. */
    uint32_t retained_head;
    uint32_t retained_pages;
    void *class_runs[ASTRA_ALLOCATOR_CLASS_COUNT];
    int (*growth_policy)(uint32_t current_bytes, uint32_t growth_bytes,
                         void *context);
    void *growth_policy_context;
} AstraAllocatorControl;

AstraAllocatorControl *astra_allocator_control(void);
int astra_allocator_control_acquire(AstraAllocatorControl **control);
void astra_allocator_control_release(AstraAllocatorControl *control);
void *astra_runtime_sbrk_locked(AstraAllocatorControl *control,
                                intptr_t increment);

#endif
