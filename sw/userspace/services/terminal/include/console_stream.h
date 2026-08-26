#ifndef ASTRA_TERMINAL_CONSOLE_STREAM_H
#define ASTRA_TERMINAL_CONSOLE_STREAM_H

#include <stdint.h>

#include <astra/stream.h>
#include <astra/terminal.h>

/*
 * The terminal's end of a child's streams.
 *
 * The supervisor owns the screen and the keyboard, so it is where a launched
 * program's STDOUT arrives and where its STDIN comes from. Two ports: one that
 * takes text and renders it into the terminal the shell already owns, and one
 * that hands out canonical lines or lossless raw key bytes.
 *
 * Both move out of this process the day the terminal service does, and no
 * client changes when they do -- which is the point of them being ports.
 */

/* Creates both ports and binds the sink to `terminal`. Zero on failure. */
int console_stream_start(AstraTerminal *terminal);

/* Non-zero once the ports exist and there is something to grant. */
int console_stream_ready(void);

/* Receive endpoint used to sleep until a child writes terminal output. */
uint32_t console_stream_wait_handle(void);
uint32_t console_stream_input_wait_handle(void);

/*
 * The send handles a launch grants a child. Both point at the one sink today,
 * granted separately so they can later point at different things.
 */
uint32_t console_stream_stdout(void);
uint32_t console_stream_stderr(void);
uint32_t console_stream_stdin(void);

/* Publishes a live window-size change through the same terminal endpoint. */
void console_stream_resize(uint32_t columns, uint32_t rows,
                           uint32_t pixel_width, uint32_t pixel_height);

/* Saves/restores the controlling terminal around a foreground child. */
void console_stream_tty_state(AstraTtyState *state);
void console_stream_tty_restore(const AstraTtyState *state);

/* Feeds one translated key through the terminal's shared line discipline. */
int console_stream_key(uint32_t key);
int console_stream_terminal_reply(void *context, const uint8_t *bytes,
                                  uint32_t length);

/*
 * Points a launched program's STDOUT somewhere other than the terminal.
 *
 * `render` is handed each chunk the child writes, on the same loop that pumps
 * everything else here; NULL puts STDOUT back on the terminal and is the
 * ordinary state. This is what makes `ls > out.txt` a capability operation
 * rather than a shell trick: the child is granted a send handle to a different
 * port and never learns that anything is unusual about it.
 *
 * The port is made once and kept. Zero on failure, which leaves STDOUT where
 * it was -- a redirect that could not be made is a command that must not run,
 * and the caller is the one that knows how to say so.
 *
 * **STDERR is not moved.** A program whose output is in a file still has to be
 * able to say it failed somewhere a person is looking.
 */
int console_stream_redirect(AstraStreamRender render, void *context);

/* Non-zero while STDOUT points somewhere other than the terminal. */
int console_stream_redirected(void);

/*
 * The redirected sink's receive endpoint, or zero while nothing is redirected.
 *
 * A caller that sleeps on `console_stream_wait_handle` alone would sleep
 * through a redirected child filling its port -- STDERR still arrives on the
 * terminal's sink, so neither handle stands in for the other, and both have to
 * be in the wait.
 */
uint32_t console_stream_redirect_wait_handle(void);

/* Non-zero while input is waiting to be read. */
int console_stream_pending(void);

/*
 * Renders whatever arrived and answers whatever asked, bounded per call. This
 * runs on the loop a person is typing at, so a burst costs several passes
 * rather than a stall -- the same rule as the event drain beside it. Returns
 * the number of terminal-output messages rendered by this pass.
 */
uint32_t console_stream_pump(void);

/*
 * Relays what the sink still holds and returns when it holds nothing, so that
 * a launcher can finish reporting a child's output before it reports the
 * child. Bounded: a live writer cannot hold the prompt hostage. Returns how
 * many messages were relayed.
 */
uint32_t console_stream_drain(void);

/* Rendered messages, for the boot report and the tests. */
uint32_t console_stream_messages(void);

#endif
