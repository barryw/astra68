/*
 * The terminal the machine boots into.
 *
 * This is the glue the design conversation called glue: it pumps keys from the
 * input device into the line editor, runs the command the editor hands back,
 * and pushes the resulting cells at the display lease. The parts worth keeping
 * are underneath it -- the cell model, the keymap, the line editor.
 *
 * It names no filesystem implementation. Every command reaches storage
 * through filesystem.library, whether the serving backend is a disk, RAM, or
 * something added later.
 *
 * It also builds no paths. There is no root on this machine: a path is
 * ASSIGN:rest, the shell stands in an assign and a directory under it, and
 * turning a typed word into something the protocol can carry is two Kit calls
 * -- qualify, then resolve. Both are host-tested; this file is glue, and the
 * arrangement is deliberate because glue cannot be tested here.
 */

#include <console_shell.h>
#include <console_stream.h>
#include <loader.h>
#include <volume.h>

#include <astra/bytes.h>
#include <astra/event_control.h>
#include <astra/keymap.h>
#include <astra/runtime.h>
#include <astra/shell.h>
#include <astra/stream.h>
#include <astra/supervisor.h>
#include <astra/syscall.h>
#include <astra/terminal.h>

#include <astra/event_emit.h>
#include <astra/vfs_assign.h>
#include <vfs_host.h>

/* A path the protocol will refuse to carry is not worth building. */
#define SHELL_PATH_MAX ASTRA_VFS_PATH_MAX
#define CONSOLE_INPUT_POLL_NS 10000000ull
#define CONSOLE_PRESENT_NS 16666667ull

/* Commands larger than this belong in application bundles. */
#define SHELL_LOAD_MAX (64u * 1024u)

/*
 * The two numbers every shell answers with when it never got as far as the
 * program. They are conventions rather than anything this kernel defines, and
 * they are here because `$?` is worth nothing if a shell invents its own.
 */
#define SHELL_STATUS_NOT_RUN 126u
#define SHELL_STATUS_NOT_FOUND 127u

typedef struct ConsoleShell {
    AstraTerminal terminal;
    astra_shell_editor_t editor;
    /*
     * The shell's variables, and what a child's environment is built from.
     * `?` is one of them, set after every command, which is why nothing here
     * carries a "last status" field of its own.
     */
    astra_shell_variables_t variables;
    ConsoleShellBackend backend;
    char assign[ASTRA_CAPABILITY_NAME_MAX];  /* the assign it is standing in */
    char directory[SHELL_PATH_MAX];          /* normalised, under that assign */
    /*
     * The child, while there is one. A line typed with this set is that
     * child's input rather than the shell's next command: the terminal has one
     * keyboard, and whoever is in the foreground gets it.
     */
    uint32_t child;
    uint64_t present_deadline;
    int running;
} ConsoleShell;

static ConsoleShell shell;
static uint8_t load_buffer[SHELL_LOAD_MAX];

static uint32_t shell_strlen(const char *text)
{
    uint32_t length = 0u;

    while (text[length] != '\0')
        ++length;
    return length;
}

static uint32_t elapsed_us(uint64_t from, uint64_t to)
{
    return to > from ? (uint32_t)((to - from) / 1000u) : 0u;
}

/*
 * Every line the shell prints, into the machine's own record.
 *
 * The screen used to be readable from outside -- the terminal owned a
 * character plane and a harness could read the cells out of it. It draws
 * glyphs into a window now, so the screen says nothing to anything but an
 * eye, and a machine with no eye on it had no way to show what its shell
 * answered. This is that way: the same text, in the ring the debugger, the
 * gates and a panic report all already read -- at debug level, so it is live
 * commentary and never displaces the machine's own record of what happened.
 */
