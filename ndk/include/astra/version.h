#ifndef ASTRA_VERSION_H
#define ASTRA_VERSION_H

/**
 * @file version.h
 * @brief Compile-time Astra OS and NDK version information.
 *
 * @defgroup astra_version Version information
 * @brief Compile-time source API version checks.
 * @{
 */

/** Astra OS major version. */
#define ASTRA_OS_VERSION_MAJOR 0
/** Astra OS minor version. */
#define ASTRA_OS_VERSION_MINOR 1
/** Astra OS patch version. */
#define ASTRA_OS_VERSION_PATCH 0
/** Complete Astra OS SemVer, including its pre-release identifier. */
#define ASTRA_OS_VERSION_STRING "0.1.0-dev"

/** NDK major version; it always matches the Astra OS. */
#define ASTRA_NDK_VERSION_MAJOR ASTRA_OS_VERSION_MAJOR
/** NDK minor version; it always matches the Astra OS. */
#define ASTRA_NDK_VERSION_MINOR ASTRA_OS_VERSION_MINOR
/** NDK patch version; it always matches the Astra OS. */
#define ASTRA_NDK_VERSION_PATCH ASTRA_OS_VERSION_PATCH
/** Complete NDK SemVer; it always matches the Astra OS. */
#define ASTRA_NDK_VERSION_STRING ASTRA_OS_VERSION_STRING
/** Packed version value: `major << 16 | minor << 8 | patch`. */
#define ASTRA_NDK_VERSION ((ASTRA_NDK_VERSION_MAJOR << 16) | \
                           (ASTRA_NDK_VERSION_MINOR << 8) | \
                           ASTRA_NDK_VERSION_PATCH)

/** @} */

#endif
