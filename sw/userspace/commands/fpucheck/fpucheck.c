/*
 * `fpucheck` -- does a thread's FPU state belong to that thread?
 *
 * The integration check for the kernel's FPU context (docs/USERSPACE_FPU.md,
 * section 5.2), driven by emu/qemu/test-fpu.py. It touches the FPU only
 * through the inline assembly below, so it builds and means the same thing
 * under the soft-float ABI: nothing else in the process -- libc, the
 * runtime, this file's C -- executes an FPU instruction, and whatever the
 * registers hold between two of these blocks is what the kernel left there.
 *
 * Each subtest prints `FPU <NAME> PASS` and exits 0, or prints
 * `FPU <NAME> FAIL: ...` naming the first wrong register and exits 1.
 *
 *   self        load a pattern and read it straight back; checks the check
 *   threads     two threads, two patterns, across many switches
 *   processes   two processes, two patterns, across many switches
 *   fork        the child inherits the pattern; its changes stay its own
 *   signals     a handler starts with FPCR 0; its registers do not leak
 *   dirty       leave a pattern in the FPU and exit
 *   fresh       a new process and a new thread see zeros, never a pattern
 *   contract    run fsin, which the MC68040 does not implement
 *   privilege   run fsave, which user mode may not; must not return
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/program.h>

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

ASTRA_PROGRAM("fpucheck", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define FPU_REGISTERS 8u
#define SWITCH_ROUNDS 2000u
#define SPIN_PER_ROUND 200u

/* FMOVEM.X's memory image: sign and exponent, a zero word, the mantissa. */
typedef struct {
    uint8_t data[FPU_REGISTERS][12];
    uint32_t fpcr;
    uint32_t fpsr;
} FpuState;

static void
fpu_load(const FpuState *state)
{
    __asm__ volatile("fmovem.x (%0),%%fp0-%%fp7\n\t"
                     "fmove.l %1,%%fpcr\n\t"
                     "fmove.l %2,%%fpsr"
                     :
                     : "a"(state->data), "d"(state->fpcr), "d"(state->fpsr)
                     : "memory");
}

static void
fpu_store(FpuState *state)
{
    uint32_t fpcr;
    uint32_t fpsr;

    __asm__ volatile("fmovem.x %%fp0-%%fp7,(%2)\n\t"
                     "fmove.l %%fpcr,%0\n\t"
                     "fmove.l %%fpsr,%1"
                     : "=d"(fpcr), "=d"(fpsr)
                     : "a"(state->data)
                     : "memory");
    state->fpcr = fpcr;
    state->fpsr = fpsr;
}

/*
 * A distinct, normalised extended value per register and seed, a rounding
 * mode and precision (never the reserved precision 3, and never an exception
 * enable), and FPSR condition codes.
 */
static void
pattern(FpuState *state, uint32_t seed)
{
    memset(state, 0, sizeof(*state));
    for (uint32_t index = 0; index < FPU_REGISTERS; ++index) {
        uint32_t exponent = 0x3fffu + seed * 16u + index;
        uint32_t high = 0x80000000u | (seed << 20) | (index << 8) | 0x5au;
        uint32_t low = 0x12345678u ^ (seed * 0x01010101u) ^ index;
        uint8_t *bytes = state->data[index];

        bytes[0] = (uint8_t)(exponent >> 8);
        bytes[1] = (uint8_t)exponent;
        for (int shift = 0; shift < 4; ++shift) {
            bytes[4 + shift] = (uint8_t)(high >> (24 - 8 * shift));
            bytes[8 + shift] = (uint8_t)(low >> (24 - 8 * shift));
        }
    }
    state->fpcr = ((seed % 3u) << 6) | (((seed / 3u) % 4u) << 4);
    state->fpsr = (seed & 15u) << 24;
}

/* Returns 0 or writes the first difference into `why`. */
static int
compare(const FpuState *got, const FpuState *want, char *why, size_t size)
{
    for (uint32_t index = 0; index < FPU_REGISTERS; ++index) {
        const uint8_t *a = got->data[index];
        const uint8_t *b = want->data[index];

        /* Bytes 2-3 are the format's unused word; FMOVEM need not keep it. */
        if (memcmp(a, b, 2) != 0 || memcmp(a + 4, b + 4, 8) != 0) {
            snprintf(why, size,
                     "fp%u %02x%02x %02x%02x%02x%02x%02x%02x%02x%02x "
                     "want %02x%02x %02x%02x%02x%02x%02x%02x%02x%02x",
                     (unsigned)index, a[0], a[1], a[4], a[5], a[6], a[7],
                     a[8], a[9], a[10], a[11], b[0], b[1], b[4], b[5], b[6],
                     b[7], b[8], b[9], b[10], b[11]);
            return 1;
        }
    }
    if (got->fpcr != want->fpcr) {
        snprintf(why, size, "fpcr %08lx want %08lx",
                 (unsigned long)got->fpcr, (unsigned long)want->fpcr);
        return 1;
    }
    if (got->fpsr != want->fpsr) {
        snprintf(why, size, "fpsr %08lx want %08lx",
                 (unsigned long)got->fpsr, (unsigned long)want->fpsr);
        return 1;
    }
    return 0;
}

