#ifndef ASTRA_KERNEL_HANDLE_H
#define ASTRA_KERNEL_HANDLE_H

#include <stdint.h>

#define KERNEL_HANDLE_INVALID 0u
#define KERNEL_HANDLE_MAX_ENTRIES 16u

typedef uint32_t KernelHandle;

typedef enum KernelObjectType {
    KERNEL_OBJECT_NONE = 0,
    KERNEL_OBJECT_PROCESS = 1,
    KERNEL_OBJECT_THREAD = 2,
    KERNEL_OBJECT_SYNC = 3,
    KERNEL_OBJECT_DEVICE = 4
} KernelObjectType;

typedef enum KernelHandleStatus {
    KERNEL_HANDLE_OK = 0,
    KERNEL_HANDLE_INVALID_ARGUMENT,
    KERNEL_HANDLE_TABLE_FULL,
    KERNEL_HANDLE_INVALID_HANDLE,
    KERNEL_HANDLE_TYPE_MISMATCH,
    KERNEL_HANDLE_ACCESS_DENIED
} KernelHandleStatus;

typedef void (*KernelHandleRelease)(void *object, void *context);

typedef struct KernelHandleEntry {
    void *object;
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
} KernelHandleTable;

void kernel_handle_table_init(KernelHandleTable *table);
KernelHandleStatus kernel_handle_install(KernelHandleTable *table,
                                         KernelObjectType type,
                                         uint32_t rights, void *object,
                                         KernelHandleRelease release,
                                         void *release_context,
                                         KernelHandle *handle);
KernelHandleStatus kernel_handle_lookup(const KernelHandleTable *table,
                                        KernelHandle handle,
                                        KernelObjectType required_type,
                                        uint32_t required_rights,
                                        void **object);
KernelHandleStatus kernel_handle_close(KernelHandleTable *table,
                                       KernelHandle handle);
uint32_t kernel_handle_close_all(KernelHandleTable *table);
uint32_t kernel_handle_count(const KernelHandleTable *table);

#endif
