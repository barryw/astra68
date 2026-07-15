#ifndef ASTRA_ATTRIBUTES_H
#define ASTRA_ATTRIBUTES_H

/**
 * @file attributes.h
 * @brief Portable compiler attributes used by the public NDK headers.
 *
 * @addtogroup astra_core
 * @{
 */

#if defined(__GNUC__) || defined(__clang__)
#define ASTRA_NODISCARD __attribute__((warn_unused_result))
#define ASTRA_CLEANUP(function) __attribute__((cleanup(function)))
#else
#define ASTRA_NODISCARD
#define ASTRA_CLEANUP(function)
#endif

/**
 * @def ASTRA_NODISCARD
 * @brief Ask the compiler to diagnose an ignored function result.
 */

/**
 * @def ASTRA_CLEANUP(function)
 * @brief Run @p function with the variable address when its scope ends.
 */

/** @} */

#endif