static int
report(const char *name, int failed, const char *why)
{
    if (failed)
        printf("FPU %s FAIL: %s\n", name, why);
    else
        printf("FPU %s PASS\n", name);
    fflush(stdout);
    return failed ? 1 : 0;
}

typedef struct {
    uint32_t seed;
    int failed;
    char why[96];
} Worker;

/*
 * Hold a pattern across SWITCH_ROUNDS yields, spinning between them so the
 * timer takes some of the switches too, and check it every round.
 */
static void
hold_pattern(Worker *worker)
{
    FpuState want;
    FpuState got;

    pattern(&want, worker->seed);
    fpu_load(&want);
    for (uint32_t round = 0; round < SWITCH_ROUNDS; ++round) {
        for (volatile uint32_t spin = 0; spin < SPIN_PER_ROUND; ++spin)
            ;
        fpu_store(&got);
        if (compare(&got, &want, worker->why, sizeof(worker->why))) {
            worker->failed = 1;
            return;
        }
        sched_yield();
    }
}

static void *
hold_pattern_thread(void *argument)
{
    hold_pattern(argument);
    return NULL;
}

static int
check_self(void)
{
    FpuState want;
    FpuState got;
    char why[96] = "";

    pattern(&want, 1u);
    fpu_load(&want);
    fpu_store(&got);
    return report("SELF", compare(&got, &want, why, sizeof(why)), why);
}

static int
check_threads(void)
{
    Worker workers[2] = {{.seed = 1u}, {.seed = 2u}};
    pthread_t threads[2];

    for (int index = 0; index < 2; ++index)
        if (pthread_create(&threads[index], NULL, hold_pattern_thread,
                           &workers[index]) != 0)
            return report("THREADS", 1, "pthread_create failed");
    for (int index = 0; index < 2; ++index)
        pthread_join(threads[index], NULL);
    for (int index = 0; index < 2; ++index)
        if (workers[index].failed)
            return report("THREADS", 1, workers[index].why);
    return report("THREADS", 0, "");
}

static int
wait_child(pid_t child)
{
    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int
check_processes(void)
{
    Worker worker = {.seed = 4u};
    pid_t child = fork();
    int child_status;

    if (child < 0)
        return report("PROCESSES", 1, "fork failed");
    if (child == 0) {
        Worker mine = {.seed = 3u};

        hold_pattern(&mine);
        if (mine.failed)
            printf("FPU PROCESSES child: %s\n", mine.why);
        fflush(stdout);
        _exit(mine.failed);
    }
    hold_pattern(&worker);
    child_status = wait_child(child);
    if (worker.failed)
        return report("PROCESSES", 1, worker.why);
    if (child_status != 0)
        return report("PROCESSES", 1, "child's pattern changed");
    return report("PROCESSES", 0, "");
}

static int
check_fork(void)
{
    FpuState want;
    FpuState got;
    char why[96] = "";
    pid_t child;
    int child_status;

    pattern(&want, 1u);
    fpu_load(&want);
    child = fork();
    if (child < 0)
        return report("FORK", 1, "fork failed");
    if (child == 0) {
        Worker mine = {.seed = 2u};

        fpu_store(&got);
        if (compare(&got, &want, why, sizeof(why))) {
            printf("FPU FORK child did not inherit: %s\n", why);
            fflush(stdout);
            _exit(2);
        }
        hold_pattern(&mine);
        _exit(mine.failed);
    }
    child_status = wait_child(child);
    fpu_store(&got);
    if (compare(&got, &want, why, sizeof(why)))
        return report("FORK", 1, why);
    if (child_status != 0)
        return report("FORK", 1, "child failed");
    return report("FORK", 0, "");
}

static FpuState handler_entry;
static volatile sig_atomic_t handler_ran;

static void
overwrite_handler(int signal_number)
{
    FpuState other;

    (void)signal_number;
    fpu_store(&handler_entry);
    pattern(&other, 2u);
    fpu_load(&other);
    handler_ran = 1;
}

static int
check_signals(void)
{
    struct sigaction action;
    FpuState want;
    FpuState got;
    char why[96] = "";

    memset(&action, 0, sizeof(action));
    action.sa_handler = overwrite_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, NULL) != 0)
        return report("SIGNALS", 1, "sigaction failed");
    pattern(&want, 1u);
    fpu_load(&want);
    if (raise(SIGUSR1) != 0 || !handler_ran)
        return report("SIGNALS", 1, "handler did not run");
    fpu_store(&got);
    if (compare(&got, &want, why, sizeof(why)))
        return report("SIGNALS", 1, why);
    if (handler_entry.fpcr != 0u) {
        snprintf(why, sizeof(why), "handler entered with fpcr %08lx",
                 (unsigned long)handler_entry.fpcr);
        return report("SIGNALS", 1, why);
    }
    return report("SIGNALS", 0, "");
}

