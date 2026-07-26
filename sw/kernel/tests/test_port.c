#include "port.h"

#include <astra/syscall.h>

#include "allocation.h"
#include "performance.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct TestObject {
    uint32_t releases;
    uint32_t value;
} TestObject;

static KernelHandle next_thread_handle = 0x00000101u;

static void test_release(void *object, void *context)
{
    TestObject *test = object;

    assert(context == (void *)(uintptr_t)0x51u);
    ++test->releases;
}

static void initialize_test(void)
{
    kernel_performance_init();
    kernel_thread_pool_init();
    kernel_handle_transfer_pool_init();
    kernel_port_pool_init();
    assert(kernel_port_pool_valid());
}

static void make_message(uint8_t *message, uint32_t size, uint8_t seed)
{
    assert(size >= KERNEL_PORT_MESSAGE_SIZE_MIN);
    for (uint32_t index = 0u; index < size; ++index)
        message[index] = (uint8_t)(seed + index);
}

static KernelHandle install_transfer_handle(KernelHandleTable *table,
                                            TestObject *object,
                                            uint32_t rights)
{
    KernelHandle handle;

    assert(kernel_handle_install(
               table, KERNEL_OBJECT_DEVICE, rights, object, test_release,
               (void *)(uintptr_t)0x51u, &handle) == KERNEL_HANDLE_OK);
    return handle;
}

static KernelThread *allocate_running_thread(uint16_t stack_slot)
{
    KernelThread *thread;

    assert(kernel_thread_allocate(
               0u, 0x10000001u, stack_slot,
               0x00100000u + (uint32_t)stack_slot * 2u,
               0x70001000u +
                   (uint32_t)stack_slot * KERNEL_THREAD_STACK_STRIDE,
               0u, KERNEL_THREAD_PRIORITY_NORMAL, &thread) ==
           KERNEL_THREAD_OK);
    assert(kernel_thread_attach_handle(thread, next_thread_handle) ==
           KERNEL_THREAD_OK);
    next_thread_handle += 0x00000100u;
    assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&thread) == KERNEL_THREAD_OK);
    return thread;
}

static void release_port(KernelPort *port)
{
    kernel_port_handle_release(
        port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_SEND);
    kernel_port_handle_release(
        port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_RECEIVE);
}

static void receive_and_commit(KernelPort *port,
                               KernelHandleTable *destination,
                               const uint8_t *expected,
                               uint32_t expected_size,
                               uint32_t expected_handles,
                               KernelPortReceipt *receipt)
{
    uint32_t required_size;
    uint32_t required_handles;
    uint32_t woken;

    assert(kernel_port_receive_prepare(
               port, destination, KERNEL_PORT_MESSAGE_SIZE_MAX,
               KERNEL_PORT_MESSAGE_HANDLE_MAX, receipt, &required_size,
               &required_handles) == KERNEL_PORT_OK);
    assert(required_size == expected_size);
    assert(required_handles == expected_handles);
    assert(receipt->message_size == expected_size);
    assert(receipt->handle_count == expected_handles);
    assert(memcmp(receipt->message, expected, expected_size) == 0);
    assert(kernel_port_receive_commit(receipt, &woken) == KERNEL_PORT_OK);
}

