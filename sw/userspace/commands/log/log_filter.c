#include "log_filter.h"

#include <stddef.h>
#include <string.h>

static const char *const level_names[] = {
    "debug", "info", "notice", "warning", "error"
};

/* Exactly `count` decimal digits at `text`. */
static int
number(const char *text, uint32_t count, int32_t *out)
{
    int32_t value = 0;

    for (uint32_t index = 0u; index < count; ++index) {
        if (text[index] < '0' || text[index] > '9')
            return 0;
        value = value * 10 + (text[index] - '0');
    }
    *out = value;
    return 1;
}

/* Howard Hinnant's days_from_civil: days since 1970-01-01, proleptic
 * Gregorian. */
static int64_t
days_from_civil(int32_t year, int32_t month, int32_t day)
{
    int32_t era;
    int32_t year_of_era;
    int32_t day_of_year;
    int32_t day_of_era;

    year -= month <= 2;
    era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = year - era * 400;
    day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 +
                 day_of_year;
    return (int64_t)era * 146097 + day_of_era - 719468;
}

int
log_filter_line_time(const char *line, int64_t *unix_seconds)
{
    int32_t year, month, day, hour, minute, second, millis;
    int32_t offset_hour, offset_minute;
    int64_t offset;

    if (line == NULL || unix_seconds == NULL || strlen(line) < 29u ||
        line[4] != '-' || line[7] != '-' || line[10] != 'T' ||
        line[13] != ':' || line[16] != ':' || line[19] != '.' ||
        (line[23] != '+' && line[23] != '-') || line[26] != ':' ||
        !number(line, 4u, &year) || !number(line + 5, 2u, &month) ||
        !number(line + 8, 2u, &day) || !number(line + 11, 2u, &hour) ||
        !number(line + 14, 2u, &minute) || !number(line + 17, 2u, &second) ||
        !number(line + 20, 3u, &millis) ||
        !number(line + 24, 2u, &offset_hour) ||
        !number(line + 27, 2u, &offset_minute))
        return 0;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        minute > 59 || second > 60 || offset_hour > 23 || offset_minute > 59)
        return 0;
    offset = (int64_t)offset_hour * 3600 + offset_minute * 60;
    if (line[23] == '-')
        offset = -offset;
    *unix_seconds = days_from_civil(year, month, day) * 86400 +
                    (int64_t)hour * 3600 + minute * 60 + second - offset;
    return 1;
}

int
log_filter_duration(const char *text, int64_t *seconds)
{
    int64_t value = 0;
    uint32_t length = 0u;

    if (text == NULL || seconds == NULL)
        return 0;
    while (text[length] >= '0' && text[length] <= '9') {
        if (length == 9u)
            return 0;   /* a billion units is not a duration anyone means */
        value = value * 10 + (text[length] - '0');
        ++length;
    }
    if (length == 0u || text[length] == '\0' || text[length + 1u] != '\0')
        return 0;
    switch (text[length]) {
    case 's': *seconds = value; return 1;
    case 'm': *seconds = value * 60; return 1;
    case 'h': *seconds = value * 3600; return 1;
    case 'd': *seconds = value * 86400; return 1;
    default: return 0;
    }
}

int
log_filter_is_boot_marker(const char *line)
{
    return line != NULL && strncmp(line, "--- boot ", 9u) == 0;
}

int
log_filter_level(const char *name)
{
    for (uint32_t index = 0u;
         index < sizeof(level_names) / sizeof(level_names[0]); ++index) {
        if (strcmp(name, level_names[index]) == 0)
            return (int)index;
    }
    return -1;
}

/* Whether [start, start + length) equals the NUL-terminated `word`. */
static int
same(const char *start, size_t length, const char *word)
{
    return strlen(word) == length && memcmp(start, word, length) == 0;
}

int
log_filter_match(const LogFilter *filter, const char *line)
{
    const char *field[4];
    const char *text;
    const char *at = line;
    const char *open;
    const char *close;
    uint32_t pid = 0u;
    int64_t when;
    int level = -1;
    const int empty = !filter->has_level && filter->program == NULL &&
                      !filter->has_pid && filter->stream == NULL &&
                      filter->grep == NULL && !filter->has_since;

    /* A marker ("--- boot ...", "--- events service restarted ...") is not
     * a record: only an empty filter shows it. No record starts "--- ". */
    if (strncmp(line, "--- ", 4u) == 0)
        return empty;
    for (uint32_t index = 0u; index < 4u; ++index) {
        const char *space = strchr(at, ' ');

        field[index] = at;
        if (space == NULL) {
            at = NULL;
            break;
        }
        at = space + 1;
    }
    /* Not a record (damage): only an empty filter shows it. */
    if (at == NULL)
        return empty;
    text = at;
    /* ident[pid]: the pid is the last bracket pair, ident is before it. */
    close = field[3] - 2;
    open = close;
    while (open > field[2] && *open != '[')
        --open;
    if (*close != ']' || *open != '[' || close - open != 9)
        return 0;
    for (const char *digit = open + 1; digit < close; ++digit) {
        uint32_t nibble;

        if (*digit >= '0' && *digit <= '9')
            nibble = (uint32_t)(*digit - '0');
        else if (*digit >= 'a' && *digit <= 'f')
            nibble = (uint32_t)(*digit - 'a') + 10u;
        else
            return 0;
        pid = pid << 4 | nibble;
    }
    for (uint32_t index = 0u;
         index < sizeof(level_names) / sizeof(level_names[0]); ++index) {
        if (same(field[1], (size_t)(field[2] - 1 - field[1]),
                 level_names[index]))
            level = (int)index;
    }
    if (level < 0 || text - field[3] < 3 || text[-2] != ':')
        return 0;
    if (filter->has_level && (uint32_t)level < filter->level)
        return 0;
    if (filter->program != NULL &&
        !same(field[2], (size_t)(open - field[2]), filter->program))
        return 0;
    if (filter->has_pid && pid != filter->pid)
        return 0;
    if (filter->stream != NULL &&
        !same(field[3], (size_t)(text - 2 - field[3]), filter->stream))
        return 0;
    if (filter->grep != NULL && strstr(text, filter->grep) == NULL)
        return 0;
    /* A boot-relative stamp has no wall time, so --last cannot place it. */
    if (filter->has_since && log_filter_line_time(line, &when) &&
        when < filter->since_unix_s)
        return 0;
    return 1;
}
