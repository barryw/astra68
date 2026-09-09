/* Terminal transport and lifecycle for one interactive zsh session. */

#include <console_session.h>
#include <console_stream.h>

#include <astra/config_document.h>
#include <astra/event_control.h>
#include <astra/network.h>
#include <astra/ntp.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

#include <astra/event_emit.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_process.h>
#include <astra/vfs_reader.h>

#include <string.h>

#define CONSOLE_INPUT_POLL_NS 10000000ull
#define CONSOLE_PRESENT_NS 16666667ull
#define SESSION_NOT_RUN 126u

typedef struct ConsoleSession {
    AstraTerminal terminal;
    ConsoleSessionBackend backend;
    uint32_t child;
    uint32_t child_id;
    uint32_t pending_key;
    uint8_t pending_key_valid;
    uint64_t present_deadline;
    int running;
} ConsoleSession;

static ConsoleSession session;

static const AstraFilesystemLibraryV2 *filesystem_library(void)
{
    return session.backend.process_filesystem->library;
}

static void echo_line(void *context, const char *line, uint32_t length)
{
    (void)context;
    (void)astra_log_debug(line, length);
}

static void prompt_ready(void *context)
{
    (void)context;
    ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "shell ready");
}

static void write_line(const char *text)
{
    astra_terminal_write(&session.terminal, text);
    astra_terminal_putc(&session.terminal, '\n');
}

static void write_number(uint32_t value)
{
    char digits[12];
    uint32_t index = 0u;

    if (value == 0u) {
        astra_terminal_putc(&session.terminal, '0');
        return;
    }
    while (value != 0u && index < sizeof(digits)) {
        digits[index++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (index != 0u)
        astra_terminal_putc(&session.terminal, (uint8_t)digits[--index]);
}

static void write_hex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    astra_terminal_write(&session.terminal, "0x");
    for (uint32_t shift = 32u; shift != 0u; shift -= 4u)
        astra_terminal_putc(&session.terminal,
                           digits[(value >> (shift - 4u)) & 0x0fu]);
}

static uint32_t launch_grants(AstraLaunchGrant *grants)
{
    static const char *const stream_names[] = {"STDOUT", "STDERR", "STDIN"};
    static const char *const mount_names[] = {
        "WORK", "HOME", "COMMANDS", "LIBS", "EVENTS", "PROC", "METRICS",
        ASTRA_CONFIG_COMMANDS_CAPABILITY
    };
    static const char *const authority_names[] = {
        ASTRA_CAPABILITY_EVENT_CONTROL,
        ASTRA_CAPABILITY_NETWORK,
        ASTRA_CAPABILITY_NETWORK_LISTEN,
        ASTRA_CAPABILITY_NTP,
        ASTRA_CAPABILITY_POSIX_PROCESS,
    };
    const uint32_t streams[] = {
        console_stream_stdout(), console_stream_stderr(),
        console_stream_stdin()
    };
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < 3u; ++index) {
        if (streams[index] == 0u)
            continue;
        astra_capability_name_set(grants[count].name, stream_names[index]);
        grants[count].handle = streams[index];
        grants[count].rights = ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
                               ASTRA_RIGHT_TRANSFER;
        ++count;
    }
    for (uint32_t index = 0u;
         index < sizeof(mount_names) / sizeof(mount_names[0]); ++index) {
        const int config = strcmp(mount_names[index],
                                  ASTRA_CONFIG_COMMANDS_CAPABILITY) == 0;

        for (uint32_t member = 0u; ; ++member) {
            const AstraAssign *assign = filesystem_library()->assign_member(
                astra_process_vfs_assigns(), mount_names[index], member);
            uint32_t namespace_rights;

            if (assign == NULL)
                break;
            if (count >= ASTRA_LAUNCH_GRANT_MAX)
                return UINT32_MAX;
            astra_capability_name_set(
                grants[count].name, config ? ASTRA_CONFIG_CAPABILITY :
                                             mount_names[index]);
            grants[count].handle = assign->handle;
            grants[count].rights = ASTRA_RIGHT_SIGNAL;
            namespace_rights = assign->rights;
            if (strcmp(mount_names[index], "LIBS") == 0)
                namespace_rights &= ~ASTRA_RIGHT_WRITE;
            grants[count].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE |
                ((namespace_rights & ASTRA_RIGHT_READ) != 0u ?
                     ASTRA_CAPABILITY_FLAG_READ : 0u) |
                ((namespace_rights & ASTRA_RIGHT_WRITE) != 0u ?
                     ASTRA_CAPABILITY_FLAG_WRITE : 0u);
            if (config) {
                char root[ASTRA_CAPABILITY_ROOT_MAX];

                if (astra_config_owner_root(assign->root, "zsh", root,
                                            sizeof(root)) != ASTRA_CONFIG_OK)
                    return UINT32_MAX;
                astra_capability_root_set(grants[count].root, root);
            } else {
                astra_capability_root_set(grants[count].root, assign->root);
            }
            ++count;
        }
    }
    {
        const AstraAssign *work = filesystem_library()->assign_lookup(
            astra_process_vfs_assigns(), "WORK");

        if (work != NULL) {
            if (count >= ASTRA_LAUNCH_GRANT_MAX)
                return UINT32_MAX;
            astra_capability_name_set(grants[count].name, "CWD");
            grants[count].handle = work->handle;
            grants[count].rights = ASTRA_RIGHT_SIGNAL;
            grants[count].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE |
                ((work->rights & ASTRA_RIGHT_READ) != 0u ?
                     ASTRA_CAPABILITY_FLAG_READ : 0u) |
                ((work->rights & ASTRA_RIGHT_WRITE) != 0u ?
                     ASTRA_CAPABILITY_FLAG_WRITE : 0u);
            astra_capability_root_set(grants[count].root, work->root);
            ++count;
        }
    }
    for (uint32_t index = 0u;
         index < sizeof(authority_names) / sizeof(authority_names[0]);
         ++index) {
        const AstraStartupCapability *held = astra_startup_capability(
            session.backend.startup, authority_names[index]);

        if (held == NULL || (held->rights & ASTRA_RIGHT_SIGNAL) == 0u)
            continue;
        if (count >= ASTRA_LAUNCH_GRANT_MAX)
            return UINT32_MAX;
        astra_capability_name_set(grants[count].name, authority_names[index]);
        grants[count].handle = held->handle;
        grants[count].rights = ASTRA_RIGHT_SIGNAL;
        ++count;
    }
    return count;
}