static void test_fifo_capacity_and_peer_drain(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelPortReceipt receipt;
    KernelPortSnapshot snapshot;
    KernelPort *port;
    uint8_t first[24];
    uint8_t second[40];
    uint32_t required_size;
    uint32_t required_handles;
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    make_message(first, sizeof(first), 0x10u);
    make_message(second, sizeof(second), 0x80u);
    assert(kernel_port_create(1u, 2u, 64u, &port) == KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, first, sizeof(first), NULL, 0u,
                            &woken) == KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, second, sizeof(second), NULL, 0u,
                            &woken) == KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, first, sizeof(first), NULL, 0u,
                            &woken) == KERNEL_PORT_WOULD_BLOCK);
    assert(kernel_port_snapshot(0u, &snapshot));
    assert(snapshot.queued_messages == 2u);
    assert(snapshot.queued_bytes == 64u);

    kernel_port_handle_release(
        port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_SEND);
    assert(kernel_port_snapshot(0u, &snapshot));
    assert(snapshot.state == KERNEL_PORT_PEER_CLOSED);
    receive_and_commit(port, &destination, first, sizeof(first), 0u,
                       &receipt);
    receive_and_commit(port, &destination, second, sizeof(second), 0u,
                       &receipt);
    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(first), 0u, &receipt,
               &required_size, &required_handles) ==
           KERNEL_PORT_PEER_DEAD);
    kernel_port_handle_release(
        port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_RECEIVE);
    assert(kernel_port_snapshot(0u, &snapshot));
    assert(snapshot.state == KERNEL_PORT_FREE);
    assert(kernel_port_pool_valid());
}

static void test_allocation_injection_preserves_queue(void)
{
    KernelAllocationStats message_allocation;
    KernelAllocationStats port_allocation;
    KernelHandleTable table;
    KernelPort *port = (KernelPort *)(uintptr_t)1u;
    KernelPortPoolStats before;
    KernelPortPoolStats after;
    uint8_t message[KERNEL_PORT_MESSAGE_SIZE_MIN];
    uint32_t woken = UINT32_MAX;

    initialize_test();
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_PORT_OBJECT, 1u);
    assert(kernel_port_create(27u, 2u, 128u, &port) ==
           KERNEL_PORT_NO_SLOT);
    assert(port == NULL);
    port = (KernelPort *)(uintptr_t)1u;
    kernel_allocation_test_fail_global(1u);
    assert(kernel_port_create(27u, 2u, 128u, &port) ==
           KERNEL_PORT_NO_SLOT);
    assert(port == NULL);

    assert(kernel_port_create(27u, 2u, 128u, &port) == KERNEL_PORT_OK);
    kernel_handle_table_init(&table);
    assert(kernel_handle_table_set_owner(&table, 27u));
    make_message(message, sizeof(message), 0x31u);
    assert(kernel_port_pool_stats(&before));
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_PORT_MESSAGE, 1u);
    assert(kernel_port_send(port, &table, message, sizeof(message), NULL,
                            0u, &woken) == KERNEL_PORT_NO_SLOT);
    assert(woken == 0u);
    assert(kernel_port_pool_stats(&after));
    assert(after.queued_messages == before.queued_messages);
    assert(after.queued_bytes == before.queued_bytes);
    assert(after.queued_handles == before.queued_handles);
    assert(after.sends == before.sends);
    assert(kernel_handle_count(&table) == 0u);
    kernel_allocation_test_fail_global(1u);
    assert(kernel_port_send(port, &table, message, sizeof(message), NULL,
                            0u, &woken) == KERNEL_PORT_NO_SLOT);
    assert(woken == 0u);
    assert(kernel_port_pool_stats(&after));
    assert(after.queued_messages == before.queued_messages);
    assert(after.queued_bytes == before.queued_bytes);
    assert(after.sends == before.sends);
    release_port(port);
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_PORT_OBJECT, &port_allocation));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_PORT_MESSAGE, &message_allocation));
    assert(port_allocation.current_units == 0u);
    assert(message_allocation.current_units == 0u);
    assert(port_allocation.injected_failures == 2u);
    assert(message_allocation.injected_failures == 2u);
    assert(kernel_port_pool_valid());
    assert(kernel_handle_transfer_pool_valid());
    assert(kernel_allocation_valid());
}

