#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service_manager.h>

#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("service", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

static AstraServiceDefinition definition;

static const char *state_name(uint32_t state)
{
    switch (state) {
    case ASTRA_SERVICE_STATE_STOPPED: return "stopped";
    case ASTRA_SERVICE_STATE_STARTING: return "starting";
    case ASTRA_SERVICE_STATE_READY: return "running";
    case ASTRA_SERVICE_STATE_PAUSED: return "paused";
    case ASTRA_SERVICE_STATE_STOPPING: return "stopping";
    case ASTRA_SERVICE_STATE_FAILED: return "failed";
    default: return "unknown";
    }
}

static const char *runs_name(uint32_t flags)
{
    return (flags & ASTRA_SERVICE_RUNS_PAIRED) != 0u ? "paired" : "astra";
}

static const char *start_name(uint32_t value)
{
    return value == ASTRA_SERVICE_START_BOOT ? "boot" :
           value == ASTRA_SERVICE_START_ON_DEMAND ? "on-demand" : "manual";
}

static const char *restart_name(uint32_t value)
{
    return value == ASTRA_SERVICE_RESTART_ALWAYS ? "always" :
           value == ASTRA_SERVICE_RESTART_ON_FAULT ? "on-fault" : "never";
}

static int failure(const char *operation, AstraResult result)
{
    const char *reason = result == ASTRA_ERROR_PERMISSION ? "not permitted" :
        result == ASTRA_ERROR_BUSY ? "service is busy" :
        result == ASTRA_ERROR_INVALID_HANDLE ? "service not found" :
        result == ASTRA_ERROR_NO_RESOURCES ? "insufficient resources" :
        result == ASTRA_ERROR_INVALID_ARGUMENT ? "invalid definition" :
        result == ASTRA_ERROR_UNSUPPORTED ? "operation unsupported" :
        "operation failed";

    (void)fprintf(stderr, "service: %s: %s\n", operation, reason);
    return result == ASTRA_OK ? 0 : (int)-result;
}

static void print_info(const AstraServiceInfo *info)
{
    (void)printf("%-24s %-7s %-8s %-8s %5u\n", info->name,
                 runs_name(info->flags),
                 (info->flags & ASTRA_SERVICE_ENABLED) != 0u ? "enabled" :
                                                               "disabled",
                 state_name(info->state), info->process_id);
}

static int list(AstraHandle manager)
{
    AstraServiceListCursor cursor = ASTRA_SERVICE_LIST_CURSOR_INIT;

    (void)fputs("SERVICE                  RUNS    BOOT     STATE      PID\n",
                stdout);
    for (;;) {
        AstraServiceInfo info;
        AstraServiceListCursor next;
        AstraResult result = astra_service_list(
            manager, &cursor, &info, &next);

        if (result == ASTRA_ERROR_INVALID_HANDLE)
            return 0;
        if (result != ASTRA_OK)
            return failure("list", result);
        print_info(&info);
        if (next.source == ASTRA_SERVICE_LIST_SOURCE_DONE)
            return 0;
        cursor = next;
    }
}

static int inspect(AstraHandle manager, const char *name)
{
    AstraServiceInfo info;
    AstraResult result = astra_service_inspect(
        manager, name, &info, &definition);
    uint32_t argument = 0u;

    if (result != ASTRA_OK)
        return failure("inspect", result);
    (void)printf("name: %s\nexecutable: %s\nruns: %s\nenabled: %s\n"
                 "protected: %s\nstate: %s\npid: %u\nstart: %s\n"
                 "restart: %s\n",
                 info.name, info.executable, runs_name(info.flags),
                 (info.flags & ASTRA_SERVICE_ENABLED) != 0u ? "yes" : "no",
                 (info.flags & ASTRA_SERVICE_PROTECTED) != 0u ? "yes" : "no",
                 state_name(info.state), info.process_id,
                 start_name(info.start_policy),
                 restart_name(info.restart_policy));
    for (uint32_t index = 0u; index < definition.argument_count; ++index) {
        (void)printf("argument: %s\n", definition.arguments + argument);
        argument += (uint32_t)strlen(definition.arguments + argument) + 1u;
    }
    for (uint32_t index = 0u; index < definition.grant_count; ++index)
        (void)printf("grant: %s\n", definition.grants[index].name);
    for (uint32_t index = 0u; index < definition.publication_count; ++index)
        (void)printf("provides: %s\n",
                     definition.publications[index].name);
    for (uint32_t index = 0u; index < definition.dependency_count; ++index)
        (void)printf("needs: %s\n", definition.dependencies[index]);
    return 0;
}

