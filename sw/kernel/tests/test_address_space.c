#include <astra/address_space.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct AddressRegion {
    const char *name;
    const char *owner;
    uint32_t start;
    uint32_t end;
} AddressRegion;

#define ADDRESS_REGION(id, name, start, end, owner) \
    { #name, owner, (start), (end) },

static const AddressRegion regions[] = {
    ASTRA_USER_ADDRESS_REGIONS(ADDRESS_REGION)
};

#undef ADDRESS_REGION

int main(void)
{
    uint32_t cursor = 0u;

    assert(sizeof(regions) / sizeof(regions[0]) != 0u);
    for (uint32_t index = 0u;
         index < sizeof(regions) / sizeof(regions[0]); ++index) {
        assert(regions[index].name[0] != '\0');
        assert(regions[index].owner[0] != '\0');
        assert(regions[index].start == cursor);
        assert(regions[index].end > regions[index].start);
        assert(astra_address_region(regions[index].start,
                                    regions[index].end -
                                        regions[index].start) ==
               (AstraAddressRegion)index);
        assert(astra_address_region(regions[index].end - 1u, 1u) ==
               (AstraAddressRegion)index);
        cursor = regions[index].end;
    }
    assert(cursor == ASTRA_USER_ADDRESS_END);
    assert(astra_address_region(0u, 0u) == ASTRA_ADDRESS_REGION_INVALID);
    assert(astra_address_region(ASTRA_NULL_GUARD_END - 1u, 2u) ==
           ASTRA_ADDRESS_REGION_INVALID);
    assert(astra_address_region(ASTRA_USER_ADDRESS_END, 1u) ==
           ASTRA_ADDRESS_REGION_INVALID);
    assert(astra_address_region(UINT32_MAX, 2u) ==
           ASTRA_ADDRESS_REGION_INVALID);
    puts("KERNEL ADDRESS SPACE PASS");
    return 0;
}
