#include "process_table.h"

#include <astra/bytes.h>
#include <astra/posix_process.h>
#include <astra/process.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("posixd", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static PosixProcessEntry entries[ASTRA_PROCESS_COUNT_MAX];
static PosixSessionEntry sessions[ASTRA_PROCESS_COUNT_MAX];
static PosixProcessTable table;
static uint32_t control_receive;
static uint32_t control_send;

static uint32_t
status_from_syscall(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_OK:
        return ASTRA_STATUS_OK;
    case ASTRA_SYSCALL_ACCESS_DENIED:
        return ASTRA_STATUS_ACCESS;
    case ASTRA_SYSCALL_RESOURCE_LIMIT:
    case ASTRA_SYSCALL_OUT_OF_MEMORY:
        return ASTRA_STATUS_LIMIT;
    case ASTRA_SYSCALL_INVALID_HANDLE:
    case ASTRA_SYSCALL_PEER_DEAD:
        return ASTRA_STATUS_NOT_FOUND;
    default:
        return ASTRA_STATUS_INVALID;
    }
}

typedef struct SignalContext {
    uint32_t signal;
} SignalContext;

static uint32_t
signal_one(void *raw, uint32_t handle, int32_t process)
{
    SignalContext *context = raw;
    uint32_t status;

    (void)process;
    if (context->signal == 0u)
        return ASTRA_STATUS_OK;
    if (context->signal == ASTRA_POSIX_SIGNAL_STOP)
        status = astra_process_suspend(handle);
    else if (context->signal == ASTRA_POSIX_SIGNAL_CONTINUE) {
        status = astra_process_resume(handle);
        if (status == ASTRA_SYSCALL_OK)
            status = astra_process_signal(handle, context->signal);
    } else
        status = context->signal == ASTRA_POSIX_SIGNAL_KILL ?
            astra_process_terminate(handle, context->signal) :
            astra_process_signal(handle, context->signal);
    return status_from_syscall(status);
}

static int
request_valid(const AstraPosixProcessRequest *request, uint32_t size)
{
    return size == sizeof(*request) &&
           request->header.total_size == sizeof(*request) &&
           request->header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           request->header.flags == 0u && request->header.reserved == 0u &&
           request->header.protocol == ASTRA_POSIX_PROCESS_PROTOCOL &&
           request->header.protocol_version == ASTRA_POSIX_PROCESS_VERSION &&
           request->reserved == 0u;
}

static void
send_reply(uint32_t reply_handle, const AstraPosixProcessRequest *request,
           uint32_t status, const PosixProcessEntry *entry)
{
    AstraPosixProcessReply reply = {0};

    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_POSIX_PROCESS_PROTOCOL,
                             ASTRA_POSIX_PROCESS_VERSION,
                             request->header.operation,
                             request->header.transaction_id);
    reply.status = status;
    if (entry != NULL) {
        reply.process = entry->process;
        reply.parent = entry->parent;
        reply.group = entry->group;
        reply.session = entry->session;
    }
    (void)astra_port_send(reply_handle, &reply, sizeof(reply), NULL, 0u);
}

