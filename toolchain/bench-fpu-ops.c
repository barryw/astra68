/*
 * Floating-point kernels for comparing the MC68040 FPU with Astra's
 * soft-float helpers under QEMU (docs/USERSPACE_FPU.md, phase 0).
 *
 * A freestanding static m68k Linux program, so qemu-m68k user mode can run
 * it on the DE25 without an Astra image: the same source is built hard
 * float and soft float, and both print a checksum of every result so a
 * difference in arithmetic shows as a difference in output.
 *
 * usage: bench-fpu-ops KERNEL ROUNDS
 *   pan      _Eff_position_s16msb's shape: s16 -> float, two multiplies,
 *            back to s16
 *   muladd   float x * gain + 0.5
 *   div      float x / gain
 *   dmuladd  double x * gain + 0.5
 *   int      the pan loop in integer arithmetic: the loop's own cost
 *
 * Hard-float builds also have `flags PAIRS` and `flagtrace PAIRS`: every
 * operation emu/qemu/qemu-9.2/target-m68k-host-float.patch touches, over
 * random and edge operands in every rounding mode and precision, folded
 * into one checksum (or printed one line per operation).
 * emu/qemu/test-host-float.sh compares it with and without the patch.
 */

#define SAMPLES 4096

static short input[SAMPLES], output[SAMPLES];
static float values[SAMPLES];
static double doubles[SAMPLES];
static volatile float gain_left = 0.70710678f, gain_distance = 0.9f;
static volatile float gain = 1.0001f;
static volatile double dgain = 1.0000001;

static long syscall3(long number, long a, long b, long c)
{
    register long d0 __asm__("d0") = number;
    register long d1 __asm__("d1") = a;
    register long d2 __asm__("d2") = b;
    register long d3 __asm__("d3") = c;

    __asm__ volatile("trap #0"
                     : "+d"(d0)
                     : "d"(d1), "d"(d2), "d"(d3)
                     : "memory");
    return d0;
}

static void write_text(const char *text)
{
    long length = 0;

    while (text[length] != '\0')
        length++;
    syscall3(4, 1, (long)text, length);
}

static void write_hex(unsigned long value)
{
    char text[9];

    for (int i = 7; i >= 0; i--) {
        text[i] = "0123456789abcdef"[value & 15];
        value >>= 4;
    }
    text[8] = '\0';
    write_text(text);
}