static void echo_line(void *context, const char *line, uint32_t length)
{
    (void)context;
    (void)astra_log_debug(line, length);
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

static AstraFilesystem *filesystem(void)
{
    return shell.backend.filesystem;
}

static const AstraFilesystemLibraryV1 *filesystem_library(void)
{
    return shell.backend.filesystem_library;
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
 * any client of the storage protocol sees, and the words for them are shared
 * with the programs that have to say the same things.
 */
static void report_status(const char *what, uint32_t status)
{
    const char *text = astra_vfs_status_text(status);

    /*
     * The screen tells the person; the event tells the machine. A refused
     * command used to leave nothing behind at all once the line scrolled off,
     * which is the failure this whole system exists to stop.
     */
    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                 "command refused, status %u", status);
    astra_terminal_write(&shell.terminal, what);
    astra_terminal_write(&shell.terminal, ": ");
    if (text != NULL) {
        astra_terminal_write(&shell.terminal, text);
    } else {
        astra_terminal_write(&shell.terminal, "status ");
        write_number(status);
    }
    astra_terminal_putc(&shell.terminal, '\n');
}

/*
 * `ls`, `cat`, `mkdir` and `rm` were builtins here and are COMMANDS: programs
 * now. They moved out for the reason anything moves out of a shell: a builtin
 * cannot be replaced, cannot be run by anything but the shell carrying it, is
 * not available to a script, and can only ever do what this file happens to
 * know how to do -- `ls` showing mode, owner, link count and time is what that
 * cost, and it would have been a second implementation of a listing to keep in
 * step with the first.
 *
 * `rm` gained something the others did not. A builtin runs holding everything
 * the shell holds, so a builtin `rm` declining to touch a read-only member was
 * the shell being careful; the program holds only what it was granted, so the
 * refusal comes from the member. That is the difference between a rule and a
 * guarantee.
 *
 * `write` stays, for now. It exists because there is no other way to put bytes
 * in a file, and the answer to that is a stream pointed somewhere other than
 * the terminal -- `cat` into a redirect, and eventually an editor -- not a
 * better builtin.
 */

static uint32_t command_cd(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    char wire[SHELL_PATH_MAX];
    char name[ASTRA_CAPABILITY_NAME_MAX];
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;

    /* `cd` alone goes to the assign's root, not to where it already is. */
    status = filesystem_library()->qualify(
        shell.assign, argc > 1 ? shell.directory : "",
        argc > 1 ? argv[1] : NULL, typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = filesystem_library()->stat(filesystem(), typed, &info);
    if (status != ASTRA_VFS_OK) {
        report_status("cd", status);
        return status;
    }
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
        write_line("cd: not a directory");
        return ASTRA_VFS_ERR_NOT_DIR;
    }
    /*
     * Adopted only now, and taken apart by the same parser that resolved it.
     * `wire` is finished with, so it is the scratch the split needs rather
     * than a fourth buffer on a small user stack.
     */
    if (filesystem_library()->path_split(
            typed, name, sizeof(name), wire, sizeof(wire)) != ASTRA_VFS_OK ||
        filesystem_library()->path_normalise(
            wire, shell.directory, sizeof(shell.directory)) != ASTRA_VFS_OK) {
        shell.directory[0] = '\0';
        write_line("cd: path too long");
        return ASTRA_VFS_ERR_LIMIT;
    }
    (void)memcpy(shell.assign, name, sizeof(shell.assign));
    return ASTRA_VFS_OK;
}

/* write NAME TEXT... -- creates or truncates, then writes the rest of the line. */
static uint32_t command_write(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t moved = 0u;
    uint32_t status;
    int index;

    if (argc < 2) {
        write_line("write: needs a name");
        return ASTRA_VFS_ERR_INVALID;
    }
    status = filesystem_library()->qualify(
        shell.assign, shell.directory, argv[1], typed, sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("write", status);
        return status;
    }
    status = filesystem_library()->open(
        filesystem(), typed, ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                                 ASTRA_VFS_OPEN_TRUNCATE,
        &file);
    if (status != ASTRA_VFS_OK) {
        report_status("write", status);
        return status;
    }
    for (index = 2; index < argc; ++index) {
        const char *word = argv[index];
        uint32_t length = shell_strlen(word);
        status = filesystem_library()->write(&file, word, length, &moved);
        if (status != ASTRA_VFS_OK || moved != length) {
            if (status != ASTRA_VFS_OK)
                report_status("write", status);
            else
                write_line("write: stalled");
            goto finish;
        }
        status = filesystem_library()->write(
            &file, index + 1 < argc ? " " : "\n", 1u, &moved);
        if (status != ASTRA_VFS_OK) {
            report_status("write", status);
            goto finish;
        }
    }
finish:
    (void)filesystem_library()->close(&file);
    return status;
}

/*
 * What this shell's namespace actually is: every name, its members in order,
 * and what each member carries.
 *
 * It is a builtin rather than a program because it prints the *shell's*
 * namespace. A launched program holds its own, so a program answering this
 * question would truthfully answer about itself and be read as answering about
 * the prompt.
 *
 * It is read-only. Joining a member at the prompt is a shell language decision
 * the layout spec defers, and nothing needs to rebind at runtime yet.
 */
static uint32_t command_assign(int argc, char *const *argv)
{
    const AstraAssignTable *table = supervisor_assigns();

    (void)argc;
    (void)argv;
    for (uint32_t index = 0u; index < table->count; ++index) {
        const AstraAssign *entry = &table->entries[index];
        uint32_t member = 0u;

        /* Its position among the members of its own name. */
        for (uint32_t at = 0u; at < index; ++at) {
            if (astra_capability_name_equal(table->entries[at].name,
                                            entry->name)) {
                ++member;
            }
        }
        astra_terminal_write(&shell.terminal, entry->name);
        astra_terminal_write(&shell.terminal, ": [");
        write_number(member);
        astra_terminal_write(&shell.terminal, "] ");
        astra_terminal_write(&shell.terminal,
                             (entry->rights & ASTRA_RIGHT_WRITE) != 0u ?
                                 "rw " : "r  ");
        astra_terminal_putc(&shell.terminal, '/');
        astra_terminal_write(&shell.terminal, entry->root);
        astra_terminal_putc(&shell.terminal, '\n');
    }
    return ASTRA_VFS_OK;
}
static void command_help(void)
{
    write_line("builtins: cd [dir], write FILE TEXT..., assign,");
    write_line("          pwd, clear, help");
    write_line("paths are ASSIGN:path -- there is no root. try ls WORK:");
    /*
     * `events` is named here as a file, not a builtin, and that is the whole
     * point of the line. It moved out on the day a program could be launched,
     * and a help text that still listed it as a builtin would be the machine
     * describing itself as it used to be.
     */
    write_line("programs live in COMMANDS:. try ls -l, cat FILE, mkdir DIR,");
    write_line("          rm FILE, status 7, or events");
    write_line("assign shows every name and its members, in the order tried");
    /*
     * Both halves, because a person who knows one and not the other has half a
     * shell: quoting is what makes one argument out of two words, redirection
     * is what makes a file out of what a program said.
     */
    write_line("quote with ' or \" -- date +\"%H %M\" is one argument");
    write_line("redirect a program with > FILE, or >> FILE to keep what is");
    write_line("          there. builtins write to the terminal only");
    /*
     * The status is not printed any more, so where it went has to be said
     * somewhere a person will look. This is that place.
     */
    write_line("NAME=VALUE sets a name; set lists them, set NAME forgets one");
    write_line("$? is the last command's status -- try echo $?");
}

static void prompt(void)
{
    astra_terminal_write(&shell.terminal, shell.assign);
    astra_terminal_write(&shell.terminal, ":");
    astra_terminal_write(&shell.terminal, shell.directory);
    astra_terminal_putc(&shell.terminal, '>');
    astra_terminal_putc(&shell.terminal, ' ');
}

static int pump_once(void);

static uint32_t read_launch_image(const char *path, const uint8_t **image,
                                  uint32_t *length)
{
    uint32_t status = supervisor_vfs_read_borrow(path, image, length);

    if (status != ASTRA_VFS_ERR_LIMIT && status != ASTRA_VFS_ERR_UNSUPPORTED)
        return status;
    status = supervisor_vfs_read(path, load_buffer, sizeof(load_buffer), length);
    if (status == ASTRA_VFS_OK)
        *image = load_buffer;
    return status;
}

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
static uint32_t launch_image(const char *word, const uint8_t **image,
                             uint32_t *length)
{
    static const char *const places[] = {"APPS", "COMMANDS"};
    char typed[SHELL_PATH_MAX];
    uint32_t worst = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t index = 0u; word[index] != '\0'; ++index) {
        if (word[index] == ':') {
            uint32_t status = read_launch_image(word, image, length);

            return status == ASTRA_VFS_OK && *length > SHELL_LOAD_MAX ?
                ASTRA_VFS_ERR_LIMIT : status;
        }
    }
    for (uint32_t place = 0u; place < 2u; ++place) {
        uint32_t status = filesystem_library()->qualify(
            places[place], "", word, typed, sizeof(typed));

        if (status != ASTRA_VFS_OK)
            return status;
        status = read_launch_image(typed, image, length);
        if (status != ASTRA_VFS_OK) {
            if (status != ASTRA_VFS_ERR_NOT_FOUND &&
                worst == ASTRA_VFS_ERR_NOT_FOUND)
                worst = status;
            continue;
        }
        if (*length > SHELL_LOAD_MAX)
            return ASTRA_VFS_ERR_LIMIT;
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_NOTICE,
                     "launching from place %u", place);
        return ASTRA_VFS_OK;
    }
    return worst;
}

