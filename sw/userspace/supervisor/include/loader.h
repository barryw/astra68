#ifndef ASTRA_SUPERVISOR_LOADER_H
#define ASTRA_SUPERVISOR_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include <astra/process.h>
#include <astra/proc.h>
#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/vfs_service.h>

/* One reader at a time is the shape of `ps`; a queue of one is enough. */
#define SUPERVISOR_PROC_PORT_MESSAGES 4u
#define SUPERVISOR_PROC_PORT_BUDGET 8u
#define SUPERVISOR_MANIFEST_GRANT_MAX ASTRA_LAUNCH_GRANT_MAX
#define SUPERVISOR_MANIFEST_PUBLICATION_MAX ASTRA_MESSAGE_HANDLES_MAX
#define SUPERVISOR_MANIFEST_PATH_MAX ASTRA_VFS_PATH_MAX
/* Program-local boot-controller failures. They are never kernel verdicts. */
#define SUPERVISOR_LOADER_FAIL_MANIFEST 32u
#define SUPERVISOR_LOADER_FAIL_ORDER    33u
#define SUPERVISOR_LOADER_FAIL_CHILD    34u
#define SUPERVISOR_LOADER_FAIL_PUBLISH  35u

static inline uint32_t
supervisor_loader_child_status(uint32_t status)
{
    if (status == ASTRA_STATUS_OK)
        return ASTRA_STATUS_PEER_DEAD;
    return ASTRA_STATUS_IS_VERDICT(status) ?
        SUPERVISOR_LOADER_FAIL_CHILD : status;
}

typedef struct SupervisorManifestGrant {
    char name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t rights;
    uint32_t is_namespace;
} SupervisorManifestGrant;

typedef struct SupervisorManifestPublication {
    char name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t rights;
} SupervisorManifestPublication;

typedef struct SupervisorManifestEntry {
    char path[SUPERVISOR_MANIFEST_PATH_MAX];
    SupervisorManifestGrant grants[SUPERVISOR_MANIFEST_GRANT_MAX];
    uint32_t grant_count;
    SupervisorManifestPublication
        serves[SUPERVISOR_MANIFEST_PUBLICATION_MAX];
    uint32_t serves_count;
    uint32_t delegates;
    uint32_t required;
    uint32_t resident;
} SupervisorManifestEntry;

typedef struct SupervisorManifest {
    SupervisorManifestEntry *entries;
    uint32_t count;
    uint32_t capacity;
} SupervisorManifest;

#define SUPERVISOR_MANIFEST_INIT {0}

typedef struct SupervisorProcessRecord {
    uint32_t handle;
    uint32_t resident;
    uint32_t id;
    uint32_t paused;
    uint32_t service_flags;
    uint32_t restart_policy;
    uint32_t action;
    char service_name[ASTRA_VFS_NAME_MAX];
} SupervisorProcessRecord;

typedef struct SupervisorProcessTable {
    SupervisorProcessRecord *records;
    uint32_t count;
    uint32_t capacity;
} SupervisorProcessTable;

typedef void *(*SupervisorReallocate)(void *pointer, size_t size);

static inline int
supervisor_process_table_reserve(SupervisorProcessTable *table,
                                 uint32_t required,
                                 SupervisorReallocate reallocate)
{
    SupervisorProcessRecord *grown;
    uint32_t capacity;

    if (table == NULL || reallocate == NULL)
        return 0;
    if (required <= table->capacity)
        return 1;
    capacity = table->capacity == 0u ? 8u : table->capacity;
    while (capacity < required) {
        if (capacity > UINT32_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
#if SIZE_MAX < UINT32_MAX
    if (capacity > SIZE_MAX / sizeof(*table->records))
        return 0;
#endif
    grown = reallocate(table->records,
                       (size_t)capacity * sizeof(*table->records));
    if (grown == NULL)
        return 0;
    table->records = grown;
    table->capacity = capacity;
    return 1;
}

/* Parses a mutable byte span. Zero retains no entry. */
int supervisor_manifest_parse(char *text, uint32_t length,
                              SupervisorManifest *manifest);
void supervisor_manifest_destroy(SupervisorManifest *manifest);
int supervisor_manifest_grant(char *text, SupervisorManifestGrant *grant);
int supervisor_manifest_authority(char *text, char *name, uint32_t *rights,
                                  int allow_raw);
/* Launches the shipped manifest from the temporary bootstrap mount. */
uint32_t supervisor_loader_start(const AstraStartupInfo *startup);

/* The supervisor's local event target and the client capability it publishes. */
uint32_t supervisor_loader_proc_mount(void);
void supervisor_loader_pump_proc(void);
uint32_t supervisor_loader_process_handle(void);

uint32_t supervisor_loader_event_control(void);
void supervisor_loader_pump_event_control(void);

/* Keeps supervising after a resident service exits; the kernel's init lives. */
uint32_t supervisor_loader_watch(const AstraStartupInfo *startup);

#endif
