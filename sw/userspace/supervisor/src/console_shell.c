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
#define SHELL_READ_CHUNK 128u
#define CONSOLE_INPUT_POLL_NS 10000000ull

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
    ConsoleShellBackend backend;
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
 * any client of the storage protocol sees.
 */
static void report_status(const char *what, uint32_t status)
{
    static const char *const text[] = {
        "ok", "protocol error", "not found", "already exists",
        "not a directory", "is a directory", "access denied", "no space",
        "invalid", "bad handle", "limit reached", "I/O error", "not empty",
        "unsupported", "busy", "buffer too small",
        /*
         * 16, and the first status here that a transport produces rather than
         * a filesystem. "not found" is a volume answering; this is nobody
         * answering, and a person needs to be able to tell those apart at the
         * prompt because only one of them is worth retrying.
         */
        "the service is gone"
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
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[ASTRA_FILESYSTEM_DIRECTORY_BATCH_MAX];
    char typed[SHELL_PATH_MAX];
    uint32_t shown = 0u;
    uint32_t status;

    status = filesystem_library()->qualify(
        shell.assign, shell.directory, argc > 1 ? argv[1] : NULL, typed,
        sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("ls", status);
        return;
    }
    status = filesystem_library()->directory_open(filesystem(), typed,
                                                  &directory);
    if (status != ASTRA_VFS_OK) {
        report_status("ls", status);
        return;
    }
    for (;;) {
        uint32_t count = 0u;

        status = filesystem_library()->directory_read(
            &directory, entries,
            (uint32_t)(sizeof(entries) / sizeof(entries[0])), &count);
        if (status != ASTRA_VFS_OK || count == 0u)
            break;
        for (uint32_t entry = 0u; entry < count; ++entry) {
            astra_terminal_write(&shell.terminal, entries[entry].name);
            if (entries[entry].kind == ASTRA_VFS_KIND_DIRECTORY)
                astra_terminal_putc(&shell.terminal, '/');
            astra_terminal_write(&shell.terminal, "  [");
            write_number(entries[entry].member);
            astra_terminal_write(&shell.terminal, "]\n");
            ++shown;
        }
    }
    filesystem_library()->directory_close(&directory);
    if (status != ASTRA_VFS_OK) {
        report_status("ls", status);
        return;
    }
    if (shown == 0u)
        write_line("(empty)");
}

static void command_cd(int argc, char *const *argv)
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
        return;
    }
    if (info.kind != ASTRA_VFS_KIND_DIRECTORY) {
        write_line("cd: not a directory");
        return;
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
        return;
    }
    (void)memcpy(shell.assign, name, sizeof(shell.assign));
}

static void command_cat(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    uint8_t chunk[SHELL_READ_CHUNK];
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t moved = 0u;
    uint32_t status;

    if (argc < 2) {
        write_line("cat: needs a file");
        return;
    }
    status = filesystem_library()->qualify(
        shell.assign, shell.directory, argv[1], typed, sizeof(typed));
    if (status == ASTRA_VFS_OK)
        status = filesystem_library()->open(
            filesystem(), typed, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) {
        report_status("cat", status);
        return;
    }
    for (;;) {
        status = filesystem_library()->read(&file, chunk, sizeof(chunk),
                                            &moved);
        if (status != ASTRA_VFS_OK) {
            report_status("cat", status);
            break;
        }
        /* A short read is normal: one message carries a bounded payload. */
        if (moved == 0u)
            break;
        astra_terminal_write_bytes(&shell.terminal, chunk, moved);
    }
    (void)filesystem_library()->close(&file);
    astra_terminal_putc(&shell.terminal, '\n');
}

static void command_mkdir(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    uint32_t status;

    if (argc < 2) {
        write_line("mkdir: needs a name");
        return;
    }
    status = filesystem_library()->qualify(
        shell.assign, shell.directory, argv[1], typed, sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("mkdir", status);
        return;
    }
    status = filesystem_library()->mkdir(filesystem(), typed);
    if (status != ASTRA_VFS_OK)
        report_status("mkdir", status);
}