/*
 * The grants a launched command is handed: three streams, and a namespace.
 *
 * This uses nine grants -- three streams, WORK, two COMMANDS members, LIBS,
 * EVENTS and EVENT_CONTROL -- worth stating
 * rather than discovering. **`SYS:` is the one left out**, deliberately: a
 * command needs somewhere to read its own data, somewhere to write, and its
 * history, and it does not need the whole volume. The day something does, the
 * ceiling is what has to move, and moving it is a decision rather than an
 * accident.
 *
 * SIGNAL and nothing more, on all of them. A child sends on these and never
 * receives; the reply port a request needs is one the child creates and holds
 * itself. The rights that decide what a child may *do* with a mount are on its
 * own assign table, seeded from these grants -- which is why the namespace
 * entries carry their read and write rights in `rights` as well.
 */
/* Bounded append into a root buffer; false the moment it would not fit. */
static int root_append(char *root, uint32_t capacity, uint32_t *at,
                       const char *text)
{
    while (*text != '\0') {
        if (*at + 1u >= capacity)
            return 0;
        root[(*at)++] = *text++;
    }
    root[*at] = '\0';
    return 1;
}

static uint32_t launch_grants(AstraLaunchGrant *grants)
{
    static const char *const stream_names[] = {"STDOUT", "STDERR", "STDIN"};
    /*
     * PROC: is a mount like any other from here, and that is the point: `ps`
     * reads process state with the protocol `cat` reads a file with, and this
     * shell passes on what it was granted without knowing the supervisor
     * renders it. Read-only, because looking at a process list is not
     * authority to change one -- killing needs process-control authority and
     * does not travel down this path.
     */
    static const char *const mount_names[] = {
        "WORK", "COMMANDS", "LIBS", "EVENTS", "PROC"
    };
    uint32_t streams[3];
    uint32_t count = 0u;
    _Static_assert(sizeof(stream_names) / sizeof(stream_names[0]) ==
                       sizeof(streams) / sizeof(streams[0]),
                   "stream names and handles must stay paired");
    /*
     * Whether either loop below ran out of room before it ran out of
     * grants to make. It is 10 of 10 today -- three streams, WORK, two
     * COMMANDS members, LIBS, EVENTS, PROC and EVENT_CONTROL -- so the next
     * grant added here needs ASTRA_LAUNCH_GRANT_MAX raised with it. Until
     * then a third COMMANDS member would push something out of a child's
     * namespace, which is why the drop is counted and logged rather than
     * silent, the same guard bind_standard_assigns has on the other side.
     */
    int dropped = 0;

    streams[0] = console_stream_stdout();
    streams[1] = console_stream_stderr();
    streams[2] = console_stream_stdin();
    for (uint32_t index = 0u;
         index < sizeof(streams) / sizeof(streams[0]); ++index) {
        if (streams[index] == 0u) {
            continue;
        }
        if (count >= ASTRA_LAUNCH_GRANT_MAX) {
            dropped = 1;
            break;
        }
        astra_capability_name_set(grants[count].name, stream_names[index]);
        grants[count].handle = streams[index];
        grants[count].rights = ASTRA_RIGHT_SIGNAL;
        /* A stream is authority, not a name: STDOUT:file.txt is nonsense. */
        grants[count].flags = 0u;
        ++count;
    }

    /*
     * The namespace, member by member. A name with two members is granted
     * twice, in order, because a member is a repeated name on both sides of a
     * launch -- and a child that was handed only the first member would be a
     * child whose COMMANDS: quietly means less than the prompt's.
     *
     * The root travels now, so a child's COMMANDS: means the directory it was
     * granted rather than the whole volume.
     */
    for (uint32_t index = 0u;
         index < sizeof(mount_names) / sizeof(mount_names[0]); ++index) {
        for (uint32_t member = 0u; ; ++member) {
            const AstraAssign *assign =
                filesystem_library()->assign_member(
                    supervisor_assigns(), mount_names[index], member);
            uint32_t namespace_rights;

            if (assign == NULL) {
                break;
            }
            if (count >= ASTRA_LAUNCH_GRANT_MAX) {
                dropped = 1;
                break;
            }
            astra_capability_name_set(grants[count].name, mount_names[index]);
            grants[count].handle = assign->handle;
            grants[count].rights = ASTRA_RIGHT_SIGNAL;
            namespace_rights = assign->rights;
            if (shell_equal(mount_names[index], "LIBS"))
                namespace_rights &= ~ASTRA_RIGHT_WRITE;
            grants[count].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE |
                ((namespace_rights & ASTRA_RIGHT_READ) != 0u ?
                     ASTRA_CAPABILITY_FLAG_READ : 0u) |
                ((namespace_rights & ASTRA_RIGHT_WRITE) != 0u ?
                     ASTRA_CAPABILITY_FLAG_WRITE : 0u);
            astra_capability_root_set(grants[count].root, assign->root);
            ++count;
        }
    }
    /*
     * Where the prompt is standing, as a name the child can use.
     *
     * A program has no current directory and the machine has no root: a path
     * is ASSIGN:path and nothing else. That was invisible while `cat`, `mkdir`
     * and `rm` were builtins qualifying against `shell.directory` before they
     * touched anything -- and it would have become a silent regression the
     * moment they became programs, because `cat foo` after `cd proto` has
     * nothing to resolve `foo` against. So the shell says where it is, the
     * only way this machine says anything: as a grant. The directory is folded
     * into the root, so CWD: names the place and cannot walk above it.
     */
    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = filesystem_library()->assign_member(
            supervisor_assigns(), shell.assign, member);
        char root[ASTRA_CAPABILITY_ROOT_MAX];
        uint32_t at = 0u;
        uint32_t namespace_rights;

        if (assign == NULL)
            break;
        if (count >= ASTRA_LAUNCH_GRANT_MAX) {
            dropped = 1;
            break;
        }
        /*
         * Composed here rather than trusted to the setter, which truncates.
         * A truncated root is a child pointed at a different directory and
         * told nothing about it; no CWD: at all is a child that says it does
         * not know where it is, which is the answer a person can act on.
         */
        if (!root_append(root, sizeof(root), &at, assign->root) ||
            (shell.directory[0] != '\0' &&
             ((assign->root[0] != '\0' &&
               !root_append(root, sizeof(root), &at, "/")) ||
              !root_append(root, sizeof(root), &at, shell.directory)))) {
            ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL,
                         ASTRA_EVENT_LEVEL_WARNING,
                         "no CWD: granted, the prompt's path is too long");
            break;
        }
        astra_capability_name_set(grants[count].name, "CWD");
        grants[count].handle = assign->handle;
        grants[count].rights = ASTRA_RIGHT_SIGNAL;
        namespace_rights = assign->rights;
        grants[count].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE |
            ((namespace_rights & ASTRA_RIGHT_READ) != 0u ?
                 ASTRA_CAPABILITY_FLAG_READ : 0u) |
            ((namespace_rights & ASTRA_RIGHT_WRITE) != 0u ?
                 ASTRA_CAPABILITY_FLAG_WRITE : 0u);
        astra_capability_root_set(grants[count].root, root);
        ++count;
    }
    if (supervisor_loader_event_control() != 0u) {
        if (count < ASTRA_LAUNCH_GRANT_MAX) {
            astra_capability_name_set(grants[count].name,
                                      ASTRA_CAPABILITY_EVENT_CONTROL);
            grants[count].handle = supervisor_loader_event_control();
            grants[count].rights = ASTRA_RIGHT_SIGNAL;
            grants[count].flags = 0u;
            ++count;
        } else {
            dropped = 1;
        }
    }
    if (dropped) {
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                     "launch grant dropped, ASTRA_LAUNCH_GRANT_MAX reached");
    }
    return count;
}

