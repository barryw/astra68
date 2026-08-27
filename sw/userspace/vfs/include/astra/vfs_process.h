#ifndef ASTRA_VFS_PROCESS_H
#define ASTRA_VFS_PROCESS_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/filesystem_library.h>
#include <astra/shared_library.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>

typedef struct AstraProcessFilesystem {
    AstraFilesystem filesystem;
    AstraLibraryHandle *handle;
    const AstraFilesystemLibraryV1 *library;
} AstraProcessFilesystem;

#define ASTRA_PROCESS_FILESYSTEM_INIT { ASTRA_FILESYSTEM_INIT, 0, 0 }

/* Native file state transferred across an in-place personality exec. */
typedef struct AstraProcessFileState {
    uint64_t offset;
    uint64_t size;
    uint32_t service;
    uint32_t file;
    uint32_t flags;
    uint16_t kind;
    uint16_t member;
} AstraProcessFileState;

#define ASTRA_PROCESS_FILE_STATE_SIZE 32u
_Static_assert(sizeof(AstraProcessFileState) ==
                   ASTRA_PROCESS_FILE_STATE_SIZE,
               "process file state changed");

uint32_t astra_process_vfs_init(const AstraStartupInfo *startup);
void astra_process_vfs_close(void);
uint32_t astra_process_vfs_after_fork_child(
    const AstraStartupInfo *startup);
AstraAssignTable *astra_process_vfs_assigns(void);
AstraVfsClient *astra_process_vfs_client(void);
AstraVfsClient *astra_process_vfs_client_for(const AstraAssign *assign);
AstraVfsClient *astra_process_vfs_assign_client(const AstraAssign *assign,
                                                void *context);
void astra_process_vfs_set_activity(uint32_t activity);
uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *startup);
uint32_t astra_process_filesystem_open_bootstrap(
    AstraProcessFilesystem *filesystem, const AstraStartupInfo *startup);
uint32_t astra_process_path(const char *typed, char *out, uint32_t capacity);
uint32_t astra_process_read_file(AstraProcessFilesystem *filesystem,
                                 const char *path, void *bytes,
                                 uint32_t capacity, uint32_t *length);
void astra_process_filesystem_close(AstraProcessFilesystem *filesystem);

/* Opaque process-transport state used by personality exec handoff. */
uint32_t astra_process_vfs_state_size(void);
uint32_t astra_process_vfs_export(void *state, uint32_t capacity,
                                  uint32_t *used);
uint32_t astra_process_vfs_import(const AstraStartupInfo *startup,
                                  const void *state, uint32_t size);
uint32_t astra_process_vfs_client_handle(const AstraVfsClient *client);
AstraVfsClient *astra_process_vfs_client_handle_lookup(uint32_t handle);
uint32_t astra_process_file_export(const AstraFile *file,
                                   AstraProcessFileState *state);
uint32_t astra_process_file_import(const AstraProcessFileState *state,
                                   AstraFile *file);

#endif
