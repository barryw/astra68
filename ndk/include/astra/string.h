#ifndef ASTRA_STRING_H
#define ASTRA_STRING_H

/**
 * @file string.h
 * @brief Bounded strings: building, copying, comparing, UTF-8 truncation.
 *
 * Every function takes the destination's capacity and never writes past it.
 * Every result is NUL-terminated. A string that did not fit is reported,
 * never silently shortened: the builder records `truncated`, and the one-shot
 * helpers return zero. Truncation always lands on a UTF-8 scalar boundary,
 * so a shortened string is still valid text.
 *
 * Provided by runtime.library (ASTRA_RUNTIME_1.9) and libastrart.
 */

#include <stdint.h>

/** A string built into caller storage. */
typedef struct AstraString {
    char *text; /**< Caller storage, always NUL-terminated. */
    uint32_t capacity; /**< Bytes of storage, including the NUL. */
    uint32_t length; /**< Bytes before the NUL. */
    uint32_t truncated; /**< Nonzero once an append did not fit. */
} AstraString;

/** Start an empty string in @p storage. @p capacity must be at least 1;
 * with zero capacity or NULL storage the builder refuses every append.
 * @param string Builder to initialize; NULL is ignored.
 * @param storage Destination buffer, or NULL for an always-truncated
 * builder.
 * @param capacity Bytes in @p storage, including the NUL.
 */
void astra_string_init(AstraString *string, char *storage, uint32_t capacity);

/** Append NUL-terminated @p text. @return 1 if all of it fit, else 0 (the
 * longest prefix ending on a scalar boundary is kept and `truncated` set).
 * A truncated builder refuses every later append.
 * @param string Builder from astra_string_init().
 * @param text Text to append; NULL returns 0 without effect.
 */
int astra_string_append(AstraString *string, const char *text);
/** Append @p length bytes; see ::astra_string_append.
 * @param string Builder from astra_string_init().
 * @param bytes Bytes to append; may be NULL only when @p length is zero.
 * @param length Number of bytes in @p bytes.
 * @return 1 if all of it fit, else 0.
 */
int astra_string_append_bytes(AstraString *string, const char *bytes,
                              uint32_t length);
/** Append one byte; see ::astra_string_append.
 * @param string Builder from astra_string_init().
 * @param value Byte to append.
 * @return 1 if it fit, else 0.
 */
int astra_string_append_char(AstraString *string, char value);
/** Append @p value in decimal; see ::astra_string_append.
 * @param string Builder from astra_string_init().
 * @param value Value to render in decimal.
 * @return 1 if it fit, else 0.
 */
int astra_string_append_u64(AstraString *string, uint64_t value);
/** Append signed @p value in decimal, with a leading '-' when negative.
 * @param string Builder from astra_string_init().
 * @param value Value to render in decimal.
 * @return 1 if it fit, else 0.
 */
int astra_string_append_i64(AstraString *string, int64_t value);
/** Append @p value as exactly @p digits lowercase hex digits (1 through 16,
 * zero-padded, high digits dropped); see ::astra_string_append.
 * @param string Builder from astra_string_init().
 * @param value Value to render in hex.
 * @param digits Digit count, 1 through 16.
 * @return 1 if it fit; 0 if @p digits is outside 1..16 (the builder is left
 * unchanged) or the rendered digits did not fit.
 */
int astra_string_append_hex(AstraString *string, uint64_t value,
                            uint32_t digits);

/** Copy @p text whole. @return 1 on success; 0 when it does not fit, with
 * @p out left empty.
 * @param out Destination buffer.
 * @param capacity Bytes in @p out, including the NUL.
 * @param text Source text; NULL is treated as not fitting.
 */
int astra_string_copy(char *out, uint32_t capacity, const char *text);

/** Append @p text to the NUL-terminated string in @p out, whole.
 * @return 1 on success; 0 when it does not fit, with @p out unchanged.
 * @param out NUL-terminated destination buffer.
 * @param capacity Bytes in @p out, including the NUL.
 * @param text Source text; NULL returns 0 without effect.
 */
int astra_string_concat(char *out, uint32_t capacity, const char *text);

/** Nonzero when @p text begins with @p prefix.
 * @param text Text to examine; NULL is never prefixed.
 * @param prefix Prefix to look for; NULL never matches.
 * @return 1 if @p text begins with @p prefix, else 0.
 */
int astra_string_has_prefix(const char *text, const char *prefix);
/** Nonzero when @p text ends with @p suffix.
 * @param text Text to examine; NULL never ends with anything.
 * @param suffix Suffix to look for; NULL never matches.
 * @return 1 if @p text ends with @p suffix, else 0.
 */
int astra_string_has_suffix(const char *text, const char *suffix);
/** strcmp order with ASCII letters folded to lower case.
 * @param left First NUL-terminated string.
 * @param right Second NUL-terminated string.
 * @return Negative, zero or positive as @p left orders before, equal to or
 * after @p right.
 */
int astra_string_compare_ascii_nocase(const char *left, const char *right);

/** The byte length of the longest prefix of @p text (@p length bytes) that
 * is at most @p limit bytes and ends on a UTF-8 scalar boundary.
 * @param text Text to measure; NULL returns 0.
 * @param length Byte length of @p text.
 * @param limit Maximum byte length to keep.
 * @return The kept prefix length, at most @p limit.
 */
uint32_t astra_string_utf8_prefix(const char *text, uint32_t length,
                                  uint32_t limit);

#endif