/*
 * Reads the whole image in, launches it, and serves its terminal streams until
 * it is done. Storage and events run independently in protected processes.
 */
static uint32_t command_launch(const char *word, int argc,
                               char *const *argv)
{
    AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX];
    AstraLaunchArguments arguments;
    const uint8_t *image = NULL;
    uint32_t length = 0u;
    uint32_t handle = 0u;
    uint32_t child_id = 0u;
    uint32_t exit_status = 0u;
    uint32_t status;
    uint64_t started = astra_clock_monotonic();
    uint64_t read;
    uint64_t spawned;

    status = launch_image(word, &image, &length);
    read = astra_clock_monotonic();
    if (status != ASTRA_VFS_OK) {
        /*
         * "Not a command" is what a genuinely absent name says. Anything
         * else -- a member that refused for its own reason, a device that
         * answered ASTRA_VFS_ERR_IO -- is reported for what it is, the same
         * distinction the Filesystem Kit preserves: absence is not the only
         * reason a lookup can fail.
         */
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            astra_terminal_write(&shell.terminal, word);
            write_line(": not a command");
            return SHELL_STATUS_NOT_FOUND;
        }
        report_status(word, status);
        return SHELL_STATUS_NOT_RUN;
    }
    if (astra_launch_arguments_pack(
            &arguments, ASTRA_LAUNCH_SOURCE_SHELL, (uint32_t)argc,
            (const char *const *)argv) != ASTRA_SYSCALL_OK) {
        write_line("too many arguments");
        return SHELL_STATUS_NOT_RUN;
    }

    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "launching, %u bytes of image", length);
    status = astra_launch(image, length, grants,
                          launch_grants(grants), &arguments, &handle,
                          &child_id);
    spawned = astra_clock_monotonic();
    if (status != ASTRA_SYSCALL_OK) {
        /*
         * With the number. "would not start" on its own says nothing a person
         * can act on, and the difference between a grant refused and a
         * malformed image is the whole of what they need to know.
         */
        astra_terminal_write(&shell.terminal, word);
        astra_terminal_write(&shell.terminal, ": would not start, status ");
        write_number(status);
        astra_terminal_putc(&shell.terminal, '\n');
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                     "launch refused, status %u", status);
        return SHELL_STATUS_NOT_RUN;
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
    /*
     * The child's last words before this shell's account of the child. Its
     * exit was noticed by the wait above, which leaves whatever it wrote on
     * the way out still queued on the sink -- so without this the shell's
     * "exited 13" prints above the line that says what 13 meant.
     */
    (void)console_stream_drain();

    /*
     * **The status is answered, not narrated.** A shell that printed
     * "ls: exited 0" after every line was telling a person something they did
     * not ask for and could not use; `$?` is where a status belongs, and it is
     * a value a later command can act on rather than a line on a screen. What
     * a *failing* command has to say, it says itself on STDERR.
     *
     * A child that did not finish leaves no status at all, so `$?` gets the
     * shell's own -- 126, the number every shell uses for "found it, could not
     * run it" -- rather than a stale zero.
     */
    if (status != ASTRA_SYSCALL_OK) {
        astra_terminal_write(&shell.terminal, word);
        write_line(": did not finish");
        exit_status = SHELL_STATUS_NOT_RUN;
    }
    ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "child %u finished with status %u", child_id, exit_status);
    ASTRA_EVENT3(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_NOTICE,
                 "command stages: image %u spawn %u run %u us",
                 elapsed_us(started, read), elapsed_us(read, spawned),
                 elapsed_us(spawned, astra_clock_monotonic()));
    return exit_status;
}

