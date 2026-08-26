#include "bytes.h"

#include <stdint.h>

typedef uint32_t KernelBytesWord __attribute__((may_alias));

#define KERNEL_BYTES_WORD_SIZE ((uint32_t)sizeof(KernelBytesWord))
#define KERNEL_BYTES_WORD_MASK (KERNEL_BYTES_WORD_SIZE - 1u)

static void clear_words(KernelBytesWord **destination, uint32_t *size)
{
    KernelBytesWord *words = *destination;
    uint32_t remaining = *size;

    while (remaining >= 8u * KERNEL_BYTES_WORD_SIZE) {
        words[0] = 0u;
        words[1] = 0u;
        words[2] = 0u;
        words[3] = 0u;
        words[4] = 0u;
        words[5] = 0u;
        words[6] = 0u;
        words[7] = 0u;
        words += 8;
        remaining -= 8u * KERNEL_BYTES_WORD_SIZE;
    }
    while (remaining >= KERNEL_BYTES_WORD_SIZE) {
        *words++ = 0u;
        remaining -= KERNEL_BYTES_WORD_SIZE;
    }
    *destination = words;
    *size = remaining;
}

void kernel_bytes_clear(void *destination, uint32_t size)
{
    uint8_t *bytes = destination;

    while (size != 0u &&
           ((uintptr_t)bytes & KERNEL_BYTES_WORD_MASK) != 0u) {
        *bytes++ = 0u;
        --size;
    }
    KernelBytesWord *words = (KernelBytesWord *)(void *)bytes;
    clear_words(&words, &size);
    bytes = (uint8_t *)(void *)words;
    while (size-- != 0u)
        *bytes++ = 0u;
}

void kernel_words_fill(volatile uint32_t *destination,
                       uint32_t word_count, uint32_t value)
{
    while (word_count >= 8u) {
        destination[0] = value;
        destination[1] = value;
        destination[2] = value;
        destination[3] = value;
        destination[4] = value;
        destination[5] = value;
        destination[6] = value;
        destination[7] = value;
        destination += 8;
        word_count -= 8u;
    }
    while (word_count-- != 0u)
        *destination++ = value;
}

static void copy_words(KernelBytesWord **destination,
                       const KernelBytesWord **source, uint32_t *size)
{
    KernelBytesWord *out = *destination;
    const KernelBytesWord *in = *source;
    uint32_t remaining = *size;

    while (remaining >= 8u * KERNEL_BYTES_WORD_SIZE) {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        out[3] = in[3];
        out[4] = in[4];
        out[5] = in[5];
        out[6] = in[6];
        out[7] = in[7];
        out += 8;
        in += 8;
        remaining -= 8u * KERNEL_BYTES_WORD_SIZE;
    }
    while (remaining >= KERNEL_BYTES_WORD_SIZE) {
        *out++ = *in++;
        remaining -= KERNEL_BYTES_WORD_SIZE;
    }
    *destination = out;
    *source = in;
    *size = remaining;
}

void kernel_bytes_copy(void *destination, const void *source, uint32_t size)
{
    uint8_t *out = destination;
    const uint8_t *in = source;

    if ((((uintptr_t)out ^ (uintptr_t)in) & KERNEL_BYTES_WORD_MASK) == 0u) {
        while (size != 0u &&
               ((uintptr_t)out & KERNEL_BYTES_WORD_MASK) != 0u) {
            *out++ = *in++;
            --size;
        }
        KernelBytesWord *out_words = (KernelBytesWord *)(void *)out;
        const KernelBytesWord *in_words =
            (const KernelBytesWord *)(const void *)in;
        copy_words(&out_words, &in_words, &size);
        out = (uint8_t *)(void *)out_words;
        in = (const uint8_t *)(const void *)in_words;
    }
    while (size-- != 0u)
        *out++ = *in++;
}

bool kernel_bytes_equal(const void *left, const void *right, uint32_t size)
{
    const uint8_t *a = left;
    const uint8_t *b = right;

    while (size-- != 0u)
        if (*a++ != *b++)
            return false;
    return true;
}
