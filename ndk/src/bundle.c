#include <astra/bundle.h>
#include <astra/endian.h>
#include <astra/manifest.h>

#include <stddef.h>
#include <string.h>

#define WORD_MAX 5u

static int copy(char *out, uint32_t capacity, const char *text)
{
    uint32_t at = 0u;
    while (text[at] != '\0') {
        if (at + 1u >= capacity) return 0;
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
    return at != 0u;
}

static int number(const char *text, uint16_t *value)
{
    uint32_t result = 0u;
    uint32_t digits = 0u;
    while (*text >= '0' && *text <= '9') {
        result = result * 10u + (uint32_t)(*text++ - '0');
        if (result > UINT16_MAX) return 0;
        ++digits;
    }
    if (digits == 0u || *text != '\0') return 0;
    *value = (uint16_t)result;
    return 1;
}

static int version(const char *text, AstraBundleVersion *out)
{
    uint16_t *part[3] = {&out->major, &out->minor, &out->patch};
    for (uint32_t at = 0u; at < 3u; ++at) {
        char digits[6];
        uint32_t count = 0u;
        while (*text >= '0' && *text <= '9' && count + 1u < sizeof(digits))
            digits[count++] = *text++;
        digits[count] = '\0';
        if (!number(digits, part[at]) ||
            (at != 2u && *text++ != '.') ||
            (at == 2u && *text != '\0')) return 0;
    }
    return 1;
}

static int identifier(const char *text)
{
    uint32_t length = 0u;
    int segment = 0;
    while (text[length] != '\0') {
        char value = text[length++];
        if (value == '.') {
            if (!segment) return 0;
            segment = 0;
        } else if ((value >= 'a' && value <= 'z') ||
                   (value >= '0' && value <= '9') || value == '-') {
            segment = 1;
        } else return 0;
    }
    return segment && length < ASTRA_BUNDLE_ID_MAX;
}

static int library_name(const char *text)
{
    uint32_t length = 0u;

    while (text[length] != '\0') {
        char value = text[length++];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-') ||
            length >= ASTRA_BUNDLE_LIBRARY_NAME_MAX) return 0;
    }
    return length != 0u;
}

static int relative_path(const char *path)
{
    uint32_t at = 0u;
    if (path[0] == '\0' || path[0] == '/' || path[0] == '.') return 0;
    while (path[at] != '\0') {
        if (path[at] == ':' || path[at] == '\\') return 0;
        if ((at == 0u || path[at - 1u] == '/') && path[at] == '.' &&
            (path[at + 1u] == '/' || path[at + 1u] == '\0' ||
             (path[at + 1u] == '.' &&
              (path[at + 2u] == '/' || path[at + 2u] == '\0')))) return 0;
        ++at;
    }
    return at < ASTRA_BUNDLE_PATH_MAX;
}

static int library(char **word, AstraBundleLibrary *out)
{
    return library_name(word[1]) &&
           copy(out->name, sizeof(out->name), word[1]) &&
           number(word[2], &out->abi) && out->abi != 0u &&
           version(word[3], &out->version);
}