/* What `$?` expands to, after every command, builtin or program alike. */
static void shell_set_status(uint32_t status)
{
    char digits[12];
    uint32_t index = (uint32_t)sizeof(digits) - 1u;

    digits[index] = '\0';
    do {
        digits[--index] = (char)('0' + (status % 10u));
        status /= 10u;
    } while (status != 0u && index != 0u);
    (void)astra_shell_variable_set(&shell.variables, "?", &digits[index]);
}

static const char *shell_value(void *context, const char *name, size_t length)
{
    (void)context;
    return astra_shell_variable_get(&shell.variables, name, length);
}

/*
 * `NAME=VALUE` in one buffer. The parser left it whole because the split is
 * the same one a child's environment makes, and doing it twice is two places
 * to disagree about where a name ends.
 */
static uint32_t shell_assign(char *pair)
{
    char *value = pair;

    while (*value != '\0' && *value != '=')
        ++value;
    if (*value == '\0')
        return ASTRA_VFS_ERR_INVALID;
    *value++ = '\0';
    return astra_shell_variable_set(&shell.variables, pair, value) ==
                   ASTRA_SHELL_OK ?
               ASTRA_VFS_OK :
               ASTRA_VFS_ERR_LIMIT;
}

/*
 * The words this file answers itself, as one list.
 *
 * It was a chain of `shell_equal` until a redirect had to be refused on a
 * builtin, which needs the question asked *before* the answer runs -- and two
 * places naming the same six words is one place for them to drift apart. The
 * type is the shell library's, so a builtin here has the shape a builtin
 * anywhere on this machine has.
 */
static int builtin_help(void *context, int argc, char *const argv[])
{
    (void)context; (void)argc; (void)argv;
    command_help();
    return 0;
}

static int builtin_clear(void *context, int argc, char *const argv[])
{
    (void)context; (void)argc; (void)argv;
    astra_terminal_clear(&shell.terminal);
    return 0;
}

static int builtin_pwd(void *context, int argc, char *const argv[])
{
    (void)context; (void)argc; (void)argv;
    astra_terminal_write(&shell.terminal, shell.assign);
    astra_terminal_putc(&shell.terminal, ':');
    write_line(shell.directory);
    return 0;
}

