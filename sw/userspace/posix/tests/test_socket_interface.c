#include "../src/socket_internal.h"

#include <assert.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>

int main(void)
{
    AstraNetworkInterfaceAddress addresses[2] = {0};
    struct ifreq output[2] = {0};
    struct ifconf config = {0};
    struct sockaddr_in socket_address;

    addresses[0].address.size = sizeof(addresses[0].address);
    addresses[0].address.family = ASTRA_NETWORK_FAMILY_IPV4;
    addresses[0].address.address[0] = 192u;
    addresses[0].address.address[1] = 0u;
    addresses[0].address.address[2] = 2u;
    addresses[0].address.address[3] = 9u;
    (void)memcpy(addresses[0].name, "eth0", 5u);
    addresses[1].address.size = sizeof(addresses[1].address);
    addresses[1].address.family = ASTRA_NETWORK_FAMILY_IPV6;
    (void)memcpy(addresses[1].name, "lo", 3u);
    config.ifc_len = sizeof(output);
    config.ifc_buf = (char *)output;
    assert(astra_posix_pack_ifconf(addresses, 2u, &config) == 0);
    assert(config.ifc_len == (int)sizeof(struct ifreq));
    assert(strcmp(output[0].ifr_name, "eth0") == 0);
    (void)memcpy(&socket_address, &output[0].ifr_addr,
                 sizeof(socket_address));
    assert(socket_address.sin_family == AF_INET);
    assert(memcmp(&socket_address.sin_addr,
                  addresses[0].address.address, 4u) == 0);

    config.ifc_len = 0;
    assert(astra_posix_pack_ifconf(addresses, 2u, &config) == 0);
    assert(config.ifc_len == 0);
    (void)memset(addresses[0].name, 'x', sizeof(addresses[0].name));
    config.ifc_len = sizeof(output);
    assert(astra_posix_pack_ifconf(addresses, 2u, &config) == -1);
    assert(errno == EIO);
    addresses[0].name[0] = '\0';
    addresses[0].address.size = 0u;
    assert(astra_posix_pack_ifconf(addresses, 2u, &config) == -1);
    assert(errno == EIO);
    return 0;
}
