/*
 * The kernel's printf, against the host's.
 *
 * It is a small formatter and it is the one the panic dump uses, so the
 * properties that matter are the ones a panic depends on: it never writes past
 * the buffer, it always terminates, and it says how much it wanted rather than
 * how much it managed -- a truncated fault report that claims to be complete
 * is worse than one that admits it.
 *
 * The rendering itself is checked against snprintf, because agreeing with the
 * C library is the whole point of using the same specifiers.
 */

#include "format.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Opaque to the optimiser, so `%s` with a null argument reaches the formatter
 * rather than being diagnosed as a mistake at compile time. It is a mistake --
 * and the point is that a panic path meets it and prints something instead of
 * faulting.
 */
static const char *null_string(void)
{
    static const char *value;

    return value;
}

static void same(const char *format, ...)
{
    char ours[128];
    char theirs[128];
    va_list first;
    va_list second;

    va_start(first, format);
    (void)kernel_format_list(ours, sizeof(ours), format, first);
    va_end(first);
    va_start(second, format);
    (void)vsnprintf(theirs, sizeof(theirs), format, second);
    va_end(second);
    if (strcmp(ours, theirs) != 0) {
        printf("FAIL %s: kernel %s, libc %s\n", format, ours, theirs);
        assert(0);
    }
}

int main(void)
{
    char out[16];
    uint32_t written;

    same("%s", "plain");
    same("%s and %s", "one", "two");
    same("%c%c", 'o', 'k');
    same("100%%");
    same("%u", 0u);
    same("%u", 4294967295u);
    same("%d", -1);
    same("%d", 2147483647);
    same("%d", (int)-2147483648);
    same("%x", 0u);
    same("%x", 0xdeadbeefu);
    same("%X", 0xdeadbeefu);
    same("%08x", 0x1234u);
    same("%8x", 0x1234u);
    same("%3u", 7u);
    same("%03u", 7u);
    same("%8s|", "pad");
    same("pc %08x sr %04x vector %u", 0x02046bb0u, 0x2010u, 2u);

    /* A null string is named rather than dereferenced. */
    written = kernel_format(out, sizeof(out), "%s", null_string());
    assert(strcmp(out, "(null)") == 0 && written == 6u);

    /*
     * Truncation. The buffer holds what fits and is terminated; the return is
     * what the caller asked for, so a report that did not fit can say so.
     */
    written = kernel_format(out, sizeof(out), "%s", "0123456789abcdefghij");
    assert(written == 20u);
    assert(strlen(out) == sizeof(out) - 1u);
    assert(strncmp(out, "0123456789abcde", 15) == 0);
    written = kernel_format(out, 1u, "anything");
    assert(written == 8u && out[0] == '\0');

    /*
     * An unknown specifier prints itself instead of eating an argument, and a
     * trailing percent survives as one. Both formats are held in variables:
     * they are deliberately malformed, and the printf attribute on
     * kernel_format is doing its job by rejecting them as literals.
     */
    {
        const char *unknown = "%q%u";
        const char *trailing = "done %";

        written = kernel_format(out, sizeof(out), unknown, 5u);
        assert(strcmp(out, "%q5") == 0);
        (void)written;
        (void)kernel_format(out, sizeof(out), trailing, 0u);
        assert(strcmp(out, "done %") == 0);
    }

    puts("KERNEL FORMAT PASS");
    return 0;
}
