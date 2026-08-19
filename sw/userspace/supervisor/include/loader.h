#ifndef ASTRA_SUPERVISOR_LOADER_H
#define ASTRA_SUPERVISOR_LOADER_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

#define SUPERVISOR_MANIFEST_ENTRY_MAX 6u
/* Long enough for "SERVICES:display" and an application bundle name. */
#define SUPERVISOR_PROCESS_NAME_MAX 32u
/* One reader at a time is the shape of `ps`; a queue of one is enough. */
#define SUPERVISOR_PROC_PORT_MESSAGES 4u
#define SUPERVISOR_PROC_PORT_BUDGET 8u
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
    uint32_t resident;
} SupervisorManifestEntry;

typedef struct SupervisorManifest {
    SupervisorManifestEntry entries[SUPERVISOR_MANIFEST_ENTRY_MAX];
    uint32_t count;
} SupervisorManifest;

/* Parses a mutable, NUL-terminated file whole. Zero retains no entry. */
int supervisor_manifest_parse(char *text, uint32_t length,
                              SupervisorManifest *manifest);
int supervisor_manifest_grant(char *text, SupervisorManifestGrant *grant);

/* Launches the shipped manifest from the temporary bootstrap mount. */
uint32_t supervisor_loader_start(const AstraStartupInfo *startup,
                                 const AstraStartupCapability *capabilities);

/* The supervisor's local event target and the client capability it publishes. */
/*
 * The resident process table, for the PROC: tree. `supervisor_loader_process_at`
 * yields the handle and, if asked, the path it was launched from; index is
 * dense and shifts when a process exits, so a reader walks it in one pass.
 */
uint32_t supervisor_loader_proc_mount(void);
void supervisor_loader_pump_proc(void);
uint32_t supervisor_loader_process_count(void);
uint32_t supervisor_loader_process_at(uint32_t index, const char **path);

uint32_t supervisor_loader_event_control(void);
void supervisor_loader_pump_event_control(void);

/* Keeps supervising after a resident service exits; the kernel's init lives. */
uint32_t supervisor_loader_watch(
    const AstraStartupInfo *startup,
    const AstraStartupCapability *capabilities);

#endif
