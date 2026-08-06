/*
 * The terminal the machine boots into.
 *
 * This is the glue the design conversation called glue: it pumps keys from the
 * input device into the line editor, runs the command the editor hands back,
 * and pushes the resulting cells at the display lease. The parts worth keeping
 * are underneath it -- the cell model, the keymap, the line editor.
 *
 * It names no filesystem. Every command reaches storage through the protocol
 * in astra/vfs_client.h, like any other client would, and this file is built
 * without lwext4 on its include path so a filesystem call reappearing here
 * fails to compile rather than being noticed in review.
 *
 * It also builds no paths. There is no root on this machine: a path is
 * ASSIGN:rest, the shell stands in an assign and a directory under it, and
 * turning a typed word into something the protocol can carry is two Kit calls
 * -- qualify, then resolve. Both are host-tested; this file is glue, and the
 * arrangement is deliberate because glue cannot be tested here.
 */

#include <console_shell.h>
#include <console_stream.h>
#include <events_host.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/keymap.h>
#include <astra/runtime.h>
#include <astra/shell.h>
#include <astra/supervisor.h>
#include <astra/syscall.h>
#include <astra/terminal.h>

#include <astra/event_emit.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>
#include <astra/vfs_path.h>
#include <vfs_host.h>

/* A path the protocol will refuse to carry is not worth building. */
#define SHELL_PATH_MAX ASTRA_VFS_PATH_MAX
#define SHELL_READ_CHUNK 128u

/*
 * The load buffer, and its written ceiling.
 *
 * A launch takes an image in the launcher's memory, so somebody has to hold
 * the whole program while the kernel copies it out. One buffer, one launch at
 * a time: the alternative is an allocation whose failure mode is a shell that
 * cannot start anything when memory is tight, which is exactly when a person
 * needs to start something.
 *
 * 64 KiB is generous for a command and cheap against 29 MiB of usable RAM.
 * Anything larger is an application, and applications are bundles in APPS:,
 * which is a different mechanism and a later milestone.
 */
#define SHELL_LOAD_MAX (64u * 1024u)

typedef struct ConsoleShell {
    AstraTerminal terminal;
    astra_shell_editor_t editor;
    uint32_t display;
    uint32_t input;
    uint32_t modifiers;
    char assign[ASTRA_CAPABILITY_NAME_MAX];  /* the assign it is standing in */
    char directory[SHELL_PATH_MAX];          /* normalised, under that assign */
    /*
     * The child, while there is one. A line typed with this set is that
     * child's input rather than the shell's next command: the terminal has one
     * keyboard, and whoever is in the foreground gets it.
     */
    uint32_t child;
    int running;
} ConsoleShell;

static ConsoleShell shell;
/*
 * Outside the frame for the same reason the input batch is: a user thread gets
 * one 4 KiB stack, and 64 KiB was never going to be on it.
 */
static uint8_t load_buffer[SHELL_LOAD_MAX];

static uint32_t shell_strlen(const char *text)
{
    uint32_t length = 0u;

    while (text[length] != '\0')
        ++length;
    return length;
}

static int shell_equal(const char *left, const char *right)
{
    uint32_t index = 0u;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index])
            return 0;
        ++index;
    }
    return left[index] == right[index];
}

/*
 * A word typed at the prompt becomes a path the protocol can carry, or a
 * refusal that says which kind it was. The rights are the command's own, so a
 * write through an assign granted only read is refused here rather than by the
 * disk -- and a name the shell was never given does not resolve at all.
 */
static uint32_t shell_path(const char *name, uint32_t rights, char *wire,
                           uint32_t capacity, AstraVfsClient **client)
{
    char typed[SHELL_PATH_MAX];
    const AstraAssign *assign = NULL;
    uint32_t status;

    status = astra_path_qualify(shell.assign, shell.directory, name, typed,
                                sizeof(typed));
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_assign_resolve(supervisor_assigns(), typed, rights, wire,
                                  capacity, &assign);
    if (status != ASTRA_VFS_OK)
        return status;
    /*
     * A name is a binding to a mount, and there are two mounts now. Asking
     * which service speaks for the assign is what keeps the shell from sending
     * an EVENTS: path to the volume -- and it is the same question a launched
     * program will answer with a port handle.
     */
    *client = supervisor_vfs_client_for(assign);
    return *client != NULL ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
}

/* The one place the shell reaches storage. NULL when no volume is mounted. */
static AstraVfsClient *storage(void)
{
    return supervisor_vfs_client();
}

/* "input read refused, status N" without a formatter, since there is none. */
static uint32_t describe_input_failure(char *out, uint32_t capacity,
                                       uint32_t status)
{
    static const char prefix[] = "input read refused, status ";
    uint32_t length = 0u;
    char digits[10];
    uint32_t count = 0u;

    while (prefix[length] != '\0' && length + 1u < capacity) {
        out[length] = prefix[length];
        ++length;
    }
    if (status == 0u && length + 1u < capacity) {
        out[length++] = '0';
    }
    while (status != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (status % 10u));
        status /= 10u;
    }
    while (count != 0u && length + 1u < capacity) {
        out[length++] = digits[--count];
    }
    out[length] = '\0';
    return length;
}

static void write_line(const char *text)
{
    astra_terminal_write(&shell.terminal, text);
    astra_terminal_putc(&shell.terminal, '\n');
}

