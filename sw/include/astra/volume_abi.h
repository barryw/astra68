#ifndef ASTRA_VOLUME_ABI_H
#define ASTRA_VOLUME_ABI_H

/** @file volume_abi.h @brief Native volume identities and catalog wire records. */
#include <stdint.h>
#include <astra/compiler.h>

/** Object identities are meaningful only in the provider epoch that issued them. */
typedef struct AstraVolumeReference {
    uint64_t epoch; /**< Provider incarnation. */
    uint64_t id; /**< Never-reused object identity within this epoch. */
    uint64_t generation; /**< Exact admitted incarnation of this object. */
} AstraVolumeReference;

/** Immutable authority of one native root broker. A zero source is global
 * public-volume authority; otherwise only mounts of this exact source qualify. */
typedef struct AstraVolumeBrokerInfo {
    AstraVolumeReference source; /**< Exact allowed source, all zero for global public-volume brokers. */
    uint32_t rights; /**< ASTRA_RIGHT_READ, optionally ASTRA_RIGHT_WRITE. */
    uint32_t reserved; /**< Zero. */
} AstraVolumeBrokerInfo;

/** Administrator operations use exact native references. Mount is admitted
 * asynchronously; catalog state reports the actual Linux outcome. */
enum AstraVolumeOperation {
    ASTRA_VOLUME_MOUNT_SOURCE = 1u,
    ASTRA_VOLUME_UNMOUNT,
    ASTRA_VOLUME_IMAGE,
    ASTRA_VOLUME_EJECT,
    ASTRA_VOLUME_RELEASE_IMAGE_SOURCE
};
/** Native lifecycle parameters; no host pathname or descriptor is accepted. */
typedef struct AstraVolumeControl {
    AstraVolumeReference reference; /**< Exact source or mount, zero for IMAGE. */
    uint32_t operation; /**< AstraVolumeOperation. */
    uint32_t flags; /**< MOUNT_SOURCE: READ_ONLY only; otherwise zero. */
    uint64_t byte_limit; /**< RAM admission byte limit; otherwise zero. */
    uint64_t inode_limit; /**< RAM admission inode limit; otherwise zero. */
    uint64_t image_share; /**< One-use retained native description, IMAGE only. */
} AstraVolumeControl;

/** Distinct device, inserted medium, filesystem source and namespace objects. */
enum AstraVolumeKind {
    ASTRA_VOLUME_DEVICE = 1,
    ASTRA_VOLUME_MEDIUM,
    ASTRA_VOLUME_SOURCE,
    ASTRA_VOLUME_MOUNT,
    ASTRA_VOLUME_BINDING
};

/** Physical or configured storage class. A private binding has class zero. */
enum AstraVolumeClass {
    ASTRA_VOLUME_FIXED = 1,
    ASTRA_VOLUME_USB,
    ASTRA_VOLUME_OPTICAL,
    ASTRA_VOLUME_RAM
};

/** Lifecycle is reported independently from metadata visibility or data authority. */
enum AstraVolumeState {
    ASTRA_VOLUME_DISCOVERED = 1,
    ASTRA_VOLUME_PROBING,
    ASTRA_VOLUME_UNMOUNTED,
    ASTRA_VOLUME_MOUNTING,
    ASTRA_VOLUME_MOUNTED,
    ASTRA_VOLUME_QUIESCING,
    ASTRA_VOLUME_LOST,
    ASTRA_VOLUME_FAILED
};

/** Recognized filesystem formats; recognition does not authorize repair or writes. */
enum AstraVolumeFormat {
    ASTRA_VOLUME_FORMAT_UNKNOWN,
    ASTRA_VOLUME_EXT4,
    ASTRA_VOLUME_FAT32,
    ASTRA_VOLUME_EXFAT,
    ASTRA_VOLUME_ISO9660,
    ASTRA_VOLUME_UDF,
    ASTRA_VOLUME_TMPFS,
    ASTRA_VOLUME_FAT12,
    ASTRA_VOLUME_FAT16,
    ASTRA_VOLUME_NTFS
};

/** Sources retain their actual origin instead of fabricating physical devices. */
enum AstraVolumeOrigin {
    ASTRA_VOLUME_ORIGIN_DEVICE = 1,
    ASTRA_VOLUME_ORIGIN_IMAGE,
    ASTRA_VOLUME_ORIGIN_RAM,
    ASTRA_VOLUME_ORIGIN_EXPORT,
    ASTRA_VOLUME_ORIGIN_SYSTEM_IMAGE
};

