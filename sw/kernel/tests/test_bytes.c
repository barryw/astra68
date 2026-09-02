#include "bytes.h"

#include <astra/endian.h>

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

static void test_equal(void)
{
    uint8_t left[TEST_MAX_SIZE + 3u];
    uint8_t right[TEST_MAX_SIZE + 3u];

    for (uint32_t left_offset = 0u; left_offset < 4u; ++left_offset) {
        for (uint32_t right_offset = 0u; right_offset < 4u; ++right_offset) {
            for (uint32_t size = 0u; size <= TEST_MAX_SIZE; ++size) {
                for (uint32_t index = 0u; index < size; ++index) {
                    uint8_t value = (uint8_t)(index * 37u + 11u);

                    left[left_offset + index] = value;
                    right[right_offset + index] = value;
                }
                assert(kernel_bytes_equal(left + left_offset,
                                          right + right_offset, size));
                for (uint32_t index = 0u; index < size; ++index) {
                    right[right_offset + index] ^= 0x80u;
                    assert(!kernel_bytes_equal(left + left_offset,
                                               right + right_offset, size));
                    right[right_offset + index] ^= 0x80u;
                }
            }
        }
    }
    assert(kernel_bytes_equal(NULL, NULL, 0u));
}

static void test_word_fill(void)
{
    uint32_t storage[24];

    for (uint32_t count = 0u; count <= 16u; ++count) {
        for (uint32_t index = 0u; index < 24u; ++index)
            storage[index] = 0x11223344u;
        kernel_words_fill(&storage[4], count, 0xa5a55a5au);
        for (uint32_t index = 0u; index < 4u; ++index)
            assert(storage[index] == 0x11223344u);
        for (uint32_t index = 4u; index < 4u + count; ++index)
            assert(storage[index] == 0xa5a55a5au);
        for (uint32_t index = 4u + count; index < 24u; ++index)
            assert(storage[index] == 0x11223344u);
    }
}

static void test_unaligned_big_endian(void)
{
    uint8_t storage[12] = {0xa5u};

    astra_store_be16(&storage[1], 0x1234u);
    astra_store_be32(&storage[3], 0x89abcdefu);
    astra_store_be64(&storage[2], UINT64_C(0x0123456789abcdef));
    assert(astra_load_be64(&storage[2]) == UINT64_C(0x0123456789abcdef));
    astra_store_be16(&storage[1], 0x1234u);
    astra_store_be32(&storage[3], 0x89abcdefu);
    assert(astra_load_be16(&storage[1]) == 0x1234u);
    assert(astra_load_be32(&storage[3]) == 0x89abcdefu);
    assert(storage[0] == 0xa5u && storage[10] == 0u && storage[11] == 0u);
}

int main(void)
{
    test_clear();
    test_word_fill();
    test_copy();
    test_equal();
    test_unaligned_big_endian();
    puts("byte primitive tests passed");
    return 0;
}