static int parse_line(char *line, AstraBundleManifest *manifest)
{
    char *word[WORD_MAX];
    uint32_t count = astra_manifest_words(line, word, WORD_MAX);
    if (count == 0u) return 2;
    if (count > WORD_MAX) return 0;
    if (strcmp(word[0], "astra-bundle") == 0)
        return count == 2u && manifest->format_version == 0u &&
               number(word[1], &manifest->format_version) &&
               manifest->format_version == ASTRA_BUNDLE_MANIFEST_VERSION;
    if (strcmp(word[0], "kind") == 0) {
        if (count != 2u || manifest->kind != 0u) return 0;
        if (strcmp(word[1], "application") == 0)
            manifest->kind = ASTRA_BUNDLE_APPLICATION;
        else if (strcmp(word[1], "kit") == 0)
            manifest->kind = ASTRA_BUNDLE_KIT;
        else return 0;
        return 1;
    }
    if (strcmp(word[0], "id") == 0)
        return count == 2u && manifest->id[0] == '\0' && identifier(word[1]) &&
               copy(manifest->id, sizeof(manifest->id), word[1]);
    if (strcmp(word[0], "name") == 0)
        return count == 2u && manifest->name[0] == '\0' &&
               copy(manifest->name, sizeof(manifest->name), word[1]);
    if (strcmp(word[0], "version") == 0)
        return count == 2u && manifest->version.major == 0u &&
               manifest->version.minor == 0u && manifest->version.patch == 0u &&
               version(word[1], &manifest->version);
    if (strcmp(word[0], "executable") == 0)
        return count == 2u && manifest->executable[0] == '\0' &&
               relative_path(word[1]) &&
               copy(manifest->executable, sizeof(manifest->executable), word[1]);
    if (strcmp(word[0], "icon") == 0)
        return count == 2u && manifest->icon[0] == '\0' &&
               relative_path(word[1]) &&
               copy(manifest->icon, sizeof(manifest->icon), word[1]);
    if (strcmp(word[0], "capability") == 0) {
        if (count != 2u || manifest->capability_count == ASTRA_BUNDLE_CAPABILITY_MAX)
            return 0;
        if (!copy(manifest->capabilities[manifest->capability_count],
                  ASTRA_BUNDLE_NAME_MAX, word[1])) return 0;
        ++manifest->capability_count;
        return 1;
    }
    if (strcmp(word[0], "requires") == 0 ||
        strcmp(word[0], "provides") == 0) {
        AstraBundleLibrary *entry;
        uint16_t *used;
        if (count != 4u) return 0;
        if (strcmp(word[0], "requires") == 0) {
            entry = manifest->requires;
            used = &manifest->require_count;
        } else {
            entry = manifest->provides;
            used = &manifest->provide_count;
        }
        if (*used == ASTRA_BUNDLE_LIBRARY_MAX || !library(word, &entry[*used]))
            return 0;
        ++*used;
        return 1;
    }
    return 0;
}

uint32_t astra_bundle_manifest_parse(char *text, uint32_t length,
                                     AstraBundleManifest *manifest,
                                     uint32_t *error_line)
{
    uint32_t start = 0u;
    uint32_t line = 1u;
    if (error_line != NULL) *error_line = 0u;
    if (text == NULL || manifest == NULL || length == 0u ||
        length > ASTRA_BUNDLE_MANIFEST_MAX ||
        !astra_manifest_text_valid(text, length))
        return ASTRA_BUNDLE_INVALID;
    memset(manifest, 0, sizeof(*manifest));
    for (uint32_t at = 0u; at <= length; ++at) {
        if (at != length && text[at] != '\n' && text[at] != '\r') continue;
        {
            char separator = text[at];
            text[at] = '\0';
            if (parse_line(&text[start], manifest) == 0) {
                if (error_line != NULL) *error_line = line;
                memset(manifest, 0, sizeof(*manifest));
                return ASTRA_BUNDLE_INVALID;
            }
            if (separator == '\r' && at < length && text[at + 1u] == '\n') ++at;
        }
        start = at + 1u;
        ++line;
    }
    if (manifest->format_version == 0u || manifest->kind == 0u ||
        manifest->id[0] == '\0' || manifest->name[0] == '\0' ||
        (manifest->version.major == 0u && manifest->version.minor == 0u &&
         manifest->version.patch == 0u) ||
        (manifest->kind == ASTRA_BUNDLE_APPLICATION &&
         (manifest->executable[0] == '\0' || manifest->icon[0] == '\0')) ||
        (manifest->kind == ASTRA_BUNDLE_KIT && manifest->provide_count == 0u))
        return ASTRA_BUNDLE_MISSING;
    return ASTRA_BUNDLE_OK;
}

