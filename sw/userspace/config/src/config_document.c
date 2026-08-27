#include <astra/config_document.h>
#include <astra/config_library.h>
#include <astra/manifest.h>

#include <stddef.h>
#include <string.h>

typedef struct ConfigLine {
    uint32_t start, end;
    const char *key, *value;
    uint32_t key_length, value_length;
    uint8_t quoted, setting;
} ConfigLine;

static int key_character(char value);

static int component_valid(const char *text)
{
    uint32_t at = 0u;

    if (text == NULL || text[0] == '\0' || text[0] == '.')
        return 0;
    while (text[at] != '\0') {
        if (!key_character(text[at]))
            return 0;
        ++at;
    }
    return 1;
}

static uint32_t build_root(const char *parent, const char *directory,
                           const char *owner, char *out, uint32_t capacity)
{
    uint32_t used = 0u;

    if (parent == NULL || parent[0] == '\0' ||
        (directory != NULL && !component_valid(directory)) ||
        (owner != NULL && !component_valid(owner)) ||
        out == NULL || capacity == 0u)
        return ASTRA_CONFIG_INVALID;
#define APPEND(TEXT) do { \
        const char *part = (TEXT); \
        while (*part != '\0') { \
            if (used + 1u >= capacity) { out[0] = '\0'; \
                return ASTRA_CONFIG_BUFFER_TOO_SMALL; } \
            out[used++] = *part++; \
        } \
    } while (0)
    out[0] = '\0';
    APPEND(parent);
    if (directory != NULL) {
        if (used != 0u && out[used - 1u] != '/') APPEND("/");
        APPEND(directory);
    }
    if (owner != NULL) {
        if (used != 0u && out[used - 1u] != '/') APPEND("/");
        APPEND(owner);
    }
    out[used] = '\0';
#undef APPEND
    return ASTRA_CONFIG_OK;
}

static const char *scope_directory(uint32_t owner_kind)
{
    switch (owner_kind) {
    case ASTRA_CONFIG_OWNER_SYSTEM: return "system";
    case ASTRA_CONFIG_OWNER_SERVICE: return "services";
    case ASTRA_CONFIG_OWNER_COMMAND: return "commands";
    case ASTRA_CONFIG_OWNER_APPLICATION: return "applications";
    default: return NULL;
    }
}

uint32_t astra_config_scope_root(const char *parent, uint32_t owner_kind,
                                 char *out, uint32_t capacity)
{
    const char *directory = scope_directory(owner_kind);

    return directory != NULL ? build_root(parent, directory, NULL, out,
                                           capacity) : ASTRA_CONFIG_INVALID;
}

uint32_t astra_config_owner_root(const char *parent, const char *owner,
                                 char *out, uint32_t capacity)
{
    return build_root(parent, NULL, owner, out, capacity);
}

uint32_t astra_config_capability_root(const char *parent, uint32_t owner_kind,
                                      const char *owner, char *out,
                                      uint32_t capacity)
{
    const char *directory = scope_directory(owner_kind);

    return directory != NULL ? build_root(parent, directory, owner, out,
                                           capacity) : ASTRA_CONFIG_INVALID;
}

static int space(char value)
{
    return value == ' ' || value == '\t';
}

static int key_character(char value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' ||
           value == '_' || value == '.';
}

static int key_valid(const char *key)
{
    uint32_t at = 0u;

    if (key == NULL || !key_character(key[0]))
        return 0;
    while (key[at] != '\0')
        if (!key_character(key[at++]))
            return 0;
    return 1;
}

