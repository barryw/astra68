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
    char secret_key[128];
    char maximum[122];
    memset(maximum, 'x', 120u);
    maximum[120] = '\0';
    assert(secret_name(maximum, secret_key));
    assert(strlen(secret_key) == 127u);
    maximum[120] = 'x'; maximum[121] = '\0';
    assert(!secret_name(maximum, secret_key));
    assert(!secret_name("../private", secret_key));
    assert(!secret_name("", secret_key));
    assert(secret_name("smb.password", secret_key));
    assert(strcmp(secret_key, "secret.smb.password") == 0);
    assert(hex_digit('f') == 15 && hex_digit('F') < 0);
    puts("configuration paths and bounded secret keys PASS");
    return 0;
}
