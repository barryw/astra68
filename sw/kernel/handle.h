#ifndef ASTRA_KERNEL_HANDLE_H
#define ASTRA_KERNEL_HANDLE_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_HANDLE_INVALID 0u
/*
 * How many objects one process may hold at once.
 *
 * It was 16, which was generous when a process held its own two, a device or
 * three, and nothing else. The supervisor now hosts services, and a service is
 * a port: two endpoints each for the stream sink, the stream source, the
 * storage service and the events service is eight handles before anything else
 * -- and then a launch adds the child's process handle, and receiving a
 * request adds the reply handle that came with it.
 *
 * At 16 the table filled exactly one slot short of that last one. The symptom
 * was a launched program's first request sitting in a port nobody could take
 * it out of: `astra_port_receive` answered RESOURCE_LIMIT because it could not
 * install the attached reply handle, the message stayed queued, and the child
 * waited for an answer that could not be produced. Nothing was broken and
 * nothing said so.
 *
 * The protected GUI terminal adds the seventh live process needed to launch a
 * foreground command while storage, events, input, display, terminal, and the
 * supervisor remain resident. Adding the read-only LIBS: namespace raises the
 * exact demand to 38. The table remains a bounded two-word bitmap and the
 * compile-time demand proof below remains exact.
 */
#define KERNEL_HANDLE_MAX_ENTRIES 38u
#define KERNEL_HANDLE_BITMAP_WORDS \
    ((KERNEL_HANDLE_MAX_ENTRIES + 31u) / 32u)
#define KERNEL_HANDLE_TRANSFER_MAX 8u
#define KERNEL_HANDLE_DETACHED_MAX 256u

typedef uint32_t KernelHandle;
typedef uint32_t KernelDetachedHandle;

typedef enum KernelObjectType {
    KERNEL_OBJECT_NONE = 0,
    KERNEL_OBJECT_PROCESS = 1,
    KERNEL_OBJECT_THREAD = 2,
    KERNEL_OBJECT_SYNC = 3,
    KERNEL_OBJECT_DEVICE = 4,
    KERNEL_OBJECT_TIMER = 5,
    KERNEL_OBJECT_PORT_SEND = 6,
    KERNEL_OBJECT_PORT_RECEIVE = 7,
    KERNEL_OBJECT_AREA = 8,
    KERNEL_OBJECT_RING_PRODUCER = 9,
    KERNEL_OBJECT_RING_CONSUMER = 10,
    KERNEL_OBJECT_IRQ = 11,
    KERNEL_OBJECT_DMA = 12
} KernelObjectType;

typedef enum KernelHandleStatus {
    KERNEL_HANDLE_OK = 0,
    KERNEL_HANDLE_INVALID_ARGUMENT,
    KERNEL_HANDLE_TABLE_FULL,
    KERNEL_HANDLE_INVALID_HANDLE,
    KERNEL_HANDLE_TYPE_MISMATCH,
    KERNEL_HANDLE_ACCESS_DENIED,
    KERNEL_HANDLE_DUPLICATE,
    KERNEL_HANDLE_TRANSFER_POOL_FULL,
    KERNEL_HANDLE_INVALID_STATE,
    KERNEL_HANDLE_CORRUPT
} KernelHandleStatus;

typedef void (*KernelHandleRelease)(void *object, void *context);
typedef bool (*KernelHandleRetain)(void *object, void *context);

typedef struct KernelHandleEntry {
    void *object;
    KernelHandleRetain retain;
    KernelHandleRelease release;
    void *release_context;
    uint32_t rights;
    uint32_t generation;
    uint16_t type;
    uint8_t occupied;
    uint8_t reserved;
} KernelHandleEntry;

typedef struct KernelHandleTable {
    KernelHandleEntry entries[KERNEL_HANDLE_MAX_ENTRIES];
    uint32_t owner;
    uint32_t free_slots[KERNEL_HANDLE_BITMAP_WORDS];
} KernelHandleTable;

typedef struct KernelHandleTransferBatch {
    KernelDetachedHandle detached[KERNEL_HANDLE_TRANSFER_MAX];
    KernelHandle source[KERNEL_HANDLE_TRANSFER_MAX];
    uint8_t count;
    uint8_t state;
    uint8_t reserved[2];
} KernelHandleTransferBatch;

