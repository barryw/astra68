#ifndef ASTRA_KERNEL_PANIC_H
#define ASTRA_KERNEL_PANIC_H

void kernel_panic(const char *reason) __attribute__((noreturn));
void kernel_exception_panic(const void *frame) __attribute__((noreturn));

#endif
