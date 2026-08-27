#include <assert.h>
#include <stdio.h>

#include <astra/keymap.h>

static void test_letters_and_case(void)
{
    /* 0x04 is 'a'. */
    assert(astra_keymap_translate(0x04u, 0u) == 'a');
    assert(astra_keymap_translate(0x1du, 0u) == 'z');
    assert(astra_keymap_translate(0x04u, ASTRA_KEYMAP_MOD_SHIFT) == 'A');
    assert(astra_keymap_translate(0x04u, ASTRA_KEYMAP_MOD_CAPS) == 'A');

    /* Shift and caps together cancel, as on any real keyboard. */
    assert(astra_keymap_translate(
               0x04u, ASTRA_KEYMAP_MOD_SHIFT | ASTRA_KEYMAP_MOD_CAPS) == 'a');
}

static void test_control_codes(void)
{
    /* ^A is 1, ^C is 3. */
    assert(astra_keymap_translate(0x04u, ASTRA_KEYMAP_MOD_CONTROL) == 1u);
    assert(astra_keymap_translate(0x06u, ASTRA_KEYMAP_MOD_CONTROL) == 3u);
}

static void test_digits_and_symbols(void)
{
    /* 0x1e is '1' and 0x27 is '0', which is the ordering that trips people. */
    assert(astra_keymap_translate(0x1eu, 0u) == '1');
    assert(astra_keymap_translate(0x26u, 0u) == '9');
    assert(astra_keymap_translate(0x27u, 0u) == '0');
    assert(astra_keymap_translate(0x1eu, ASTRA_KEYMAP_MOD_SHIFT) == '!');
    assert(astra_keymap_translate(0x27u, ASTRA_KEYMAP_MOD_SHIFT) == ')');

    assert(astra_keymap_translate(0x2cu, 0u) == ' ');
    assert(astra_keymap_translate(0x2du, 0u) == '-');
    assert(astra_keymap_translate(0x2du, ASTRA_KEYMAP_MOD_SHIFT) == '_');
    assert(astra_keymap_translate(0x38u, 0u) == '/');
    assert(astra_keymap_translate(0x37u, 0u) == '.');
    /* Caps lock must not shift punctuation, only letters. */
    assert(astra_keymap_translate(0x2du, ASTRA_KEYMAP_MOD_CAPS) == '-');
}

static void test_editing_keys(void)
{
    assert(astra_keymap_translate(0x28u, 0u) == ASTRA_KEYMAP_ENTER);
    assert(astra_keymap_translate(0x29u, 0u) == ASTRA_KEYMAP_ESCAPE);
    assert(astra_keymap_translate(0x2au, 0u) == ASTRA_KEYMAP_BACKSPACE);
    assert(astra_keymap_translate(0x2bu, 0u) == ASTRA_KEYMAP_TAB);
    assert(astra_keymap_translate(0x4fu, 0u) == ASTRA_KEYMAP_RIGHT);
    assert(astra_keymap_translate(0x50u, 0u) == ASTRA_KEYMAP_LEFT);
    assert(astra_keymap_translate(0x52u, 0u) == ASTRA_KEYMAP_UP);
    assert(astra_keymap_translate(0x51u, 0u) == ASTRA_KEYMAP_DOWN);
    assert(astra_keymap_translate(0x4au, 0u) == ASTRA_KEYMAP_HOME);
    assert(astra_keymap_translate(0x4du, 0u) == ASTRA_KEYMAP_END);
    assert(astra_keymap_translate(0x4cu, 0u) == ASTRA_KEYMAP_DELETE);
}

static void test_unmapped_usages_produce_nothing(void)
{
    assert(astra_keymap_translate(0x00u, 0u) == ASTRA_KEYMAP_NONE);
    assert(astra_keymap_translate(0x65u, 0u) == ASTRA_KEYMAP_NONE);
    assert(astra_keymap_translate(0xffffu, 0u) == ASTRA_KEYMAP_NONE);
    /* Modifier keys themselves type nothing. */
    assert(astra_keymap_translate(ASTRA_KEYMAP_USAGE_LEFT_SHIFT, 0u) ==
           ASTRA_KEYMAP_NONE);
}

static void test_modifier_tracking(void)
{
    uint32_t modifiers = 0u;

    assert(astra_keymap_is_modifier(ASTRA_KEYMAP_USAGE_LEFT_SHIFT));
    assert(astra_keymap_is_modifier(ASTRA_KEYMAP_USAGE_CAPS_LOCK));
    assert(!astra_keymap_is_modifier(0x04u));

    modifiers = astra_keymap_apply_modifier(
        modifiers, ASTRA_KEYMAP_USAGE_LEFT_SHIFT, 1);
    assert((modifiers & ASTRA_KEYMAP_MOD_SHIFT) != 0u);
    assert(astra_keymap_translate(0x04u, modifiers) == 'A');

    modifiers = astra_keymap_apply_modifier(
        modifiers, ASTRA_KEYMAP_USAGE_LEFT_SHIFT, 0);
    assert((modifiers & ASTRA_KEYMAP_MOD_SHIFT) == 0u);

    /* Caps latches on press and ignores the release. */
    modifiers = astra_keymap_apply_modifier(
        modifiers, ASTRA_KEYMAP_USAGE_CAPS_LOCK, 1);
    assert((modifiers & ASTRA_KEYMAP_MOD_CAPS) != 0u);
    modifiers = astra_keymap_apply_modifier(
        modifiers, ASTRA_KEYMAP_USAGE_CAPS_LOCK, 0);
    assert((modifiers & ASTRA_KEYMAP_MOD_CAPS) != 0u);
    modifiers = astra_keymap_apply_modifier(
        modifiers, ASTRA_KEYMAP_USAGE_CAPS_LOCK, 1);
    assert((modifiers & ASTRA_KEYMAP_MOD_CAPS) == 0u);

    /* A key that is not a modifier leaves the mask alone. */
    assert(astra_keymap_apply_modifier(modifiers, 0x04u, 1) == modifiers);
}

int main(void)
{
    test_letters_and_case();
    test_control_codes();
    test_digits_and_symbols();
    test_editing_keys();
    test_unmapped_usages_produce_nothing();
    test_modifier_tracking();
    puts("astra keymap: PASS");
    return 0;
}
