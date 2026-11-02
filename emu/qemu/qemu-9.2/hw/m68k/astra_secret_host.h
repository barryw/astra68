#ifndef ASTRA_SECRET_HOST_H
#define ASTRA_SECRET_HOST_H

#include <stddef.h>
#include <stdint.h>

#define ASTRA_SECRET_KEY_BYTES 32u
#define ASTRA_SECRET_NONCE_BYTES 24u
#define ASTRA_SECRET_OVERHEAD 44u
#define ASTRA_SECRET_MAX_BYTES 1024u

/* Versioned authenticated envelope: "ASC1", nonce, tag, ciphertext. */
int astra_secret_seal(const uint8_t key[ASTRA_SECRET_KEY_BYTES],
    const uint8_t nonce[ASTRA_SECRET_NONCE_BYTES], const uint8_t *context,
    size_t context_bytes, const uint8_t *plain, size_t plain_bytes,
    uint8_t *sealed, size_t sealed_capacity);
int astra_secret_open(const uint8_t key[ASTRA_SECRET_KEY_BYTES],
    const uint8_t *context, size_t context_bytes, const uint8_t *sealed,
    size_t sealed_bytes, uint8_t *plain, size_t plain_capacity);

#endif