/* write NAME TEXT... -- creates or truncates, then writes the rest of the line. */
static void command_write(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t moved = 0u;
    uint32_t status;
    int index;

    if (argc < 2) {
        write_line("write: needs a name");
        return;
    }
    status = filesystem_library()->qualify(
        shell.assign, shell.directory, argv[1], typed, sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("write", status);
        return;
    }
    status = filesystem_library()->open(
        filesystem(), typed, ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                                 ASTRA_VFS_OPEN_TRUNCATE,
        &file);
    if (status != ASTRA_VFS_OK) {
        report_status("write", status);
        return;
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
}

static void command_rm(int argc, char *const *argv)
{
    char typed[SHELL_PATH_MAX];
    uint32_t status;

    if (argc < 2) {
        write_line("rm: needs a name");
        return;
    }
    status = filesystem_library()->qualify(
        shell.assign, shell.directory, argv[1], typed, sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("rm", status);
        return;
    }
    status = filesystem_library()->unlink(filesystem(), typed);
    if (status != ASTRA_VFS_OK)
        report_status("rm", status);
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
static void command_assign(int argc, char *const *argv)
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
}

static void command_help(void)
{
    write_line("builtins: ls [dir], cd [dir], cat FILE, mkdir DIR, assign,");
    write_line("          write FILE TEXT..., rm FILE, pwd, clear, help");
    write_line("paths are ASSIGN:path -- there is no root. try ls WORK:");
    /*
     * `events` is named here as a file, not a builtin, and that is the whole
     * point of the line. It moved out on the day a program could be launched,
     * and a help text that still listed it as a builtin would be the machine
     * describing itself as it used to be.
     */
    write_line("programs live in COMMANDS:. try status 7, or events");
    write_line("assign shows every name and its members, in the order tried");
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
static uint32_t launch_path(const char *word, AstraFile *file,
                            AstraFileInfo *info)
{
    static const char *const places[] = {"APPS", "COMMANDS"};
    char typed[SHELL_PATH_MAX];
    uint32_t worst = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t index = 0u; word[index] != '\0'; ++index) {
        if (word[index] == ':') {
            uint32_t status = filesystem_library()->open(
                filesystem(), word, ASTRA_VFS_OPEN_READ, file);

            if (status != ASTRA_VFS_OK)
                return status;
            status = filesystem_library()->file_info(file, info);
            if (status == ASTRA_VFS_OK &&
                info->kind != ASTRA_VFS_KIND_DIRECTORY)
                return ASTRA_VFS_OK;
            (void)filesystem_library()->close(file);
            return status == ASTRA_VFS_OK ? ASTRA_VFS_ERR_NOT_FOUND : status;
        }
    }
    for (uint32_t place = 0u; place < 2u; ++place) {
        uint32_t status = filesystem_library()->qualify(
            places[place], "", word, typed, sizeof(typed));

        if (status != ASTRA_VFS_OK)
            return status;
        status = filesystem_library()->open(
            filesystem(), typed, ASTRA_VFS_OPEN_READ, file);
        if (status != ASTRA_VFS_OK) {
            if (status != ASTRA_VFS_ERR_NOT_FOUND &&
                worst == ASTRA_VFS_ERR_NOT_FOUND)
                worst = status;
            continue;
        }
        status = filesystem_library()->file_info(file, info);
        if (status == ASTRA_VFS_OK && info->kind != ASTRA_VFS_KIND_DIRECTORY) {
            ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL,
                         ASTRA_EVENT_LEVEL_NOTICE,
                         "launching from place %u member %u", place,
                         info->member);
            return ASTRA_VFS_OK;
        }
        (void)filesystem_library()->close(file);
        if (status != ASTRA_VFS_OK && worst == ASTRA_VFS_ERR_NOT_FOUND)
            worst = status;
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
static uint32_t launch_grants(AstraLaunchGrant *grants)
{
    static const char *const stream_names[] = {"STDOUT", "STDERR", "STDIN"};
    static const char *const mount_names[] = {
        "WORK", "COMMANDS", "LIBS", "EVENTS"
    };
    uint32_t streams[3];
    uint32_t count = 0u;
    _Static_assert(sizeof(stream_names) / sizeof(stream_names[0]) ==
                       sizeof(streams) / sizeof(streams[0]),
                   "stream names and handles must stay paired");
    /*
     * Whether either loop below ran out of room before it ran out of
     * grants to make. It is provably 9 of 10 today -- three streams, WORK,
     * two COMMANDS members, LIBS, EVENTS and EVENT_CONTROL -- but a third
     * COMMANDS member, or a fourth stream, would push something out of a
     * child's namespace with nothing recorded, the same silence
     * bind_standard_assigns already guards against on the supervisor's side.
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
static void command_launch(const char *word, int argc, char *const *argv)
{
    AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX];
    AstraLaunchArguments arguments;
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t length = 0u;
    uint32_t handle = 0u;
    uint32_t child_id = 0u;
    uint32_t exit_status = 0u;
    uint32_t status;

    status = launch_path(word, &file, &info);
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
        } else {
            report_status(word, status);
        }
        return;
    }
    if (info.byte_size > SHELL_LOAD_MAX) {
        (void)filesystem_library()->close(&file);
        report_status(word, ASTRA_VFS_ERR_LIMIT);
        return;
    }
    status = filesystem_library()->read(
        &file, load_buffer, (uint32_t)info.byte_size, &length);
    (void)filesystem_library()->close(&file);
    if (status != ASTRA_VFS_OK || length != info.byte_size || length == 0u) {
        /*
         * With how far it got. A read that fails partway through an image says
         * something quite different from one that fails at the first byte, and
         * the status alone cannot tell them apart.
         */
        ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                     "image read stopped at %u bytes, status %u", length,
                     status);
        astra_terminal_write(&shell.terminal, word);
        astra_terminal_write(&shell.terminal, ": read stopped at ");
        write_number(length);
        astra_terminal_write(&shell.terminal, " of ");
        write_number((uint32_t)info.byte_size);
        /*
         * And what the device said, because a generic I/O status does not say
         * whether retrying is useful.
         */
        astra_terminal_write(&shell.terminal, ", device ");
        write_number(supervisor_volume_device_status());
        astra_terminal_write(&shell.terminal, "/");
        write_number(supervisor_volume_device_failure());
        astra_terminal_putc(&shell.terminal, '\n');
        report_status(word, status != ASTRA_VFS_OK ? status :
                                                     ASTRA_VFS_ERR_INVALID);
        return;
    }
    if (astra_launch_arguments_pack(
            &arguments, ASTRA_LAUNCH_SOURCE_SHELL, (uint32_t)argc,
            (const char *const *)argv) != ASTRA_SYSCALL_OK) {
        write_line("too many arguments");
        return;
    }

    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "launching, %u bytes of image", length);
    status = astra_launch(load_buffer, length, grants,
                          launch_grants(grants), &arguments, &handle,
                          &child_id);
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
    /*
     * The child's last words before this shell's account of the child. Its
     * exit was noticed by the wait above, which leaves whatever it wrote on
     * the way out still queued on the sink -- so without this the shell's
     * "exited 13" prints above the line that says what 13 meant.
     */
    (void)console_stream_drain();

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
    else if (shell_equal(words.argv[0], "assign"))
        command_assign(words.argc, words.argv);
    else
        /*
         * Not a builtin, so it is a program. There is no third case: a word
         * the machine does not recognise is a file it has not got, and saying
         * that is the whole of the answer.
         */
        command_launch(words.argv[0], words.argc, words.argv);
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
    int input_result;
    int had_key = 0;

    /*
     * The streams remain hosted by the terminal. A launched
     * program's output arrives here and its input leaves from here, so this
     * call is the whole reason a wait for a child can be a wait at all.
     */
    supervisor_loader_pump_event_control();
    console_stream_pump();
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
    if (!flush_terminal()) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return 0;
    }
    if (!had_key) {
        uint32_t waits[3];
        uint32_t wait_count = 0u;
        uint32_t sink = console_stream_wait_handle();

        if (sink != 0u)
            waits[wait_count++] = sink;
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
