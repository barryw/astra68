#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t comparisons;
static int counted_strcmp(const char *left, const char *right)
{
    ++comparisons;
    return strcmp(left, right);
}

#define strcmp counted_strcmp
#include "../src/package.c"
#undef strcmp

int main(void)
{
    PackageEntry entries[1024];
    char names[1024][8];
    for (uint32_t at = 0u; at < 1024u; ++at) {
        uint32_t descending = 1023u - at;
        names[at][0] = (char)('0' + descending / 1000u);
        names[at][1] = (char)('0' + (descending / 100u) % 10u);
        names[at][2] = (char)('0' + (descending / 10u) % 10u);
        names[at][3] = (char)('0' + descending % 10u);
        names[at][4] = '\0';
        entries[at] = (PackageEntry){.path = names[at]};
    }
    PackageList list = {.entries = entries, .count = 1024u};
    comparisons = 0u;
    sort_entries(&list);
    assert(comparisons < 1024u * 32u);
    assert(strcmp(entries[0].path, "0000") == 0);
    assert(strcmp(entries[1023].path, "1023") == 0);
    uint8_t header[512];
    assert(tar_build_header(header, "files.sha256", 0644u, 32u,
                            '0', NULL) == ASTRA_VFS_OK);
    uint64_t checksum = 0u;
    assert(parse_octal(header + 148u, 8u, &checksum));
    uint32_t sum = 0u;
    for (uint32_t at = 0u; at < sizeof(header); ++at)
        sum += at >= 148u && at < 156u ? (uint8_t)' ' : header[at];
    assert(checksum == sum);
    assert(valid_component("Doom.app"));
    assert(!valid_component(".."));
    assert(!valid_component("bad/name"));
    assert(!valid_component("bad:name"));
    assert(safe_link("Doom.app/resources/icon", "../bin/doom"));
    assert(safe_link("Doom.app/link", "bin/doom"));
    assert(!safe_link("Doom.app/link", "../outside"));
    assert(!safe_link("Doom.app/resources/link", "../../outside"));
    assert(!safe_link("Doom.app/link", "/absolute"));
    assert(!safe_link("Doom.app/link", "bad//path"));
    return 0;
}
