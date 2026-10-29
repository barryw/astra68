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
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First control ID in panel's reserved range. */
    uint32_t format; /**< One ASTRA_COLOUR_PANEL_* format. */
    uint32_t packed; /**< Current encoded color in the selected format. */
    const AstraColorRGBA8 *palette; /**< Borrowed indexed palette, if used. */
    uint32_t palette_count; /**< Number of palette entries. */
    AstraColorRGBA8 wanted; /**< Current opaque preview color. */
    AstraTextModel hex_model; /**< Editable hexadecimal color model. */
    uint8_t hex_content[4096]; /**< Caller-owned hex text storage. */
    _Alignas(4) uint8_t hex_metadata[4096]; /**< Hex model metadata. */
    char hex_text[8]; /**< Formatted hexadecimal value. */
    char difference_text[2][24]; /**< Alternating color-difference labels. */
    char stored_text[2][32]; /**< Alternating stored-value labels. */
    uint32_t active_text; /**< Active alternating label slot. */
    AstraCollectionModel theme_model; /**< Theme color list model. */
    AstraCollectionModel recent_model; /**< Recent color list model. */
    AstraCollectionItem recent_items[2][8]; /**< Recent list row storage. */
    AstraColorRGBA8 recent_colors[2][8]; /**< Owned recent colors. */
    char recent_labels[2][8][8]; /**< Recent row label storage. */
    uint32_t recent_count; /**< Number of stored recent colors. */
    uint32_t active_recent; /**< Active recent row buffer. */
    uint32_t active_tab; /**< Selected color mode tab. */
    uint32_t hex_valid; /**< Whether entered hexadecimal color is valid. */
    uint32_t reserved[3]; /**< Must be zero. */
} AstraColourPanel;

/** Empty caller-owned Colour panel state. */
#define ASTRA_COLOUR_PANEL_INIT { .size = sizeof(AstraColourPanel) }

/** Compose the Colour panel from ordinary Container, Label, Colour Well,
 * Slider, Field, List, Segmented, and Button controls. Reserve
 * ASTRA_COLOUR_PANEL_CONTROL_COUNT consecutive slots and IDs.
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

/** Replace the panel's Recent palette, most recent first, up to eight opaque
 * colors. The panel copies the values and keeps all UI data caller-owned.
 * @param panel Initialized panel state.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param colors New recent colors in recency order.
 * @param count Number of colors, at most eight.
 * @return ASTRA_OK or a validation/update error. */
AstraResult astra_interface_colour_panel_set_recent(AstraColourPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraColorRGBA8 *colors, uint32_t count);

enum {
    ASTRA_FILE_PANEL_OPEN = 1u,
    ASTRA_FILE_PANEL_SAVE = 2u
};

/** Eight visible ancestors; deeper paths show their eight deepest segments.
 * Older ancestors remain reachable through Up. */
#define ASTRA_BREADCRUMB_VISIBLE 8u
/** Caller-owned navigation breadcrumb state composed from Buttons. */
typedef struct AstraBreadcrumb {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First breadcrumb control ID. */
    uint32_t parent_id; /**< Parent container control ID. */
    const char *path; /**< Borrowed current path. */
    uint32_t visible_count; /**< Count of visible segments. */
    uint32_t start[ASTRA_BREADCRUMB_VISIBLE]; /**< Segment start offsets. */
    uint32_t end[ASTRA_BREADCRUMB_VISIBLE]; /**< Segment end offsets. */
} AstraBreadcrumb;
/** Empty caller-owned breadcrumb state. */
#define ASTRA_BREADCRUMB_INIT { .size = sizeof(AstraBreadcrumb) }

/** Compose breadcrumb buttons for a path.
 * @param breadcrumb Empty caller-owned breadcrumb state.
 * @param controls Array of breadcrumb controls.
 * @param first_id First reserved control ID.
 * @param parent_id Parent row container ID.
 * @param path Borrowed initial path.
 * @return ASTRA_OK or a validation/layout error. */
AstraResult astra_interface_breadcrumb_init(AstraBreadcrumb *breadcrumb,
    AstraControl *controls, uint32_t first_id, uint32_t parent_id,
    const char *path);
/** Replace breadcrumb path and labels.
 * @param breadcrumb Initialized breadcrumb state.
 * @param context Owning UI context.
 * @param controls Breadcrumb control array.
 * @param path Borrowed replacement path.
 * @return ASTRA_OK or a validation/update error. */
