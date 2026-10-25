#include <service_definition_store.h>

#include <astra/status.h>
#include <astra/service_manager.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

void *astra_runtime_allocate(size_t size) { return malloc(size); }

void *astra_runtime_reallocate(void *pointer, size_t size)
{
    return realloc(pointer, size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

static void same_definition(const AstraServiceDefinition *left, const AstraServiceDefinition *right)
{
    size_t offset = offsetof(AstraServiceDefinition, executable);
    size_t after = offset + sizeof(left->executable);
    assert(strcmp(left->executable, right->executable) == 0);
    assert(memcmp(left, right, offset) == 0);
    assert(memcmp((const char *)left + after, (const char *)right + after, sizeof(*left) - after) == 0);
}

int main(void)
{
    static const char source[] =
        "# hand editable\n"
        "astra-config 1\n"
        "schema = 1\n"
        "name remote-desktop\n"
        "executable /services/remote-desktop\n"
        "runs paired\n"
        "enabled true\n"
        "delegates false\n"
        "start manual\n"
        "restart on-fault\n"
        "priority 12\n"
        "argument /services/remote-desktop\n"
        "grant NETWORK\n"
        "provides REMOTE\n"
        "needs NETWORK\n";
    AstraServiceDefinition first ASTRA_AUTO_SERVICE_DEFINITION = ASTRA_SERVICE_DEFINITION_INIT;
    AstraServiceDefinition second ASTRA_AUTO_SERVICE_DEFINITION = ASTRA_SERVICE_DEFINITION_INIT;
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
    same_definition(&first, &second);
    assert(first.priority == 12u);
    first.priority = 7u;
    assert(supervisor_service_definition_serialize(
               &first, serialized, sizeof(serialized), &required) ==
           ASTRA_STATUS_OK);
    assert(strstr(serialized, "\npriority 7\n") != NULL);
    assert(supervisor_service_definition_parse(
               serialized, required - 1u, &second, &line) ==
           ASTRA_STATUS_OK);
    same_definition(&first, &second);
    {
        static const char linux_only[] =
            "astra-config 1\nschema 1\nruns linux\n";

        assert(supervisor_service_definition_parse(
                   linux_only, sizeof(linux_only) - 1u,
                   &second, &line) == ASTRA_STATUS_INVALID);
    }
    {
        static const char ram_volume[] =
            "astra-config 1\nschema 1\n"
            "name ramfs\nexecutable /services/ramfs\n"
            "runs astra\nenabled true\ndelegates false\n"
            "start boot\nrestart on-fault\n"
            "argument /services/ramfs\n"
            "provides RAM_WRITE\ngrant CONFIG:r\nneeds SYSTEM\n";
        static const char invalid_volume[] =
            "astra-config 1\nschema 1\n"
            "name ramfs\nexecutable /services/ramfs\n"
            "runs astra\nenabled true\ndelegates false\n"
            "start boot\nrestart on-fault\n"
            "argument /services/ramfs\n"
            "provides RAM_WRITE:bogus\ngrant CONFIG:r\nneeds SYSTEM\n";
        static const char disabled_volume[] =
            "astra-config 1\nschema 1\n"
            "name ramfs\nexecutable /services/ramfs\n"
            "runs astra\nenabled false\ndelegates false\n"
            "start boot\nrestart on-fault\n"
            "argument /services/ramfs\n"
            "provides RAM_WRITE\ngrant CONFIG:r\nneeds SYSTEM\n";

        assert(supervisor_service_definition_parse(
                   ram_volume, sizeof(ram_volume) - 1u,
                   &second, &line) == ASTRA_STATUS_OK);
        assert((second.flags & ASTRA_SERVICE_RUNS_ASTRA) != 0u &&
               second.publication_count == 1u &&
               strcmp(second.publications[0].name, "RAM_WRITE") == 0 &&
               second.publications[0].rights == 0u);
        assert(supervisor_service_definition_parse(
                   disabled_volume, sizeof(disabled_volume) - 1u,
                   &second, &line) == ASTRA_STATUS_OK);
        assert((second.flags & ASTRA_SERVICE_ENABLED) == 0u);
        assert(supervisor_service_definition_parse(
                   invalid_volume, sizeof(invalid_volume) - 1u,
                   &second, &line) == ASTRA_STATUS_INVALID);
    }
    {
        SupervisorManifestEntry entry;
        AstraServiceDefinition service ASTRA_AUTO_SERVICE_DEFINITION = ASTRA_SERVICE_DEFINITION_INIT;
        char policy[128];
        static const char chosen[] =
            "astra-config 1\nschema 1\nstart manual\n";
        static const char bad[] =
            "astra-config 1\nschema 1\nrestart sometimes\n";

        (void)memset(&entry, 0, sizeof(entry));
        (void)strcpy(entry.path, "/services/storage");
        entry.resident = 1u;
        entry.critical = 1u;
        assert(supervisor_service_definition_from_manifest(
                   &entry, &service) == ASTRA_STATUS_OK);
        assert((service.flags & ASTRA_SERVICE_PROTECTED) != 0u &&
               (service.flags & ASTRA_SERVICE_CRITICAL) != 0u &&
               service.start_policy == ASTRA_SERVICE_START_BOOT &&
               service.restart_policy == ASTRA_SERVICE_RESTART_NEVER);
        /* A protected service takes no user choice at all. */
        assert(supervisor_service_policy_apply(ASTRA_STATUS_OK,
                   chosen, sizeof(chosen) - 1u, &service) ==
               ASTRA_STATUS_INVALID);
        assert(supervisor_service_policy_serialize(
                   &service, policy, sizeof(policy), &required) ==
               ASTRA_STATUS_INVALID);
        entry.critical = 0u;
        entry.required = 1u;
        assert(supervisor_service_definition_from_manifest(
                   &entry, &service) == ASTRA_STATUS_OK);
        assert((service.flags & ASTRA_SERVICE_PROTECTED) != 0u &&
               (service.flags & ASTRA_SERVICE_CRITICAL) == 0u &&
               service.restart_policy == ASTRA_SERVICE_RESTART_ALWAYS);
        {
            SupervisorManifestEntry back;

            assert(supervisor_service_definition_to_manifest(
                       &service, &back) == ASTRA_STATUS_OK);
            assert(back.required == 1u && back.critical == 0u);
        }

        (void)strcpy(entry.path, "/services/media");
        entry.required = 0u;
        entry.priority = ASTRA_PROCESS_PRIORITY_MEDIA;
        entry.start_policy = ASTRA_SERVICE_START_BOOT;
        entry.restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;
        assert(supervisor_service_definition_from_manifest(
                   &entry, &service) == ASTRA_STATUS_OK);
        assert((service.flags & ASTRA_SERVICE_PROTECTED) == 0u);
        assert(service.priority == ASTRA_PROCESS_PRIORITY_MEDIA);
        assert(supervisor_service_policy_apply(ASTRA_STATUS_NOT_FOUND,
            NULL, 0u, &service) == ASTRA_STATUS_OK);
        assert(service.start_policy == ASTRA_SERVICE_START_BOOT);
        for (uint32_t at = 0u; at < 3u; ++at) {
            uint32_t fault = at == 0u ? ASTRA_STATUS_LIMIT :
                at == 1u ? ASTRA_STATUS_IO : ASTRA_STATUS_ACCESS;
            assert(supervisor_service_policy_apply(fault, NULL, 0u, &service) == fault);
            assert(service.start_policy == ASTRA_SERVICE_START_BOOT);
        }
        {
            SupervisorManifestEntry back;

            assert(supervisor_service_definition_to_manifest(
                       &service, &back) == ASTRA_STATUS_OK);
            assert(back.priority == ASTRA_PROCESS_PRIORITY_MEDIA);
        }
        /* An absent key keeps the default; a present one overrides. */
        assert(supervisor_service_policy_apply(ASTRA_STATUS_OK,
                   chosen, sizeof(chosen) - 1u, &service) ==
               ASTRA_STATUS_OK);
        assert(service.start_policy == ASTRA_SERVICE_START_MANUAL &&
               service.restart_policy == ASTRA_SERVICE_RESTART_ON_FAULT);
        assert(supervisor_service_policy_apply(ASTRA_STATUS_LIMIT,
            NULL, 0u, &service) == ASTRA_STATUS_LIMIT);
        assert(supervisor_service_policy_apply(ASTRA_STATUS_IO,
            NULL, 0u, &service) == ASTRA_STATUS_IO);
        assert(service.start_policy == ASTRA_SERVICE_START_MANUAL);
        assert(supervisor_service_policy_apply(ASTRA_STATUS_OK,
                   bad, sizeof(bad) - 1u, &service) == ASTRA_STATUS_INVALID);
        assert(service.start_policy == ASTRA_SERVICE_START_MANUAL);
        service.restart_policy = ASTRA_SERVICE_RESTART_NEVER;
        assert(supervisor_service_policy_serialize(
                   &service, policy, sizeof(policy), &required) ==
               ASTRA_STATUS_OK);
        assert(strcmp(policy, "astra-config 1\nschema 1\nstart manual\n"
                              "restart never\n") == 0);
        service.start_policy = ASTRA_SERVICE_START_BOOT;
        service.restart_policy = ASTRA_SERVICE_RESTART_ALWAYS;
        assert(supervisor_service_policy_apply(ASTRA_STATUS_OK,
                   policy, required - 1u, &service) == ASTRA_STATUS_OK);
        assert(service.start_policy == ASTRA_SERVICE_START_MANUAL &&
               service.restart_policy == ASTRA_SERVICE_RESTART_NEVER);
    }
    puts("service definition store tests passed");
    return 0;
}
