#include <astra/interface.h>
#include <astra/surface.h>
#include <astra/theme.h>

#include <assert.h>

int astra_interface_test_valid(const AstraAlertInfo *info);
void astra_interface_test_paint(AstraSurfaceView *surface,
                                const AstraAlertInfo *info);

static uint16_t first_text_color;
static uint32_t text_calls;

uint16_t astra_surface_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) | (blue >> 3));
}

void astra_surface_clear(AstraSurfaceView *surface, uint16_t color)
{
    (void)surface;
    (void)color;
}

void astra_surface_fill(AstraSurfaceView *surface, int32_t x, int32_t y,
                        uint32_t width, uint32_t height, uint16_t color)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
}

void astra_surface_fill_round(AstraSurfaceView *surface, int32_t x, int32_t y,
                              uint32_t width, uint32_t height,
                              uint16_t radius, uint16_t color)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)radius;
    (void)color;
}

uint32_t astra_surface_ui_text_width(const char *text, uint32_t length,
                                     uint16_t height)
{
    (void)text;
    (void)height;
    return length;
}

uint32_t astra_surface_ui_text_fit(const char *text, uint32_t length,
                                   uint16_t height, uint32_t maximum_width)
{
    (void)text;
    (void)height;
    (void)maximum_width;
    return length;
}

void astra_surface_ui_text(AstraSurfaceView *surface, int32_t x, int32_t y,
                           const char *text, uint32_t length,
                           uint16_t height, uint16_t color)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)text;
    (void)length;
    (void)height;
    if (text_calls++ == 0u) first_text_color = color;
}

int main(void)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;

    info.title = "Error";
    info.title_length = 5u;
    info.message = "Could not launch application.";
    info.message_length = 29u;
    info.button = "OK";
    info.button_length = 2u;
    assert(astra_interface_test_valid(&info));
    info.kind = 0u;
    assert(!astra_interface_test_valid(&info));
    info.kind = ASTRA_ALERT_ERROR;
    info.reserved[0] = 1u;
    assert(!astra_interface_test_valid(&info));
    info.reserved[0] = 0u;
    {
        AstraSurfaceView surface = {0};
        AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;

        astra_interface_test_paint(&surface, &info);
        assert(text_calls == 2u);
        assert(first_text_color == astra_surface_rgb565(
            theme.frame.red, theme.frame.green, theme.frame.blue));
        assert(first_text_color != astra_surface_rgb565(
            theme.client.red, theme.client.green, theme.client.blue));
    }
    return 0;
}
