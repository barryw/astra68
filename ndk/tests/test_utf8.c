#include <astra/utf8.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void test_validation(void)
{
    static const uint8_t valid[] = {
        'A', 0xc3u, 0xa9u, 0xe2u, 0x82u, 0xacu,
        0xf0u, 0x9fu, 0x98u, 0x80u};
    static const uint8_t nul[] = {'A', 0u, 'B'};
    static const uint8_t overlong[] = {0xc0u, 0x80u};
    static const uint8_t surrogate[] = {0xedu, 0xa0u, 0x80u};
    static const uint8_t too_high[] = {0xf4u, 0x90u, 0x80u, 0x80u};
    static const uint8_t truncated[] = {0xe2u, 0x82u};
    static const uint8_t continuation[] = {0x80u};

    assert(astra_utf8_validate(NULL, 0u, 0u));
    assert(!astra_utf8_validate(NULL, 1u, 0u));
    assert(astra_utf8_validate(valid, sizeof(valid), 0u));
    assert(!astra_utf8_validate(nul, sizeof(nul), 0u));
    assert(astra_utf8_validate(nul, sizeof(nul), ASTRA_UTF8_ALLOW_NUL));
    assert(!astra_utf8_validate(overlong, sizeof(overlong), 0u));
    assert(!astra_utf8_validate(surrogate, sizeof(surrogate), 0u));
    assert(!astra_utf8_validate(too_high, sizeof(too_high), 0u));
    assert(!astra_utf8_validate(truncated, sizeof(truncated), 0u));
    assert(!astra_utf8_validate(continuation, sizeof(continuation), 0u));
    assert(!astra_utf8_validate(valid, sizeof(valid), UINT32_C(0x80000000)));
    assert(astra_unicode_scalar_valid(0u));
    assert(astra_unicode_scalar_valid(0x10ffffu));
    assert(!astra_unicode_scalar_valid(0xd800u));
    assert(!astra_unicode_scalar_valid(0xdfffu));
    assert(!astra_unicode_scalar_valid(0x110000u));
}

static void test_scalar_navigation(void)
{
    static const uint8_t text[] = {
        'A', 0xc3u, 0xa9u, 0xf0u, 0x9fu, 0x98u, 0x80u};
    uint32_t offset = 0u;

    assert(astra_utf8_scalar_advance(text, sizeof(text), &offset));
    assert(offset == 1u);
    assert(astra_utf8_scalar_advance(text, sizeof(text), &offset));
    assert(offset == 3u);
    assert(astra_utf8_scalar_advance(text, sizeof(text), &offset));
    assert(offset == sizeof(text));
    assert(!astra_utf8_scalar_advance(text, sizeof(text), &offset));
    assert(offset == sizeof(text));
    assert(astra_utf8_scalar_retreat(text, sizeof(text), &offset));
    assert(offset == 3u);
    assert(astra_utf8_scalar_retreat(text, sizeof(text), &offset));
    assert(offset == 1u);
    assert(astra_utf8_scalar_retreat(text, sizeof(text), &offset));
    assert(offset == 0u);
    assert(!astra_utf8_scalar_retreat(text, sizeof(text), &offset));
    offset = 2u;
    assert(!astra_utf8_scalar_advance(text, sizeof(text), &offset));
    assert(offset == 2u);
    assert(!astra_utf8_scalar_retreat(text, sizeof(text), &offset));
    assert(offset == 2u);
}

int main(void)
{
    test_validation();
    test_scalar_navigation();
    return 0;
}