AstraResult astra_interface_breadcrumb_set_path(AstraBreadcrumb *breadcrumb,
    AstraUIContext *context, AstraControl *controls, const char *path);
/** Resolve one breadcrumb button into its ancestor path.
 * @param breadcrumb Initialized breadcrumb state.
 * @param control_id Activated breadcrumb control ID.
 * @param target Receives null-terminated ancestor path.
 * @param capacity Target buffer capacity in bytes.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_breadcrumb_target(
    const AstraBreadcrumb *breadcrumb, uint32_t control_id,
    char *target, uint32_t capacity);

enum {
    ASTRA_FILE_PANEL_ROOT = 0u,
    ASTRA_FILE_PANEL_TITLE,
    ASTRA_FILE_PANEL_NAME_LABEL,
    ASTRA_FILE_PANEL_NAME,
    ASTRA_FILE_PANEL_LOCATION_ROW,
    ASTRA_FILE_PANEL_UP,
    ASTRA_FILE_PANEL_CRUMB0,
    ASTRA_FILE_PANEL_CRUMB1,
    ASTRA_FILE_PANEL_CRUMB2,
    ASTRA_FILE_PANEL_CRUMB3,
    ASTRA_FILE_PANEL_CRUMB4,
    ASTRA_FILE_PANEL_CRUMB5,
    ASTRA_FILE_PANEL_CRUMB6,
    ASTRA_FILE_PANEL_CRUMB7,
    ASTRA_FILE_PANEL_LOCATION,
    ASTRA_FILE_PANEL_TOOLS,
    ASTRA_FILE_PANEL_VIEW,
    ASTRA_FILE_PANEL_SEARCH,
    ASTRA_FILE_PANEL_TYPE,
    ASTRA_FILE_PANEL_CONTENT,
    ASTRA_FILE_PANEL_PLACES_SCROLL,
    ASTRA_FILE_PANEL_PLACES_LIST,
    ASTRA_FILE_PANEL_SCROLL,
    ASTRA_FILE_PANEL_LIST,
    ASTRA_FILE_PANEL_GRID_SCROLL,
    ASTRA_FILE_PANEL_GRID,
    ASTRA_FILE_PANEL_ACTIONS,
    ASTRA_FILE_PANEL_CANCEL,
    ASTRA_FILE_PANEL_PRIMARY,
    ASTRA_FILE_PANEL_CONTROL_COUNT
};

enum {
    ASTRA_FILE_PANEL_NONE = 0u,
    ASTRA_FILE_PANEL_NAVIGATE,
    ASTRA_FILE_PANEL_LOAD_CHILD,
    ASTRA_FILE_PANEL_PICKED,
    ASTRA_FILE_PANEL_OVERWRITE,
    ASTRA_FILE_PANEL_CANCELLED
};

enum {
    ASTRA_FILE_PANEL_VIEW_LIST = 0u,
    ASTRA_FILE_PANEL_VIEW_GRID = 1u,
    ASTRA_FILE_PANEL_VIEW_COLUMNS = 2u
};

/** Additional column controls follow the fixed panel block. The caller must
 * reserve this many controls and IDs when column_count is nonzero. */
#define ASTRA_FILE_PANEL_CONTROLS_FOR_COLUMNS(count) \
    (ASTRA_FILE_PANEL_CONTROL_COUNT + 1u + 2u * (count))

/** Borrowed snapshot for one visible depth of Columns. Slot zero mirrors the
 * panel's current directory; later slots are filled by set_column. Child
 * directory strings must remain stable and must not alias panel->target. */
typedef struct AstraFilePanelColumnSnapshot {
    const char *directory; /**< Borrowed directory path at this depth. */
    const AstraDirectoryEntry *entries; /**< Borrowed directory entries. */
    uint32_t entry_count; /**< Count of borrowed entries. */
    AstraCollectionItem *items; /**< Caller-owned visible rows. */
    uint32_t item_count; /**< Number of populated rows. */
    uint32_t item_capacity; /**< Capacity of caller-owned row storage. */
} AstraFilePanelColumnSnapshot;

/** One caller-owned Places/Volumes sidebar entry. */
typedef struct AstraFilePanelPlace {
    const char *label; /**< Borrowed displayed place name. */
    uint32_t label_length; /**< Label length in bytes. */
    const char *path; /**< Borrowed target path. */
} AstraFilePanelPlace;

