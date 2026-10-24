#ifndef ASTRA_DOS_LIBRARY_H
#define ASTRA_DOS_LIBRARY_H

/** @file dos_library.h @brief Native disk inventory and administration. */
#include <astra/volume.h>

/** Dynamic library name. */
#define ASTRA_DOS_LIBRARY_NAME "dos.library"
/** Current major ABI. */
#define ASTRA_DOS_LIBRARY_ABI_MAJOR 1u
/** Current minor ABI. */
#define ASTRA_DOS_LIBRARY_ABI_MINOR 0u

/** One coherent catalog and resource view. Close after use. */
typedef struct AstraDosDiskSnapshot {
    AstraVolumeSnapshot volumes; /**< Owned catalog. */
    AstraVolumeUsageSnapshot usage; /**< Owned resource holders. */
} AstraDosDiskSnapshot;
/** Empty snapshot initializer. */
#define ASTRA_DOS_DISK_SNAPSHOT_INIT { ASTRA_VOLUME_SNAPSHOT_INIT, ASTRA_VOLUME_USAGE_SNAPSHOT_INIT }

/** Disk operations shared by the native GUI and CLI. */
enum AstraDosDiskOperation {
    ASTRA_DOS_DISK_INITIALIZE_MBR = 1u,
    ASTRA_DOS_DISK_INITIALIZE_GPT,
    ASTRA_DOS_DISK_CREATE_FAT32,
    ASTRA_DOS_DISK_CREATE_EXFAT,
    ASTRA_DOS_DISK_DELETE_PARTITION,
    ASTRA_DOS_DISK_FORMAT_FAT32,
    ASTRA_DOS_DISK_FORMAT_EXFAT
};

/** Target is exact catalog identity. Create size is MiB-aligned bytes;
 * format label is NUL-terminated and follows provider format policy. */
typedef struct AstraDosDiskRequest {
    AstraVolumeReference target; /**< Exact catalog incarnation. */
    uint32_t operation; /**< AstraDosDiskOperation. */
    uint64_t size_bytes; /**< MiB-aligned partition length for create. */
    const char *label; /**< Format label; unused by other operations. */
} AstraDosDiskRequest;

/** Returned only after admission; retain for asynchronous status polling. */
typedef struct AstraDosDiskJob {
    AstraVolumeReference reference; /**< Admitted device or source incarnation. */
    uint32_t operation; /**< AstraDosDiskOperation submitted. */
} AstraDosDiskJob;

ASTRA_EXTERN_C_BEGIN
/** Open matching catalog and usage snapshots into an empty snapshot;
 * BUSY means retry from start.
 * @param metadata VOLUMES capability.
 * @param usage VOLUME_USAGE capability.
 * @param snapshot Empty destination; owns both snapshots on success.
 * @return ASTRA_OK or error; on failure destination remains empty.
 */
ASTRA_NODISCARD AstraResult astra_dos_disk_snapshot_open(AstraHandle metadata,
    AstraHandle usage, AstraDosDiskSnapshot *snapshot);
/** Release both owned snapshots and reset their contents.
 * @param snapshot Snapshot to close, or NULL.
 */
void astra_dos_disk_snapshot_close(AstraDosDiskSnapshot *snapshot);
/** Check current selection, protection, mounts and free extent without edits.
 * Provider repeats admission checks against exact generation at submit time.
 * @param snapshot Coherent catalog and usage snapshot.
 * @param request Exact edit to check.
 * @return ASTRA_OK when edit is admissible in this snapshot, or error.
 */
ASTRA_NODISCARD AstraResult astra_dos_disk_check(
    const AstraDosDiskSnapshot *snapshot, const AstraDosDiskRequest *request);
/** Admit one edit. No operation is started on error; job is cleared.
 * @param administrator DISK_ADMIN capability.
 * @param snapshot Coherent catalog and usage snapshot.
 * @param request Exact edit to admit.
 * @param job Receives admitted asynchronous job.
 * @return ASTRA_OK on admission, or error before edit starts.
 */
ASTRA_NODISCARD AstraResult astra_dos_disk_submit(AstraHandle administrator,
    const AstraDosDiskSnapshot *snapshot, const AstraDosDiskRequest *request,
    AstraDosDiskJob *job);
/** Poll admitted job. outcome is BUSY until terminal result.
 * @param administrator DISK_ADMIN capability.
 * @param job Job returned by astra_dos_disk_submit.
 * @param outcome Receives worker outcome; BUSY while running.
 * @return ASTRA_OK on a valid query, or query error.
 */
ASTRA_NODISCARD AstraResult astra_dos_disk_status(AstraHandle administrator,
    const AstraDosDiskJob *job, AstraResult *outcome);
ASTRA_EXTERN_C_END

#endif
