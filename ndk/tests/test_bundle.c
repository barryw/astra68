#include <astra/bundle.h>

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
void astra_runtime_deallocate(void *pointer) { free(pointer); }

static void manifest_test(void)
{
    char text[] =
        "astra-bundle 1\nkind application\nid org.astra.terminal\n"
        "name \"Astra Terminal\"\nversion 0.1.0\n"
        "executable bin/m68k-68040/Terminal\n"
        "icon resources/Terminal.aicon\ncapability GUI\n"
        "requires graphics.library 1 1.0.0\n"
        "requires font.library 1 1.0.0 # comment\n";
    AstraBundleManifest manifest = ASTRA_BUNDLE_MANIFEST_INIT;
    uint32_t line = 0u;

    assert(astra_bundle_manifest_parse(text, sizeof(text) - 1u, &manifest,
                                       &line) == ASTRA_BUNDLE_OK);
    assert(manifest.kind == ASTRA_BUNDLE_APPLICATION);
    assert(strcmp(manifest.name, "Astra Terminal") == 0);
    assert(strcmp(manifest.executable, "bin/m68k-68040/Terminal") == 0);
    assert(manifest.require_count == 2u);
    assert(manifest.capability_count == 1u);
    {
        static const char exact_source[] =
            "astra-bundle 1\nkind application\nid org.astra.exactspan\n"
            "name Exact\nversion 1.0.0\nexecutable bin/app\nicon icon.aicon";
        size_t length = sizeof(exact_source) - 1u;
        char *exact = malloc(length);

        assert(exact != NULL);
        memcpy(exact, exact_source, length);
        assert(astra_bundle_manifest_parse(
                   exact, (uint32_t)length, &manifest, &line) ==
               ASTRA_BUNDLE_OK);
        free(exact);
        exact = malloc(length);
        assert(exact != NULL);
        memcpy(exact, exact_source, length);
        fail_reallocation = 1;
        assert(astra_bundle_manifest_parse(
                   exact, (uint32_t)length, &manifest, &line) ==
               ASTRA_BUNDLE_LIMIT);
        fail_reallocation = 0;
        free(exact);
    }
    {
        char capabilities[] =
            "astra-bundle 1\nkind application\nid org.astra.capabilities\n"
            "name Capabilities\nversion 1.0.0\nexecutable bin/app\n"
            "icon resources/app.aicon\n"
            "capability GUI\ncapability WORK:rw\n"
            "capability COMMANDS:r\ncapability LIBS:r\n"
            "capability EVENTS:r\ncapability PROC:r\n"
            "capability EVENT_CONTROL\ncapability NETWORK\n"
            "capability NETWORK_LISTEN\n";

        assert(astra_bundle_manifest_parse(
                   capabilities, sizeof(capabilities) - 1u, &manifest,
                   &line) == ASTRA_BUNDLE_OK);
        assert(manifest.capability_count == 9u);
    }
    {
        char bad[] = "astra-bundle 1\nkind application\nid Bad/Id\n";
        assert(astra_bundle_manifest_parse(bad, sizeof(bad) - 1u, &manifest,
                                           &line) == ASTRA_BUNDLE_INVALID);
        assert(line == 3u);
    }
    {
        char bad_utf8[] = {'n', 'a', 'm', 'e', ' ', (char)0xc0, (char)0x80,
                           '\n', '\0'};
        assert(astra_bundle_manifest_parse(bad_utf8,
                                           sizeof(bad_utf8) - 1u, &manifest,
                                           &line) == ASTRA_BUNDLE_INVALID);
    }
    {
        char bad_library[] =
            "astra-bundle 1\nkind kit\nid org.astra.bad\nname Bad\n"
            "version 1.0.0\nprovides ../bad 1 1.0.0\n";
        assert(astra_bundle_manifest_parse(
                   bad_library, sizeof(bad_library) - 1u, &manifest, &line) ==
               ASTRA_BUNDLE_INVALID);
    }
    {
        char exact_library[] =
            "astra-bundle 1\nkind kit\nid org.astra.exact\nname Exact\n"
            "version 1.0.0\nprovides abcdefghijklmnopqrstuvw 1 1.0.0\n";
        char long_library[] =
            "astra-bundle 1\nkind kit\nid org.astra.long\nname Long\n"
            "version 1.0.0\nprovides abcdefghijklmnopqrstuvwx 1 1.0.0\n";

        assert(strlen("abcdefghijklmnopqrstuvw") + 1u ==
               ASTRA_LIBRARY_NAME_MAX);
        assert(astra_bundle_manifest_parse(
                   exact_library, sizeof(exact_library) - 1u, &manifest,
                   &line) == ASTRA_BUNDLE_OK);
        assert(astra_bundle_manifest_parse(
                   long_library, sizeof(long_library) - 1u, &manifest,
                   &line) == ASTRA_BUNDLE_INVALID);
    }
    {
        char exact_capability[] =
            "astra-bundle 1\nkind application\nid org.astra.exactcap\n"
            "name Exact\nversion 1.0.0\nexecutable bin/app\n"
            "icon resources/app.aicon\ncapability ABCDEFGHIJKLMNO:rw\n";
        char long_capability[] =
            "astra-bundle 1\nkind application\nid org.astra.longcap\n"
            "name Long\nversion 1.0.0\nexecutable bin/app\n"
            "icon resources/app.aicon\ncapability ABCDEFGHIJKLMNOP:rw\n";

        assert(strlen("ABCDEFGHIJKLMNO:rw") + 1u ==
               ASTRA_BUNDLE_CAPABILITY_NAME_MAX);
        assert(astra_bundle_manifest_parse(
                   exact_capability, sizeof(exact_capability) - 1u,
                   &manifest, &line) == ASTRA_BUNDLE_OK);
        assert(astra_bundle_manifest_parse(
                   long_capability, sizeof(long_capability) - 1u,
                   &manifest, &line) == ASTRA_BUNDLE_INVALID);
    }
    {
        char large[5000u];
        const char prefix[] =
            "astra-bundle 1\nkind kit\nid org.astra.large\nname Large\n"
            "version 1.0.0\nprovides large.library 1 1.0.0\n# ";

        memcpy(large, prefix, sizeof(prefix) - 1u);
        memset(large + sizeof(prefix) - 1u, 'x',
               sizeof(large) - sizeof(prefix) - 1u);
        large[sizeof(large) - 2u] = '\n';
        large[sizeof(large) - 1u] = '\0';
        assert(astra_bundle_manifest_parse(
                   large, sizeof(large) - 1u, &manifest, &line) ==
               ASTRA_BUNDLE_OK);
        large[sizeof(prefix) - 1u] = (char)0xff;
        assert(astra_bundle_manifest_parse(
                   large, sizeof(large) - 1u, &manifest, &line) ==
               ASTRA_BUNDLE_INVALID);
    }
    {
        char long_path[ASTRA_BUNDLE_PATH_MAX + 1u];
        char path_manifest[ASTRA_BUNDLE_PATH_MAX + 160u];

        memset(long_path, 'p', 180u);
        long_path[180] = '\0';
        (void)snprintf(path_manifest, sizeof(path_manifest),
                       "astra-bundle 1\nkind application\n"
                       "id org.astra.path\nname Path\nversion 1.0.0\n"
                       "executable %s\nicon icon.aicon\n", long_path);
        assert(astra_bundle_manifest_parse(
                   path_manifest, strlen(path_manifest), &manifest, &line) ==
               ASTRA_BUNDLE_OK);
        assert(strlen(manifest.executable) == 180u);
        memset(long_path, 'p', ASTRA_BUNDLE_PATH_MAX);
        long_path[ASTRA_BUNDLE_PATH_MAX] = '\0';
        (void)snprintf(path_manifest, sizeof(path_manifest),
                       "astra-bundle 1\nkind application\n"
                       "id org.astra.path\nname Path\nversion 1.0.0\n"
                       "executable %s\nicon icon.aicon\n", long_path);
        assert(astra_bundle_manifest_parse(
                   path_manifest, strlen(path_manifest), &manifest, &line) ==
               ASTRA_BUNDLE_INVALID);
    }
    {
        char libraries[2048u] =
            "astra-bundle 1\nkind kit\nid org.astra.many\nname Many\n"
            "version 1.0.0\n";

        for (uint32_t index = 0u; index < 12u; ++index) {
            char line_text[64u];

            (void)snprintf(line_text, sizeof(line_text),
                           "provides library%u 1 1.0.0\n", index);
            (void)strcat(libraries, line_text);
        }
        assert(astra_bundle_manifest_parse(
                   libraries, strlen(libraries), &manifest, &line) ==
               ASTRA_BUNDLE_OK);
        assert(manifest.provide_count == 12u);
        assert(strcmp(manifest.provides[11].name, "library11") == 0);
    }
    {
        char unavailable[] =
            "astra-bundle 1\nkind kit\nid org.astra.unavailable\n"
            "name Unavailable\nversion 1.0.0\n"
            "provides unavailable.library 1 1.0.0\n";

        fail_reallocation = 1;
        assert(astra_bundle_manifest_parse(
                   unavailable, sizeof(unavailable) - 1u, &manifest, &line) ==
               ASTRA_BUNDLE_LIMIT);
        fail_reallocation = 0;
        assert(manifest.provides == NULL);
        assert(manifest.provide_count == 0u);
    }
    astra_bundle_manifest_destroy(&manifest);
}

