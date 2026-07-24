#include "bytes.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_GUARD_SIZE 8u
#define TEST_MAX_SIZE   80u
#define TEST_STORAGE_SIZE \
    (TEST_GUARD_SIZE + 3u + TEST_MAX_SIZE + TEST_GUARD_SIZE)

static void fill(uint8_t *bytes, uint32_t size, uint8_t value)
{
    for (uint32_t index = 0u; index < size; ++index)
        bytes[index] = value;
}

static void test_clear(void)
{
    uint8_t storage[TEST_STORAGE_SIZE];

    for (uint32_t offset = 0u; offset < 4u; ++offset) {
        for (uint32_t size = 0u; size <= TEST_MAX_SIZE; ++size) {
            uint32_t start = TEST_GUARD_SIZE + offset;

            fill(storage, sizeof(storage), 0xa5u);
            kernel_bytes_clear(&storage[start], size);
            for (uint32_t index = 0u; index < start; ++index)
                assert(storage[index] == 0xa5u);
            for (uint32_t index = start; index < start + size; ++index)
                assert(storage[index] == 0u);
            for (uint32_t index = start + size;
                 index < sizeof(storage); ++index)
                assert(storage[index] == 0xa5u);
        }
    }
}

static void test_copy(void)
{
    uint8_t source[TEST_STORAGE_SIZE];
    uint8_t destination[TEST_STORAGE_SIZE];

    for (uint32_t source_offset = 0u; source_offset < 4u; ++source_offset) {
        for (uint32_t destination_offset = 0u;
             destination_offset < 4u; ++destination_offset) {
            for (uint32_t size = 0u; size <= TEST_MAX_SIZE; ++size) {
                uint32_t source_start = TEST_GUARD_SIZE + source_offset;
                uint32_t destination_start =
                    TEST_GUARD_SIZE + destination_offset;

                for (uint32_t index = 0u; index < sizeof(source); ++index)
                    source[index] = (uint8_t)(index * 37u + 11u);
                fill(destination, sizeof(destination), 0x5au);
                kernel_bytes_copy(&destination[destination_start],
                                  &source[source_start], size);
                for (uint32_t index = 0u; index < destination_start; ++index)
                    assert(destination[index] == 0x5au);
                for (uint32_t index = 0u; index < size; ++index)
                    assert(destination[destination_start + index] ==
                           source[source_start + index]);
                for (uint32_t index = destination_start + size;
                     index < sizeof(destination); ++index)
                    assert(destination[index] == 0x5au);
            }
        }
    }
}

int main(void)
{
    test_clear();
    test_copy();
    puts("byte primitive tests passed");
    return 0;
}
