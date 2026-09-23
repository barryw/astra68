#ifndef ASTRA_POSIX_SYS_SYSMACROS_H
#define ASTRA_POSIX_SYS_SYSMACROS_H

#include <limits.h>
#include <sys/stat.h>

/* Picolibc stores the major and minor numbers in the two halves of dev_t. */
#define ASTRA_POSIX_DEVICE_MINOR_BITS (sizeof(dev_t) * CHAR_BIT / 2u)
#ifndef major
#define major(device) ((unsigned int)((dev_t)(device) >> \
    ASTRA_POSIX_DEVICE_MINOR_BITS))
#endif
#ifndef minor
#define minor(device) ((unsigned int)((dev_t)(device) & \
    (((dev_t)1u << ASTRA_POSIX_DEVICE_MINOR_BITS) - 1u)))
#endif
#ifndef makedev
#define makedev(major_number, minor_number) \
    (((dev_t)(major_number) << ASTRA_POSIX_DEVICE_MINOR_BITS) | \
     ((dev_t)(minor_number) & \
      (((dev_t)1u << ASTRA_POSIX_DEVICE_MINOR_BITS) - 1u)))
#endif

#endif
