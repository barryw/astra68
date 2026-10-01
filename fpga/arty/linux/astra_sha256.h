#ifndef ASTRA_SHA256_H
#define ASTRA_SHA256_H

/* SHA-256 (FIPS 180-4), for naming host-side assets such as SoundFonts by
 * their content. Streaming: init, any number of updates, final. */

#include <stddef.h>
#include <stdint.h>

typedef struct AstraSha256 {
    uint32_t state[8];
    uint64_t length;
    uint8_t block[64];
    uint32_t used;
} AstraSha256;

void astra_sha256_init(AstraSha256 *context);
void astra_sha256_update(AstraSha256 *context, const void *data,
                         size_t bytes);
void astra_sha256_final(AstraSha256 *context, uint8_t digest[32]);

#endif
