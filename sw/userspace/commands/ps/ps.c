/*
 * `ps` -- the process list, read out of PROC:.
 *
 * It looks like `ps` because that is what it is for, and it gets there without
 * the thing that makes a Unix `ps` work. There is no global process namespace
 * here: `PROCESS_INFO` is scoped to a handle the caller already holds, so a
 * process cannot enumerate its neighbours by counting upwards. What this reads
 * is a *view* -- PROC:, rendered by the supervisor, which can answer because it
 * holds the handles, and visible to this program only because the mount was
 * granted to it. See docs/OBSERVABILITY.md.
 *
 * So the familiar shape survives and the property underneath it changes: a
 * program with no PROC: mount prints nothing and says why, rather than seeing
 * an empty machine.
 *
 * GEN is not decoration. A number alone must never name a process here,
 * because numbers get reused; every identifier this prints carries the
 * generation observed with it, which is what a later control operation has to
 * present for the kernel to accept it.
 */

#include <astra/program.h>
#include <astra/proc.h>
#include <astra/divide.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>

ASTRA_PROGRAM("ps", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    PS_OUTPUT_MAX = 2048u,
};

static char output[PS_OUTPUT_MAX];
static const char header[] =
    "       PID   GEN STATE  PRI  NI THR MEM(K)  CPU%        TIME    RUNS "
    "  CALLS HND COMMAND\n";
static uint32_t output_used;
static uint32_t stdout_handle;
static uint32_t error_handle;

