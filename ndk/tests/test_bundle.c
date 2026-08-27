#include <astra/bundle.h>

#include <assert.h>
#include <string.h>

static void manifest_test(void)
{
    char text[] =
        "astra-bundle 1\nkind application\nid org.astra.terminal\n"
        "name \"Astra Terminal\"\nversion 0.1.0\n"
        "executable bin/m68k-68030/Terminal\n"
        "icon resources/Terminal.aicon\ncapability GUI\n"
        "requires graphics.library 1 1.0.0\n"
        "requires font.library 1 1.0.0 # comment\n";
    AstraBundleManifest manifest;
    uint32_t line = 0u;

    assert(astra_bundle_manifest_parse(text, sizeof(text) - 1u, &manifest,
                                       &line) == ASTRA_BUNDLE_OK);
    assert(manifest.kind == ASTRA_BUNDLE_APPLICATION);
    assert(strcmp(manifest.name, "Astra Terminal") == 0);
    assert(strcmp(manifest.executable, "bin/m68k-68030/Terminal") == 0);
    assert(manifest.require_count == 2u);
    assert(manifest.capability_count == 1u);
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