static void test_atomic_handle_move_cancel_and_commit(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelPortReceipt receipt;
    KernelPort *port;
    TestObject object = {0u, 0x12345678u};
    KernelHandle source_handle;
    KernelHandle future;
    void *found = NULL;
    uint8_t message[32];
    uint32_t required_size;
    uint32_t required_handles;
    uint32_t available;
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    make_message(message, sizeof(message), 0x21u);
    source_handle = install_transfer_handle(
        &source, &object, ASTRA_RIGHT_READ | ASTRA_RIGHT_TRANSFER);
    assert(kernel_port_create(1u, 2u, 128u, &port) == KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, message, sizeof(message),
                            &source_handle, 1u, &woken) == KERNEL_PORT_OK);
    assert(kernel_handle_lookup_any(&source, source_handle, 0u, NULL,
                                    &found) ==
           KERNEL_HANDLE_INVALID_ARGUMENT);
    assert(kernel_handle_lookup(&source, source_handle, KERNEL_OBJECT_NONE,
                                0u, &found) == KERNEL_HANDLE_INVALID_HANDLE);
    assert(object.releases == 0u);
    assert(kernel_port_pool_valid());

    available = kernel_handle_available(&destination);
    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(message), 1u, &receipt,
               &required_size, &required_handles) == KERNEL_PORT_OK);
    future = receipt.import.handles[0];
    assert(kernel_handle_available(&destination) == available - 1u);
    assert(kernel_handle_lookup(&destination, future, KERNEL_OBJECT_DEVICE,
                                ASTRA_RIGHT_READ, &found) ==
           KERNEL_HANDLE_INVALID_HANDLE);
    assert(kernel_port_receive_cancel(&receipt, &woken) == KERNEL_PORT_OK);
    assert(kernel_handle_available(&destination) == available);
    assert(kernel_port_pool_valid());

    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(message), 1u, &receipt,
               &required_size, &required_handles) == KERNEL_PORT_OK);
    assert(receipt.import.handles[0] == future);
    assert(kernel_port_receive_commit(&receipt, &woken) == KERNEL_PORT_OK);
    assert(kernel_handle_lookup(&destination, future, KERNEL_OBJECT_DEVICE,
                                ASTRA_RIGHT_READ, &found) == KERNEL_HANDLE_OK);
    assert(found == &object);
    assert(kernel_handle_close(&destination, future) == KERNEL_HANDLE_OK);
    assert(object.releases == 1u);
    release_port(port);
    assert(kernel_port_pool_valid());
}

static void test_failed_send_leaves_source_authority(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelPortReceipt receipt;
    KernelPort *port;
    TestObject transferable = {0u, 1u};
    TestObject denied = {0u, 2u};
    KernelHandle transferable_handle;
    KernelHandle denied_handle;
    KernelHandle duplicates[2];
    void *found = NULL;
    uint8_t message[24];
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    make_message(message, sizeof(message), 0x31u);
    transferable_handle = install_transfer_handle(
        &source, &transferable,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_TRANSFER);
    denied_handle = install_transfer_handle(
        &source, &denied, ASTRA_RIGHT_READ);
    assert(kernel_port_create(1u, 1u, sizeof(message), &port) ==
           KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, message, sizeof(message), NULL,
                            0u, &woken) == KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, message, sizeof(message),
                            &transferable_handle, 1u, &woken) ==
           KERNEL_PORT_WOULD_BLOCK);
    assert(kernel_handle_lookup(&source, transferable_handle,
                                KERNEL_OBJECT_DEVICE,
                                ASTRA_RIGHT_TRANSFER, &found) ==
           KERNEL_HANDLE_OK);
    receive_and_commit(port, &destination, message, sizeof(message), 0u,
                       &receipt);

    assert(kernel_port_send(port, &source, message, sizeof(message),
                            &denied_handle, 1u, &woken) ==
           KERNEL_PORT_ACCESS_DENIED);
    assert(kernel_handle_lookup(&source, denied_handle,
                                KERNEL_OBJECT_DEVICE, ASTRA_RIGHT_READ,
                                &found) == KERNEL_HANDLE_OK);
    duplicates[0] = transferable_handle;
    duplicates[1] = transferable_handle;
    assert(kernel_port_send(port, &source, message, sizeof(message),
                            duplicates, 2u, &woken) ==
           KERNEL_PORT_DUPLICATE_HANDLE);
    assert(kernel_handle_lookup(&source, transferable_handle,
                                KERNEL_OBJECT_DEVICE,
                                ASTRA_RIGHT_TRANSFER, &found) ==
           KERNEL_HANDLE_OK);
    assert(kernel_handle_close_all(&source) == 2u);
    assert(transferable.releases == 1u);
    assert(denied.releases == 1u);
    release_port(port);
    assert(kernel_port_pool_valid());
}

