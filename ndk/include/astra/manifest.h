/** @file manifest.h @brief Forgiving UTF-8 bundle-manifest parsing. */
#ifndef ASTRA_MANIFEST_H
#define ASTRA_MANIFEST_H

#include <stdint.h>

/**
 * Split one mutable manifest line using Astra comments and quoted tokens.
 * @param line Mutable NUL-terminated line; separators are replaced by NULs.
 * @param out Receives pointers into `line` for at most `capacity` words.
 * @param capacity Number of entries available in `out`.
 * @return Number of words written to `out`.
 */
uint32_t astra_manifest_words(char *line, char **out, uint32_t capacity);

/**
 * Validate one length-delimited UTF-8 manifest value.
 * @param text Bytes to validate; may be NULL only when `length` is zero.
 * @param length Number of bytes in `text`, excluding any terminator.
 * @return Nonzero when the bytes are valid UTF-8 without embedded NULs.
 */
int astra_manifest_text_valid(const char *text, uint32_t length);

#endif
