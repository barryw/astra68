#define _GNU_SOURCE
#include "astra_network_nm.h"

#include <astra/network_library.h>

#include <errno.h>
#include <string.h>

#define NM_BUS "org.freedesktop.NetworkManager"
#define NM_PATH "/org/freedesktop/NetworkManager"
#define NM_INTERFACE "org.freedesktop.NetworkManager"
#define NM_SETTINGS_PATH "/org/freedesktop/NetworkManager/Settings"
#define NM_SETTINGS_INTERFACE "org.freedesktop.NetworkManager.Settings"
#define NM_CONNECTION_INTERFACE "org.freedesktop.NetworkManager.Settings.Connection"
#define NM_DEVICE_INTERFACE "org.freedesktop.NetworkManager.Device"
#define NM_ACTIVE_INTERFACE "org.freedesktop.NetworkManager.Connection.Active"

static GVariant *call(AstraNetworkNm *state, const char *path,
                      const char *interface, const char *method,
                      GVariant *arguments)
{
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(state->bus, NM_BUS,
        path, interface, method, arguments, NULL,
        G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
    if (result == NULL) {
        g_warning("NetworkManager %s: %s", method,
                  error == NULL ? "no reply" : error->message);
        g_clear_error(&error);
    }
    return result;
}

static char *ipv4_text(const uint8_t address[4])
{
    return g_strdup_printf("%u.%u.%u.%u", address[0], address[1],
                           address[2], address[3]);
}

GVariant *astra_network_nm_profile(const AstraNetworkEthernetIPv4 *profile,
                                   const char *uuid, gboolean autoconnect)
{
    GVariantBuilder settings, connection, ipv4, ipv6, dns, addresses, entry;
    char *id;

    if (astra_network_ethernet_ipv4_validate(profile) != ASTRA_NETWORK_OK ||
        uuid == NULL)
        return NULL;
    g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_init(&connection, G_VARIANT_TYPE("a{sv}"));
    id = g_strdup_printf("Astra %s", profile->name);
    g_variant_builder_add(&connection, "{sv}", "id",
                          g_variant_new_string(id));
    g_variant_builder_add(&connection, "{sv}", "uuid",
                          g_variant_new_string(uuid));
    g_variant_builder_add(&connection, "{sv}", "type",
                          g_variant_new_string("802-3-ethernet"));
    g_variant_builder_add(&connection, "{sv}", "interface-name",
                          g_variant_new_string(profile->name));
    g_variant_builder_add(&connection, "{sv}", "autoconnect",
                          g_variant_new_boolean(autoconnect));
    if (autoconnect)
        g_variant_builder_add(&connection, "{sv}", "autoconnect-priority",
                              g_variant_new_int32(100));
    g_variant_builder_add(&settings, "{s@a{sv}}", "connection",
                          g_variant_builder_end(&connection));
    g_free(id);

    g_variant_builder_init(&ipv4, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&ipv4, "{sv}", "method", g_variant_new_string(
        profile->method == ASTRA_NETWORK_ETHERNET_IPV4_DHCP ?
        "auto" : "manual"));
    if (profile->method == ASTRA_NETWORK_ETHERNET_IPV4_MANUAL) {
        char *address = ipv4_text(profile->address);
        g_variant_builder_init(&addresses, G_VARIANT_TYPE("aa{sv}"));
        g_variant_builder_init(&entry, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&entry, "{sv}", "address",
                              g_variant_new_string(address));
        g_variant_builder_add(&entry, "{sv}", "prefix",
                              g_variant_new_uint32(profile->prefix));
        g_variant_builder_add_value(&addresses, g_variant_builder_end(&entry));
        g_variant_builder_add(&ipv4, "{sv}", "address-data",
                              g_variant_builder_end(&addresses));
        g_free(address);
        if (memcmp(profile->gateway, (uint8_t[4]){0}, 4u) != 0) {
            char *gateway = ipv4_text(profile->gateway);
            g_variant_builder_add(&ipv4, "{sv}", "gateway",
                                  g_variant_new_string(gateway));
            g_free(gateway);
        }
    }
    if (profile->dns_count != 0u) {
        g_variant_builder_init(&dns, G_VARIANT_TYPE("as"));
        for (uint32_t at = 0u; at < profile->dns_count; ++at) {
            char *server = ipv4_text(profile->dns[at]);
            g_variant_builder_add(&dns, "s", server);
            g_free(server);
        }
        g_variant_builder_add(&ipv4, "{sv}", "dns-data",
                              g_variant_builder_end(&dns));
        if (profile->method == ASTRA_NETWORK_ETHERNET_IPV4_DHCP)
            g_variant_builder_add(&ipv4, "{sv}", "ignore-auto-dns",
                                  g_variant_new_boolean(TRUE));
    }
    g_variant_builder_add(&settings, "{s@a{sv}}", "ipv4",
                          g_variant_builder_end(&ipv4));
    g_variant_builder_init(&ipv6, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&ipv6, "{sv}", "method",
                          g_variant_new_string("auto"));
    g_variant_builder_add(&settings, "{s@a{sv}}", "ipv6",
                          g_variant_builder_end(&ipv6));
    return g_variant_ref_sink(g_variant_builder_end(&settings));
}

int astra_network_nm_open(AstraNetworkNm *state)
{
    GError *error = NULL;
    if (state == NULL || state->bus != NULL)
        return EINVAL;
    state->bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (state->bus != NULL)
        return 0;
    g_warning("NetworkManager bus: %s",
              error == NULL ? "unavailable" : error->message);
    g_clear_error(&error);
    return ENETDOWN;
}

static void clear_pending(AstraNetworkNm *state)
{
    g_clear_pointer(&state->checkpoint, g_free);
    g_clear_pointer(&state->connection, g_free);
    g_clear_pointer(&state->device, g_free);
    g_clear_pointer(&state->uuid, g_free);
    memset(&state->pending, 0, sizeof(state->pending));
}

static int checkpoint_exists(AstraNetworkNm *state, const char *target)
{
    GVariant *reply = call(state, NM_PATH,
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", NM_INTERFACE, "Checkpoints"));
    if (reply == NULL) return -1;
    GVariant *boxed = g_variant_get_child_value(reply, 0u);
    GVariant *paths = g_variant_get_variant(boxed);
    GVariantIter iter;
    const char *path;
    int found = 0;
    g_variant_iter_init(&iter, paths);
    while (g_variant_iter_next(&iter, "&o", &path))
        if (target == NULL || strcmp(path, target) == 0) found = 1;
    g_variant_unref(paths);
    g_variant_unref(boxed);
    g_variant_unref(reply);
    return found;
}

static int find_ethernet(AstraNetworkNm *state, const char *name, char **device)
{
    GVariant *result, *variant, *type;
    const char *path;
    uint32_t kind;

    result = call(state, NM_PATH, NM_INTERFACE, "GetDeviceByIpIface",
                  g_variant_new("(s)", name));
    if (result == NULL)
        return ENODEV;
    g_variant_get(result, "(&o)", &path);
    *device = g_strdup(path);
    g_variant_unref(result);
    result = call(state, *device, "org.freedesktop.DBus.Properties", "Get",
                  g_variant_new("(ss)", NM_DEVICE_INTERFACE, "DeviceType"));
    if (result == NULL) {
        g_clear_pointer(device, g_free);
        return EIO;
    }
    variant = g_variant_get_child_value(result, 0u);
    type = g_variant_get_variant(variant);
    kind = g_variant_get_uint32(type);
    g_variant_unref(type);
    g_variant_unref(variant);
    g_variant_unref(result);
    if (kind != 1u && kind != 20u) {
        g_clear_pointer(device, g_free);
        return EPROTOTYPE;
    }
    return 0;
}

static int await_activation(AstraNetworkNm *state, const char *path)
{
    gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (g_get_monotonic_time() < deadline) {
        GVariant *reply = call(state, path,
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", NM_ACTIVE_INTERFACE, "State"));
        if (reply == NULL) return EIO;
        GVariant *boxed = g_variant_get_child_value(reply, 0u);
        GVariant *value = g_variant_get_variant(boxed);
        uint32_t status = g_variant_get_uint32(value);
        g_variant_unref(value);
        g_variant_unref(boxed);
        g_variant_unref(reply);
        if (status == 2u) return 0;
        if (status >= 3u) return ENETDOWN;
        g_usleep(50000u);
    }
    return ETIMEDOUT;
}

int astra_network_nm_probe(AstraNetworkNm *state, const char *name)
{
    char *device = NULL;
    size_t length;
    int result;

    if (state == NULL || state->bus == NULL || name == NULL)
        return EINVAL;
    length = strnlen(name, ASTRA_NETWORK_INTERFACE_NAME_SIZE);
    if (length == 0u || length == ASTRA_NETWORK_INTERFACE_NAME_SIZE)
        return EINVAL;
    result = find_ethernet(state, name, &device);
    g_free(device);
    return result;
}

int astra_network_nm_revert(AstraNetworkNm *state)
{
    GVariant *result;
    if (state == NULL || state->bus == NULL || state->checkpoint == NULL)
        return EINVAL;
    int present = checkpoint_exists(state, state->checkpoint);
    if (present == 0) {
        clear_pending(state);
        return 0;
    }
    if (present < 0) return EIO;
    result = call(state, NM_PATH, NM_INTERFACE, "CheckpointRollback",
                  g_variant_new("(o)", state->checkpoint));
    if (result == NULL)
        return EIO;
    GVariant *outcomes = g_variant_get_child_value(result, 0u);
    GVariantIter iter;
    const char *device;
    uint32_t outcome;
    int failed = 0;
    g_variant_iter_init(&iter, outcomes);
    while (g_variant_iter_next(&iter, "{&su}", &device, &outcome))
        if (outcome != 0u) failed = 1;
    g_variant_unref(outcomes);
    g_variant_unref(result);
    clear_pending(state);
    return failed ? EIO : 0;
}

int astra_network_nm_stage(AstraNetworkNm *state,
                           const AstraNetworkEthernetIPv4 *profile,
                           unsigned rollback_seconds)
{
    GVariant *result, *settings, *options;
    const char *path;
    const char *devices[1];

    if (state == NULL || state->bus == NULL || rollback_seconds < 30u ||
        rollback_seconds > 300u ||
        astra_network_ethernet_ipv4_validate(profile) != ASTRA_NETWORK_OK)
        return EINVAL;
    if (state->checkpoint != NULL) {
        int present = checkpoint_exists(state, state->checkpoint);
        if (present != 0) return present < 0 ? EIO : EBUSY;
        clear_pending(state);
    }
    /* A previous helper may have died while its checkpoint still owns rollback. */
    int other = checkpoint_exists(state, NULL);
    if (other != 0) return other < 0 ? EIO : EBUSY;
    int found = find_ethernet(state, profile->name, &state->device);
    if (found != 0)
        return found;
    devices[0] = state->device;
    result = call(state, NM_PATH, NM_INTERFACE, "CheckpointCreate",
        g_variant_new("(@aouu)", g_variant_new_objv(devices, 1),
                      rollback_seconds, 2u));
    if (result == NULL) {
        clear_pending(state);
        return EIO;
    }
    g_variant_get(result, "(&o)", &path);
    state->checkpoint = g_strdup(path);
    g_variant_unref(result);

    state->uuid = g_uuid_string_random();
    state->pending = *profile;
    settings = astra_network_nm_profile(profile, state->uuid, FALSE);
    options = g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
    result = call(state, NM_SETTINGS_PATH, NM_SETTINGS_INTERFACE,
                  "AddConnection2", g_variant_new("(@a{sa{sv}}u@a{sv})",
                                                  settings, 2u, options));
    if (result == NULL) {
        (void)astra_network_nm_revert(state);
        return EIO;
    }
    g_variant_get_child(result, 0, "&o", &path);
    state->connection = g_strdup(path);
    g_variant_unref(result);
    result = call(state, NM_PATH, NM_INTERFACE, "ActivateConnection",
                  g_variant_new("(ooo)", state->connection,
                                state->device, "/"));
    if (result == NULL) {
        (void)astra_network_nm_revert(state);
        return EIO;
    }
    g_variant_get(result, "(&o)", &path);
    int active = await_activation(state, path);
    g_variant_unref(result);
    if (active != 0) (void)astra_network_nm_revert(state);
    return active;
}

int astra_network_nm_confirm(AstraNetworkNm *state)
{
    GVariant *result, *settings, *options;
    if (state == NULL || state->bus == NULL || state->checkpoint == NULL ||
        state->connection == NULL)
        return EINVAL;
    int present = checkpoint_exists(state, state->checkpoint);
    if (present == 0) {
        clear_pending(state);
        return ETIMEDOUT;
    }
    if (present < 0) return EIO;
    settings = astra_network_nm_profile(&state->pending, state->uuid, TRUE);
    options = g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
    result = call(state, state->connection, NM_CONNECTION_INTERFACE,
                  "Update2", g_variant_new("(@a{sa{sv}}u@a{sv})",
                                           settings, 1u, options));
    if (result == NULL)
        return EIO;
    g_variant_unref(result);
    result = call(state, NM_PATH, NM_INTERFACE, "CheckpointDestroy",
                  g_variant_new("(o)", state->checkpoint));
    if (result == NULL)
        return EIO;
    g_variant_unref(result);
    /* One managed connection per interface after successful cutover. */
    result = call(state, NM_SETTINGS_PATH, NM_SETTINGS_INTERFACE,
                  "ListConnections", g_variant_new("()"));
    if (result != NULL) {
        char *expected_id = g_strdup_printf("Astra %s", state->pending.name);
        GVariant *paths = g_variant_get_child_value(result, 0u);
        GVariantIter iter;
        const char *path;
        g_variant_iter_init(&iter, paths);
        while (g_variant_iter_next(&iter, "&o", &path)) {
            GVariant *stored, *connection, *found;
            const char *id = NULL, *name = NULL, *kind = NULL;
            if (strcmp(path, state->connection) == 0) continue;
            found = call(state, path, NM_CONNECTION_INTERFACE,
                         "GetSettings", g_variant_new("()"));
            if (found == NULL) continue;
            stored = g_variant_get_child_value(found, 0u);
            connection = g_variant_lookup_value(stored, "connection",
                                                 G_VARIANT_TYPE("a{sv}"));
            if (connection != NULL &&
                g_variant_lookup(connection, "id", "&s", &id) &&
                g_variant_lookup(connection, "interface-name", "&s", &name) &&
                g_variant_lookup(connection, "type", "&s", &kind) &&
                g_strcmp0(id, expected_id) == 0 &&
                g_strcmp0(name, state->pending.name) == 0 &&
                g_strcmp0(kind, "802-3-ethernet") == 0) {
                GVariant *deleted = call(state, path,
                    NM_CONNECTION_INTERFACE, "Delete", g_variant_new("()"));
                if (deleted != NULL) g_variant_unref(deleted);
            }
            if (connection != NULL) g_variant_unref(connection);
            g_variant_unref(stored);
            g_variant_unref(found);
        }
        g_variant_unref(paths);
        g_variant_unref(result);
        g_free(expected_id);
    }
    clear_pending(state);
    return 0;
}

void astra_network_nm_close(AstraNetworkNm *state)
{
    if (state == NULL)
        return;
    if (state->checkpoint != NULL)
        (void)astra_network_nm_revert(state);
    clear_pending(state);
    g_clear_object(&state->bus);
}