static int same(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned long parse(const char *text)
{
    unsigned long value = 0;

    while (*text >= '0' && *text <= '9')
        value = value * 10 + (unsigned long)(*text++ - '0');
    return value;
}

static unsigned long mix(unsigned long sum, unsigned long word)
{
    return (sum ^ word) * 0x01000193ul;
}

static unsigned long bits_of(float value)
{
    union { float f; unsigned long l; } u = { .f = value };

    return u.l;
}

static void fill(void)
{
    unsigned long state = 0x2545f491ul;

    for (int i = 0; i < SAMPLES; i++) {
        state = state * 1103515245ul + 12345ul;
        input[i] = (short)(state >> 16);
        values[i] = (float)input[i] / 256.0f;
        doubles[i] = (double)input[i] / 256.0;
    }
}

#ifdef __HAVE_68881__
static unsigned long xorshift_state = 0x9e3779b9ul;

static unsigned long next_random(void)
{
    xorshift_state ^= xorshift_state << 13;
    xorshift_state ^= xorshift_state >> 17;
    xorshift_state ^= xorshift_state << 5;
    return xorshift_state;
}

/*
 * A single's bits, weighted toward the edges a fast path could get wrong:
 * zeros, subnormals, the ends of the normal range, infinities and NaNs.
 */
static unsigned long random_single(void)
{
    static const unsigned long exponents[] = {
        0x00, 0x01, 0x02, 0x18, 0x19, 0x1a, 0x7e, 0x7f, 0x80, 0xfd, 0xfe, 0xff,
    };
    unsigned long bits = next_random();
    unsigned long pick = next_random() & 31u;

    if (pick < 12u)
        bits = (bits & 0x807ffffful) | (exponents[pick] << 23);
    if ((next_random() & 7u) == 0)
        bits &= 0xffff0000ul;
    return bits;
}

static void random_double(unsigned long out[2])
{
    static const unsigned long exponents[] = {
        0x000, 0x001, 0x002, 0x035, 0x036, 0x037, 0x3fe, 0x3ff, 0x400, 0x7fd,
        0x7fe, 0x7ff,
    };
    unsigned long pick = next_random() & 31u;

    out[0] = next_random();
    out[1] = next_random();
    if (pick < 12u)
        out[0] = (out[0] & 0x800ffffful) | (exponents[pick] << 20);
    if ((next_random() & 7u) == 0)
        out[1] = 0;
}

/* Every observable result of one operation: the register, its single and
 * double images, and FPSR's condition codes and exception bytes. */
#define OBSERVE(op, operand)                                                 \
    __asm__ volatile("fmove.l %[zero],%%fpsr\n\t"                            \
                     "fmovem.x (%[x]),%%fp0\n\t"                             \
                     op " %[source],%%fp0\n\t"                               \
                     "fmovem.x %%fp0,(%[ext])\n\t"                           \
                     "fmove.s %%fp0,%[single]\n\t"                           \
                     "fmove.d %%fp0,%[dbl]\n\t"                              \
                     "fmove.l %%fp0,%[integer]\n\t"                          \
                     "fmove.l %%fpsr,%[status]"                              \
                     : [single] "=m"(single), [dbl] "=m"(dbl),               \
                       [integer] "=m"(integer), [status] "=d"(status)        \
                     : [zero] "d"(0ul), [x] "a"(left), [ext] "a"(ext),       \
                       [source] "m"(operand), "m"(double_operand[1])  \
                     : "memory")

static int tracing;

static void trace_line(const unsigned char *left, const unsigned long *s,
                       const unsigned long *d, const unsigned char *ext,
                       unsigned long single, const unsigned long *dbl,
                       unsigned long status)
{
    unsigned long word;

    for (int i = 0; i < 12; i += 4) {
        word = (unsigned long)left[i] << 24 | left[i + 1] << 16 |
               left[i + 2] << 8 | left[i + 3];
        write_hex(word);
    }
    write_text(" ");
    write_hex(s[0]);
    write_text(" ");
    write_hex(d[0]);
    write_hex(d[1]);
    write_text(" = ");
    for (int i = 0; i < 12; i += 4) {
        word = (unsigned long)ext[i] << 24 | ext[i + 1] << 16 |
               ext[i + 2] << 8 | ext[i + 3];
        write_hex(word);
    }
    write_text(" ");
    write_hex(single);
    write_text(" ");
    write_hex(dbl[0]);
    write_hex(dbl[1]);
    write_text(" ");
    write_hex(status);
    write_text("\n");
}

static unsigned long observe_all(unsigned long sum, const unsigned char *left,
                                 const unsigned long *single_operand,
                                 const unsigned long *double_operand)
{
    unsigned char ext[12];
    unsigned long single, dbl[2], integer, status;
#define FOLD()                                                               \
    if (tracing) trace_line(left, single_operand, double_operand, ext,     \
                            single, dbl, status ^ integer);                 \
    sum = mix(mix(mix(mix(mix(mix(sum, ext[0] << 8 | ext[1]),                \
        (unsigned long)ext[4] << 24 | ext[5] << 16 | ext[6] << 8 | ext[7]),  \
        (unsigned long)ext[8] << 24 | ext[9] << 16 | ext[10] << 8 | ext[11]),\
        single), dbl[0] ^ dbl[1]), status ^ integer * 0x9e3779b1ul)
    OBSERVE("fsadd.s", single_operand[0]); FOLD();
    OBSERVE("fssub.s", single_operand[0]); FOLD();
    OBSERVE("fsmul.s", single_operand[0]); FOLD();
    OBSERVE("fsdiv.s", single_operand[0]); FOLD();
    OBSERVE("fdadd.d", double_operand[0]); FOLD();
    OBSERVE("fdsub.d", double_operand[0]); FOLD();
    OBSERVE("fdmul.d", double_operand[0]); FOLD();
    OBSERVE("fddiv.d", double_operand[0]); FOLD();
    OBSERVE("fsmove.d", double_operand[0]); FOLD();
    OBSERVE("fdmove.s", single_operand[0]); FOLD();
    OBSERVE("fmul.s", single_operand[0]); FOLD();
    OBSERVE("fadd.l", single_operand[0]); FOLD();
    OBSERVE("fmove.w", single_operand[0]); FOLD();
#undef FOLD
    return sum;
}

/*
 * The fast path's contract: the same results and the same FPSR as the
 * floatx80 path, for every operation it touches, in every rounding mode
 * and precision, with operands that are single, double, and neither.
 */
static unsigned long check_flags(unsigned long pairs)
{
    static const unsigned long modes[] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x80};
    unsigned long sum = 0x811c9dc5ul;

    for (unsigned m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        xorshift_state = 0x9e3779b9ul;
        for (unsigned long i = 0; i < pairs; i++) {
            unsigned long a = random_single(), b = random_single();
            unsigned long da[2], db[2];
            unsigned char left[12];

            random_double(da);
            random_double(db);
            /* Left operand: a single, a double, or an extended product. */
            switch (next_random() % 3u) {
            case 0:
                __asm__ volatile("fmove.s %1,%%fp0\n\t"
                                 "fmovem.x %%fp0,(%0)"
                                 : : "a"(left), "m"(a) : "memory");
                break;
            case 1:
                __asm__ volatile("fmove.d %1,%%fp0\n\t"
                                 "fmovem.x %%fp0,(%0)"
                                 : : "a"(left), "m"(da) : "memory");
                break;
            default:
                __asm__ volatile("fmove.l %2,%%fpcr\n\t"
                                 "fmove.d %1,%%fp0\n\t"
                                 "fmul.d %1,%%fp0\n\t"
                                 "fmovem.x %%fp0,(%0)"
                                 : : "a"(left), "m"(da), "d"(0ul)
                                 : "memory");
                break;
            }
            __asm__ volatile("fmove.l %0,%%fpcr" : : "d"(modes[m]));
            sum = observe_all(sum, left, &b, db);
        }
        __asm__ volatile("fmove.l %0,%%fpcr" : : "d"(0ul));
    }
    return sum;
}
#endif

