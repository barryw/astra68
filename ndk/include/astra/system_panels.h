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
    ASTRA_COLOUR_PANEL_SWATCH,
    ASTRA_COLOUR_PANEL_RED_LABEL,
    ASTRA_COLOUR_PANEL_RED,
    ASTRA_COLOUR_PANEL_GREEN_LABEL,
    ASTRA_COLOUR_PANEL_GREEN,
    ASTRA_COLOUR_PANEL_BLUE_LABEL,
    ASTRA_COLOUR_PANEL_BLUE,
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
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First control ID in panel's reserved range. */
    uint32_t format; /**< One ASTRA_COLOUR_PANEL_* format. */
    uint32_t packed; /**< Current encoded color in the selected format. */
    const AstraColorRGBA8 *palette; /**< Borrowed indexed palette, if used. */
    uint32_t palette_count; /**< Number of palette entries. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraColourPanel;

/** Empty caller-owned Colour panel state. */
#define ASTRA_COLOUR_PANEL_INIT { \
    sizeof(AstraColourPanel), 0u, 0u, 0u, 0, 0u, { 0u, 0u, 0u, 0u } \
}

/** Compose the Colour panel from ordinary Container, Label, Colour Well,
 * Slider, and Button controls. All 12 slots and IDs are caller-owned.
 * INDEX8 requires a borrowed palette of 1..256 opaque entries.
 * @param panel Empty caller-owned state initialized with ASTRA_COLOUR_PANEL_INIT.
 * @param controls Array of ASTRA_COLOUR_PANEL_CONTROL_COUNT controls.
 * @param first_id First ID reserved for panel controls.
 * @param format Requested ASTRA_COLOUR_PANEL_* encoding.
 * @param initial_packed Starting color in that encoding.
 * @param palette Borrowed indexed palette, or NULL for direct RGB.
 * @param palette_count Number of indexed entries, or zero for direct RGB.
 * @return ASTRA_OK or a validation/layout error. */
AstraResult astra_interface_colour_panel_init(AstraColourPanel *panel,
    AstraControl *controls, uint32_t first_id, uint32_t format,
    uint32_t initial_packed, const AstraColorRGBA8 *palette,
    uint32_t palette_count);

/** Apply one ordinary UI action; returns semantic panel outcome. On ACCEPTED
 * read panel->packed. A VALUE_CHANGED action refreshes the swatch.
 * @param panel Initialized panel state.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param action Normal UI action to route.
 * @param outcome Receives one ASTRA_COLOUR_PANEL_* outcome.
 * @return ASTRA_OK or a validation/update error. */
AstraResult astra_interface_colour_panel_action(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome);

/** Resolve the selected encoded color to opaque RGB for preview/use.
 * @param panel Initialized panel state.
 * @param color Receives decoded opaque color.
 * @return ASTRA_OK or a validation error. */
AstraResult astra_interface_colour_panel_color(
    const AstraColourPanel *panel, AstraColorRGBA8 *color);

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
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First control ID in panel's reserved range. */
    uint32_t mode; /**< ASTRA_FILE_PANEL_OPEN or ASTRA_FILE_PANEL_SAVE. */
    const char *directory; /**< Borrowed initial directory path. */
    const AstraDirectoryEntry *entries; /**< Borrowed directory snapshot. */
    uint32_t entry_count; /**< Count of directory entries. */
    AstraCollectionItem *item_storage; /**< Caller-owned List row storage. */
    uint32_t item_capacity; /**< Count of item storage slots. */
    AstraCollectionModel *collection; /**< Caller-owned List model. */
    AstraScrollModel *scroll; /**< Caller-owned directory scroll model. */
    AstraTextModel *name_model; /**< Caller-owned Save name model. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFilePanelInfo;

/** Empty Open/Save panel construction data. */
#define ASTRA_FILE_PANEL_INFO_INIT { \
    sizeof(AstraFilePanelInfo), 0u, 0u, 0, 0, 0u, 0, 0u, 0, 0, 0, \
    { 0u, 0u, 0u, 0u } \
}

