#ifndef ASTRA_VOLUME_H
#define ASTRA_VOLUME_H

/** @file volume.h @brief Native volume identities and catalog records. */
#include <stdint.h>
#include <stddef.h>
#include <astra/volume_abi.h>
#include <astra/resource.h>

/** Metadata-only capability. It never conveys volume data or mount authority. */
#define ASTRA_CAPABILITY_VOLUMES "VOLUMES"
/** Read authority for public mounted volumes; excludes system/private roots. */
#define ASTRA_CAPABILITY_VOLUME_READ "VOLUME_READ"
/** Read/write authority for public mounted volumes; excludes system/private roots. */
#define ASTRA_CAPABILITY_VOLUME_WRITE "VOLUME_WRITE"
/** Trusted lifecycle authority; independent of metadata and volume data. */
#define ASTRA_CAPABILITY_VOLUME_ADMIN "VOLUME_ADMIN"
/** Destructive physical-disk formatting authority, granted only to Disk Setup. */
#define ASTRA_CAPABILITY_DISK_ADMIN "DISK_ADMIN"
/** Trusted usage inspection; exposes exact resource holders, not file data. */
#define ASTRA_CAPABILITY_VOLUME_USAGE "VOLUME_USAGE"
/** Source-scoped configured RAM authority; survives mount reincarnation. */
#define ASTRA_CAPABILITY_RAM_READ "RAM_READ"
/** Source-scoped configured RAM read/write authority; survives reincarnation. */
#define ASTRA_CAPABILITY_RAM_WRITE "RAM_WRITE"
struct AstraFile;

/** Owned complete catalog. Publish it only after every page has matched one
 * epoch and sequence. Records are metadata, never data or mount authority. */
typedef struct AstraVolumeSnapshot {
    uint64_t epoch; /**< Provider incarnation. */
    uint64_t sequence; /**< Coherent catalog sequence. */
    AstraVolumeRecord *records; /**< Owned array; do not free or resize directly. */
    uint32_t count; /**< Initialized records; no truncated tail. */
} AstraVolumeSnapshot;
/** Empty complete-catalog initializer. */
#define ASTRA_VOLUME_SNAPSHOT_INIT { 0u, 0u, NULL, 0u }

/** Parse one exact catalog reference from launch arguments. Invalid input
 * leaves the caller's reference unchanged, including overflow and zero IDs.
 * @param epoch Decimal provider epoch.
 * @param id Decimal object identity.
 * @param generation Decimal object generation.
 * @param reference Receives parsed identity only on success.
 * @return One on success, zero on invalid input.
 */
static inline int astra_volume_reference_parse(const char *epoch,
    const char *id, const char *generation, AstraVolumeReference *reference)
{
    const char *parts[3] = {epoch, id, generation};
    uint64_t values[3] = {0u, 0u, 0u};
    if (reference == NULL) return 0;
    for (uint32_t at = 0u; at < 3u; ++at) {
        const char *part = parts[at];
        if (part == NULL || *part == '\0') return 0;
        for (; *part != '\0'; ++part) {
            if (*part < '0' || *part > '9' ||
                values[at] > (UINT64_MAX - (uint32_t)(*part - '0')) / 10u)
                return 0;
            values[at] = values[at] * 10u + (uint32_t)(*part - '0');
        }
        if (values[at] == 0u) return 0;
    }
    reference->epoch = values[0];
    reference->id = values[1];
    reference->generation = values[2];
    return 1;
}

/** Stable user-facing format name; NDOS is a source issue, not a format.
 * @param format AstraVolumeFormat value.
 * @return Static name, or question mark for an unknown value.
 */
static inline const char *astra_volume_format_name(uint32_t format)
{
    static const char *const names[] = {
        "unknown", "ext4", "FAT32", "exFAT", "ISO9660", "UDF",
        "tmpfs", "FAT12", "FAT16", "NTFS", "unsupported"
    };
    return format < sizeof(names) / sizeof(names[0]) ? names[format] : "?";
}

/** Stable user-facing partition-map name.
 * @param map AstraVolumePartitionMap value.
 * @return Static name, or question mark for an unknown value.
 */
static inline const char *astra_volume_partition_map_name(uint32_t map)
{
    static const char *const names[] = {"-", "MBR", "GPT", "other"};
    return map < sizeof(names) / sizeof(names[0]) ? names[map] : "?";
}

