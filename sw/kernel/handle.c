#include "handle.h"

#include "generation.h"

#include <stddef.h>

#define KERNEL_HANDLE_SLOT_BITS 8u
#define KERNEL_HANDLE_SLOT_MASK ((1u << KERNEL_HANDLE_SLOT_BITS) - 1u)
#define KERNEL_HANDLE_GENERATION_MASK 0x00ffffffu

typedef struct KernelHandleReleaseRecord {
    void *object;
    KernelHandleRelease release;
    void *context;
} KernelHandleReleaseRecord;

static KernelHandle make_handle(uint32_t index, uint32_t generation)
{
    return (generation << KERNEL_HANDLE_SLOT_BITS) | (index + 1u);
}

static KernelHandleStatus find_entry(const KernelHandleTable *table,
                                     KernelHandle handle,
                                     const KernelHandleEntry **entry)
{
    uint32_t slot;
    uint32_t generation;

    if (table == NULL || entry == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    slot = handle & KERNEL_HANDLE_SLOT_MASK;
    generation = handle >> KERNEL_HANDLE_SLOT_BITS;
    if (slot == 0u || slot > KERNEL_HANDLE_MAX_ENTRIES || generation == 0u)
        return KERNEL_HANDLE_INVALID_HANDLE;
    --slot;
    if (table->entries[slot].occupied == 0u ||
        table->entries[slot].generation != generation)
        return KERNEL_HANDLE_INVALID_HANDLE;
    *entry = &table->entries[slot];
    return KERNEL_HANDLE_OK;
}

static void invalidate_entry(KernelHandleEntry *entry,
                             KernelHandleReleaseRecord *record)
{
    if (record != NULL) {
        record->object = entry->object;
        record->release = entry->release;
        record->context = entry->release_context;
    }
    entry->object = NULL;
    entry->release = NULL;
    entry->release_context = NULL;
    entry->rights = 0u;
    entry->type = KERNEL_OBJECT_NONE;
    entry->occupied = 0u;
    entry->reserved = 0u;
    entry->generation = kernel_generation_next_masked(
        entry->generation, KERNEL_HANDLE_GENERATION_MASK);
}

void kernel_handle_table_init(KernelHandleTable *table)
{
    if (table == NULL)
        return;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        KernelHandleEntry *entry = &table->entries[index];

        entry->object = NULL;
        entry->release = NULL;
        entry->release_context = NULL;
        entry->rights = 0u;
        entry->generation = 1u;
        entry->type = KERNEL_OBJECT_NONE;
        entry->occupied = 0u;
        entry->reserved = 0u;
    }
}

KernelHandleStatus kernel_handle_install(KernelHandleTable *table,
                                         KernelObjectType type,
                                         uint32_t rights, void *object,
                                         KernelHandleRelease release,
                                         void *release_context,
                                         KernelHandle *handle)
{
    if (table == NULL || object == NULL || handle == NULL ||
        type == KERNEL_OBJECT_NONE || rights == 0u)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *handle = KERNEL_HANDLE_INVALID;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        KernelHandleEntry *entry = &table->entries[index];

        if (entry->occupied != 0u)
            continue;
        if (entry->generation == 0u ||
            entry->generation > KERNEL_HANDLE_GENERATION_MASK)
            entry->generation = 1u;
        entry->object = object;
        entry->release = release;
        entry->release_context = release_context;
        entry->rights = rights;
        entry->type = (uint16_t)type;
        entry->occupied = 1u;
        entry->reserved = 0u;
        *handle = make_handle(index, entry->generation);
        return KERNEL_HANDLE_OK;
    }
    return KERNEL_HANDLE_TABLE_FULL;
}

KernelHandleStatus kernel_handle_lookup(const KernelHandleTable *table,
                                        KernelHandle handle,
                                        KernelObjectType required_type,
                                        uint32_t required_rights,
                                        void **object)
{
    const KernelHandleEntry *entry;
    KernelHandleStatus status;

    if (object == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *object = NULL;
    status = find_entry(table, handle, &entry);
    if (status != KERNEL_HANDLE_OK)
        return status;
    if (required_type != KERNEL_OBJECT_NONE &&
        entry->type != (uint16_t)required_type)
        return KERNEL_HANDLE_TYPE_MISMATCH;
    if ((entry->rights & required_rights) != required_rights)
        return KERNEL_HANDLE_ACCESS_DENIED;
    *object = entry->object;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_lookup_any(const KernelHandleTable *table,
                                            KernelHandle handle,
                                            uint32_t required_rights,
                                            KernelObjectType *type,
                                            void **object)
{
    const KernelHandleEntry *entry;
    KernelHandleStatus status;

    if (type == NULL || object == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *type = KERNEL_OBJECT_NONE;
    *object = NULL;
    status = find_entry(table, handle, &entry);
    if (status != KERNEL_HANDLE_OK)
        return status;
    if ((entry->rights & required_rights) != required_rights)
        return KERNEL_HANDLE_ACCESS_DENIED;
    if (entry->type == KERNEL_OBJECT_NONE || entry->object == NULL)
        return KERNEL_HANDLE_INVALID_HANDLE;
    *type = (KernelObjectType)entry->type;
    *object = entry->object;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_close(KernelHandleTable *table,
                                       KernelHandle handle)
{
    const KernelHandleEntry *found;
    KernelHandleReleaseRecord record;
    KernelHandleStatus status = find_entry(table, handle, &found);

    if (status != KERNEL_HANDLE_OK)
        return status;
    invalidate_entry((KernelHandleEntry *)found, &record);
    if (record.release != NULL)
        record.release(record.object, record.context);
    return KERNEL_HANDLE_OK;
}

uint32_t kernel_handle_close_all(KernelHandleTable *table)
{
    KernelHandleReleaseRecord records[KERNEL_HANDLE_MAX_ENTRIES];
    uint32_t count = 0u;

    if (table == NULL)
        return 0u;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        if (table->entries[index].occupied == 0u)
            continue;
        invalidate_entry(&table->entries[index], &records[count]);
        ++count;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        if (records[index].release != NULL)
            records[index].release(records[index].object,
                                   records[index].context);
    }
    return count;
}

uint32_t kernel_handle_count(const KernelHandleTable *table)
{
    uint32_t count = 0u;

    if (table == NULL)
        return 0u;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        if (table->entries[index].occupied != 0u)
            ++count;
    }
    return count;
}
