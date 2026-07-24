#include "handle.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define RIGHT_QUERY   (1u << 0)
#define RIGHT_CONTROL (1u << 1)

typedef struct ReleaseState {
    uint32_t calls;
    uintptr_t object_sum;
} ReleaseState;

static void release_object(void *object, void *context)
{
    ReleaseState *state = context;

    ++state->calls;
    state->object_sum += (uintptr_t)object;
}

static void test_lookup_rights_type_and_stale_reuse(void)
{
    KernelHandleTable table;
    ReleaseState released = {0};
    KernelHandle first;
    KernelHandle second;
    void *object;

    kernel_handle_table_init(&table);
    assert(kernel_handle_count(&table) == 0u);
    assert(kernel_handle_install(&table, KERNEL_OBJECT_PROCESS,
                                 RIGHT_QUERY | RIGHT_CONTROL,
                                 (void *)(uintptr_t)0x1234u, release_object,
                                 &released, &first) == KERNEL_HANDLE_OK);
    assert(first != KERNEL_HANDLE_INVALID);
    assert(kernel_handle_count(&table) == 1u);
    assert(kernel_handle_lookup(&table, first, KERNEL_OBJECT_PROCESS,
                                RIGHT_QUERY, &object) == KERNEL_HANDLE_OK);
    assert((uintptr_t)object == 0x1234u);
    assert(kernel_handle_lookup(&table, first, KERNEL_OBJECT_THREAD,
                                RIGHT_QUERY, &object) ==
           KERNEL_HANDLE_TYPE_MISMATCH);
    assert(kernel_handle_lookup(&table, first, KERNEL_OBJECT_PROCESS,
                                1u << 7, &object) ==
           KERNEL_HANDLE_ACCESS_DENIED);

    assert(kernel_handle_close(&table, first) == KERNEL_HANDLE_OK);
    assert(released.calls == 1u);
    assert(released.object_sum == 0x1234u);
    assert(kernel_handle_close(&table, first) == KERNEL_HANDLE_INVALID_HANDLE);
    assert(kernel_handle_lookup(&table, first, KERNEL_OBJECT_NONE, 0u,
                                &object) == KERNEL_HANDLE_INVALID_HANDLE);

    assert(kernel_handle_install(&table, KERNEL_OBJECT_PROCESS, RIGHT_QUERY,
                                 (void *)(uintptr_t)0x5678u, release_object,
                                 &released, &second) == KERNEL_HANDLE_OK);
    assert(second != first);
    assert(kernel_handle_lookup(&table, first, KERNEL_OBJECT_NONE, 0u,
                                &object) == KERNEL_HANDLE_INVALID_HANDLE);
    assert(kernel_handle_close(&table, second) == KERNEL_HANDLE_OK);
    assert(released.calls == 2u);
    assert(released.object_sum == 0x68acu);
}

static void test_capacity_and_close_all(void)
{
    KernelHandleTable table;
    ReleaseState released = {0};
    KernelHandle handles[KERNEL_HANDLE_MAX_ENTRIES];
    KernelHandle extra = 0xdeadbeefu;
    void *object;

    kernel_handle_table_init(&table);
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        assert(kernel_handle_install(
                   &table, KERNEL_OBJECT_SYNC, RIGHT_QUERY,
                   (void *)(uintptr_t)(index + 1u), release_object, &released,
                   &handles[index]) == KERNEL_HANDLE_OK);
    }
    assert(kernel_handle_count(&table) == KERNEL_HANDLE_MAX_ENTRIES);
    assert(kernel_handle_install(&table, KERNEL_OBJECT_SYNC, RIGHT_QUERY,
                                 (void *)(uintptr_t)99u, NULL, NULL,
                                 &extra) == KERNEL_HANDLE_TABLE_FULL);
    assert(extra == KERNEL_HANDLE_INVALID);
    assert(kernel_handle_close_all(&table) == KERNEL_HANDLE_MAX_ENTRIES);
    assert(kernel_handle_count(&table) == 0u);
    assert(released.calls == KERNEL_HANDLE_MAX_ENTRIES);
    assert(released.object_sum ==
           KERNEL_HANDLE_MAX_ENTRIES * (KERNEL_HANDLE_MAX_ENTRIES + 1u) / 2u);
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        assert(kernel_handle_lookup(&table, handles[index],
                                    KERNEL_OBJECT_SYNC, RIGHT_QUERY,
                                    &object) == KERNEL_HANDLE_INVALID_HANDLE);
    }
    assert(kernel_handle_close_all(&table) == 0u);
}

static void test_invalid_arguments(void)
{
    KernelHandleTable table;
    KernelHandle handle;
    void *object;

    kernel_handle_table_init(&table);
    assert(kernel_handle_install(NULL, KERNEL_OBJECT_PROCESS, RIGHT_QUERY,
                                 &table, NULL, NULL,
                                 &handle) == KERNEL_HANDLE_INVALID_ARGUMENT);
    assert(kernel_handle_install(&table, KERNEL_OBJECT_NONE, RIGHT_QUERY,
                                 &table, NULL, NULL,
                                 &handle) == KERNEL_HANDLE_INVALID_ARGUMENT);
    assert(kernel_handle_install(&table, KERNEL_OBJECT_PROCESS, 0u, &table,
                                 NULL, NULL,
                                 &handle) == KERNEL_HANDLE_INVALID_ARGUMENT);
    assert(kernel_handle_install(&table, KERNEL_OBJECT_PROCESS, RIGHT_QUERY,
                                 NULL, NULL, NULL,
                                 &handle) == KERNEL_HANDLE_INVALID_ARGUMENT);
    assert(kernel_handle_lookup(&table, KERNEL_HANDLE_INVALID,
                                KERNEL_OBJECT_NONE, 0u,
                                &object) == KERNEL_HANDLE_INVALID_HANDLE);
    assert(kernel_handle_lookup(&table, KERNEL_HANDLE_INVALID,
                                KERNEL_OBJECT_NONE, 0u,
                                NULL) == KERNEL_HANDLE_INVALID_ARGUMENT);
    assert(kernel_handle_close(NULL, 1u) == KERNEL_HANDLE_INVALID_ARGUMENT);
}

int main(void)
{
    test_lookup_rights_type_and_stale_reuse();
    test_capacity_and_close_all();
    test_invalid_arguments();
    puts("handle tests passed");
    return 0;
}
