/*
 * One record, one line, always: control bytes are escaped so a program cannot
 * split its line or dress the next one up as someone else's.
 */

#include <astra/log_format.h>

#include <astra/civil.h>
#include <astra/divide.h>
#include <astra/event.h>
#include <astra/log_ring.h>

#include <stddef.h>

static const char *const level_names[] = {
    "debug", "info", "notice", "warning", "error" };
static const char *const stream_names[] = {
    "log", "stdout", "stderr", "kernel" };

typedef struct Out { char *at; uint32_t used; uint32_t capacity; } Out;

static void put(Out *o, char c)
{ if (o->used < o->capacity) o->at[o->used++] = c; }
static void puts_(Out *o, const char *s) { while (*s) put(o, *s++); }
static void digits(Out *o, uint32_t value, uint32_t width)
{
    char buffer[10]; uint32_t n = 0u;
    do { buffer[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (value != 0u || n < width);
    while (n != 0u) put(o, buffer[--n]);
}
static void hex(Out *o, uint32_t value, uint32_t width)
{
    static const char x[] = "0123456789abcdef";
    for (uint32_t shift = width * 4u; shift != 0u; shift -= 4u)
        put(o, x[(value >> (shift - 4u)) & 0xfu]);
}

static int
civil(const AstraLogLine *line, AstraCivilTime *out_time)
{
    AstraTimeZone zone;

    if (!line->wall_valid)
        return 0;
    astra_civil_zone_unpack(line->utc_offset, line->zone, &zone);
    return astra_civil_from_unix_ns_zone(line->wall_ns, &zone, out_time);
}

uint32_t
astra_log_local_day(const AstraLogLine *line)
{
    AstraCivilTime time;

    if (!civil(line, &time) || time.year < 0)
        return 0u;
    return (uint32_t)time.year * 10000u + time.month * 100u + time.day;
}

uint32_t
astra_log_format(char *out, uint32_t capacity, const AstraLogLine *line)
{
    Out o = { out, 0u, capacity };
    AstraCivilTime time;

    if (civil(line, &time)) {
        uint32_t offset = (uint32_t)(time.utc_offset < 0 ?
                                     -time.utc_offset : time.utc_offset);

        digits(&o, (uint32_t)time.year, 4u); put(&o, '-');
        digits(&o, time.month, 2u); put(&o, '-');
        digits(&o, time.day, 2u); put(&o, 'T');
        digits(&o, time.hour, 2u); put(&o, ':');
        digits(&o, time.minute, 2u); put(&o, ':');
        digits(&o, time.second, 2u); put(&o, '.');
        digits(&o, time.nanosecond / 1000000u, 3u);
        put(&o, time.utc_offset < 0 ? '-' : '+');
        digits(&o, offset / 3600u, 2u); put(&o, ':');
        digits(&o, offset / 60u % 60u, 2u);
    } else {
        uint32_t rest = 0u;
        uint64_t seconds = astra_divide_u64(line->monotonic_ns, 1000000000u,
                                            &rest);

        puts_(&o, "boot+");
        digits(&o, (uint32_t)seconds, 1u); put(&o, '.');
        digits(&o, rest / 1000000u, 3u);
    }
    put(&o, ' ');
    puts_(&o, level_names[line->level <= 4u ? line->level : 4u]);
    put(&o, ' ');
    puts_(&o, line->ident != NULL && line->ident[0] != '\0' ?
              line->ident : "?");
    put(&o, '['); hex(&o, line->pid, 8u); puts_(&o, "] ");
    puts_(&o, stream_names[line->stream <= 3u ? line->stream : 0u]);
    puts_(&o, ": ");
    for (uint32_t index = 0u; index < line->length; ++index) {
        unsigned char c = (unsigned char)line->text[index];

        if (c < 0x20u || c == 0x7fu) {
            puts_(&o, "\\x"); hex(&o, c, 2u);
        } else if (c == '\\') {
            puts_(&o, "\\\\");
        } else {
            put(&o, (char)c);
        }
    }
    if (line->truncated)
        puts_(&o, " [truncated]");
    /* The newline always fits: back up over text if the buffer is full. */
    if (o.used == o.capacity && o.capacity != 0u)
        --o.used;
    put(&o, '\n');
    return o.used;
}
