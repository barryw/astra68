#!/bin/sh
# Check Astra's single-precision multiply and divide in a patched
# libgcc/config/m68k/fpgnulib.c bit-for-bit against the host's IEEE
# arithmetic: every exponent pair with random significands, every special
# class, denormals both ways, and halfway cases. The code under test is
# extracted from the source file itself.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 PATCHED-GCC-SOURCE/libgcc/config/m68k/fpgnulib.c" >&2
    exit 2
fi
SOURCE=$1
WORK=$(mktemp -d "${TMPDIR:-/tmp}/astra-muldiv.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
{
    # m68k's long is 32 bits; the host's int is. The 64-bit significand
    # product keeps its width through an alias made before that mapping.
    printf '%s\n' '#include <stdint.h>' '#include <stdio.h>' '#include <string.h>' \
        'typedef uint64_t astra_host_u64;' '#undef __UINT64_TYPE__' \
        '#define __UINT64_TYPE__ astra_host_u64' \
        '#define long int' '#define __builtin_clzl __builtin_clz'
    sed -n '/^#define EXCESS\t/,/^#define PACK/p' "$SOURCE"
    printf '%s\n' 'union float_long { float f; long l; };'
    sed -n '/ASTRA-MUL-DIV-SINGLE-BEGIN/,/ASTRA-MUL-DIV-SINGLE-END/p' "$SOURCE"
    cat <<'C'
#undef long
static uint32_t bits_of(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }
static float of_bits(uint32_t b) { float v; memcpy(&v, &b, 4); return v; }
static uint64_t state = 0x9e3779b97f4a7c15u;
static uint32_t next(void)
{
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return (uint32_t)(state >> 11);
}
static unsigned long failures, checked;
static void check(uint32_t a, uint32_t b)
{
    float x = of_bits(a), y = of_bits(b);
    uint32_t want_m = bits_of(x * y), got_m = bits_of(__mulsf3(x, y));
    uint32_t want_d = bits_of(x / y), got_d = bits_of(__divsf3(x, y));
    int nan_m = (want_m & 0x7fffffffu) > 0x7f800000u;
    int nan_d = (want_d & 0x7fffffffu) > 0x7f800000u;

    checked += 2;
    if (nan_m ? got_m != 0x7fffffffu : got_m != want_m) {
        if (failures++ < 10)
            printf("mul %08x * %08x: got %08x want %08x\n", a, b, got_m, want_m);
    }
    if (nan_d ? got_d != 0x7fffffffu : got_d != want_d) {
        if (failures++ < 10)
            printf("div %08x / %08x: got %08x want %08x\n", a, b, got_d, want_d);
    }
}
int main(void)
{
    static const uint32_t special[] = {
        0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff,
        0x00400000, 0x00800000, 0x00800001, 0x3f800000, 0xbf800000,
        0x3f800001, 0x3fffffff, 0x40000000, 0x7f7fffff, 0xff7fffff,
        0x7f800000, 0xff800000, 0x7fc00000, 0x7f800001, 0xffffffff,
        0x34000000, 0x33800000, 0x3effffff, 0x4b000000, 0x00000003,
    };
    unsigned n = sizeof(special) / sizeof(special[0]);

    for (unsigned i = 0; i < n; ++i)
        for (unsigned j = 0; j < n; ++j)
            check(special[i], special[j]);
    for (uint32_t ea = 0; ea < 256; ++ea)
        for (uint32_t eb = 0; eb < 256; ++eb)
            for (int k = 0; k < 24; ++k) {
                uint32_t a = (next() & 0x807fffffu) | ea << 23;
                uint32_t b = (next() & 0x807fffffu) | eb << 23;
                check(a, b);
            }
    /* Halfway products: short significands whose exact product sits on a
       rounding boundary, and quotients of exact multiples. */
    for (int k = 0; k < 2000000; ++k) {
        uint32_t a = next(), b = next() & ~0xfffu;
        check(a, b);
        check(a | 0x3f000000u, (b & 0x007ff000u) | 0x3f800000u);
    }
    printf("ASTRA FPGNULIB MULDIV %s checked=%lu failures=%lu\n",
           failures ? "FAIL" : "PASS", checked, failures);
    return failures != 0;
}
C
} >"$WORK/test.c"
${CC:-cc} -std=gnu11 -O2 -fno-fast-math -ffp-contract=off -o "$WORK/test" "$WORK/test.c"
"$WORK/test"
