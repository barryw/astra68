#ifndef ASTRA_KERNEL_FORMAT_H
#define ASTRA_KERNEL_FORMAT_H

#include <stdarg.h>
#include <stdint.h>

/*
 * The kernel's own printf, because it links no C library.
 *
 * Every freestanding kernel grows one: Linux keeps `vsprintf.c` for printk,
 * the BSDs keep it in libkern. This is the small end of that -- what the boot
 * report, the panic dump and the fault reports actually use -- and it is
 * deliberately not a general one.
 *
 * Supported: %s %c %% and %d %i %u %x %X on 32-bit values, with a zero flag
 * and a width of one digit (%08x, %3u). A 64-bit value is printed as two
 * halves by its caller; nothing here allocates, and nothing here can fault on
 * a length, because the output is bounded and always terminated.
 *
 * An unsupported specifier is copied through verbatim rather than swallowed,
 * so a mistake shows up in the output instead of eating the argument list.
 *
 * Returns what the format asked for, which may be more than fits -- snprintf's
 * rule. The buffer is always terminated inside itself, so a caller that
 * ignores the count still has a string, and one that checks it can tell a
 * truncated report from a complete one.
 */
uint32_t kernel_format(char *out, uint32_t capacity, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
uint32_t kernel_format_list(char *out, uint32_t capacity, const char *format,
                            va_list arguments);

#endif