/** Caller-owned panel state. After NAVIGATE, PICKED, or OVERWRITE, directory
 * and leaf identify the requested entry. Only the caller performs I/O. */
typedef struct AstraFilePanel {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First control ID in panel's reserved range. */
    uint32_t mode; /**< Open or Save mode. */
    const char *directory; /**< Current directory, borrowed or internal. */
    char directory_storage[2][ASTRA_VFS_PATH_MAX]; /**< Owned navigation paths. */
    uint32_t active_directory; /**< Active owned directory slot. */
    char leaf[ASTRA_VFS_NAME_MAX]; /**< Selected or entered leaf name. */
    const AstraDirectoryEntry *entries; /**< Borrowed current snapshot. */
    uint32_t entry_count; /**< Count of directory entries. */
    uint32_t item_count; /**< Number of populated collection rows. */
    AstraCollectionItem *items; /**< Caller-owned row storage. */
    uint32_t item_capacity; /**< Count of item slots. */
    AstraCollectionModel *collection; /**< Borrowed List model. */
    AstraScrollModel *scroll; /**< Borrowed scroll model. */
    AstraTextModel *name_model; /**< Borrowed Save name model. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFilePanel;

/** Empty caller-owned Open/Save panel state. */
#define ASTRA_FILE_PANEL_INIT { .size = sizeof(AstraFilePanel) }

/** Compose Open or Save from shared List, ScrollView, Field, Labels and
 * Buttons. Entries and item storage remain caller-owned until replacement.
 * @param panel Empty caller-owned state initialized with ASTRA_FILE_PANEL_INIT.
 * @param controls Array of ASTRA_FILE_PANEL_CONTROL_COUNT controls.
 * @param info Borrowed directory data and caller-owned collection storage.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_file_panel_init(AstraFilePanel *panel,
    AstraControl *controls, const AstraFilePanelInfo *info);

/** Replace the viewed directory after the caller loads a new snapshot. The
 * previous entries/items must remain alive until this function succeeds.
 * @param panel Initialized Open/Save panel.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param directory New borrowed directory path.
 * @param entries New borrowed directory snapshot.
 * @param entry_count Number of entries in snapshot.
 * @param item_storage Caller-owned row storage for new entries.
 * @param item_capacity Available row slots.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_file_panel_set_directory(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls, const char *directory,
    const AstraDirectoryEntry *entries, uint32_t entry_count,
    AstraCollectionItem *item_storage, uint32_t item_capacity);

/** Route one normal UI action. Save returns OVERWRITE for an existing regular
 * file, allowing the caller to present confirmation before creating it.
 * @param panel Initialized Open/Save panel.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param action Normal UI action to route.
 * @param outcome Receives one ASTRA_FILE_PANEL_* outcome.
 * @return ASTRA_OK or a validation/update error. */
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
    char label[32]; /**< NUL-terminated strike size label. */
    uint16_t pixel_height; /**< Exact AFNT strike height. */
    uint16_t reserved; /**< Must be zero. */
} AstraFontPanelStrikeRow;

/** Caller storage for a friendly family/style row label. */
typedef struct AstraFontPanelFaceRow {
    char label[192]; /**< NUL-terminated family and style label. */
} AstraFontPanelFaceRow;

/** A Font Kit catalog snapshot remains borrowed for the panel lifetime.
 * The two List models, scroll models, and row storage are caller-owned. */
