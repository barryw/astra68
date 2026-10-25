#ifndef ASTRA_STORAGE_MOUNT_H
#define ASTRA_STORAGE_MOUNT_H

#include "astra_storage_catalog.h"

#include <sys/types.h>

typedef struct AstraStorageDomain {
    int directory;
    uid_t uid;
    gid_t gid;
    char path[64];
} AstraStorageDomain;

/* Owned Linux descriptors, not a second lifecycle or guest handle table. */
typedef struct AstraStorageMounted {
    uint64_t id;
    uint64_t linux_mount_id;
    int root;
    int loop;
    int backing;
    int attached; /* kernel acknowledged mount; retain rollback even before root open */
    int directory; /* only a target created by this operation may be removed */
    char target[128];
} AstraStorageMounted;

/* Root only; creates an owned mount directory without changing namespaces.
 * A launcher may retain this exact descriptor outside its provider child. */
int astra_storage_domain_open(AstraStorageDomain *domain, uid_t uid, gid_t gid);
/* Enter the provider's private mount namespace and replace its directory FD
 * with a verified descriptor anchored in that namespace. The launcher retains
 * its own process copy of the same owned inode outside the namespace. */
int astra_storage_domain_enter(AstraStorageDomain *domain);
/* Refuses cleanup while any mountpoint remains. */
int astra_storage_domain_close(AstraStorageDomain *domain);
/* After the exact provider and its descendants retire, remove only empty
 * canonical mountpoint directories through this owned FD. No mounts, loops,
 * recursive removal or global temporary-directory enumeration occurs here. */
int astra_storage_domain_retire(AstraStorageDomain *domain);
/* Source descriptor was opened/verified by the trusted provider. Image windows
 * are explicit; ordinary ISO files use offset zero and their exact file size.
 * The canonical QEMU file facility must hold image write exclusion and backing
 * mount authority before transferring the descriptor. This seam owns no second
 * file-reference table and is not directly callable by guest clients. */
int astra_storage_linux_mount(AstraStorageDomain *domain,
                              const AstraStorageRecord *record, int source,
                              uint64_t offset, uint64_t length,
                              AstraStorageMounted *mounted);
/* Prepare in the catalog owner; attach may run in a forked worker sharing
 * this mount namespace. Parent retains loop/backing/target even if it dies. */
int astra_storage_linux_prepare(AstraStorageDomain *domain,
                                const AstraStorageRecord *record, int source,
                                uint64_t offset, uint64_t length,
                                AstraStorageMounted *mounted);
int astra_storage_linux_attach(AstraStorageDomain *domain,
                               const AstraStorageRecord *record, int source,
                               AstraStorageMounted *mounted);
int astra_storage_linux_bind(AstraStorageDomain *domain,
                             const AstraStorageRecord *record, int root,
                             AstraStorageMounted *mounted);
/* Sync writable storage, drop provider root, real unmount, then loop cleanup.
 * On refusal, reacquire only the same verified Linux mount. EAGAIN after real
 * unmount keeps loop/backing charges and requires a later cleanup retry. */
int astra_storage_linux_unmount(AstraStorageMounted *mounted, int writable);
int astra_storage_linux_detach(AstraStorageMounted *mounted, int writable);
int astra_storage_linux_cleanup(AstraStorageMounted *mounted);
/* After worker exit, inspect the protected target rather than guessing whether
 * an interrupted mount/unmount syscall committed. */
int astra_storage_linux_reconcile(AstraStorageDomain *domain,
                                  AstraStorageMounted *mounted);
int astra_storage_linux_loop(int source, uint64_t offset, uint64_t length, int readonly);
int astra_storage_linux_loop_release(int *loop);

#endif