/** A type policy supplied by the caller's file-type service. Directories
 * remain visible for navigation. The panel never infers types from suffixes.
 * @param context Caller-owned type service context.
 * @param entry Directory entry to classify.
 * @param filter_index Active type filter index.
 * @return Nonzero when this entry matches the filter. */
typedef uint32_t (*AstraFilePanelTypeAccept)(void *context,
    const AstraDirectoryEntry *entry, uint32_t filter_index);

/** Borrowed directory snapshot and caller storage. The name model is required
 * for Save and unused for Open. item_storage and spare_item_storage must each
 * hold a complete visible directory. grid_icons is parallel visible storage.
 * The caller loads entries and resolves file types via the canonical Kits. */
typedef struct AstraFilePanelInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First reserved control ID. */
    uint32_t mode; /**< ASTRA_FILE_PANEL_OPEN or ASTRA_FILE_PANEL_SAVE. */
    const char *directory; /**< Borrowed initial directory path. */
    const AstraDirectoryEntry *entries; /**< Borrowed directory snapshot. */
    uint32_t entry_count; /**< Count of directory entries. */
    AstraCollectionItem *item_storage; /**< Caller-owned visible row storage. */
    uint32_t item_capacity; /**< Count of item storage slots. */
    AstraCollectionModel *collection; /**< Caller-owned List model. */
    AstraScrollModel *scroll; /**< Caller-owned directory scroll model. */
    AstraTextModel *name_model; /**< Caller-owned Save name model. */
    AstraCollectionItem *spare_item_storage; /**< Filtering swap storage. */
    uint32_t spare_item_capacity; /**< Filtering swap storage capacity. */
    const AstraSurface **grid_icons; /**< Caller-owned visible icon slots. */
    uint32_t grid_icon_capacity; /**< Icon slot capacity. */
    const AstraFilePanelPlace *places; /**< Borrowed sidebar Places. */
    uint32_t place_count; /**< Count of Places. */
    AstraCollectionItem *place_items; /**< Caller-owned Places rows. */
    uint32_t place_capacity; /**< Places row capacity. */
    const AstraChoiceItem *type_filters; /**< Borrowed type choices. */
    uint32_t type_filter_count; /**< Number of type choices. */
    AstraFilePanelTypeAccept type_accept; /**< Caller type predicate. */
    void *type_context; /**< Context passed to type predicate. */
    AstraColumnView *columns; /**< Caller-owned column view model. */
    AstraFilePanelColumnSnapshot *column_snapshots; /**< Borrowed depths. */
    uint32_t column_count; /**< Number of column depths. */
    uint32_t control_capacity; /**< Control array capacity. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFilePanelInfo;

/** Empty Open/Save panel construction data. */
#define ASTRA_FILE_PANEL_INFO_INIT { .size = sizeof(AstraFilePanelInfo) }

/** Caller-owned panel state. After NAVIGATE, PICKED, or OVERWRITE, target is
 * the full requested path; directory and leaf retain current-location context.
 * Only the caller performs I/O. */
