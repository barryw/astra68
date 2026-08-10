#ifndef ASTRA_SUPERVISOR_CONSOLE_SHELL_H
#define ASTRA_SUPERVISOR_CONSOLE_SHELL_H

#include <stdint.h>

/*
 * Runs the terminal until it is asked to stop. Never returns a failure code:
 * the status halfword has no bits left, so how far it got is reported through
 * the progress counter and the caller parks either way.
 */
void console_shell_run(uint32_t display, uint32_t input, uint32_t input_irq,
                       int volume_ready);

#endif
