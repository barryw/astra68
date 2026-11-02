#include "astra_secret_host.h"
#include "monocypher.h"

#include <string.h>

int astra_secret_seal(const uint8_t key[ASTRA_SECRET_KEY_BYTES],
    const uint8_t nonce[ASTRA_SECRET_NONCE_BYTES], const uint8_t *context,
    size_t context_bytes, const uint8_t *plain, size_t plain_bytes,
    uint8_t *sealed, size_t sealed_capacity)
{
    if (key == NULL || nonce == NULL || context == NULL || context_bytes == 0 ||
        plain == NULL || plain_bytes == 0 || plain_bytes > ASTRA_SECRET_MAX_BYTES ||
        sealed == NULL || sealed_capacity < ASTRA_SECRET_OVERHEAD + plain_bytes)
        return -1;
    memcpy(sealed, "ASC1", 4);
    memcpy(sealed + 4, nonce, ASTRA_SECRET_NONCE_BYTES);
    crypto_aead_lock(sealed + ASTRA_SECRET_OVERHEAD, sealed + 28, key,
        nonce, context, context_bytes, plain, plain_bytes);
    return (int)(ASTRA_SECRET_OVERHEAD + plain_bytes);
}

int astra_secret_open(const uint8_t key[ASTRA_SECRET_KEY_BYTES],
    const uint8_t *context, size_t context_bytes, const uint8_t *sealed,
    size_t sealed_bytes, uint8_t *plain, size_t plain_capacity)
{
    if (key == NULL || context == NULL || context_bytes == 0 ||
        sealed == NULL || sealed_bytes <= ASTRA_SECRET_OVERHEAD ||
        sealed_bytes > ASTRA_SECRET_OVERHEAD + ASTRA_SECRET_MAX_BYTES ||
        plain == NULL || plain_capacity < sealed_bytes - ASTRA_SECRET_OVERHEAD ||
        memcmp(sealed, "ASC1", 4) != 0)
        return -1;
    size_t length = sealed_bytes - ASTRA_SECRET_OVERHEAD;
    if (crypto_aead_unlock(plain, sealed + 28, key, sealed + 4,
            context, context_bytes, sealed + ASTRA_SECRET_OVERHEAD,
            length) != 0) {
        crypto_wipe(plain, length);
        return -1;
    }
    return (int)length;
}