static void put16(uint8_t *bytes, uint32_t at, uint16_t value)
{
    bytes[at] = (uint8_t)(value >> 8);
    bytes[at + 1u] = (uint8_t)value;
}

static void put32(uint8_t *bytes, uint32_t at, uint32_t value)
{
    bytes[at] = (uint8_t)(value >> 24);
    bytes[at + 1u] = (uint8_t)(value >> 16);
    bytes[at + 2u] = (uint8_t)(value >> 8);
    bytes[at + 3u] = (uint8_t)value;
}

static void icon_test(void)
{
    uint8_t bytes[32u + 4u + 3u * 16u + 16u * 16u + 32u * 32u +
                  64u * 64u] = {0};
    AstraAicon icon;
    AstraAiconStrike strike;
    uint32_t data = 32u + 4u + 3u * 16u;

    put32(bytes, 0u, ASTRA_AICON_MAGIC);
    put16(bytes, 4u, ASTRA_AICON_VERSION);
    put16(bytes, 6u, ASTRA_AICON_HEADER_SIZE);
    put32(bytes, 8u, sizeof(bytes));
    put16(bytes, 12u, 3u);
    put16(bytes, 14u, 1u);
    put32(bytes, 16u, 32u);
    put32(bytes, 20u, 36u);
    put32(bytes, 24u, data);
    for (uint32_t at = 0u, size = 16u; at < 3u; ++at, size *= 2u) {
        uint32_t record = 36u + at * 16u;
        put16(bytes, record, (uint16_t)size);
        put16(bytes, record + 2u, (uint16_t)size);
        put32(bytes, record + 4u, data);
        put32(bytes, record + 8u, size * size);
        data += size * size;
    }
    assert(astra_aicon_open(bytes, sizeof(bytes), &icon) == ASTRA_BUNDLE_OK);
    assert(astra_aicon_strike(&icon, 32u, &strike) == ASTRA_BUNDLE_OK);
    assert(strike.width == 32u && strike.length == 1024u);
    assert(astra_aicon_strike(&icon, ASTRA_AICON_STRIKE_WIDTH_MAX, &strike) ==
           ASTRA_BUNDLE_OK);
    assert(strike.width == ASTRA_AICON_STRIKE_WIDTH_MAX &&
           strike.length == ASTRA_AICON_STRIKE_WIDTH_MAX *
                            ASTRA_AICON_STRIKE_WIDTH_MAX);
    put16(bytes, 36u + 2u * ASTRA_AICON_STRIKE_SIZE, 128u);
    assert(astra_aicon_open(bytes, sizeof(bytes), &icon) ==
           ASTRA_BUNDLE_INVALID);
    put16(bytes, 36u + 2u * ASTRA_AICON_STRIKE_SIZE,
          ASTRA_AICON_STRIKE_WIDTH_MAX);
    assert(astra_aicon_open(bytes, sizeof(bytes), &icon) == ASTRA_BUNDLE_OK);
    bytes[28] = 1u;
    assert(astra_aicon_open(bytes, sizeof(bytes), &icon) ==
           ASTRA_BUNDLE_INVALID);
    assert(icon.bytes == NULL);
    bytes[28] = 0u;
    bytes[32u + 4u + 3u * 16u] = 1u;
    assert(astra_aicon_open(bytes, sizeof(bytes), &icon) ==
           ASTRA_BUNDLE_INVALID);
}

int main(void)
{
    manifest_test();
    icon_test();
    return 0;
}
