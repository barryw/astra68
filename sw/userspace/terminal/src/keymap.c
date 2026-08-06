#include <astra/keymap.h>

/*
 * The table is indexed by HID usage and holds the unshifted and shifted
 * character for the keys that produce text. Everything outside the table is
 * either an editing key, a modifier, or nothing at all.
 *
 * HID usage 0x04..0x1d are 'a'..'z' and 0x1e..0x27 are '1'..'0', which is why
 * those two ranges are computed rather than listed: a typo in fifty-odd
 * entries is easy to write and hard to see.
 */

typedef struct KeymapEntry {
    uint8_t plain;
    uint8_t shifted;
} KeymapEntry;

/* Usages 0x2c..0x38, the punctuation block, in order. */
static const KeymapEntry punctuation[] = {
    {' ', ' '},   /* 0x2c space */
    {'-', '_'},   /* 0x2d */
    {'=', '+'},   /* 0x2e */
    {'[', '{'},   /* 0x2f */
    {']', '}'},   /* 0x30 */
    {'\\', '|'},  /* 0x31 */
    {'\\', '|'},  /* 0x32 non-US hash, treated as backslash */
    {';', ':'},   /* 0x33 */
    {'\'', '"'},  /* 0x34 */
    {'`', '~'},   /* 0x35 */
    {',', '<'},   /* 0x36 */
    {'.', '>'},   /* 0x37 */
    {'/', '?'}    /* 0x38 */
};

/* The digit row's shifted faces, '1'..'9' then '0'. */
static const char digit_shifted[10] = {
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')'
};

int astra_keymap_is_modifier(uint32_t usage)
{
    return usage == ASTRA_KEYMAP_USAGE_CAPS_LOCK ||
           (usage >= ASTRA_KEYMAP_USAGE_LEFT_CONTROL && usage <= 0xe7u);
}

uint32_t astra_keymap_apply_modifier(uint32_t modifiers, uint32_t usage,
                                     int pressed)
{
    uint32_t bit = 0u;

    if (usage == ASTRA_KEYMAP_USAGE_LEFT_SHIFT ||
        usage == ASTRA_KEYMAP_USAGE_RIGHT_SHIFT)
        bit = ASTRA_KEYMAP_MOD_SHIFT;
    else if (usage == ASTRA_KEYMAP_USAGE_LEFT_CONTROL ||
             usage == ASTRA_KEYMAP_USAGE_RIGHT_CONTROL)
        bit = ASTRA_KEYMAP_MOD_CONTROL;
    else if (usage == ASTRA_KEYMAP_USAGE_CAPS_LOCK) {
        /* Caps lock latches on press and is ignored on release. */
        return pressed ? (modifiers ^ ASTRA_KEYMAP_MOD_CAPS) : modifiers;
    } else
        return modifiers;

    return pressed ? (modifiers | bit) : (modifiers & ~bit);
}

uint32_t astra_keymap_translate(uint32_t usage, uint32_t modifiers)
{
    int shift = (modifiers & ASTRA_KEYMAP_MOD_SHIFT) != 0u;
    int caps = (modifiers & ASTRA_KEYMAP_MOD_CAPS) != 0u;
    uint32_t character = ASTRA_KEYMAP_NONE;

    if (usage >= 0x04u && usage <= 0x1du) {
        /*
         * Caps lock and shift both reach upper case, and together they cancel
         * -- the behaviour every keyboard has and every first draft misses.
         */
        int upper = shift ^ caps;

        character = (uint32_t)((upper ? 'A' : 'a') + (int)(usage - 0x04u));
        if ((modifiers & ASTRA_KEYMAP_MOD_CONTROL) != 0u)
            character = (uint32_t)(usage - 0x04u + 1u);
        return character;
    }
    if (usage >= 0x1eu && usage <= 0x27u) {
        uint32_t index = usage - 0x1eu;

        /* Usage 0x27 is '0', which sorts after '9' rather than before '1'. */
        if (shift)
            return (uint32_t)digit_shifted[index];
        return index == 9u ? (uint32_t)'0' : (uint32_t)('1' + (int)index);
    }
    if (usage >= 0x2cu && usage <= 0x38u) {
        const KeymapEntry *entry = &punctuation[usage - 0x2cu];

        return shift ? entry->shifted : entry->plain;
    }

    switch (usage) {
    case 0x28u: return ASTRA_KEYMAP_ENTER;
    case 0x29u: return ASTRA_KEYMAP_ESCAPE;
    case 0x2au: return ASTRA_KEYMAP_BACKSPACE;
    case 0x2bu: return ASTRA_KEYMAP_TAB;
    case 0x4au: return ASTRA_KEYMAP_HOME;
    case 0x4bu: return ASTRA_KEYMAP_NONE;  /* page up */
    case 0x4cu: return ASTRA_KEYMAP_DELETE;
    case 0x4du: return ASTRA_KEYMAP_END;
    case 0x4eu: return ASTRA_KEYMAP_NONE;  /* page down */
    case 0x4fu: return ASTRA_KEYMAP_RIGHT;
    case 0x50u: return ASTRA_KEYMAP_LEFT;
    case 0x51u: return ASTRA_KEYMAP_DOWN;
    case 0x52u: return ASTRA_KEYMAP_UP;
    case 0x58u: return ASTRA_KEYMAP_ENTER;  /* keypad enter */
    default: break;
    }
    return ASTRA_KEYMAP_NONE;
}
