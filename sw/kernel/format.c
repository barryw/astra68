#include "format.h"

#include <stdbool.h>
#include <stddef.h>

#define DIGITS_MAX 10u   /* 4294967295 */

static uint32_t put(char *out, uint32_t capacity, uint32_t written, char value)
{
    if (written + 1u < capacity)
        out[written] = value;
    return written + 1u;
}

/*
 * One unsigned number, in any base this kernel prints, right-aligned into
 * `width` with `pad`. The digits are produced backwards into a bounded buffer
 * rather than by recursion: a formatter that can recurse is a formatter that
 * can overrun a supervisor stack from a fault handler.
 */
static uint32_t put_number(char *out, uint32_t capacity, uint32_t written,
                           uint32_t value, uint32_t base, bool upper,
                           uint32_t width, char pad)
{
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower_digits;
    char text[DIGITS_MAX];
    uint32_t length = 0u;

    do {
        text[length++] = digits[value % base];
        value /= base;
    } while (value != 0u && length < DIGITS_MAX);
    while (width > length) {
        written = put(out, capacity, written, pad);
        --width;
    }
    while (length != 0u)
        written = put(out, capacity, written, text[--length]);
    return written;
}

uint32_t kernel_format_list(char *out, uint32_t capacity, const char *format,
                            va_list arguments)
{
    uint32_t written = 0u;
    uint32_t index = 0u;

    if (out == NULL || capacity == 0u || format == NULL)
        return 0u;
    while (format[index] != '\0') {
        char pad = ' ';
        uint32_t width = 0u;
        char specifier;

        if (format[index] != '%') {
            written = put(out, capacity, written, format[index++]);
            continue;
        }
        ++index;
        if (format[index] == '0') {
            pad = '0';
            ++index;
        }
        if (format[index] >= '1' && format[index] <= '9') {
            width = (uint32_t)(format[index] - '0');
            ++index;
        }
        specifier = format[index];
        switch (specifier) {
        case '\0':
            /* A trailing percent is a typo, and it prints as one. */
            written = put(out, capacity, written, '%');
            continue;
        case '%':
            written = put(out, capacity, written, '%');
            break;
        case 'c':
            written = put(out, capacity, written,
                          (char)va_arg(arguments, int));
            break;
        case 's': {
            const char *text = va_arg(arguments, const char *);
            uint32_t length = 0u;

            if (text == NULL)
                text = "(null)";
            while (text[length] != '\0')
                ++length;
            while (width > length) {
                written = put(out, capacity, written, pad);
                --width;
            }
            for (uint32_t at = 0u; at < length; ++at)
                written = put(out, capacity, written, text[at]);
            break;
        }
        case 'd':
        case 'i': {
            int32_t value = va_arg(arguments, int32_t);
            uint32_t magnitude;

            if (value < 0) {
                written = put(out, capacity, written, '-');
                /* INT32_MIN has no positive counterpart; negate in unsigned. */
                magnitude = (uint32_t)0 - (uint32_t)value;
                if (width != 0u)
                    --width;
            } else {
                magnitude = (uint32_t)value;
            }
            written = put_number(out, capacity, written, magnitude, 10u,
                                 false, width, pad);
            break;
        }
        case 'u':
            written = put_number(out, capacity, written,
                                 va_arg(arguments, uint32_t), 10u, false,
                                 width, pad);
            break;
        case 'x':
        case 'X':
            written = put_number(out, capacity, written,
                                 va_arg(arguments, uint32_t), 16u,
                                 specifier == 'X', width, pad);
            break;
        default:
            /*
             * Not a specifier this kernel knows. It is copied through with its
             * percent rather than consuming an argument, so the mistake is
             * visible in the output and the rest of the line still lines up.
             */
            written = put(out, capacity, written, '%');
            written = put(out, capacity, written, specifier);
            break;
        }
        ++index;
    }
    /*
     * Terminated inside the buffer, but the count returned is what the caller
     * asked for. snprintf's rule, and it is the useful one here: a fault
     * report that did not fit can say so instead of quietly claiming to be
     * complete.
     */
    out[written < capacity ? written : capacity - 1u] = '\0';
    return written;
}

uint32_t kernel_format(char *out, uint32_t capacity, const char *format, ...)
{
    va_list arguments;
    uint32_t written;

    va_start(arguments, format);
    written = kernel_format_list(out, capacity, format, arguments);
    va_end(arguments);
    return written;
}
