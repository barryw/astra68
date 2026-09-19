#include <loader.h>

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
            "service SERVICES:storage grants BLOCK_DEVICE BLOCK_IRQ "
            "serves SYS:r required\n"
            "service SERVICES:hostfs grants HOST_DEVICE "
            "serves WORK:rw METRICS:r required\n"
        "service SERVICES:events grants SYS:r STORE:rw serves EVENTS:r\n";
    SupervisorManifest manifest;

    assert(supervisor_manifest_parse(text, sizeof(text) - 1u, &manifest));
    assert(manifest.count == 3u);
    assert(manifest.entries[0].required == 1u);
    assert(manifest.entries[0].grant_count == 2u);
    assert(strcmp(manifest.entries[0].grants[0].name,
                  "BLOCK_DEVICE") == 0);
    assert(manifest.entries[0].grants[0].is_namespace == 0u);
    assert(manifest.entries[0].serves_count == 1u);
    assert(strcmp(manifest.entries[0].serves[0].name, "SYS") == 0);
    assert(manifest.entries[1].serves_count == 2u);
    assert(strcmp(manifest.entries[1].serves[1].name, "METRICS") == 0);
    assert(manifest.entries[2].grants[1].rights ==
           (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));

    {
        char terminal[] =
        "application SERVICES:terminal grants DISPLAY INPUT INPUT_IRQ "
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
            "service SERVICES:display grants DISPLAY DISPLAY_IRQ "
            "VBLANK_IRQ "
            "serves GUI required\n"
            "application SERVICES:terminal grants GUI WORK:rw COMMANDS:r "
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
            "service SERVICES:network grants NETWORK_DEVICE NETWORK_IRQ "
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
}

static void refuses_whole_file(void)
{
    char bad_right[] =
        "service SERVICES:storage grants BLOCK_DEVICE serves SYS:r required\n"
        "service SERVICES:events grants STORE:write serves EVENTS:r\n";
    char wrong_order[] =
        "service SERVICES:events serves EVENTS:r grants STORE:rw\n";
    char command[] = "command COMMANDS:shell grants SYS:r required\n";
    char required_application[] =
        "application APPS:Broken.app grants GUI required\n";
    char too_many[4096] = "";
    SupervisorManifest manifest;

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
    for (uint32_t index = 0u; index < ASTRA_PROCESS_COUNT_MAX; ++index)
        (void)strcat(too_many, "service SERVICES:extra grants SYS:r\n");
    assert(!supervisor_manifest_parse(too_many, strlen(too_many),
                                      &manifest));
    assert(manifest.count == 0u);
}

static void parses_bundle_grants(void)
{
    SupervisorManifestGrant grant;
    char raw[] = "GUI";
    char namespaced[] = "WORK:rw";

    assert(supervisor_manifest_grant(raw, &grant));
    assert(strcmp(grant.name, "GUI") == 0 && grant.is_namespace == 0u);
    assert(supervisor_manifest_grant(namespaced, &grant));
    assert(strcmp(grant.name, "WORK") == 0 && grant.is_namespace == 1u);
    assert(grant.rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));
}

static void accepts_large_commented_manifest(void)
{
    char text[262144u];
    SupervisorManifest manifest;
    const char service[] = "\nservice SERVICES:storage grants BLOCK_DEVICE\n";

    text[0] = '#';
    memset(text + 1u, 'x', sizeof(text) - sizeof(service) - 1u);
    memcpy(text + sizeof(text) - sizeof(service), service, sizeof(service));
    assert(supervisor_manifest_parse(text, sizeof(text) - 1u, &manifest));
    assert(manifest.count == 1u);
}

static void path_uses_vfs_authority(void)
{
    char path[ASTRA_VFS_PATH_MAX + 1u];
    char text[ASTRA_VFS_PATH_MAX + 64u];
    SupervisorManifest manifest;

    memcpy(path, "SERVICES:", sizeof("SERVICES:") - 1u);
    memset(path + sizeof("SERVICES:") - 1u, 'p',
           150u - (sizeof("SERVICES:") - 1u));
    path[150] = '\0';
    (void)snprintf(text, sizeof(text), "service %s grants SYS:r\n", path);
    assert(supervisor_manifest_parse(text, strlen(text), &manifest));
    assert(strlen(manifest.entries[0].path) == 150u);
    memset(path + sizeof("SERVICES:") - 1u, 'p',
           ASTRA_VFS_PATH_MAX - (sizeof("SERVICES:") - 1u));
    path[ASTRA_VFS_PATH_MAX] = '\0';
    (void)snprintf(text, sizeof(text), "service %s grants SYS:r\n", path);
    assert(!supervisor_manifest_parse(text, strlen(text), &manifest));
}

static void parses_exact_span_without_terminator(void)
{
    const char source[] = "service SERVICES:test grants SYS:r";
    size_t length = sizeof(source) - 1u;
    char *text = malloc(length);
    SupervisorManifest manifest;

    assert(text != NULL);
    memcpy(text, source, length);
    assert(supervisor_manifest_parse(text, (uint32_t)length, &manifest));
    assert(manifest.count == 1u);
    assert(strcmp(manifest.entries[0].path, "SERVICES:test") == 0);
    free(text);

    text = malloc(length);
    assert(text != NULL);
    memcpy(text, source, length);
    fail_reallocation = 1;
    assert(!supervisor_manifest_parse(text, (uint32_t)length, &manifest));
    fail_reallocation = 0;
    assert(manifest.count == 0u);
    free(text);
}

int main(void)
{
    valid_manifest();
    refuses_whole_file();
    parses_bundle_grants();
    accepts_large_commented_manifest();
    path_uses_vfs_authority();
    parses_exact_span_without_terminator();
    return 0;
}
