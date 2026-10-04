#include <astra/keymap.h>

typedef struct KeymapEntry {
    uint8_t plain;
    uint8_t shifted;
} KeymapEntry;

static const KeymapEntry punctuation[] = {
    {' ', ' '}, {'-', '_'}, {'=', '+'}, {'[', '{'}, {']', '}'},
    {'\\', '|'}, {'\\', '|'}, {';', ':'}, {'\'', '"'}, {'`', '~'},
    {',', '<'}, {'.', '>'}, {'/', '?'}
};

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
    else if (usage == ASTRA_KEYMAP_USAGE_CAPS_LOCK)
        return pressed ? (modifiers ^ ASTRA_KEYMAP_MOD_CAPS) : modifiers;
    else
        return modifiers;

    return pressed ? (modifiers | bit) : (modifiers & ~bit);
}

uint32_t astra_keymap_translate(uint32_t usage, uint32_t modifiers)
{
    int shift = (modifiers & ASTRA_KEYMAP_MOD_SHIFT) != 0u;
    int caps = (modifiers & ASTRA_KEYMAP_MOD_CAPS) != 0u;

    if (usage >= 0x04u && usage <= 0x1du) {
        int upper = shift ^ caps;
        uint32_t character =
            (uint32_t)((upper ? 'A' : 'a') + (int)(usage - 0x04u));

        return (modifiers & ASTRA_KEYMAP_MOD_CONTROL) != 0u ?
            usage - 0x04u + 1u : character;
    }
    if (usage >= 0x1eu && usage <= 0x27u) {
        uint32_t index = usage - 0x1eu;

        if (shift) return (uint32_t)digit_shifted[index];
        return index == 9u ? (uint32_t)'0' :
                             (uint32_t)('1' + (int)index);
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
    case 0x4cu: return ASTRA_KEYMAP_DELETE;
    case 0x4du: return ASTRA_KEYMAP_END;
    case 0x4fu: return ASTRA_KEYMAP_RIGHT;
    case 0x50u: return ASTRA_KEYMAP_LEFT;
    case 0x51u: return ASTRA_KEYMAP_DOWN;
    case 0x52u: return ASTRA_KEYMAP_UP;
    case 0x58u: return ASTRA_KEYMAP_ENTER;
    default: return ASTRA_KEYMAP_NONE;
    }
}
