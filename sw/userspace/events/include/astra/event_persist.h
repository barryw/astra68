#ifndef ASTRA_EVENT_PERSIST_H
#define ASTRA_EVENT_PERSIST_H

#include <stdint.h>

#include <astra/event_store.h>

/*
 * The durable form of one bounded store. I/O is supplied by the owner because
 * the target writes through an Astra VFS client while the host test writes to
 * memory; the format and recovery rules must be identical in both places.
 */
typedef int (*AstraEventSnapshotRead)(void *context, uint32_t offset,
                                      void *buffer, uint32_t length);
typedef int (*AstraEventSnapshotWrite)(void *context, uint32_t offset,
                                       const void *buffer, uint32_t length);

typedef struct AstraEventSnapshotInfo {
    uint32_t boot;
    uint32_t generation;
    uint32_t catalog_crc;
} AstraEventSnapshotInfo;

/* Validates the whole snapshot without changing a store. */
int astra_event_snapshot_probe(AstraEventSnapshotRead read, void *context,
                               uint32_t size, AstraEventSnapshotInfo *info);

/* Loads a snapshot already proven by probe into an initialized store. */
int astra_event_snapshot_load(AstraEventStore *store,
                              AstraEventSnapshotRead read, void *context,
                              uint32_t size, AstraEventSnapshotInfo *info);

/* Writes one complete, checksummed snapshot. */
int astra_event_snapshot_save(const AstraEventStore *store, uint32_t boot,
                              uint32_t generation,
                              AstraEventSnapshotWrite write, void *context);

#endif
