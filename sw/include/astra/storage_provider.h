#ifndef ASTRA_STORAGE_PROVIDER_H
#define ASTRA_STORAGE_PROVIDER_H

#include <stdint.h>

#include <astra/volume_abi.h>

/* No host path, descriptor, client pointer or claimed application owner. */
typedef struct AstraStorageRecord {
    uint64_t id;
    uint64_t generation;
    uint64_t parent;
    uint64_t parent_generation;
    uint64_t linux_mount_id;
    uint64_t byte_limit;
    uint64_t inode_limit;
    uint64_t backing_object;
    uint64_t capacity_bytes;
    uint64_t offset_bytes;
    uint32_t partition_number;
    uint32_t partition_map;
    uint32_t kind;
    uint32_t class_id;
    uint32_t state;
    uint32_t format;
    uint32_t origin;
    uint32_t flags;
    int32_t error;
    char name[32];
    char uuid[128];
    char label[256];
    char detected_type[64];
} AstraStorageRecord;

/* Private Linux protocol. Only the launched QEMU peer receives its socket;
 * application authority remains in the native rooted scopes. */
#define ASTRA_STORAGE_PROVIDER_VERSION 6u
#define ASTRA_STORAGE_PROVIDER_PATH_MAX 4096u
#define ASTRA_STORAGE_PROVIDER_FDS 3u
#define ASTRA_STORAGE_PROVIDER_PAGE 16u

enum AstraStorageControlOperation {
    ASTRA_STORAGE_CONTROL_SNAPSHOT = 1,
    ASTRA_STORAGE_CONTROL_ROOT,
    ASTRA_STORAGE_CONTROL_MOUNT,
    ASTRA_STORAGE_CONTROL_UNMOUNT,
    ASTRA_STORAGE_CONTROL_QUIESCE,
    ASTRA_STORAGE_CONTROL_RESUME,
    ASTRA_STORAGE_CONTROL_IMAGE,
    ASTRA_STORAGE_CONTROL_EJECT,
    ASTRA_STORAGE_CONTROL_RELEASE_IMAGE_SOURCE,
    ASTRA_STORAGE_CONTROL_BACKING,
    ASTRA_STORAGE_CONTROL_FORMAT,
    ASTRA_STORAGE_CONTROL_FORMAT_STATUS,
    ASTRA_STORAGE_CONTROL_PARTITION,
    ASTRA_STORAGE_CONTROL_PARTITION_STATUS
};

typedef struct AstraStorageControlRequest {
    uint32_t version;
    uint32_t operation;
    uint64_t serial;
    uint64_t epoch;
    uint64_t target;
    uint64_t generation;
    uint64_t sequence;
    uint64_t after;
    uint64_t byte_limit;
    uint64_t inode_limit;
    uint64_t backing_object;
    uint32_t flags;
    uint32_t reserved;
    uint32_t format;
    char label[12];
    AstraVolumePartitionEdit edit;
} AstraStorageControlRequest;

typedef struct AstraStorageControlReply {
    uint32_t version;
    int32_t error;
    uint64_t serial;
    uint64_t epoch;
    uint64_t sequence;
    uint64_t next;
    uint64_t object;
    uint32_t count;
    uint32_t reserved;
    int32_t operation_error;
    uint32_t reserved2;
    AstraStorageRecord records[ASTRA_STORAGE_PROVIDER_PAGE];
} AstraStorageControlReply;

enum AstraStoragePathOperation {
    ASTRA_STORAGE_PATH_OPEN = 1,
    ASTRA_STORAGE_PATH_RESOLVE,
    ASTRA_STORAGE_PATH_ROOTED,
    ASTRA_STORAGE_PATH_NAME,
    ASTRA_STORAGE_PATH_MKDIR,
    ASTRA_STORAGE_PATH_UNLINK,
    ASTRA_STORAGE_PATH_RENAME,
    ASTRA_STORAGE_PATH_LINK,
    ASTRA_STORAGE_PATH_CHMOD,
    ASTRA_STORAGE_PATH_READLINK,
    ASTRA_STORAGE_PATH_SYMLINK
};

/* FDs are root, optional base, optional second base. Their positions are
 * described by fd_mask, never by caller-supplied Linux descriptor numbers. */
typedef struct AstraStoragePathRequest {
    uint32_t version;
    uint32_t operation;
    uint64_t serial;
    uint64_t epoch;
    uint64_t mount;
    uint64_t generation;
    uint32_t fd_mask;
    uint32_t flags;
    uint32_t mode;
    uint32_t resolve;
    uint32_t links;
    uint32_t capacity;
    uint32_t reserved[2];
    char path[ASTRA_STORAGE_PROVIDER_PATH_MAX];
    char path2[ASTRA_STORAGE_PROVIDER_PATH_MAX];
} AstraStoragePathRequest;

typedef struct AstraStoragePathReply {
    uint32_t version;
    int32_t error;
    uint64_t serial;
    uint64_t epoch;
    uint32_t trailing;
    uint32_t missing;
    uint32_t escape;
    uint32_t consumed;
    uint32_t links;
    uint32_t length;
    char leaf[256];
    char continuation[ASTRA_STORAGE_PROVIDER_PATH_MAX];
    char data[ASTRA_STORAGE_PROVIDER_PATH_MAX];
} AstraStoragePathReply;

#endif
