#include "astra_font_convert.h"
#include "astra_font_host.h"

#include <astra/afnt.h>
#include <astra/font_worker.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path = "../../../sw/userspace/graphics/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-Regular.ttf";
    FILE *file = fopen(path, "rb");
    AstraFontHostResult result = {0};
    AstraAfntView view;
    uint8_t *source;
    long size;

    assert(argc == 2 && file != NULL && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0 && size <= ASTRA_FONT_WORKER_SOURCE_MAX &&
           fseek(file, 0, SEEK_SET) == 0);
    source = malloc((size_t)size);
    assert(source != NULL &&
           fread(source, 1u, (size_t)size, file) == (size_t)size &&
           fclose(file) == 0);
    assert(astra_font_host_convert(argv[1], source, (uint32_t)size, 0u, 0u,
        "test.ttf", 8u, "OFL-1.1", 7u, &result) == 0);
    assert(result.status == ASTRA_FONT_CONVERT_OK && result.faces >= 1u &&
           result.afnt_size > 128u &&
           astra_afnt_validate(result.afnt, result.afnt_size, &view));
    free(result.afnt);
    assert(astra_font_host_convert(argv[1], source, (uint32_t)size, 0u, 0u,
        "test.ttf", 8u, "", 0u, &result) == 0);
    assert(result.status == ASTRA_FONT_CONVERT_OK &&
           astra_afnt_validate(result.afnt, result.afnt_size, &view));
    free(result.afnt);
    memset(source, 0, 16u);
    assert(astra_font_host_convert(argv[1], source, (uint32_t)size, 0u, 0u,
        "test.ttf", 8u, "", 0u, &result) == 0);
    assert(result.status == ASTRA_FONT_CONVERT_INVALID &&
           result.afnt == NULL && result.afnt_size == 0u);
    assert(astra_font_host_convert("/no/such/astra-font-worker", source,
        (uint32_t)size, 0u, 0u, "test.ttf", 8u, "", 0u,
        &result) == -ENOENT);
    assert(astra_font_host_convert("/bin/true", source, (uint32_t)size,
        0u, 0u, "test.ttf", 8u, "", 0u, &result) < 0);
    free(source);
    puts("QEMU font worker invocation passed");
    return 0;
}