static void write_number(uint32_t value)
{
    char digits[12];
    uint32_t index = 0u;

    if (value == 0u) {
        astra_terminal_putc(&shell.terminal, '0');
        return;
    }
    while (value != 0u && index < sizeof(digits)) {
        digits[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (index != 0u)
        astra_terminal_putc(&shell.terminal, (uint8_t)digits[--index]);
}

/*
 * Protocol statuses, not errno. The shell cannot know what filesystem answered
 * and has no business printing its error numbers; these are the same values
 * any client of the storage protocol sees.
 */
static void report_status(const char *what, uint32_t status)
{
    static const char *const text[] = {
        "ok", "protocol error", "not found", "already exists",
        "not a directory", "is a directory", "access denied", "no space",
        "invalid", "bad handle", "limit reached", "I/O error", "not empty",
        "unsupported", "busy", "buffer too small"
    };

    /*
     * The screen tells the person; the event tells the machine. A refused
     * command used to leave nothing behind at all once the line scrolled off,
     * which is the failure this whole system exists to stop.
     */
    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                 "command refused, status %u", status);
    astra_terminal_write(&shell.terminal, what);
    astra_terminal_write(&shell.terminal, ": ");
    if (status < (uint32_t)(sizeof(text) / sizeof(text[0]))) {
        astra_terminal_write(&shell.terminal, text[status]);
    } else {
        astra_terminal_write(&shell.terminal, "status ");
        write_number(status);
    }
    astra_terminal_putc(&shell.terminal, '\n');
}

static void command_ls(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    char name[ASTRA_VFS_NAME_MAX];
    AstraVfsClient *client = NULL;
    uint64_t cursor = 0u;
    uint32_t shown = 0u;
    uint16_t kind = 0u;
    uint32_t status;

    if (storage() == NULL) {
        write_line("ls: no volume");
        return;
    }
    status = shell_path(argc > 1 ? argv[1] : NULL, ASTRA_RIGHT_READ, path,
                        sizeof(path), &client);
    if (status != ASTRA_VFS_OK) {
        report_status("ls", status);
        return;
    }
    for (;;) {
        status = astra_vfs_readdir(client, path, cursor, name, sizeof(name),
                                   &kind, &cursor);
        /* Running past the last entry is how a listing ends, not a failure. */
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            break;
        if (status != ASTRA_VFS_OK) {
            report_status("ls", status);
            return;
        }
        astra_terminal_write(&shell.terminal, name);
        if (kind == ASTRA_VFS_KIND_DIRECTORY)
            astra_terminal_putc(&shell.terminal, '/');
        astra_terminal_putc(&shell.terminal, '\n');
        ++shown;
    }
    if (shown == 0u)
        write_line("(empty)");
}

static void command_cd(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    char wire[SHELL_PATH_MAX];
    char name[ASTRA_CAPABILITY_NAME_MAX];
    const AstraAssign *assign = NULL;
    AstraVfsClient *client = NULL;
    uint16_t kind = 0u;
    uint32_t status;

    if (storage() == NULL) {
        write_line("cd: no volume");
        return;
    }
    /* `cd` alone goes to the assign's root, not to where it already is. */
    status = astra_path_qualify(shell.assign,
                                argc > 1 ? shell.directory : "",
                                argc > 1 ? argv[1] : NULL, typed,
                                sizeof(typed));
    if (status == ASTRA_VFS_OK) {
        status = astra_assign_resolve(supervisor_assigns(), typed,
                                      ASTRA_RIGHT_READ, wire, sizeof(wire),
                                      &assign);
    }
    if (status == ASTRA_VFS_OK) {
        client = supervisor_vfs_client_for(assign);
        if (client == NULL)
            status = ASTRA_VFS_ERR_NOT_FOUND;
    }
    if (status != ASTRA_VFS_OK) {
        report_status("cd", status);
        return;
    }
    /* Verified before it is adopted, so the prompt never lies. */
    status = astra_vfs_stat(client, wire, NULL, &kind);
    if (status != ASTRA_VFS_OK) {
        report_status("cd", status);
        return;
    }
    if (kind != ASTRA_VFS_KIND_DIRECTORY) {
        write_line("cd: not a directory");
        return;
    }
    /*
     * Adopted only now, and taken apart by the same parser that resolved it.
     * `wire` is finished with, so it is the scratch the split needs rather
     * than a fourth buffer on a stack that has lwext4 underneath it.
     */
    if (astra_path_split(typed, name, sizeof(name), wire, sizeof(wire)) !=
            ASTRA_VFS_OK ||
        astra_path_normalise(wire, shell.directory,
                             sizeof(shell.directory)) != ASTRA_VFS_OK) {
        shell.directory[0] = '\0';
        write_line("cd: path too long");
        return;
    }
    (void)memcpy(shell.assign, name, sizeof(shell.assign));
}

static void command_cat(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    uint8_t chunk[SHELL_READ_CHUNK];
    AstraVfsClient *client = NULL;
    AstraVfsFile file;
    uint64_t offset = 0u;
    uint32_t moved = 0u;
    uint32_t status;

    if (storage() == NULL) {
        write_line("cat: no volume");
        return;
    }
    if (argc < 2) {
        write_line("cat: needs a file");
        return;
    }
    status = shell_path(argv[1], ASTRA_RIGHT_READ, path, sizeof(path),
                        &client);
    if (status != ASTRA_VFS_OK) {
        report_status("cat", status);
        return;
    }
    status = astra_vfs_open(client, path, ASTRA_VFS_OPEN_READ, &file, NULL,
                            NULL);
    if (status != ASTRA_VFS_OK) {
        report_status("cat", status);
        return;
    }
    for (;;) {
        status = astra_vfs_read(client, file, offset, chunk, sizeof(chunk),
                                &moved);
        if (status != ASTRA_VFS_OK) {
            report_status("cat", status);
            break;
        }
        /* A short read is normal: one message carries a bounded payload. */
        if (moved == 0u)
            break;
        astra_terminal_write_bytes(&shell.terminal, chunk, moved);
        offset += moved;
    }
    (void)astra_vfs_close(client, file);
    astra_terminal_putc(&shell.terminal, '\n');
}

static void command_mkdir(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    AstraVfsClient *client = NULL;
    uint32_t status;

    if (storage() == NULL) {
        write_line("mkdir: no volume");
        return;
    }
    if (argc < 2) {
        write_line("mkdir: needs a name");
        return;
    }
    status = shell_path(argv[1], ASTRA_RIGHT_WRITE, path, sizeof(path),
                        &client);
    if (status != ASTRA_VFS_OK) {
        report_status("mkdir", status);
        return;
    }
    status = astra_vfs_mkdir(client, path);
    if (status != ASTRA_VFS_OK)
        report_status("mkdir", status);
}

/* write NAME TEXT... -- creates or truncates, then writes the rest of the line. */
static void command_write(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    AstraVfsClient *client = NULL;
    AstraVfsFile file;
    uint64_t offset = 0u;
    uint32_t moved = 0u;
    uint32_t status;
    int index;

    if (storage() == NULL) {
        write_line("write: no volume");
        return;
    }
    if (argc < 2) {
        write_line("write: needs a name");
        return;
    }
    status = shell_path(argv[1], ASTRA_RIGHT_WRITE, path, sizeof(path),
                        &client);
    if (status != ASTRA_VFS_OK) {
        report_status("write", status);
        return;
    }
    status = astra_vfs_open(client, path,
                            ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                                ASTRA_VFS_OPEN_TRUNCATE,
                            &file, NULL, NULL);
    if (status != ASTRA_VFS_OK) {
        report_status("write", status);
        return;
    }
    for (index = 2; index < argc; ++index) {
        const char *word = argv[index];
        uint32_t length = shell_strlen(word);
        uint32_t sent = 0u;

        /*
         * Looping over a short write rather than treating one as an error.
         * A message carries a bounded payload today and a ring will still
         * transfer short later, so the caller has to be written this way
         * regardless.
         */
        while (sent < length) {
            status = astra_vfs_write(client, file, offset + sent,
                                     word + sent, length - sent, &moved);
            if (status != ASTRA_VFS_OK) {
                report_status("write", status);
                goto finish;
            }
            if (moved == 0u) {
                write_line("write: stalled");
                goto finish;
            }
            sent += moved;
        }
        offset += sent;
        status = astra_vfs_write(client, file, offset,
                                 index + 1 < argc ? " " : "\n", 1u, &moved);
        if (status != ASTRA_VFS_OK) {
            report_status("write", status);
            goto finish;
        }
        offset += moved;
    }
finish:
    (void)astra_vfs_close(client, file);
}

static void command_rm(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    AstraVfsClient *client = NULL;
    uint32_t status;

    if (storage() == NULL) {
        write_line("rm: no volume");
        return;
    }
    if (argc < 2) {
        write_line("rm: needs a name");
        return;
    }
    status = shell_path(argv[1], ASTRA_RIGHT_WRITE, path, sizeof(path),
                        &client);
    if (status != ASTRA_VFS_OK) {
        report_status("rm", status);
        return;
    }
    status = astra_vfs_unlink(client, path);
    if (status != ASTRA_VFS_OK)
        report_status("rm", status);
}

/*
 * `events` -- the half of reading that a path cannot express.
 *
 * EVENTS: already answers "show me": that is `cat`, and it is the whole
 * interface for one filter. What a path cannot say is "the last screen of it",
 * "two dimensions at once" and "keep going". So this command is a path builder
 * and a pager, and nothing it can ask for is anything `cat` could not have
 * asked for -- which is what stops it becoming a second door onto the store.
 */

#define EVENTS_TAIL_MAX 64u

static uint32_t events_append(char *out, uint32_t capacity, uint32_t at,
                              const char *text)
{
    while (*text != '\0' && at + 1u < capacity) {
        out[at++] = *text++;
    }
    out[at] = '\0';
    return at;
}

/* Exactly what the activity directory prints: eight lowercase hex digits. */
static int events_activity_name(const char *typed, char *out)
{
    static const char digits[] = "0123456789abcdef";
    uint32_t value = 0u;
    uint32_t seen = 0u;

    while (typed[seen] != '\0') {
        char digit = typed[seen];
        uint32_t nibble;

        if (digit >= '0' && digit <= '9')
            nibble = (uint32_t)(digit - '0');
        else if (digit >= 'a' && digit <= 'f')
            nibble = 10u + (uint32_t)(digit - 'a');
        else if (digit >= 'A' && digit <= 'F')
            nibble = 10u + (uint32_t)(digit - 'A');
        else
            return 0;
        value = (value << 4) | nibble;
        ++seen;
        if (seen > 8u)
            return 0;
    }
    if (seen == 0u)
        return 0;
    for (uint32_t index = 0u; index < 8u; ++index) {
        out[index] = digits[(value >> ((7u - index) * 4u)) & 0xFu];
    }
    out[8] = '\0';
    return 1;
}

/* Prints from `offset` to the end, and returns where it stopped. */
static uint64_t events_print(AstraVfsClient *client, AstraVfsFile file,
                             uint64_t offset)
{
    uint8_t chunk[SHELL_READ_CHUNK];

    for (;;) {
        uint32_t moved = 0u;

        if (astra_vfs_read(client, file, offset, chunk, sizeof(chunk),
                           &moved) != ASTRA_VFS_OK || moved == 0u)
            break;
        astra_terminal_write_bytes(&shell.terminal, chunk, moved);
        offset += moved;
    }
    return offset;
}

/*
 * Where the last `lines` lines begin. A ring of offsets rather than of text:
 * the screen decides how many, and re-reading is cheaper than holding a copy of
 * something the store is still appending to.
 */
static uint64_t events_tail(AstraVfsClient *client, AstraVfsFile file,
                            uint32_t lines, uint64_t *end)
{
    uint64_t starts[EVENTS_TAIL_MAX];
    uint8_t chunk[SHELL_READ_CHUNK];
    uint64_t offset = 0u;
    uint32_t count = 0u;
    uint32_t oldest = 0u;

    if (lines == 0u || lines > EVENTS_TAIL_MAX)
        lines = EVENTS_TAIL_MAX;
    starts[0] = 0u;
    count = 1u;
    for (;;) {
        uint32_t moved = 0u;

        if (astra_vfs_read(client, file, offset, chunk, sizeof(chunk),
                           &moved) != ASTRA_VFS_OK || moved == 0u)
            break;
        for (uint32_t index = 0u; index < moved; ++index) {
            if (chunk[index] != '\n')
                continue;
            /* A line begins after the newline that ended the one before. */
            if (count == lines) {
                starts[oldest] = offset + index + 1u;
                oldest = (oldest + 1u) % lines;
            } else {
                starts[count++] = offset + index + 1u;
            }
        }
        offset += moved;
    }
    if (end != NULL)
        *end = offset;
    return count == lines ? starts[oldest] : starts[0];
}

/* Live, until a key is pressed. Nothing else on this machine can interrupt. */
static void events_follow(AstraVfsClient *client, AstraVfsFile file,
                          uint64_t offset)
{
    static AstraInputEvent keys[ASTRA_INPUT_READ_BATCH_MAX];

    write_line("-- following, press any key --");
    for (;;) {
        uint32_t count = 0u;
        uint32_t flags = 0u;
        uint64_t moved_to;

        /*
         * The drain, because the events service is in this process and this
         * loop is now the machine's only one. Without it the file cannot grow
         * and a follow would watch a store nobody is filling.
         */
        supervisor_events_pump();
        moved_to = events_print(client, file, offset);
        if (moved_to != offset) {
            offset = moved_to;
            (void)astra_terminal_flush(&shell.terminal);
        }
        if (astra_input_read(shell.input, keys, ASTRA_INPUT_READ_BATCH_MAX,
                             &count, &flags) == ASTRA_SYSCALL_OK && count != 0u)
            break;
        (void)astra_yield();
    }
    write_line("");
}

static void command_events(int argc, char *const *argv)
{
    static const char *const level_name[] = {"all", "notice", "warning",
                                             "error"};
    char path[SHELL_PATH_MAX];
    char activity[9];
    const char *level = "notice";
    const char *subsystem = NULL;
    AstraVfsClient *client = NULL;
    AstraVfsFile file;
    uint64_t offset;
    uint64_t end = 0u;
    uint32_t at = 0u;
    uint32_t status;
    int follow = 0;
    int by_activity = 0;

    for (int index = 1; index < argc; ++index) {
        const char *word = argv[index];
        const char *value = index + 1 < argc ? argv[index + 1] : NULL;

        if (shell_equal(word, "--all")) {
            level = "all";
        } else if (shell_equal(word, "--follow")) {
            follow = 1;
        } else if (shell_equal(word, "--level")) {
            int known = 0;

            if (value == NULL) {
                write_line("events: --level needs a name");
                return;
            }
            /*
             * debug is not a level the store has. It is a live subscription,
             * dropped at drain, and saying so is better than showing `all` and
             * letting somebody conclude that nothing was emitted.
             */
            if (shell_equal(value, "debug")) {
                write_line("events: debug is never stored, showing all");
                level = "all";
                known = 1;
            }
            for (uint32_t name = 0u;
                 !known && name < sizeof(level_name) / sizeof(level_name[0]);
                 ++name) {
                if (shell_equal(value, level_name[name])) {
                    level = level_name[name];
                    known = 1;
                }
            }
            if (!known) {
                write_line("events: levels are all, notice, warning, error");
                return;
            }
            ++index;
        } else if (shell_equal(word, "--subsystem")) {
            if (value == NULL) {
                write_line("events: --subsystem needs a name");
                return;
            }
            subsystem = value;
            ++index;
        } else if (shell_equal(word, "--activity")) {
            if (value == NULL || !events_activity_name(value, activity)) {
                write_line("events: --activity takes up to eight hex digits");
                return;
            }
            by_activity = 1;
            ++index;
        } else if (shell_equal(word, "--boot")) {
            /*
             * Recognised and refused with the reason, rather than built into a
             * path that answers `not found` -- which is also what a typo gives,
             * and the two must not look the same.
             */
            write_line("events: only this boot is kept; the store is RAM");
            return;
        } else if (shell_equal(word, "--since")) {
            write_line("events: no wall clock yet, so no time range");
            return;
        } else if (shell_equal(word, "--level-set")) {
            write_line("events: setting a level needs the port transport");
            return;
        } else {
            write_line("events: unknown option");
            return;
        }
    }

    if (by_activity && subsystem != NULL) {
        /* An activity is already a slice through every other dimension. */
        write_line("events: --activity is every subsystem, by definition");
        return;
    }
    if (follow && shell.input == 0u) {
        write_line("events: no keyboard, so nothing could end a follow");
        return;
    }

    at = events_append(path, sizeof(path), at, "EVENTS:");
    if (by_activity) {
        at = events_append(path, sizeof(path), at, "activity/");
        at = events_append(path, sizeof(path), at, activity);
    } else if (subsystem != NULL) {
        at = events_append(path, sizeof(path), at, "subsystem/");
        at = events_append(path, sizeof(path), at, subsystem);
        at = events_append(path, sizeof(path), at, "/");
        at = events_append(path, sizeof(path), at, level);
    } else {
        at = events_append(path, sizeof(path), at, "boot/current/");
        at = events_append(path, sizeof(path), at, level);
    }

    {
        char wire[SHELL_PATH_MAX];

        status = shell_path(path, ASTRA_RIGHT_READ, wire, sizeof(wire),
                            &client);
        if (status != ASTRA_VFS_OK) {
            report_status("events", status);
            return;
        }
        status = astra_vfs_open(client, wire, ASTRA_VFS_OPEN_READ, &file, NULL,
                                NULL);
    }
    if (status != ASTRA_VFS_OK) {
        report_status("events", status);
        return;
    }
    /* The last screen of it, because a terminal is what is doing the reading. */
    offset = events_tail(client, file, shell.terminal.rows - 1u, &end);
    offset = events_print(client, file, offset);
    if (offset == 0u)
        write_line("(nothing at that level)");
    if (follow)
        events_follow(client, file, offset);
    (void)astra_vfs_close(client, file);
}

static void command_help(void)
{
    write_line("commands: ls [dir], cd [dir], cat FILE, mkdir DIR,");
    write_line("          write FILE TEXT..., rm FILE, pwd, clear, help");
    write_line("          events [--all|--level L|--subsystem S|--activity ID]");
    write_line("                 [--follow]");
    write_line("paths are ASSIGN:path -- there is no root. try ls SYS:");
    write_line("any other word is a program in COMMANDS:. try status 7");
}

static void prompt(void)
{
    astra_terminal_write(&shell.terminal, shell.assign);
    astra_terminal_write(&shell.terminal, ":");
    astra_terminal_write(&shell.terminal, shell.directory);
    astra_terminal_write(&shell.terminal, "> ");
}

static int pump_once(void);

/*
 * Where a bare word is looked for, and the whole of the order.
 *
 * `APPS:` then `COMMANDS:`, top level only, per layout 2.5 and launch 5.1.
 * Subdirectories exist and are never searched: a name with a `/` in it is a
 * category the person typed, not somewhere the shell went looking. There is no
 * PATH, and the thing PATH is actually for -- reaching a program that is not
 * where the machine looks -- is an assign, so a word carrying a `:` is taken as
 * the name of one and resolved directly.
 */
static uint32_t launch_path(const char *word, char *wire, uint32_t capacity,
                            AstraVfsClient **client)
{
    static const char *const places[] = {"APPS:", "COMMANDS:"};
    char typed[SHELL_PATH_MAX];
    const AstraAssign *assign = NULL;
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t index = 0u; word[index] != '\0'; ++index) {
        if (word[index] != ':') {
            continue;
        }
        /* Named its own assign: one place, no search, and answerable. */
        status = astra_assign_resolve(supervisor_assigns(), word,
                                      ASTRA_RIGHT_READ, wire, capacity,
                                      &assign);
        if (status != ASTRA_VFS_OK) {
            return status;
        }
        *client = supervisor_vfs_client_for(assign);
        return *client != NULL ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
    }

    for (uint32_t place = 0u; place < 2u; ++place) {
        uint32_t length = 0u;

        while (places[place][length] != '\0' &&
               length + 1u < sizeof(typed)) {
            typed[length] = places[place][length];
            ++length;
        }
        for (uint32_t index = 0u;
             word[index] != '\0' && length + 1u < sizeof(typed); ++index) {
            typed[length++] = word[index];
        }
        typed[length] = '\0';
        status = astra_assign_resolve(supervisor_assigns(), typed,
                                      ASTRA_RIGHT_READ, wire, capacity,
                                      &assign);
        if (status != ASTRA_VFS_OK) {
            continue;   /* not bound, or not there: try the next place */
        }
        *client = supervisor_vfs_client_for(assign);
        if (*client != NULL) {
            return ASTRA_VFS_OK;
        }
    }
    return status;
}

