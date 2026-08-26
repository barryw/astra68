#include <astra/posix_descriptor.h>
#include <astra/stream.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>

#define ASTRA_TIOCGWINSZ ((unsigned long)(('T' << 8) | 1))
#define ASTRA_TIOCSWINSZ ((unsigned long)(('T' << 8) | 2))

_Static_assert(NCCS == ASTRA_TTY_CONTROL_CHARACTERS,
               "picolibc and Astra terminal control arrays differ");
_Static_assert(ICRNL == ASTRA_TTY_IFLAG_ICRNL &&
               OPOST == ASTRA_TTY_OFLAG_OPOST &&
               ONLCR == ASTRA_TTY_OFLAG_ONLCR &&
               CS8 == ASTRA_TTY_CFLAG_CS8 &&
               ECHO == ASTRA_TTY_LFLAG_ECHO &&
               ICANON == ASTRA_TTY_LFLAG_ICANON &&
               ISIG == ASTRA_TTY_LFLAG_ISIG,
               "picolibc termios flags drifted from the Astra wire ABI");
_Static_assert(VEOF == ASTRA_TTY_VEOF && VEOL == ASTRA_TTY_VEOL &&
               VERASE == ASTRA_TTY_VERASE && VINTR == ASTRA_TTY_VINTR &&
               VKILL == ASTRA_TTY_VKILL && VMIN == ASTRA_TTY_VMIN &&
               VQUIT == ASTRA_TTY_VQUIT && VSTART == ASTRA_TTY_VSTART &&
               VSTOP == ASTRA_TTY_VSTOP && VSUSP == ASTRA_TTY_VSUSP &&
               VTIME == ASTRA_TTY_VTIME,
               "picolibc termios indices drifted from the Astra wire ABI");

static uint32_t tty_handle(int fd)
{
    uint32_t handle = astra_posix_descriptor_handle(fd);

    if (handle == 0u)
        errno = EBADF;
    return handle;
}

static int tty_error(uint32_t status)
{
    errno = status == ASTRA_SYSCALL_INVALID_ARGUMENT ? ENOTTY : EIO;
    return -1;
}

static void state_to_termios(const AstraTtyState *state,
                             struct termios *attributes)
{
    attributes->c_iflag = state->input_flags;
    attributes->c_oflag = state->output_flags;
    attributes->c_cflag = state->control_flags;
    attributes->c_lflag = state->local_flags;
    (void)memcpy(attributes->c_cc, state->control_characters, NCCS);
    attributes->c_ispeed = state->input_speed;
    attributes->c_ospeed = state->output_speed;
}

static void termios_to_state(const struct termios *attributes,
                             AstraTtyState *state)
{
    state->input_flags = attributes->c_iflag;
    state->output_flags = attributes->c_oflag;
    state->control_flags = attributes->c_cflag;
    state->local_flags = attributes->c_lflag;
    (void)memcpy(state->control_characters, attributes->c_cc, NCCS);
    state->reserved8 = 0u;
    state->input_speed = attributes->c_ispeed;
    state->output_speed = attributes->c_ospeed;
}

speed_t cfgetispeed(const struct termios *attributes)
{
    return attributes != NULL ? attributes->c_ispeed : 0u;
}

speed_t cfgetospeed(const struct termios *attributes)
{
    return attributes != NULL ? attributes->c_ospeed : 0u;
}

int cfsetispeed(struct termios *attributes, speed_t speed)
{
    if (attributes == NULL) {
        errno = EINVAL;
        return -1;
    }
    attributes->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *attributes, speed_t speed)
{
    if (attributes == NULL) {
        errno = EINVAL;
        return -1;
    }
    attributes->c_ospeed = speed;
    return 0;
}

