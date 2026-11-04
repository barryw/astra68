#include <astra/network_library.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/secret_memory.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTRA_PROGRAM("shares", 1, 0, 0, "Barry Walker", "Copyright 2026 Barry Walker");

static int usage(void)
{
    fputs("usage: shares [list | show ID | remove ID | password ID | forget-password ID]\n"
          "       shares add nfs SERVER EXPORT LABEL\n"
          "       shares add smb SERVER /SHARE LABEL [ACCOUNT [DOMAIN]]\n"
          "       shares set ID FIELD VALUE [--password]\n"
          "FIELD: server, share, label, account, domain, auto, read-only\n"
          "VALUE for auto/read-only: on or off. Password is prompted, never an argument.\n", stderr);
    return 2;
}

static int parse_id(const char *text, uint32_t *id)
{
    char *end = NULL;
    unsigned long value;
    if (text == NULL || text[0] == '\0' || text[0] == '-') return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || value == 0ul || value >= UINT32_MAX)
        return 0;
    *id = (uint32_t)value;
    return 1;
}

static int copy_field(char *field, size_t size, const char *value)
{
    size_t length = strlen(value);
    if (length >= size) return 0;
    memcpy(field, value, length + 1u);
    return 1;
}

static AstraNetworkStatus find_mapping(AstraNetworkSession *session,
    uint32_t id, AstraNetworkMapping *mapping)
{
    uint32_t count = 0u;
    for (uint32_t index = 0u;; ++index) {
        AstraNetworkStatus status = astra_network_mapping_read(session,
            index, mapping, &count);
        if (status != ASTRA_NETWORK_OK) return status;
        if (mapping->id == id) return ASTRA_NETWORK_OK;
        if (index + 1u >= count) return ASTRA_NETWORK_NAME_NOT_FOUND;
    }
}

static void print_mapping(const AstraNetworkMapping *mapping)
{
    printf("%u %s %s %s %s auto=%s read-only=%s credential=%s account=%s domain=%s\n",
        (unsigned)mapping->id,
        mapping->source.format == ASTRA_VOLUME_SMB ? "smb" : "nfs",
        mapping->source.server, mapping->source.share, mapping->source.label,
        (mapping->flags & ASTRA_NETWORK_MAPPING_AUTO_CONNECT) ? "on" : "off",
        (mapping->flags & ASTRA_NETWORK_MAPPING_READ_ONLY) ? "on" : "off",
        (mapping->flags & ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL) ? "saved" : "none",
        mapping->account, mapping->domain);
}

static int list_mappings(AstraNetworkSession *session)
{
    for (uint32_t index = 0u;; ++index) {
        AstraNetworkMapping mapping = {0};
        uint32_t count = 0u;
        AstraNetworkStatus status = astra_network_mapping_read(session,
            index, &mapping, &count);
        if (status == ASTRA_NETWORK_NAME_NOT_FOUND && index == count) return 1;
        if (status != ASTRA_NETWORK_OK) {
            fprintf(stderr, "shares: list failed (%u)\n", (unsigned)status);
            return 0;
        }
        print_mapping(&mapping);
    }
}

