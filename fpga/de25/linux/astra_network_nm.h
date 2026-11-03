#ifndef ASTRA_NETWORK_NM_H
#define ASTRA_NETWORK_NM_H

#include <astra/network.h>
#include <gio/gio.h>

typedef struct AstraNetworkNm {
    GDBusConnection *bus;
    char *checkpoint;
    char *connection;
    char *device;
    char *uuid;
    AstraNetworkEthernetIPv4 pending;
} AstraNetworkNm;

GVariant *astra_network_nm_profile(const AstraNetworkEthernetIPv4 *profile,
                                   const char *uuid, gboolean autoconnect);
int astra_network_nm_open(AstraNetworkNm *state);
int astra_network_nm_probe(AstraNetworkNm *state, const char *name);
int astra_network_nm_stage(AstraNetworkNm *state,
                           const AstraNetworkEthernetIPv4 *profile,
                           unsigned rollback_seconds);
int astra_network_nm_confirm(AstraNetworkNm *state);
int astra_network_nm_revert(AstraNetworkNm *state);
void astra_network_nm_close(AstraNetworkNm *state);

#endif
