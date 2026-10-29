#ifndef ASTRA_SYSTEM_PANELS_H
#define ASTRA_SYSTEM_PANELS_H

/** @file system_panels.h @brief Shared, retained system panels. */

#include <astra/control.h>
#include <astra/filesystem_library.h>
#include <astra/fonts.h>

ASTRA_EXTERN_C_BEGIN

enum {
    ASTRA_COLOUR_PANEL_RGB565 = 1u,
    ASTRA_COLOUR_PANEL_XRGB8888 = 2u,
    ASTRA_COLOUR_PANEL_INDEX8 = 3u
};

enum {
    ASTRA_COLOUR_PANEL_ROOT = 0u,
    ASTRA_COLOUR_PANEL_TITLE,
    ASTRA_COLOUR_PANEL_TABS,
    ASTRA_COLOUR_PANEL_SWATCHES,
    ASTRA_COLOUR_PANEL_WANTED,
    ASTRA_COLOUR_PANEL_SWATCH,
    ASTRA_COLOUR_PANEL_DIFFERENCE,
    ASTRA_COLOUR_PANEL_SLIDERS,
    ASTRA_COLOUR_PANEL_RED_LABEL,
    ASTRA_COLOUR_PANEL_RED,
    ASTRA_COLOUR_PANEL_GREEN_LABEL,
    ASTRA_COLOUR_PANEL_GREEN,
    ASTRA_COLOUR_PANEL_BLUE_LABEL,
    ASTRA_COLOUR_PANEL_BLUE,
    ASTRA_COLOUR_PANEL_FIELD_PAGE,
    ASTRA_COLOUR_PANEL_HEX_FIELD,
    ASTRA_COLOUR_PANEL_STORED_VALUE,
    ASTRA_COLOUR_PANEL_THEME_PAGE,
    ASTRA_COLOUR_PANEL_THEME_LIST,
    ASTRA_COLOUR_PANEL_RECENT_PAGE,
    ASTRA_COLOUR_PANEL_RECENT_LIST,
    ASTRA_COLOUR_PANEL_ACTIONS,
    ASTRA_COLOUR_PANEL_CANCEL,
    ASTRA_COLOUR_PANEL_USE,
    ASTRA_COLOUR_PANEL_CONTROL_COUNT
};

enum {
    ASTRA_COLOUR_PANEL_NONE = 0u,
    ASTRA_COLOUR_PANEL_CHANGED,
    ASTRA_COLOUR_PANEL_CANCELLED,
    ASTRA_COLOUR_PANEL_ACCEPTED
};

/** Caller-owned panel state. The palette remains borrowed until the panel
 * closes. packed holds RGB565, 0x00RRGGBB, or an 8-bit palette index. */
typedef struct AstraColourPanel {
    uint32_t size;
    uint32_t first_id;
    uint32_t format;
    uint32_t packed;
    const AstraColorRGBA8 *palette;
    uint32_t palette_count;
    AstraColorRGBA8 wanted;
    AstraTextModel hex_model;
    uint8_t hex_content[4096];
    _Alignas(4) uint8_t hex_metadata[4096];
    char hex_text[8];
    char difference_text[2][24];
    char stored_text[2][32];
    uint32_t active_text;
    AstraCollectionModel theme_model;
    AstraCollectionModel recent_model;
    AstraCollectionItem recent_items[2][8];
    AstraColorRGBA8 recent_colors[2][8];
    char recent_labels[2][8][8];
    uint32_t recent_count;
    uint32_t active_recent;
    uint32_t active_tab;
    uint32_t hex_valid;
    uint32_t reserved[3];
} AstraColourPanel;

#define ASTRA_COLOUR_PANEL_INIT { .size = sizeof(AstraColourPanel) }

/** Compose the Colour panel from ordinary Container, Label, Colour Well,
 * Slider, Field, List, Segmented, and Button controls. Reserve
 * ASTRA_COLOUR_PANEL_CONTROL_COUNT consecutive slots and IDs.
 * INDEX8 requires a borrowed palette of 1..256 opaque entries. */
AstraResult astra_interface_colour_panel_init(AstraColourPanel *panel,
    AstraControl *controls, uint32_t first_id, uint32_t format,
    uint32_t initial_packed, const AstraColorRGBA8 *palette,
    uint32_t palette_count);

/** Apply one ordinary UI action; returns semantic panel outcome. On ACCEPTED
 * read panel->packed. A VALUE_CHANGED action refreshes the swatch. */
