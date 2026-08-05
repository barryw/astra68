/*
 * Assertion failure handling for Astra userspace.
 *
 * Reached only from the freestanding <assert.h>, which vendored code compiles
 * against. There is nowhere to print to, so the diagnosis has to survive in
 * the exit status: the kernel turns any exit of the initial image into a
 * panic, and the panic reports that status.
 *
 * The line number is carried and the file name is not, because the status is
 * one word and a halfword of it is already the tag. A line number plus the
 * knowledge that the assertion came from the filesystem library is enough to
 * find the site; a status that said only "assertion failed" would not be.
 */

#include <astra/runtime.h>

void
astra_assert_failed(const char *file, unsigned int line,
                    const char *expression)
{
    (void)file;
    (void)expression;
    astra_process_exit(ASTRA_ASSERT_STATUS_TAG | (line & 0xffffu));
}
