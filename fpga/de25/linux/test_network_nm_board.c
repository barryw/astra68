#include "astra_network_nm.h"

#include <stdio.h>
#include <string.h>

static int delete_connection(AstraNetworkNm *state, const char *path)
{
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(state->bus,
        "org.freedesktop.NetworkManager", path,
        "org.freedesktop.NetworkManager.Settings.Connection", "Delete",
        g_variant_new("()"), NULL, G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
    if (reply == NULL) {
        g_clear_error(&error);
        return 1;
    }
    g_variant_unref(reply);
    return 0;
}

int main(int argc, char **argv)
{
    AstraNetworkNm state = {0};
    int result;

    if (argc != 2 && argc != 3)
        return 2;
    if (argc == 3 &&
        (strcmp(argv[1], "--trial") != 0 &&
         strcmp(argv[1], "--trial-timeout") != 0 &&
         strcmp(argv[1], "--trial-confirm") != 0))
        return 2;
    if (argc == 3 && strcmp(argv[2], "astra-test0") != 0)
        return 2;
    result = astra_network_nm_open(&state);
    if (result == 0 && argc == 3) {
        AstraNetworkEthernetIPv4 candidate = {.size = sizeof(candidate),
            .name = "astra-test0",
            .method = ASTRA_NETWORK_ETHERNET_IPV4_MANUAL,
            .address = {198u, 18u, 0u, 2u}, .prefix = 30u};
        result = astra_network_nm_stage(&state, &candidate, 30u);
        if (result == 0 && strcmp(argv[1], "--trial-timeout") == 0) {
            g_usleep(32000000u);
            result = astra_network_nm_revert(&state);
            if (result == 0)
                result = astra_network_nm_stage(&state, &candidate, 30u);
        }
        if (result == 0 && strcmp(argv[1], "--trial-confirm") == 0) {
            char *path = g_strdup(state.connection);
            result = astra_network_nm_confirm(&state);
            if (delete_connection(&state, path) != 0) result = 1;
            g_free(path);
        } else if (result == 0)
            result = astra_network_nm_revert(&state);
    } else if (result == 0)
        result = astra_network_nm_probe(&state, argv[1]);
    astra_network_nm_close(&state);
    if (result != 0) {
        fprintf(stderr, "NetworkManager probe %s failed: %d\n",
                argv[1], result);
        return 1;
    }
    printf("NetworkManager Ethernet %s: PASS\n",
           argc == 3 ? argv[1] : "probe");
    return 0;
}