static void test_receive_capacity_and_destination_full(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelPortReceipt receipt;
    KernelPortSnapshot snapshot;
    KernelPort *port;
    TestObject transferred = {0u, 0x99u};
    TestObject fillers[KERNEL_HANDLE_MAX_ENTRIES];
    KernelHandle filler_handles[KERNEL_HANDLE_MAX_ENTRIES];
    KernelHandle attached;
    uint8_t message[48];
    uint32_t required_size;
    uint32_t required_handles;
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    make_message(message, sizeof(message), 0x41u);
    attached = install_transfer_handle(
        &source, &transferred, ASTRA_RIGHT_READ | ASTRA_RIGHT_TRANSFER);
    assert(kernel_port_create(1u, 1u, sizeof(message), &port) ==
           KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, message, sizeof(message),
                            &attached, 1u, &woken) == KERNEL_PORT_OK);
    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(message) - 1u, 0u, &receipt,
               &required_size, &required_handles) ==
           KERNEL_PORT_BUFFER_TOO_SMALL);
    assert(required_size == sizeof(message));
    assert(required_handles == 1u);
    assert(kernel_port_snapshot(0u, &snapshot));
    assert(snapshot.queued_messages == 1u);

    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        fillers[index].releases = 0u;
        fillers[index].value = index;
        filler_handles[index] = install_transfer_handle(
            &destination, &fillers[index], ASTRA_RIGHT_READ);
    }
    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(message), 1u, &receipt,
               &required_size, &required_handles) ==
           KERNEL_PORT_HANDLE_TABLE_FULL);
    assert(kernel_handle_close(&destination, filler_handles[0]) ==
           KERNEL_HANDLE_OK);
    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(message), 1u, &receipt,
               &required_size, &required_handles) == KERNEL_PORT_OK);
    assert(kernel_port_receive_commit(&receipt, &woken) == KERNEL_PORT_OK);
    assert(kernel_handle_close_all(&destination) ==
           KERNEL_HANDLE_MAX_ENTRIES);
    assert(transferred.releases == 1u);
    release_port(port);
    assert(kernel_port_pool_valid());
}

static void test_owner_death_discards_queued_authority(void)
{
    KernelHandleTable source;
    KernelPortSnapshot snapshot;
    KernelPortPoolStats stats;
    KernelPort *port;
    TestObject object = {0u, 7u};
    KernelHandle attached;
    uint8_t message[24];
    uint32_t closed;
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    make_message(message, sizeof(message), 0x51u);
    attached = install_transfer_handle(
        &source, &object, ASTRA_RIGHT_READ | ASTRA_RIGHT_TRANSFER);
    assert(kernel_port_create(0x1001u, 4u, 256u, &port) ==
           KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, message, sizeof(message),
                            &attached, 1u, &woken) == KERNEL_PORT_OK);
    assert(kernel_port_owner_died(0x1001u, &closed, &woken) ==
           KERNEL_PORT_OK);
    assert(closed == 1u);
    assert(object.releases == 1u);
    assert(kernel_port_snapshot(0u, &snapshot));
    assert(snapshot.state == KERNEL_PORT_CLOSING);
    assert(snapshot.queued_messages == 0u);
    assert(snapshot.capacity_reserved == 0u);
    assert(kernel_port_pool_stats(&stats));
    assert(stats.discarded_messages == 1u);
    assert(stats.discarded_handles == 1u);
    assert(stats.reserved_message_capacity == 0u);
    kernel_port_handle_release(
        port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_SEND);
    kernel_port_handle_release(
        port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_RECEIVE);
    assert(kernel_port_snapshot(0u, &snapshot));
    assert(snapshot.state == KERNEL_PORT_FREE);
    assert(kernel_port_pool_valid());
}

