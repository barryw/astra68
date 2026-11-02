#include "hw/m68k/astra_secret_host.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    uint8_t key[ASTRA_SECRET_KEY_BYTES] = {1};
    uint8_t wrong[ASTRA_SECRET_KEY_BYTES] = {2};
    uint8_t nonce[ASTRA_SECRET_NONCE_BYTES] = {3};
    uint8_t sealed[ASTRA_SECRET_OVERHEAD + ASTRA_SECRET_MAX_BYTES];
    uint8_t plain[ASTRA_SECRET_MAX_BYTES];
    const uint8_t context[] = "/config/system/network\0smb.password\0";
    const uint8_t secret[] = "correct horse battery staple";
    int length = astra_secret_seal(key, nonce, context, sizeof(context),
        secret, sizeof(secret), sealed, sizeof(sealed));
    assert(length == ASTRA_SECRET_OVERHEAD + sizeof(secret));
    for (int at = 0; at <= length - (int)sizeof(secret); ++at)
        assert(memcmp(sealed + at, secret, sizeof(secret)) != 0);
    assert(astra_secret_open(key, context, sizeof(context), sealed,
        length, plain, sizeof(plain)) == sizeof(secret));
    assert(memcmp(plain, secret, sizeof(secret)) == 0);
    sealed[length - 1] ^= 1;
    assert(astra_secret_open(key, context, sizeof(context), sealed,
        length, plain, sizeof(plain)) < 0);
    sealed[length - 1] ^= 1;
    assert(astra_secret_open(wrong, context, sizeof(context), sealed,
        length, plain, sizeof(plain)) < 0);
    assert(astra_secret_open(key, (const uint8_t *)"other", 5u, sealed,
        length, plain, sizeof(plain)) < 0);
    assert(astra_secret_open(key, context, sizeof(context), sealed,
        length - 1, plain, sizeof(plain)) < 0);
    assert(astra_secret_open(key, context, sizeof(context), sealed,
        length, plain, sizeof(secret) - 1u) < 0);
    return 0;
}