typedef struct AstraFontPanelInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First control ID in panel's reserved range. */
    AstraHandle gui; /**< GUI handle used for glyph preview. */
    const AstraFontsCatalog *catalog; /**< Borrowed Font Kit catalog. */
    AstraFontPanelFaceRow *face_rows; /**< Caller-owned face labels. */
    AstraCollectionItem *face_items; /**< Caller-owned face List rows. */
    uint32_t face_capacity; /**< Slots in both face arrays. */
    AstraFontPanelStrikeRow *strike_rows; /**< Primary strike labels. */
    AstraCollectionItem *strike_items; /**< Primary strike List rows. */
    AstraFontPanelStrikeRow *strike_rows_spare; /**< Spare strike labels. */
    AstraCollectionItem *strike_items_spare; /**< Spare strike List rows. */
    uint32_t strike_capacity; /**< Slots in each strike array. */
    AstraCollectionModel *face_collection; /**< Caller-owned face List model. */
    AstraCollectionModel *strike_collection; /**< Caller-owned strike List model. */
    AstraScrollModel *face_scroll; /**< Caller-owned face scroll model. */
    AstraScrollModel *strike_scroll; /**< Caller-owned strike scroll model. */
    uint32_t system_only; /**< One limits selection to system-eligible faces. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFontPanelInfo;

/** Empty installed-font panel construction data. */
#define ASTRA_FONT_PANEL_INFO_INIT { \
    sizeof(AstraFontPanelInfo), 0u, 0, 0, 0, 0, 0u, 0, 0, 0, 0, 0u, 0, 0, 0, 0, \
    0u, { 0u, 0u, 0u, 0u } \
}

/** Selected face/strike state; use `astra_interface_font_panel_choice` after
 * ACCEPTED. The chosen ID remains borrowed from the catalog snapshot. */
typedef struct AstraFontPanel {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First control ID in panel's reserved range. */
    AstraHandle gui; /**< GUI handle used for glyph preview. */
    const AstraFontsCatalog *catalog; /**< Borrowed Font Kit catalog. */
    AstraFontPanelFaceRow *face_rows; /**< Borrowed face label storage. */
    AstraCollectionItem *face_items; /**< Borrowed face List rows. */
    AstraFontPanelStrikeRow *strike_rows; /**< Active strike labels. */
    AstraCollectionItem *strike_items; /**< Active strike List rows. */
    AstraFontPanelStrikeRow *strike_rows_spare; /**< Alternate strike labels. */
    AstraCollectionItem *strike_items_spare; /**< Alternate strike List rows. */
    uint32_t strike_capacity; /**< Slots in each strike array. */
    uint32_t strike_count; /**< Current face's populated strike rows. */
    AstraCollectionModel *face_collection; /**< Borrowed face List model. */
    AstraCollectionModel *strike_collection; /**< Borrowed strike List model. */
    uint32_t selected_face; /**< Selected catalog face index. */
    uint32_t selected_strike; /**< Selected strike index in current face. */
    uint32_t system_only; /**< One limits selection to system-eligible faces. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFontPanel;

/** Empty caller-owned installed-font panel state. */
#define ASTRA_FONT_PANEL_INIT { .size = sizeof(AstraFontPanel) }

/** Compose a shared installed-font chooser from Font Kit and retained Lists,
 * ScrollViews, Labels, and Buttons. system_only disables selection of faces
 * without ASTRA_INSTALLED_FONT_SELECTABLE while keeping them visible.
 * @param panel Empty caller-owned state initialized with ASTRA_FONT_PANEL_INIT.
 * @param controls Array of ASTRA_FONT_PANEL_CONTROL_COUNT controls.
 * @param info Borrowed catalog and caller-owned row/model storage.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_font_panel_init(AstraFontPanel *panel,
    AstraControl *controls, const AstraFontPanelInfo *info);

/** Route a normal action, updating strike choices and hardware-glyph preview.
 * @param panel Initialized installed-font panel.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param action Normal UI action to route.
 * @param outcome Receives one ASTRA_FONT_PANEL_* outcome.
 * @return ASTRA_OK or a validation/update error. */
AstraResult astra_interface_font_panel_action(AstraFontPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome);

/** Resolve the current installed face and exact strike for the caller.
 * @param panel Initialized installed-font panel.
 * @param face Receives selected installed face.
 * @param strike Receives selected exact pixel-height strike.
 * @return ASTRA_OK or a validation/selection error. */
AstraResult astra_interface_font_panel_choice(const AstraFontPanel *panel,
    AstraInstalledFont *face, AstraInstalledFontStrike *strike);

ASTRA_EXTERN_C_END

#endif
