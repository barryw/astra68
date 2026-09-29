#ifndef _NET_IF_H
#define _NET_IF_H

#include <sys/socket.h>
#include <sys/types.h>

#define IFNAMSIZ 16
#define SIOCGIFCONF 0x8912UL

struct ifreq {
    char ifr_name[IFNAMSIZ];
    struct sockaddr ifr_addr;
};

struct ifconf {
    int ifc_len;
    union {
        char *ifcu_buf;
        struct ifreq *ifcu_req;
    } ifc_ifcu;
};

#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

#endif
