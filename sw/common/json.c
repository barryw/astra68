#include <astra/json.h>
#include <astra/utf8.h>

#include <stddef.h>
#include <string.h>

static void space(AstraJsonValue value, uint32_t *at)
{
    while (*at < value.length && (value.bytes[*at] == ' ' ||
        value.bytes[*at] == '\t' || value.bytes[*at] == '\r' ||
        value.bytes[*at] == '\n')) ++*at;
}

static int hex4(AstraJsonValue value, uint32_t *at, uint32_t *scalar)
{
    uint32_t result = 0u;
    if (*at > value.length || value.length - *at < 4u) return 0;
    for (uint32_t digit = 0u; digit < 4u; ++digit) {
        uint8_t byte = value.bytes[(*at)++];
        uint32_t nibble;
        if (byte >= '0' && byte <= '9') nibble = byte - '0';
        else if (byte >= 'a' && byte <= 'f') nibble = byte - 'a' + 10u;
        else if (byte >= 'A' && byte <= 'F') nibble = byte - 'A' + 10u;
        else return 0;
        result = result * 16u + nibble;
    }
    *scalar = result;
    return 1;
}

static int string_scalar(AstraJsonValue value, uint32_t *at, uint32_t *scalar)
{
    if (*at >= value.length) return 0;
    uint8_t byte = value.bytes[*at];
    if (byte == '\\') {
        if (++*at >= value.length) return 0;
        byte = value.bytes[(*at)++];
        switch (byte) {
        case '"': case '\\': case '/': *scalar = byte; return 1;
        case 'b': *scalar = '\b'; return 1;
        case 'f': *scalar = '\f'; return 1;
        case 'n': *scalar = '\n'; return 1;
        case 'r': *scalar = '\r'; return 1;
        case 't': *scalar = '\t'; return 1;
        case 'u': break;
        default: return 0;
        }
        if (!hex4(value, at, scalar)) return 0;
        if (*scalar >= 0xd800u && *scalar <= 0xdbffu) {
            uint32_t low;
            if (value.length - *at < 6u || value.bytes[*at] != '\\' ||
                value.bytes[*at + 1u] != 'u') return 0;
            *at += 2u;
            if (!hex4(value, at, &low) || low < 0xdc00u || low > 0xdfffu)
                return 0;
            *scalar = 0x10000u + ((*scalar - 0xd800u) << 10u) + low - 0xdc00u;
        }
        return astra_unicode_scalar_valid(*scalar);
    }
    if (byte < 0x20u || byte == '"') return 0;
    uint32_t start = *at, consumed;
    if (!astra_utf8_scalar_advance(value.bytes, value.length, at)) return 0;
    *scalar = astra_utf8_decode(value.bytes + start, *at - start, &consumed);
    return 1;
}

static int string_token(AstraJsonValue value, uint32_t *at)
{
    if (*at >= value.length || value.bytes[(*at)++] != '"') return 0;
    while (*at < value.length) {
        uint32_t scalar;
        if (value.bytes[*at] == '"') { ++*at; return 1; }
        if (!string_scalar(value, at, &scalar)) return 0;
    }
    return 0;
}

static int digit(AstraJsonValue value, uint32_t at)
{
    return at < value.length && value.bytes[at] >= '0' && value.bytes[at] <= '9';
}

static int number_token(AstraJsonValue value, uint32_t *at)
{
    if (*at < value.length && value.bytes[*at] == '-') ++*at;
    if (!digit(value, *at)) return 0;
    if (value.bytes[*at] == '0') ++*at;
    else while (digit(value, *at)) ++*at;
    if (*at < value.length && value.bytes[*at] == '.') {
        if (!digit(value, ++*at)) return 0;
        while (digit(value, *at)) ++*at;
    }
    if (*at < value.length && (value.bytes[*at] == 'e' || value.bytes[*at] == 'E')) {
        ++*at;
        if (*at < value.length && (value.bytes[*at] == '+' || value.bytes[*at] == '-'))
            ++*at;
        if (!digit(value, *at)) return 0;
        while (digit(value, *at)) ++*at;
    }
    return 1;
}

static int parse(AstraJsonValue value, uint32_t *at, uint32_t depth)
{
    space(value, at);
    if (*at >= value.length) return 0;
    uint8_t byte = value.bytes[*at];
    if (byte == '"') return string_token(value, at);
    if (byte == '[' || byte == '{') {
        uint8_t close = byte == '[' ? ']' : '}';
        if (depth == ASTRA_JSON_DEPTH_MAX) return 0;
        ++*at; space(value, at);
        if (*at < value.length && value.bytes[*at] == close) { ++*at; return 1; }
        for (;;) {
            if (byte == '{') {
                if (!string_token(value, at)) return 0;
                space(value, at);
                if (*at >= value.length || value.bytes[(*at)++] != ':') return 0;
            }
            if (!parse(value, at, depth + 1u)) return 0;
            space(value, at);
            if (*at >= value.length) return 0;
            if (value.bytes[*at] == close) { ++*at; return 1; }
            if (value.bytes[(*at)++] != ',') return 0;
            space(value, at);
        }
    }
    const char *literal = byte == 't' ? "true" : byte == 'f' ? "false" :
                          byte == 'n' ? "null" : NULL;
    if (literal != NULL) {
        uint32_t bytes = (uint32_t)strlen(literal);
        if (bytes > value.length - *at || memcmp(value.bytes + *at, literal, bytes) != 0)
            return 0;
        *at += bytes; return 1;
    }
    return number_token(value, at);
}

