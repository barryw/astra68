#include "self_relocate.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct TestDynamic {
    int32_t tag;
    uint32_t value;
} TestDynamic;

typedef struct TestRela {
    uint32_t offset;
    uint32_t info;
    int32_t addend;
} TestRela;

typedef struct TestImage {
    TestRela relocations[2];
    uint32_t targets[2];
    TestDynamic dynamic[4];
} TestImage;

static void
valid_metadata(TestImage *image)
{
    uintptr_t base = (uintptr_t)image;

    (void)memset(image, 0, sizeof(*image));
    image->relocations[0] = (TestRela){
        (uint32_t)((uintptr_t)&image->targets[0] - base), 22u, 4
    };
    image->relocations[1] = (TestRela){
        (uint32_t)((uintptr_t)&image->targets[1] - base), 42u, 0x7010
    };
    image->dynamic[0] = (TestDynamic){
        7, (uint32_t)((uintptr_t)image->relocations - base)
    };
    image->dynamic[1] = (TestDynamic){8, sizeof(image->relocations)};
    image->dynamic[2] = (TestDynamic){9, sizeof(TestRela)};
    image->dynamic[3] = (TestDynamic){0, 0u};
}

static int
relocate(TestImage *image)
{
    return astra_loader_self_relocate(
        (uintptr_t)image, image->dynamic, image->dynamic + 4,
        (uintptr_t)image->targets,
        (uintptr_t)(image->targets + 2),
        (uintptr_t)(image + 1));
}

int
main(void)
{
    TestImage image;

    valid_metadata(&image);
    assert(relocate(&image));
    assert(image.targets[0] == (uint32_t)((uintptr_t)&image + 4u));
    assert(image.targets[1] == 0x10u);

    valid_metadata(&image);
    image.dynamic[3].tag = 1;
    assert(!relocate(&image));
    valid_metadata(&image);
    image.relocations[0].offset = 0xfffffffcu;
    assert(!relocate(&image));
    valid_metadata(&image);
    image.dynamic[0].value = (uint32_t)sizeof(image);
    assert(!relocate(&image));
    valid_metadata(&image);
    image.relocations[0].info = 19u;
    assert(!relocate(&image));
    return 0;
}
