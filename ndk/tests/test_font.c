#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <astra/font.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

_Static_assert(sizeof(AstraColorRGBA8) == 4, "AstraColorRGBA8 ABI");
_Static_assert(sizeof(AstraFontFace) == 4, "AstraFontFace ABI");
_Static_assert(sizeof(AstraFont) == 4, "AstraFont ABI");
_Static_assert(sizeof(AstraTextLayout) == 4, "AstraTextLayout ABI");
_Static_assert(sizeof(AstraFontRequest) == 44, "AstraFontRequest ABI");
_Static_assert(sizeof(AstraFontInfo) == 68, "AstraFontInfo ABI");
_Static_assert(sizeof(AstraFontMetrics) == 52, "AstraFontMetrics ABI");
_Static_assert(sizeof(AstraTextLayoutOptions) == 48,
               "AstraTextLayoutOptions ABI");
_Static_assert(sizeof(AstraTextRect) == 16, "AstraTextRect ABI");
_Static_assert(sizeof(AstraTextMetrics) == 84, "AstraTextMetrics ABI");
_Static_assert(sizeof(AstraTextHit) == 48, "AstraTextHit ABI");
_Static_assert(sizeof(AstraTextPaint) == 40, "AstraTextPaint ABI");

static void exercise_empty_auto_cleanup(void)
{
    ASTRA_AUTO_FONT_FACE(face);
    ASTRA_AUTO_FONT(font);
    ASTRA_AUTO_TEXT_LAYOUT(layout);

    CHECK(face._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(font._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(layout._private_handle == ASTRA_INVALID_HANDLE);
}

static void test_initializers(void)
{
    AstraFontRequest request = ASTRA_FONT_REQUEST_INIT;
    AstraFontInfo info = ASTRA_FONT_INFO_INIT;
    AstraFontMetrics metrics = ASTRA_FONT_METRICS_INIT;
    AstraTextLayoutOptions options = ASTRA_TEXT_LAYOUT_OPTIONS_INIT;
    AstraTextMetrics text_metrics = ASTRA_TEXT_METRICS_INIT;
    AstraTextHit hit = ASTRA_TEXT_HIT_INIT;
    AstraTextPaint paint = ASTRA_TEXT_PAINT_INIT;

    CHECK(request.size == sizeof(request));
    CHECK(request.match_flags == ASTRA_FONT_MATCH_ALLOW_FALLBACK);
    CHECK(request.weight == 400 && request.stretch_percent == 100);
    CHECK(info.size == sizeof(info));
    CHECK(metrics.size == sizeof(metrics));
    CHECK(options.size == sizeof(options));
    CHECK(options.alignment == ASTRA_TEXT_ALIGNMENT_START);
    CHECK(options.direction == ASTRA_TEXT_DIRECTION_AUTO);
    CHECK(text_metrics.size == sizeof(text_metrics));
    CHECK(hit.size == sizeof(hit));
    CHECK(paint.size == sizeof(paint));
    CHECK(paint.foreground.red == 255 && paint.foreground.green == 255);
    CHECK(paint.foreground.blue == 255 && paint.foreground.alpha == 255);
    CHECK(paint.background.alpha == 0);
    CHECK(paint.embedded_color_policy == ASTRA_TEXT_EMBEDDED_COLOR_USE);
}

static void test_unavailable_face(void)
{
    static const char valid_utf8_family[] = "Caf\xc3\xa9";
    static const char embedded_nul[] = { 'A', '\0', 'B' };
    static const char surrogate[] = "\xed\xa0\x80";
    static const char out_of_range[] = "\xf4\x90\x80\x80";
    AstraFontFace face = ASTRA_FONT_FACE_INIT;
    AstraFontFace occupied = { UINT32_C(1) };
    AstraFontInfo info = ASTRA_FONT_INFO_INIT;
    uint32_t required = 0;

    CHECK(!astra_fonts_present());
    CHECK(astra_font_face_open_system(ASTRA_FONT_ROLE_UI, &face) ==
          ASTRA_ERROR_NOT_PRESENT);
    CHECK(face._private_handle == ASTRA_INVALID_HANDLE);
    CHECK(astra_font_face_open_system(0, &face) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_open_system(ASTRA_FONT_ROLE_UI, 0) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_open_system(ASTRA_FONT_ROLE_UI, &occupied) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_font_face_open_family("Astra Sans", 10, 0, 0, &face) ==
          ASTRA_ERROR_NOT_PRESENT);
    CHECK(astra_font_face_open_family(valid_utf8_family,
                                      sizeof(valid_utf8_family) - 1,
                                      0, 0, &face) ==
          ASTRA_ERROR_NOT_PRESENT);
    CHECK(astra_font_face_open_family(0, 10, 0, 0, &face) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_open_family("A", 1, 0, 1, &face) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_open_family("\xc0\x80", 2, 0, 0, &face) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_open_family(embedded_nul, sizeof(embedded_nul),
                                      0, 0, &face) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_open_family(surrogate, sizeof(surrogate) - 1,
                                      0, 0, &face) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_open_family(out_of_range,
                                      sizeof(out_of_range) - 1,
                                      0, 0, &face) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_font_face_close(0) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_close(&face) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_font_face_get_info(&face, &info) ==
          ASTRA_ERROR_INVALID_HANDLE);
    info.size = 0;
    CHECK(astra_font_face_get_info(&face, &info) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_get_string(&face, ASTRA_FONT_STRING_FAMILY,
                                     0, 0, &required) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_font_face_get_string(&face, 0, 0, 0, &required) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_face_get_string(&face, ASTRA_FONT_STRING_FAMILY,
                                     0, 1, &required) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
}

static void test_unavailable_font_and_layout(void)
{
    static const char valid_utf8_text[] =
        "\xc2\xa2\xe2\x82\xac\xf0\x9f\x98\x80";
    static const char embedded_nul[] = { 'A', '\0', 'B' };
    static const char truncated_utf8[] = "\xe2\x82";
    static const char surrogate[] = "\xed\xa0\x80";
    static const char out_of_range[] = "\xf4\x90\x80\x80";
    AstraFontFace face = ASTRA_FONT_FACE_INIT;
    AstraFont font = ASTRA_FONT_INIT;
    AstraTextLayout layout = ASTRA_TEXT_LAYOUT_INIT;
    AstraFontRequest request = ASTRA_FONT_REQUEST_INIT;
    AstraFontInfo info = ASTRA_FONT_INFO_INIT;
    AstraFontMetrics metrics = ASTRA_FONT_METRICS_INIT;
    AstraTextLayoutOptions options = ASTRA_TEXT_LAYOUT_OPTIONS_INIT;
    AstraTextMetrics text_metrics = ASTRA_TEXT_METRICS_INIT;
    AstraTextHit hit = ASTRA_TEXT_HIT_INIT;
    uint32_t required = 0;

    request.pixel_height = 16 * ASTRA_FIXED26_6_ONE;
    CHECK(astra_font_create(&face, &request, 0, 0, &font) ==
          ASTRA_ERROR_INVALID_HANDLE);
    request.pixel_height = 0;
    CHECK(astra_font_create(&face, &request, 0, 0, &font) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    request.pixel_height = 16 * ASTRA_FIXED26_6_ONE;
    request.reserved[2] = 1;
    CHECK(astra_font_create(&face, &request, 0, 0, &font) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    request.reserved[2] = 0;
    CHECK(astra_font_create(&face, &request, "zh-Hant-TW", 10, &font) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_font_create(&face, &request, "en--US", 6, &font) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_create(&face, &request, "-en", 3, &font) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_create(&face, &request, "en-", 3, &font) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_create(&face, &request, "en_US", 5, &font) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_font_close(0) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_font_close(&font) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_font_get_info(&font, &info) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_font_get_metrics(&font, &metrics) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_font_get_string(&font, ASTRA_FONT_STRING_STYLE,
                                0, 0, &required) ==
          ASTRA_ERROR_INVALID_HANDLE);

    CHECK(astra_text_layout_create(&font, "test", 4, &options, &layout) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_text_layout_create(&font, valid_utf8_text,
                                   sizeof(valid_utf8_text) - 1,
                                   &options, &layout) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_text_layout_create(&font, embedded_nul,
                                   sizeof(embedded_nul), &options, &layout) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_text_layout_create(&font, 0, 1, &options, &layout) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_text_layout_create(&font, "\xff", 1, &options, &layout) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_text_layout_create(&font, truncated_utf8,
                                   sizeof(truncated_utf8) - 1,
                                   &options, &layout) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_text_layout_create(&font, surrogate,
                                   sizeof(surrogate) - 1,
                                   &options, &layout) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_text_layout_create(&font, out_of_range,
                                   sizeof(out_of_range) - 1,
                                   &options, &layout) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
    options.reserved16 = 1;
    CHECK(astra_text_layout_create(&font, "test", 4, &options, &layout) ==
          ASTRA_ERROR_INVALID_ARGUMENT);

    CHECK(astra_text_layout_close(0) == ASTRA_ERROR_INVALID_ARGUMENT);
    CHECK(astra_text_layout_close(&layout) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_text_layout_measure(&layout, &text_metrics) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_text_layout_hit_test(&layout, 0, 0, &hit) ==
          ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_text_layout_get_caret(&layout, 0, ASTRA_TEXT_EDGE_LEADING,
                                      &hit) == ASTRA_ERROR_INVALID_HANDLE);
    CHECK(astra_text_layout_get_caret(&layout, 0, 3, &hit) ==
          ASTRA_ERROR_INVALID_ARGUMENT);
}

int main(void)
{
    test_initializers();
    test_unavailable_face();
    test_unavailable_font_and_layout();
    exercise_empty_auto_cleanup();
    puts("PASS Astra NDK font contract and unavailable backend");
    return 0;
}
