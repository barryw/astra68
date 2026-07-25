#include "handle.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define RIGHT_QUERY   (1u << 0)
#define RIGHT_CONTROL (1u << 1)
#define RIGHT_TRANSFER (1u << 5)

typedef struct ReleaseState {
    uint32_t calls;
    uintptr_t object_sum;
} ReleaseState;

typedef struct CloneState {
    uint32_t references;
    uint32_t retain_calls;
    uint32_t release_calls;
    bool reject_retain;
} CloneState;

static void release_object(void *object, void *context)
{
    ReleaseState *state = context;

    ++state->calls;
    state->object_sum += (uintptr_t)object;
}

static bool retain_clone(void *object, void *context)
{
    CloneState *state = context;

    (void)object;
    ++state->retain_calls;
    if (state->reject_retain)
        return false;
    ++state->references;
    return true;
}

static void release_clone(void *object, void *context)
{
    CloneState *state = context;

    (void)object;
    assert(state->references != 0u);
    --state->references;
    ++state->release_calls;
}

static void test_cloneable_rights_reduction_and_lifetime(void)
{
    KernelHandleTable table;
    CloneState state = {1u, 0u, 0u, false};
    KernelHandle source;
    KernelHandle reduced;
    KernelHandle denied = 0xdeadbeefu;
    void *object;

    kernel_handle_table_init(&table);
    assert(kernel_handle_install_cloneable(
               &table, KERNEL_OBJECT_AREA,
               RIGHT_QUERY | RIGHT_CONTROL | RIGHT_TRANSFER,
               (void *)(uintptr_t)0xabcdu, retain_clone, release_clone,
               &state, &source) == KERNEL_HANDLE_OK);
    assert(kernel_handle_duplicate(
               &table, source, RIGHT_QUERY | RIGHT_TRANSFER, &reduced) ==
           KERNEL_HANDLE_OK);
    assert(state.references == 2u && state.retain_calls == 1u);
    assert(kernel_handle_lookup(&table, reduced, KERNEL_OBJECT_AREA,
                                RIGHT_QUERY, &object) == KERNEL_HANDLE_OK);
    assert((uintptr_t)object == 0xabcdu);
    assert(kernel_handle_lookup(&table, reduced, KERNEL_OBJECT_AREA,
                                RIGHT_CONTROL, &object) ==
           KERNEL_HANDLE_ACCESS_DENIED);
    assert(kernel_handle_duplicate(
               &table, reduced, RIGHT_QUERY | RIGHT_CONTROL, &denied) ==
           KERNEL_HANDLE_ACCESS_DENIED);
    assert(denied == KERNEL_HANDLE_INVALID);
    assert(kernel_handle_duplicate(&table, source, 0u, &denied) ==
           KERNEL_HANDLE_INVALID_ARGUMENT);

    state.reject_retain = true;
    assert(kernel_handle_duplicate(&table, source, RIGHT_QUERY, &denied) ==
           KERNEL_HANDLE_INVALID_STATE);
    assert(state.references == 2u && state.retain_calls == 2u);
    state.reject_retain = false;
    assert(kernel_handle_close(&table, source) == KERNEL_HANDLE_OK);
    assert(state.references == 1u && state.release_calls == 1u);
    assert(kernel_handle_close(&table, reduced) == KERNEL_HANDLE_OK);
    assert(state.references == 0u && state.release_calls == 2u);

    assert(kernel_handle_install_cloneable(
               &table, KERNEL_OBJECT_AREA, RIGHT_QUERY, &state, NULL,
               release_clone, &state, &source) ==
           KERNEL_HANDLE_INVALID_ARGUMENT);
}

