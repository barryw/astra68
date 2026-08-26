#include <astra/font.h>
#include <astra/bytes.h>

/* The direct-MMIO NDK has no font service; keep validation ABI-compatible. */

#define FONT_STYLE_FLAGS \
    (ASTRA_FONT_STYLE_ITALIC | ASTRA_FONT_STYLE_OBLIQUE)
#define FONT_MATCH_FLAGS \
    (ASTRA_FONT_MATCH_EXACT_STRIKE | ASTRA_FONT_MATCH_ALLOW_FALLBACK | \
     ASTRA_FONT_MATCH_PREFER_COLOR | ASTRA_FONT_MATCH_REQUIRE_COLOR)
#define TEXT_LAYOUT_FLAGS \
    (ASTRA_TEXT_LAYOUT_WRAP_WORD | ASTRA_TEXT_LAYOUT_WRAP_GRAPHEME | \
     ASTRA_TEXT_LAYOUT_ELLIPSIZE_END | \
     ASTRA_TEXT_LAYOUT_INCLUDE_TRAILING_WHITESPACE)

static int font_role_valid(uint32_t role)
{
    return role >= ASTRA_FONT_ROLE_UI && role <= ASTRA_FONT_ROLE_RESCUE;
}

static int font_string_property_valid(uint32_t property)
{
    return property >= ASTRA_FONT_STRING_FAMILY &&
           property <= ASTRA_FONT_STRING_PROVENANCE;
}

static int utf8_continuation(unsigned char value)
{
    return value >= 0x80u && value <= 0xbfu;
}

