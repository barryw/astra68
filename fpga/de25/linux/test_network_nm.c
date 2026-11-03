#include "astra_network_nm.h"

#include <assert.h>
#include <string.h>

static GVariant *section(GVariant *profile, const char *name)
{
    GVariant *value = g_variant_lookup_value(profile, name,
                                             G_VARIANT_TYPE("a{sv}"));
    assert(value != NULL);
    return value;
}

int main(void)
{
    AstraNetworkEthernetIPv4 candidate = {.size = sizeof(candidate)};
    GVariant *profile, *ipv4, *connection, *addresses, *first;
    const char *text;
    uint32_t prefix;
    gboolean enabled;

    memcpy(candidate.name, "eth0", 5u);
    candidate.method = ASTRA_NETWORK_ETHERNET_IPV4_DHCP;
    candidate.dns_count = 1u;
    candidate.dns[0][0] = 192u;
    candidate.dns[0][1] = 168u;
    candidate.dns[0][2] = 1u;
    candidate.dns[0][3] = 3u;
    profile = astra_network_nm_profile(&candidate,
        "00000000-0000-4000-8000-000000000001", FALSE);
    assert(profile != NULL && g_variant_is_normal_form(profile));
    connection = section(profile, "connection");
    assert(g_variant_lookup(connection, "interface-name", "&s", &text));
    assert(strcmp(text, "eth0") == 0);
    assert(g_variant_lookup(connection, "autoconnect", "b", &enabled));
    assert(!enabled);
    g_variant_unref(connection);
    ipv4 = section(profile, "ipv4");
    assert(g_variant_lookup(ipv4, "method", "&s", &text));
    assert(strcmp(text, "auto") == 0);
    assert(g_variant_lookup(ipv4, "ignore-auto-dns", "b", &enabled));
    assert(enabled);
    addresses = g_variant_lookup_value(ipv4, "dns-data",
                                        G_VARIANT_TYPE("as"));
    assert(addresses != NULL && g_variant_n_children(addresses) == 1u);
    g_variant_get_child(addresses, 0u, "&s", &text);
    assert(strcmp(text, "192.168.1.3") == 0);
    g_variant_unref(addresses);
    g_variant_unref(ipv4);
    g_variant_unref(profile);

    candidate.method = ASTRA_NETWORK_ETHERNET_IPV4_MANUAL;
    candidate.address[0] = 192u;
    candidate.address[1] = 168u;
    candidate.address[2] = 1u;
    candidate.address[3] = 52u;
    candidate.prefix = 24u;
    candidate.gateway[0] = 192u;
    candidate.gateway[1] = 168u;
    candidate.gateway[2] = 1u;
    candidate.gateway[3] = 1u;
    profile = astra_network_nm_profile(&candidate,
        "00000000-0000-4000-8000-000000000001", TRUE);
    assert(profile != NULL && g_variant_is_normal_form(profile));
    ipv4 = section(profile, "ipv4");
    connection = section(profile, "connection");
    assert(g_variant_lookup(connection, "autoconnect", "b", &enabled));
    assert(enabled);
    g_variant_unref(connection);
    assert(g_variant_lookup(ipv4, "method", "&s", &text));
    assert(strcmp(text, "manual") == 0);
    assert(g_variant_lookup(ipv4, "gateway", "&s", &text));
    assert(strcmp(text, "192.168.1.1") == 0);
    addresses = g_variant_lookup_value(ipv4, "address-data",
                                        G_VARIANT_TYPE("aa{sv}"));
    assert(addresses != NULL && g_variant_n_children(addresses) == 1u);
    first = g_variant_get_child_value(addresses, 0u);
    assert(g_variant_lookup(first, "address", "&s", &text));
    assert(strcmp(text, "192.168.1.52") == 0);
    assert(g_variant_lookup(first, "prefix", "u", &prefix));
    assert(prefix == 24u);
    g_variant_unref(first);
    g_variant_unref(addresses);
    g_variant_unref(ipv4);
    g_variant_unref(profile);

    candidate.prefix = 33u;
    assert(astra_network_nm_profile(&candidate,
        "00000000-0000-4000-8000-000000000001", FALSE) == NULL);
    return 0;
}