typedef struct AstraFilePanel {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First reserved control ID. */
    uint32_t mode; /**< Open or Save mode. */
    const char *directory; /**< Current directory, borrowed or internal. */
    char directory_storage[2][ASTRA_VFS_PATH_MAX]; /**< Owned navigation paths. */
    uint32_t active_directory; /**< Active owned path slot. */
    char leaf[ASTRA_VFS_NAME_MAX]; /**< Selected or entered leaf name. */
    char target[ASTRA_VFS_PATH_MAX]; /**< Full requested path after action. */
    AstraBreadcrumb breadcrumb; /**< Current location breadcrumb. */
    const AstraDirectoryEntry *entries; /**< Borrowed directory snapshot. */
    uint32_t entry_count; /**< Count of directory entries. */
    uint32_t item_count; /**< Number of populated rows. */
    AstraCollectionItem *items; /**< Caller-owned visible rows. */
    uint32_t item_capacity; /**< Visible row slot count. */
    AstraCollectionModel *collection; /**< Borrowed List model. */
    AstraScrollModel *scroll; /**< Borrowed List scroll model. */
    AstraTextModel *name_model; /**< Borrowed Save name model. */
    AstraCollectionItem *spare_items; /**< Caller-owned filtering storage. */
    uint32_t spare_capacity; /**< Filtering storage slot count. */
    AstraCollectionModel grid_collection; /**< Icon Grid item model. */
    AstraIconGridModel grid_model; /**< Icon Grid layout model. */
    const AstraSurface **grid_icons; /**< Caller-owned visible icon slots. */
    uint32_t grid_icon_capacity; /**< Icon slot capacity. */
    const AstraSurface *const *entry_icons; /**< Borrowed source icons. */
    AstraScrollModel grid_scroll; /**< Icon Grid scroll state. */
    AstraCollectionModel places_collection; /**< Places List model. */
    AstraScrollModel places_scroll; /**< Places List scroll state. */
    const AstraFilePanelPlace *places; /**< Borrowed sidebar Places. */
    uint32_t place_count; /**< Number of Places. */
    AstraCollectionItem *place_items; /**< Caller-owned Places rows. */
    const AstraChoiceItem *type_filters; /**< Borrowed type choices. */
    uint32_t type_filter_count; /**< Number of type choices. */
    AstraFilePanelTypeAccept type_accept; /**< Caller type predicate. */
    void *type_context; /**< Context for type predicate. */
    uint32_t selected_filter; /**< Selected type filter index. */
    uint32_t view_mode; /**< ASTRA_FILE_PANEL_VIEW_* mode. */
    AstraColumnView *columns; /**< Caller-owned Columns model. */
    AstraFilePanelColumnSnapshot *column_snapshots; /**< Column depths. */
    uint32_t column_count; /**< Number of Column depths. */
    uint32_t selected_depth; /**< Selected Columns depth. */
    AstraTextModel search_model; /**< Search Field text model. */
    uint8_t search_content[4096]; /**< Search text storage. */
    _Alignas(4) uint8_t search_metadata[4096]; /**< Search metadata. */
    char search_query[ASTRA_VFS_NAME_MAX]; /**< Current search query. */
    uint32_t search_length; /**< Query length in bytes. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFilePanel;

/** Empty caller-owned Open/Save panel state. */
#define ASTRA_FILE_PANEL_INIT { .size = sizeof(AstraFilePanel) }

/** Compose Open or Save from shared List, Icon Grid, ScrollView, Search Field,
 * Segmented, Popup, Breadcrumb Buttons, Labels and Buttons. Entries and item
 * storage remain caller-owned until replacement.
 * @param panel Empty caller-owned state initialized with ASTRA_FILE_PANEL_INIT.
 * @param controls Array of panel controls.
 * @param info Borrowed directory data and caller-owned model storage.
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
 * @param entry_count Number of snapshot entries.
 * @param item_storage Caller-owned row storage for new entries.
 * @param item_capacity Available row slots.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_file_panel_set_directory(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls, const char *directory,
    const AstraDirectoryEntry *entries, uint32_t entry_count,
    AstraCollectionItem *item_storage, uint32_t item_capacity);

/** Rebind caller-owned item and icon buffers without changing the viewed
 * directory, selection, search, view mode, or scroll offset. The new buffers
 * must be distinct and large enough for all current visible items. The old
 * buffers remain borrowed if this operation fails. After success the caller
 * may release them and use spare_item_storage for the next directory.
 * @param panel Initialized Open/Save panel.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param item_storage Replacement row storage.
 * @param item_capacity Replacement row capacity.
 * @param spare_item_storage Replacement spare row storage.
 * @param spare_item_capacity Replacement spare row capacity.
 * @param grid_icons Replacement icon pointer slots.
 * @param grid_icon_capacity Replacement icon slot capacity.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_file_panel_rebind_storage(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    AstraCollectionItem *item_storage, uint32_t item_capacity,
    AstraCollectionItem *spare_item_storage, uint32_t spare_item_capacity,
    const AstraSurface **grid_icons, uint32_t grid_icon_capacity);

/** Replace sidebar Places/Volumes after a volume lifecycle change. The
 * caller supplies a new item array while the old List still borrows it.
 * @param panel Initialized Open/Save panel.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param places Borrowed new Places array.
 * @param count Number of Places.
 * @param item_storage Caller-owned Places row storage.
 * @param item_capacity Row storage capacity.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_file_panel_set_places(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraFilePanelPlace *places, uint32_t count,
    AstraCollectionItem *item_storage, uint32_t item_capacity);

/** Attach file-type or thumbnail icon surfaces parallel to the current
 * directory snapshot. Search and type filters retain the correct mapping.
 * @param panel Initialized Open/Save panel.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param entry_icons Borrowed icon pointers parallel to directory entries.
 * @param count Number of icon pointers.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_file_panel_set_icons(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraSurface *const *entry_icons, uint32_t count);

/** Fill a child depth after the panel reports ASTRA_FILE_PANEL_LOAD_CHILD.
 * The caller owns the snapshot and item storage until replacement. Passing
 * NULL directory clears that depth and every deeper column.
 * @param panel Initialized Open/Save panel.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param depth Child column depth to replace.
 * @param directory Borrowed child path, or NULL to clear.
 * @param entries Borrowed entries for child path.
 * @param entry_count Number of child entries.
 * @param items Caller-owned child row storage.
 * @param capacity Child row capacity.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_file_panel_set_column(AstraFilePanel *panel,
    AstraUIContext *context, AstraControl *controls, uint32_t depth,
    const char *directory, const AstraDirectoryEntry *entries,
    uint32_t entry_count, AstraCollectionItem *items, uint32_t capacity);

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
    ASTRA_FONT_PANEL_FAMILY_GROUP,
    ASTRA_FONT_PANEL_FAMILY_HEADING,
    ASTRA_FONT_PANEL_FACE_SCROLL,
    ASTRA_FONT_PANEL_FACE_LIST,
    ASTRA_FONT_PANEL_STRIKE_GROUP,
    ASTRA_FONT_PANEL_STRIKE_HEADING,
    ASTRA_FONT_PANEL_STRIKE_SCROLL,
    ASTRA_FONT_PANEL_STRIKE_LIST,
    ASTRA_FONT_PANEL_ROLE_GROUP,
    ASTRA_FONT_PANEL_ROLE_HEADING,
    ASTRA_FONT_PANEL_ROLE_SCROLL,
    ASTRA_FONT_PANEL_ROLE_LIST,
    ASTRA_FONT_PANEL_PREVIEW,
    ASTRA_FONT_PANEL_DETAIL,
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
    char label[32]; /**< Formatted strike size label. */
    uint16_t pixel_height; /**< Exact installed pixel height. */
    uint16_t reserved; /**< Must be zero. */
} AstraFontPanelStrikeRow;

