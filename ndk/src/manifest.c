#include <astra/manifest.h>

#include <stddef.h>
#include <stdint.h>

int astra_manifest_text_valid(const char *text, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)text;

    if (text == NULL)
        return 0;
    for (uint32_t at = 0u; at < length; ++at) {
        uint8_t first = bytes[at];
        uint32_t continuation;

        if (first == 0u) return 0;
        if (first < 0x80u) continue;
        if (first >= 0xc2u && first <= 0xdfu) continuation = 1u;
        else if (first >= 0xe0u && first <= 0xefu) continuation = 2u;
        else if (first >= 0xf0u && first <= 0xf4u) continuation = 3u;
        else return 0;
        if (continuation > length - at - 1u) return 0;
        if ((bytes[at + 1u] & 0xc0u) != 0x80u ||
            (continuation >= 2u && (bytes[at + 2u] & 0xc0u) != 0x80u) ||
            (continuation == 3u && (bytes[at + 3u] & 0xc0u) != 0x80u) ||
            (first == 0xe0u && bytes[at + 1u] < 0xa0u) ||
            (first == 0xedu && bytes[at + 1u] >= 0xa0u) ||
            (first == 0xf0u && bytes[at + 1u] < 0x90u) ||
            (first == 0xf4u && bytes[at + 1u] >= 0x90u)) return 0;
        at += continuation;
    }
    return 1;
}

uint32_t astra_manifest_words(char *line, char **out, uint32_t capacity)
{
    uint32_t count = 0u;

    while (*line != '\0') {
        char *write;
        int quoted = 0;
        int comment = 0;

        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '\0' || *line == '#') break;
        if (count == capacity) return capacity + 1u;
        if (*line == '"') {
            ++line;
            quoted = 1;
        }
        out[count++] = line;
        write = line;
        while (*line != '\0') {
            char value = *line++;
            if (quoted && value == '"') {
                quoted = 0;
                break;
            }
            if (!quoted && (value == ' ' || value == '\t' || value == '#')) {
                comment = value == '#';
                break;
            }
            if (value == '\\' && quoted) {
                value = *line++;
                if (value == '\0') return capacity + 1u;
                if (value == 'n') value = '\n';
                else if (value == 'r') value = '\r';
                else if (value == 't') value = '\t';
                else if (value != '\\' && value != '"')
                    return capacity + 1u;
            }
            *write++ = value;
        }
        if (quoted) return capacity + 1u;
        *write = '\0';
        if (comment) break;
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '#') break;
    }
    return count;
}
