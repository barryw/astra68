#ifndef ASTRA_POSIX_SOCKET_INTERNAL_H
#define ASTRA_POSIX_SOCKET_INTERNAL_H

#include <astra/network_library.h>

/* Shared native session for POSIX sockets and legacy resolver adapters. */
AstraNetworkSession *astra_posix_network_session(void);
int astra_posix_socket_interface_ioctl(int fd, void *parameter);
struct ifconf;
int astra_posix_pack_ifconf(const AstraNetworkInterfaceAddress *interfaces,
                           uint32_t count, struct ifconf *config);

#endif
