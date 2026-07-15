#ifndef ASTRA_TYPES_H
#define ASTRA_TYPES_H

/**
 * @file types.h
 * @brief Common fixed-width types and result codes.
 */

#include <stdint.h>

#ifdef __cplusplus
#define ASTRA_EXTERN_C_BEGIN extern "C" {
#define ASTRA_EXTERN_C_END }
#else
#define ASTRA_EXTERN_C_BEGIN
#define ASTRA_EXTERN_C_END
#endif

/**
 * @def ASTRA_EXTERN_C_BEGIN
 * @brief Begin a block of declarations with C linkage in C++ builds.
 */

/**
 * @def ASTRA_EXTERN_C_END
 * @brief End a block opened by ::ASTRA_EXTERN_C_BEGIN.
 */

/**
 * @defgroup astra_core Core types
 * @brief Common language and error-handling contracts used throughout the NDK.
 * @{
 */

/**
 * Result of an NDK operation.
 *
 * Zero is success and negative values are failures. Additional negative error
 * codes may be introduced in later source-compatible NDK releases; callers
 * must not assume the values below are exhaustive.
 *
 * @since 0.1.0
 */
typedef int32_t AstraResult;

/** Standard NDK result values. */
enum {
    /** The operation completed successfully. */
    ASTRA_OK = 0,
    /** The requested hardware or service is not available. */
    ASTRA_ERROR_NOT_PRESENT = -1,
    /** A pointer, value, flag combination, or object state is invalid. */
    ASTRA_ERROR_INVALID_ARGUMENT = -2,
    /** The implementation cannot provide the requested valid operation. */
    ASTRA_ERROR_UNSUPPORTED = -3,
    /** A resource is currently owned or otherwise unavailable. */
    ASTRA_ERROR_BUSY = -4,
    /** The operation did not complete before its deadline. */
    ASTRA_ERROR_TIMEOUT = -5,
    /** The caller lacks a required capability or access right. */
    ASTRA_ERROR_PERMISSION = -6,
    /** An implementation capacity limit prevented allocation. */
    ASTRA_ERROR_NO_RESOURCES = -7,
    /** A handle is invalid, stale, or has already been consumed. */
    ASTRA_ERROR_INVALID_HANDLE = -8,
    /** A device or transport operation failed. */
    ASTRA_ERROR_IO = -9,
    /** A caller-provided output buffer cannot hold the complete value. */
    ASTRA_ERROR_BUFFER_TOO_SMALL = -10
};

/**
 * Signed 26.6 fixed-point scalar used for device-independent layout metrics.
 *
 * The value 64 represents one whole unit. Arithmetic that can exceed the
 * signed 32-bit range must use a wider intermediate and clamp before storing.
 *
 * @since 0.1.0
 */
typedef int32_t AstraFixed26_6;

/** One whole unit in ::AstraFixed26_6 representation. */
#define ASTRA_FIXED26_6_ONE INT32_C(64)

/**
 * Unpremultiplied 8-bit sRGB color with alpha.
 *
 * Alpha zero is transparent and 255 is opaque. Conversion to a destination
 * pixel format and any premultiplication happen inside the owning service.
 *
 * @since 0.1.0
 */
typedef struct AstraColorRGBA8 {
    /** Red sRGB channel. */
    uint8_t red;
    /** Green sRGB channel. */
    uint8_t green;
    /** Blue sRGB channel. */
    uint8_t blue;
    /** Alpha channel. */
    uint8_t alpha;
} AstraColorRGBA8;

/** @} */

#endif
