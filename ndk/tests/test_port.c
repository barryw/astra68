#include <astra/port.h>

#include "syscall.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(AstraMessageHeader) == 24u,
               "AstraMessageHeader ABI");
_Static_assert(_Alignof(AstraMessageHeader) == 4u,
               "AstraMessageHeader alignment");
_Static_assert(offsetof(AstraMessageHeader, total_size) == 0u,
               "total_size offset");
_Static_assert(offsetof(AstraMessageHeader, header_size) == 4u,
               "header_size offset");
_Static_assert(offsetof(AstraMessageHeader, flags) == 6u,
               "flags offset");
_Static_assert(offsetof(AstraMessageHeader, protocol) == 8u,
               "protocol offset");
_Static_assert(offsetof(AstraMessageHeader, protocol_version) == 12u,
               "protocol_version offset");
_Static_assert(offsetof(AstraMessageHeader, reserved) == 14u,
               "reserved offset");
_Static_assert(offsetof(AstraMessageHeader, operation) == 16u,
               "operation offset");
_Static_assert(offsetof(AstraMessageHeader, transaction_id) == 20u,
               "transaction_id offset");
_Static_assert(sizeof(AstraPort) == 8u, "AstraPort ABI");

#define SCRIPT_MAX 16u

typedef struct ExpectedCall {
    uint32_t number;
    uintptr_t arguments[5];
    uint32_t status;
    uint32_t out_d1;
    uint32_t out_d2;
} ExpectedCall;

typedef struct TestMessage {
    AstraMessageHeader header;
    uint32_t payload[2];
} TestMessage;

static ExpectedCall script[SCRIPT_MAX];
static uint32_t script_count;
static uint32_t script_cursor;

static void script_reset(void)
{
    script_count = 0u;
    script_cursor = 0u;
}

static void expect_call(uint32_t number,
                        uintptr_t d1,
                        uintptr_t d2,
                        uintptr_t d3,
                        uintptr_t d4,
                        uintptr_t d5,
                        uint32_t status,
                        uint32_t out_d1,
                        uint32_t out_d2)
{
    ExpectedCall *call;

    assert(script_count < SCRIPT_MAX);
    call = &script[script_count++];
    call->number = number;
    call->arguments[0] = d1;
    call->arguments[1] = d2;
    call->arguments[2] = d3;
    call->arguments[3] = d4;
    call->arguments[4] = d5;
    call->status = status;
    call->out_d1 = out_d1;
    call->out_d2 = out_d2;
}

static void script_done(void)
{
    assert(script_cursor == script_count);
}

uint32_t astra_ndk_test_syscall(uint32_t number,
                                uintptr_t d1,
                                uintptr_t d2,
                                uintptr_t d3,
                                uintptr_t d4,
                                uintptr_t d5,
                                uint32_t *out_d1,
                                uint32_t *out_d2)
{
    const ExpectedCall *call;

    assert(script_cursor < script_count);
    call = &script[script_cursor++];
    assert(number == call->number);
    assert(d1 == call->arguments[0]);
    assert(d2 == call->arguments[1]);
    assert(d3 == call->arguments[2]);
    assert(d4 == call->arguments[3]);
    assert(d5 == call->arguments[4]);
    assert(out_d1 != 0);
    assert(out_d2 != 0);
    *out_d1 = call->out_d1;
    *out_d2 = call->out_d2;
    return call->status;
}

static TestMessage test_message(void)
{
    TestMessage message;

    assert(astra_message_header_init(
               &message.header, sizeof(message), UINT32_C(0x54455354),
               3u, 7u, UINT32_C(0x12345678)) == ASTRA_OK);
    message.payload[0] = UINT32_C(0x11223344);
    message.payload[1] = UINT32_C(0x55667788);
    return message;
}