uint32_t astra_aicon_open(const void *source, uint32_t length, AstraAicon *icon)
{
    const uint8_t *bytes = source;
    AstraAicon parsed;
    uint32_t palette_bytes;
    uint32_t strike_bytes;
    uint32_t seen = 0u;

    if (icon == NULL) return ASTRA_BUNDLE_INVALID;
    memset(icon, 0, sizeof(*icon));
    if (bytes == NULL || length < ASTRA_AICON_HEADER_SIZE ||
        astra_load_be32(bytes) != ASTRA_AICON_MAGIC ||
        astra_load_be16(bytes + 4u) != ASTRA_AICON_VERSION ||
        astra_load_be16(bytes + 6u) != ASTRA_AICON_HEADER_SIZE ||
        astra_load_be32(bytes + 8u) != length ||
        astra_load_be16(bytes + 12u) != ASTRA_AICON_REQUIRED_STRIKES ||
        astra_load_be16(bytes + 14u) == 0u ||
        astra_load_be16(bytes + 14u) > 256u ||
        astra_load_be32(bytes + 28u) != 0u)
        return ASTRA_BUNDLE_INVALID;
    parsed.bytes = bytes;
    parsed.length = length;
    parsed.strike_count = astra_load_be16(bytes + 12u);
    parsed.palette_count = astra_load_be16(bytes + 14u);
    parsed.palette_offset = astra_load_be32(bytes + 16u);
    parsed.strike_offset = astra_load_be32(bytes + 20u);
    parsed.data_offset = astra_load_be32(bytes + 24u);
    palette_bytes = (uint32_t)parsed.palette_count * 4u;
    strike_bytes = (uint32_t)parsed.strike_count * ASTRA_AICON_STRIKE_SIZE;
    if (parsed.palette_offset < ASTRA_AICON_HEADER_SIZE ||
        parsed.palette_offset > length ||
        palette_bytes > length - parsed.palette_offset ||
        parsed.strike_offset < parsed.palette_offset + palette_bytes ||
        parsed.strike_offset > length ||
        strike_bytes > length - parsed.strike_offset ||
        parsed.data_offset < parsed.strike_offset + strike_bytes ||
        parsed.data_offset > length ||
        bytes[parsed.palette_offset + 3u] != 0u)
        return ASTRA_BUNDLE_INVALID;
    for (uint32_t at = 0u; at < parsed.strike_count; ++at) {
        const uint8_t *record = bytes + parsed.strike_offset +
                                at * ASTRA_AICON_STRIKE_SIZE;
        uint16_t width = astra_load_be16(record);
        uint16_t height = astra_load_be16(record + 2u);
        uint32_t offset = astra_load_be32(record + 4u);
        uint32_t count = astra_load_be32(record + 8u);
        uint32_t bit = width == 16u ? 1u : (width == 32u ? 2u :
                       (width == 64u ? 4u : 0u));

        if (bit == 0u || (seen & bit) != 0u || width != height ||
            count != (uint32_t)width * height ||
            astra_load_be32(record + 12u) != 0u ||
            offset < parsed.data_offset ||
            offset > length || count > length - offset)
            return ASTRA_BUNDLE_INVALID;
        for (uint32_t pixel = 0u; pixel < count; ++pixel)
            if (bytes[offset + pixel] >= parsed.palette_count)
                return ASTRA_BUNDLE_INVALID;
        seen |= bit;
    }
    if (seen != 7u) return ASTRA_BUNDLE_INVALID;
    *icon = parsed;
    return ASTRA_BUNDLE_OK;
}

uint32_t astra_aicon_strike(const AstraAicon *icon, uint16_t size,
                            AstraAiconStrike *strike)
{
    if (icon == NULL || strike == NULL || icon->bytes == NULL ||
        icon->strike_offset > icon->length ||
        (uint32_t)icon->strike_count * ASTRA_AICON_STRIKE_SIZE >
            icon->length - icon->strike_offset)
        return ASTRA_BUNDLE_INVALID;
    for (uint32_t at = 0u; at < icon->strike_count; ++at) {
        const uint8_t *record = icon->bytes + icon->strike_offset +
                                at * ASTRA_AICON_STRIKE_SIZE;
        uint16_t width = astra_load_be16(record);
        uint16_t height = astra_load_be16(record + 2u);
        uint32_t offset = astra_load_be32(record + 4u);
        uint32_t count = astra_load_be32(record + 8u);
        if (width != height || count != (uint32_t)width * height ||
            offset < icon->data_offset || offset > icon->length ||
            count > icon->length - offset) return ASTRA_BUNDLE_INVALID;
        if (width == size) {
            strike->width = width;
            strike->height = height;
            strike->pixels = icon->bytes + offset;
            strike->length = count;
            return ASTRA_BUNDLE_OK;
        }
    }
    return ASTRA_BUNDLE_MISSING;
}

uint32_t astra_aicon_palette(const AstraAicon *icon, uint16_t index,
                             uint8_t rgba[4])
{
    if (icon == NULL || rgba == NULL || icon->bytes == NULL ||
        index >= icon->palette_count || icon->palette_offset > icon->length ||
        (uint32_t)icon->palette_count * 4u >
            icon->length - icon->palette_offset)
        return ASTRA_BUNDLE_INVALID;
    memcpy(rgba, icon->bytes + icon->palette_offset + index * 4u, 4u);
    return ASTRA_BUNDLE_OK;
}