/*
 * The grants a launched command is handed.
 *
 * The three streams and nothing else, today. The namespace a child ought to
 * get -- WORK:, COMMANDS:, EVENTS: -- cannot be granted yet: an assign's handle
 * is a routing token this file invented, not a kernel handle, because the
 * storage service is still reached by a function pointer. Task 5 turns those
 * into port handles and this function grows three entries.
 *
 * SIGNAL and nothing more. A child sends on these and never receives; the reply
 * port a read needs is one the child creates and holds itself.
 */
static uint32_t launch_grants(AstraLaunchGrant *grants)
{
    static const char *const names[] = {"STDOUT", "STDERR", "STDIN"};
    uint32_t handles[3];
    uint32_t count = 0u;

    handles[0] = console_stream_stdout();
    handles[1] = console_stream_stderr();
    handles[2] = console_stream_stdin();
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (handles[index] == 0u) {
            continue;
        }
        astra_capability_name_set(grants[count].name, names[index]);
        grants[count].handle = handles[index];
        grants[count].rights = ASTRA_RIGHT_SIGNAL;
        /* A stream is authority, not a name: STDOUT:file.txt is nonsense. */
        grants[count].flags = 0u;
        ++count;
    }
    return count;
}

/* argv, packed the way the ABI carries it: the words back to back, each NUL. */
static int launch_arguments(AstraLaunchArguments *arguments, int argc,
                            char *const *argv)
{
    uint32_t length = 0u;
    uint32_t count = 0u;

    (void)memset(arguments, 0, sizeof(*arguments));
    while ((int)count < argc && count < ASTRA_LAUNCH_ARGUMENT_MAX) {
        const char *word = argv[count];
        uint32_t index = 0u;

        while (word[index] != '\0') {
            if (length + 2u > ASTRA_LAUNCH_ARGUMENT_BYTES) {
                return 0;   /* refused whole: a cut argument is a wrong one */
            }
            arguments->bytes[length++] = word[index++];
        }
        arguments->bytes[length++] = '\0';
        ++count;
    }
    if ((int)count != argc) {
        return 0;
    }
    arguments->count = (uint16_t)count;
    arguments->length = (uint16_t)length;
    return 1;
}