static void test_lookup_rights_type_and_stale_reuse(void)
{
    KernelHandleTable table;
    ReleaseState released = {0};
    KernelHandle first;
    KernelHandle second;
    KernelObjectType type;
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
    assert(kernel_handle_lookup_any(&table, first, RIGHT_QUERY, &type,
                                    &object) == KERNEL_HANDLE_OK);
    assert(type == KERNEL_OBJECT_PROCESS);
    assert((uintptr_t)object == 0x1234u);
    assert(kernel_handle_lookup_any(&table, first, 1u << 7, &type,
                                    &object) ==
           KERNEL_HANDLE_ACCESS_DENIED);
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
    KernelObjectType type;
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
    assert(kernel_handle_lookup_any(&table, KERNEL_HANDLE_INVALID, 0u,
                                    &type, &object) ==
           KERNEL_HANDLE_INVALID_HANDLE);
    assert(kernel_handle_lookup_any(&table, KERNEL_HANDLE_INVALID, 0u,
                                    NULL, &object) ==
           KERNEL_HANDLE_INVALID_ARGUMENT);
    assert(kernel_handle_close(NULL, 1u) == KERNEL_HANDLE_INVALID_ARGUMENT);
}

static void test_atomic_export_import_and_cancel(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelHandleTransferBatch batch;
    KernelHandleImportReservation reservation;
    KernelHandleTransferStats stats;
    ReleaseState released = {0};
    KernelHandle source_handles[2];
    KernelDetachedHandle detached[2];
    void *object;

    kernel_handle_transfer_pool_init();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    for (uint32_t index = 0u; index < 2u; ++index) {
        assert(kernel_handle_install(
                   &source, KERNEL_OBJECT_SYNC,
                   RIGHT_QUERY | RIGHT_TRANSFER,
                   (void *)(uintptr_t)(0x1000u + index), release_object,
                   &released, &source_handles[index]) == KERNEL_HANDLE_OK);
    }

    assert(kernel_handle_transfer_prepare(
               &source, source_handles, 2u, RIGHT_TRANSFER,
               &batch) == KERNEL_HANDLE_OK);
    assert(kernel_handle_transfer_stats(&stats));
    assert(stats.reserved_detached == 2u);
    assert(stats.live_detached == 0u);
    for (uint32_t index = 0u; index < 2u; ++index) {
        assert(kernel_handle_lookup(&source, source_handles[index],
                                    KERNEL_OBJECT_SYNC, RIGHT_QUERY,
                                    &object) == KERNEL_HANDLE_OK);
        detached[index] = batch.detached[index];
    }

    assert(kernel_handle_transfer_commit_export(&source, &batch) ==
           KERNEL_HANDLE_OK);
    assert(kernel_handle_count(&source) == 0u);
    assert(released.calls == 0u);
    assert(kernel_handle_transfer_stats(&stats));
    assert(stats.reserved_detached == 0u);
    assert(stats.live_detached == 2u);
    for (uint32_t index = 0u; index < 2u; ++index) {
        assert(kernel_handle_lookup(&source, source_handles[index],
                                    KERNEL_OBJECT_SYNC, 0u, &object) ==
               KERNEL_HANDLE_INVALID_HANDLE);
    }

    assert(kernel_handle_import_reserve(
               &destination, detached, 2u, &reservation) == KERNEL_HANDLE_OK);
    assert(kernel_handle_available(&destination) ==
           KERNEL_HANDLE_MAX_ENTRIES - 2u);
    for (uint32_t index = 0u; index < 2u; ++index) {
        assert(kernel_handle_lookup(&destination, reservation.handles[index],
                                    KERNEL_OBJECT_SYNC, RIGHT_QUERY,
                                    &object) == KERNEL_HANDLE_INVALID_HANDLE);
    }
    assert(kernel_handle_import_cancel(&destination, &reservation) ==
           KERNEL_HANDLE_OK);
    assert(kernel_handle_available(&destination) ==
           KERNEL_HANDLE_MAX_ENTRIES);
    assert(kernel_handle_transfer_stats(&stats));
    assert(stats.live_detached == 2u);

    assert(kernel_handle_import_reserve(
               &destination, detached, 2u, &reservation) == KERNEL_HANDLE_OK);
    KernelHandle imported[2] = {
        reservation.handles[0], reservation.handles[1]
    };
    assert(kernel_handle_import_commit(&destination, &reservation,
                                       detached) == KERNEL_HANDLE_OK);
    assert(kernel_handle_count(&destination) == 2u);
    assert(kernel_handle_transfer_stats(&stats));
    assert(stats.live_detached == 0u);
    for (uint32_t index = 0u; index < 2u; ++index) {
        assert(kernel_handle_lookup(&destination, imported[index],
                                    KERNEL_OBJECT_SYNC,
                                    RIGHT_QUERY | RIGHT_TRANSFER,
                                    &object) == KERNEL_HANDLE_OK);
        assert((uintptr_t)object == 0x1000u + index);
        assert(kernel_handle_close(&destination, imported[index]) ==
               KERNEL_HANDLE_OK);
    }
    assert(released.calls == 2u);
    assert(released.object_sum == 0x2001u);
    assert(kernel_handle_transfer_pool_valid());
}

