#include <astra/icon_tile.h>

#include <astra/bytes.h>
#include <astra/draw_list.h>
#include <astra/inline_edit.h>
#include <astra/surface.h>
#include <astra/theme.h>
#include <astra/utf8.h>

#include <limits.h>

typedef struct IconTileMetrics {
    uint32_t icon_size;
    uint32_t icon_top;
    uint32_t label_inset;
    uint32_t label_y;
    uint32_t editor_y;
} IconTileMetrics;

static int metrics(const AstraIconTileInfo *info, IconTileMetrics *out)
{
    if (info == NULL || out == NULL ||
        info->size < offsetof(AstraIconTileInfo, badge) ||
        info->reserved[0] != 0u || info->reserved[1] != 0u ||
        (info->label_bytes != 0u && (info->label == NULL ||
            !astra_utf8_validate(info->label, info->label_bytes, 0u))))
        return 0;
    if (info->style == ASTRA_ICON_TILE_DESKTOP)
        *out = (IconTileMetrics){ASTRA_ICON_TILE_DESKTOP_ICON_SIZE,
            ASTRA_ICON_TILE_DESKTOP_ICON_TOP, 0u,
            ASTRA_ICON_TILE_DESKTOP_LABEL_Y,
            ASTRA_ICON_TILE_DESKTOP_EDITOR_Y};
    else if (info->style == ASTRA_ICON_TILE_CLIENT)
        *out = (IconTileMetrics){ASTRA_ICON_TILE_CLIENT_ICON_SIZE,
            ASTRA_ICON_TILE_CLIENT_ICON_TOP, ASTRA_ICON_TILE_CLIENT_LABEL_INSET,
            ASTRA_ICON_TILE_CLIENT_LABEL_Y,
            ASTRA_ICON_TILE_CLIENT_EDITOR_Y};
    else return 0;
    return info->frame.width >= out->icon_size &&
        info->frame.height >= out->label_y &&
        info->clip.width != 0u && info->clip.height != 0u;
}

static int inside(const AstraRectI32 *rect, int32_t x, int32_t y)
{
    return x >= rect->x && y >= rect->y &&
        (int64_t)x < (int64_t)rect->x + rect->width &&
        (int64_t)y < (int64_t)rect->y + rect->height;
}

int astra_interface_icon_tile_hit(const AstraIconTileInfo *info,
    int32_t x, int32_t y)
{
    IconTileMetrics style;
    return metrics(info, &style) &&
        inside(&info->frame, x, y) && inside(&info->clip, x, y);
}

