#include "ps_support.h"

#include <astra/divide.h>
#include <astra/process.h>
#include <astra/vfs_service.h>

#include <stddef.h>

uint32_t
astra_ps_snapshot_allocate(uint64_t bytes, AstraPsReallocate reallocate,
                           AstraProcSnapshot **records)
{
    if (reallocate == NULL || records == NULL)
        return ASTRA_VFS_ERR_PROTOCOL;
    *records = NULL;
    if (bytes > SIZE_MAX || bytes % sizeof(**records) != 0u)
        return ASTRA_VFS_ERR_PROTOCOL;
    if (bytes == 0u)
        return ASTRA_VFS_OK;
    *records = reallocate(NULL, (size_t)bytes);
    return *records != NULL ? ASTRA_VFS_OK : ASTRA_VFS_ERR_LIMIT;
}

typedef struct RowWriter {
    char *out;
    uint32_t at;
} RowWriter;

static void byte(RowWriter *writer, char value)
{
    if (writer->out != NULL)
        writer->out[writer->at] = value;
    ++writer->at;
}

static void text(RowWriter *writer, const char *value)
{
    while (*value != '\0')
        byte(writer, *value++);
}

static void number(RowWriter *writer, uint64_t value, uint32_t width)
{
    char reverse[20];
    uint32_t count = 0u;

    do {
        uint64_t quotient = astra_divide_u64_u64(value, 10u, NULL);

        reverse[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (width > count) {
        byte(writer, ' ');
        --width;
    }
    while (count != 0u)
        byte(writer, reverse[--count]);
}

static void signed_number(RowWriter *writer, int32_t value, uint32_t width)
{
    uint32_t magnitude = value < 0 ? (uint32_t)(-(int64_t)value) :
                                     (uint32_t)value;
    uint32_t count = 1u;

    for (uint32_t rest = magnitude; rest >= 10u; rest /= 10u)
        ++count;
    while (width > count + (value < 0 ? 1u : 0u)) {
        byte(writer, ' ');
        --width;
    }
    if (value < 0)
        byte(writer, '-');
    number(writer, magnitude, count);
}

static void two_digits(RowWriter *writer, uint32_t value)
{
    byte(writer, (char)('0' + (value / 10u) % 10u));
    byte(writer, (char)('0' + value % 10u));
}

static const char *state_name(const AstraProcessInfo *process)
{
    static const char *const names[] = {
        "unused", "new", "ready", "run", "wait", "dead"
    };

    if (process->suspended != 0u)
        return "stop";
    return process->thread_state < sizeof(names) / sizeof(names[0]) ?
        names[process->thread_state] : "?";
}

static void cpu(RowWriter *writer, uint64_t runtime, uint64_t elapsed)
{
    uint64_t scaled = elapsed == 0u ? 0u :
        astra_multiply_divide_u64(runtime, 1000u, elapsed);
    uint32_t tenths = scaled > 1000u ? 1000u : (uint32_t)scaled;

    number(writer, tenths / 10u, 3u);
    byte(writer, '.');
    number(writer, tenths % 10u, 1u);
}

static void runtime(RowWriter *writer, uint64_t nanoseconds)
{
    uint32_t hundredths;
    uint32_t within_hour;
    uint64_t seconds = astra_divide_u64(nanoseconds, 10000000u, NULL);
    uint64_t hours;

    seconds = astra_divide_u64(seconds, 100u, &hundredths);
    hours = astra_divide_u64(seconds, 3600u, &within_hour);
    if (hours < 100u)
        two_digits(writer, (uint32_t)hours);
    else
        number(writer, hours, 2u);
    byte(writer, ':');
    two_digits(writer, within_hour / 60u);
    byte(writer, ':');
    two_digits(writer, within_hour % 60u);
    byte(writer, '.');
    two_digits(writer, hundredths);
}

static int format(RowWriter *writer, const AstraProcSnapshot *record)
{
    const AstraProcessInfo *process = &record->process;
    const char *state = state_name(process);
    uint32_t state_length = 0u;
    uint32_t name_length = 0u;

    while (state[state_length] != '\0')
        ++state_length;
    while (name_length < ASTRA_PROC_NAME_MAX &&
           record->name[name_length] != '\0')
        ++name_length;
    if (name_length == ASTRA_PROC_NAME_MAX ||
        process->default_priority < ASTRA_PROCESS_PRIORITY_MIN ||
        process->default_priority > ASTRA_PROCESS_PRIORITY_MAX)
        return 0;
    number(writer, process->id, 10u);
    byte(writer, ' ');
    number(writer, process->generation, 5u);
    byte(writer, ' ');
    text(writer, state);
    while (state_length++ < 6u)
        byte(writer, ' ');
    byte(writer, ' ');
    number(writer, process->default_priority, 3u);
    byte(writer, ' ');
    signed_number(writer,
                  (int32_t)ASTRA_PROCESS_PRIORITY_NORMAL -
                      (int32_t)process->default_priority,
                  3u);
    byte(writer, ' ');
    number(writer, process->live_threads, 3u);
    byte(writer, ' ');
    number(writer, (uint64_t)process->resident_frames * 4u, 6u);
    byte(writer, ' ');
    cpu(writer, process->runtime_ns, process->elapsed_ns);
    byte(writer, ' ');
    runtime(writer, process->runtime_ns);
    byte(writer, ' ');
    number(writer, process->run_count, 7u);
    byte(writer, ' ');
    number(writer, process->syscall_count, 7u);
    byte(writer, ' ');
    number(writer, process->handle_references, 3u);
    byte(writer, ' ');
    for (uint32_t index = 0u; index < name_length; ++index)
        byte(writer, record->name[index]);
    byte(writer, '\n');
    return 1;
}

uint32_t
astra_ps_format_row(char *out, uint32_t capacity,
                    const AstraProcSnapshot *record)
{
    RowWriter measure = {0};
    RowWriter writer = {out, 0u};

    if (record == NULL || !format(&measure, record) || measure.at == 0u)
        return 0u;
    if (out == NULL)
        return measure.at;
    if (capacity < measure.at)
        return 0u;
    if (!format(&writer, record) || writer.at != measure.at)
        return 0u;
    return writer.at;
}
