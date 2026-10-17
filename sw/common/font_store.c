#include <astra/font_store.h>
#include <astra/sha256.h>

#include <stddef.h>

static const char base32[] = "abcdefghijklmnopqrstuvwxyz234567";

void astra_font_object_token(const void *bytes, uint32_t length,
                             char token[ASTRA_FONT_OBJECT_TOKEN_SIZE])
{
    AstraSha256 hash;
    uint8_t digest[32];
    uint32_t value = 0u, available = 0u, used = 0u;

    astra_sha256_init(&hash);
    astra_sha256_update(&hash, bytes, length);
    astra_sha256_final(&hash, digest);
    for (uint32_t at = 0u; at < sizeof(digest); ++at) {
        value = (value << 8) | digest[at];
        available += 8u;
        while (available >= 5u) {
            available -= 5u;
            token[used++] = base32[(value >> available) & 31u];
        }
    }
    if (available != 0u)
        token[used++] = base32[(value << (5u - available)) & 31u];
    token[used] = '\0';
}

int astra_font_object_token_valid(const char *token, uint32_t length)
{
    if (token == NULL || length != ASTRA_FONT_OBJECT_TOKEN_LENGTH)
        return 0;
    for (uint32_t at = 0u; at < length; ++at)
        if (!((token[at] >= 'a' && token[at] <= 'z') ||
              (token[at] >= '2' && token[at] <= '7')))
            return 0;
    return token[length - 1u] == 'a' || token[length - 1u] == 'q';
}