/*
 * Reads the whole image in, launches it, and serves it until it is done.
 *
 * **The wait serves.** The events service, the stream sink and the stream
 * source are all in this process, so a wait that stopped pumping them would
 * stop the child that is calling them -- and the shell would be blocked on a
 * child blocked on the shell. That is the deadlock this architecture makes
 * easiest to write, and a poll inside the loop that already pumps everything is
 * what avoids it.
 */
static void command_launch(const char *word, int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX];
    AstraLaunchArguments arguments;
    AstraVfsClient *client = NULL;
    AstraVfsFile file;
    uint64_t size = 0u;
    uint32_t length = 0u;
    uint32_t handle = 0u;
    uint32_t child_id = 0u;
    uint32_t exit_status = 0u;
    uint32_t status;
    uint16_t kind = 0u;

    if (launch_path(word, path, sizeof(path), &client) != ASTRA_VFS_OK) {
        astra_terminal_write(&shell.terminal, word);
        write_line(": not a command");
        return;
    }
    status = astra_vfs_open(client, path, ASTRA_VFS_OPEN_READ, &file, &size,
                            &kind);
    if (status != ASTRA_VFS_OK) {
        astra_terminal_write(&shell.terminal, word);
        write_line(": not a command");
        return;
    }
    if (size > SHELL_LOAD_MAX) {
        (void)astra_vfs_close(client, file);
        report_status(word, ASTRA_VFS_ERR_LIMIT);
        return;
    }
    for (;;) {
        uint32_t moved = 0u;

        status = astra_vfs_read(client, file, length, load_buffer + length,
                                SHELL_LOAD_MAX - length, &moved);
        if (status != ASTRA_VFS_OK) {
            break;
        }
        if (moved == 0u) {
            break;
        }
        length += moved;
    }
    (void)astra_vfs_close(client, file);
    if (status != ASTRA_VFS_OK || length == 0u) {
        report_status(word, status != ASTRA_VFS_OK ? status :
                                                     ASTRA_VFS_ERR_INVALID);
        return;
    }
    if (!launch_arguments(&arguments, argc, argv)) {
        write_line("too many arguments");
        return;
    }

    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "launching, %u bytes of image", length);
    status = astra_launch(load_buffer, length, grants,
                          launch_grants(grants), &arguments, &handle,
                          &child_id);
    if (status != ASTRA_SYSCALL_OK) {
        astra_terminal_write(&shell.terminal, word);
        write_line(": would not start");
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                     "launch refused, status %u", status);
        return;
    }

    shell.child = handle;
    for (;;) {
        /*
         * A poll, not a block. Everything the child might be calling is in
         * this process, so this loop has to keep running for the child to get
         * anywhere -- and pump_once is the same loop body the prompt uses,
         * which is what stops the two drifting apart.
         */
        status = astra_process_wait(handle, 0u, &exit_status);
        if (status != ASTRA_SYSCALL_TIMED_OUT) {
            break;
        }
        if (!pump_once()) {
            break;
        }
    }
    shell.child = 0u;
    (void)astra_close(handle);

    astra_terminal_write(&shell.terminal, word);
    if (status == ASTRA_SYSCALL_OK) {
        astra_terminal_write(&shell.terminal, ": exited ");
        write_number(exit_status);
        astra_terminal_putc(&shell.terminal, '\n');
    } else {
        write_line(": did not finish");
    }
    ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "child %u finished with status %u", child_id, exit_status);
}