AstraResult astra_interface_colour_panel_action(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome);

/** Resolve the selected encoded color to opaque RGB for preview/use. */
AstraResult astra_interface_colour_panel_color(
    const AstraColourPanel *panel, AstraColorRGBA8 *color);

/** Replace the panel's Recent palette, most recent first, up to eight opaque
 * colors. The panel copies the values and keeps all UI data caller-owned. */
AstraResult astra_interface_colour_panel_set_recent(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraColorRGBA8 *colors, uint32_t count);

enum {
    ASTRA_FILE_PANEL_OPEN = 1u,
    ASTRA_FILE_PANEL_SAVE = 2u
};

enum {
    ASTRA_FILE_PANEL_ROOT = 0u,
    ASTRA_FILE_PANEL_TITLE,
    ASTRA_FILE_PANEL_LOCATION,
    ASTRA_FILE_PANEL_SCROLL,
    ASTRA_FILE_PANEL_LIST,
    ASTRA_FILE_PANEL_NAME_LABEL,
    ASTRA_FILE_PANEL_NAME,
    ASTRA_FILE_PANEL_ACTIONS,
    ASTRA_FILE_PANEL_CANCEL,
    ASTRA_FILE_PANEL_PRIMARY,
    ASTRA_FILE_PANEL_CONTROL_COUNT
};

enum {
    ASTRA_FILE_PANEL_NONE = 0u,
    ASTRA_FILE_PANEL_NAVIGATE,
    ASTRA_FILE_PANEL_PICKED,
    ASTRA_FILE_PANEL_OVERWRITE,
    ASTRA_FILE_PANEL_CANCELLED
};

/** Borrowed directory snapshot and caller storage. The name model is required
 * for Save and unused for Open. The caller loads entries via Filesystem Kit. */
typedef struct AstraFilePanelInfo {
    uint32_t size;
    uint32_t first_id;
    uint32_t mode;
    const char *directory;
    const AstraDirectoryEntry *entries;
    uint32_t entry_count;
    AstraCollectionItem *item_storage;
    uint32_t item_capacity;
    AstraCollectionModel *collection;
    AstraScrollModel *scroll;
    AstraTextModel *name_model;
    uint32_t reserved[4];
} AstraFilePanelInfo;

#define ASTRA_FILE_PANEL_INFO_INIT { \
    sizeof(AstraFilePanelInfo), 0u, 0u, 0, 0, 0u, 0, 0u, 0, 0, 0, \
    { 0u, 0u, 0u, 0u } \
}

/** Caller-owned panel state. After NAVIGATE, PICKED, or OVERWRITE, directory
 * and leaf identify the requested entry. Only the caller performs I/O. */
typedef struct AstraFilePanel {
    uint32_t size;
    uint32_t first_id;
    uint32_t mode;
    const char *directory;
    char directory_storage[2][ASTRA_VFS_PATH_MAX];
    uint32_t active_directory;
    char leaf[ASTRA_VFS_NAME_MAX];
    const AstraDirectoryEntry *entries;
    uint32_t entry_count;
    uint32_t item_count;
    AstraCollectionItem *items;
    uint32_t item_capacity;
    AstraCollectionModel *collection;
    AstraScrollModel *scroll;
    AstraTextModel *name_model;
    uint32_t reserved[4];
} AstraFilePanel;

#define ASTRA_FILE_PANEL_INIT { .size = sizeof(AstraFilePanel) }

/** Compose Open or Save from shared List, ScrollView, Field, Labels and
 * Buttons. Entries and item storage remain caller-owned until replacement. */
AstraResult astra_interface_file_panel_init(AstraFilePanel *panel,
    AstraControl *controls, const AstraFilePanelInfo *info);

/** Replace the viewed directory after the caller loads a new snapshot. The
 * previous entries/items must remain alive until this function succeeds. */
AstraResult astra_interface_file_panel_set_directory(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls, const char *directory,
    const AstraDirectoryEntry *entries, uint32_t entry_count,
    AstraCollectionItem *item_storage, uint32_t item_capacity);

/** Route one normal UI action. Save returns OVERWRITE for an existing regular
 * file, allowing the caller to present confirmation before creating it. */
AstraResult astra_interface_file_panel_action(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome);

