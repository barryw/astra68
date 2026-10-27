#ifndef ASTRA_PACKAGE_LIBRARY_H
#define ASTRA_PACKAGE_LIBRARY_H

/** @file package_library.h @brief Native Astra package archive operations. */

#include <astra/filesystem_library.h>
#include <astra/types.h>

/** Logical package library name. */
#define ASTRA_PACKAGE_LIBRARY_NAME "package.library"
/** Current package library ABI major. */
#define ASTRA_PACKAGE_LIBRARY_VERSION 1u

/** Build a version-1 `.apkg` from one validated application bundle.
 * The archive is published only after its complete, durable write. Existing
 * destinations are never replaced. `filesystem` needs read access to the
 * bundle and write access to the archive's parent directory.
 * @param filesystem Filesystem Kit client with required grants.
 * @param bundle Absolute source application bundle path.
 * @param archive Absolute output package path.
 * @return ASTRA_OK or a result describing the failure.
 */
AstraResult astra_package_create_bundle(AstraFilesystem *filesystem,
    const char *bundle, const char *archive);

/** Validate and install one application package into `apps_directory`.
 * Caller must hold write authority to that directory. The complete bundle
 * is staged, verified against its ledger, then published as one directory.
 * Existing bundles are left untouched.
 * @param filesystem Filesystem Kit client with required grants.
 * @param archive Absolute source package path.
 * @param apps_directory Destination applications directory.
 * @return ASTRA_OK or a result describing the failure.
 */
AstraResult astra_package_install_bundle(AstraFilesystem *filesystem,
    const char *archive, const char *apps_directory);

/** Read the bounded application preview stored in a package. Caller releases
 * `*bytes` with `astra_runtime_deallocate`. Preview is display data only;
 * installation validates the complete archive and payload independently.
 * @param filesystem Filesystem Kit client with source read access.
 * @param archive Absolute source package path.
 * @param bytes Receives heap-owned icon data.
 * @param length Receives icon byte count.
 * @return ASTRA_OK or a result describing the failure.
 */
AstraResult astra_package_read_icon(AstraFilesystem *filesystem,
    const char *archive, void **bytes, uint32_t *length);

#endif