static void test_self_send_teardown_is_reentrant_safe(void)
{
    KernelHandleTable source;
    KernelPortSnapshot snapshot;
    KernelPort *port;
    KernelHandle send_handle;
    uint8_t message[24];
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    make_message(message, sizeof(message), 0x61u);
    assert(kernel_port_create(1u, 1u, sizeof(message), &port) ==
           KERNEL_PORT_OK);
    assert(kernel_handle_install(
               &source, KERNEL_OBJECT_PORT_SEND, KERNEL_PORT_SEND_RIGHTS,
               port, kernel_port_handle_release,
               (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_SEND,
               &send_handle) == KERNEL_HANDLE_OK);
    assert(kernel_port_send(port, &source, message, sizeof(message),
                            &send_handle, 1u, &woken) == KERNEL_PORT_OK);
    assert(kernel_handle_count(&source) == 0u);
    kernel_port_handle_release(
        port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_RECEIVE);
    assert(kernel_port_snapshot(0u, &snapshot));
    assert(snapshot.state == KERNEL_PORT_FREE);
    assert(kernel_port_pool_valid());
}

static void test_readable_and_writable_wait_queues(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelPortReceipt receipt;
    KernelThreadWaitSpec spec;
    KernelThread *thread;
    KernelPort *port;
    uint8_t message[24];
    uint32_t required_size;
    uint32_t required_handles;
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    make_message(message, sizeof(message), 0x71u);
    assert(kernel_port_create(1u, 1u, sizeof(message), &port) ==
           KERNEL_PORT_OK);
    assert(kernel_port_prepare_wait(port, KERNEL_PORT_ENDPOINT_RECEIVE,
                                    &spec) == KERNEL_PORT_WOULD_BLOCK);
    thread = allocate_running_thread(0u);
    assert(kernel_thread_block(
               thread, spec.queue, spec.sequence) == KERNEL_THREAD_OK);
    assert(kernel_port_commit_wait(port, KERNEL_PORT_ENDPOINT_RECEIVE) ==
           KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, message, sizeof(message), NULL,
                            0u, &woken) == KERNEL_PORT_OK);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);

    thread = NULL;
    assert(kernel_thread_take_next(&thread) == KERNEL_THREAD_OK);
    assert(kernel_port_prepare_wait(port, KERNEL_PORT_ENDPOINT_SEND,
                                    &spec) == KERNEL_PORT_WOULD_BLOCK);
    assert(kernel_thread_block(
               thread, spec.queue, spec.sequence) == KERNEL_THREAD_OK);
    assert(kernel_port_commit_wait(port, KERNEL_PORT_ENDPOINT_SEND) ==
           KERNEL_PORT_OK);
    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(message), 0u, &receipt,
               &required_size, &required_handles) == KERNEL_PORT_OK);
    assert(kernel_port_receive_commit(&receipt, &woken) == KERNEL_PORT_OK);
    assert(woken == 1u);
    assert(thread->context.data[0] == ASTRA_SYSCALL_OK);
    release_port(port);
    assert(kernel_port_pool_valid());
}

