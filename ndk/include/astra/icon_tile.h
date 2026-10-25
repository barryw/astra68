#ifndef ASTRA_ICON_TILE_H
#define ASTRA_ICON_TILE_H

/** @file icon_tile.h @brief Shared icon-and-label tile for Desktop and browsers. */

#include <stddef.h>

#include <astra/graphics.h>

ASTRA_EXTERN_C_BEGIN

enum {
    ASTRA_ICON_TILE_DESKTOP = 1u,
    ASTRA_ICON_TILE_CLIENT = 2u
};

#define ASTRA_ICON_TILE_SOURCE_SIZE 64u
#define ASTRA_ICON_TILE_DESKTOP_WIDTH 80u
#define ASTRA_ICON_TILE_DESKTOP_HEIGHT 104u
#define ASTRA_ICON_TILE_DESKTOP_ICON_SIZE 64u
#define ASTRA_ICON_TILE_DESKTOP_ICON_TOP 8u
#define ASTRA_ICON_TILE_DESKTOP_LABEL_Y 80u
#define ASTRA_ICON_TILE_DESKTOP_EDITOR_Y 75u
#define ASTRA_ICON_TILE_CLIENT_WIDTH 104u
#define ASTRA_ICON_TILE_CLIENT_HEIGHT 88u
#define ASTRA_ICON_TILE_CLIENT_ICON_SIZE 40u
#define ASTRA_ICON_TILE_CLIENT_ICON_TOP 8u
#define ASTRA_ICON_TILE_CLIENT_LABEL_Y 56u
#define ASTRA_ICON_TILE_CLIENT_EDITOR_Y 47u
#define ASTRA_ICON_TILE_CLIENT_LABEL_INSET 8u

typedef struct AstraIconTileInfo {
    uint32_t size;
    uint32_t style;
    AstraRectI32 frame;
    AstraRectI32 clip;
    const AstraSurface *icon;
    const char *label;
    uint32_t label_bytes;
    const AstraSurface *editor;
    uint32_t reserved[2];
} AstraIconTileInfo;

#define ASTRA_ICON_TILE_INFO_INIT { \
    sizeof(AstraIconTileInfo), ASTRA_ICON_TILE_CLIENT, \
    {0, 0, 0, 0}, {0, 0, 0, 0}, NULL, NULL, 0u, NULL, {0u, 0u} \
}

/** Paint icon, fitted label or active inline editor through a hardware draw list.
 * Caller supplies tile frame from its existing Container/ScrollView layout.
 * Editor, when present, replaces label but keeps icon and hit target.
 */
AstraResult astra_interface_icon_tile_render(AstraDrawList *list,
    const AstraIconTileInfo *info);

/** Test pointer against tile frame intersected with viewport clip. */
int astra_interface_icon_tile_hit(const AstraIconTileInfo *info,
    int32_t x, int32_t y);

ASTRA_EXTERN_C_END

#endif
