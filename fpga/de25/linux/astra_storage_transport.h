#ifndef ASTRA_STORAGE_TRANSPORT_H
#define ASTRA_STORAGE_TRANSPORT_H

#include <astra/storage_provider.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

int astra_storage_send_packet(int socket, const void *packet, size_t size,
                               const int *fds, unsigned count);
int astra_storage_receive_packet(int socket, void *packet, size_t size,
                                  int fds[ASTRA_STORAGE_PROVIDER_FDS],
                                  unsigned *count, struct ucred *credentials);

/* One packet, exact credentials and exact descriptor count. On every failure
 * received descriptors are closed. The caller serializes calls on a socket. */
int astra_storage_path_call(int socket, const AstraStoragePathRequest *request,
                             const int fds[ASTRA_STORAGE_PROVIDER_FDS],
                             AstraStoragePathReply *reply, int *result);
int astra_storage_control_call(int socket, const AstraStorageControlRequest *request,
                                int source, AstraStorageControlReply *reply, int *root);

#endif
