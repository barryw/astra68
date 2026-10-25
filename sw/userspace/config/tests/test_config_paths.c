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
    puts("configuration rooted grant, maximum startup prefix and bounded document paths PASS");
    return 0;
}
