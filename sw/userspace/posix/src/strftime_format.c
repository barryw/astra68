/*
 * Picolibc's strftime only asks its private snprintf for integer fields, but
 * its default implementation pulls the floating stdio scanner and printer
 * into every clock command. This is the one shared implementation of the
 * integer formats used by picolibc's strftime object.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct FormatOutput {
    char *bytes;
    size_t capacity;
    size_t length;
} FormatOutput;

static void
format_byte(FormatOutput *out, char byte)
{
    if (out->length + 1u < out->capacity)
        out->bytes[out->length] = byte;
    ++out->length;
}

static void
format_integer(FormatOutput *out, int64_t value, uint32_t width,
               int precision, int plus, int zero)
{
    char digits[20];
    uint64_t magnitude = value < 0 ?
        (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    uint32_t count = 0u;
    uint32_t minimum;
    uint32_t sign = value < 0 || plus ? 1u : 0u;

    do {
        uint64_t quotient = magnitude / 10u;

        digits[count++] = (char)('0' + magnitude - quotient * 10u);
        magnitude = quotient;
    } while (magnitude != 0u);
    minimum = precision >= 0 ? (uint32_t)precision : 0u;
    if (zero && width > sign && minimum < width - sign)
        minimum = width - sign;
    if (!zero)
        while (width > sign + (count > minimum ? count : minimum)) {
            format_byte(out, ' ');
            --width;
        }
    if (value < 0)
        format_byte(out, '-');
    else if (plus)
        format_byte(out, '+');
    while (minimum > count) {
        format_byte(out, '0');
        --minimum;
    }
    while (count != 0u)
        format_byte(out, digits[--count]);
}

int
__d_snprintf(char *bytes, size_t capacity, const char *format, ...)
{
    FormatOutput out = {bytes, capacity, 0u};
    va_list arguments;

    va_start(arguments, format);
    while (*format != '\0') {
        uint32_t width = 0u;
        int precision = -1;
        int plus = 0;
        int zero = 0;
        uint32_t longs = 0u;

        if (*format != '%') {
            format_byte(&out, *format++);
            continue;
        }
        ++format;
        if (*format == '+') {
            plus = 1;
            ++format;
        }
        if (*format == '0') {
            zero = 1;
            ++format;
        }
        while (*format >= '0' && *format <= '9')
            width = width * 10u + (uint32_t)(*format++ - '0');
        if (*format == '.') {
            ++format;
            if (*format == '*') {
                precision = va_arg(arguments, int);
                ++format;
            } else {
                precision = 0;
                while (*format >= '0' && *format <= '9')
                    precision = precision * 10 + (*format++ - '0');
            }
        }
        while (*format == 'l') {
            ++longs;
            ++format;
        }
        if (*format == 's') {
            const char *text = va_arg(arguments, const char *);

            while (*text != '\0')
                format_byte(&out, *text++);
            ++format;
        } else if (*format == 'd' || *format == 'u') {
            int64_t value;

            if (*format == 'u') {
                if (longs >= 2u)
                    value = (int64_t)va_arg(arguments, unsigned long long);
                else if (longs == 1u)
                    value = (int64_t)va_arg(arguments, unsigned long);
                else
                    value = (int64_t)va_arg(arguments, unsigned int);
            } else if (longs >= 2u) {
                value = va_arg(arguments, long long);
            } else if (longs == 1u) {
                value = va_arg(arguments, long);
            } else {
                value = va_arg(arguments, int);
            }
            format_integer(&out, value, width, precision, plus, zero);
            ++format;
        } else {
            format_byte(&out, '%');
            if (*format != '\0')
                format_byte(&out, *format++);
        }
    }
    va_end(arguments);
    if (capacity != 0u)
        bytes[out.length < capacity ? out.length : capacity - 1u] = '\0';
    return (int)out.length;
}
