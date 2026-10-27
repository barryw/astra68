#ifndef ASTRA_ICON_TILE_H
#define ASTRA_ICON_TILE_H

/** @file icon_tile.h @brief Shared icon-and-label tile for Desktop and browsers. */

#include <stddef.h>

#include <astra/graphics.h>

ASTRA_EXTERN_C_BEGIN

/** Tile layout style. */
enum {
    ASTRA_ICON_TILE_DESKTOP = 1u, /**< Desktop icon and label layout. */
    ASTRA_ICON_TILE_CLIENT = 2u /**< Browser/client icon and label layout. */
};

/** Source icon edge length in pixels. */
#define ASTRA_ICON_TILE_SOURCE_SIZE 64u
/** Desktop tile width in pixels. */
#define ASTRA_ICON_TILE_DESKTOP_WIDTH 80u
/** Desktop tile height in pixels. */
#define ASTRA_ICON_TILE_DESKTOP_HEIGHT 104u
/** Desktop icon edge length in pixels. */
#define ASTRA_ICON_TILE_DESKTOP_ICON_SIZE 64u
/** Desktop icon top offset in pixels. */
#define ASTRA_ICON_TILE_DESKTOP_ICON_TOP 8u
/** Desktop label top offset in pixels. */
#define ASTRA_ICON_TILE_DESKTOP_LABEL_Y 80u
/** Desktop editor top offset in pixels. */
#define ASTRA_ICON_TILE_DESKTOP_EDITOR_Y 75u
/** Client tile width in pixels. */
#define ASTRA_ICON_TILE_CLIENT_WIDTH 104u
/** Client tile height in pixels. */
#define ASTRA_ICON_TILE_CLIENT_HEIGHT 88u
/** Client icon edge length in pixels. */
#define ASTRA_ICON_TILE_CLIENT_ICON_SIZE 40u
/** Client icon top offset in pixels. */
#define ASTRA_ICON_TILE_CLIENT_ICON_TOP 8u
/** Client label top offset in pixels. */
#define ASTRA_ICON_TILE_CLIENT_LABEL_Y 56u
/** Client editor top offset in pixels. */
#define ASTRA_ICON_TILE_CLIENT_EDITOR_Y 47u
/** Client label horizontal inset in pixels. */
#define ASTRA_ICON_TILE_CLIENT_LABEL_INSET 8u

/** Caller-owned tile presentation data. */
typedef struct AstraIconTileInfo {
    uint32_t size; /**< Structure size in bytes. */
    uint32_t style; /**< ASTRA_ICON_TILE_DESKTOP or ASTRA_ICON_TILE_CLIENT. */
    AstraRectI32 frame; /**< Tile bounds in destination coordinates. */
    AstraRectI32 clip; /**< Visible viewport bounds. */
    const AstraSurface *icon; /**< Borrowed icon surface. */
    const char *label; /**< Borrowed UTF-8 label. */
    uint32_t label_bytes; /**< Label byte count. */
    const AstraSurface *editor; /**< Optional borrowed inline editor surface. */
    uint32_t reserved[2]; /**< Reserved; set to zero. */
    const AstraSurface *badge; /**< Optional 64x64 icon, shown at icon's lower right. */
} AstraIconTileInfo;

/** Empty tile initializer with client layout. */
#define ASTRA_ICON_TILE_INFO_INIT { \
    sizeof(AstraIconTileInfo), ASTRA_ICON_TILE_CLIENT, \
    {0, 0, 0, 0}, {0, 0, 0, 0}, NULL, NULL, 0u, NULL, {0u, 0u}, NULL \
}

/** Paint icon, fitted label or active inline editor through a hardware draw list.
 * Caller supplies tile frame from its existing Container/ScrollView layout.
 * Editor, when present, replaces label but keeps icon and hit target.
 * @param list Destination hardware draw list.
 * @param info Tile presentation data.
 * @return ASTRA_OK or a draw-list/validation error.
 */
AstraResult astra_interface_icon_tile_render(AstraDrawList *list,
    const AstraIconTileInfo *info);

/** Test pointer against tile frame intersected with viewport clip.
 * @param info Tile presentation data.
 * @param x Pointer x in destination coordinates.
 * @param y Pointer y in destination coordinates.
 * @return Nonzero when pointer is inside visible tile, zero otherwise.
 */
int astra_interface_icon_tile_hit(const AstraIconTileInfo *info,
    int32_t x, int32_t y);

ASTRA_EXTERN_C_END

#endif
