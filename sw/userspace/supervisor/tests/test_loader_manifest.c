#include <loader.h>

#include <assert.h>
#include <string.h>

static void valid_manifest(void)
{
    char text[] =
        "# shipped startup\n"
        "service SERVICES:storage grants BLOCK_DEVICE BLOCK_IRQ "
        "serves SYS:r required\n"
        "service SERVICES:events grants SYS:r STORE:rw serves EVENTS:r\n";
    SupervisorManifest manifest;

    assert(supervisor_manifest_parse(text, sizeof(text) - 1u, &manifest));
    assert(manifest.count == 2u);
    assert(manifest.entries[0].required == 1u);
    assert(manifest.entries[0].grant_count == 2u);
    assert(strcmp(manifest.entries[0].grants[0].name,
                  "BLOCK_DEVICE") == 0);
    assert(manifest.entries[0].grants[0].is_namespace == 0u);
    assert(strcmp(manifest.entries[0].serves, "SYS") == 0);
    assert(manifest.entries[1].grants[1].rights ==
           (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));

    {
        char terminal[] =
        "application SERVICES:terminal grants DISPLAY INPUT INPUT_IRQ "
            "WORK:rw COMMANDS:r LIBS:r EVENTS:r EVENT_CONTROL delegates "
            "required\n";

        assert(supervisor_manifest_parse(terminal, sizeof(terminal) - 1u,
                                         &manifest));
        assert(manifest.count == 1u);
        assert(manifest.entries[0].grant_count == 8u);
        assert(manifest.entries[0].resident == 0u);
        assert(manifest.entries[0].delegates == 1u);
        assert(manifest.entries[0].serves[0] == '\0');
    }
    {
        char display[] =
            "service SERVICES:display grants DISPLAY DISPLAY_IRQ "
            "serves GUI required\n"
            "application SERVICES:terminal grants GUI WORK:rw COMMANDS:r "
            "LIBS:r EVENTS:r EVENT_CONTROL delegates required\n";

        assert(supervisor_manifest_parse(display, sizeof(display) - 1u,
                                         &manifest));
        assert(manifest.count == 2u);
        assert(manifest.entries[0].grant_count == 2u);
        assert(manifest.entries[0].resident == 1u);
        assert(strcmp(manifest.entries[0].grants[1].name,
                      "DISPLAY_IRQ") == 0);
        assert(strcmp(manifest.entries[0].serves, "GUI") == 0);
        assert(manifest.entries[0].serves_rights == 0u);
        assert(manifest.entries[1].grant_count == 6u);
        assert(manifest.entries[1].resident == 0u);
        assert(strcmp(manifest.entries[1].grants[0].name, "GUI") == 0);
        assert(manifest.entries[1].delegates == 1u);
    }
    {
        char installer[] =
            "application SERVICES:installer grants LIBS:rw required\n";

        assert(supervisor_manifest_parse(installer, sizeof(installer) - 1u,
                                         &manifest));
        assert(manifest.entries[0].grant_count == 1u);
        assert(manifest.entries[0].grants[0].rights ==
               (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE));
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
    char too_many[] =
        "service SERVICES:storage grants BLOCK_DEVICE serves SYS:r required\n"
        "service SERVICES:events grants STORE:rw serves EVENTS:r\n"
        "service SERVICES:input grants SYS:r serves INPUT:r\n"
        "service SERVICES:display grants SYS:r serves DISPLAY:r\n"
        "service SERVICES:desktop grants GUI\n"
        "service SERVICES:extra grants SYS:r\n";
    SupervisorManifest manifest;

    assert(!supervisor_manifest_parse(bad_right, sizeof(bad_right) - 1u,
                                      &manifest));
    assert(manifest.count == 0u);
    assert(!supervisor_manifest_parse(wrong_order,
                                      sizeof(wrong_order) - 1u, &manifest));
    assert(!supervisor_manifest_parse(command, sizeof(command) - 1u,
                                      &manifest));
    assert(!supervisor_manifest_parse(too_many, sizeof(too_many) - 1u,
                                      &manifest));
    assert(manifest.count == 0u);
}

int main(void)
{
    valid_manifest();
    refuses_whole_file();
    return 0;
}
