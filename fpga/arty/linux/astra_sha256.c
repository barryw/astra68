// SPDX-License-Identifier: MIT

#include "astra_sha256.h"

#include <string.h>

static const uint32_t round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t rotate(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void compress(AstraSha256 *context, const uint8_t *block)
{
    uint32_t schedule[64];
    uint32_t a = context->state[0], b = context->state[1];
    uint32_t c = context->state[2], d = context->state[3];
    uint32_t e = context->state[4], f = context->state[5];
    uint32_t g = context->state[6], h = context->state[7];

    for (unsigned i = 0u; i < 16u; ++i)
        schedule[i] = (uint32_t)block[4u * i] << 24 |
                      (uint32_t)block[4u * i + 1u] << 16 |
                      (uint32_t)block[4u * i + 2u] << 8 |
                      block[4u * i + 3u];
    for (unsigned i = 16u; i < 64u; ++i) {
        uint32_t s0 = rotate(schedule[i - 15u], 7u) ^
                      rotate(schedule[i - 15u], 18u) ^
                      (schedule[i - 15u] >> 3);
        uint32_t s1 = rotate(schedule[i - 2u], 17u) ^
                      rotate(schedule[i - 2u], 19u) ^
                      (schedule[i - 2u] >> 10);

        schedule[i] = schedule[i - 16u] + s0 + schedule[i - 7u] + s1;
    }
    for (unsigned i = 0u; i < 64u; ++i) {
        uint32_t t1 = h + (rotate(e, 6u) ^ rotate(e, 11u) ^ rotate(e, 25u)) +
                      ((e & f) ^ (~e & g)) + round_constants[i] + schedule[i];
        uint32_t t2 = (rotate(a, 2u) ^ rotate(a, 13u) ^ rotate(a, 22u)) +
                      ((a & b) ^ (a & c) ^ (b & c));

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void astra_sha256_init(AstraSha256 *context)
{
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    memcpy(context->state, initial, sizeof(initial));
    context->length = 0u;
    context->used = 0u;
}

void astra_sha256_update(AstraSha256 *context, const void *data,
                         size_t bytes)
{
    const uint8_t *from = data;

    context->length += bytes;
    while (bytes != 0u) {
        size_t take = 64u - context->used;

        if (take > bytes)
            take = bytes;
        memcpy(context->block + context->used, from, take);
        context->used += (uint32_t)take;
        from += take;
        bytes -= take;
        if (context->used == 64u) {
            compress(context, context->block);
            context->used = 0u;
        }
    }
}

void astra_sha256_final(AstraSha256 *context, uint8_t digest[32])
{
    uint64_t bits = context->length * 8u;

    context->block[context->used++] = 0x80u;
    if (context->used > 56u) {
        memset(context->block + context->used, 0, 64u - context->used);
        compress(context, context->block);
        context->used = 0u;
    }
    memset(context->block + context->used, 0, 56u - context->used);
    for (unsigned i = 0u; i < 8u; ++i)
        context->block[56u + i] = (uint8_t)(bits >> (56u - 8u * i));
    compress(context, context->block);
    for (unsigned i = 0u; i < 8u; ++i) {
        digest[4u * i] = (uint8_t)(context->state[i] >> 24);
        digest[4u * i + 1u] = (uint8_t)(context->state[i] >> 16);
        digest[4u * i + 2u] = (uint8_t)(context->state[i] >> 8);
        digest[4u * i + 3u] = (uint8_t)context->state[i];
    }
}
