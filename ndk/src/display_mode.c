#include <astra/graphics.h>
#include <astra/bytes.h>

static uint32_t min_u32(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

AstraResult astra_display_layout_calculate(
    const AstraDisplayMode *mode, uint16_t output_width,
    uint16_t output_height, AstraDisplayLayout *layout)
{
    uint32_t scale;
    uint32_t width;
    uint32_t height;

    if (mode == 0 || layout == 0 || mode->size < sizeof(*mode) ||
        mode->width == 0 || mode->height == 0 || output_width == 0 ||
        output_height == 0 || mode->scaling > ASTRA_DISPLAY_SCALE_FILL ||
        !astra_words_zero(mode->reserved, 4))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (mode->width > output_width || mode->height > output_height)
        return ASTRA_ERROR_UNSUPPORTED;

    layout->source_width = mode->width;
    layout->source_height = mode->height;
    layout->crop_x = 0;
    layout->crop_y = 0;
    layout->crop_width = mode->width;
    layout->crop_height = mode->height;

    if (mode->scaling == ASTRA_DISPLAY_SCALE_FILL) {
        layout->viewport_x = 0;
        layout->viewport_y = 0;
        layout->viewport_width = output_width;
        layout->viewport_height = output_height;
        if ((uint32_t)mode->width * output_height >
            (uint32_t)mode->height * output_width) {
            width = (uint32_t)mode->height * output_width / output_height;
            layout->crop_width = (uint16_t)width;
            layout->crop_x = (uint16_t)((mode->width - width) / 2u);
        } else {
            height = (uint32_t)mode->width * output_height / output_width;
            layout->crop_height = (uint16_t)height;
            layout->crop_y = (uint16_t)((mode->height - height) / 2u);
        }
        return ASTRA_OK;
    }

    scale = min_u32(output_width / mode->width,
                    output_height / mode->height);
    if (mode->scaling == ASTRA_DISPLAY_SCALE_INTEGER ||
        (mode->scaling == ASTRA_DISPLAY_SCALE_AUTO && scale >= 2u)) {
        width = (uint32_t)mode->width * scale;
        height = (uint32_t)mode->height * scale;
    } else if ((uint32_t)output_width * mode->height <=
               (uint32_t)output_height * mode->width) {
        width = output_width;
        height = (uint32_t)output_width * mode->height / mode->width;
    } else {
        height = output_height;
        width = (uint32_t)output_height * mode->width / mode->height;
    }
    layout->viewport_width = (uint16_t)width;
    layout->viewport_height = (uint16_t)height;
    layout->viewport_x = (uint16_t)((output_width - width) / 2u);
    layout->viewport_y = (uint16_t)((output_height - height) / 2u);
    return ASTRA_OK;
}
