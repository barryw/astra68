#include "self_relocate.h"

#include <stddef.h>
#include <stdint.h>

#include <astra/tls.h>

typedef struct SelfDynamic {
    int32_t tag;
    uint32_t value;
} SelfDynamic;

typedef struct SelfRela {
    uint32_t offset;
    uint32_t info;
    int32_t addend;
} SelfRela;

enum {
    SELF_DT_NULL = 0,
    SELF_DT_RELA = 7,
    SELF_DT_RELASZ = 8,
    SELF_DT_RELAENT = 9,
    SELF_R_68K_RELATIVE = 22,
    SELF_R_68K_TLS_TPREL32 = 42
};

_Static_assert(sizeof(SelfDynamic) == 8u, "unexpected Elf32_Dyn size");
_Static_assert(sizeof(SelfRela) == 12u, "unexpected Elf32_Rela size");

static int
range_contains(uintptr_t start, uintptr_t end, uintptr_t address, size_t size)
{
    return end >= start && address >= start && address <= end &&
           size <= end - address;
}

int
astra_loader_self_relocate(uintptr_t base, const void *dynamic_address,
                           const void *dynamic_end_address,
                           uintptr_t writable_start, uintptr_t writable_end,
                           uintptr_t image_end)
{
    const SelfDynamic *dynamic = dynamic_address;
    const SelfDynamic *dynamic_end = dynamic_end_address;
    uint32_t rela_offset = 0u;
    uint32_t rela_size = 0u;
    uint32_t rela_entry = 0u;
    unsigned seen = 0u;
    int terminated = 0;

    if (dynamic == NULL || dynamic_end <= dynamic || image_end < base ||
        !range_contains(base, image_end, (uintptr_t)dynamic,
                        (uintptr_t)dynamic_end - (uintptr_t)dynamic) ||
        writable_start < base || writable_end > image_end ||
        writable_end < writable_start)
        return 0;
    for (; dynamic < dynamic_end; ++dynamic) {
        unsigned bit;
        uint32_t *value;

        if (dynamic->tag == SELF_DT_NULL) {
            terminated = 1;
            break;
        }
        if (dynamic->tag == SELF_DT_RELA) {
            bit = 1u;
            value = &rela_offset;
        } else if (dynamic->tag == SELF_DT_RELASZ) {
            bit = 2u;
            value = &rela_size;
        } else if (dynamic->tag == SELF_DT_RELAENT) {
            bit = 4u;
            value = &rela_entry;
        } else {
            continue;
        }
        if ((seen & bit) != 0u)
            return 0;
        seen |= bit;
        *value = dynamic->value;
    }
    if (!terminated || seen != 7u || rela_size == 0u ||
        rela_entry != sizeof(SelfRela) || rela_size % sizeof(SelfRela) != 0u ||
        rela_offset > image_end - base ||
        rela_size > image_end - base - rela_offset)
        return 0;

    const SelfRela *rela = (const SelfRela *)(base + rela_offset);
    const SelfRela *rela_end =
        (const SelfRela *)((const uint8_t *)rela + rela_size);

    for (; rela < rela_end; ++rela) {
        uint32_t kind = rela->info & 0xffu;
        uintptr_t target;

        if (kind == 0u)
            continue;
        if ((rela->info >> 8) != 0u ||
            (kind != SELF_R_68K_RELATIVE &&
             kind != SELF_R_68K_TLS_TPREL32) ||
            rela->offset > image_end - base ||
            (rela->offset & (sizeof(uint32_t) - 1u)) != 0u)
            return 0;
        target = base + rela->offset;
        if (!range_contains(writable_start, writable_end, target,
                            sizeof(uint32_t)))
            return 0;
        if (kind == SELF_R_68K_RELATIVE)
            *(uint32_t *)target = (uint32_t)(base + (uint32_t)rela->addend);
        else
            *(uint32_t *)target =
                (uint32_t)rela->addend - ASTRA_M68K_TLS_THREAD_POINTER_BIAS;
    }
    return 1;
}