static void run_line(const char *line)
{
    astra_shell_words_t words;

    if (astra_shell_parse(line, &words) != ASTRA_SHELL_OK || words.argc == 0)
        return;

    /*
     * A typed line is a unit of work, so it is where a story begins. Every
     * event emitted from here until the next line -- by the shell, by the Kit,
     * by the service and by the backend, because they are all downstream of
     * this thread -- carries this activity, and reading one command's account
     * is then reading one number.
     */
    (void)astra_activity_begin();
    supervisor_vfs_set_activity(astra_activity_current());
    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "command accepted, %u words", (uint32_t)words.argc);

    if (shell_equal(words.argv[0], "help"))
        command_help();
    else if (shell_equal(words.argv[0], "clear"))
        astra_terminal_clear(&shell.terminal);
    else if (shell_equal(words.argv[0], "pwd")) {
        astra_terminal_write(&shell.terminal, shell.assign);
        astra_terminal_putc(&shell.terminal, ':');
        write_line(shell.directory);
    } else if (shell_equal(words.argv[0], "ls"))
        command_ls(words.argc, words.argv);
    else if (shell_equal(words.argv[0], "cd"))
        command_cd(words.argc, words.argv);
    else if (shell_equal(words.argv[0], "cat"))
        command_cat(words.argc, words.argv);
    else if (shell_equal(words.argv[0], "mkdir"))
        command_mkdir(words.argc, words.argv);
    else if (shell_equal(words.argv[0], "write"))
        command_write(words.argc, words.argv);
    else if (shell_equal(words.argv[0], "rm"))
        command_rm(words.argc, words.argv);
    else if (shell_equal(words.argv[0], "events"))
        command_events(words.argc, words.argv);
    else
        /*
         * Not a builtin, so it is a program. There is no third case: a word
         * the machine does not recognise is a file it has not got, and saying
         * that is the whole of the answer.
         */
        command_launch(words.argv[0], words.argc, words.argv);
}