static void
process_request(void)
{
    AstraPosixProcessRequest request = {0};
    PosixProcessEntry result_entry;
    uint32_t handles[2] = {0u};
    uint32_t handle_count = 0u;
    uint32_t sender = 0u;
    uint32_t size = 0u;
    uint32_t status;
    uint32_t expected;

    status = astra_port_receive_from(
        control_receive, &request, sizeof(request), handles, 2u,
        &size, &handle_count, &sender);
    if (status != ASTRA_SYSCALL_OK)
        return;
    expected = request.header.operation == ASTRA_POSIX_PROCESS_REGISTER ?
        2u : 1u;
    if (!request_valid(&request, size) || handle_count != expected ||
        handles[0] == 0u || sender == 0u) {
        if (handle_count != 0u && handles[0] != 0u)
            send_reply(handles[0], &request, ASTRA_STATUS_PROTOCOL, NULL);
        for (uint32_t index = 0u; index < handle_count; ++index)
            if (handles[index] != 0u)
                (void)astra_close(handles[index]);
        return;
    }

    memset(&result_entry, 0, sizeof(result_entry));
    switch (request.header.operation) {
    case ASTRA_POSIX_PROCESS_REGISTER: {
        AstraProcessInfo info;

        status = astra_process_info(handles[1], &info);
        if (status != ASTRA_SYSCALL_OK ||
            info.id != (uint32_t)request.process) {
            status = ASTRA_STATUS_ACCESS;
        } else {
            status = posix_process_register(
                &table, (int32_t)sender, request.process, handles[1],
                request.flags);
            if (status == ASTRA_STATUS_OK)
                handles[1] = 0u;
        }
        break;
    }
    case ASTRA_POSIX_PROCESS_SIGNAL: {
        SignalContext context = {.signal = (uint32_t)request.value};
        uint32_t matched = 0u;

        if (request.value < 0 || request.value >= 32 || request.flags != 0u)
            status = ASTRA_STATUS_INVALID;
        else
            status = posix_process_visit(
                &table, (int32_t)sender, request.process, signal_one,
                &context, &matched);
        break;
    }
    case ASTRA_POSIX_PROCESS_SIGNAL_GROUP: {
        SignalContext context = {.signal = (uint32_t)request.value};
        PosixProcessEntry caller;
        uint32_t matched = 0u;

        if (request.process <= 0 || request.value < 0 ||
            request.value >= 32 || request.flags != 0u) {
            status = ASTRA_STATUS_INVALID;
            break;
        }
        status = posix_process_query(&table, (int32_t)sender, &caller);
        if (status == ASTRA_STATUS_OK)
            status = posix_process_visit_session_group(
                &table, caller.session, request.process, signal_one,
                &context, &matched);
        break;
    }
    case ASTRA_POSIX_PROCESS_SETPGID:
        status = request.flags == 0u ?
            posix_process_setpgid(&table, (int32_t)sender, request.process,
                                  request.value) :
            ASTRA_STATUS_INVALID;
        break;
    case ASTRA_POSIX_PROCESS_SETSID:
        status = request.flags == 0u ?
            posix_process_setsid(&table, (int32_t)sender, &result_entry) :
            ASTRA_STATUS_INVALID;
        break;
    case ASTRA_POSIX_PROCESS_QUERY:
        status = request.flags == 0u ?
            posix_process_query(
                &table, request.process == 0 ? (int32_t)sender :
                                               request.process,
                &result_entry) :
            ASTRA_STATUS_INVALID;
        break;
    case ASTRA_POSIX_PROCESS_TTY_ATTACH:
        status = request.process == 0 && request.value == 0 &&
                         request.flags == 0u ?
            posix_process_tty_attach(&table, (int32_t)sender) :
            ASTRA_STATUS_INVALID;
        break;
    case ASTRA_POSIX_PROCESS_TTY_FOREGROUND:
        status = request.process == 0 && request.value == 0 &&
                         request.flags == 0u ?
            posix_process_tty_foreground(
                &table, (int32_t)sender, &result_entry.session,
                &result_entry.group) :
            ASTRA_STATUS_INVALID;
        break;
    case ASTRA_POSIX_PROCESS_TTY_SET_FOREGROUND:
        status = request.process == 0 && request.flags == 0u ?
            posix_process_tty_set_foreground(
                &table, (int32_t)sender, request.value) :
            ASTRA_STATUS_INVALID;
        break;
    case ASTRA_POSIX_PROCESS_TTY_SIGNAL: {
        SignalContext context = {.signal = (uint32_t)request.value};
        int32_t session;
        int32_t group;
        uint32_t matched = 0u;

        if (request.process != 0 || request.value <= 0 ||
            request.value >= 32 || request.flags != 0u) {
            status = ASTRA_STATUS_INVALID;
            break;
        }
        status = posix_process_tty_foreground(
            &table, (int32_t)sender, &session, &group);
        if (status == ASTRA_STATUS_OK)
            status = posix_process_visit_session_group(
                &table, session, group, signal_one, &context, &matched);
        break;
    }
    default:
        status = ASTRA_STATUS_PROTOCOL;
        break;
    }
    send_reply(handles[0], &request, status,
               status == ASTRA_STATUS_OK &&
                       (request.header.operation ==
                            ASTRA_POSIX_PROCESS_QUERY ||
                        request.header.operation ==
                            ASTRA_POSIX_PROCESS_SETSID ||
                        request.header.operation ==
                            ASTRA_POSIX_PROCESS_TTY_FOREGROUND) ?
                   &result_entry : NULL);
    for (uint32_t index = 0u; index < handle_count; ++index)
        if (handles[index] != 0u)
            (void)astra_close(handles[index]);
}

static void
remove_dead(void)
{
    for (uint32_t index = 0u; index < table.capacity; ++index) {
        PosixProcessEntry parent;
        uint32_t exit_status = 0u;
        uint32_t handle;
        uint32_t parent_handle = 0u;

        if (table.entries[index].process == 0 ||
            astra_process_wait(table.entries[index].handle, 0u,
                               &exit_status) == ASTRA_SYSCALL_TIMED_OUT)
            continue;
        if (table.entries[index].parent > 0 &&
            posix_process_query(&table, table.entries[index].parent,
                                &parent) == ASTRA_STATUS_OK)
            parent_handle = parent.handle;
        if (posix_process_remove(&table, table.entries[index].process,
                                 &handle) == ASTRA_STATUS_OK) {
            (void)astra_close(handle);
            if (parent_handle != 0u)
                (void)astra_process_signal(parent_handle,
                                           ASTRA_POSIX_SIGNAL_CHILD);
        }
    }
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    uint32_t published;
    uint32_t status;

    _Static_assert(ASTRA_PROCESS_COUNT_MAX + 1u <= ASTRA_WAIT_MULTIPLE_MAX,
                   "process service wait set exceeds the kernel wait set");

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    status = posix_process_table_init(&table, entries, sessions,
                                      ASTRA_PROCESS_COUNT_MAX);
    if (status == ASTRA_STATUS_OK &&
        astra_rt_port_create(ASTRA_PORT_MESSAGES_MAX,
                             ASTRA_PORT_MESSAGES_MAX *
                                 ASTRA_POSIX_PROCESS_REQUEST_SIZE,
                             &control_receive, &control_send) !=
            ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_LIMIT;
    published = control_send;
    (void)astra_service_ready(bootstrap->handle, status,
                              status == ASTRA_STATUS_OK ? &published : NULL,
                              status == ASTRA_STATUS_OK ? 1u : 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    for (;;) {
        uint32_t waits[ASTRA_PROCESS_COUNT_MAX + 1u];
        uint32_t count = 1u;
        uint32_t index = ASTRA_WAIT_INDEX_NONE;

        remove_dead();
        waits[0] = control_receive;
        for (uint32_t slot = 0u; slot < table.capacity; ++slot)
            if (table.entries[slot].process != 0)
                waits[count++] = table.entries[slot].handle;
        status = astra_wait_multiple(waits, count, ASTRA_DEADLINE_FOREVER,
                                     &index, NULL);
        if (status == ASTRA_SYSCALL_OK && index == 0u)
            process_request();
        else if (status == ASTRA_SYSCALL_OK ||
                 status == ASTRA_SYSCALL_PEER_DEAD)
            remove_dead();
        else
            return ASTRA_STATUS_PEER_DEAD;
    }
}
