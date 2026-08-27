#ifndef ASTRA_MANIFEST_H
#define ASTRA_MANIFEST_H

#include <stdint.h>

/* Splits one mutable manifest line using Astra's comments and quoted tokens. */
uint32_t astra_manifest_words(char *line, char **out, uint32_t capacity);

/* Rejects embedded NULs, malformed UTF-8, surrogates and overlong forms. */
int astra_manifest_text_valid(const char *text, uint32_t length);

#endif
