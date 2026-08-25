#ifndef ASTRA_POSIX_PATH_H
#define ASTRA_POSIX_PATH_H

#include <stdint.h>

/* Zero names the synthetic POSIX root; one names `native`. */
int astra_posix_path_resolve(const char *cwd, const char *path,
                             char *normal, uint32_t normal_capacity,
                             char *native, uint32_t native_capacity);

#endif
