#ifndef _NETINET_IP_ICMP_H
#define _NETINET_IP_ICMP_H

#include <stdint.h>

#define ICMP_ECHOREPLY 0
#define ICMP_ECHO 8
#define ICMP6_ECHO_REQUEST 128
#define ICMP6_ECHO_REPLY 129

struct icmp_echo_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
};

#endif