static int flush_terminal(void)
{
    if (astra_terminal_flush(&session.terminal) != ASTRA_TERMINAL_OK)
        return 0;
    return session.backend.present == NULL ||
           session.backend.present(session.backend.context, &session.terminal);
}

static void feed_key(uint32_t key)
{
    if (session.child == 0u)
        return;
    if (console_stream_key(key) == 0) {
        session.pending_key = key;
        session.pending_key_valid = 1u;
    }
}

static int pump_once(void)
{
    uint32_t key = 0u;
    uint32_t rendered = console_stream_pump();
    int input_result = CONSOLE_SESSION_INPUT_NONE;
    int had_key = 0;

    if (session.pending_key_valid &&
        console_stream_key(session.pending_key) > 0) {
        session.pending_key_valid = 0u;
        had_key = 1;
    }
    do {
        if (session.pending_key_valid)
            break;
        input_result = session.backend.next_key != NULL ?
            session.backend.next_key(session.backend.context, &key) : 0;
        if (input_result > 0) {
            had_key = 1;
            feed_key(key);
        }
    } while (input_result > 0);
    if (input_result == CONSOLE_SESSION_INPUT_STOP) {
        session.running = 0;
        return 1;
    }
    if (input_result < 0)
        return 0;
    {
        uint64_t now = astra_clock_monotonic();

        if (session.present_deadline == 0u && (rendered != 0u || had_key))
            session.present_deadline = now + CONSOLE_PRESENT_NS;
        if (session.present_deadline == 0u ||
            now >= session.present_deadline) {
            if (!flush_terminal())
                return 0;
            session.present_deadline = 0u;
        }
    }
    if (!had_key) {
        uint32_t waits[4];
        uint32_t wait_count = 0u;
        uint32_t child_index = ASTRA_WAIT_INDEX_NONE;
        uint32_t sink = console_stream_wait_handle();
        uint32_t source = console_stream_input_wait_handle();

        if (sink != 0u)
            waits[wait_count++] = sink;
        if (source != 0u)
            waits[wait_count++] = source;
        if (session.child != 0u) {
            child_index = wait_count;
            waits[wait_count++] = session.child;
        }
        if (session.backend.wait_handle != 0u)
            waits[wait_count++] = session.backend.wait_handle;
        if (wait_count != 0u) {
            uint64_t deadline = session.backend.idle_poll_ns != 0u ?
                astra_clock_monotonic() + session.backend.idle_poll_ns :
                (session.backend.wait_handle != 0u ?
                     ASTRA_DEADLINE_FOREVER :
                     astra_clock_monotonic() + CONSOLE_INPUT_POLL_NS);
            uint32_t ready = ASTRA_WAIT_INDEX_NONE;
            uint32_t status;

            if (session.present_deadline != 0u &&
                session.present_deadline < deadline)
                deadline = session.present_deadline;
            status = astra_wait_multiple(waits, wait_count, deadline, &ready,
                                         NULL);
            if (status != ASTRA_SYSCALL_OK &&
                status != ASTRA_SYSCALL_TIMED_OUT &&
                !(status == ASTRA_SYSCALL_PEER_DEAD &&
                  ready == child_index))
                return 0;
        } else {
            (void)astra_yield();
        }
    }
    return 1;
}