static int builtin_cd(void *context, int argc, char *const argv[])
{
    (void)context;
    return (int)command_cd(argc, argv);
}

static int builtin_write(void *context, int argc, char *const argv[])
{
    (void)context;
    return (int)command_write(argc, argv);
}

static int builtin_assign(void *context, int argc, char *const argv[])
{
    (void)context;
    return (int)command_assign(argc, argv);
}

/*
 * `set` with no argument lists what the shell holds; `set NAME` forgets one.
 * There is no `set NAME=VALUE`, because `NAME=VALUE` on its own line already
 * is that -- and a second spelling of one thing is a second thing to keep
 * right.
 */
static int builtin_set(void *context, int argc, char *const argv[])
{
    const char *name;
    const char *value;

    (void)context;
    if (argc > 1) {
        for (int index = 1; index < argc; ++index) {
            if (astra_shell_variable_unset(&shell.variables, argv[index]) !=
                ASTRA_SHELL_OK) {
                astra_terminal_write(&shell.terminal, argv[index]);
                write_line(": no such name");
                return (int)ASTRA_VFS_ERR_NOT_FOUND;
            }
        }
        return 0;
    }
    for (size_t index = 0u;
         astra_shell_variable_at(&shell.variables, index, &name, &value);
         ++index) {
        astra_terminal_write(&shell.terminal, name);
        astra_terminal_putc(&shell.terminal, '=');
        astra_terminal_write(&shell.terminal, value);
        /*
         * Which of them a program would see. `?` is the shell's own answer and
         * never crosses a launch, and saying so beside it is cheaper than a
         * person discovering it from a child that did not get it.
         */
        if (!astra_shell_variable_exportable(name))
            astra_terminal_write(&shell.terminal, "   (this shell only)");
        astra_terminal_putc(&shell.terminal, '\n');
    }
    return 0;
}

static const astra_shell_builtin_t shell_builtins[] = {
    {"set", builtin_set},
    {"help", builtin_help},
    {"clear", builtin_clear},
    {"pwd", builtin_pwd},
    {"cd", builtin_cd},
    {"write", builtin_write},
    {"assign", builtin_assign}
};

static const astra_shell_builtin_t *shell_builtin(const char *word)
{
    for (uint32_t index = 0u;
         index < sizeof(shell_builtins) / sizeof(shell_builtins[0]); ++index) {
        if (shell_equal(shell_builtins[index].name, word))
            return &shell_builtins[index];
    }
    return NULL;
}

/*
 * A redirected command's output, on its way into a file.
 *
 * The sink calls `redirect_render` on the loop that pumps everything else, so
 * the write happens on the same thread that renders the terminal -- which is
 * the arrangement every other part of this shell already has. A write that
 * fails is remembered rather than reported: the sink has nowhere to say it,
 * and one report when the command finishes is worth more than one per chunk.
 *
 * The activity is kept, unlike the terminal's sink which drops it. A file is
 * read later by somebody who was not here, and the number is the only way back
 * to the events the command emitted while it wrote.
 */
static struct {
    AstraFile file;
    uint32_t status;     /* the first write that refused */
    int stalled;         /* a write that took less than it was given */
    uint32_t bytes;
    uint32_t activity;
} redirect;

static void redirect_render(void *context, const uint8_t *bytes,
                            uint32_t length, uint32_t activity)
{
    uint32_t moved = 0u;
    uint32_t status;

    (void)context;
    redirect.activity = activity;
    if (redirect.status != ASTRA_VFS_OK || redirect.stalled)
        return;
    status = filesystem_library()->write(&redirect.file, bytes, length, &moved);
    if (status != ASTRA_VFS_OK)
        redirect.status = status;
    else if (moved != length)
        redirect.stalled = 1;
    else
        redirect.bytes += moved;
}

/*
 * Opens the name the line gave and points STDOUT at it. Zero if it could not,
 * having said why -- a redirect that failed is a command that must not run,
 * because a program whose output was meant for a file must never quietly get
 * the screen instead.
 */
static int redirect_begin(const char *name, int append)
{
    static const AstraFile closed = ASTRA_FILE_INIT;
    char typed[SHELL_PATH_MAX];
    uint64_t position = 0u;
    uint32_t status;

    redirect.file = closed;
    redirect.status = ASTRA_VFS_OK;
    redirect.stalled = 0;
    redirect.bytes = 0u;
    redirect.activity = 0u;
    status = filesystem_library()->qualify(
        shell.assign, shell.directory, name, typed, sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status(name, status);
        return 0;
    }
    status = filesystem_library()->open(
        filesystem(), typed,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
            (append ? 0u : ASTRA_VFS_OPEN_TRUNCATE),
        &redirect.file);
    if (status != ASTRA_VFS_OK) {
        report_status(name, status);
        return 0;
    }
    /*
     * `>>` is a seek and not an open flag. The protocol has no append bit, and
     * adding one to carry what a seek to the end already says would be a
     * second way to say one thing -- and the one that races, because a flag is
     * evaluated once at open and a seek is where the writing starts.
     */
    if (append) {
        status = filesystem_library()->seek(
            &redirect.file, 0, ASTRA_FILE_SEEK_END, &position);
        if (status != ASTRA_VFS_OK) {
            (void)filesystem_library()->close(&redirect.file);
            report_status(name, status);
            return 0;
        }
    }
    if (!console_stream_redirect(redirect_render, NULL)) {
        (void)filesystem_library()->close(&redirect.file);
        write_line("redirect: no stream to point");
        return 0;
    }
    return 1;
}

