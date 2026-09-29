#include "socket_internal.h"

#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>

int astra_posix_pack_ifconf(const AstraNetworkInterfaceAddress *interfaces,
                           uint32_t count, struct ifconf *config)
{
    size_t written = 0u;

    for (uint32_t index = 0u; index < count; ++index) {
        struct ifreq item = {0};
        struct sockaddr_in address = {0};

        if (interfaces[index].address.size !=
                sizeof(interfaces[index].address) ||
            memchr(interfaces[index].name, '\0', IFNAMSIZ) == NULL) {
            errno = EIO;
            return -1;
        }
        if (interfaces[index].address.family == ASTRA_NETWORK_FAMILY_IPV6)
            continue;
        if (interfaces[index].address.family != ASTRA_NETWORK_FAMILY_IPV4) {
            errno = EIO;
            return -1;
        }
        if (written + sizeof(item) > (size_t)config->ifc_len)
            break;
        (void)memcpy(item.ifr_name, interfaces[index].name, IFNAMSIZ);
        address.sin_family = AF_INET;
        (void)memcpy(&address.sin_addr,
                     interfaces[index].address.address,
                     sizeof(address.sin_addr));
        (void)memcpy(&item.ifr_addr, &address, sizeof(address));
        (void)memcpy(config->ifc_buf + written, &item, sizeof(item));
        written += sizeof(item);
    }
    config->ifc_len = (int)written;
    return 0;
}
