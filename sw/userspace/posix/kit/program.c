#include <astra/program.h>
#include <astra/posix.h>
#include <astra/runtime.h>

#include <stdint.h>

#if !defined(ASTRA_POSIX_EXTERNAL_PROGRAM_IDENTITY)
# ifndef ASTRA_POSIX_PROGRAM_NAME
#  error "define ASTRA_POSIX_PROGRAM_NAME"
# endif
# ifndef ASTRA_POSIX_PROGRAM_MAJOR
#  error "define ASTRA_POSIX_PROGRAM_MAJOR"
# endif
# ifndef ASTRA_POSIX_PROGRAM_MINOR
#  error "define ASTRA_POSIX_PROGRAM_MINOR"
# endif
# ifndef ASTRA_POSIX_PROGRAM_PATCH
#  error "define ASTRA_POSIX_PROGRAM_PATCH"
# endif
# ifndef ASTRA_POSIX_PROGRAM_AUTHOR
#  error "define ASTRA_POSIX_PROGRAM_AUTHOR"
# endif
# ifndef ASTRA_POSIX_PROGRAM_COPYRIGHT
#  error "define ASTRA_POSIX_PROGRAM_COPYRIGHT"
# endif

/* Build-supplied provenance lets an unmodified POSIX source tree satisfy the
 * same image contract as a native Astra program. A source-native Astra
 * command already owns its ASTRA_PROGRAM record and selects the external
 * identity mode so the same entry adapter can be reused without duplicating
 * metadata. */
ASTRA_PROGRAM(ASTRA_POSIX_PROGRAM_NAME, ASTRA_POSIX_PROGRAM_MAJOR,
              ASTRA_POSIX_PROGRAM_MINOR, ASTRA_POSIX_PROGRAM_PATCH,
              ASTRA_POSIX_PROGRAM_AUTHOR, ASTRA_POSIX_PROGRAM_COPYRIGHT);
#endif

extern int main(int argc, char **argv);

/* Requiring this symbol keeps the standard-main adapter in static recovery
 * images; normal dynamic programs link this object explicitly. */
const uint32_t astra_posix_entry_contract = 1u;

int
astra_main(const AstraStartupInfo *startup)
{
    return astra_posix_enter(startup, main);
}