static void test_failed_large_send_waits_for_queue_change(void)
{
    KernelHandleTable source;
    KernelHandleTable destination;
    KernelPortReceipt receipt;
    KernelThreadWaitSpec spec;
    KernelPort *port;
    uint8_t small[24];
    uint8_t large[32];
    uint32_t required_size;
    uint32_t required_handles;
    uint32_t sequence;
    uint32_t woken;

    initialize_test();
    kernel_handle_table_init(&source);
    kernel_handle_table_init(&destination);
    make_message(small, sizeof(small), 0x72u);
    make_message(large, sizeof(large), 0x73u);
    assert(kernel_port_create(1u, 2u, 55u, &port) == KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, small, sizeof(small), NULL,
                            0u, &woken) == KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, large, sizeof(large), NULL,
                            0u, &woken) == KERNEL_PORT_WOULD_BLOCK);
    assert(kernel_port_prepare_wait(port, KERNEL_PORT_ENDPOINT_SEND,
                                    &spec) == KERNEL_PORT_OK);
    assert(kernel_port_wait_sequence(port, KERNEL_PORT_ENDPOINT_SEND,
                                     &sequence) == KERNEL_PORT_OK);
    assert(kernel_port_prepare_wait_after(
               port, KERNEL_PORT_ENDPOINT_SEND, sequence, &spec) ==
           KERNEL_PORT_WOULD_BLOCK);

    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(small), 0u, &receipt,
               &required_size, &required_handles) == KERNEL_PORT_OK);
    assert(kernel_port_receive_commit(&receipt, &woken) == KERNEL_PORT_OK);
    assert(kernel_port_prepare_wait_after(
               port, KERNEL_PORT_ENDPOINT_SEND, sequence, &spec) ==
           KERNEL_PORT_OK);
    assert(kernel_port_send(port, &source, large, sizeof(large), NULL,
                            0u, &woken) == KERNEL_PORT_OK);
    assert(kernel_port_receive_prepare(
               port, &destination, sizeof(large), 0u, &receipt,
               &required_size, &required_handles) == KERNEL_PORT_OK);
    assert(kernel_port_receive_commit(&receipt, &woken) == KERNEL_PORT_OK);
    release_port(port);
    assert(kernel_port_pool_valid());
}

static void test_pool_quotas_and_generation_reuse(void)
{
    KernelPort *owned[KERNEL_PORT_OWNER_MAX];
    KernelPort *extra;
    KernelPortSnapshot before;
    KernelPortSnapshot after;

    initialize_test();
    for (uint32_t index = 0u; index < KERNEL_PORT_OWNER_MAX; ++index) {
        assert(kernel_port_create(1u, 4u, 1120u, &owned[index]) ==
               KERNEL_PORT_OK);
    }
    assert(kernel_port_create(1u, 1u, 24u, &extra) ==
           KERNEL_PORT_QUOTA_EXCEEDED);
    assert(kernel_port_snapshot(0u, &before));
    for (uint32_t index = 0u; index < KERNEL_PORT_OWNER_MAX; ++index)
        kernel_port_abandon_unpublished(owned[index]);
    assert(kernel_port_create(1u, 1u, 24u, &extra) == KERNEL_PORT_OK);
    assert(kernel_port_snapshot(0u, &after));
    assert(after.generation != before.generation);
    kernel_port_abandon_unpublished(extra);
    assert(kernel_port_create(0u, 1u, 24u, &extra) ==
           KERNEL_PORT_INVALID_ARGUMENT);
    assert(kernel_port_create(1u, 0u, 24u, &extra) ==
           KERNEL_PORT_INVALID_ARGUMENT);
    assert(kernel_port_create(1u, 1u, 23u, &extra) ==
           KERNEL_PORT_INVALID_ARGUMENT);
    assert(kernel_port_pool_valid());
}

int main(void)
{
    test_allocation_injection_preserves_queue();
    test_fifo_capacity_and_peer_drain();
    test_atomic_handle_move_cancel_and_commit();
    test_failed_send_leaves_source_authority();
    test_receive_capacity_and_destination_full();
    test_owner_death_discards_queued_authority();
    test_self_send_teardown_is_reentrant_safe();
    test_readable_and_writable_wait_queues();
    test_failed_large_send_waits_for_queue_change();
    test_pool_quotas_and_generation_reuse();
    puts("port tests passed");
    return 0;
}
