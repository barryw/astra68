#ifndef ASTRA_STORAGE_WORK_H
#define ASTRA_STORAGE_WORK_H

#include "astra_storage_mount.h"
#include "astra_storage_path.h"

enum AstraStorageWorkOperation {
    ASTRA_STORAGE_WORK_ATTACH = 1,
    ASTRA_STORAGE_WORK_DETACH,
    ASTRA_STORAGE_WORK_PATH,
    ASTRA_STORAGE_WORK_EJECT,
    ASTRA_STORAGE_WORK_FORMAT,
    ASTRA_STORAGE_WORK_PARTITION
};

typedef struct AstraStorageWork {
    pid_t pid;
    int result;
    uint64_t id;
    uint64_t generation;
    uint32_t operation;
    int cancelled;
    uint64_t serial;
    uint64_t epoch;
    uint32_t path_operation;
    uint64_t deadline_ns;
} AstraStorageWork;

typedef struct AstraStorageWorkReply {
    uint64_t id;
    uint64_t generation;
    uint64_t linux_mount_id;
    uint32_t operation;
    int32_t error;
    uint32_t attached;
    uint32_t root;
} AstraStorageWorkReply;

/* Catalog owner prepares every loop/backing/target before fork. A killed worker
 * therefore leaves identifiable resources, reconciled after actual reap. */
int astra_storage_work_start(AstraStorageWork *work, const AstraStorageDomain *domain,
                              const AstraStorageRecord *record, int source,
                              const AstraStorageMounted *mounted, uint32_t operation);
int astra_storage_work_path_start(AstraStorageWork *work, const AstraStorageRecord *record,
                                   int allowed_root, const AstraStoragePathRequest *request,
                                   const int fds[ASTRA_STORAGE_PROVIDER_FDS]);
/* Formatter executable and exact exclusive block FD are opened by provider.
 * Child drops to provider user before exec and receives no device path. */
int astra_storage_work_format_start(AstraStorageWork *work,
    const AstraStorageRecord *record, int device, int executable,
    uint32_t format, const char *label, uid_t uid, gid_t gid);
/* Fixed-command disk edit. Provider validates all sibling usage and holds the
 * exact exclusive parent-disk FD. The child runs the verified partition tool
 * as root because stock sfdisk reopens /proc/self/fd/3 after exec. */
int astra_storage_work_partition_start(AstraStorageWork *work,
    const AstraStorageRecord *record, int device, int executable,
    const AstraVolumePartitionEdit *edit);
void astra_storage_work_cancel(AstraStorageWork *work);
/* 0 pending; 1 reaped (reply.error may be nonzero); negative invalid job. */
int astra_storage_work_poll(AstraStorageWork *work, AstraStorageWorkReply *reply, int *root);
int astra_storage_work_path_poll(AstraStorageWork *work, AstraStoragePathReply *reply, int *result);
int astra_storage_work_edit_poll(AstraStorageWork *work, int *error);

#endif