static uint32_t line_parse(const char *text, uint32_t length,
                           uint32_t *cursor, ConfigLine *line)
{
    uint32_t at = *cursor;
    uint32_t limit = at;
    int separated = 0, equal = 0;

    (void)memset(line, 0, sizeof(*line));
    line->start = at;
    while (limit < length && text[limit] != '\n' && text[limit] != '\r')
        ++limit;
    line->end = limit;
    if (limit < length && text[limit] == '\r') {
        ++line->end;
        if (line->end < length && text[line->end] == '\n')
            ++line->end;
    } else if (limit < length) {
        ++line->end;
    }
    *cursor = line->end;
    while (at < limit && space(text[at]))
        ++at;
    if (at == limit || text[at] == '#')
        return ASTRA_CONFIG_OK;
    line->key = text + at;
    while (at < limit && key_character(text[at]))
        ++at;
    line->key_length = (uint32_t)((text + at) - line->key);
    if (line->key_length == 0u)
        return ASTRA_CONFIG_MALFORMED;
    if (at < limit && !space(text[at]) && text[at] != '=')
        return ASTRA_CONFIG_MALFORMED;
    while (at < limit && space(text[at])) {
        separated = 1;
        ++at;
    }
    if (at < limit && text[at] == '=') {
        equal = 1;
        ++at;
        while (at < limit && space(text[at]))
            ++at;
    }
    if (!separated && !equal)
        return ASTRA_CONFIG_MALFORMED;
    line->setting = 1u;
    if (at == limit || text[at] == '#') {
        if (!equal)
            return ASTRA_CONFIG_MALFORMED;
        line->value = text + at;
        return ASTRA_CONFIG_OK;
    }
    if (text[at] == '"') {
        uint32_t value_start = ++at;

        line->quoted = 1u;
        while (at < limit && text[at] != '"') {
            if (text[at] == '\\') {
                ++at;
                if (at == limit || (text[at] != '\\' && text[at] != '"' &&
                                    text[at] != 'n' && text[at] != 'r' &&
                                    text[at] != 't'))
                    return ASTRA_CONFIG_MALFORMED;
            }
            ++at;
        }
        if (at == limit)
            return ASTRA_CONFIG_MALFORMED;
        line->value = text + value_start;
        line->value_length = at - value_start;
        ++at;
        while (at < limit && space(text[at]))
            ++at;
        if (at < limit && text[at] != '#')
            return ASTRA_CONFIG_MALFORMED;
        return ASTRA_CONFIG_OK;
    }
    line->value = text + at;
    while (at < limit) {
        if (text[at] == '#' &&
            (at == (uint32_t)(line->value - text) || space(text[at - 1u])))
            break;
        ++at;
    }
    while (at > (uint32_t)(line->value - text) && space(text[at - 1u]))
        --at;
    line->value_length = (uint32_t)((text + at) - line->value);
    return ASTRA_CONFIG_OK;
}

static int key_equal(const ConfigLine *line, const char *key)
{
    uint32_t length = (uint32_t)strlen(key);

    return line->key_length == length &&
           memcmp(line->key, key, length) == 0;
}

static uint32_t decoded_length(const ConfigLine *line)
{
    uint32_t length = 0u;

    for (uint32_t at = 0u; at < line->value_length; ++at) {
        if (line->quoted && line->value[at] == '\\')
            ++at;
        ++length;
    }
    return length;
}

static void decode(const ConfigLine *line, char *out)
{
    uint32_t used = 0u;

    for (uint32_t at = 0u; at < line->value_length; ++at) {
        char value = line->value[at];

        if (line->quoted && value == '\\') {
            value = line->value[++at];
            if (value == 'n') value = '\n';
            else if (value == 'r') value = '\r';
            else if (value == 't') value = '\t';
        }
        out[used++] = value;
    }
    out[used] = '\0';
}

static int decimal(const ConfigLine *line, uint32_t *value)
{
    uint32_t result = 0u;

    if (line->quoted || line->value_length == 0u)
        return 0;
    for (uint32_t at = 0u; at < line->value_length; ++at) {
        uint32_t digit;

        if (line->value[at] < '0' || line->value[at] > '9')
            return 0;
        digit = (uint32_t)(line->value[at] - '0');
        if (result > (UINT32_MAX - digit) / 10u)
            return 0;
        result = result * 10u + digit;
    }
    if (result == 0u)
        return 0;
    *value = result;
    return 1;
}

uint32_t astra_config_document_validate(const char *text, uint32_t length,
                                        uint32_t schema_version,
                                        AstraConfigDocumentError *error)
{
    uint32_t cursor = 0u, line_number = 1u, header = 0u;

    if (error != NULL) {
        error->line = 0u;
        error->version = 0u;
    }
    if (text == NULL || schema_version == 0u ||
        !astra_manifest_text_valid(text, length))
        return ASTRA_CONFIG_INVALID;
    while (cursor < length) {
        ConfigLine line;
        uint32_t status = line_parse(text, length, &cursor, &line);

        if (status != ASTRA_CONFIG_OK) {
            if (error != NULL) error->line = line_number;
            return status;
        }
        if (line.setting != 0u && header < 2u) {
            static const char *const names[2] = {"astra-config", "schema"};
            uint32_t found = 0u;
            uint32_t wanted = header == 0u ? 1u : schema_version;

            if (!key_equal(&line, names[header]) || !decimal(&line, &found)) {
                if (error != NULL) error->line = line_number;
                return ASTRA_CONFIG_MALFORMED;
            }
            if (found != wanted) {
                if (error != NULL) {
                    error->line = line_number;
                    error->version = found;
                }
                return ASTRA_CONFIG_UNSUPPORTED_VERSION;
            }
            ++header;
        }
        ++line_number;
    }
    if (header != 2u) {
        if (error != NULL) error->line = line_number;
        return ASTRA_CONFIG_MALFORMED;
    }
    return ASTRA_CONFIG_OK;
}