/** Why an attached physical source cannot be opened as a mounted volume. */
enum AstraVolumeSourceIssue {
    ASTRA_VOLUME_SOURCE_READY = 0u,
    ASTRA_VOLUME_SOURCE_NDOS,
    ASTRA_VOLUME_SOURCE_UNSUPPORTED,
    ASTRA_VOLUME_SOURCE_AMBIGUOUS,
    ASTRA_VOLUME_SOURCE_PROBE_ERROR
};

/** Shared source presentation policy for desktop, Disk Setup and tools.
 * Empty optical drives are device rows and do not qualify as source icons.
 * @param record Catalog record to classify; NULL is accepted.
 * @return AstraVolumeSourceIssue value.
 */
static inline uint32_t astra_volume_source_issue(const AstraVolumeRecord *record)
{
    if (record == NULL || record->kind != ASTRA_VOLUME_SOURCE ||
        record->origin != ASTRA_VOLUME_ORIGIN_DEVICE ||
        (record->state != ASTRA_VOLUME_UNMOUNTED && record->state != ASTRA_VOLUME_FAILED))
        return ASTRA_VOLUME_SOURCE_READY;
    if ((record->flags & ASTRA_VOLUME_AMBIGUOUS_FORMAT) != 0u)
        return ASTRA_VOLUME_SOURCE_AMBIGUOUS;
    if (record->status != 0u || record->state == ASTRA_VOLUME_FAILED)
        return ASTRA_VOLUME_SOURCE_PROBE_ERROR;
    if (record->format == ASTRA_VOLUME_UNSUPPORTED)
        return ASTRA_VOLUME_SOURCE_UNSUPPORTED;
    return record->format == ASTRA_VOLUME_FORMAT_UNKNOWN ?
        ASTRA_VOLUME_SOURCE_NDOS : ASTRA_VOLUME_SOURCE_READY;
}

/** States of one admitted unmount and its optional retained image source. */
enum AstraVolumeRemovalState {
    ASTRA_VOLUME_REMOVAL_PENDING = 0u,
    ASTRA_VOLUME_REMOVAL_COMPLETE,
    ASTRA_VOLUME_REMOVAL_REFUSED,
    ASTRA_VOLUME_REMOVAL_RETAINED,
    ASTRA_VOLUME_REMOVAL_PROVIDER_RETIRED
};

/** Progress record retained between unmount observations. */
typedef struct AstraVolumeRemoval {
    AstraVolumeReference mount; /**< Exact admitted mount. */
    AstraVolumeReference image_source; /**< Optional retained source; all zero otherwise. */
    uint32_t source_admitted; /**< Source release admitted; wait for actual disappearance. */
    uint32_t state; /**< AstraVolumeRemovalState after observation. */
    uint32_t status; /**< Provider status, or LOST if RETAINED has no status. */
} AstraVolumeRemoval;

/** Owned complete resource snapshot. Failed collection leaves it empty. */
typedef struct AstraVolumeUsageSnapshot {
    AstraVolumeReference volume; /**< Exact filter, ID zero for every mount. */
    uint64_t sequence; /**< Native resource generation. */
    uint64_t catalog_sequence; /**< Provider graph generation. */
    AstraVolumeUsageRecord *records; /**< Owned complete resource array. */
    uint32_t count; /**< Initialized rows; no truncated tail. */
} AstraVolumeUsageSnapshot;
/** Empty complete-usage-snapshot initializer. */
#define ASTRA_VOLUME_USAGE_SNAPSHOT_INIT { {0u, 0u, 0u}, 0u, 0u, NULL, 0u }

/** Owned subscription. Closing its receive endpoint retires it at the service. */
typedef struct AstraVolumeWatch {
    uint64_t id; /**< Exact subscription identity; never reused by the service. */
    AstraHandle receive; /**< Waitable notification receive endpoint. */
    uint32_t reserved; /**< Zero. */
} AstraVolumeWatch;

/** Empty subscription initializer. */
#define ASTRA_VOLUME_WATCH_INIT { 0u, ASTRA_INVALID_HANDLE, 0u }

ASTRA_EXTERN_C_BEGIN
/** Inspect server-owned root-broker authority. No data grant is acquired.
 * @param broker VOLUME_READ/WRITE or RAM_READ/WRITE capability.
 * @param info Cleared on failure; exact allowed source and read/write rights.
 * @return ASTRA_RESULT_*; metadata/admin/usage endpoints refuse this operation.
 */
ASTRA_NODISCARD AstraResult astra_volume_broker_info(AstraHandle broker,
    AstraVolumeBrokerInfo *info);
