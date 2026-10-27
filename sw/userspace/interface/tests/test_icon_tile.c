#include <astra/icon_tile.h>
#include <astra/inline_edit.h>
#include <astra/surface.h>

#include <assert.h>
#include <string.h>

static AstraRectI32 clip_seen, source_seen, destination_seen;
static AstraPointI32 text_seen;
static unsigned blits, texts;

AstraResult astra_draw_list_set_clip(AstraDrawList *list, const AstraRectI32 *clip)
{
    (void)list;
    clip_seen = *clip;
    return ASTRA_OK;
}

AstraResult astra_draw_blit(AstraDrawList *list, const AstraSurface *source,
    const AstraRectI32 *source_rect, const AstraRectI32 *destination_rect,
    const AstraBlitOptions *options)
{
    (void)list;
    (void)source;
    assert(options->blend == ASTRA_BLEND_ALPHA);
    source_seen = *source_rect;
    destination_seen = *destination_rect;
    ++blits;
    return ASTRA_OK;
}

uint32_t astra_surface_ui_text_fit(const char *text, uint32_t bytes,
    uint16_t height, uint32_t width)
{
    (void)text;
    (void)height;
    assert(width == ASTRA_ICON_TILE_DESKTOP_WIDTH ||
        width == ASTRA_ICON_TILE_CLIENT_WIDTH - ASTRA_ICON_TILE_CLIENT_LABEL_INSET);
    return bytes;
}

uint32_t astra_surface_ui_text_width(const char *text, uint32_t bytes,
    uint16_t height)
{
    (void)text;
    (void)height;
    return bytes * 8u;
}

AstraResult astra_draw_ui_text(AstraDrawList *list, AstraPointI32 origin,
    const char *text, uint32_t bytes, uint32_t height, uint32_t flags,
    AstraColorRGBA8 color)
{
    (void)list;
    (void)text;
    (void)bytes;
    (void)height;
    (void)flags;
    (void)color;
    text_seen = origin;
    ++texts;
    return ASTRA_OK;
}

int main(void)
{
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraSurface icon = ASTRA_SURFACE_INIT;
    AstraSurface editor = ASTRA_SURFACE_INIT;
    AstraSurface badge = ASTRA_SURFACE_INIT;
    AstraIconTileInfo tile = ASTRA_ICON_TILE_INFO_INIT;
    icon._private_handle = 1u;
    editor._private_handle = 2u;
    badge._private_handle = 3u;
    tile.icon = &icon;
    tile.label = "Folder1";
    tile.label_bytes = (uint32_t)strlen(tile.label);
    tile.frame = (AstraRectI32){32, 26, ASTRA_ICON_TILE_DESKTOP_WIDTH,
        ASTRA_ICON_TILE_DESKTOP_HEIGHT};
    tile.clip = (AstraRectI32){0, 0, 200, 200};
    tile.style = ASTRA_ICON_TILE_DESKTOP;
    assert(astra_interface_icon_tile_render(&list, &tile) == ASTRA_OK);
    assert(clip_seen.width == 200u && clip_seen.height == 200u);
    assert(source_seen.width == ASTRA_ICON_TILE_SOURCE_SIZE);
    assert(destination_seen.x == 40 && destination_seen.y == 34);
    assert(destination_seen.width == ASTRA_ICON_TILE_DESKTOP_ICON_SIZE);
    assert(text_seen.x == 44 && text_seen.y == 106);
    assert(blits == 1u && texts == 1u);
    tile.badge = &badge;
    assert(astra_interface_icon_tile_render(&list, &tile) == ASTRA_OK);
    assert(blits == 3u && texts == 2u);
    assert(destination_seen.x == 72 && destination_seen.y == 66);
    assert(destination_seen.width == 32u);
    tile.badge = NULL;
    assert(astra_interface_icon_tile_hit(&tile, 32, 26));
    assert(astra_interface_icon_tile_hit(&tile, 111, 129));
    assert(!astra_interface_icon_tile_hit(&tile, 112, 129));
    tile.clip = (AstraRectI32){40, 40, 20, 20};
    assert(!astra_interface_icon_tile_hit(&tile, 32, 26));

    tile.style = ASTRA_ICON_TILE_CLIENT;
    tile.frame = (AstraRectI32){20, 40, ASTRA_ICON_TILE_CLIENT_WIDTH,
        ASTRA_ICON_TILE_CLIENT_HEIGHT};
    tile.clip = (AstraRectI32){0, 0, 200, 200};
    tile.editor = &editor;
    blits = texts = 0u;
    assert(astra_interface_icon_tile_render(&list, &tile) == ASTRA_OK);
    assert(blits == 2u && texts == 0u);
    assert(source_seen.width == ASTRA_INLINE_EDIT_WIDTH);
    assert(destination_seen.x == 10 && destination_seen.y == 87);
    assert(destination_seen.width == ASTRA_INLINE_EDIT_WIDTH);
    tile.editor = NULL;
    assert(astra_interface_icon_tile_render(&list, &tile) == ASTRA_OK);
    assert(text_seen.x == 44 && text_seen.y == 96);

    tile.label = "\xff";
    tile.label_bytes = 1u;
    assert(astra_interface_icon_tile_render(&list, &tile) == ASTRA_ERROR_INVALID_ARGUMENT);
    tile.label = "Folder1";
    tile.label_bytes = 7u;
    tile.reserved[0] = 1u;
    assert(!astra_interface_icon_tile_hit(&tile, 30, 50));
    return 0;
}