static uint32_t find(const char *text, uint32_t length, const char *key,
                     uint32_t wanted, ConfigLine *found, uint32_t *count)
{
    uint32_t cursor = 0u, effective = 0u, matches = 0u;

    if (text == NULL || !key_valid(key))
        return ASTRA_CONFIG_INVALID;
    while (cursor < length) {
        ConfigLine line;
        uint32_t status = line_parse(text, length, &cursor, &line);

        if (status != ASTRA_CONFIG_OK)
            return status;
        if (line.setting == 0u)
            continue;
        if (effective++ < 2u)
            continue;
        if (!key_equal(&line, key))
            continue;
        if (found != NULL && matches == wanted)
            *found = line;
        ++matches;
    }
    if (count != NULL)
        *count = matches;
    return found == NULL || wanted < matches ? ASTRA_CONFIG_OK :
                                              ASTRA_CONFIG_NOT_FOUND;
}

uint32_t astra_config_document_count(const char *text, uint32_t length,
                                     const char *key, uint32_t *count)
{
    if (count == NULL)
        return ASTRA_CONFIG_INVALID;
    *count = 0u;
    return find(text, length, key, 0u, NULL, count);
}

uint32_t astra_config_document_get(const char *text, uint32_t length,
                                   const char *key, uint32_t index,
                                   char *out, uint32_t capacity,
                                   uint32_t *value_length)
{
    ConfigLine line;
    uint32_t decoded, status;

    if (value_length == NULL)
        return ASTRA_CONFIG_INVALID;
    *value_length = 0u;
    status = find(text, length, key, index, &line, NULL);
    if (status != ASTRA_CONFIG_OK)
        return status;
    decoded = decoded_length(&line);
    *value_length = decoded;
    if (out == NULL || capacity <= decoded)
        return ASTRA_CONFIG_BUFFER_TOO_SMALL;
    decode(&line, out);
    return ASTRA_CONFIG_OK;
}

static uint32_t scalar(const char *text, uint32_t length, const char *key,
                       uint32_t index, char *out, uint32_t capacity,
                       uint32_t *value_length)
{
    uint32_t status = astra_config_document_get(
        text, length, key, index, out, capacity, value_length);

    return status == ASTRA_CONFIG_BUFFER_TOO_SMALL ? ASTRA_CONFIG_MALFORMED :
                                                    status;
}

uint32_t astra_config_document_get_u64(const char *text, uint32_t length,
                                       const char *key, uint32_t index,
                                       uint64_t *value)
{
    char number[21];
    uint32_t count;
    uint64_t result = 0u;

    if (value == NULL)
        return ASTRA_CONFIG_INVALID;
    {
        uint32_t status = scalar(text, length, key, index, number,
                                 sizeof(number), &count);
        if (status != ASTRA_CONFIG_OK)
            return status;
    }
    if (count == 0u)
        return ASTRA_CONFIG_MALFORMED;
    for (uint32_t at = 0u; at < count; ++at) {
        uint64_t digit;

        if (number[at] < '0' || number[at] > '9')
            return ASTRA_CONFIG_MALFORMED;
        digit = (uint64_t)(number[at] - '0');
        if (result > (UINT64_MAX - digit) / UINT64_C(10))
            return ASTRA_CONFIG_MALFORMED;
        result = result * UINT64_C(10) + digit;
    }
    *value = result;
    return ASTRA_CONFIG_OK;
}

uint32_t astra_config_document_get_i64(const char *text, uint32_t length,
                                       const char *key, uint32_t index,
                                       int64_t *value)
{
    char number[21];
    uint32_t count, at = 0u;
    uint64_t magnitude = 0u, limit;
    int negative = 0;

    if (value == NULL)
        return ASTRA_CONFIG_INVALID;
    {
        uint32_t status = scalar(text, length, key, index, number,
                                 sizeof(number), &count);
        if (status != ASTRA_CONFIG_OK)
            return status;
    }
    if (count != 0u && (number[0] == '-' || number[0] == '+')) {
        negative = number[0] == '-';
        ++at;
    }
    if (at == count)
        return ASTRA_CONFIG_MALFORMED;
    limit = negative ? (uint64_t)INT64_MAX + UINT64_C(1) :
                       (uint64_t)INT64_MAX;
    while (at < count) {
        uint64_t digit;

        if (number[at] < '0' || number[at] > '9')
            return ASTRA_CONFIG_MALFORMED;
        digit = (uint64_t)(number[at++] - '0');
        if (magnitude > (limit - digit) / UINT64_C(10))
            return ASTRA_CONFIG_MALFORMED;
        magnitude = magnitude * UINT64_C(10) + digit;
    }
    if (negative && magnitude == (uint64_t)INT64_MAX + UINT64_C(1))
        *value = INT64_MIN;
    else
        *value = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return ASTRA_CONFIG_OK;
}

static char lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) :
                                         value;
}