/** Caller storage for a friendly family/style row label. */
typedef struct AstraFontPanelFaceRow {
    char label[192]; /**< Formatted family and style label. */
} AstraFontPanelFaceRow;

/** A Font Kit catalog snapshot remains borrowed for the panel lifetime.
 * Face/strike List models, scroll models, and row storage are caller-owned.
 * The optional role collection identifies uses defined by the consumer,
 * such as Terminal or Code; preview_text is borrowed UTF-8. */
typedef struct AstraFontPanelInfo {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First reserved control ID. */
    AstraHandle gui; /**< Hardware glyph surface handle. */
    const AstraFontsCatalog *catalog; /**< Borrowed installed-font catalog. */
    AstraFontPanelFaceRow *face_rows; /**< Caller-owned face label storage. */
    AstraCollectionItem *face_items; /**< Caller-owned face List rows. */
    uint32_t face_capacity; /**< Face row capacity. */
    AstraFontPanelStrikeRow *strike_rows; /**< Caller-owned strike labels. */
    AstraCollectionItem *strike_items; /**< Caller-owned strike List rows. */
    AstraFontPanelStrikeRow *strike_rows_spare; /**< Spare strike labels. */
    AstraCollectionItem *strike_items_spare; /**< Spare strike List rows. */
    uint32_t strike_capacity; /**< Strike row capacity. */
    AstraCollectionModel *face_collection; /**< Caller-owned face List model. */
    AstraCollectionModel *strike_collection; /**< Caller-owned strike model. */
    AstraScrollModel *face_scroll; /**< Caller-owned face scroll model. */
    AstraScrollModel *strike_scroll; /**< Caller-owned strike scroll model. */
    uint32_t system_only; /**< Restrict choice to system-eligible faces. */
    const AstraCollectionItem *roles; /**< Borrowed caller-defined uses. */
    uint32_t role_count; /**< Number of caller-defined uses. */
    AstraCollectionModel *role_collection; /**< Optional role List model. */
    AstraScrollModel *role_scroll; /**< Optional role scroll model. */
    const char *preview_text; /**< Borrowed UTF-8 preview text. */
    uint32_t preview_length; /**< Preview length in bytes. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFontPanelInfo;

/** Empty installed-font chooser construction data. */
#define ASTRA_FONT_PANEL_INFO_INIT { .size = sizeof(AstraFontPanelInfo) }

/** Selected face/strike state; use `astra_interface_font_panel_choice` after
 * ACCEPTED. The chosen ID remains borrowed from the catalog snapshot. */
typedef struct AstraFontPanel {
    uint32_t size; /**< Structure size. */
    uint32_t first_id; /**< First reserved control ID. */
    AstraHandle gui; /**< Hardware glyph surface handle. */
    const AstraFontsCatalog *catalog; /**< Borrowed installed-font catalog. */
    AstraFontPanelFaceRow *face_rows; /**< Caller-owned face label storage. */
    AstraCollectionItem *face_items; /**< Caller-owned face List rows. */
    AstraFontPanelStrikeRow *strike_rows; /**< Caller-owned strike labels. */
    AstraCollectionItem *strike_items; /**< Caller-owned strike List rows. */
    AstraFontPanelStrikeRow *strike_rows_spare; /**< Spare strike labels. */
    AstraCollectionItem *strike_items_spare; /**< Spare strike List rows. */
    uint32_t strike_capacity; /**< Strike row capacity. */
    uint32_t strike_count; /**< Currently visible strike count. */
    AstraCollectionModel *face_collection; /**< Borrowed face List model. */
    AstraCollectionModel *strike_collection; /**< Borrowed strike model. */
    uint32_t selected_face; /**< Selected catalog face index. */
    uint32_t selected_strike; /**< Selected strike index in current face. */
    uint32_t system_only; /**< Restrict choice to system-eligible faces. */
    const AstraCollectionItem *roles; /**< Borrowed caller-defined uses. */
    uint32_t role_count; /**< Number of caller-defined uses. */
    AstraCollectionModel *role_collection; /**< Borrowed role List model. */
    AstraCollectionModel role_fallback_collection; /**< Empty role model. */
    AstraScrollModel role_fallback_scroll; /**< Empty role scroll state. */
    uint32_t selected_role; /**< Selected caller-defined use index. */
    const char *preview_text; /**< Borrowed UTF-8 preview text. */
    uint32_t preview_length; /**< Preview length in bytes. */
    char detail_text[2][96]; /**< Alternating font detail labels. */
    uint32_t active_detail; /**< Active detail label slot. */
    uint32_t reserved[4]; /**< Must be zero. */
} AstraFontPanel;

/** Empty caller-owned installed-font chooser state. */
#define ASTRA_FONT_PANEL_INIT { .size = sizeof(AstraFontPanel) }

/** Compose a shared installed-font chooser from Font Kit and retained Lists,
 * ScrollViews, Labels, and Buttons. Three labeled columns show Family,
 * installed Strike, and optional caller-defined Role. system_only disables
 * selection of faces without ASTRA_INSTALLED_FONT_SELECTABLE while keeping
 * them visible.
 * @param panel Empty caller-owned state initialized with ASTRA_FONT_PANEL_INIT.
 * @param controls Array of ASTRA_FONT_PANEL_CONTROL_COUNT controls.
 * @param info Borrowed catalog and caller-owned row/model storage.
 * @return ASTRA_OK or a validation/capacity error. */
AstraResult astra_interface_font_panel_init(AstraFontPanel *panel,
    AstraControl *controls, const AstraFontPanelInfo *info);

/** Route a normal action, updating strike choices and hardware-glyph preview.
 * @param panel Initialized installed-font chooser.
 * @param context Owning UI context.
 * @param controls Panel's retained control array.
 * @param action Normal UI action to route.
 * @param outcome Receives one ASTRA_FONT_PANEL_* outcome.
 * @return ASTRA_OK or a validation/update error. */
AstraResult astra_interface_font_panel_action(AstraFontPanel *panel,
    AstraUIContext *context, AstraControl *controls,
    const AstraUIAction *action, uint32_t *outcome);

/** Resolve the current installed face and exact strike for the caller.
 * @param panel Initialized installed-font chooser.
 * @param face Receives selected installed face.
 * @param strike Receives selected exact pixel-height strike.
 * @return ASTRA_OK or a validation/selection error. */
AstraResult astra_interface_font_panel_choice(const AstraFontPanel *panel,
    AstraInstalledFont *face, AstraInstalledFontStrike *strike);

/** Return the selected caller-defined use role, or NOT_PRESENT when no role
 * column was supplied.
 * @param panel Initialized installed-font chooser.
 * @param role_id Receives selected caller-defined use ID.
 * @return ASTRA_OK or ASTRA_ERROR_NOT_PRESENT when no role exists. */
AstraResult astra_interface_font_panel_role(const AstraFontPanel *panel,
    uint64_t *role_id);

ASTRA_EXTERN_C_END

#endif
