#include <astra/font.h>

/* Prepare reusable UI text when the operating-system font service is present. */
AstraResult prepare_status_layout(const char *utf8,
                                  uint32_t utf8_bytes,
                                  AstraTextLayout *result)
{
    ASTRA_AUTO_FONT_FACE(face);
    ASTRA_AUTO_FONT(font);
    AstraFontRequest request = ASTRA_FONT_REQUEST_INIT;
    AstraTextLayoutOptions options = ASTRA_TEXT_LAYOUT_OPTIONS_INIT;
    AstraResult status;

    if (result == 0 || (utf8 == 0 && utf8_bytes != 0))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!astra_fonts_present())
        return ASTRA_ERROR_NOT_PRESENT;

    status = astra_font_face_open_system(ASTRA_FONT_ROLE_UI, &face);
    if (status != ASTRA_OK)
        return status;

    request.pixel_height = 16 * ASTRA_FIXED26_6_ONE;
    status = astra_font_create(&face, &request, "en", 2, &font);
    if (status != ASTRA_OK)
        return status;

    options.flags = ASTRA_TEXT_LAYOUT_WRAP_WORD |
                    ASTRA_TEXT_LAYOUT_WRAP_GRAPHEME;
    options.max_width = 640 * ASTRA_FIXED26_6_ONE;
    return astra_text_layout_create(&font, utf8, utf8_bytes,
                                    &options, result);
}