static int same_word(const char *left, uint32_t length, const char *right)
{
    uint32_t at = 0u;

    while (at < length && right[at] != '\0' &&
           lower(left[at]) == right[at])
        ++at;
    return at == length && right[at] == '\0';
}

uint32_t astra_config_document_get_bool(const char *text, uint32_t length,
                                        const char *key, uint32_t index,
                                        int *value)
{
    char word[6];
    uint32_t count;

    if (value == NULL)
        return ASTRA_CONFIG_INVALID;
    {
        uint32_t status = scalar(text, length, key, index, word,
                                 sizeof(word), &count);
        if (status != ASTRA_CONFIG_OK)
            return status;
    }
    if (same_word(word, count, "true") || same_word(word, count, "yes") ||
        same_word(word, count, "on") || same_word(word, count, "1")) {
        *value = 1;
        return ASTRA_CONFIG_OK;
    }
    if (same_word(word, count, "false") || same_word(word, count, "no") ||
        same_word(word, count, "off") || same_word(word, count, "0")) {
        *value = 0;
        return ASTRA_CONFIG_OK;
    }
    return ASTRA_CONFIG_MALFORMED;
}

static uint32_t encoded_length(const char *value)
{
    uint32_t length = 0u;

    while (*value != '\0') {
        if (*value == '\\' || *value == '"' || *value == '\n' ||
            *value == '\r' || *value == '\t')
            ++length;
        ++length;
        ++value;
    }
    return length;
}

static char *encode(char *out, const char *value)
{
    while (*value != '\0') {
        char current = *value++;

        if (current == '\\' || current == '"' || current == '\n' ||
            current == '\r' || current == '\t') {
            *out++ = '\\';
            if (current == '\n') current = 'n';
            else if (current == '\r') current = 'r';
            else if (current == '\t') current = 't';
        }
        *out++ = current;
    }
    return out;
}

uint32_t astra_config_document_replace(
    const char *text, uint32_t length, const char *key, uint32_t index,
    const char *value, char *out, uint32_t capacity, uint32_t *required)
{
    ConfigLine line;
    uint32_t count = 0u, key_length, value_length, prefix, suffix, newline;
    uint32_t total, status;
    char *at;

    if (required == NULL || value == NULL || !key_valid(key))
        return ASTRA_CONFIG_INVALID;
    *required = 0u;
    status = find(text, length, key, index, &line, &count);
    if (status == ASTRA_CONFIG_NOT_FOUND && index == count) {
        line.start = length;
        line.end = length;
    } else if (status != ASTRA_CONFIG_OK) {
        return status;
    }
    prefix = line.start;
    suffix = length - line.end;
    newline = line.start == length && length != 0u &&
              text[length - 1u] != '\n' && text[length - 1u] != '\r';
    key_length = (uint32_t)strlen(key);
    value_length = encoded_length(value);
    if (key_length > UINT32_MAX - value_length)
        return ASTRA_CONFIG_INVALID;
    total = key_length + value_length;
    if (suffix > UINT32_MAX - total)
        return ASTRA_CONFIG_INVALID;
    total += suffix;
    if (prefix > UINT32_MAX - total)
        return ASTRA_CONFIG_INVALID;
    total += prefix;
    if (total > UINT32_MAX - 7u - newline)
        return ASTRA_CONFIG_INVALID;
    total += 6u + newline;
    *required = total + 1u;
    if (out == NULL || capacity <= total)
        return ASTRA_CONFIG_BUFFER_TOO_SMALL;
    (void)memcpy(out, text, prefix);
    at = out + prefix;
    if (newline != 0u) *at++ = '\n';
    (void)memcpy(at, key, key_length);
    at += key_length;
    *at++ = ' ';
    *at++ = '=';
    *at++ = ' ';
    *at++ = '"';
    at = encode(at, value);
    *at++ = '"';
    *at++ = '\n';
    (void)memcpy(at, text + line.end, suffix);
    at += suffix;
    *at = '\0';
    return ASTRA_CONFIG_OK;
}

uint32_t astra_config_document_remove(
    const char *text, uint32_t length, const char *key, uint32_t index,
    char *out, uint32_t capacity, uint32_t *required)
{
    ConfigLine line;
    uint32_t total, status;

    if (required == NULL)
        return ASTRA_CONFIG_INVALID;
    *required = 0u;
    status = find(text, length, key, index, &line, NULL);
    if (status != ASTRA_CONFIG_OK)
        return status;
    total = length - (line.end - line.start);
    *required = total + 1u;
    if (out == NULL || capacity <= total)
        return ASTRA_CONFIG_BUFFER_TOO_SMALL;
    (void)memcpy(out, text, line.start);
    (void)memcpy(out + line.start, text + line.end, length - line.end);
    out[total] = '\0';
    return ASTRA_CONFIG_OK;
}
