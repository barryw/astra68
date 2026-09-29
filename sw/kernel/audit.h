#ifndef ASTRA_KERNEL_AUDIT_H
#define ASTRA_KERNEL_AUDIT_H

/*
 * Whole-structure audits -- a *_pool_valid() that walks every slot of a pool,
 * the deadline heap's pairwise check -- cost hundreds to thousands of
 * instructions and were measured at a quarter of the kernel's time when they
 * ran after every syscall. Each pool also keeps an O(1) health state (its
 * corrupt flag) that every mutation maintains.
 *
 * The host suites run the audits after every operation, which is where a
 * broken invariant is found. The MC68040 image checks health instead, unless
 * it is built with `make KERNEL_AUDIT=1`, which puts every audit back for a
 * run on the machine itself.
 */
#ifndef KERNEL_AUDIT
#if defined(__m68k__)
#define KERNEL_AUDIT 0
#else
#define KERNEL_AUDIT 1
#endif
#endif

#endif
