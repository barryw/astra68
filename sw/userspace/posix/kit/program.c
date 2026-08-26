#include <astra/program.h>

#ifndef ASTRA_POSIX_PROGRAM_NAME
#error "define ASTRA_POSIX_PROGRAM_NAME"
#endif
#ifndef ASTRA_POSIX_PROGRAM_MAJOR
#error "define ASTRA_POSIX_PROGRAM_MAJOR"
#endif
#ifndef ASTRA_POSIX_PROGRAM_MINOR
#error "define ASTRA_POSIX_PROGRAM_MINOR"
#endif
#ifndef ASTRA_POSIX_PROGRAM_PATCH
#error "define ASTRA_POSIX_PROGRAM_PATCH"
#endif
#ifndef ASTRA_POSIX_PROGRAM_AUTHOR
#error "define ASTRA_POSIX_PROGRAM_AUTHOR"
#endif
#ifndef ASTRA_POSIX_PROGRAM_COPYRIGHT
#error "define ASTRA_POSIX_PROGRAM_COPYRIGHT"
#endif

/* Build-supplied provenance lets an unmodified POSIX source tree satisfy the
 * same image contract as a native Astra program. */
ASTRA_PROGRAM(ASTRA_POSIX_PROGRAM_NAME, ASTRA_POSIX_PROGRAM_MAJOR,
              ASTRA_POSIX_PROGRAM_MINOR, ASTRA_POSIX_PROGRAM_PATCH,
              ASTRA_POSIX_PROGRAM_AUTHOR, ASTRA_POSIX_PROGRAM_COPYRIGHT);
