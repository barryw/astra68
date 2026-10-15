#ifndef ASTRA_STORAGE_PATH_H
#define ASTRA_STORAGE_PATH_H

#include "astra_storage_transport.h"

int astra_storage_path_receive(int socket, pid_t peer, uid_t uid, gid_t gid,
                                AstraStoragePathRequest *request,
                                int fds[ASTRA_STORAGE_PROVIDER_FDS]);
int astra_storage_path_reply(int socket, const AstraStoragePathReply *reply,
                              int result);
int astra_storage_path_roots(int allowed_root,
                              const int fds[ASTRA_STORAGE_PROVIDER_FDS]);
/* The provider selects a live private mount by ID/generation before calling
 * this. Exports must be private bind mounts: an arbitrary directory on the
 * host root filesystem is not sufficient authority for retained file FDs. */
void astra_storage_path_execute(int allowed_root, int readonly,
                                 const AstraStoragePathRequest *request,
                                 const int fds[ASTRA_STORAGE_PROVIDER_FDS],
                                 AstraStoragePathReply *reply, int *result);
/* Trusted release provisioning only. Existing entries are preserved, including
 * symlinks; destination directories are resolved beneath the private root.
 * Each new regular file is published atomically after fsync, never overwritten.
 * This is not exposed through either application or private peer protocols. */
int astra_storage_path_seed(int root, int source, const char *directory);

#endif