AstraResult astra_interface_icon_tile_render(AstraDrawList *list,
    const AstraIconTileInfo *info)
{
    IconTileMetrics style;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraBlitOptions blend = ASTRA_BLIT_OPTIONS_INIT;
    AstraResult result;

    if (list == NULL || !metrics(info, &style))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    int64_t icon_x = (int64_t)info->frame.x +
        (info->frame.width - style.icon_size) / 2u;
    int64_t icon_y = (int64_t)info->frame.y + style.icon_top;
    int64_t label_y = (int64_t)info->frame.y + style.label_y;
    int64_t editor_x = (int64_t)info->frame.x +
        ((int64_t)info->frame.width - ASTRA_INLINE_EDIT_WIDTH) / 2;
    int64_t editor_y = (int64_t)info->frame.y + style.editor_y;
    if (icon_x < INT32_MIN || icon_x > INT32_MAX ||
        icon_y < INT32_MIN || icon_y > INT32_MAX ||
        label_y < INT32_MIN || label_y > INT32_MAX ||
        editor_x < INT32_MIN || editor_x > INT32_MAX ||
        editor_y < INT32_MIN || editor_y > INT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_draw_list_set_clip(list, &info->clip);
    blend.blend = ASTRA_BLEND_ALPHA;
    if (result == ASTRA_OK && info->icon != NULL &&
        info->icon->_private_handle != ASTRA_INVALID_HANDLE)
        result = astra_draw_blit(list, info->icon,
            &(AstraRectI32){0, 0, ASTRA_ICON_TILE_SOURCE_SIZE,
                ASTRA_ICON_TILE_SOURCE_SIZE},
            &(AstraRectI32){(int32_t)icon_x, (int32_t)icon_y,
                style.icon_size, style.icon_size}, &blend);
    if (result == ASTRA_OK && info->size >= sizeof(*info) &&
        info->badge != NULL &&
        info->badge->_private_handle != ASTRA_INVALID_HANDLE) {
        uint32_t badge_size = style.icon_size / 2u;
        result = astra_draw_blit(list, info->badge,
            &(AstraRectI32){0, 0, ASTRA_ICON_TILE_SOURCE_SIZE,
                ASTRA_ICON_TILE_SOURCE_SIZE},
            &(AstraRectI32){(int32_t)(icon_x + style.icon_size - badge_size),
                (int32_t)(icon_y + style.icon_size - badge_size),
                badge_size, badge_size}, &blend);
    }
    if (result != ASTRA_OK) return result;
    if (info->editor != NULL)
        return astra_draw_blit(list, info->editor,
            &(AstraRectI32){0, 0, ASTRA_INLINE_EDIT_WIDTH, ASTRA_INLINE_EDIT_HEIGHT},
            &(AstraRectI32){(int32_t)editor_x, (int32_t)editor_y,
                ASTRA_INLINE_EDIT_WIDTH, ASTRA_INLINE_EDIT_HEIGHT}, &blend);
    if (info->label_bytes == 0u) return ASTRA_OK;
    uint32_t limit = info->frame.width - style.label_inset;
    uint32_t length = astra_surface_ui_text_fit(info->label,
        info->label_bytes, theme.title_font_height, limit);
    if (length == 0u) return ASTRA_OK;
    uint32_t width = astra_surface_ui_text_width(info->label, length,
        theme.title_font_height);
    int64_t label_x = (int64_t)info->frame.x +
        ((int64_t)info->frame.width - width) / 2;
    if (label_x < INT32_MIN || label_x > INT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    return astra_draw_ui_text(list,
        (AstraPointI32){(int32_t)label_x, (int32_t)label_y},
        info->label, length, theme.title_font_height, 0u,
        info->style == ASTRA_ICON_TILE_DESKTOP ?
            theme.text_primary : theme.client_text);
}

AstraResult astra_interface_icon_tile_render_view(AstraSurfaceView *view,
    const AstraIconTileInfo *info)
{
    IconTileMetrics style;
    AstraTheme theme = ASTRA_THEME_SYSTEM_INIT;
    AstraBlitOptions blend = ASTRA_BLIT_OPTIONS_INIT;
    AstraSurfaceView clipped;
    int64_t icon_x, icon_y, label_y, editor_x, editor_y;
    uint32_t limit, length, width;
    int64_t label_x;

    if (view == NULL || view->kind != ASTRA_SURFACE_VIEW_DRAW_LIST ||
        !metrics(info, &style))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    clipped = *view;
    if (!astra_surface_clip(&clipped, info->clip.x, info->clip.y,
                            info->clip.width, info->clip.height))
        return ASTRA_OK;
    icon_x = (int64_t)info->frame.x +
        (info->frame.width - style.icon_size) / 2u;
    icon_y = (int64_t)info->frame.y + style.icon_top;
    label_y = (int64_t)info->frame.y + style.label_y;
    editor_x = (int64_t)info->frame.x +
        ((int64_t)info->frame.width - ASTRA_INLINE_EDIT_WIDTH) / 2;
    editor_y = (int64_t)info->frame.y + style.editor_y;
    if (icon_x < INT32_MIN || icon_x > INT32_MAX ||
        icon_y < INT32_MIN || icon_y > INT32_MAX ||
        label_y < INT32_MIN || label_y > INT32_MAX ||
        editor_x < INT32_MIN || editor_x > INT32_MAX ||
        editor_y < INT32_MIN || editor_y > INT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    blend.blend = ASTRA_BLEND_ALPHA;
    if (info->icon != NULL &&
        info->icon->_private_handle != ASTRA_INVALID_HANDLE &&
        !astra_surface_draw_blit(&clipped, info->icon,
            &(AstraRectI32){0, 0, ASTRA_ICON_TILE_SOURCE_SIZE,
                ASTRA_ICON_TILE_SOURCE_SIZE},
            &(AstraRectI32){(int32_t)icon_x, (int32_t)icon_y,
                style.icon_size, style.icon_size}, &blend))
        return ASTRA_ERROR_NO_RESOURCES;
    if (info->size >= sizeof(*info) && info->badge != NULL &&
        info->badge->_private_handle != ASTRA_INVALID_HANDLE) {
        uint32_t badge_size = style.icon_size / 2u;
        if (!astra_surface_draw_blit(&clipped, info->badge,
            &(AstraRectI32){0, 0, ASTRA_ICON_TILE_SOURCE_SIZE,
                ASTRA_ICON_TILE_SOURCE_SIZE},
            &(AstraRectI32){(int32_t)(icon_x + style.icon_size - badge_size),
                (int32_t)(icon_y + style.icon_size - badge_size),
                badge_size, badge_size}, &blend))
            return ASTRA_ERROR_NO_RESOURCES;
    }
    if (info->editor != NULL) {
        if (!astra_surface_draw_blit(&clipped, info->editor,
            &(AstraRectI32){0, 0, ASTRA_INLINE_EDIT_WIDTH,
                ASTRA_INLINE_EDIT_HEIGHT},
            &(AstraRectI32){(int32_t)editor_x, (int32_t)editor_y,
                ASTRA_INLINE_EDIT_WIDTH, ASTRA_INLINE_EDIT_HEIGHT}, &blend))
            return ASTRA_ERROR_NO_RESOURCES;
        return ASTRA_OK;
    }
    if (info->label_bytes == 0u) return ASTRA_OK;
    limit = info->frame.width - style.label_inset;
    length = astra_surface_ui_text_fit(info->label, info->label_bytes,
        theme.title_font_height, limit);
    if (length == 0u) return ASTRA_OK;
    width = astra_surface_ui_text_width(info->label, length,
        theme.title_font_height);
    label_x = (int64_t)info->frame.x +
        ((int64_t)info->frame.width - width) / 2;
    if (label_x < INT32_MIN || label_x > INT32_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    astra_surface_ui_text(&clipped, (int32_t)label_x, (int32_t)label_y,
        info->label, length, theme.title_font_height,
        astra_surface_rgb565(theme.client_text.red, theme.client_text.green,
                             theme.client_text.blue));
    return ASTRA_OK;
}