static AstraResult authority(AstraServiceDefinition *service,
                             const char *text, int publication)
{
    char name[ASTRA_CAPABILITY_NAME_MAX + 3u];
    const char *colon = strchr(text, ':');
    size_t length = colon == NULL ? strlen(text) : (size_t)(colon - text);
    uint32_t rights = 0u;

    if (length == 0u || length >= ASTRA_CAPABILITY_NAME_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    (void)memcpy(name, text, length);
    name[length] = '\0';
    if (colon != NULL) {
        rights = strcmp(colon + 1u, "r") == 0 ? ASTRA_RIGHT_READ :
                 strcmp(colon + 1u, "rw") == 0 ?
                     ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE : UINT32_MAX;
        if (rights == UINT32_MAX)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    return publication ? astra_service_definition_add_publication(
                             service, name, rights) :
                         astra_service_definition_add_grant(
                             service, name, rights, rights != 0u);
}

static int add(AstraHandle manager, int argc, char **argv)
{
    uint32_t runs = ASTRA_SERVICE_RUNS_ASTRA;
    int arguments = 0;
    AstraResult result;

    if (argc < 4)
        return failure("add", ASTRA_ERROR_INVALID_ARGUMENT);
    for (int index = 4; index < argc; ++index)
        if (strcmp(argv[index], "--paired") == 0)
            runs = ASTRA_SERVICE_RUNS_PAIRED;
    result = astra_service_definition_init(
        &definition, argv[2], argv[3], runs);
    if (result != ASTRA_OK)
        return failure("add", result);
    for (int index = 4; index < argc; ++index) {
        const char *option = argv[index];

        if (arguments) {
            result = astra_service_definition_add_argument(&definition,
                                                            option);
        } else if (strcmp(option, "--") == 0) {
            arguments = 1;
            continue;
        } else if (strcmp(option, "--paired") == 0) {
            continue;
        } else if (strcmp(option, "--disabled") == 0) {
            definition.flags &= ~ASTRA_SERVICE_ENABLED;
            continue;
        } else if (strcmp(option, "--boot") == 0) {
            definition.start_policy = ASTRA_SERVICE_START_BOOT;
            continue;
        } else if (strcmp(option, "--on-demand") == 0) {
            definition.start_policy = ASTRA_SERVICE_START_ON_DEMAND;
            continue;
        } else if (strcmp(option, "--manual") == 0) {
            definition.start_policy = ASTRA_SERVICE_START_MANUAL;
            continue;
        } else if (strncmp(option, "--restart=", 10u) == 0) {
            const char *value = option + 10u;

            definition.restart_policy = strcmp(value, "never") == 0 ?
                ASTRA_SERVICE_RESTART_NEVER :
                strcmp(value, "on-fault") == 0 ?
                    ASTRA_SERVICE_RESTART_ON_FAULT :
                strcmp(value, "always") == 0 ?
                    ASTRA_SERVICE_RESTART_ALWAYS : UINT32_MAX;
            result = definition.restart_policy == UINT32_MAX ?
                ASTRA_ERROR_INVALID_ARGUMENT : ASTRA_OK;
        } else if (strncmp(option, "--grant=", 8u) == 0) {
            result = authority(&definition, option + 8u, 0);
        } else if (strncmp(option, "--provide=", 10u) == 0) {
            result = authority(&definition, option + 10u, 1);
        } else if (strncmp(option, "--need=", 7u) == 0) {
            result = astra_service_definition_add_dependency(
                &definition, option + 7u);
        } else {
            result = ASTRA_ERROR_INVALID_ARGUMENT;
        }
        if (result != ASTRA_OK)
            return failure("add", result);
    }
    result = astra_service_add(manager, &definition);
    return result == ASTRA_OK ? 0 : failure("add", result);
}

static uint32_t operation(const char *verb)
{
    static const char *const names[] = {
        "delete", "start", "stop", "restart", "pause", "resume",
        "enable", "disable"
    };
    static const uint32_t operations[] = {
        ASTRA_SERVICE_MANAGER_REMOVE, ASTRA_SERVICE_MANAGER_START,
        ASTRA_SERVICE_MANAGER_STOP, ASTRA_SERVICE_MANAGER_RESTART,
        ASTRA_SERVICE_MANAGER_PAUSE, ASTRA_SERVICE_MANAGER_RESUME,
        ASTRA_SERVICE_MANAGER_ENABLE, ASTRA_SERVICE_MANAGER_DISABLE
    };

    for (uint32_t index = 0u; index < sizeof(names) / sizeof(names[0]); ++index)
        if (strcmp(names[index], verb) == 0)
            return operations[index];
    return 0u;
}

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *capability;
    uint32_t action;
    AstraServiceInfo info;
    AstraResult result;

    if (!astra_startup_validate(startup))
        return failure("startup", ASTRA_ERROR_INVALID_ARGUMENT);
    capability = astra_startup_capability(
        startup, ASTRA_CAPABILITY_SERVICE_MANAGER);
    if (capability == NULL ||
        (capability->rights & ASTRA_RIGHT_SIGNAL) == 0u)
        return failure("manager", ASTRA_ERROR_PERMISSION);
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "list") == 0))
        return list(capability->handle);
    if (argc == 3 && strcmp(argv[1], "inspect") == 0)
        return inspect(capability->handle, argv[2]);
    if (strcmp(argv[1], "add") == 0)
        return add(capability->handle, argc, argv);
    action = argc == 3 ? operation(argv[1]) : 0u;
    if (action == 0u) {
        (void)fputs(
            "usage: service [list|inspect NAME|start NAME|stop NAME|"
            "restart NAME|pause NAME|resume NAME|enable NAME|disable NAME|"
            "delete NAME|add NAME EXECUTABLE [options] [-- ARG ...]]\n",
            stderr);
        return (int)-ASTRA_ERROR_INVALID_ARGUMENT;
    }
    result = astra_service_control(capability->handle, action, argv[2], &info);
    return result == ASTRA_OK ? 0 : failure(argv[1], result);
}
