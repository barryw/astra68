#ifndef ASTRA_MANIFEST_H
#define ASTRA_MANIFEST_H

#include <stdint.h>

/* Splits one mutable manifest line using Astra's comments and quoted tokens. */
uint32_t astra_manifest_words(char *line, char **out, uint32_t capacity);

#endif