static int
check_dirty(void)
{
    FpuState state;

    pattern(&state, 3u);
    fpu_load(&state);
    printf("FPU DIRTY DONE\n");
    fflush(stdout);
    return 0;
}

static Worker fresh_thread;

static void *
read_fresh(void *argument)
{
    FpuState zero;
    FpuState got;

    (void)argument;
    memset(&zero, 0, sizeof(zero));
    fpu_store(&got);
    fresh_thread.failed = compare(&got, &zero, fresh_thread.why,
                                  sizeof(fresh_thread.why));
    return NULL;
}

/*
 * Run right after `fpucheck dirty`: this process is new, so it must start
 * with zeros. Then a child leaves a pattern, and a thread created after it
 * must also start with zeros -- and with its creator's FPCR, which is zero
 * here.
 */
static int
check_fresh(void)
{
    FpuState zero;
    FpuState got;
    char why[96] = "";
    pthread_t thread;
    pid_t child;

    memset(&zero, 0, sizeof(zero));
    fpu_store(&got);
    if (compare(&got, &zero, why, sizeof(why)))
        return report("FRESH", 1, why);
    child = fork();
    if (child < 0)
        return report("FRESH", 1, "fork failed");
    if (child == 0) {
        pattern(&got, 4u);
        fpu_load(&got);
        _exit(0);
    }
    if (wait_child(child) != 0)
        return report("FRESH", 1, "child failed");
    if (pthread_create(&thread, NULL, read_fresh, NULL) != 0)
        return report("FRESH", 1, "pthread_create failed");
    pthread_join(thread, NULL);
    if (fresh_thread.failed) {
        snprintf(why, sizeof(why), "new thread: %s", fresh_thread.why);
        return report("FRESH", 1, why);
    }
    return report("FRESH", 0, "");
}

/*
 * The MC68040 has no FSIN; silicon takes the F-line vector (11). Until
 * Astra's QEMU does the same, the instruction runs and this says so.
 */
static int
check_contract(void)
{
    __asm__ volatile("fmove.l #0,%%fp0\n\t"
                     "fsin.x %%fp0"
                     :
                     :
                     : "memory");
    printf("FPU CONTRACT RAN: fsin executed in user mode\n");
    fflush(stdout);
    return 0;
}

/* FSAVE is privileged: vector 8, and this process does not come back. */
static int
check_privilege(void)
{
    static uint8_t frame[256];

    printf("FPU PRIVILEGE TRYING\n");
    fflush(stdout);
    __asm__ volatile("fsave (%0)" : : "a"(frame + sizeof(frame) / 2)
                     : "memory");
    printf("FPU PRIVILEGE FAIL: fsave returned in user mode\n");
    fflush(stdout);
    return 1;
}

int
main(int argc, char **argv)
{
    static const struct {
        const char *name;
        int (*run)(void);
    } subtests[] = {
        {"self", check_self},
        {"threads", check_threads},
        {"processes", check_processes},
        {"fork", check_fork},
        {"signals", check_signals},
        {"dirty", check_dirty},
        {"fresh", check_fresh},
        {"contract", check_contract},
        {"privilege", check_privilege},
    };

    if (argc == 2)
        for (size_t index = 0;
             index < sizeof(subtests) / sizeof(subtests[0]); ++index)
            if (strcmp(argv[1], subtests[index].name) == 0)
                return subtests[index].run();
    fputs("usage: fpucheck self|threads|processes|fork|signals|dirty|fresh|"
          "contract|privilege\n", stderr);
    return 2;
}