/* Puts STDOUT back, closes the file, and reports a write that did not land. */
static void redirect_end(const char *name)
{
    (void)console_stream_redirect(NULL, NULL);
    (void)filesystem_library()->close(&redirect.file);
    ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "redirect wrote %u bytes for activity %u", redirect.bytes,
                 redirect.activity);
    if (redirect.status != ASTRA_VFS_OK)
        report_status(name, redirect.status);
    else if (redirect.stalled)
        write_line("redirect: the file took less than was written");
}

static void run_line(const char *line)
{
    astra_shell_words_t words;
    astra_shell_result_t parsed;
    const astra_shell_builtin_t *builtin;
    uint32_t status = ASTRA_VFS_OK;

    parsed = astra_shell_parse(line, &words, shell_value, NULL);
    /*
     * Said, rather than swallowed. A line the parser refused used to leave no
     * mark at all, so an unclosed quote looked exactly like a machine that had
     * stopped taking input.
     */
    if (parsed == ASTRA_SHELL_ERR_SYNTAX) {
        write_line("syntax: close the quote, and give a redirect one name");
        shell_set_status(SHELL_STATUS_NOT_RUN);
        return;
    }
    if (parsed != ASTRA_SHELL_OK)
        return;
    if (words.argc == 0 && words.assignments == 0)
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

    /*
     * Assignments with nothing after them are this shell's own. Assignments
     * *ahead of a command* are that command's environment and never this
     * shell's, which is what `TZ=... date` has to mean: the difference is the
     * whole reason a shell has the form at all.
     */
    if (words.argc == 0) {
        for (int index = 0; index < words.assignments; ++index) {
            status = shell_assign(words.assignment[index]);
            if (status != ASTRA_VFS_OK) {
                report_status(words.assignment[index], status);
                break;
            }
        }
        shell_set_status(status);
        return;
    }

    /*
     * ponytail: a child's environment is not carried yet. The startup block
     * has `environment_count` and `environment_address` and the kernel fills
     * neither, so `TZ=... date` is refused rather than run with the
     * assignment quietly dropped -- a command that ran without the name it was
     * given is worse than one that did not run.
     */
    if (words.assignments != 0) {
        write_line("a command cannot be handed an environment yet -- set the "
                   "name on its own line");
        shell_set_status(SHELL_STATUS_NOT_RUN);
        return;
    }

    builtin = shell_builtin(words.argv[0]);
    /*
     * A builtin writes to the terminal directly and has no stream to move and
     * no environment to be handed, so a redirect or an assignment on one is
     * refused before it runs rather than ignored after. The builtins left here
     * are on their way out for exactly this reason: `ls > out.txt` works
     * because `ls` is a program.
     */
    if (builtin != NULL && words.redirect != NULL) {
        astra_terminal_write(&shell.terminal, words.argv[0]);
        write_line(": a builtin writes to the terminal and cannot be redirected");
        status = SHELL_STATUS_NOT_RUN;
    } else if (builtin != NULL)
        status = (uint32_t)builtin->function(NULL, words.argc, words.argv);
    else if (words.redirect == NULL)
        /*
         * Not a builtin, so it is a program. There is no third case: a word
         * the machine does not recognise is a file it has not got, and saying
         * that is the whole of the answer.
         */
        status = command_launch(words.argv[0], words.argc, words.argv);
    else if (redirect_begin(words.redirect, words.redirect_append)) {
        /*
         * The order is the whole of it: STDOUT is pointed at the file before
         * the launch, because the grant is read there, and it is put back only
         * after `command_launch` has drained what the child left queued.
         */
        status = command_launch(words.argv[0], words.argc, words.argv);
        redirect_end(words.redirect);
    } else
        status = SHELL_STATUS_NOT_RUN;
    shell_set_status(status);
}

