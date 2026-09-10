#include <astra/manifest.h>
#include <astra/utf8.h>

#include <stddef.h>
#include <stdint.h>

int astra_manifest_text_valid(const char *text, uint32_t length)
{
    return text != NULL && astra_utf8_validate(text, length, 0u);
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
