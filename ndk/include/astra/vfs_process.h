/** @file vfs_process.h @brief Process-global Filesystem Kit binding. */
#ifndef ASTRA_VFS_PROCESS_H
#define ASTRA_VFS_PROCESS_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/filesystem_library.h>
#include <astra/shared_library.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>

/** Process-global Filesystem Kit binding. */
typedef struct AstraProcessFilesystem {
    AstraFilesystem filesystem; /**< Attached high-level filesystem context. */
    AstraLibraryHandle *handle; /**< Open filesystem.library handle. */
    const AstraFilesystemLibraryV2 *library; /**< Validated export table. */
} AstraProcessFilesystem;

/** Static initializer for a closed process Filesystem Kit binding. */
#define ASTRA_PROCESS_FILESYSTEM_INIT { ASTRA_FILESYSTEM_INIT, 0, 0 }

/** Native file state transferred across an in-place personality exec. */
typedef struct AstraProcessFileState {
    uint64_t offset; /**< Current byte offset. */
    uint64_t size; /**< Known file length. */
    uint32_t service; /**< Filesystem service handle. */
    uint32_t file; /**< Service-side file handle. */
    uint32_t flags; /**< Open flags. */
    uint16_t kind; /**< ASTRA_VFS_NODE_* kind. */
    uint16_t member; /**< Union-assign member index. */
} AstraProcessFileState;

/** Serialized byte size of AstraProcessFileState. */
#define ASTRA_PROCESS_FILE_STATE_SIZE 32u
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraProcessFileState) ==
                   ASTRA_PROCESS_FILE_STATE_SIZE,
               "process file state changed");
/** @endcond */

/**
 * Initialize the current process's assign namespace and VFS clients.
 * @param startup Borrowed process startup record.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_vfs_init(const AstraStartupInfo *startup);
/** Close every current-process VFS client and clear its namespace. */
void astra_process_vfs_close(void);
/**
 * Reinitialize process-local synchronization after fork in the child.
 * @param startup Child startup record.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_vfs_after_fork_child(
    const AstraStartupInfo *startup);
/**
 * Set the base used by bare paths without changing the assign table.
 * @param assign Current assign name.
 * @param path Mount-relative current directory.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_vfs_set_current_directory(const char *assign,
                                                 const char *path);
/** @return Mutable process-global assign table. */
AstraAssignTable *astra_process_vfs_assigns(void);
/** @return Borrowed primary process-global VFS client, or NULL. */
AstraVfsClient *astra_process_vfs_client(void);
/**
 * Resolve the process client serving an assign member.
 * @param assign Borrowed namespace member.
 * @return Borrowed connected client, or NULL.
 */
AstraVfsClient *astra_process_vfs_client_for(const AstraAssign *assign);
/**
 * Callback-compatible process client resolver.
 * @param assign Borrowed namespace member.
 * @param context Unused; must be NULL.
 * @return Borrowed connected client, or NULL.
 */
AstraVfsClient *astra_process_vfs_assign_client(const AstraAssign *assign,
                                                void *context);
/**
 * Stamp subsequent process VFS requests with a diagnostic activity ID.
 * @param activity Activity identifier, or zero for none.
 */
void astra_process_vfs_set_activity(uint32_t activity);
/**
 * Open and attach filesystem.library using normal runtime loading.
 * @param filesystem Binding storage initialized on success.
 * @param startup Borrowed process startup record.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *startup);
/**
 * Open and attach filesystem.library through its bootstrap entry.
 * @param filesystem Binding storage initialized on success.
 * @param startup Borrowed process startup record.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_filesystem_open_bootstrap(
    AstraProcessFilesystem *filesystem, const AstraStartupInfo *startup);
/**
 * Qualify a user path against the current process directory.
 * @param typed Qualified or relative UTF-8 path.
 * @param out Receives an assign-qualified path.
 * @param capacity Bytes available in `out`.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_path(const char *typed, char *out, uint32_t capacity);
/**
 * Read an entire file into caller storage.
 * @param filesystem Open Filesystem Kit binding.
 * @param path Assign-qualified or process-relative path.
 * @param bytes Receives file bytes.
 * @param capacity Bytes available in `bytes`.
 * @param length Receives bytes read.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_read_file(AstraProcessFilesystem *filesystem,
                                 const char *path, void *bytes,
                                 uint32_t capacity, uint32_t *length);
/**
 * Detach and close a process Filesystem Kit binding.
 * @param filesystem Open binding to close.
 */
void astra_process_filesystem_close(AstraProcessFilesystem *filesystem);

/** @return Bytes required to export opaque personality-exec VFS state. */
uint32_t astra_process_vfs_state_size(void);
/**
 * Export process VFS state for an in-place personality exec.
 * @param state Receives opaque state bytes.
 * @param capacity Bytes available in `state`.
 * @param used Receives bytes written.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_vfs_export(void *state, uint32_t capacity,
                                  uint32_t *used);
/**
 * Import process VFS state after an in-place personality exec.
 * @param startup New personality's startup record.
 * @param state Opaque bytes produced by astra_process_vfs_export().
 * @param size Number of bytes in `state`.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_vfs_import(const AstraStartupInfo *startup,
                                  const void *state, uint32_t size);
/**
 * Return the startup capability handle corresponding to a client.
 * @param client Borrowed process VFS client.
 * @return Capability handle, or zero when not process-owned.
 */
uint32_t astra_process_vfs_client_handle(const AstraVfsClient *client);
/**
 * Find a process VFS client by startup capability handle.
 * @param handle Filesystem service capability.
 * @return Borrowed client, or NULL when unknown.
 */
AstraVfsClient *astra_process_vfs_client_handle_lookup(uint32_t handle);
/**
 * Export an open file for an in-place personality exec.
 * @param file Open high-level file.
 * @param state Receives portable process file state.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_file_export(const AstraFile *file,
                                   AstraProcessFileState *state);
/**
 * Import an open file after an in-place personality exec.
 * @param state Previously exported process file state.
 * @param file Receives an open high-level file.
 * @return ASTRA_VFS_* status.
 */
uint32_t astra_process_file_import(const AstraProcessFileState *state,
                                   AstraFile *file);

#endif