/** Object attributes and pending cleanup status. */
enum AstraVolumeFlags {
    ASTRA_VOLUME_READ_ONLY = 1u << 0,
    ASTRA_VOLUME_SYSTEM = 1u << 1,
    ASTRA_VOLUME_VOLATILE = 1u << 2,
    ASTRA_VOLUME_DUPLICATE_UUID = 1u << 3,
    ASTRA_VOLUME_AMBIGUOUS_FORMAT = 1u << 4,
    ASTRA_VOLUME_LABEL_INVALID = 1u << 5,
    ASTRA_VOLUME_TEMPORARY = 1u << 6,
    ASTRA_VOLUME_CLEANUP_PENDING = 1u << 7,
    ASTRA_VOLUME_CONFIGURED_RAM = 1u << 8
};

/** Public metadata, with no Linux path, descriptor, mount ID or data grant. */
typedef struct AstraVolumeRecord {
    uint64_t id; /**< Object identity, interpreted with the page's epoch. */
    uint64_t generation; /**< Current object incarnation. */
    uint64_t parent; /**< Retained device/medium/source/backing-mount dependency. */
    uint64_t parent_generation; /**< Exact admitted parent incarnation. */
    uint64_t byte_limit; /**< Configured volatile byte limit, otherwise zero. */
    uint64_t inode_limit; /**< Configured volatile inode limit, otherwise zero. */
    uint64_t backing_object; /**< Native backing-file identity for image sources. */
    uint32_t kind; /**< AstraVolumeKind. */
    uint32_t class_id; /**< AstraVolumeClass or zero for a binding. */
    uint32_t state; /**< AstraVolumeState. */
    uint32_t format; /**< AstraVolumeFormat. */
    uint32_t origin; /**< AstraVolumeOrigin. */
    uint32_t flags; /**< AstraVolumeFlags. */
    uint32_t status; /**< Native AstraStatus for the last lifecycle result. */
    uint32_t reserved; /**< Zero. */
    char name[32]; /**< Canonical namespace spelling, empty before publication. */
    char uuid[128]; /**< Format identifier metadata; never object authority. */
    char label[256]; /**< Valid UTF-8 volume label, empty when absent or invalid. */
} AstraVolumeRecord;

/** Number of rows per catalog page; use next/sequence to reach every row. */
#define ASTRA_VOLUME_PAGE_RECORDS 16u
/** Native volume catalog wire version. */
#define ASTRA_VOLUME_CATALOG_VERSION 3u

/** Sequence zero begins a new snapshot. Epoch zero is allowed only on that first page. */
typedef struct AstraVolumeCatalogQuery {
    uint32_t version; /**< ASTRA_VOLUME_CATALOG_VERSION. */
    uint32_t size; /**< sizeof(AstraVolumeCatalogQuery). */
    uint64_t epoch; /**< Expected provider incarnation, or zero to discover it. */
    uint64_t sequence; /**< Snapshot sequence, or zero to begin. */
    uint64_t after; /**< Exclusive last identity from the preceding page. */
} AstraVolumeCatalogQuery;

/** Coherent keyset page. A changed sequence refuses continuation and requires restart. */
typedef struct AstraVolumeCatalogPage {
    uint32_t version; /**< ASTRA_VOLUME_CATALOG_VERSION. */
    uint32_t size; /**< sizeof(AstraVolumeCatalogPage). */
    uint64_t epoch; /**< Provider incarnation that owns every returned identity. */
    uint64_t sequence; /**< Snapshot sequence. */
    uint64_t next; /**< Exclusive cursor, zero at the end. */
    uint32_t count; /**< Initialized records. */
    uint32_t reserved; /**< Zero. */
    AstraVolumeRecord records[ASTRA_VOLUME_PAGE_RECORDS]; /**< Visible object graph. */
} AstraVolumeCatalogPage;

/** One coalesced catalog invalidation. Read a fresh snapshot after receiving it. */
typedef struct AstraVolumeChange {
    uint64_t epoch; /**< Current provider incarnation, zero when unavailable. */
    uint64_t sequence; /**< Latest observed catalog sequence. */
    uint32_t status; /**< Native AstraStatus; failure withdraws cached metadata. */
    uint32_t reserved; /**< Zero. */
} AstraVolumeChange;