static void hangup_session(void)
{
    const AstraStartupCapability *posix = astra_startup_capability(
        session.backend.startup, ASTRA_CAPABILITY_POSIX_PROCESS);
    AstraPosixProcessReply child;

    if (posix != NULL) {
        (void)astra_posix_process_tty_signal(posix->handle,
                                             ASTRA_SIGNAL_HANGUP);
        if (astra_posix_process_query(posix->handle, (int32_t)session.child_id,
                                      &child) == ASTRA_STATUS_OK)
            (void)astra_posix_process_signal_group(
                posix->handle, child.group, ASTRA_SIGNAL_HANGUP);
    }
    if (session.child != 0u)
        (void)astra_process_terminate(session.child, ASTRA_SIGNAL_HANGUP);
}

static uint32_t run_zsh(void)
{
    static const char *const argv[] = {"zsh"};
    static const char *const environment_names[] = {
        "HOME", "PATH", "SHELL", "TERM"
    };
    static const char *const environment_values[] = {
        "HOME:", "/commands", "/commands/zsh", "astra-256color"
    };
    AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX] = {0};
    AstraLaunchArguments arguments;
    AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
    char argument_storage[16];
    char environment_storage[96];
    AstraProcessInfo crash = {0};
    uint32_t grant_count;
    uint32_t exit_status = SESSION_NOT_RUN;
    uint32_t status;

    if (astra_launch_arguments_pack(
            &arguments, argument_storage, sizeof(argument_storage),
            ASTRA_LAUNCH_SOURCE_SHELL, 1u, argv) != ASTRA_SYSCALL_OK ||
        astra_launch_environment_pack(
            &arguments, environment_storage, sizeof(environment_storage),
            4u, environment_names, environment_values) != ASTRA_SYSCALL_OK)
        return SESSION_NOT_RUN;
    status = astra_vfs_read_source_open(
        &source, astra_process_vfs_assigns(), "COMMANDS:zsh",
        astra_process_vfs_assign_client, NULL);
    if (status != ASTRA_VFS_OK) {
        write_line("zsh: command not found");
        return SESSION_NOT_RUN;
    }
    grant_count = launch_grants(grants);
    if (grant_count == UINT32_MAX) {
        (void)astra_vfs_read_source_close(&source);
        write_line("zsh: startup capabilities do not fit");
        return SESSION_NOT_RUN;
    }
    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "launching zsh, %u bytes of image", source.length);
    status = astra_launch_stream(
        source.length, astra_vfs_read_source_read_at,
        astra_vfs_read_source_close, &source, grants, grant_count,
        &arguments, &session.child, &session.child_id);
    if (status != ASTRA_SYSCALL_OK) {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                     "zsh launch refused, status %u", status);
        write_line("zsh: could not start");
        session.child = 0u;
        return SESSION_NOT_RUN;
    }
    {
        const AstraStartupCapability *posix = astra_startup_capability(
            session.backend.startup, ASTRA_CAPABILITY_POSIX_PROCESS);

        status = posix != NULL ? astra_posix_process_register(
            posix->handle, session.child, session.child_id, 0u) :
            ASTRA_STATUS_BAD_HANDLE;
    }
    if (status != ASTRA_STATUS_OK) {
        write_line("zsh: process registration failed");
        (void)astra_process_terminate(session.child, ASTRA_SIGNAL_KILL);
        (void)astra_close(session.child);
        session.child = 0u;
        return SESSION_NOT_RUN;
    }
    for (;;) {
        status = astra_process_wait(session.child, 0u, &exit_status);
        if (status != ASTRA_SYSCALL_TIMED_OUT)
            break;
        if (!session.running || !pump_once()) {
            hangup_session();
            status = astra_process_wait(session.child,
                                        ASTRA_DEADLINE_FOREVER, &exit_status);
            break;
        }
    }
    (void)console_stream_drain();
    if (status == ASTRA_SYSCALL_PEER_DEAD &&
        astra_process_info(session.child, &crash) == ASTRA_SYSCALL_OK) {
        astra_terminal_write(&session.terminal, "zsh: crashed: pc ");
        write_hex32(crash.fault_pc);
        astra_terminal_write(&session.terminal, ", address ");
        write_hex32(crash.fault_address);
        astra_terminal_write(&session.terminal, ", vector ");
        write_number(crash.fault_vector);
        astra_terminal_putc(&session.terminal, '\n');
        exit_status = SESSION_NOT_RUN;
    } else if (status != ASTRA_SYSCALL_OK) {
        write_line("zsh: wait failed");
        exit_status = SESSION_NOT_RUN;
    }
    ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "zsh process %u finished with status %u", session.child_id,
                 exit_status);
    (void)astra_close(session.child);
    session.child = 0u;
    return exit_status;
}