static void test_export_validation_and_rollback(void)
{
    KernelHandleTable table;
    KernelHandleTransferBatch batch;
    KernelHandleTransferStats stats;
    ReleaseState released = {0};
    KernelHandle transferable;
    KernelHandle fixed;
    KernelHandle duplicate[2];
    void *object;

    kernel_handle_transfer_pool_init();
    kernel_handle_table_init(&table);
    assert(kernel_handle_install(
               &table, KERNEL_OBJECT_SYNC, RIGHT_QUERY | RIGHT_TRANSFER,
               (void *)(uintptr_t)0x2222u, release_object, &released,
               &transferable) == KERNEL_HANDLE_OK);
    assert(kernel_handle_install(
               &table, KERNEL_OBJECT_SYNC, RIGHT_QUERY,
               (void *)(uintptr_t)0x3333u, release_object, &released,
               &fixed) == KERNEL_HANDLE_OK);

    duplicate[0] = transferable;
    duplicate[1] = transferable;
    assert(kernel_handle_transfer_prepare(
               &table, duplicate, 2u, RIGHT_TRANSFER, &batch) ==
           KERNEL_HANDLE_DUPLICATE);
    assert(kernel_handle_transfer_prepare(
               &table, &fixed, 1u, RIGHT_TRANSFER, &batch) ==
           KERNEL_HANDLE_ACCESS_DENIED);
    assert(kernel_handle_transfer_prepare(
               &table, &transferable, 1u, RIGHT_TRANSFER,
               &batch) == KERNEL_HANDLE_OK);
    KernelDetachedHandle stale = batch.detached[0];
    assert(kernel_handle_transfer_rollback(&batch) == KERNEL_HANDLE_OK);
    assert(kernel_handle_lookup(&table, transferable, KERNEL_OBJECT_SYNC,
                                RIGHT_QUERY, &object) == KERNEL_HANDLE_OK);
    assert((uintptr_t)object == 0x2222u);
    assert(kernel_handle_import_reserve(&table, &stale, 1u, NULL) ==
           KERNEL_HANDLE_INVALID_ARGUMENT);
    KernelHandleImportReservation reservation;
    assert(kernel_handle_import_reserve(&table, &stale, 1u, &reservation) ==
           KERNEL_HANDLE_INVALID_HANDLE);
    assert(kernel_handle_transfer_stats(&stats));
    assert(stats.export_rollbacks == 1u);
    assert(stats.live_detached == 0u);
    assert(stats.reserved_detached == 0u);
    assert(released.calls == 0u);

    assert(kernel_handle_close(&table, transferable) == KERNEL_HANDLE_OK);
    assert(kernel_handle_close(&table, fixed) == KERNEL_HANDLE_OK);
    assert(released.calls == 2u);
    assert(kernel_handle_transfer_pool_valid());
}