/** Resources that retain native storage authority or active descriptions. */
enum AstraVolumeUsageKind {
    ASTRA_VOLUME_USAGE_FILE = 1u,
    ASTRA_VOLUME_USAGE_DIRECTORY,
    ASTRA_VOLUME_USAGE_ROOT,
    ASTRA_VOLUME_USAGE_OPERATION,
    ASTRA_VOLUME_USAGE_SHARE,
    ASTRA_VOLUME_USAGE_IMAGE
};
/** Explicit absence of a current name is never represented by truncation. */
enum AstraVolumeUsageFlags {
    ASTRA_VOLUME_USAGE_LOST = 1u << 0,
    ASTRA_VOLUME_USAGE_UNNAMED = 1u << 1,
    ASTRA_VOLUME_USAGE_ACTIVE = 1u << 2,
    ASTRA_VOLUME_USAGE_CLEANUP_PENDING = 1u << 3,
    ASTRA_VOLUME_USAGE_INVALID_NAME = 1u << 4
};
/** Complete native namespace spelling, including its terminating zero. */
#define ASTRA_VOLUME_USAGE_PATH_MAX 4096u
/** Native usage wire representation version. */
#define ASTRA_VOLUME_USAGE_VERSION 2u
/** Small transfer pages; snapshot collectors own the complete array. */
#define ASTRA_VOLUME_USAGE_PAGE_RECORDS 2u
/** One canonical retained object; handle is its native file/share/root ID. */
typedef struct AstraVolumeUsageRecord {
    AstraVolumeReference volume; /**< Exact mount incarnation. */
    uint64_t handle; /**< Native resource identity, never a Linux descriptor. */
    uint64_t inode; /**< Retained file identity; zero when unavailable. */
    uint32_t process; /**< Kernel-authenticated holder PID, zero for service ownership. */
    uint32_t kind; /**< AstraVolumeUsageKind. */
    uint32_t rights; /**< ASTRA_RIGHT_READ/WRITE description access. */
    uint32_t flags; /**< AstraVolumeUsageFlags. */
    uint32_t path_length; /**< Exact UTF-8 bytes, excluding terminator. */
    uint32_t reserved; /**< Zero. */
    char path[ASTRA_VOLUME_USAGE_PATH_MAX]; /**< Native path, empty when explicitly unnamed. */
} AstraVolumeUsageRecord;
/** Zero ID/generation selects every mount; nonzero selects one exact incarnation. */
typedef struct AstraVolumeUsageQuery {
    uint32_t version; /**< ASTRA_VOLUME_USAGE_VERSION. */
    uint32_t size; /**< sizeof(AstraVolumeUsageQuery). */
    AstraVolumeReference volume; /**< Exact filter, epoch zero only on first page. */
    uint64_t sequence; /**< Canonical resource generation, zero to begin. */
    uint64_t catalog_sequence; /**< Provider graph generation, zero to begin. */
    uint64_t after; /**< Exclusive native resource identity. */
    uint32_t after_kind; /**< Exclusive kind paired with after. */
    uint32_t after_process; /**< Exclusive holder PID paired with kind and handle. */
} AstraVolumeUsageQuery;
/** Ordered by kind, handle and holder PID; mutation refuses continuation. */
typedef struct AstraVolumeUsagePage {
    uint32_t version; /**< ASTRA_VOLUME_USAGE_VERSION. */
    uint32_t size; /**< sizeof(AstraVolumeUsagePage). */
    AstraVolumeReference volume; /**< Admitted filter with nonzero provider epoch. */
    uint64_t sequence; /**< Coherent native resource generation. */
    uint64_t catalog_sequence; /**< Coherent provider graph generation. */
    uint64_t next; /**< Exclusive handle, zero at the end. */
    uint32_t next_kind; /**< Exclusive kind, zero at the end. */
    uint32_t count; /**< Initialized complete rows. */
    uint32_t next_process; /**< Exclusive holder PID paired with next/next_kind. */
    uint32_t reserved; /**< Zero. */
    AstraVolumeUsageRecord records[ASTRA_VOLUME_USAGE_PAGE_RECORDS]; /**< No Linux paths or descriptors. */
} AstraVolumeUsagePage;

/** @cond ASTRA_INTERNAL */
static inline int astra_volume_usage_key_compare(uint32_t kind, uint64_t handle,
    uint32_t process, uint32_t other_kind, uint64_t other_handle, uint32_t other_process)
{
    if (kind != other_kind) return kind < other_kind ? -1 : 1;
    if (handle != other_handle) return handle < other_handle ? -1 : 1;
    return process == other_process ? 0 : process < other_process ? -1 : 1;
}
_Static_assert(sizeof(AstraVolumeReference) == 24u, "volume reference ABI");
_Static_assert(sizeof(AstraVolumeRecord) == 504u, "volume record ABI");
_Static_assert(sizeof(AstraVolumeCatalogQuery) == 32u, "volume query ABI");
_Static_assert(sizeof(AstraVolumeCatalogPage) == 8104u, "volume page ABI");
_Static_assert(sizeof(AstraVolumeUsageRecord) == 4160u, "volume usage record ABI");
_Static_assert(sizeof(AstraVolumeUsageQuery) == 64u, "volume usage query ABI");
_Static_assert(sizeof(AstraVolumeUsagePage) == 8392u, "volume usage page ABI");
/** @endcond */
#endif