static int flush_terminal(void)
{
    if (astra_terminal_flush(&shell.terminal) != ASTRA_TERMINAL_OK)
        return 0;
    return shell.backend.present == NULL ||
           shell.backend.present(shell.backend.context, &shell.terminal);
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
        /* Show that Enter was accepted before storage or a child can delay us. */
        if (!flush_terminal())
            return;
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
            /*
             * With the newline the person pressed. Without it an empty line
             * offers nothing at all, so a child waiting for input would never
             * see that return was pressed -- and a child reading a line could
             * not tell where it ended. The newline is the line.
             */
            static uint8_t line[ASTRA_STREAM_WRITE_MAX];
            uint32_t length = shell_strlen(shell.editor.line);

            if (length > ASTRA_STREAM_WRITE_MAX - 1u) {
                length = ASTRA_STREAM_WRITE_MAX - 1u;
            }
            (void)memcpy(line, shell.editor.line, length);
            line[length++] = '\n';
            if (console_stream_offer(line, length) == length) {
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
    /* Erase the old tail, then return to the editor's actual insertion point. */
    astra_terminal_putc(&shell.terminal, ' ');
    for (size_t index = shell.editor.cursor; index <= shell.editor.length;
         ++index)
        astra_terminal_putc(&shell.terminal, '\b');
}

/*
 * One pass of the terminal's loop: serve its streams, then take whatever was
 * typed. Zero means the terminal cannot go on, and the caller stops.
 *
 * It is a function rather than a loop body because it is run from two places
 * -- the prompt, and the wait for a child -- and those two must not drift
 * apart. A serving wait that pumped a subset of what the prompt pumps is a
 * child that works until it calls the one service the wait forgot.
 */
static int pump_once(void)
{
    uint32_t key = 0u;
    uint32_t rendered;
    int input_result;
    int had_key = 0;

    /*
     * The streams remain hosted by the terminal. A launched
     * program's output arrives here and its input leaves from here, so this
     * call is the whole reason a wait for a child can be a wait at all.
     */
    supervisor_loader_pump_event_control();
    rendered = console_stream_pump();
    /*
     * A machine with no keyboard still has a screen, and a child still writes
     * to it. This used to return here, which meant the flush at the bottom --
     * the only thing that paints -- was reachable only by way of a keystroke.
     */
    do {
        input_result = shell.backend.next_key != NULL ?
            shell.backend.next_key(shell.backend.context, &key) : 0;
        if (input_result > 0) {
            had_key = 1;
            feed_key(key);
        }
    } while (input_result > 0);
    if (input_result == CONSOLE_SHELL_INPUT_STOP) {
        shell.running = 0;
        return 1;
    }
    if (input_result < 0) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return 0;
    }
    /*
     * Flushed whenever anything might have changed the cells, which is not the
     * same as "a key arrived": a child's output reaches the model through the
     * sink and nobody typed anything at all.
     */
    {
        uint64_t now = astra_clock_monotonic();

        if (rendered != 0u && shell.child != 0u &&
            shell.present_deadline == 0u)
            shell.present_deadline = now + CONSOLE_PRESENT_NS;
        if (shell.present_deadline == 0u || shell.child == 0u || had_key ||
            now >= shell.present_deadline) {
            if (!flush_terminal()) {
                (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
                return 0;
            }
            shell.present_deadline = 0u;
        }
    }
    if (!had_key) {
        uint32_t waits[4];
        uint32_t wait_count = 0u;
        uint32_t sink = console_stream_wait_handle();
        uint32_t redirected = console_stream_redirect_wait_handle();

        if (sink != 0u)
            waits[wait_count++] = sink;
        /*
         * And the file's port, while a child is writing into one. STDERR still
         * arrives on the terminal's sink, so this is a second port and not the
         * same one moved -- and a wait that named only the first would sleep
         * through a child that had filled the second and stopped.
         */
        if (redirected != 0u)
            waits[wait_count++] = redirected;
        if (shell.child != 0u)
            waits[wait_count++] = shell.child;
        if (shell.backend.wait_handle != 0u)
            waits[wait_count++] = shell.backend.wait_handle;
        if (wait_count != 0u) {
            uint64_t deadline = shell.backend.idle_poll_ns != 0u ?
                astra_clock_monotonic() + shell.backend.idle_poll_ns :
                (shell.backend.wait_handle != 0u ?
                     ASTRA_DEADLINE_FOREVER :
                     astra_clock_monotonic() + CONSOLE_INPUT_POLL_NS);
            if (shell.present_deadline != 0u &&
                shell.present_deadline < deadline)
                deadline = shell.present_deadline;
            uint32_t wait_status = astra_wait_multiple(
                waits, wait_count, deadline, NULL, NULL);

            if (wait_status != ASTRA_SYSCALL_OK &&
                wait_status != ASTRA_SYSCALL_TIMED_OUT) {
                ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL,
                             ASTRA_EVENT_LEVEL_WARNING,
                             "terminal wait refused, status %u", wait_status);
                return 0;
            }
        } else {
            /* ponytail: only the degraded no-stream boot still polls. */
            (void)astra_yield();
        }
    }
    return 1;
}

void console_shell_run_backend(const ConsoleShellBackend *backend,
                               int volume_ready)
{
    if (backend == NULL || backend->columns == 0u || backend->rows == 0u ||
        backend->render == NULL || backend->filesystem == NULL ||
        backend->filesystem_library == NULL) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return;
    }

    (void)memset(&shell, 0, sizeof(shell));
    shell.backend = *backend;
    shell.running = 1;
    /*
     * A namespace is granted, so the shell starts wherever it was actually
     * given rather than at a root that does not exist. WORK: first, because a
     * person's files are what a terminal is for; SYS: is the fallback on a
     * volume that would not take a work directory.
     */
    if (filesystem_library()->assign_lookup(supervisor_assigns(), "WORK") !=
        NULL) {
        (void)memcpy(shell.assign, "WORK", 5u);
    } else if (filesystem_library()->assign_lookup(
                   supervisor_assigns(), "SYS") != NULL) {
        (void)memcpy(shell.assign, "SYS", 4u);
    }
    if (astra_terminal_init(&shell.terminal, backend->columns, backend->rows,
                            backend->render, backend->context) !=
        ASTRA_TERMINAL_OK) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return;
    }
    astra_terminal_set_scroll(&shell.terminal, backend->scroll);
    astra_terminal_set_echo(&shell.terminal, echo_line, NULL);
    astra_shell_editor_init(&shell.editor);
    astra_shell_variables_init(&shell.variables);
    /*
     * `?` exists from the first prompt. A shell whose `$?` was empty until
     * something had run would make `echo $?` a different thing on line one
     * than on line two, and nothing about a fresh shell has failed.
     */
    (void)astra_shell_variable_set(&shell.variables, "?", "0");
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
    write_line(volume_ready ? "namespace: WORK: writable" :
                             "volume: not mounted, file commands will fail");
    command_help();
    prompt();
    if (!flush_terminal()) {
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
