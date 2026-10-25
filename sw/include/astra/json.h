#ifndef ASTRA_JSON_H
#define ASTRA_JSON_H

#include <stdint.h>

/* Borrowed, counted JSON value. No allocation or application-owned parser. */
typedef struct AstraJsonValue {
    const uint8_t *bytes;
    uint32_t length;
} AstraJsonValue;

#define ASTRA_JSON_DEPTH_MAX 32u
enum { ASTRA_JSON_INVALID = -1, ASTRA_JSON_MISSING = 0, ASTRA_JSON_FOUND = 1 };

int astra_json_validate(AstraJsonValue value);
/* Lookups reject malformed input and duplicate matching object keys. */
int astra_json_member(AstraJsonValue object, const char *key, AstraJsonValue *value);
int astra_json_element(AstraJsonValue array, uint32_t index, AstraJsonValue *value);
int astra_json_array_count(AstraJsonValue array, uint32_t *count);
/* NULL/zero measures decoded bytes; successful copies add a final NUL.
 * Embedded NUL remains counted data. Failure never partially writes output. */
int astra_json_string(AstraJsonValue value, char *output, uint32_t capacity,
                       uint32_t *bytes);
int astra_json_u32(AstraJsonValue value, uint32_t *number);

#endif
