#ifndef ASTRA_SUPERVISOR_LOADER_H
#define ASTRA_SUPERVISOR_LOADER_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

#define SUPERVISOR_MANIFEST_ENTRY_MAX 4u
#define SUPERVISOR_MANIFEST_GRANT_MAX ASTRA_LAUNCH_GRANT_MAX
#define SUPERVISOR_MANIFEST_PATH_MAX 128u

typedef struct SupervisorManifestGrant {
    char name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t rights;
    uint32_t is_namespace;
} SupervisorManifestGrant;

typedef struct SupervisorManifestEntry {
    char path[SUPERVISOR_MANIFEST_PATH_MAX];
    SupervisorManifestGrant grants[SUPERVISOR_MANIFEST_GRANT_MAX];
    uint32_t grant_count;
    char serves[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t serves_rights;
    uint32_t delegates;
    uint32_t required;
} SupervisorManifestEntry;

typedef struct SupervisorManifest {
    SupervisorManifestEntry entries[SUPERVISOR_MANIFEST_ENTRY_MAX];
    uint32_t count;
} SupervisorManifest;

/* Parses a mutable, NUL-terminated file whole. Zero retains no entry. */
int supervisor_manifest_parse(char *text, uint32_t length,
                              SupervisorManifest *manifest);

/* Launches the shipped manifest from the temporary bootstrap mount. */
uint32_t supervisor_loader_start(const AstraStartupInfo *startup,
                                 const AstraStartupCapability *capabilities);

/* The supervisor's local event target and the client capability it publishes. */
uint32_t supervisor_loader_event_control(void);
void supervisor_loader_pump_event_control(void);

/* Keeps required services alive and reports the first one that exits. */
uint32_t supervisor_loader_watch(void);

#endif