static int change_field(AstraNetworkMapping *mapping,
    const char *field, const char *value)
{
    if (strcmp(field, "server") == 0)
        return copy_field(mapping->source.server,
            sizeof(mapping->source.server), value);
    if (strcmp(field, "share") == 0)
        return copy_field(mapping->source.share,
            sizeof(mapping->source.share), value);
    if (strcmp(field, "label") == 0)
        return copy_field(mapping->source.label,
            sizeof(mapping->source.label), value);
    if (strcmp(field, "account") == 0)
        return copy_field(mapping->account, sizeof(mapping->account), value);
    if (strcmp(field, "domain") == 0)
        return copy_field(mapping->domain, sizeof(mapping->domain), value);
    if (strcmp(field, "auto") == 0 || strcmp(field, "read-only") == 0) {
        uint32_t flag = strcmp(field, "auto") == 0 ?
            ASTRA_NETWORK_MAPPING_AUTO_CONNECT : ASTRA_NETWORK_MAPPING_READ_ONLY;
        if (strcmp(value, "on") == 0) mapping->flags |= flag;
        else if (strcmp(value, "off") == 0) mapping->flags &= ~flag;
        else return 0;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const AstraStartupCapability *admin = astra_startup_capability(
        astra_posix_startup(), ASTRA_CAPABILITY_NETWORK_ADMIN);
    AstraNetworkSession session = ASTRA_NETWORK_SESSION_INIT;
    AstraNetworkMappingSave save = {0};
    AstraNetworkStatus status = ASTRA_NETWORK_INVALID;
    uint32_t id = 0u;
    int success = 0;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) return usage();
    if (admin == NULL || astra_network_session_open(admin->handle, &session) !=
            ASTRA_NETWORK_OK) {
        fputs("shares: network admin unavailable\n", stderr);
        return 1;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "list") == 0)) {
        success = list_mappings(&session);
        goto done;
    }
    if (argc == 3 && parse_id(argv[2], &id) &&
        strcmp(argv[1], "show") == 0) {
        status = find_mapping(&session, id, &save.mapping);
        if (status == ASTRA_NETWORK_OK) {
            AstraNetworkMappingVolume volume = {0};
            print_mapping(&save.mapping);
            status = astra_network_mapping_state(&session, id, &volume);
            if (status == ASTRA_NETWORK_OK) {
                printf("state=%s mount=%llu status=%u\n",
                    volume.mount.id == 0u ? "not-mounted" :
                    volume.state == ASTRA_VOLUME_MOUNTED ? "mounted" :
                    volume.state == ASTRA_VOLUME_MOUNTING ? "connecting" :
                    volume.state == ASTRA_VOLUME_QUIESCING ? "disconnecting" :
                    volume.state == ASTRA_VOLUME_FAILED ? "failed" :
                    volume.state == ASTRA_VOLUME_LOST ? "lost" : "unmounted",
                    (unsigned long long)volume.mount.id,
                    (unsigned)volume.status);
                success = 1;
            }
        }
        goto result;
    }
    if (argc == 3 && parse_id(argv[2], &id) &&
        strcmp(argv[1], "remove") == 0) {
        status = astra_network_mapping_remove(&session, id);
        if (status == ASTRA_NETWORK_OK) {
            printf("removed %u\n", (unsigned)id);
            success = 1;
        }
        goto result;
    }
    if (argc >= 6 && strcmp(argv[1], "add") == 0 &&
        ((argc == 6 && strcmp(argv[2], "nfs") == 0) ||
         (argc <= 8 && strcmp(argv[2], "smb") == 0))) {
        save.mapping.size = sizeof(save.mapping);
        save.mapping.source.format = strcmp(argv[2], "smb") == 0 ?
            ASTRA_VOLUME_SMB : ASTRA_VOLUME_NFS;
        if (!copy_field(save.mapping.source.server,
                sizeof(save.mapping.source.server), argv[3]) ||
            !copy_field(save.mapping.source.share,
                sizeof(save.mapping.source.share), argv[4]) ||
            !copy_field(save.mapping.source.label,
                sizeof(save.mapping.source.label), argv[5]) ||
            (argc >= 7 && !copy_field(save.mapping.account,
                sizeof(save.mapping.account), argv[6])) ||
            (argc == 8 && !copy_field(save.mapping.domain,
                sizeof(save.mapping.domain), argv[7]))) goto invalid;
        if (save.mapping.account[0] != '\0') {
            if (astra_posix_read_secret("SMB password: ", save.password,
                    sizeof(save.password)) != 0 || save.password[0] == '\0') {
                fputs("shares: password required for account\n", stderr);
                goto done;
            }
            save.password_action = ASTRA_NETWORK_PASSWORD_REPLACE;
        }
    } else if (argc >= 3 && parse_id(argv[2], &id) &&
        (strcmp(argv[1], "set") == 0 ||
         strcmp(argv[1], "password") == 0 ||
         strcmp(argv[1], "forget-password") == 0)) {
        status = find_mapping(&session, id, &save.mapping);
        if (status != ASTRA_NETWORK_OK) goto result;
        save.mapping.flags &= ~ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL;
        if (strcmp(argv[1], "set") == 0) {
            if ((argc != 5 && argc != 6) ||
                (argc == 6 && strcmp(argv[5], "--password") != 0) ||
                !change_field(&save.mapping, argv[3], argv[4])) goto invalid;
            if (argc == 6) save.password_action = ASTRA_NETWORK_PASSWORD_REPLACE;
        } else if (argc == 3 && strcmp(argv[1], "password") == 0)
            save.password_action = ASTRA_NETWORK_PASSWORD_REPLACE;
        else if (argc == 3 && strcmp(argv[1], "forget-password") == 0)
            save.password_action = ASTRA_NETWORK_PASSWORD_CLEAR;
        else goto invalid;
        if (save.password_action == ASTRA_NETWORK_PASSWORD_REPLACE &&
            (save.mapping.source.format != ASTRA_VOLUME_SMB ||
             save.mapping.account[0] == '\0')) {
            fputs("shares: SMB account required before setting password\n", stderr);
            goto done;
        }
        if (save.password_action == ASTRA_NETWORK_PASSWORD_REPLACE &&
            (astra_posix_read_secret("SMB password: ", save.password,
                sizeof(save.password)) != 0 || save.password[0] == '\0')) {
            fputs("shares: password required\n", stderr);
            goto done;
        }
    } else goto invalid;
    status = astra_network_mapping_save(&session, &save, &id);
    if (status == ASTRA_NETWORK_OK) {
        printf("saved %u\n", (unsigned)id);
        success = 1;
    }
result:
    if (!success) {
        fprintf(stderr, "shares: operation failed (%u)", (unsigned)status);
        if (status == ASTRA_NETWORK_BUSY)
            fputs("; unmount active share before changing or removing it", stderr);
        fputc('\n', stderr);
    }
    goto done;
invalid:
    (void)usage();
done:
    astra_secret_wipe(&save, sizeof(save));
    (void)astra_network_session_close(&session);
    return success && fflush(stdout) == 0 ? 0 : 1;
}
