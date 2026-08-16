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

uint32_t astra_process_vfs_init(const AstraStartupInfo *startup);
AstraAssignTable *astra_process_vfs_assigns(void);
AstraVfsClient *astra_process_vfs_client(void);
AstraVfsClient *astra_process_vfs_client_for(const AstraAssign *assign);
AstraVfsClient *astra_process_vfs_assign_client(const AstraAssign *assign,
                                                void *context);
uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *startup);
void astra_process_filesystem_close(AstraProcessFilesystem *filesystem);

#endif
