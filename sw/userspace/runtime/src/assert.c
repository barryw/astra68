/*
 * Assertion failure handling for Astra userspace.
 *
 * Reached only from the freestanding <assert.h>, which vendored code compiles
 * against.
 *
 * The file name and the expression go out on the diagnostic channel, and the
 * line number still goes into the exit status. Both, because they fail in
 * different circumstances: a build with no debug surface refuses the write and
 * leaves only the status, and a status is one word that cannot say which of
 * two files had an assertion on line 231.
 *
 * The log is attempted first and its refusal ignored. An assertion that could
 * not report itself must still exit with the same status it always did.
 */

#include <astra/runtime.h>

#include "log_internal.h"

void
astra_assert_failed(const char *file, unsigned int line,
                    const char *expression)
{
    (void)astra_log_assertion(file, line, expression);
    astra_process_exit(ASTRA_ASSERT_STATUS_TAG | (line & 0xffffu));
}