/** Read one coherent resource page. Metadata/data capabilities refuse this.
 * Shared directory roots include each authenticated native-session holder;
 * cursor order is kind, full handle and holder PID.
 * @param service VOLUME_USAGE capability. @param query Exact filter/cursor.
 * @param page Receives validated complete rows, cleared on error.
 * @return ASTRA_OK; BUSY requires restarting after resource mutation;
 * retired/unknown exact identities return INVALID_HANDLE. Lost objects still
 * retained by the native substrate remain inspectable under their old identity.
 */
ASTRA_NODISCARD AstraResult astra_volume_usage(AstraHandle service,
    const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page);
/** Collect every usage page, publishing no partial result on failure.
 * @param service VOLUME_USAGE capability. @param volume Exact mount, all-zero filter or NULL for every mount.
 * @param snapshot Empty owned output. @return ASTRA_OK or transport/resource/BUSY error.
 */
ASTRA_NODISCARD AstraResult astra_volume_usage_snapshot_open(AstraHandle service,
    const AstraVolumeReference *volume, AstraVolumeUsageSnapshot *snapshot);
/** Release complete usage rows and reset ownership. @param snapshot Owned snapshot or NULL. */
void astra_volume_usage_snapshot_close(AstraVolumeUsageSnapshot *snapshot);
/** Admit a native mount or safely unmount an exact volume incarnation.
 * Unmount refuses retained files, directory roots, shares, active operations
 * and image dependencies; idle whole-volume grants do not pin storage.
 * @param administrator VOLUME_ADMIN capability.
 * @param control MOUNT_SOURCE or UNMOUNT; image_share must be zero.
 * @param reference Receives the admitted object's exact identity, cleared on failure.
 * @return ASTRA_OK means admission, not completed mount/unmount. Observe catalog.
 */
ASTRA_NODISCARD AstraResult astra_volume_control(AstraHandle administrator,
    const AstraVolumeControl *control, AstraVolumeReference *reference);
/** Start FAT32 or exFAT format of one exact, unmounted physical source.
 * The request is asynchronous; poll with astra_volume_format_status until
 * the result is no longer ASTRA_ERROR_BUSY. Formatting invalidates the old
 * source generation and triggers a fresh probe, even on worker failure.
 * @param administrator DISK_ADMIN capability.
 * @param source Exact physical source selected from a coherent catalog.
 * @param format ASTRA_VOLUME_FAT32 or ASTRA_VOLUME_EXFAT.
 * @param label Zero-terminated ASCII label of at most 11 characters.
 */
ASTRA_NODISCARD AstraResult astra_volume_format(AstraHandle administrator,
    const AstraVolumeReference *source, uint32_t format, const char *label);
/** Poll one admitted format using the generation passed to astra_volume_format.
 * @param outcome Receives ASTRA_ERROR_BUSY while running, ASTRA_OK on success,
 * or the mapped failure. Cleared on query failure.
 */
ASTRA_NODISCARD AstraResult astra_volume_format_status(AstraHandle administrator,
    const AstraVolumeReference *source, AstraResult *outcome);
/** Admit an exact physical disk-map edit. All mounted descendants must be
 * unmounted first. Completion retires old partition sources and rereads the
 * kernel map before publishing a new device generation. */
ASTRA_NODISCARD AstraResult astra_volume_partition(AstraHandle administrator,
    const AstraVolumeReference *device, const AstraVolumePartitionEdit *edit);
/** Poll an admitted edit using the device generation passed at admission. */
ASTRA_NODISCARD AstraResult astra_volume_partition_status(AstraHandle administrator,
    const AstraVolumeReference *device, AstraResult *outcome);
/** Find first MiB-aligned free extent on one current physical device, keeping
 * one MiB clear at each edge for disk-map metadata. */
ASTRA_NODISCARD AstraResult astra_volume_partition_find_free(
    const AstraVolumeSnapshot *snapshot, const AstraVolumeReference *device,
    uint64_t length_bytes, uint64_t *offset_bytes);
/** Admit an image from an already-open native readonly regular file.
 * Provider retains the actual description and backing-volume dependency.
 * @param administrator VOLUME_ADMIN capability.
 * @param file Open readonly native file; no host pathname is used.
 * @param reference Receives image source identity; observe probing in catalog.
 * @return ASTRA_RESULT_*; writable descriptions and conflicting writers refuse.
 */
ASTRA_NODISCARD AstraResult astra_volume_image(AstraHandle administrator,
    struct AstraFile *file, AstraVolumeReference *reference);
/** Read a coherent catalog page without acquiring data authority.
 * @param service VOLUMES send capability. @param query Valid keyset query.
 * @param page Receives a validated page, cleared on failure.
 * @return ASTRA_OK; BUSY requires restarting pagination; INVALID_HANDLE means
 * the requested provider epoch retired. Other errors report transport/resources.
 */
