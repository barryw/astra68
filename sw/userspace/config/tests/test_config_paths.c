#include "../src/config_library.c"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    char root[ASTRA_CAPABILITY_ROOT_MAX + 1u];
    char normalized[CONFIG_ROOT_CAPACITY];
    struct {
        char document[CONFIG_PATH_CAPACITY];
        unsigned char guard;
    } output = {.guard = 0xa5u};
    memset(root, 'r', ASTRA_CAPABILITY_ROOT_MAX - 1u);
    root[ASTRA_CAPABILITY_ROOT_MAX - 1u] = '\0';
    assert(root_copy(normalized, root));
    assert(normalized[0] == '/' && strlen(normalized) == ASTRA_CAPABILITY_ROOT_MAX);
    assert(path(output.document, sizeof(output.document), normalized, "settings.conf"));
    assert(strlen(output.document) == ASTRA_CAPABILITY_ROOT_MAX + 1u + strlen("settings.conf"));
    assert(output.guard == 0xa5u);
    assert(!path(output.document, sizeof(output.document) - 1u, normalized, "settings.conf"));
    assert(output.guard == 0xa5u);
    assert(path(output.document, sizeof(output.document), normalized, ".settings.tmp"));
    assert(output.guard == 0xa5u);
    memset(root, 'r', ASTRA_CAPABILITY_ROOT_MAX);
    root[ASTRA_CAPABILITY_ROOT_MAX] = '\0';
    assert(!root_copy(normalized, root));
    assert(root_copy(normalized, ""));
    assert(path(output.document, sizeof(output.document), normalized, "settings.conf"));
    assert(strcmp(output.document, "/settings.conf") == 0);
    assert(root_copy(normalized, "/root///") && strcmp(normalized, "/root") == 0);
    const AstraConfigField fields[] = {{"network.server", 0u},
        {"network.password", ASTRA_CONFIG_FIELD_SENSITIVE}};
    const AstraConfigSchema schema = {1u, fields, 2u};
    assert(schema_valid(&schema));
    assert(!public_schema_valid(&schema));
    const AstraConfigSchema public_schema = {1u, fields, 1u};
    assert(public_schema_valid(&public_schema));
    const ConfigState state = {.schema = &schema};
    assert(schema_field(&state, "network.server") == &fields[0]);
    assert(schema_field(&state, "network.password") == &fields[1]);
    assert(schema_field(&state, "other") == NULL);
    const AstraConfigField duplicate[] = {{"network.server", 0u},
        {"network.server", ASTRA_CONFIG_FIELD_SENSITIVE}};
    const AstraConfigSchema bad = {1u, duplicate, 2u};
    assert(!schema_valid(&bad));
    assert(!astra_config_document_key_valid("../private"));
    assert(hex_digit('f') == 15 && hex_digit('F') < 0);
    puts("configuration paths and owner schema PASS");
    return 0;
}