static void test_header(void)
{
    AstraMessageHeader header;
    uint8_t unaligned[sizeof(header) + 1u];

    assert(astra_message_header_init(
               &header, sizeof(header), 9u, 2u, 3u, 4u) == ASTRA_OK);
    assert(header.total_size == sizeof(header));
    assert(header.header_size == ASTRA_MESSAGE_HEADER_SIZE);
    assert(header.flags == 0u);
    assert(header.protocol == 9u);
    assert(header.protocol_version == 2u);
    assert(header.reserved == 0u);
    assert(header.operation == 3u);
    assert(header.transaction_id == 4u);
    assert(astra_message_header_init(0, sizeof(header), 0, 0, 0, 0) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_message_header_init(
               (AstraMessageHeader *)(void *)&unaligned[1], sizeof(header),
               0, 0, 0, 0) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_message_header_init(
               &header, ASTRA_MESSAGE_SIZE_MAX + 1u, 0, 0, 0, 0) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_result_mapping(void)
{
    assert(astra_internal_result(ASTRA_SYSCALL_OK) == ASTRA_OK);
    assert(astra_internal_result(ASTRA_SYSCALL_BAD_SYSCALL) ==
           ASTRA_ERROR_UNSUPPORTED);
    assert(astra_internal_result(ASTRA_SYSCALL_INVALID_ARGUMENT) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_internal_result(ASTRA_SYSCALL_INVALID_HANDLE) ==
           ASTRA_ERROR_INVALID_HANDLE);
    assert(astra_internal_result(ASTRA_SYSCALL_ACCESS_DENIED) ==
           ASTRA_ERROR_PERMISSION);
    assert(astra_internal_result(ASTRA_SYSCALL_RESOURCE_LIMIT) ==
           ASTRA_ERROR_NO_RESOURCES);
    assert(astra_internal_result(ASTRA_SYSCALL_WOULD_BLOCK) ==
           ASTRA_ERROR_WOULD_BLOCK);
    assert(astra_internal_result(ASTRA_SYSCALL_TIMED_OUT) ==
           ASTRA_ERROR_TIMEOUT);
    assert(astra_internal_result(ASTRA_SYSCALL_PEER_DEAD) ==
           ASTRA_ERROR_PEER_DEAD);
    assert(astra_internal_result(ASTRA_SYSCALL_BAD_ADDRESS) ==
           ASTRA_ERROR_BAD_ADDRESS);
    assert(astra_internal_result(ASTRA_SYSCALL_CANCELLED) ==
           ASTRA_ERROR_CANCELLED);
    assert(astra_internal_result(ASTRA_SYSCALL_OUT_OF_MEMORY) ==
           ASTRA_ERROR_OUT_OF_MEMORY);
    assert(astra_internal_result(ASTRA_SYSCALL_IO_ERROR) == ASTRA_ERROR_IO);
    assert(astra_internal_result(ASTRA_SYSCALL_CLOSED) ==
           ASTRA_ERROR_CLOSED);
    assert(astra_internal_result(ASTRA_SYSCALL_BUFFER_TOO_SMALL) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(astra_internal_result(UINT32_MAX) == ASTRA_ERROR_IO);
}

static void test_create_and_close(void)
{
    AstraPort port = ASTRA_PORT_INIT;
    AstraPort stale = { 0x201u, 0x202u };

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_CREATE, 4u, 560u, 0, 0, 0,
                ASTRA_SYSCALL_OK, 0x101u, 0x102u);
    assert(astra_port_create(4u, 560u, &port) == ASTRA_OK);
    assert(port.receive == 0x101u);
    assert(port.send == 0x102u);
    script_done();

    script_reset();
    expect_call(ASTRA_SYSCALL_CLOSE, 0x102u, 0, 0, 0, 0,
                ASTRA_SYSCALL_OK, 0, 0);
    expect_call(ASTRA_SYSCALL_CLOSE, 0x101u, 0, 0, 0, 0,
                ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_port_close(&port) == ASTRA_OK);
    assert(port.receive == ASTRA_INVALID_HANDLE);
    assert(port.send == ASTRA_INVALID_HANDLE);
    script_done();

    script_reset();
    expect_call(ASTRA_SYSCALL_CLOSE, 0x202u, 0, 0, 0, 0,
                ASTRA_SYSCALL_INVALID_HANDLE, 0, 0);
    expect_call(ASTRA_SYSCALL_CLOSE, 0x201u, 0, 0, 0, 0,
                ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_port_close(&stale) == ASTRA_ERROR_INVALID_HANDLE);
    assert(stale.receive == ASTRA_INVALID_HANDLE);
    assert(stale.send == 0x202u);
    script_done();

    script_reset();
    assert(astra_port_create(0u, 24u, &port) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_port_create(1u, 23u, &port) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_port_close(&port) == ASTRA_ERROR_INVALID_HANDLE);
    assert(astra_handle_close(0) == ASTRA_ERROR_INVALID_ARGUMENT);
    script_done();
}

static void test_send(void)
{
    TestMessage message = test_message();
    AstraHandle handles[2] = { 0x301u, 0x302u };

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_SEND_TRY, 0x200u,
                (uintptr_t)&message, sizeof(message), (uintptr_t)handles, 2u,
                ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_port_send_try(0x200u, &message, sizeof(message),
                               handles, 2u) == ASTRA_OK);
    assert(handles[0] == ASTRA_INVALID_HANDLE);
    assert(handles[1] == ASTRA_INVALID_HANDLE);
    script_done();

    handles[0] = 0x303u;
    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_SEND_TRY, 0x200u,
                (uintptr_t)&message, sizeof(message), (uintptr_t)handles, 1u,
                ASTRA_SYSCALL_WOULD_BLOCK, 0, 0);
    assert(astra_port_send_try(0x200u, &message, sizeof(message),
                               handles, 1u) == ASTRA_ERROR_WOULD_BLOCK);
    assert(handles[0] == 0x303u);
    script_done();

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_SEND_TRY, 0x200u,
                (uintptr_t)&message, sizeof(message), 0, 0,
                ASTRA_SYSCALL_PEER_DEAD, 0, 0);
    assert(astra_port_send_try(0x200u, &message, sizeof(message),
                               0, 0) == ASTRA_ERROR_PEER_DEAD);
    script_done();

    message.header.reserved = 1u;
    script_reset();
    assert(astra_port_send_try(0x200u, &message, sizeof(message), 0, 0) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    script_done();
}