static void test_destination_full_and_detached_release(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelHandleTransferBatch batch;
    KernelHandleImportReservation reservation;
    ReleaseState released = {0};
    KernelHandle source_handle;
    KernelHandle fillers[KERNEL_HANDLE_MAX_ENTRIES];
    KernelDetachedHandle detached;

    kernel_handle_transfer_pool_init();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    assert(kernel_handle_install(
               &source, KERNEL_OBJECT_SYNC, RIGHT_QUERY | RIGHT_TRANSFER,
               (void *)(uintptr_t)0x4444u, release_object, &released,
               &source_handle) == KERNEL_HANDLE_OK);
    assert(kernel_handle_transfer_prepare(
               &source, &source_handle, 1u, RIGHT_TRANSFER,
               &batch) == KERNEL_HANDLE_OK);
    detached = batch.detached[0];
    assert(kernel_handle_transfer_commit_export(&source, &batch) ==
           KERNEL_HANDLE_OK);
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        assert(kernel_handle_install(
                   &destination, KERNEL_OBJECT_DEVICE, RIGHT_QUERY,
                   (void *)(uintptr_t)(0x5000u + index), release_object,
                   &released, &fillers[index]) == KERNEL_HANDLE_OK);
    }
    assert(kernel_handle_import_reserve(
               &destination, &detached, 1u, &reservation) ==
           KERNEL_HANDLE_TABLE_FULL);
    assert(kernel_handle_detached_release(&detached, 1u) == KERNEL_HANDLE_OK);
    assert(released.calls == 1u);
    assert(released.object_sum == 0x4444u);
    assert(kernel_handle_detached_release(&detached, 1u) ==
           KERNEL_HANDLE_INVALID_HANDLE);
    assert(kernel_handle_close_all(&destination) ==
           KERNEL_HANDLE_MAX_ENTRIES);
    assert(released.calls == KERNEL_HANDLE_MAX_ENTRIES + 1u);
    assert(kernel_handle_transfer_pool_valid());
}

