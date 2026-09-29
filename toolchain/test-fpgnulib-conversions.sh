#!/bin/sh
# Exhaustively check Astra's single-precision integer conversions in a patched
# libgcc/config/m68k/fpgnulib.c against the host's IEEE conversions: every
# 32-bit integer both ways into float, and every float bit pattern into int.
# The code under test is extracted from the source file itself, so the check
# cannot drift from what the toolchain builds.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 PATCHED-GCC-SOURCE/libgcc/config/m68k/fpgnulib.c" >&2
    exit 2
fi
SOURCE=$1
WORK=$(mktemp -d "${TMPDIR:-/tmp}/astra-fpgnulib.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

{
    # m68k's long is 32 bits; the host's int is.
    printf '%s\n' '#include <stdint.h>' '#include <stdio.h>' '#include <string.h>' \
        '#define long int' '#define __builtin_clzl __builtin_clz'
    sed -n '/^#define EXCESS\t/,/^#define PACK/p' "$SOURCE"
    printf '%s\n' 'union float_long { float f; long l; };'
    sed -n '/ASTRA-SINGLE-CONVERSIONS-BEGIN/,/ASTRA-SINGLE-CONVERSIONS-END/p' \
        "$SOURCE"
    sed -n '/ASTRA-FIX-SINGLE-BEGIN/,/ASTRA-FIX-SINGLE-END/p' "$SOURCE"
    cat <<'EOF'
#undef long
static uint32_t bits_of(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* __fixdfsi's contract: truncate; |x| >= 2^31, Inf and NaN saturate by sign. */
static int32_t reference_fix(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    if (((bits >> 23) & 0xffu) == 0xffu ||
        value >= 2147483648.0f || value <= -2147483648.0f)
        return (bits & 0x80000000u) ? INT32_MIN : INT32_MAX;
    return (int32_t)value;
}

int main(void)
{
    uint32_t index = 0u;

    do {
        int32_t signed_value = (int32_t)index;
        float value;

        if (bits_of(__floatsisf(signed_value)) != bits_of((float)signed_value)) {
            printf("__floatsisf(%d) wrong\n", signed_value);
            return 1;
        }
        if (bits_of(__floatunsisf(index)) != bits_of((float)index)) {
            printf("__floatunsisf(%u) wrong\n", index);
            return 1;
        }
        memcpy(&value, &index, sizeof(value));
        if (__fixsfsi(value) != reference_fix(index)) {
            printf("__fixsfsi(0x%08x) = %d, want %d\n", index,
                   __fixsfsi(value), reference_fix(index));
            return 1;
        }
    } while (++index != 0u);
    puts("Astra fpgnulib single conversions: PASS");
    return 0;
}
EOF
} >"$WORK/test.c"
${CC:-cc} -std=c99 -O2 -fno-strict-aliasing -Wall -Wno-overflow \
    "$WORK/test.c" -o "$WORK/test"
"$WORK/test"