static uint32_t
read_snapshot(AstraProcSnapshot *records, uint32_t capacity, uint32_t *moved)
{
    const AstraAssign *assign = NULL;
    AstraVfsClient *client;
    const uint8_t *bytes = NULL;
    uint64_t size = 0u;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    *moved = 0u;
    status = astra_assign_resolve(astra_process_vfs_assigns(),
                                  "PROC:snapshot", ASTRA_RIGHT_READ, 0u,
                                  path, sizeof(path), &assign);
    if (status != ASTRA_VFS_OK)
        return status;
    client = astra_process_vfs_client_for(assign);
    if (client == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    status = astra_vfs_port_read_path(client, path, &bytes, moved, &size);
    if (status != ASTRA_VFS_OK || *moved != size || *moved > capacity ||
        (*moved != 0u && bytes == NULL))
        return status != ASTRA_VFS_OK ? status : ASTRA_VFS_ERR_PROTOCOL;
    for (uint32_t index = 0u; index < *moved; ++index)
        ((uint8_t *)records)[index] = bytes[index];
    return ASTRA_VFS_OK;
}

static void
say_error(const char *text)
{
    (void)astra_print(error_handle, text);
}

static int
flush_output(void)
{
    uint32_t status = astra_stream_write_all(stdout_handle, output,
                                             output_used);

    output_used = 0u;
    return status == ASTRA_SYSCALL_OK;
}

static int
append_output(const char *text, uint32_t length)
{
    if (length > sizeof(output) - output_used && !flush_output())
        return 0;
    for (uint32_t index = 0u; index < length; ++index)
        output[output_used++] = text[index];
    return 1;
}

static uint32_t
append_text(char *out, uint32_t at, const char *text)
{
    while (*text != '\0')
        out[at++] = *text++;
    return at;
}

static uint32_t
append_number(char *out, uint32_t at, uint32_t value, uint32_t width)
{
    char digits[10];
    uint32_t count = 0u;

    do {
        uint32_t quotient = value / 10u;

        digits[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (width > count) {
        out[at++] = ' ';
        --width;
    }
    while (count != 0u)
        out[at++] = digits[--count];
    return at;
}

static uint32_t
append_number64(char *out, uint32_t at, uint64_t value, uint32_t width)
{
    char digits[20];
    uint32_t count = 0u;

    do {
        uint32_t remainder;

        value = astra_divide_u64(value, 10u, &remainder);
        digits[count++] = (char)('0' + remainder);
    } while (value != 0u);
    while (width > count) {
        out[at++] = ' ';
        --width;
    }
    while (count != 0u)
        out[at++] = digits[--count];
    return at;
}

static uint32_t
append_signed(char *out, uint32_t at, int32_t value, uint32_t width)
{
    uint32_t magnitude = value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    uint32_t digits = 1u;

    for (uint32_t rest = magnitude; rest >= 10u; rest /= 10u)
        ++digits;
    while (width > digits + (value < 0 ? 1u : 0u)) {
        out[at++] = ' ';
        --width;
    }
    if (value < 0)
        out[at++] = '-';
    return append_number(out, at, magnitude, digits);
}

static uint32_t
append_two_digits(char *out, uint32_t at, uint32_t value)
{
    out[at++] = (char)('0' + (value / 10u) % 10u);
    out[at++] = (char)('0' + value % 10u);
    return at;
}

/* The states AstraProcessInfo reports, in the order the kernel numbers them. */
static const char *
state_name(uint32_t value)
{
    static const char *const names[] = {
        "unused", "new", "ready", "run", "wait", "dead"
    };

    return value < sizeof(names) / sizeof(names[0]) ? names[value] : "?";
}

static uint32_t
append_cpu(char *out, uint32_t at, uint64_t runtime, uint64_t elapsed)
{
    uint32_t tenths;

    while (elapsed > UINT32_MAX) {
        runtime >>= 1u;
        elapsed >>= 1u;
    }
    tenths = elapsed == 0u ? 0u : (uint32_t)astra_divide_u64(
        runtime * 1000u, (uint32_t)elapsed, NULL);
    if (tenths > 1000u)
        tenths = 1000u;
    at = append_number(out, at, tenths / 10u, 3u);
    out[at++] = '.';
    return append_number(out, at, tenths % 10u, 1u);
}

static uint32_t
append_time(char *out, uint32_t at, uint64_t runtime)
{
    uint32_t hundredths;
    uint32_t within_hour;
    uint64_t seconds = astra_divide_u64(runtime, 10000000u, NULL);
    uint64_t hours;

    seconds = astra_divide_u64(seconds, 100u, &hundredths);
    hours = astra_divide_u64(seconds, 3600u, &within_hour);
    at = hours < 100u ? append_two_digits(out, at, (uint32_t)hours) :
        append_number64(out, at, hours, 2u);
    out[at++] = ':';
    at = append_two_digits(out, at, within_hour / 60u);
    out[at++] = ':';
    at = append_two_digits(out, at, within_hour % 60u);
    out[at++] = '.';
    return append_two_digits(out, at, hundredths);
}

static int
append_row(uint32_t id, uint32_t generation, const char *state,
           uint32_t priority, uint32_t live,
           uint32_t frames, uint64_t runtime, uint64_t elapsed,
           uint32_t runs, uint32_t syscalls, uint32_t handles,
           const char *name)
{
    char line[112];
    uint32_t at = 0u;
    uint32_t state_length = 0u;

    at = append_number(line, at, id, 10u);
    line[at++] = ' ';
    at = append_number(line, at, generation, 5u);
    line[at++] = ' ';
    while (state[state_length] != '\0')
        ++state_length;
    at = append_text(line, at, state);
    while (state_length++ < 6u)
        line[at++] = ' ';
    line[at++] = ' ';
    at = append_number(line, at, priority, 3u);
    line[at++] = ' ';
    at = append_signed(line, at,
                       (int32_t)ASTRA_PROCESS_PRIORITY_NORMAL -
                           (int32_t)priority,
                       3u);
    line[at++] = ' ';
    at = append_number(line, at, live, 3u);
    line[at++] = ' ';
    at = append_number(line, at, frames * 4u, 6u);
    line[at++] = ' ';
    at = append_cpu(line, at, runtime, elapsed);
    line[at++] = ' ';
    at = append_time(line, at, runtime);
    line[at++] = ' ';
    at = append_number(line, at, runs, 7u);
    line[at++] = ' ';
    at = append_number(line, at, syscalls, 7u);
    line[at++] = ' ';
    at = append_number(line, at, handles, 3u);
    line[at++] = ' ';
    at = append_text(line, at, name);
    line[at++] = '\n';
    return append_output(line, at);
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capability;
    AstraProcSnapshot records[ASTRA_PROCESS_COUNT_MAX] = {0};
    uint32_t listed = 0u;
    uint32_t moved = 0u;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    capability = astra_startup_capability(startup, "STDOUT");
    if (capability != NULL)
        stdout_handle = capability->handle;
    capability = astra_startup_capability(startup, "STDERR");
    if (capability != NULL)
        error_handle = capability->handle;
    if (stdout_handle == 0u)
        return ASTRA_STATUS_ACCESS;
    if (error_handle == 0u)
        error_handle = stdout_handle;
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK) {
        say_error("ps: filesystem unavailable\n");
        return (int)status;
    }
    status = read_snapshot(records, sizeof(records), &moved);
    if (status != ASTRA_VFS_OK) {
        say_error("ps: PROC: not granted to this program\n");
        astra_process_vfs_close();
        return (int)status;
    }
    if (moved % sizeof(records[0]) != 0u)
        status = ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK &&
        !append_output(header, sizeof(header) - 1u))
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK) {
        for (uint32_t index = 0u;
             status == ASTRA_VFS_OK && index < moved / sizeof(records[0]);
             ++index) {
            const AstraProcessInfo *process = &records[index].process;

            if (!append_row(process->id, process->generation,
                            state_name(process->thread_state),
                            process->default_priority, process->live_threads,
                            process->resident_frames,
                            process->runtime_ns, process->elapsed_ns,
                            process->run_count, process->syscall_count,
                            process->handle_references,
                            records[index].name))
                status = ASTRA_VFS_ERR_IO;
            else
                ++listed;
        }
    }
    if (status == ASTRA_VFS_OK) {
        AstraProcessInfo self = {0};

        self.size = sizeof(self);
        if (astra_process_info(startup->process_handle, &self) !=
                ASTRA_SYSCALL_OK ||
            !append_row(self.id, self.generation,
                        state_name(self.thread_state), self.default_priority,
                        self.live_threads,
                        self.resident_frames, self.runtime_ns,
                        self.elapsed_ns, self.run_count, self.syscall_count,
                        self.handle_references, "COMMANDS:ps"))
            status = ASTRA_VFS_ERR_IO;
        else
            ++listed;
    }
    astra_process_vfs_close();
    if (status == ASTRA_VFS_OK && !flush_output())
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_ERR_IO)
        return (int)status;
    return listed != 0u ? 0 : (int)ASTRA_STATUS_NOT_FOUND;
}
