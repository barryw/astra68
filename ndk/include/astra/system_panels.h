#ifndef ASTRA_SYSTEM_PANELS_H
#define ASTRA_SYSTEM_PANELS_H

/** @file system_panels.h @brief Shared, retained system panels. */

#include <astra/control.h>
#include <astra/filesystem_library.h>

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
    uint32_t size;
    uint32_t first_id;
    uint32_t format;
    uint32_t packed;
    const AstraColorRGBA8 *palette;
    uint32_t palette_count;
    uint32_t reserved[4];
} AstraColourPanel;

#define ASTRA_COLOUR_PANEL_INIT { \
    sizeof(AstraColourPanel), 0u, 0u, 0u, 0, 0u, { 0u, 0u, 0u, 0u } \
}

/** Compose the Colour panel from ordinary Container, Label, Colour Well,
 * Slider, and Button controls. All 12 slots and IDs are caller-owned.
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
    char directory[ASTRA_VFS_PATH_MAX];
    char leaf[ASTRA_VFS_NAME_MAX];
    const AstraDirectoryEntry *entries;
    uint32_t entry_count;
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

ASTRA_EXTERN_C_END

#endif
