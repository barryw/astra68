/** @file string.c
 *  @brief Bounded strings (astra/string.h).
 */

#include <astra/bytes.h>
#include <astra/string.h>

#include <stddef.h>
#include <stdint.h>

static int continuation(char value)
{
    return ((uint8_t)value & 0xc0u) == 0x80u;
}

uint32_t astra_string_utf8_prefix(const char *text, uint32_t length,
                                  uint32_t limit)
{
    if (text == NULL)
        return 0u;
    if (length <= limit)
        return length;
    /* The byte at `limit` starts the first dropped scalar unless it is a
       continuation, in which case the cut backs up to that scalar's lead. */
    while (limit > 0u && continuation(text[limit]))
        --limit;
    return limit;
}

void astra_string_init(AstraString *string, char *storage, uint32_t capacity)
{
    if (string == NULL)
        return;
    string->text = storage;
    string->capacity = storage != NULL ? capacity : 0u;
    string->length = 0u;
    string->truncated = string->capacity == 0u;
    if (string->capacity != 0u)
        storage[0] = '\0';
}

int astra_string_append_bytes(AstraString *string, const char *bytes,
                              uint32_t length)
{
    uint32_t room;
    uint32_t kept;

    if (string == NULL || (bytes == NULL && length != 0u))
        return 0;
    if (string->truncated != 0u)
        return 0;
    room = string->capacity - 1u - string->length;
    kept = length <= room ? length :
                            astra_string_utf8_prefix(bytes, length, room);
    if (kept != 0u)
        memcpy(string->text + string->length, bytes, kept);
    string->length += kept;
    string->text[string->length] = '\0';
    if (kept != length) {
        string->truncated = 1u;
        return 0;
    }
    return 1;
}

int astra_string_append(AstraString *string, const char *text)
{
    if (text == NULL)
        return 0;
    return astra_string_append_bytes(string, text, (uint32_t)strlen(text));
}

int astra_string_append_char(AstraString *string, char value)
{
    return astra_string_append_bytes(string, &value, 1u);
}

int astra_string_append_u64(AstraString *string, uint64_t value)
{
    char digits[20];
    char ordered[20];
    uint32_t count = 0u;

    do {
        digits[count++] = (char)('0' + (uint32_t)(value % 10u));
        value /= 10u;
    } while (value != 0u);
    for (uint32_t at = 0u; at < count; ++at)
        ordered[at] = digits[count - 1u - at];
    return astra_string_append_bytes(string, ordered, count);
}

int astra_string_append_i64(AstraString *string, int64_t value)
{
    uint64_t magnitude = value < 0 ? 0u - (uint64_t)value : (uint64_t)value;

    if (value < 0 && !astra_string_append_char(string, '-'))
        return 0;
    return astra_string_append_u64(string, magnitude);
}

int astra_string_append_hex(AstraString *string, uint64_t value,
                            uint32_t digits)
{
    static const char hex[] = "0123456789abcdef";
    char out[16];

    if (digits == 0u || digits > 16u)
        return 0;
    for (uint32_t at = 0u; at < digits; ++at)
        out[at] = hex[(value >> (4u * (digits - 1u - at))) & 0xfu];
    return astra_string_append_bytes(string, out, digits);
}

int astra_string_copy(char *out, uint32_t capacity, const char *text)
{
    uint32_t length;

    if (out == NULL || capacity == 0u)
        return 0;
    out[0] = '\0';
    if (text == NULL)
        return 0;
    length = (uint32_t)strlen(text);
    if (length >= capacity)
        return 0;
    memcpy(out, text, length + 1u);
    return 1;
}

int astra_string_concat(char *out, uint32_t capacity, const char *text)
{
    uint32_t start = 0u;
    uint32_t length;

    if (out == NULL || text == NULL)
        return 0;
    while (start < capacity && out[start] != '\0')
        ++start;
    if (start == capacity)
        return 0;
    length = (uint32_t)strlen(text);
    if (length >= capacity - start)
        return 0;
    memcpy(out + start, text, length + 1u);
    return 1;
}

int astra_string_has_prefix(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL)
        return 0;
    while (*prefix != '\0')
        if (*text++ != *prefix++)
            return 0;
    return 1;
}

int astra_string_has_suffix(const char *text, const char *suffix)
{
    size_t length;
    size_t ending;

    if (text == NULL || suffix == NULL)
        return 0;
    length = strlen(text);
    ending = strlen(suffix);
    return ending <= length && strcmp(text + length - ending, suffix) == 0;
}

static unsigned char folded(char value)
{
    return (unsigned char)(value >= 'A' && value <= 'Z' ?
                           value - 'A' + 'a' : value);
}

int astra_string_compare_ascii_nocase(const char *left, const char *right)
{
    for (;; ++left, ++right) {
        unsigned char a = folded(*left);
        unsigned char b = folded(*right);

        if (a != b)
            return a < b ? -1 : 1;
        if (a == '\0')
            return 0;
    }
}