ASTRA_NODISCARD AstraResult astra_volume_catalog(AstraHandle service,
    const AstraVolumeCatalogQuery *query, AstraVolumeCatalogPage *page);
/** Collect all pages of one coherent catalog into an empty owned snapshot.
 * @param service Metadata capability. @param snapshot Empty output owner.
 * @return ASTRA_OK, BUSY if the catalog changed, or transport/resource error.
 * Failure leaves the output empty; callers decide when to retry.
 */
ASTRA_NODISCARD AstraResult astra_volume_snapshot_open(AstraHandle service,
    AstraVolumeSnapshot *snapshot);
/** Advance one admitted unmount against a coherent catalog. May admit release
 * of its image source only after mount retirement; completion requires source
 * disappearance. LOST alone never proves backing release. The caller chooses
 * when to observe again and how to present REFUSED/RETAINED/provider loss.
 * @param administrator VOLUME_ADMIN capability. @param snapshot Complete catalog.
 * @param removal Exact mount and optional source, retained between observations.
 * @return ASTRA_OK with updated state/status, or control/argument error.
 */
ASTRA_NODISCARD AstraResult astra_volume_removal_advance(AstraHandle administrator,
    const AstraVolumeSnapshot *snapshot, AstraVolumeRemoval *removal);
/** Release a complete catalog and reset its owner.
 * @param snapshot Owned snapshot; NULL is ignored.
 */
void astra_volume_snapshot_close(AstraVolumeSnapshot *snapshot);
/** Find one exact record in a complete snapshot. Stale generation/epoch
 * returns NULL; callers must never reselect by name or device number.
 * @param snapshot Complete catalog snapshot.
 * @param reference Exact epoch, identity and generation.
 * @return Matching record, or NULL when absent or stale.
 */
const AstraVolumeRecord *astra_volume_snapshot_find(
    const AstraVolumeSnapshot *snapshot, const AstraVolumeReference *reference);
/** Walk exact parent incarnations to an ancestor kind (including the target).
 * Broken, replaced or cyclic graphs return NULL. Useful for grouping mounted
 * partitions under their physical device without reparsing Linux paths.
 * @param snapshot Complete catalog snapshot.
 * @param reference Exact starting record.
 * @param kind Requested AstraVolumeKind.
 * @return Matching ancestor, or NULL when absent or inconsistent.
 */
const AstraVolumeRecord *astra_volume_snapshot_ancestor(
    const AstraVolumeSnapshot *snapshot, const AstraVolumeReference *reference,
    uint32_t kind);
/** Acquire a rooted VFS service grant for an exact mounted-volume incarnation.
 * Metadata capability alone refuses this operation. System/private roots keep
 * their explicit startup grants. Global brokers return nondelegable grants;
 * scoped RAM brokers allow canonical subtree derivation and delegation.
 * @param broker VOLUME_READ/WRITE or RAM_READ/WRITE capability.
 * @param reference Exact epoch/id/generation obtained from the catalog.
 * @param rights ASTRA_RIGHT_READ, optionally ASTRA_RIGHT_WRITE.
 * @param root Receives an owned VFS service send capability; close after use.
 * @return ASTRA_RESULT_*; stale/lost references cannot select replacement media.
 */
ASTRA_NODISCARD AstraResult astra_volume_root(AstraHandle broker,
    const AstraVolumeReference *reference, uint32_t rights, AstraHandle *root);
/** Subscribe before reading a snapshot, so changes cannot be missed.
 * @param service VOLUMES send capability. @param watch Empty owned subscription.
 * @return ASTRA_RESULT_*; notifications are coalesced, not an event history.
 */
ASTRA_NODISCARD AstraResult astra_volume_watch_open(AstraHandle service,
    AstraVolumeWatch *watch);
/** Try to receive one invalidation; never blocks.
 * @param watch Owned subscription. @param change Receives current observation.
 * @return ASTRA_OK, WOULD_BLOCK, PEER_DEAD or malformed-message/resource error.
 */
ASTRA_NODISCARD AstraResult astra_volume_watch_receive(const AstraVolumeWatch *watch,
    AstraVolumeChange *change);
/** Retire a subscription and close its endpoint, even if the service is lost.
 * @param service Original VOLUMES capability. @param watch Owned subscription.
 * @return First error, after attempting local cleanup.
 */
ASTRA_NODISCARD AstraResult astra_volume_watch_close(AstraHandle service,
    AstraVolumeWatch *watch);
ASTRA_EXTERN_C_END


#endif
