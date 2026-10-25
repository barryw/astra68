#ifndef ASTRA_FONT_STORE_H
#define ASTRA_FONT_STORE_H

/* Immutable AFNT object names use all 256 SHA-256 bits in unpadded base32. */
#include <stdint.h>

#define ASTRA_FONT_OBJECT_TOKEN_LENGTH 52u
#define ASTRA_FONT_OBJECT_TOKEN_SIZE 53u

/* @bytes may be NULL only when @length is zero. Output includes NUL. */
void astra_font_object_token(const void *bytes, uint32_t length,
                             char token[ASTRA_FONT_OBJECT_TOKEN_SIZE]);
/* Validate exactly @length counted bytes; no terminator is required. */
int astra_font_object_token_valid(const char *token, uint32_t length);

#endif
