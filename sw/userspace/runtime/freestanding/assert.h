/*
 * Freestanding <assert.h> for Astra userspace.
 *
 * A failed assertion inside a service is a reportable fault, not a debugging
 * aid to be printed. There is no stdout to print to and no debugger attached,
 * so the handler ends the process with a tagged status the kernel turns into a
 * panic naming the source line.
 *
 * Unlike the standard header this is include-once and does not re-evaluate
 * NDEBUG on each inclusion. Nothing in Astra toggles NDEBUG mid-translation
 * unit, and honouring that would trade a real property for a formality.
 */
#ifndef ASTRA_FREESTANDING_ASSERT_H
#define ASTRA_FREESTANDING_ASSERT_H

void astra_assert_failed(const char *file, unsigned int line,
                         const char *expression) __attribute__((noreturn));

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression)                                            \
    ((expression) ? (void)0                                           \
                  : astra_assert_failed(__FILE__, __LINE__, #expression))
#endif

#endif
