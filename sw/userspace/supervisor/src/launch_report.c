#include <launch_report.h>

#include <astra/string.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int add_length(uint32_t *total, const char *text)
{
    size_t length;

    if (total == NULL || text == NULL)
        return 0;
    length = strlen(text);
    if (length > UINT32_MAX)
        return 0;
    if (*total > UINT32_MAX - length)
        return 0;
    *total += (uint32_t)length;
    return 1;
}

static uint32_t digits(uint64_t value)
{
    uint32_t count = 1u;

    while (value >= 10u) {
        value /= 10u;
        ++count;
    }
    return count;
}

uint32_t
supervisor_launch_report_format(
    char *out, uint32_t capacity, const char *prefix, const char *path,
    const SupervisorLaunchReportField *fields, uint32_t field_count)
{
    uint32_t required = 0u;
    AstraString line;

    if (prefix == NULL || path == NULL ||
        (fields == NULL && field_count != 0u) ||
        !add_length(&required, prefix) || !add_length(&required, path))
        return 0u;
    for (uint32_t index = 0u; index < field_count; ++index) {
        uint32_t count = digits(fields[index].value);

        if (!add_length(&required, fields[index].label) ||
            required > UINT32_MAX - count)
            return 0u;
        required += count;
    }
    if (required == 0u || required == UINT32_MAX)
        return 0u;
    if (out == NULL)
        return required;
    if (capacity <= required) {
        if (capacity != 0u)
            out[0] = '\0';
        return 0u;
    }
    /* The capacity was checked against the exact length above. */
    astra_string_init(&line, out, capacity);
    (void)astra_string_append(&line, prefix);
    (void)astra_string_append(&line, path);
    for (uint32_t index = 0u; index < field_count; ++index) {
        (void)astra_string_append(&line, fields[index].label);
        (void)astra_string_append_u64(&line, fields[index].value);
    }
    return line.length;
}
