/*
 * Half of the link contract: an image that declares its provenance links, and
 * what it declared is still there afterwards.
 *
 * The other half is link_contract_without.c, which must fail to link. Both are
 * linked by the runtime's `link-contract` target against the real
 * astra_user.ld, with --gc-sections on, because the collector is the reason the
 * section needs KEEP and a test that left it off would prove nothing.
 *
 * The version is 1.2.3 rather than 1.0.0 so that a reader printing the record
 * back cannot pass by printing zeroes.
 */

#include <astra/program.h>

ASTRA_PROGRAM("contract", 1, 2, 3, "Barry Walker",
              "Copyright 2026 Barry Walker");

_Thread_local static volatile int tls_initialized = 7;
_Thread_local static volatile int tls_zero;

static void __attribute__((constructor))
contract_initialize(void)
{
    tls_zero = tls_initialized;
}

static void __attribute__((destructor))
contract_finalize(void)
{
    tls_zero = 0;
}

/*
 * This freestanding contract does not link GCC's host crtbegin/crtend. Put the
 * records down directly so it tests Astra's linker/startup contract rather
 * than the Linux startup files installed beside the cross compiler.
 */
typedef void (*ContractLifecycle)(void);
static ContractLifecycle contract_initializer
    __attribute__((section(".init_array"), used)) = contract_initialize;
static ContractLifecycle contract_finalizer
    __attribute__((section(".fini_array"), used)) = contract_finalize;

/* The real runtime provides this from the current Astra thread. */
void *__m68k_read_tp(void);

void *
__m68k_read_tp(void)
{
    return 0;
}

void _start(void);

void
_start(void)
{
    tls_zero = tls_initialized;
}