/* The renderer: one run of cells at a time, through the display lease. */
static int render_cells(void *context, uint32_t row, uint32_t column,
                        const uint8_t *cells, uint32_t count)
{
    ConsoleShell *state = context;
    uint32_t cell = row * state->terminal.columns + column;
    uint32_t written = 0u;

    while (written < count) {
        uint32_t run = count - written;

        if (run > ASTRA_CONSOLE_WRITE_MAX)
            run = ASTRA_CONSOLE_WRITE_MAX;
        if (astra_console_write(state->display, cell + written,
                                cells + written, run) != ASTRA_SYSCALL_OK)
            return 0;
        written += run;
    }
    return 1;
}

static void feed_key(uint32_t code)
{
    astra_shell_input_t event;
    astra_shell_result_t result;

    event.character = 0u;
    switch (code) {
    case ASTRA_KEYMAP_ENTER: event.key = ASTRA_SHELL_KEY_ENTER; break;
    case ASTRA_KEYMAP_BACKSPACE: event.key = ASTRA_SHELL_KEY_BACKSPACE; break;
    case ASTRA_KEYMAP_DELETE: event.key = ASTRA_SHELL_KEY_DELETE; break;
    case ASTRA_KEYMAP_LEFT: event.key = ASTRA_SHELL_KEY_LEFT; break;
    case ASTRA_KEYMAP_RIGHT: event.key = ASTRA_SHELL_KEY_RIGHT; break;
    case ASTRA_KEYMAP_HOME: event.key = ASTRA_SHELL_KEY_HOME; break;
    case ASTRA_KEYMAP_END: event.key = ASTRA_SHELL_KEY_END; break;
    case ASTRA_KEYMAP_UP:
        event.key = ASTRA_SHELL_KEY_HISTORY_PREVIOUS;
        break;
    case ASTRA_KEYMAP_DOWN: event.key = ASTRA_SHELL_KEY_HISTORY_NEXT; break;
    default:
        if (code == 0u || code > 0x7eu)
            return;
        event.key = ASTRA_SHELL_KEY_CHARACTER;
        event.character = (uint8_t)code;
        break;
    }

    result = astra_shell_editor_input(&shell.editor, event, NULL, NULL);
    if (result == ASTRA_SHELL_SUBMIT) {
        astra_terminal_putc(&shell.terminal, '\n');
        /*
         * One keyboard, and whoever is in the foreground gets it. A line typed
         * while a child runs is that child's input, not the shell's next
         * command -- the alternative is a terminal that runs commands at a
         * program that thought it was being talked to.
         *
         * A source that will not take it keeps the line where it was typed:
         * the editor is not committed, so the person sees it still there and
         * presses return again. Nothing typed is lost by a child that is slow
         * to read.
         */
        if (shell.child != 0u) {
            uint32_t length = shell_strlen(shell.editor.line);

            if (console_stream_offer((const uint8_t *)shell.editor.line,
                                     length) == length) {
                astra_shell_editor_commit(&shell.editor);
            }
            return;
        }
        run_line(shell.editor.line);
        astra_shell_editor_commit(&shell.editor);
        prompt();
        return;
    }
    if (result != ASTRA_SHELL_CHANGED)
        return;

    /*
     * The whole line is reprinted rather than patched. It is the cheapest
     * correct thing while the editor owns the text and the terminal owns the
     * cells, and the damage tracking means only the cells that differ reach
     * the screen anyway.
     */
    astra_terminal_putc(&shell.terminal, '\r');
    prompt();
    astra_terminal_write(&shell.terminal, shell.editor.line);
    astra_terminal_putc(&shell.terminal, ' ');
}