static unsigned long run(const char *kernel, unsigned long rounds)
{
    unsigned long sum = 0x811c9dc5ul;

#ifdef __HAVE_68881__
    if (same(kernel, "flags"))
        return check_flags(rounds);
    if (same(kernel, "flagtrace")) {
        tracing = 1;
        return check_flags(rounds);
    }
#endif
    if (same(kernel, "pan")) {
        for (unsigned long r = 0; r < rounds; r++) {
            float left = gain_left, distance = gain_distance;

            for (int i = 0; i < SAMPLES; i++)
                output[i] = (short)(((float)input[i] * left) * distance);
            sum = mix(sum, (unsigned short)output[r % SAMPLES]);
        }
        for (int i = 0; i < SAMPLES; i++)
            sum = mix(sum, (unsigned short)output[i]);
    } else if (same(kernel, "int")) {
        for (unsigned long r = 0; r < rounds; r++) {
            for (int i = 0; i < SAMPLES; i++)
                output[i] = (short)((input[i] * 20853L) >> 15);
            sum = mix(sum, (unsigned short)output[r % SAMPLES]);
        }
    } else if (same(kernel, "muladd")) {
        for (unsigned long r = 0; r < rounds; r++) {
            float g = gain;

            for (int i = 0; i < SAMPLES; i++)
                values[i] = values[i] * g + 0.5f;
            sum = mix(sum, bits_of(values[r % SAMPLES]));
        }
        for (int i = 0; i < SAMPLES; i++)
            sum = mix(sum, bits_of(values[i]));
    } else if (same(kernel, "div")) {
        for (unsigned long r = 0; r < rounds; r++) {
            float g = gain;

            for (int i = 0; i < SAMPLES; i++)
                values[i] = values[i] / g;
            sum = mix(sum, bits_of(values[r % SAMPLES]));
        }
        for (int i = 0; i < SAMPLES; i++)
            sum = mix(sum, bits_of(values[i]));
    } else if (same(kernel, "dmuladd")) {
        for (unsigned long r = 0; r < rounds; r++) {
            double g = dgain;

            for (int i = 0; i < SAMPLES; i++)
                doubles[i] = doubles[i] * g + 0.5;
            sum = mix(sum, bits_of((float)doubles[r % SAMPLES]));
        }
        for (int i = 0; i < SAMPLES; i++) {
            union { double d; unsigned long l[2]; } u = { .d = doubles[i] };

            sum = mix(mix(sum, u.l[0]), u.l[1]);
        }
    } else {
        write_text("unknown kernel\n");
        syscall3(1, 2, 0, 0);
    }
    return sum;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        write_text("usage: bench-fpu-ops pan|muladd|div|dmuladd|int ROUNDS\n");
        return 2;
    }
    fill();
    write_text(argv[1]);
    write_text(" ");
    write_hex(run(argv[1], parse(argv[2])));
    write_text("\n");
    return 0;
}

__asm__(".globl _start\n"
        "_start:\n"
        "\tmove.l (%sp),%d0\n"
        "\tlea 4(%sp),%a0\n"
        "\tmove.l %a0,-(%sp)\n"
        "\tmove.l %d0,-(%sp)\n"
        "\tjsr main\n"
        "\tmove.l %d0,%d1\n"
        "\tmoveq #1,%d0\n"
        "\ttrap #0\n");