uint32_t console_session_run_backend(const ConsoleSessionBackend *backend)
{
    const AstraStartupCapability *posix;
    uint32_t status;

    if (backend == NULL || backend->columns == 0u || backend->rows == 0u ||
        backend->terminal_storage == NULL ||
        backend->terminal_storage_size == 0u || backend->render == NULL ||
        backend->startup == NULL ||
        backend->process_filesystem == NULL ||
        backend->process_filesystem->library == NULL)
        return SESSION_NOT_RUN;
    (void)memset(&session, 0, sizeof(session));
    session.backend = *backend;
    session.running = 1;
    if (astra_terminal_init_capacity(
            &session.terminal, backend->columns, backend->rows,
            backend->terminal_capacity_columns != 0u ?
                backend->terminal_capacity_columns : backend->columns,
            backend->terminal_capacity_rows != 0u ?
                backend->terminal_capacity_rows : backend->rows,
            backend->terminal_storage, backend->terminal_storage_size,
            backend->render, backend->context) != ASTRA_TERMINAL_OK)
        return SESSION_NOT_RUN;
    astra_terminal_set_scroll(&session.terminal, backend->scroll);
    astra_terminal_set_echo(&session.terminal, echo_line, NULL);
    astra_terminal_set_prompt(&session.terminal, prompt_ready, NULL);
    posix = astra_startup_capability(backend->startup,
                                     ASTRA_CAPABILITY_POSIX_PROCESS);
    if (posix == NULL ||
        astra_posix_process_tty_attach(posix->handle) != ASTRA_STATUS_OK ||
        !console_stream_start(&session.terminal, posix->handle)) {
        write_line("zsh: controlling terminal unavailable");
        status = SESSION_NOT_RUN;
    } else {
        console_stream_resize(backend->columns, backend->rows,
                              backend->pixel_width, backend->pixel_height);
        astra_terminal_set_reply(&session.terminal,
                                 console_stream_terminal_reply, NULL);
        astra_terminal_clear(&session.terminal);
        status = run_zsh();
    }
    (void)flush_terminal();
    while (status == SESSION_NOT_RUN && session.running)
        if (!pump_once())
            break;
    return status;
}