/*
 * One pass of the terminal's loop: serve everything hosted here, then take
 * whatever was typed. Zero means the terminal cannot go on, and the caller
 * stops.
 *
 * It is a function rather than a loop body because it is run from two places
 * -- the prompt, and the wait for a child -- and those two must not drift
 * apart. A serving wait that pumped a subset of what the prompt pumps is a
 * child that works until it calls the one service the wait forgot.
 */
static int pump_once(void)
{
    /*
     * Static, not automatic. A user thread gets one 4 KiB stack, and this
     * function sits at the bottom of every command's call chain -- shell, Kit,
     * service, backend, then lwext4's own write path, which is the deepest
     * thing in the system. A batch buffer here is paid for by every frame
     * above it. Moving it out of the frame is the difference between `write`
     * working and faulting.
     */
    static AstraInputEvent events[ASTRA_INPUT_READ_BATCH_MAX];
    uint32_t count = 0u;
    uint32_t flags = 0u;
    uint32_t status;

    /*
     * The drain, off the critical path of everything except itself. It is
     * here because this is the loop the machine already has: the events
     * service is in this process until there is a launch path, and a
     * bounded pump per pass is what keeps a burst from becoming a stall.
     */
    supervisor_events_pump();
    /*
     * And the streams, for the same reason and on the same terms. A launched
     * program's output arrives here and its input leaves from here, so this
     * call is the whole reason a wait for a child can be a wait at all.
     */
    console_stream_pump();
    if (shell.input == 0u) {
        (void)astra_yield();
        return 1;
    }
    status = astra_input_read(shell.input, events, ASTRA_INPUT_READ_BATCH_MAX,
                              &count, &flags);
    /*
     * An empty queue is the ordinary case and the only one worth yielding
     * over. Anything else is a refused call, and treating a refusal as
     * "no input" is what turned a rejected buffer address into a terminal
     * that looked hung: the loop spun forever and said nothing. A broken
     * input path is exactly as fatal to a terminal as a broken flush, and
     * is reported the same way.
     */
    if (status == ASTRA_SYSCALL_WOULD_BLOCK) {
        (void)astra_yield();
        return 1;
    }
    if (status != ASTRA_SYSCALL_OK) {
        /*
         * The refusal that used to be invisible. A rejected input read
         * looked exactly like an empty queue for a whole session, and the
         * terminal sat there saying nothing at all; now it says which
         * status it was refused with before it goes.
         */
        char report[32];

        (void)astra_log_write(report,
                              describe_input_failure(report, sizeof(report),
                                                     status));
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return 0;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t header = events[index].header;
        uint32_t usage = events[index].value;
        int pressed =
            (ASTRA_INPUT_EVENT_FLAGS(header) & ASTRA_INPUT_FLAG_DOWN) != 0u;

        if (ASTRA_INPUT_EVENT_CLASS(header) != ASTRA_INPUT_CLASS_KEYBOARD)
            continue;
        if (astra_keymap_is_modifier(usage)) {
            shell.modifiers = astra_keymap_apply_modifier(shell.modifiers,
                                                          usage, pressed);
            continue;
        }
        /* Releases move no cursor and type no character. */
        if (!pressed)
            continue;
        feed_key(astra_keymap_translate(usage, shell.modifiers));
    }
    /*
     * Flushed whenever anything might have changed the cells, which is not the
     * same as "a key arrived": a child's output reaches the model through the
     * sink and nobody typed anything at all.
     */
    if (astra_terminal_flush(&shell.terminal) != ASTRA_TERMINAL_OK) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return 0;
    }
    if (count == 0u)
        (void)astra_yield();
    return 1;
}