int astra_json_validate(AstraJsonValue value)
{
    uint32_t at = 0u;
    if (value.bytes == NULL || !parse(value, &at, 0u)) return 0;
    space(value, &at);
    return at == value.length;
}

static int key_equal(AstraJsonValue key, const char *expected)
{
    uint32_t at = 1u, used = 0u;
    size_t length = strlen(expected);
    while (at < key.length - 1u) {
        uint32_t scalar;
        char encoded[4];
        if (!string_scalar(key, &at, &scalar)) return 0;
        uint32_t bytes = astra_utf8_encode(scalar, encoded);
        if (bytes > length - used || memcmp(encoded, expected + used, bytes) != 0)
            return 0;
        used += bytes;
    }
    return used == length;
}

int astra_json_member(AstraJsonValue object, const char *key, AstraJsonValue *value)
{
    uint32_t at = 0u;
    int found = 0;
    AstraJsonValue result = {NULL, 0u};
    if (value == NULL) return ASTRA_JSON_INVALID;
    *value = result;
    if (key == NULL || !astra_json_validate(object)) return ASTRA_JSON_INVALID;
    space(object, &at);
    if (object.bytes[at++] != '{') return ASTRA_JSON_INVALID;
    for (;;) {
        space(object, &at);
        if (object.bytes[at] == '}') break;
        uint32_t start = at;
        if (!string_token(object, &at)) return ASTRA_JSON_INVALID;
        int matches = key_equal((AstraJsonValue){object.bytes + start, at - start}, key);
        space(object, &at); ++at; space(object, &at);
        start = at;
        if (!parse(object, &at, 1u)) return ASTRA_JSON_INVALID;
        if (matches) {
            if (found) return ASTRA_JSON_INVALID;
            result = (AstraJsonValue){object.bytes + start, at - start}; found = 1;
        }
        space(object, &at);
        if (object.bytes[at] == '}') break;
        ++at;
    }
    if (found) *value = result;
    return found;
}

static int array_scan(AstraJsonValue array, uint32_t index,
                       AstraJsonValue *value, uint32_t *count)
{
    uint32_t at = 0u, size = 0u;
    AstraJsonValue result = {NULL, 0u};
    if (value != NULL) *value = result;
    if (!astra_json_validate(array)) return ASTRA_JSON_INVALID;
    space(array, &at);
    if (array.bytes[at++] != '[') return ASTRA_JSON_INVALID;
    for (;;) {
        space(array, &at);
        if (array.bytes[at] == ']') break;
        uint32_t start = at;
        if (!parse(array, &at, 1u)) return ASTRA_JSON_INVALID;
        if (index == size) result = (AstraJsonValue){array.bytes + start, at - start};
        ++size;
        space(array, &at);
        if (array.bytes[at] == ']') break;
        ++at;
    }
    if (count != NULL) *count = size;
    if (value != NULL) *value = result;
    return value == NULL || index < size ? ASTRA_JSON_FOUND : ASTRA_JSON_MISSING;
}

int astra_json_element(AstraJsonValue array, uint32_t index, AstraJsonValue *value)
{
    return value == NULL ? ASTRA_JSON_INVALID : array_scan(array, index, value, NULL);
}

int astra_json_array_count(AstraJsonValue array, uint32_t *count)
{
    return count != NULL && array_scan(array, 0u, NULL, count) == ASTRA_JSON_FOUND;
}

int astra_json_string(AstraJsonValue value, char *output, uint32_t capacity,
                       uint32_t *bytes)
{
    uint32_t start = 0u, at, used = 0u;
    if (bytes == NULL || (output == NULL && capacity != 0u) ||
        !astra_json_validate(value)) return 0;
    space(value, &start);
    if (value.bytes[start++] != '"') return 0;
    at = start;
    while (value.bytes[at] != '"') {
        uint32_t scalar;
        char encoded[4];
        if (!string_scalar(value, &at, &scalar)) return 0;
        used += astra_utf8_encode(scalar, encoded);
    }
    *bytes = used;
    if (output == NULL) return 1;
    if (capacity <= used) return 0;
    at = start; used = 0u;
    while (value.bytes[at] != '"') {
        uint32_t scalar;
        if (!string_scalar(value, &at, &scalar)) return 0;
        used += astra_utf8_encode(scalar, output + used);
    }
    output[used] = '\0';
    return 1;
}

int astra_json_u32(AstraJsonValue value, uint32_t *number)
{
    uint32_t at = 0u, result = 0u;
    if (number == NULL || !astra_json_validate(value)) return 0;
    space(value, &at);
    if (!digit(value, at)) return 0;
    do {
        uint32_t next = value.bytes[at++] - '0';
        if (result > (UINT32_MAX - next) / 10u) return 0;
        result = result * 10u + next;
    } while (digit(value, at));
    space(value, &at);
    if (at != value.length) return 0;
    *number = result;
    return 1;
}