static void test_send_deadline(void)
{
    TestMessage message = test_message();
    AstraHandle handle = 0x401u;
    const AstraMonotonicDeadline deadline =
        (AstraMonotonicDeadline)UINT64_C(0x0000000123456789);

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_SEND_TRY, 0x210u,
                (uintptr_t)&message, sizeof(message), (uintptr_t)&handle, 1u,
                ASTRA_SYSCALL_WOULD_BLOCK, 0, 0);
    expect_call(ASTRA_SYSCALL_WAIT_ONE, 0x210u, 1u, 0x23456789u, 0, 0,
                ASTRA_SYSCALL_OK, 0, 0);
    expect_call(ASTRA_SYSCALL_PORT_SEND_TRY, 0x210u,
                (uintptr_t)&message, sizeof(message), (uintptr_t)&handle, 1u,
                ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_port_send_until(0x210u, &message, sizeof(message),
                                 &handle, 1u, deadline) == ASTRA_OK);
    assert(handle == ASTRA_INVALID_HANDLE);
    script_done();

    handle = 0x402u;
    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_SEND_TRY, 0x210u,
                (uintptr_t)&message, sizeof(message), (uintptr_t)&handle, 1u,
                ASTRA_SYSCALL_WOULD_BLOCK, 0, 0);
    assert(astra_port_send_until(0x210u, &message, sizeof(message),
                                 &handle, 1u, ASTRA_DEADLINE_POLL) ==
           ASTRA_ERROR_WOULD_BLOCK);
    assert(handle == 0x402u);
    script_done();

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_SEND_TRY, 0x210u,
                (uintptr_t)&message, sizeof(message), 0, 0,
                ASTRA_SYSCALL_WOULD_BLOCK, 0, 0);
    expect_call(ASTRA_SYSCALL_WAIT_ONE, 0x210u, 1u, 0x23456789u, 0, 0,
                ASTRA_SYSCALL_TIMED_OUT, 0, 0);
    assert(astra_port_send_until(0x210u, &message, sizeof(message),
                                 0, 0, deadline) == ASTRA_ERROR_TIMEOUT);
    script_done();

    script_reset();
    assert(astra_port_send_until(0x210u, &message, sizeof(message),
                                 0, 0, -1) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    script_done();
}