void console_shell_run(uint32_t display, uint32_t input,
                       int volume_ready)
{
    uint32_t columns = 0u;
    uint32_t rows = 0u;

    if (display == 0u ||
        astra_console_info(display, &columns, &rows) != ASTRA_SYSCALL_OK) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return;
    }

    (void)memset(&shell, 0, sizeof(shell));
    shell.display = display;
    shell.input = input;
    shell.running = 1;
    /*
     * A namespace is granted, so the shell starts wherever it was actually
     * given rather than at a root that does not exist. WORK: first, because a
     * person's files are what a terminal is for; SYS: is the fallback on a
     * volume that would not take a work directory.
     */
    if (astra_assign_lookup(supervisor_assigns(), "WORK") != NULL) {
        (void)memcpy(shell.assign, "WORK", 5u);
    } else if (astra_assign_lookup(supervisor_assigns(), "SYS") != NULL) {
        (void)memcpy(shell.assign, "SYS", 4u);
    }
    if (astra_terminal_init(&shell.terminal, columns, rows, render_cells,
                            &shell) != ASTRA_TERMINAL_OK) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return;
    }
    astra_shell_editor_init(&shell.editor);
    /*
     * The streams a launched program will be granted. They exist before the
     * first prompt because a launch cannot create them -- a child is handed
     * what its launcher already holds -- and a terminal that could not offer
     * one would be a machine where no program can print.
     *
     * A failure here is not fatal to the terminal: the shell's own output does
     * not go through the sink, so what is lost is the ability to launch
     * something that writes, and that is worth booting without.
     */
    if (!console_stream_start(&shell.terminal)) {
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "console streams unavailable, launched programs "
                     "cannot write");
    }

    astra_terminal_clear(&shell.terminal);
    write_line("Astra 68");
    write_line(volume_ready ? "namespace: SYS: read-only, WORK: writable" :
                             "volume: not mounted, file commands will fail");
    command_help();
    prompt();
    if (astra_terminal_flush(&shell.terminal) != ASTRA_TERMINAL_OK) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return;
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_TERMINAL);

    while (shell.running) {
        if (!pump_once()) {
            return;
        }
    }
}
