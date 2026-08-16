#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int astra_vfs_process_test_parse_version(const char *name, uint16_t minimum,
                                         uint16_t version[3]);

int main(void)
{
    uint16_t version[3];

    assert(astra_vfs_process_test_parse_version("1.12.3", 1u, version));
    assert(version[0] == 1u && version[1] == 12u && version[2] == 3u);
    assert(!astra_vfs_process_test_parse_version("0.99.0", 1u, version));
    assert(!astra_vfs_process_test_parse_version("1.2", 1u, version));
    assert(!astra_vfs_process_test_parse_version("1.2.3x", 1u, version));
    assert(!astra_vfs_process_test_parse_version("65536.0.0", 1u, version));
    puts("astra process library resolver: PASS");
    return 0;
}
