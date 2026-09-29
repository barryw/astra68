#include <loader.h>
#include <astra/service_manager_abi.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_reallocation;

void *astra_runtime_reallocate(void *pointer, size_t size)
{
    if (fail_reallocation)
        return NULL;
    return realloc(pointer, size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

static void valid_manifest(void)
{
    char text[] =
        "# shipped startup\n"
            "service /services/storage grants BLOCK_DEVICE BLOCK_IRQ "
            "serves SYSTEM:r required\n"
            "service /services/hostfs grants HOST_DEVICE "
            "serves WORK:rw METRICS:r required\n"
        "service /services/events grants SYSTEM:r STORE:rw serves EVENTS:r\n";
    SupervisorManifest manifest = SUPERVISOR_MANIFEST_INIT;

    assert(supervisor_manifest_parse(text, sizeof(text) - 1u, &manifest));
    assert(manifest.count == 3u);
    assert(manifest.entries[0].required == 1u);
    assert(manifest.entries[0].grant_count == 2u);
    assert(strcmp(manifest.entries[0].grants[0].name,
                  "BLOCK_DEVICE") == 0);
    assert(manifest.entries[0].grants[0].is_namespace == 0u);
    assert(manifest.entries[0].serves_count == 1u);
    assert(strcmp(manifest.entries[0].serves[0].name, "SYSTEM") == 0);
    assert(manifest.entries[1].serves_count == 2u);
    assert(strcmp(manifest.entries[1].serves[1].name, "METRICS") == 0);
    assert(manifest.entries[2].grants[1].rights ==
           (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));

    {
        char terminal[] =
        "application /services/terminal grants DISPLAY INPUT INPUT_IRQ "
            "WORK:rw COMMANDS:r LIBS:r EVENTS:r EVENT_CONTROL delegates\n";

        assert(supervisor_manifest_parse(terminal, sizeof(terminal) - 1u,
                                         &manifest));
        assert(manifest.count == 1u);
        assert(manifest.entries[0].grant_count == 8u);
        assert(manifest.entries[0].resident == 0u);
        assert(manifest.entries[0].delegates == 1u);
        assert(manifest.entries[0].serves_count == 0u);
    }
    {
        char display[] =
            "service /services/display grants DISPLAY DISPLAY_IRQ "
            "VBLANK_IRQ "
            "serves GUI required\n"
            "application /services/terminal grants GUI WORK:rw COMMANDS:r "
            "LIBS:r EVENTS:r EVENT_CONTROL delegates\n";

        assert(supervisor_manifest_parse(display, sizeof(display) - 1u,
                                         &manifest));
        assert(manifest.count == 2u);
        assert(manifest.entries[0].grant_count == 3u);
        assert(manifest.entries[0].resident == 1u);
        assert(strcmp(manifest.entries[0].grants[1].name,
                      "DISPLAY_IRQ") == 0);
        assert(strcmp(manifest.entries[0].serves[0].name, "GUI") == 0);
        assert(manifest.entries[0].serves[0].rights == 0u);
        assert(manifest.entries[1].grant_count == 6u);
        assert(manifest.entries[1].resident == 0u);
        assert(strcmp(manifest.entries[1].grants[0].name, "GUI") == 0);
        assert(manifest.entries[1].delegates == 1u);
    }
    {
        char network[] =
            "service /services/network grants NETWORK_DEVICE NETWORK_IRQ "
            "serves NETWORK NETWORK_LISTEN required\n";

        assert(supervisor_manifest_parse(network, sizeof(network) - 1u,
                                         &manifest));
        assert(manifest.count == 1u);
        assert(manifest.entries[0].serves_count == 2u);
        assert(strcmp(manifest.entries[0].serves[0].name, "NETWORK") == 0);
        assert(manifest.entries[0].serves[0].rights == 0u);
        assert(strcmp(manifest.entries[0].serves[1].name,
                      "NETWORK_LISTEN") == 0);
        assert(manifest.entries[0].serves[1].rights == 0u);
    }
    supervisor_manifest_destroy(&manifest);
}

static void refuses_whole_file(void)
{
    char bad_right[] =
        "service /services/storage grants BLOCK_DEVICE serves SYS:r required\n"
        "service /services/events grants STORE:write serves EVENTS:r\n";
    char wrong_order[] =
        "service /services/events serves EVENTS:r grants STORE:rw\n";
    char command[] = "command /commands/shell grants SYS:r required\n";
    char required_application[] =
        "application /apps/Broken.app grants GUI required\n";
    char old_colon_path[] = "service SERVICES:storage grants BLOCK_DEVICE\n";
    char root_path[] = "service / grants BLOCK_DEVICE\n";
    char path_grant[] =
        "application /services/desktop grants GUI /apps/r LIBS:r\n";
    char too_many[4096] = "";
    SupervisorManifest manifest = SUPERVISOR_MANIFEST_INIT;

    assert(!supervisor_manifest_parse(bad_right, sizeof(bad_right) - 1u,
                                      &manifest));
    assert(manifest.count == 0u);
    assert(!supervisor_manifest_parse(wrong_order,
                                      sizeof(wrong_order) - 1u, &manifest));
    assert(!supervisor_manifest_parse(command, sizeof(command) - 1u,
                                      &manifest));
    assert(!supervisor_manifest_parse(required_application,
                                      sizeof(required_application) - 1u,
                                      &manifest));
    assert(!supervisor_manifest_parse(old_colon_path,
                                      sizeof(old_colon_path) - 1u,
                                      &manifest));
    assert(!supervisor_manifest_parse(root_path, sizeof(root_path) - 1u,
                                      &manifest));
    assert(!supervisor_manifest_parse(path_grant, sizeof(path_grant) - 1u,
                                      &manifest));
    for (uint32_t index = 0u; index < 40u; ++index)
        (void)strcat(too_many, "service /services/extra grants SYS:r\n");
    assert(supervisor_manifest_parse(too_many, strlen(too_many), &manifest));
    assert(manifest.count == 40u);
    fail_reallocation = 1;
    assert(!supervisor_manifest_parse(too_many, strlen(too_many),
                                      &manifest));
    fail_reallocation = 0;
    assert(manifest.count == 0u && manifest.entries == NULL);
    supervisor_manifest_destroy(&manifest);
}

static void parses_bundle_grants(void)
{
    SupervisorManifestGrant grant;
    char raw[] = "GUI";
    char namespaced[] = "WORK:rw";
    char bad_path[] = "/apps/r";

    assert(supervisor_manifest_grant(raw, &grant));
    assert(strcmp(grant.name, "GUI") == 0 && grant.is_namespace == 0u);
    assert(supervisor_manifest_grant(namespaced, &grant));
    assert(strcmp(grant.name, "WORK") == 0 && grant.is_namespace == 1u);
    assert(grant.rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));
    assert(!supervisor_manifest_grant(bad_path, &grant));
}

static void accepts_large_commented_manifest(void)
{
    char text[262144u];
    SupervisorManifest manifest = SUPERVISOR_MANIFEST_INIT;
    const char service[] = "\nservice /services/storage grants BLOCK_DEVICE\n";

    text[0] = '#';
    memset(text + 1u, 'x', sizeof(text) - sizeof(service) - 1u);
    memcpy(text + sizeof(text) - sizeof(service), service, sizeof(service));
    assert(supervisor_manifest_parse(text, sizeof(text) - 1u, &manifest));
    assert(manifest.count == 1u);
    supervisor_manifest_destroy(&manifest);
}

static void path_uses_vfs_authority(void)
{
    char path[ASTRA_VFS_PATH_MAX + 1u];
    char text[ASTRA_VFS_PATH_MAX + 64u];
    SupervisorManifest manifest = SUPERVISOR_MANIFEST_INIT;

    memcpy(path, "/services/", sizeof("/services/") - 1u);
    memset(path + sizeof("/services/") - 1u, 'p',
           150u - (sizeof("/services/") - 1u));
    path[150] = '\0';
    (void)snprintf(text, sizeof(text), "service %s grants SYS:r\n", path);
    assert(supervisor_manifest_parse(text, strlen(text), &manifest));
    assert(strlen(manifest.entries[0].path) == 150u);
    memset(path + sizeof("/services/") - 1u, 'p',
           ASTRA_VFS_PATH_MAX - (sizeof("/services/") - 1u));
    path[ASTRA_VFS_PATH_MAX] = '\0';
    (void)snprintf(text, sizeof(text), "service %s grants SYS:r\n", path);
    assert(!supervisor_manifest_parse(text, strlen(text), &manifest));
    supervisor_manifest_destroy(&manifest);
}

static void parses_exact_span_without_terminator(void)
{
    const char source[] = "service /services/test grants SYS:r";
    size_t length = sizeof(source) - 1u;
    char *text = malloc(length);
    SupervisorManifest manifest = SUPERVISOR_MANIFEST_INIT;

    assert(text != NULL);
    memcpy(text, source, length);
    assert(supervisor_manifest_parse(text, (uint32_t)length, &manifest));
    assert(manifest.count == 1u);
    assert(strcmp(manifest.entries[0].path, "/services/test") == 0);
    free(text);

    text = malloc(length);
    assert(text != NULL);
    memcpy(text, source, length);
    fail_reallocation = 1;
    assert(!supervisor_manifest_parse(text, (uint32_t)length, &manifest));
    fail_reallocation = 0;
    assert(manifest.count == 0u);
    free(text);
    supervisor_manifest_destroy(&manifest);
}

static SupervisorManifestGrant grant(const char *text)
{
    char copy[64];
    SupervisorManifestGrant out;

    assert(strlen(text) < sizeof(copy));
    strcpy(copy, text);
    assert(supervisor_manifest_grant(copy, &out));
    return out;
}

static void trusted_entries_are_ceilings(void)
{
    char text[] =
        "service /services/storage grants BLOCK_DEVICE serves SYSTEM:r "
            "required\n"
        "trusted /apps/Terminal.app grants GUI SYSTEM:r SERVICE_MANAGER "
            "WORK:rw\n";
    SupervisorManifest manifest = SUPERVISOR_MANIFEST_INIT;
    const SupervisorManifestEntry *terminal;
    SupervisorManifestGrant wanted;

    assert(supervisor_manifest_parse(text, sizeof(text) - 1u, &manifest));
    assert(manifest.count == 2u);
    terminal = &manifest.entries[1];
    assert(terminal->trusted == 1u && terminal->resident == 0u &&
           terminal->required == 0u && terminal->grant_count == 4u);
    wanted = grant("SERVICE_MANAGER");
    assert(supervisor_launch_admits(terminal, &wanted));
    wanted = grant("SYSTEM:r");
    assert(supervisor_launch_admits(terminal, &wanted));
    /* Rights above the ceiling, a raw name used as a namespace, and a name
       the ceiling lacks are all refused. */
    wanted = grant("SYSTEM:rw");
    assert(!supervisor_launch_admits(terminal, &wanted));
    wanted = grant("GUI:r");
    assert(!supervisor_launch_admits(terminal, &wanted));
    wanted = grant("DISPLAY");
    assert(!supervisor_launch_admits(terminal, &wanted));
    supervisor_manifest_destroy(&manifest);

    /* A trusted line names one bundle and nothing else. */
    {
        static const char *bad[] = {
            "trusted /services/desktop grants GUI\n",
            "trusted /apps/Terminal grants GUI\n",
            "trusted /apps/a/b.app grants GUI\n",
            "trusted /apps/.app grants GUI\n",
            "trusted /apps/Terminal.app grants GUI serves X\n",
            "trusted /apps/Terminal.app grants GUI delegates\n",
            "trusted /apps/Terminal.app grants GUI required\n",
        };

        for (uint32_t at = 0u; at < sizeof(bad) / sizeof(bad[0]); ++at) {
            char line[96];

            strcpy(line, bad[at]);
            assert(!supervisor_manifest_parse(line, (uint32_t)strlen(line),
                                              &manifest));
        }
    }
}

static void default_application_ceiling(void)
{
    static const char *admitted[] = {
        "GUI", "CLIPBOARD", "PCM", "NETWORK", "APP_LAUNCH", "APPS:r",
        "LIBS:r", "STORE:rw", "HOME:rw", "CONFIG:rw", "RAM:r",
    };
    static const char *refused[] = {
        "DISPLAY", "INPUT", "HOST_DEVICE", "BLOCK_DEVICE", "CLOCK",
        "SERVICE_MANAGER", "EVENT_CONTROL", "PROCESS", "APPS:rw",
        "LIBS:rw", "SYSTEM:r", "PROC:r", "COMMANDS:r", "STORE",
        "GUI:r", "UNKNOWN",
    };

    for (uint32_t at = 0u; at < sizeof(admitted) / sizeof(admitted[0]);
         ++at) {
        SupervisorManifestGrant wanted = grant(admitted[at]);

        assert(supervisor_launch_admits(NULL, &wanted));
    }
    for (uint32_t at = 0u; at < sizeof(refused) / sizeof(refused[0]); ++at) {
        SupervisorManifestGrant wanted = grant(refused[at]);

        assert(!supervisor_launch_admits(NULL, &wanted));
    }
    assert(!supervisor_launch_admits(NULL, NULL));
}

static void parses_service_tiers(void)
{
    char text[] =
        "service /services/storage grants BLOCK_DEVICE serves SYSTEM:r "
        "critical\n"
        "service /services/input grants INPUT serves INPUT_SERVICE required\n"
        "service /services/media grants HOST_DEVICE serves PCM "
        "restart=always start=manual\n"
        "service /services/remote grants NETWORK\n";
    SupervisorManifest manifest = SUPERVISOR_MANIFEST_INIT;
    const char *refused[] = {
        "service /services/a grants X critical required\n",
        "service /services/a grants X required required\n",
        "service /services/a grants X required start=manual\n",
        "service /services/a grants X critical restart=always\n",
        "service /services/a grants X start=on-demand\n",
        "service /services/a grants X restart=sometimes\n",
        "service /services/a grants X start=boot start=manual\n",
        "application /apps/A.app grants GUI critical\n",
        "application /apps/A.app grants GUI restart=never\n",
        "trusted /apps/A.app grants GUI start=manual\n",
    };

    assert(supervisor_manifest_parse(text, sizeof(text) - 1u, &manifest));
    assert(manifest.count == 4u);
    assert(manifest.entries[0].critical == 1u &&
           manifest.entries[0].required == 0u &&
           manifest.entries[0].serves_count == 1u);
    assert(manifest.entries[1].required == 1u &&
           manifest.entries[1].critical == 0u);
    assert(manifest.entries[2].required == 0u &&
           manifest.entries[2].start_policy == ASTRA_SERVICE_START_MANUAL &&
           manifest.entries[2].restart_policy ==
               ASTRA_SERVICE_RESTART_ALWAYS &&
           manifest.entries[2].serves_count == 1u);
    assert(manifest.entries[3].start_policy == ASTRA_SERVICE_START_BOOT &&
           manifest.entries[3].restart_policy ==
               ASTRA_SERVICE_RESTART_ON_FAULT &&
           manifest.entries[3].grant_count == 1u);
    for (uint32_t at = 0u; at < sizeof(refused) / sizeof(refused[0]); ++at) {
        char line[128];

        (void)strcpy(line, refused[at]);
        assert(!supervisor_manifest_parse(line, (uint32_t)strlen(line),
                                          &manifest));
    }
    supervisor_manifest_destroy(&manifest);
}

int main(void)
{
    trusted_entries_are_ceilings();
    default_application_ceiling();
    valid_manifest();
    refuses_whole_file();
    parses_service_tiers();
    parses_bundle_grants();
    accepts_large_commented_manifest();
    path_uses_vfs_authority();
    parses_exact_span_without_terminator();
    return 0;
}
