#include <service_definition_store.h>

#include <astra/status.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *astra_runtime_reallocate(void *pointer, size_t size)
{
    return realloc(pointer, size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

int main(void)
{
    static const char source[] =
        "# hand editable\n"
        "astra-config 1\n"
        "schema = 1\n"
        "name remote-desktop\n"
        "executable SERVICES:remote-desktop\n"
        "runs paired\n"
        "enabled true\n"
        "delegates false\n"
        "start manual\n"
        "restart on-fault\n"
        "argument SERVICES:remote-desktop\n"
        "grant NETWORK\n"
        "provides REMOTE\n"
        "needs NETWORK\n";
    AstraServiceDefinition first;
    AstraServiceDefinition second;
    char serialized[2048];
    uint32_t required = 0u;
    uint32_t line = 0u;

    assert(supervisor_service_definition_parse(
               source, sizeof(source) - 1u, &first, &line) ==
           ASTRA_STATUS_OK);
    assert(strcmp(first.name, "remote-desktop") == 0);
    assert((first.flags & ASTRA_SERVICE_RUNS_PAIRED) != 0u);
    assert(first.grant_count == 1u && first.publication_count == 1u &&
           first.dependency_count == 1u);
    assert(supervisor_service_definition_serialize(
               &first, NULL, 0u, &required) ==
           ASTRA_STATUS_BUFFER_TOO_SMALL);
    assert(required < sizeof(serialized));
    assert(supervisor_service_definition_serialize(
               &first, serialized, sizeof(serialized), &required) ==
           ASTRA_STATUS_OK);
    assert(supervisor_service_definition_parse(
               serialized, required - 1u, &second, &line) ==
           ASTRA_STATUS_OK);
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    {
        static const char linux_only[] =
            "astra-config 1\nschema 1\nruns linux\n";

        assert(supervisor_service_definition_parse(
                   linux_only, sizeof(linux_only) - 1u,
                   &second, &line) == ASTRA_STATUS_INVALID);
    }
    puts("service definition store tests passed");
    return 0;
}
