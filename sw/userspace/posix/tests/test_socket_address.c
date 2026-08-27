#include <arpa/inet.h>

#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t address[16];
    char text[INET6_ADDRSTRLEN];

    assert(inet_pton(AF_INET, "192.0.2.25", address) == 1);
    assert(address[0] == 192u && address[1] == 0u &&
           address[2] == 2u && address[3] == 25u);
    assert(inet_ntop(AF_INET, address, text, sizeof(text)) == text);
    assert(strcmp(text, "192.0.2.25") == 0);
    assert(inet_pton(AF_INET, "256.0.0.1", address) == 0);

    assert(inet_pton(AF_INET6, "2001:db8::1", address) == 1);
    assert(inet_ntop(AF_INET6, address, text, sizeof(text)) == text);
    assert(strcmp(text, "2001:db8::1") == 0);
    assert(inet_pton(AF_INET6, "::ffff:192.0.2.1", address) == 1);
    assert(inet_pton(AF_INET6, "2001:::1", address) == 0);
    return 0;
}
