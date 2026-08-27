#include <astra/ping_core.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    static const uint8_t rfc1071[] = {
        0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7
    };
    static const uint8_t odd[] = {0x01, 0x02, 0x03};

    assert(astra_ping_checksum(rfc1071, sizeof(rfc1071)) == 0x220du);
    assert(astra_ping_checksum(odd, sizeof(odd)) == 0xfbfd);
    assert(astra_ping_checksum(NULL, 0u) == 0xffffu);
    return 0;
}
