#include <sys/sysmacros.h>

_Static_assert(sizeof(dev_t) == 8, "Astra device identifiers use 64 bits");
_Static_assert(major(makedev(0x1234u, 0x5678u)) == 0x1234u,
               "major number must survive encoding");
_Static_assert(minor(makedev(0x1234u, 0x5678u)) == 0x5678u,
               "minor number must survive encoding");
_Static_assert(major(makedev(0u, 0xffffffffu)) == 0u,
               "minor number must not spill into the major half");
_Static_assert(minor(makedev(0xffffffffu, 0u)) == 0u,
               "major number must not spill into the minor half");
