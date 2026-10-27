#include <assert.h>

#include "../src/package.c"

int main(void)
{
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
