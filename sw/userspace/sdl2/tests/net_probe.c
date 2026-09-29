#include <SDL_net.h>
#include <astra/runtime.h>
#include <netdb.h>

#include <string.h>

static int fail(const char *message)
{
    (void)astra_log(message);
    return 1;
}

int main(void)
{
    static const unsigned char loopback[4] = {127u, 0u, 0u, 1u};
    IPaddress resolved, addresses[32], peer;
    UDPsocket socket = NULL;
    UDPpacket *outgoing = NULL, *incoming = NULL;
    IPaddress *local;
    struct hostent *reverse;
    int count, found = 0, result = 1;

    if (SDLNet_Init() != 0)
        return fail("SDL_NET_INIT_FAIL");
    if (SDLNet_ResolveHost(&resolved, "localhost", 0u) != 0) {
        result = fail("SDL_NET_FORWARD_FAIL");
        goto done;
    }
    reverse = gethostbyaddr(loopback, sizeof(loopback), AF_INET);
    if (reverse == NULL || reverse->h_name == NULL ||
        reverse->h_name[0] == '\0') {
        result = fail("SDL_NET_REVERSE_FAIL");
        goto done;
    }
    count = SDLNet_GetLocalAddresses(addresses, 32);
    if (count <= 0) {
        result = fail("SDL_NET_INTERFACES_FAIL");
        goto done;
    }
    for (int index = 0; index < count && index < 32; ++index)
        if (addresses[index].host != 0u)
            found = 1;
    if (!found) {
        result = fail("SDL_NET_NO_IPV4_FAIL");
        goto done;
    }
    socket = SDLNet_UDP_Open(0u);
    local = socket == NULL ? NULL : SDLNet_UDP_GetPeerAddress(socket, -1);
    if (local == NULL || local->port == 0u ||
        SDLNet_ResolveHost(&peer, "127.0.0.1",
                           SDLNet_Read16(&local->port)) != 0) {
        result = fail("SDL_NET_UDP_OPEN_FAIL");
        goto done;
    }
    outgoing = SDLNet_AllocPacket(16);
    incoming = SDLNet_AllocPacket(16);
    if (outgoing == NULL || incoming == NULL) {
        result = fail("SDL_NET_PACKET_ALLOC_FAIL");
        goto done;
    }
    (void)memcpy(outgoing->data, "ping", 4u);
    outgoing->len = 4;
    outgoing->address = peer;
    if (SDLNet_UDP_Send(socket, -1, outgoing) != 1) {
        result = fail("SDL_NET_UDP_SEND_FAIL");
        goto done;
    }
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (SDLNet_UDP_Recv(socket, incoming) == 1 &&
            incoming->len == 4 &&
            memcmp(incoming->data, "ping", 4u) == 0) {
            result = 0;
            break;
        }
        SDL_Delay(5u);
    }
    if (result != 0)
        (void)astra_log("SDL_NET_UDP_RECEIVE_FAIL");
done:
    if (incoming != NULL)
        SDLNet_FreePacket(incoming);
    if (outgoing != NULL)
        SDLNet_FreePacket(outgoing);
    if (socket != NULL)
        SDLNet_UDP_Close(socket);
    SDLNet_Quit();
    if (result == 0)
        (void)astra_log("SDL_NET_READY");
    return result;
}