int tcgetattr(int fd, struct termios *attributes)
{
    AstraTtyState state;
    uint32_t handle;
    uint32_t status;

    if (attributes == NULL) {
        errno = EINVAL;
        return -1;
    }
    handle = tty_handle(fd);
    if (handle == 0u)
        return -1;
    status = astra_stream_tty_get(handle, &state);
    if (status != ASTRA_SYSCALL_OK)
        return tty_error(status);
    state_to_termios(&state, attributes);
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *attributes)
{
    AstraTtyState state;
    uint32_t handle;
    uint32_t apply;
    uint32_t status;

    if (attributes == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (action == TCSANOW)
        apply = ASTRA_TTY_APPLY_NOW;
    else if (action == TCSADRAIN)
        apply = ASTRA_TTY_APPLY_DRAIN;
    else if (action == TCSAFLUSH)
        apply = ASTRA_TTY_APPLY_DRAIN_FLUSH_INPUT;
    else {
        errno = EINVAL;
        return -1;
    }
    handle = tty_handle(fd);
    if (handle == 0u)
        return -1;
    status = astra_stream_tty_get(handle, &state);
    if (status != ASTRA_SYSCALL_OK)
        return tty_error(status);
    termios_to_state(attributes, &state);
    status = astra_stream_tty_set(handle, apply, &state);
    return status == ASTRA_SYSCALL_OK ? 0 : tty_error(status);
}

int tcgetwinsize(int fd, struct winsize *window)
{
    AstraTtyState state;
    uint32_t handle;
    uint32_t status;

    if (window == NULL) {
        errno = EINVAL;
        return -1;
    }
    handle = tty_handle(fd);
    if (handle == 0u)
        return -1;
    status = astra_stream_tty_get(handle, &state);
    if (status != ASTRA_SYSCALL_OK)
        return tty_error(status);
    window->ws_row = state.rows;
    window->ws_col = state.columns;
    window->ws_xpixel = state.pixel_width;
    window->ws_ypixel = state.pixel_height;
    return 0;
}

int tcsetwinsize(int fd, const struct winsize *window)
{
    if (tty_handle(fd) == 0u)
        return -1;
    if (window == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* The window server owns slave geometry; applications observe it. */
    errno = EPERM;
    return -1;
}

int ioctl(int fd, unsigned long operation, void *parameter)
{
    if (operation == ASTRA_TIOCGWINSZ)
        return tcgetwinsize(fd, parameter);
    if (operation == ASTRA_TIOCSWINSZ)
        return tcsetwinsize(fd, parameter);
    errno = ENOTTY;
    return -1;
}

int tcdrain(int fd)
{
    AstraTtyState state;
    uint32_t handle = tty_handle(fd);
    uint32_t status;

    if (handle == 0u)
        return -1;
    status = astra_stream_tty_get(handle, &state);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_stream_tty_set(handle, ASTRA_TTY_APPLY_DRAIN, &state);
    return status == ASTRA_SYSCALL_OK ? 0 : tty_error(status);
}

int tcflush(int fd, int queue)
{
    AstraTtyState state;
    uint32_t handle = tty_handle(fd);
    uint32_t status;

    if (handle == 0u)
        return -1;
    if (queue != TCIFLUSH && queue != TCOFLUSH && queue != TCIOFLUSH) {
        errno = EINVAL;
        return -1;
    }
    status = astra_stream_tty_get(handle, &state);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_stream_tty_set(
            handle,
            queue == TCIFLUSH ? ASTRA_TTY_FLUSH_INPUT :
            queue == TCOFLUSH ? ASTRA_TTY_FLUSH_OUTPUT : ASTRA_TTY_FLUSH_BOTH,
            &state);
    return status == ASTRA_SYSCALL_OK ? 0 : tty_error(status);
}

int tcflow(int fd, int action)
{
    if (tty_handle(fd) == 0u)
        return -1;
    if (action != TCIOFF && action != TCION && action != TCOOFF &&
        action != TCOON) {
        errno = EINVAL;
        return -1;
    }
    errno = ENOTSUP;
    return -1;
}

int tcsendbreak(int fd, int duration)
{
    (void)duration;
    if (tty_handle(fd) == 0u)
        return -1;
    return 0;
}

pid_t tcgetsid(int fd)
{
    if (tty_handle(fd) == 0u)
        return (pid_t)-1;
    errno = ENOTSUP;
    return (pid_t)-1;
}
