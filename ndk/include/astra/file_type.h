#ifndef ASTRA_FILE_TYPE_H
#define ASTRA_FILE_TYPE_H

/** @file file_type.h @brief Content type identification for open documents. */

#include <astra/application_catalog.h>

/** Z-machine story content type; version is an attribute of content. */
#define ASTRA_FILE_TYPE_ZMACHINE "application/x-zmachine-story"
/** Glulx story content type; version comes from its header. */
#define ASTRA_FILE_TYPE_GLULX "application/x-glulx-story"
/** IFF Blorb resource container content type. */
#define ASTRA_FILE_TYPE_BLORB "application/x-blorb"
/** Astra application package content type. */
#define ASTRA_FILE_TYPE_PACKAGE "application/x-astra-package"
/** Maximum number of bytes in a content type, including NUL. */
#define ASTRA_FILE_TYPE_MAX ASTRA_BUNDLE_CONTENT_TYPE_MAX

enum {
    ASTRA_FILE_TYPE_UNKNOWN = 0u,
    ASTRA_FILE_TYPE_CONTENT = 1u,
    ASTRA_FILE_TYPE_EXTENSION_HINT = 2u
};

/** Resolved content type and provenance for one file. */
typedef struct AstraFileTypeInfo {
    char type[ASTRA_FILE_TYPE_MAX]; /**< Empty if unknown. */
    uint32_t source; /**< ASTRA_FILE_TYPE_*. */
    uint32_t story_version; /**< 1–8 for a Z-machine story, else zero. */
} AstraFileTypeInfo;

/** Empty identification record. */
#define ASTRA_FILE_TYPE_INFO_INIT {{0}, ASTRA_FILE_TYPE_UNKNOWN, 0u}

/** Inspect one file via Filesystem Kit. No file offset is changed, and no
 * metadata is written. On unknown content, returns OK with an empty type.
 * A filename extension is only a hint for formats lacking content recognition.
 * @param filesystem Open Filesystem Kit client.
 * @param path Absolute file path visible to that client.
 * @param info Receives type, source, and optional story version.
 * @return ASTRA_VFS_OK or a VFS status; unknown content returns OK. */
uint32_t astra_file_type_identify(AstraFilesystem *filesystem,
                                  const char *path, AstraFileTypeInfo *info);

/** Identify content, then use installed bundle extension declarations only
 * if content remains unknown. Conflicting declarations leave type unknown.
 * No file metadata is written. The catalog is a snapshot; each manifest is
 * checked again, so removal cannot yield a stale handler.
 * @param filesystem Open process filesystem binding.
 * @param catalog Installed application snapshot.
 * @param path Absolute file path visible to filesystem.
 * @param info Receives type, source, and optional story version.
 * @return ASTRA_VFS_OK or a VFS status. */
uint32_t astra_file_type_resolve(AstraProcessFilesystem *filesystem,
    const AstraApplicationCatalog *catalog, const char *path,
    AstraFileTypeInfo *info);

#endif
