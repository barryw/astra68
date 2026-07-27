#ifndef ASTRA_KERNEL_PANIC_H
#define ASTRA_KERNEL_PANIC_H

struct KernelFaultReport;

void kernel_panic(const char *reason) __attribute__((noreturn));
void kernel_exception_panic(const void *frame) __attribute__((noreturn));
void kernel_exception_panic_classified(
    const void *frame, const struct KernelFaultReport *fault)
    __attribute__((noreturn));

#endif
