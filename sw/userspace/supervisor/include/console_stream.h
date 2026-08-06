#ifndef ASTRA_SUPERVISOR_CONSOLE_STREAM_H
#define ASTRA_SUPERVISOR_CONSOLE_STREAM_H

#include <stdint.h>

#include <astra/terminal.h>

/*
 * The terminal's end of a child's streams.
 *
 * The supervisor owns the screen and the keyboard, so it is where a launched
 * program's STDOUT arrives and where its STDIN comes from. Two ports: one that
 * takes text and renders it into the terminal the shell already owns, and one
 * that hands out what the line editor has finished with.
 *
 * Both move out of this process the day the terminal service does, and no
 * client changes when they do -- which is the point of them being ports rather
 * than the function pointer the storage service is still reached by.
 */

/* Creates both ports and binds the sink to `terminal`. Zero on failure. */
int console_stream_start(AstraTerminal *terminal);

/* Non-zero once the ports exist and there is something to grant. */
int console_stream_ready(void);

/*
 * The send handles a launch grants a child. Both point at the one sink today,
 * granted separately so they can later point at different things.
 */
uint32_t console_stream_stdout(void);
uint32_t console_stream_stderr(void);
uint32_t console_stream_stdin(void);

/*
 * Offers a finished line to whatever is reading STDIN, and returns how many
 * bytes were taken. None is the ordinary answer while nothing is reading or
 * while the previous line has not been collected.
 */
uint32_t console_stream_offer(const uint8_t *bytes, uint32_t length);

/* Non-zero while a line is waiting to be read. */
int console_stream_pending(void);

/*
 * Renders whatever arrived and answers whatever asked, bounded per call. This
 * runs on the loop a person is typing at, so a burst costs several passes
 * rather than a stall -- the same rule as the event drain beside it.
 */
void console_stream_pump(void);

/* Rendered messages, for the boot report and the tests. */
uint32_t console_stream_messages(void);

#endif
