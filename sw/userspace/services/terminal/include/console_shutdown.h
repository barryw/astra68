#ifndef ASTRA_TERMINAL_CONSOLE_SHUTDOWN_H
#define ASTRA_TERMINAL_CONSOLE_SHUTDOWN_H

#include <stdint.h>

/* A shell prompt alone is not idle: an editor may still be in the session. */
static inline int console_shutdown_safe(uint32_t prompt, uint32_t edited,
                                        uint32_t queued_input,
                                        uint32_t session_members,
                                        int32_t shell_group,
                                        int32_t foreground_group)
{
    return prompt != 0u && edited == 0u && queued_input == 0u &&
           session_members == 2u && shell_group > 0 &&
           shell_group == foreground_group;
}

#endif
