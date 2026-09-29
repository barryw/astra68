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
 * with zero capacity or NULL storage the builder refuses every append. */
void astra_string_init(AstraString *string, char *storage, uint32_t capacity);

/** Append NUL-terminated @p text. @return 1 if all of it fit, else 0 (the
 * longest prefix ending on a scalar boundary is kept and `truncated` set).
 * A truncated builder refuses every later append. */
int astra_string_append(AstraString *string, const char *text);
/** Append @p length bytes; see ::astra_string_append. */
int astra_string_append_bytes(AstraString *string, const char *bytes,
                              uint32_t length);
/** Append one byte; see ::astra_string_append. */
int astra_string_append_char(AstraString *string, char value);
/** Append @p value in decimal; see ::astra_string_append. */
int astra_string_append_u64(AstraString *string, uint64_t value);
/** Append signed @p value in decimal, with a leading '-' when negative. */
int astra_string_append_i64(AstraString *string, int64_t value);
/** Append @p value as exactly @p digits lowercase hex digits (1 through 16,
 * zero-padded, high digits dropped); see ::astra_string_append. */
int astra_string_append_hex(AstraString *string, uint64_t value,
                            uint32_t digits);

/** Copy @p text whole. @return 1 on success; 0 when it does not fit, with
 * @p out left empty. */
int astra_string_copy(char *out, uint32_t capacity, const char *text);

/** Append @p text to the NUL-terminated string in @p out, whole.
 * @return 1 on success; 0 when it does not fit, with @p out unchanged. */
int astra_string_concat(char *out, uint32_t capacity, const char *text);

/** Nonzero when @p text begins with @p prefix. */
int astra_string_has_prefix(const char *text, const char *prefix);
/** Nonzero when @p text ends with @p suffix. */
int astra_string_has_suffix(const char *text, const char *suffix);
/** strcmp order with ASCII letters folded to lower case. */
int astra_string_compare_ascii_nocase(const char *left, const char *right);

/** The byte length of the longest prefix of @p text (@p length bytes) that
 * is at most @p limit bytes and ends on a UTF-8 scalar boundary. */
uint32_t astra_string_utf8_prefix(const char *text, uint32_t length,
                                  uint32_t limit);

#endif
