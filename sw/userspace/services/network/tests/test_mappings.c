#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main.c"

enum { TEST_CAPACITY = 8u };
static char definitions[TEST_CAPACITY][ASTRA_NETWORK_MAPPING_TEXT_MAX];
static char credentials[TEST_CAPACITY][sizeof(((AstraNetworkMappingSave *)0)->password) + 3u];
static char next_id[16];
static uint32_t definition_count, credential_count, next_count, fail_apply;

static char *entry(const char *key, uint32_t index)
{
    if (strcmp(key, "mapping") == 0)
        return index < definition_count ? definitions[index] : NULL;
    if (strcmp(key, "mapping_secret") == 0)
        return index < credential_count ? credentials[index] : NULL;
    if (strcmp(key, "mapping_next") == 0)
        return index == 0u && next_count != 0u ? next_id : NULL;
    return NULL;
}

uint32_t astra_config_count(const AstraConfig *config, const char *key,
                            uint32_t *count)
{
    assert(config == &network_config && count != NULL);
    if (strcmp(key, "mapping") == 0) *count = definition_count;
    else if (strcmp(key, "mapping_secret") == 0) *count = credential_count;
    else if (strcmp(key, "mapping_next") == 0) *count = next_count;
    else abort();
    return ASTRA_CONFIG_OK;
}

uint32_t astra_config_get_string(const AstraConfig *config, const char *key,
    uint32_t index, char *value, uint32_t capacity, uint32_t *length)
{
    const char *source;
    assert(config == &network_config && length != NULL);
    source = entry(key, index);
    if (source == NULL) return ASTRA_CONFIG_NOT_FOUND;
    *length = (uint32_t)strlen(source);
    if (capacity <= *length) return ASTRA_CONFIG_BUFFER_TOO_SMALL;
    memcpy(value, source, *length + 1u);
    return ASTRA_CONFIG_OK;
}

uint32_t astra_config_get_u64(const AstraConfig *config, const char *key,
                              uint32_t index, uint64_t *value)
{
    const char *source;
    char *end;
    assert(config == &network_config && value != NULL);
    source = entry(key, index);
    if (source == NULL) return ASTRA_CONFIG_NOT_FOUND;
    *value = strtoull(source, &end, 10);
    return *end == '\0' ? ASTRA_CONFIG_OK : ASTRA_CONFIG_INVALID;
}

uint32_t astra_config_apply(AstraConfig *config,
    const AstraConfigChange *changes, uint32_t count)
{
    assert(config == &network_config && changes != NULL && count <= 3u);
    if (fail_apply) return ASTRA_CONFIG_IO;
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraConfigChange *change = &changes[at];
        char *target;
        uint32_t *used;
        size_t capacity;
        if (strcmp(change->key, "mapping") == 0) {
            used = &definition_count; capacity = sizeof(definitions[0]);
            target = definitions[change->index];
        } else if (strcmp(change->key, "mapping_secret") == 0) {
            used = &credential_count; capacity = sizeof(credentials[0]);
            target = credentials[change->index];
        } else {
            assert(strcmp(change->key, "mapping_next") == 0);
            used = &next_count; capacity = sizeof(next_id); target = next_id;
        }
        assert(change->index < TEST_CAPACITY);
        if (change->operation == ASTRA_CONFIG_CHANGE_APPEND) {
            assert(change->index == *used); ++*used;
        } else assert(change->index < *used);
        if (change->operation == ASTRA_CONFIG_CHANGE_REMOVE) {
            assert(change->value == NULL && strcmp(change->key, "mapping_next") != 0);
            if (strcmp(change->key, "mapping") == 0)
                memmove(definitions + change->index,
                    definitions + change->index + 1u,
                    (--*used - change->index) * sizeof(definitions[0]));
            else memmove(credentials + change->index,
                    credentials + change->index + 1u,
                    (--*used - change->index) * sizeof(credentials[0]));
        } else {
            assert(strlen(change->value) < capacity);
            strcpy(target, change->value);
        }
    }
    return ASTRA_CONFIG_OK;
}

static AstraNetworkMappingSave smb(const char *account)
{
    AstraNetworkMappingSave input = {0};
    input.mapping.size = sizeof(input.mapping);
    input.mapping.source.format = ASTRA_VOLUME_SMB;
    strcpy(input.mapping.source.server, "files.example");
    strcpy(input.mapping.source.share, "/games");
    strcpy(input.mapping.source.label, "Games");
    if (account != NULL) strcpy(input.mapping.account, account);
    return input;
}

int main(void)
{
    AstraNetworkMappingSave input;
    AstraNetworkMapping saved;
    uint32_t id;
    assert(network_fields[3].flags == ASTRA_CONFIG_FIELD_SENSITIVE);
    assert(mapping_integrity());

    input = smb(NULL);
    input.mapping.flags = ASTRA_NETWORK_MAPPING_AUTO_CONNECT;
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_OK && id == 1u);
    assert(mapping_integrity() && definition_count == 1u &&
           credential_count == 1u && strcmp(credentials[0], "n") == 0);
    assert(mapping_read(0u, &saved) && saved.id == id &&
           (saved.flags & ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL) == 0u);

    input = smb("barry");
    input.mapping.flags = ASTRA_NETWORK_MAPPING_AUTO_CONNECT;
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_INVALID);
    input.password_action = ASTRA_NETWORK_PASSWORD_REPLACE;
    strcpy(input.password, "p,a=s:word");
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_OK && id == 2u);
    assert(mapping_integrity() && strcmp(credentials[1], "p:p,a=s:word") == 0);
    assert(mapping_read(1u, &saved) &&
           (saved.flags & ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL) != 0u);

    input.mapping.id = id;
    input.password_action = ASTRA_NETWORK_PASSWORD_KEEP;
    memset(input.password, 0, sizeof(input.password));
    input.mapping.flags |= ASTRA_NETWORK_MAPPING_READ_ONLY;
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_OK && id == 2u);
    assert(strcmp(credentials[1], "p:p,a=s:word") == 0);
    strcpy(input.mapping.account, "other");
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_INVALID);
    strcpy(input.mapping.account, "barry");
    strcpy(input.mapping.source.server, "other.example");
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_INVALID);
    strcpy(input.mapping.source.server, "files.example");
    strcpy(input.mapping.source.share, "/other");
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_INVALID);
    strcpy(input.mapping.source.share, "/games");

    fail_apply = 1u;
    input.mapping.id = 0u;
    input.password_action = ASTRA_NETWORK_PASSWORD_REPLACE;
    strcpy(input.password, "new");
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_IO);
    assert(mapping_integrity() && definition_count == 2u &&
           strcmp(next_id, "3") == 0);
    fail_apply = 0u;

    AstraConfigChange remove[] = {
        {"mapping", NULL, 0u, ASTRA_CONFIG_CHANGE_REMOVE},
        {"mapping_secret", NULL, 0u, ASTRA_CONFIG_CHANGE_REMOVE}
    };
    assert(astra_config_apply(&network_config, remove, 2u) == ASTRA_CONFIG_OK);
    assert(mapping_integrity() && mapping_find(1u, &id, &saved) ==
           ASTRA_NETWORK_NAME_NOT_FOUND);
    assert(mapping_save(&input, &id) == ASTRA_NETWORK_OK && id == 3u);
    assert(mapping_integrity());

    strcpy(credentials[1], "broken");
    assert(!mapping_integrity());
    puts("network saved mappings: PASS");
    return 0;
}
