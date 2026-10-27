#ifndef ASTRA_APPLICATION_CATALOG_H
#define ASTRA_APPLICATION_CATALOG_H

/**
 * @file application_catalog.h
 * @brief The installed applications, read from their bundles.
 *
 * One scan of an applications directory (normally `/apps`, which needs
 * `APPS:r`) yields every well-formed application bundle in display-name
 * order. Each manifest stays authoritative; the catalog is a snapshot of
 * them, so a desktop, a launcher and a shell all agree on what is installed.
 * A bundle whose manifest is missing, malformed or not an application is
 * counted in `skipped` and left out: one broken bundle never hides the rest.
 */

#include <astra/application_service.h>
#include <astra/bundle.h>
#include <astra/vfs_process.h>

#include <stdint.h>

/** One installed application. */
typedef struct AstraApplicationEntry {
    char *bundle; /**< Owned path, e.g. "/apps/Terminal.app". */
    char *icon; /**< Owned path to the bundle's AICON. */
    char id[ASTRA_BUNDLE_ID_MAX]; /**< Reverse-domain identifier. */
    char name[ASTRA_BUNDLE_NAME_MAX]; /**< UTF-8 display name. */
    AstraBundleVersion version; /**< Bundle version. */
} AstraApplicationEntry;

/** A catalog snapshot. Initialize with ASTRA_APPLICATION_CATALOG_INIT and
 * release with astra_application_catalog_destroy(). */
typedef struct AstraApplicationCatalog {
    AstraApplicationEntry *entries; /**< Heap-owned, `count` used. */
    uint32_t count; /**< Applications found. */
    uint32_t skipped; /**< Bundles left out as unreadable or invalid. */
    uint32_t capacity; /**< Private allocation capacity. */
} AstraApplicationCatalog;

/** Empty catalog initializer. */
#define ASTRA_APPLICATION_CATALOG_INIT { 0, 0, 0, 0 }

/**
 * Scan @p directory for `*.app` bundles.
 * @param filesystem Open process filesystem binding.
 * @param directory Applications directory, e.g. "/apps".
 * @param catalog Initialized catalog; previous contents are released.
 * @return ASTRA_VFS_OK, or the status that stopped the directory scan.
 *         Individual bad bundles do not fail the scan.
 */
uint32_t astra_application_catalog_load(AstraProcessFilesystem *filesystem,
                                        const char *directory,
                                        AstraApplicationCatalog *catalog);

/** Query a bundle's current manifest for a declared content type. A valid
 * manifest returns OK with `supports` set to zero or one; malformed bundles
 * return INVALID. The bundle path may name any mounted volume.
 * @param filesystem Open process filesystem.
 * @param bundle Absolute bundle path.
 * @param content_type MIME-style content type.
 * @param supports Receives zero or one.
 * @return ASTRA_VFS_OK or a VFS status. */
uint32_t astra_application_supports_type(AstraProcessFilesystem *filesystem,
                                         const char *bundle,
                                         const char *content_type,
                                         uint32_t *supports);

/** Return indices of currently installed applications declaring `content_type`.
 * `count` receives total matches even when `capacity` is shorter; a short
 * output buffer returns LIMIT without writing beyond it.
 * @param filesystem Open process filesystem.
 * @param catalog Application catalog snapshot.
 * @param content_type MIME-style content type.
 * @param indices Output array of matching catalog indices.
 * @param capacity Number of elements available in indices.
 * @param count Receives total matching application count.
 * @return ASTRA_VFS_OK or a VFS status. */
uint32_t astra_application_catalog_handlers(
    AstraProcessFilesystem *filesystem, const AstraApplicationCatalog *catalog,
    const char *content_type, uint32_t *indices, uint32_t capacity,
    uint32_t *count);

/** Release a catalog's storage and reset it to empty.
 * @param catalog Catalog to release; NULL is ignored.
 */
void astra_application_catalog_destroy(AstraApplicationCatalog *catalog);

#endif
