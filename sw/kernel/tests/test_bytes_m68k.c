#include <stdint.h>

#include "bytes.h"

static uint32_t storage[1032];
static uint8_t source[99];
static uint8_t destination[99];

static int check_copy(void)
{
    for (uint32_t source_offset = 0u; source_offset < 4u; ++source_offset) {
        for (uint32_t destination_offset = 0u;
             destination_offset < 4u; ++destination_offset) {
            for (uint32_t size = 0u; size <= 80u; ++size) {
                uint32_t source_start = 8u + source_offset;
                uint32_t destination_start = 8u + destination_offset;

                for (uint32_t index = 0u; index < sizeof(source); ++index)
                    source[index] = (uint8_t)(index * 37u + 11u);
                for (uint32_t index = 0u; index < sizeof(destination); ++index)
                    destination[index] = 0x5au;
                kernel_bytes_copy(&destination[destination_start],
                                  &source[source_start], size);
                for (uint32_t index = 0u; index < destination_start; ++index) {
                    if (destination[index] != 0x5au)
                        return 1;
                }
                for (uint32_t index = 0u; index < size; ++index) {
                    if (destination[destination_start + index] !=
                        source[source_start + index])
                        return 1;
                }
                for (uint32_t index = destination_start + size;
                     index < sizeof(destination); ++index) {
                    if (destination[index] != 0x5au)
                        return 1;
                }
            }
        }
    }
    return 0;
}

static int check_fill(uint32_t count)
{
    uint32_t end = count + 8u;

    for (uint32_t index = 0u; index < end; ++index)
        storage[index] = 0x11223344u;

    kernel_words_fill(&storage[4], count, 0xa5a55a5au);
    for (uint32_t index = 0u; index < 4u; ++index) {
        if (storage[index] != 0x11223344u)
            return 1;
    }
    for (uint32_t index = 4u; index < 4u + count; ++index) {
        if (storage[index] != 0xa5a55a5au)
            return 1;
    }
    for (uint32_t index = 4u + count; index < end; ++index) {
        if (storage[index] != 0x11223344u)
            return 1;
    }
    return 0;
}

int main(void)
{
    if (check_copy() != 0)
        return 1;

    for (uint32_t count = 0u; count <= 65u; ++count) {
        if (check_fill(count) != 0)
            return 1;
    }

    for (uint32_t index = 0u; index < 1032u; ++index)
        storage[index] = 0x11223344u;
    kernel_words_fill(&storage[4], 1024u, 0xa5a55a5au);
    for (uint32_t index = 0u; index < 4u; ++index) {
        if (storage[index] != 0x11223344u)
            return 1;
    }
    for (uint32_t index = 4u; index < 1028u; ++index) {
        if (storage[index] != 0xa5a55a5au)
            return 1;
    }
    for (uint32_t index = 1028u; index < 1032u; ++index) {
        if (storage[index] != 0x11223344u)
            return 1;
    }
    return 0;
}
