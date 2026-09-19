#include <launch_report.h>

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

static void append_text(char *out, uint32_t *at, const char *text)
{
    while (*text != '\0')
        out[(*at)++] = *text++;
}

static void append_number(char *out, uint32_t *at, uint64_t value)
{
    char reverse[20];
    uint32_t count = 0u;

    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u)
        out[(*at)++] = reverse[--count];
}

uint32_t
supervisor_launch_report_format(
    char *out, uint32_t capacity, const char *prefix, const char *path,
    const SupervisorLaunchReportField *fields, uint32_t field_count)
{
    uint32_t required = 0u;
    uint32_t at = 0u;

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
    append_text(out, &at, prefix);
    append_text(out, &at, path);
    for (uint32_t index = 0u; index < field_count; ++index) {
        append_text(out, &at, fields[index].label);
        append_number(out, &at, fields[index].value);
    }
    out[at] = '\0';
    return at;
}