typedef struct KernelHandleImportReservation {
    KernelHandle handles[KERNEL_HANDLE_TRANSFER_MAX];
    uint8_t slots[KERNEL_HANDLE_TRANSFER_MAX];
    uint8_t count;
    uint8_t active;
    uint8_t reserved[2];
} KernelHandleImportReservation;

typedef struct KernelHandleTransferStats {
    uint32_t prepared_exports;
    uint32_t committed_exports;
    uint32_t export_rollbacks;
    uint32_t committed_imports;
    uint32_t import_rollbacks;
    uint32_t released_detached;
    uint32_t pool_exhaustions;
    uint32_t live_detached;
    uint32_t max_live_detached;
    uint32_t reserved_detached;
} KernelHandleTransferStats;

void kernel_handle_transfer_pool_init(void);
void kernel_handle_table_init(KernelHandleTable *table);
bool kernel_handle_table_set_owner(KernelHandleTable *table, uint32_t owner);
KernelHandleStatus kernel_handle_install(KernelHandleTable *table,
                                         KernelObjectType type,
                                         uint32_t rights, void *object,
                                         KernelHandleRelease release,
                                         void *release_context,
                                         KernelHandle *handle);
KernelHandleStatus kernel_handle_install_cloneable(
    KernelHandleTable *table, KernelObjectType type, uint32_t rights,
    void *object, KernelHandleRetain retain, KernelHandleRelease release,
    void *release_context, KernelHandle *handle);
KernelHandleStatus kernel_handle_duplicate(KernelHandleTable *table,
                                           KernelHandle source,
                                           uint32_t rights,
                                           KernelHandle *duplicate);
/*
 * The same copy, into another process's table: what a launch grants a child.
 * The source must carry TRANSFER and the rights must be a subset of it, so no
 * path through here creates authority that did not already exist.
 */
KernelHandleStatus kernel_handle_duplicate_into(
    const KernelHandleTable *source_table, KernelHandle source,
    uint32_t rights, KernelHandleTable *table, KernelHandle *duplicate);
KernelHandleStatus kernel_handle_lookup(const KernelHandleTable *table,
                                        KernelHandle handle,
                                        KernelObjectType required_type,
                                        uint32_t required_rights,
                                        void **object);
KernelHandleStatus kernel_handle_lookup_any(const KernelHandleTable *table,
                                            KernelHandle handle,
                                            uint32_t required_rights,
                                            KernelObjectType *type,
                                            void **object);
KernelHandleStatus kernel_handle_close(KernelHandleTable *table,
                                       KernelHandle handle);
uint32_t kernel_handle_close_all(KernelHandleTable *table);
uint32_t kernel_handle_count(const KernelHandleTable *table);
uint32_t kernel_handle_available(const KernelHandleTable *table);
bool kernel_handle_table_valid(const KernelHandleTable *table);
KernelHandleStatus kernel_handle_transfer_prepare(
    const KernelHandleTable *source_table, const KernelHandle *source_handles,
    uint32_t count, uint32_t required_rights,
    KernelHandleTransferBatch *batch);
KernelHandleStatus kernel_handle_transfer_commit_export(
    KernelHandleTable *source_table, KernelHandleTransferBatch *batch);
KernelHandleStatus kernel_handle_transfer_rollback(
    KernelHandleTransferBatch *batch);
KernelHandleStatus kernel_handle_import_reserve(
    KernelHandleTable *destination_table,
    const KernelDetachedHandle *detached, uint32_t count,
    KernelHandleImportReservation *reservation);
KernelHandleStatus kernel_handle_import_commit(
    KernelHandleTable *destination_table,
    KernelHandleImportReservation *reservation,
    const KernelDetachedHandle *detached);
KernelHandleStatus kernel_handle_import_cancel(
    KernelHandleTable *destination_table,
    KernelHandleImportReservation *reservation);
KernelHandleStatus kernel_handle_detached_release(
    const KernelDetachedHandle *detached, uint32_t count);
bool kernel_handle_detached_slot(KernelDetachedHandle detached,
                                 uint16_t *slot);
bool kernel_handle_transfer_pool_healthy(void);
bool kernel_handle_transfer_pool_valid(void);
bool kernel_handle_transfer_stats(KernelHandleTransferStats *stats);

#endif