static void test_receive(void)
{
    union {
        uint32_t alignment;
        uint8_t bytes[ASTRA_MESSAGE_SIZE_MAX];
    } message;
    AstraHandle handles[ASTRA_MESSAGE_HANDLES_MAX];
    uint32_t message_size;
    uint32_t handle_count;

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_RECEIVE_TRY, 0x500u, 0, 0, 0, 0,
                ASTRA_SYSCALL_BUFFER_TOO_SMALL, 32u, 2u);
    assert(astra_port_receive_try(0x500u, 0, 0, 0, 0,
                                  &message_size, &handle_count) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(message_size == 32u);
    assert(handle_count == 2u);
    script_done();

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_RECEIVE_TRY, 0x500u,
                (uintptr_t)message.bytes, sizeof(message.bytes),
                (uintptr_t)handles, ASTRA_MESSAGE_HANDLES_MAX,
                ASTRA_SYSCALL_OK, 32u, 2u);
    assert(astra_port_receive_try(
               0x500u, message.bytes, sizeof(message.bytes), handles,
               ASTRA_MESSAGE_HANDLES_MAX, &message_size, &handle_count) ==
           ASTRA_OK);
    assert(message_size == 32u);
    assert(handle_count == 2u);
    script_done();

    message_size = UINT32_MAX;
    handle_count = UINT32_MAX;
    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_RECEIVE_TRY, 0x500u,
                (uintptr_t)message.bytes, sizeof(message.bytes),
                (uintptr_t)handles, ASTRA_MESSAGE_HANDLES_MAX,
                ASTRA_SYSCALL_BAD_ADDRESS, 32u, 2u);
    assert(astra_port_receive_try(
               0x500u, message.bytes, sizeof(message.bytes), handles,
               ASTRA_MESSAGE_HANDLES_MAX, &message_size, &handle_count) ==
           ASTRA_ERROR_BAD_ADDRESS);
    assert(message_size == 0u);
    assert(handle_count == 0u);
    script_done();
}

static void test_receive_deadline(void)
{
    union {
        uint32_t alignment;
        uint8_t bytes[ASTRA_MESSAGE_SIZE_MAX];
    } message;
    uint32_t message_size;
    uint32_t handle_count;
    const AstraMonotonicDeadline deadline =
        (AstraMonotonicDeadline)UINT64_C(0x00000002456789ab);

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_RECEIVE_TRY, 0x510u,
                (uintptr_t)message.bytes, sizeof(message.bytes), 0, 0,
                ASTRA_SYSCALL_WOULD_BLOCK, 0, 0);
    expect_call(ASTRA_SYSCALL_WAIT_ONE, 0x510u, 2u, 0x456789abu, 0, 0,
                ASTRA_SYSCALL_OK, 0, 0);
    expect_call(ASTRA_SYSCALL_PORT_RECEIVE_TRY, 0x510u,
                (uintptr_t)message.bytes, sizeof(message.bytes), 0, 0,
                ASTRA_SYSCALL_OK, 28u, 0u);
    assert(astra_port_receive_until(
               0x510u, message.bytes, sizeof(message.bytes), 0, 0,
               &message_size, &handle_count, deadline) == ASTRA_OK);
    assert(message_size == 28u);
    assert(handle_count == 0u);
    script_done();

    script_reset();
    expect_call(ASTRA_SYSCALL_PORT_RECEIVE_TRY, 0x510u,
                (uintptr_t)message.bytes, sizeof(message.bytes), 0, 0,
                ASTRA_SYSCALL_WOULD_BLOCK, 0, 0);
    expect_call(ASTRA_SYSCALL_WAIT_ONE, 0x510u, 2u, 0x456789abu, 0, 0,
                ASTRA_SYSCALL_CANCELLED, 0, 0);
    assert(astra_port_receive_until(
               0x510u, message.bytes, sizeof(message.bytes), 0, 0,
               &message_size, &handle_count, deadline) ==
           ASTRA_ERROR_CANCELLED);
    script_done();
}

int main(void)
{
    test_header();
    test_result_mapping();
    test_create_and_close();
    test_send();
    test_send_deadline();
    test_receive();
    test_receive_deadline();
    puts("port tests: PASS");
    return 0;
}