static void test_detached_pool_exhaustion_and_reuse(void)
{
    KernelHandleTable table;
    KernelHandleTransferBatch batch;
    KernelDetachedHandle detached[KERNEL_HANDLE_DETACHED_MAX];
    ReleaseState released = {0};
    uint32_t detached_count = 0u;
    void *object;

    kernel_handle_transfer_pool_init();
    kernel_handle_table_init(&table);
    for (uint32_t group = 0u;
         group < KERNEL_HANDLE_DETACHED_MAX / KERNEL_HANDLE_TRANSFER_MAX;
         ++group) {
        KernelHandle handles[KERNEL_HANDLE_TRANSFER_MAX];

        for (uint32_t index = 0u; index < KERNEL_HANDLE_TRANSFER_MAX; ++index) {
            uintptr_t value = 1u + detached_count + index;

            assert(kernel_handle_install(
                       &table, KERNEL_OBJECT_SYNC,
                       RIGHT_QUERY | RIGHT_TRANSFER, (void *)value,
                       release_object, &released, &handles[index]) ==
                   KERNEL_HANDLE_OK);
        }
        assert(kernel_handle_transfer_prepare(
                   &table, handles, KERNEL_HANDLE_TRANSFER_MAX,
                   RIGHT_TRANSFER, &batch) == KERNEL_HANDLE_OK);
        assert(kernel_handle_transfer_commit_export(&table, &batch) ==
               KERNEL_HANDLE_OK);
        for (uint32_t index = 0u; index < KERNEL_HANDLE_TRANSFER_MAX; ++index)
            detached[detached_count++] = batch.detached[index];
    }
    assert(detached_count == KERNEL_HANDLE_DETACHED_MAX);
    KernelHandle extra;
    assert(kernel_handle_install(
               &table, KERNEL_OBJECT_SYNC, RIGHT_QUERY | RIGHT_TRANSFER,
               (void *)(uintptr_t)0x7777u, release_object, &released,
               &extra) == KERNEL_HANDLE_OK);
    assert(kernel_handle_transfer_prepare(
               &table, &extra, 1u, RIGHT_TRANSFER, &batch) ==
           KERNEL_HANDLE_TRANSFER_POOL_FULL);
    assert(kernel_handle_lookup(&table, extra, KERNEL_OBJECT_SYNC,
                                RIGHT_QUERY, &object) ==
           KERNEL_HANDLE_OK);
    assert(kernel_handle_close(&table, extra) == KERNEL_HANDLE_OK);

    assert(kernel_handle_detached_release(detached, 4u) == KERNEL_HANDLE_OK);
    KernelHandle partial[KERNEL_HANDLE_TRANSFER_MAX];
    for (uint32_t index = 0u; index < KERNEL_HANDLE_TRANSFER_MAX; ++index) {
        assert(kernel_handle_install(
                   &table, KERNEL_OBJECT_SYNC,
                   RIGHT_QUERY | RIGHT_TRANSFER,
                   (void *)(uintptr_t)(0x9000u + index), release_object,
                   &released, &partial[index]) == KERNEL_HANDLE_OK);
    }
    assert(kernel_handle_transfer_prepare(
               &table, partial, KERNEL_HANDLE_TRANSFER_MAX,
               RIGHT_TRANSFER, &batch) == KERNEL_HANDLE_TRANSFER_POOL_FULL);
    for (uint32_t index = 0u; index < KERNEL_HANDLE_TRANSFER_MAX; ++index) {
        assert(kernel_handle_lookup(&table, partial[index],
                                    KERNEL_OBJECT_SYNC, RIGHT_TRANSFER,
                                    &object) == KERNEL_HANDLE_OK);
    }
    assert(kernel_handle_close_all(&table) == KERNEL_HANDLE_TRANSFER_MAX);
    KernelHandleTransferStats stats;
    assert(kernel_handle_transfer_stats(&stats));
    assert(stats.live_detached == KERNEL_HANDLE_DETACHED_MAX - 4u);
    assert(stats.reserved_detached == 0u);
    assert(stats.pool_exhaustions == 2u);

    for (uint32_t offset = 4u; offset < detached_count;) {
        uint32_t count = detached_count - offset;

        if (count > KERNEL_HANDLE_TRANSFER_MAX)
            count = KERNEL_HANDLE_TRANSFER_MAX;
        assert(kernel_handle_detached_release(
                   &detached[offset], count) ==
               KERNEL_HANDLE_OK);
        offset += count;
    }
    assert(released.calls == KERNEL_HANDLE_DETACHED_MAX + 1u +
                                 KERNEL_HANDLE_TRANSFER_MAX);
    assert(kernel_handle_transfer_pool_valid());

    KernelHandle replacement;
    assert(kernel_handle_install(
               &table, KERNEL_OBJECT_SYNC, RIGHT_QUERY | RIGHT_TRANSFER,
               (void *)(uintptr_t)0x8888u, release_object, &released,
               &replacement) == KERNEL_HANDLE_OK);
    assert(kernel_handle_transfer_prepare(
               &table, &replacement, 1u, RIGHT_TRANSFER,
               &batch) == KERNEL_HANDLE_OK);
    assert(batch.detached[0] != detached[0]);
    assert(kernel_handle_transfer_rollback(&batch) == KERNEL_HANDLE_OK);
    assert(kernel_handle_close(&table, replacement) == KERNEL_HANDLE_OK);
}

int main(void)
{
    kernel_handle_transfer_pool_init();
    assert(kernel_handle_transfer_pool_healthy());
    test_lookup_rights_type_and_stale_reuse();
    test_capacity_and_close_all();
    test_invalid_arguments();
    test_cloneable_rights_reduction_and_lifetime();
    test_atomic_export_import_and_cancel();
    test_export_validation_and_rollback();
    test_destination_full_and_detached_release();
    test_detached_pool_exhaustion_and_reuse();
    assert(kernel_handle_transfer_pool_healthy());
    puts("handle tests passed");
    return 0;
}