enum {
    ASTRA_FONT_PANEL_ROOT = 0u,
    ASTRA_FONT_PANEL_TITLE,
    ASTRA_FONT_PANEL_BROWSERS,
    ASTRA_FONT_PANEL_FACE_SCROLL,
    ASTRA_FONT_PANEL_FACE_LIST,
    ASTRA_FONT_PANEL_STRIKE_SCROLL,
    ASTRA_FONT_PANEL_STRIKE_LIST,
    ASTRA_FONT_PANEL_PREVIEW,
    ASTRA_FONT_PANEL_ELIGIBILITY,
    ASTRA_FONT_PANEL_ACTIONS,
    ASTRA_FONT_PANEL_CANCEL,
    ASTRA_FONT_PANEL_USE,
    ASTRA_FONT_PANEL_CONTROL_COUNT
};

enum {
    ASTRA_FONT_PANEL_NONE = 0u,
    ASTRA_FONT_PANEL_CHANGED,
    ASTRA_FONT_PANEL_CANCELLED,
    ASTRA_FONT_PANEL_ACCEPTED
};

/** Label and exact size for one installed bitmap strike. */
typedef struct AstraFontPanelStrikeRow {
    char label[32];
    uint16_t pixel_height;
    uint16_t reserved;
} AstraFontPanelStrikeRow;

/** Caller storage for a friendly family/style row label. */
typedef struct AstraFontPanelFaceRow {
    char label[192];
} AstraFontPanelFaceRow;

/** A Font Kit catalog snapshot remains borrowed for the panel lifetime.
 * The two List models, scroll models, and row storage are caller-owned. */
typedef struct AstraFontPanelInfo {
    uint32_t size;
    uint32_t first_id;
    AstraHandle gui;
    const AstraFontsCatalog *catalog;
    AstraFontPanelFaceRow *face_rows;
    AstraCollectionItem *face_items;
    uint32_t face_capacity;
    AstraFontPanelStrikeRow *strike_rows;
    AstraCollectionItem *strike_items;
    AstraFontPanelStrikeRow *strike_rows_spare;
    AstraCollectionItem *strike_items_spare;
    uint32_t strike_capacity;
    AstraCollectionModel *face_collection;
    AstraCollectionModel *strike_collection;
    AstraScrollModel *face_scroll;
    AstraScrollModel *strike_scroll;
    uint32_t system_only;
    uint32_t reserved[4];
} AstraFontPanelInfo;

#define ASTRA_FONT_PANEL_INFO_INIT { \
    sizeof(AstraFontPanelInfo), 0u, 0, 0, 0, 0, 0u, 0, 0, 0, 0, 0u, 0, 0, 0, 0, \
    0u, { 0u, 0u, 0u, 0u } \
}

/** Selected face/strike state; use `astra_interface_font_panel_choice` after
 * ACCEPTED. The chosen ID remains borrowed from the catalog snapshot. */
typedef struct AstraFontPanel {
    uint32_t size;
    uint32_t first_id;
    AstraHandle gui;
    const AstraFontsCatalog *catalog;
    AstraFontPanelFaceRow *face_rows;
    AstraCollectionItem *face_items;
    AstraFontPanelStrikeRow *strike_rows;
    AstraCollectionItem *strike_items;
    AstraFontPanelStrikeRow *strike_rows_spare;
    AstraCollectionItem *strike_items_spare;
    uint32_t strike_capacity;
    uint32_t strike_count;
    AstraCollectionModel *face_collection;
    AstraCollectionModel *strike_collection;
    uint32_t selected_face;
    uint32_t selected_strike;
    uint32_t system_only;
    uint32_t reserved[4];
} AstraFontPanel;

#define ASTRA_FONT_PANEL_INIT { .size = sizeof(AstraFontPanel) }

/** Compose a shared installed-font chooser from Font Kit and retained Lists,
 * ScrollViews, Labels, and Buttons. system_only disables selection of faces
 * without ASTRA_INSTALLED_FONT_SELECTABLE while keeping them visible. */
AstraResult astra_interface_font_panel_init(AstraFontPanel *panel,
    AstraControl *controls, const AstraFontPanelInfo *info);

/** Route a normal action, updating strike choices and hardware-glyph preview. */
AstraResult astra_interface_font_panel_action(AstraFontPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome);

/** Resolve the current installed face and exact strike for the caller. */
AstraResult astra_interface_font_panel_choice(const AstraFontPanel *panel,
    AstraInstalledFont *face, AstraInstalledFontStrike *strike);

ASTRA_EXTERN_C_END

#endif
