#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static AstraPosixProcessRequest sent;
static AstraPosixProcessReply received;
static uint32_t sent_handles[2];
static uint32_t sent_handle_count;
static uint32_t closed[4];
static uint32_t close_count;

uint32_t
astra_rt_handle_duplicate(uint32_t handle, uint32_t rights,
                          uint32_t *duplicate)
{
    assert(handle == 7u);
    assert(rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                      ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                      ASTRA_RIGHT_TRANSFER));
    *duplicate = 31u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_rt_port_create(uint32_t messages, uint32_t bytes,
                     uint32_t *receive, uint32_t *send)
{
    assert(messages == 1u && bytes == sizeof(AstraPosixProcessReply));
    *receive = 41u;
    *send = 42u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_port_send(uint32_t handle, const void *message, uint32_t size,
                const uint32_t *handles, uint32_t handle_count)
{
    assert(handle == 6u && size == sizeof(sent));
    memcpy(&sent, message, sizeof(sent));
    sent_handle_count = handle_count;
    memcpy(sent_handles, handles, handle_count * sizeof(handles[0]));
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                   uint32_t *handles, uint32_t handle_capacity,
                   uint32_t *size, uint32_t *handle_count)
{
    assert(handle == 41u && capacity == sizeof(received));
    assert(handles == NULL && handle_capacity == 0u);
    memcpy(message, &received, sizeof(received));
    *size = sizeof(received);
    if (handle_count != NULL)
        *handle_count = 0u;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    (void)handle;
    (void)deadline;
    (void)detail;
    assert(0 && "the immediate path must not wait");
    return ASTRA_SYSCALL_IO_ERROR;
}

uint32_t
astra_close(uint32_t handle)
{
    closed[close_count++] = handle;
    return ASTRA_SYSCALL_OK;
}

static void
prepare_reply(uint32_t operation, uint32_t status)
{
    memset(&received, 0, sizeof(received));
    astra_message_header_set(&received.header, sizeof(received),
                             ASTRA_POSIX_PROCESS_PROTOCOL,
                             ASTRA_POSIX_PROCESS_VERSION, operation, 0u);
    received.status = status;
}

int
main(void)
{
    AstraPosixProcessReply reply;

    prepare_reply(ASTRA_POSIX_PROCESS_REGISTER, ASTRA_STATUS_OK);
    assert(astra_posix_process_register(
               6u, 7u, 123u, ASTRA_POSIX_PROCESS_NEW_SESSION) ==
           ASTRA_STATUS_OK);
    assert(sent.header.operation == ASTRA_POSIX_PROCESS_REGISTER);
    assert(sent.process == 123 &&
           sent.flags == ASTRA_POSIX_PROCESS_NEW_SESSION);
    assert(sent_handle_count == 2u && sent_handles[0] == 42u &&
           sent_handles[1] == 31u);
    assert(close_count == 1u && closed[0] == 41u);

    prepare_reply(ASTRA_POSIX_PROCESS_SIGNAL, ASTRA_STATUS_NOT_FOUND);
    assert(astra_posix_process_signal(6u, -123, 15u) ==
           ASTRA_STATUS_NOT_FOUND);
    assert(sent.header.operation == ASTRA_POSIX_PROCESS_SIGNAL);
    assert(sent.process == -123 && sent.value == 15 &&
           sent_handle_count == 1u && sent_handles[0] == 42u);

    prepare_reply(ASTRA_POSIX_PROCESS_SIGNAL_GROUP, ASTRA_STATUS_OK);
    assert(astra_posix_process_signal_group(6u, 1, 15u) ==
           ASTRA_STATUS_OK);
    assert(sent.header.operation == ASTRA_POSIX_PROCESS_SIGNAL_GROUP);
    assert(sent.process == 1 && sent.value == 15);

    prepare_reply(ASTRA_POSIX_PROCESS_QUERY, ASTRA_STATUS_OK);
    received.process = 123;
    received.parent = 1;
    received.group = 123;
    received.session = 123;
    assert(astra_posix_process_query(6u, 123, &reply) == ASTRA_STATUS_OK);
    assert(reply.process == 123 && reply.parent == 1 &&
           reply.group == 123 && reply.session == 123);

    prepare_reply(ASTRA_POSIX_PROCESS_TTY_ATTACH, ASTRA_STATUS_OK);
    assert(astra_posix_process_tty_attach(6u) == ASTRA_STATUS_OK);
    assert(sent.header.operation == ASTRA_POSIX_PROCESS_TTY_ATTACH);

    prepare_reply(ASTRA_POSIX_PROCESS_TTY_FOREGROUND, ASTRA_STATUS_OK);
    received.group = 321;
    received.session = 123;
    assert(astra_posix_process_tty_foreground(6u, &reply) == ASTRA_STATUS_OK);
    assert(reply.group == 321 && reply.session == 123);

    prepare_reply(ASTRA_POSIX_PROCESS_TTY_SET_FOREGROUND, ASTRA_STATUS_OK);
    assert(astra_posix_process_tty_set_foreground(6u, 321) ==
           ASTRA_STATUS_OK);
    assert(sent.value == 321);

    prepare_reply(ASTRA_POSIX_PROCESS_TTY_SIGNAL, ASTRA_STATUS_OK);
    assert(astra_posix_process_tty_signal(6u, 2u) == ASTRA_STATUS_OK);
    assert(sent.value == 2);
    return 0;
}
