// astra/string.h: bounds, truncation on scalar boundaries, formatting.

#include <astra/string.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void builder(void)
{
    char storage[12];
    AstraString text;

    memset(storage, 'x', sizeof(storage));
    astra_string_init(&text, storage, sizeof(storage));
    assert(text.length == 0u && storage[0] == '\0' && !text.truncated);
    assert(astra_string_append(&text, "ab"));
    assert(astra_string_append_char(&text, ' '));
    assert(astra_string_append_u64(&text, 0u));
    assert(astra_string_append_char(&text, ' '));
    assert(astra_string_append_hex(&text, 0x1au, 2u));
    assert(strcmp(storage, "ab 0 1a") == 0 && text.length == 7u);
    /* Four bytes of room are left: "wxyz" fits, one more does not. */
    assert(astra_string_append(&text, "wxyz"));
    assert(text.length == 11u && !text.truncated);
    assert(!astra_string_append_char(&text, '!'));
    assert(text.truncated && strcmp(storage, "ab 0 1awxyz") == 0);
    /* Once truncated, nothing more is appended, even what would fit. */
    text.length = 2u;
    storage[2] = '\0';
    assert(!astra_string_append(&text, "c"));
    assert(strcmp(storage, "ab") == 0);
    assert(!astra_string_append(&text, ""));

    astra_string_init(&text, storage, sizeof(storage));
    assert(astra_string_append_u64(&text, UINT64_MAX) == 0);
    assert(text.truncated && text.length == 11u &&
           strcmp(storage, "18446744073") == 0);

    astra_string_init(&text, storage, sizeof(storage));
    assert(astra_string_append_i64(&text, -42) &&
           astra_string_append_char(&text, ' ') &&
           astra_string_append_i64(&text, 7));
    assert(strcmp(storage, "-42 7") == 0);
    {
        char wide[24];
        AstraString big;

        astra_string_init(&big, wide, sizeof(wide));
        assert(astra_string_append_i64(&big, INT64_MIN) &&
               strcmp(wide, "-9223372036854775808") == 0);
    }

    astra_string_init(&text, storage, sizeof(storage));
    assert(astra_string_append_hex(&text, 0x0123456789abcdefull, 8u));
    assert(strcmp(storage, "89abcdef") == 0);
    assert(!astra_string_append_hex(&text, 1u, 0u));
    assert(!astra_string_append_hex(&text, 1u, 17u));
    assert(strcmp(storage, "89abcdef") == 0 && !text.truncated);

    /* Degenerate builders refuse everything and never write. */
    astra_string_init(&text, NULL, 8u);
    assert(!astra_string_append(&text, "a") && text.truncated);
    astra_string_init(&text, storage, 0u);
    assert(!astra_string_append(&text, "a"));
    assert(!astra_string_append(NULL, "a"));
    astra_string_init(&text, storage, sizeof(storage));
    assert(!astra_string_append(&text, NULL));
    assert(astra_string_append_bytes(&text, NULL, 0u));
    assert(!astra_string_append_bytes(&text, NULL, 1u));
}

static void utf8(void)
{
    /* "é" is two bytes, "€" three: cuts never split them. */
    static const char text[] = "a\xc3\xa9\xe2\x82\xac";
    char storage[5];
    AstraString builder_text;

    assert(astra_string_utf8_prefix(text, 6u, 6u) == 6u);
    assert(astra_string_utf8_prefix(text, 6u, 5u) == 3u);
    assert(astra_string_utf8_prefix(text, 6u, 4u) == 3u);
    assert(astra_string_utf8_prefix(text, 6u, 3u) == 3u);
    assert(astra_string_utf8_prefix(text, 6u, 2u) == 1u);
    assert(astra_string_utf8_prefix(text, 6u, 0u) == 0u);
    assert(astra_string_utf8_prefix(NULL, 6u, 3u) == 0u);
    astra_string_init(&builder_text, storage, sizeof(storage));
    assert(!astra_string_append(&builder_text, text));
    assert(builder_text.length == 3u && strcmp(storage, "a\xc3\xa9") == 0);
}

static void helpers(void)
{
    char out[4];

    assert(astra_string_copy(out, sizeof(out), "abc") &&
           strcmp(out, "abc") == 0);
    assert(!astra_string_copy(out, sizeof(out), "abcd") && out[0] == '\0');
    assert(!astra_string_copy(out, 0u, "a"));
    assert(!astra_string_copy(NULL, 4u, "a"));
    assert(!astra_string_copy(out, sizeof(out), NULL) && out[0] == '\0');

    out[0] = '\0';
    assert(astra_string_concat(out, sizeof(out), "ab") &&
           astra_string_concat(out, sizeof(out), "c") &&
           strcmp(out, "abc") == 0);
    assert(!astra_string_concat(out, sizeof(out), "d") &&
           strcmp(out, "abc") == 0);
    assert(astra_string_concat(out, sizeof(out), ""));
    {
        char unterminated[3] = { 'x', 'y', 'z' };

        assert(!astra_string_concat(unterminated, sizeof(unterminated), ""));
    }
    assert(!astra_string_concat(NULL, 4u, "a") &&
           !astra_string_concat(out, 4u, NULL));
    assert(astra_string_has_prefix("/apps/X.app", "/apps/"));
    assert(astra_string_has_prefix("abc", ""));
    assert(!astra_string_has_prefix("/ap", "/apps/"));
    assert(astra_string_has_suffix("X.app", ".app"));
    assert(astra_string_has_suffix("abc", ""));
    assert(!astra_string_has_suffix("app", ".app"));
    assert(!astra_string_has_prefix(NULL, "a") &&
           !astra_string_has_suffix("a", NULL));

    assert(astra_string_compare_ascii_nocase("Terminal", "terminal") == 0);
    assert(astra_string_compare_ascii_nocase("alpha", "Beta") < 0);
    assert(astra_string_compare_ascii_nocase("Zeta", "alpha") > 0);
    assert(astra_string_compare_ascii_nocase("ab", "abc") < 0);
    assert(astra_string_compare_ascii_nocase("\xc3\xa9", "e") > 0);
}

int main(void)
{
    builder();
    utf8();
    helpers();
    puts("string tests passed");
    return 0;
}