static int utf8_valid(const char *text, uint32_t bytes, int allow_nul)
{
    uint32_t index = 0;

    if (text == 0)
        return bytes == 0;
    while (index < bytes) {
        const unsigned char first = (unsigned char)text[index];
        const uint32_t remaining = bytes - index;

        if (first <= 0x7fu) {
            if (first == 0 && !allow_nul)
                return 0;
            index++;
        } else if (first >= 0xc2u && first <= 0xdfu) {
            if (remaining < 2 ||
                !utf8_continuation((unsigned char)text[index + 1]))
                return 0;
            index += 2;
        } else if (first >= 0xe0u && first <= 0xefu) {
            unsigned char second;

            if (remaining < 3)
                return 0;
            second = (unsigned char)text[index + 1];
            if (!utf8_continuation((unsigned char)text[index + 2]) ||
                (first == 0xe0u && (second < 0xa0u || second > 0xbfu)) ||
                (first == 0xedu && (second < 0x80u || second > 0x9fu)) ||
                (first != 0xe0u && first != 0xedu &&
                 !utf8_continuation(second)))
                return 0;
            index += 3;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            unsigned char second;

            if (remaining < 4)
                return 0;
            second = (unsigned char)text[index + 1];
            if (!utf8_continuation((unsigned char)text[index + 2]) ||
                !utf8_continuation((unsigned char)text[index + 3]) ||
                (first == 0xf0u && (second < 0x90u || second > 0xbfu)) ||
                (first == 0xf4u && (second < 0x80u || second > 0x8fu)) ||
                (first != 0xf0u && first != 0xf4u &&
                 !utf8_continuation(second)))
                return 0;
            index += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static int language_tag_valid(const char *tag, uint32_t bytes)
{
    uint32_t index;
    int previous_hyphen = 0;

    if (bytes == 0)
        return 1;
    if (tag == 0 || tag[0] == '-' || tag[bytes - 1] == '-')
        return 0;
    for (index = 0; index < bytes; ++index) {
        const unsigned char value = (unsigned char)tag[index];
        const int alpha = (value >= 'A' && value <= 'Z') ||
                          (value >= 'a' && value <= 'z');
        const int digit = value >= '0' && value <= '9';

        if (value == '-') {
            if (previous_hyphen)
                return 0;
            previous_hyphen = 1;
        } else {
            if (!alpha && !digit)
                return 0;
            previous_hyphen = 0;
        }
    }
    return 1;
}

static int font_request_valid(const AstraFontRequest *request)
{
    if (request == 0 || request->size < sizeof(*request) ||
        request->pixel_height <= 0 || request->pixel_width < 0 ||
        request->weight == 0 || request->weight > 1000 ||
        request->stretch_percent == 0 ||
        (request->style_flags & ~FONT_STYLE_FLAGS) != 0 ||
        (request->style_flags & FONT_STYLE_FLAGS) == FONT_STYLE_FLAGS ||
        (request->match_flags & ~FONT_MATCH_FLAGS) != 0 ||
        !astra_words_zero(request->reserved, 5))
        return 0;
    return 1;
}

static int layout_options_valid(const AstraTextLayoutOptions *options)
{
    if (options == 0)
        return 1;
    if (options->size < sizeof(*options) ||
        (options->flags & ~TEXT_LAYOUT_FLAGS) != 0 ||
        options->max_width < 0 || options->max_height < 0 ||
        options->line_height < 0 ||
        options->alignment > ASTRA_TEXT_ALIGNMENT_JUSTIFY ||
        options->direction > ASTRA_TEXT_DIRECTION_RIGHT_TO_LEFT ||
        options->reserved16 != 0 ||
        !astra_words_zero(options->reserved, 5))
        return 0;
    return 1;
}

static int string_output_valid(uint32_t property,
                               char *buffer,
                               uint32_t buffer_bytes,
                               uint32_t *required_bytes)
{
    return font_string_property_valid(property) && required_bytes != 0 &&
           (buffer != 0 || buffer_bytes == 0);
}

int astra_fonts_present(void)
{
    return 0;
}

AstraResult astra_font_face_open_system(uint32_t role, AstraFontFace *face)
{
    if (!font_role_valid(role) || face == 0 ||
        face->_private_handle != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_NOT_PRESENT;
}

AstraResult astra_font_face_open_family(
    const char *family_utf8,
    uint32_t family_bytes,
    const char *style_utf8,
    uint32_t style_bytes,
    AstraFontFace *face)
{
    if (family_bytes == 0 || !utf8_valid(family_utf8, family_bytes, 0) ||
        !utf8_valid(style_utf8, style_bytes, 0) || face == 0 ||
        face->_private_handle != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_NOT_PRESENT;
}

AstraResult astra_font_face_close(AstraFontFace *face)
{
    if (face == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_font_face_get_info(const AstraFontFace *face,
                                     AstraFontInfo *info)
{
    if (face == 0 || info == 0 || info->size < sizeof(*info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_font_face_get_string(
    const AstraFontFace *face,
    uint32_t property,
    char *buffer,
    uint32_t buffer_bytes,
    uint32_t *required_bytes)
{
    if (face == 0 ||
        !string_output_valid(property, buffer, buffer_bytes, required_bytes))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_font_create(
    const AstraFontFace *face,
    const AstraFontRequest *request,
    const char *language_utf8,
    uint32_t language_bytes,
    AstraFont *font)
{
    if (face == 0 || !font_request_valid(request) ||
        !language_tag_valid(language_utf8, language_bytes) || font == 0 ||
        font->_private_handle != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_font_close(AstraFont *font)
{
    if (font == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_font_get_info(const AstraFont *font, AstraFontInfo *info)
{
    if (font == 0 || info == 0 || info->size < sizeof(*info))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_font_get_string(
    const AstraFont *font,
    uint32_t property,
    char *buffer,
    uint32_t buffer_bytes,
    uint32_t *required_bytes)
{
    if (font == 0 ||
        !string_output_valid(property, buffer, buffer_bytes, required_bytes))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_font_get_metrics(const AstraFont *font,
                                   AstraFontMetrics *metrics)
{
    if (font == 0 || metrics == 0 || metrics->size < sizeof(*metrics))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_text_layout_create(
    const AstraFont *font,
    const char *utf8,
    uint32_t utf8_bytes,
    const AstraTextLayoutOptions *options,
    AstraTextLayout *layout)
{
    if (font == 0 || !utf8_valid(utf8, utf8_bytes, 1) ||
        !layout_options_valid(options) || layout == 0 ||
        layout->_private_handle != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_text_layout_close(AstraTextLayout *layout)
{
    if (layout == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_text_layout_measure(const AstraTextLayout *layout,
                                      AstraTextMetrics *metrics)
{
    if (layout == 0 || metrics == 0 || metrics->size < sizeof(*metrics))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_text_layout_hit_test(
    const AstraTextLayout *layout,
    AstraFixed26_6 x,
    AstraFixed26_6 y,
    AstraTextHit *hit)
{
    (void)x;
    (void)y;
    if (layout == 0 || hit == 0 || hit->size < sizeof(*hit))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

AstraResult astra_text_layout_get_caret(
    const AstraTextLayout *layout,
    uint32_t utf8_byte_offset,
    uint32_t edge,
    AstraTextHit *hit)
{
    (void)utf8_byte_offset;
    if (layout == 0 || edge > ASTRA_TEXT_EDGE_TRAILING || hit == 0 ||
        hit->size < sizeof(*hit))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_ERROR_INVALID_HANDLE;
}

void astra_font_face_cleanup(AstraFontFace *face)
{
    if (face != 0 && face->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_font_face_close(face);
        (void)ignored;
    }
}

void astra_font_cleanup(AstraFont *font)
{
    if (font != 0 && font->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_font_close(font);
        (void)ignored;
    }
}

void astra_text_layout_cleanup(AstraTextLayout *layout)
{
    if (layout != 0 && layout->_private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_text_layout_close(layout);
        (void)ignored;
    }
}
