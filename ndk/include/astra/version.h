#ifndef ASTRA_VERSION_H
#define ASTRA_VERSION_H

/**
 * @file version.h
 * @brief Compile-time NDK version information.
 *
 * @defgroup astra_version Version information
 * @brief Compile-time source API version checks.
 * @{
 */

/** NDK major version. */
#define ASTRA_NDK_VERSION_MAJOR 0
/** NDK minor version. */
#define ASTRA_NDK_VERSION_MINOR 1
/** NDK patch version. */
#define ASTRA_NDK_VERSION_PATCH 0
/** Packed version value: `major << 16 | minor << 8 | patch`. */
#define ASTRA_NDK_VERSION ((ASTRA_NDK_VERSION_MAJOR << 16) | \
                           (ASTRA_NDK_VERSION_MINOR << 8) | \
                           ASTRA_NDK_VERSION_PATCH)

/** @} */

#endif
