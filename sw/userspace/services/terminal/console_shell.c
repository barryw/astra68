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

#include <astra/bytes.h>
#include <astra/config_document.h>
#include <astra/event_control.h>
#include <astra/keymap.h>
#include <astra/network.h>
#include <astra/ntp.h>
#include <astra/runtime.h>
#include <astra/shell.h>
#include <astra/shell_service.h>
#include <astra/stream.h>
#include <astra/syscall.h>
#include <astra/terminal.h>

#include <astra/event_emit.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_process.h>
#include <astra/vfs_reader.h>

/* A path the protocol will refuse to carry is not worth building. */
#define SHELL_PATH_MAX ASTRA_VFS_PATH_MAX
#define CONSOLE_INPUT_POLL_NS 10000000ull
#define CONSOLE_PRESENT_NS 16666667ull

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
    char launch_arguments[ASTRA_SHELL_LINE_CAPACITY];
    char launch_environment[ASTRA_LAUNCH_ENVIRONMENT_BYTES];
    const char *launch_environment_names[ASTRA_LAUNCH_ENVIRONMENT_MAX];
    const char *launch_environment_values[ASTRA_LAUNCH_ENVIRONMENT_MAX];
    ConsoleShellBackend backend;
    char assign[ASTRA_CAPABILITY_NAME_MAX];  /* the assign it is standing in */
    char directory[SHELL_PATH_MAX];          /* normalised, under that assign */
    /*
     * The child, while there is one. A line typed with this set is that
     * child's input rather than the shell's next command: the terminal has one
     * keyboard, and whoever is in the foreground gets it.
     */
    uint32_t child;
    uint32_t command_receive;
    uint32_t command_send;
    uint32_t launch_streams[3];
    uint32_t pending_key;
    uint8_t pending_key_valid;
    uint8_t launch_stream_override;
    uint64_t present_deadline;
    int running;
} ConsoleShell;

static ConsoleShell shell;

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

static AstraFilesystem *filesystem(void)
{
    return &shell.backend.process_filesystem->filesystem;
}

static const AstraFilesystemLibraryV1 *filesystem_library(void)
{
    return shell.backend.process_filesystem->library;
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

static void write_hex32(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    astra_terminal_write(&shell.terminal, "0x");
    for (uint32_t shift = 32u; shift != 0u; shift -= 4u)
        astra_terminal_putc(&shell.terminal,
                           digits[(value >> (shift - 4u)) & 0x0fu]);
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
        astra_terminal_write(&shell.terminal, "operation failed");
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
        uint32_t length = (uint32_t)strlen(word);
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
    const AstraAssignTable *table = astra_process_vfs_assigns();

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
static uint32_t open_launch_source(const char *word,
                                   AstraVfsReadSource *source)
{
    static const char *const places[] = {"APPS", "COMMANDS"};
    char typed[SHELL_PATH_MAX];
    uint32_t worst = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t index = 0u; word[index] != '\0'; ++index) {
        if (word[index] == ':') {
            return astra_vfs_read_source_open(
                source, astra_process_vfs_assigns(), word,
                astra_process_vfs_assign_client, NULL);
        }
    }
    for (uint32_t place = 0u; place < 2u; ++place) {
        uint32_t status = filesystem_library()->qualify(
            places[place], "", word, typed, sizeof(typed));

        if (status != ASTRA_VFS_OK)
            return status;
        status = astra_vfs_read_source_open(
            source, astra_process_vfs_assigns(), typed,
            astra_process_vfs_assign_client, NULL);
        if (status != ASTRA_VFS_OK) {
            if (status != ASTRA_VFS_ERR_NOT_FOUND &&
                worst == ASTRA_VFS_ERR_NOT_FOUND)
                worst = status;
            continue;
        }
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_NOTICE,
                     "launching from place %u", place);
        return ASTRA_VFS_OK;
    }
    return worst;
}

/*
 * The grants a launched command is handed: three streams, its namespace, and
 * the delegable service authorities the terminal itself was granted. `SYS:`
 * remains deliberately absent: a command gets its work, commands, libraries,
 * history and process view, not the whole system volume.
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

static const char *command_owner(const char *command)
{
    const char *owner = command;

    for (const char *at = command; *at != '\0'; ++at)
        if (*at == ':' || *at == '/')
            owner = at + 1u;
    return owner;
}

static uint32_t launch_grants(AstraLaunchGrant *grants, const char *command)
{
    static const char *const stream_names[] = {"STDOUT", "STDERR", "STDIN"};
    static const char *const authority_names[] = {
        ASTRA_CAPABILITY_EVENT_CONTROL,
        ASTRA_CAPABILITY_NETWORK,
        ASTRA_CAPABILITY_NETWORK_LISTEN,
        ASTRA_CAPABILITY_NTP,
    };
    /*
     * PROC: is a mount like any other from here, and that is the point: `ps`
     * reads process state with the protocol `cat` reads a file with, and this
     * shell passes on what it was granted without knowing the supervisor
     * renders it. Read-only, because looking at a process list is not
     * authority to change one -- killing needs process-control authority and
     * does not travel down this path.
     */
    static const char *const mount_names[] = {
        "WORK", "COMMANDS", "LIBS", "EVENTS", "PROC",
        ASTRA_CONFIG_COMMANDS_CAPABILITY
    };
    uint32_t streams[3];
    uint32_t count = 0u;
    _Static_assert(sizeof(stream_names) / sizeof(stream_names[0]) ==
                       sizeof(streams) / sizeof(streams[0]),
                   "stream names and handles must stay paired");
    /* The startup page is the physical bound. Never silently lose authority
     * when its actual mix of namespace members, argv and environment fills it. */
    int dropped = 0;

    streams[0] = shell.launch_stream_override ? shell.launch_streams[0] :
                                                console_stream_stdout();
    streams[1] = shell.launch_stream_override ? shell.launch_streams[1] :
                                                console_stream_stderr();
    streams[2] = shell.launch_stream_override ? shell.launch_streams[2] :
                                                console_stream_stdin();
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
        grants[count].rights = ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_TRANSFER;
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
                    astra_process_vfs_assigns(), mount_names[index], member);
            uint32_t namespace_rights;

            if (assign == NULL) {
                break;
            }
            if (count >= ASTRA_LAUNCH_GRANT_MAX) {
                dropped = 1;
                break;
            }
            astra_capability_name_set(
                grants[count].name,
                strcmp(mount_names[index],
                       ASTRA_CONFIG_COMMANDS_CAPABILITY) == 0 ?
                    ASTRA_CONFIG_CAPABILITY : mount_names[index]);
            grants[count].handle = assign->handle;
            grants[count].rights = ASTRA_RIGHT_SIGNAL;
            namespace_rights = assign->rights;
            if (strcmp(mount_names[index], "LIBS") == 0)
                namespace_rights &= ~ASTRA_RIGHT_WRITE;
            grants[count].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE |
                ((namespace_rights & ASTRA_RIGHT_READ) != 0u ?
                     ASTRA_CAPABILITY_FLAG_READ : 0u) |
                ((namespace_rights & ASTRA_RIGHT_WRITE) != 0u ?
                     ASTRA_CAPABILITY_FLAG_WRITE : 0u);
            if (strcmp(mount_names[index],
                       ASTRA_CONFIG_COMMANDS_CAPABILITY) == 0) {
                char root[ASTRA_CAPABILITY_ROOT_MAX];

                if (astra_config_owner_root(
                        assign->root, command_owner(command), root,
                        sizeof(root)) !=
                    ASTRA_CONFIG_OK)
                    continue;
                astra_capability_root_set(grants[count].root, root);
            } else {
                astra_capability_root_set(grants[count].root, assign->root);
            }
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
            astra_process_vfs_assigns(), shell.assign, member);
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
    if (shell.command_send != 0u) {
        if (count < ASTRA_LAUNCH_GRANT_MAX) {
            astra_capability_name_set(grants[count].name,
                                      ASTRA_CAPABILITY_SHELL);
            grants[count].handle = shell.command_send;
            grants[count].rights = ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_TRANSFER;
            grants[count].flags = 0u;
            ++count;
        } else {
            dropped = 1;
        }
    }
    for (uint32_t index = 0u;
         index < sizeof(authority_names) / sizeof(authority_names[0]);
         ++index) {
        const AstraStartupCapability *held = astra_startup_capability(
            shell.backend.startup, authority_names[index]);

        if (held == NULL || (held->rights & ASTRA_RIGHT_SIGNAL) == 0u)
            continue;
        if (count >= ASTRA_LAUNCH_GRANT_MAX) {
            dropped = 1;
            break;
        }
        astra_capability_name_set(grants[count].name, authority_names[index]);
        grants[count].handle = held->handle;
        grants[count].rights = ASTRA_RIGHT_SIGNAL;
        grants[count].flags = 0u;
        ++count;
    }
    if (dropped) {
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                     "launch grant dropped, ASTRA_LAUNCH_GRANT_MAX reached");
    }
    return count;
}

/* Packs the shell's exported variables into the child's startup block. */
static uint32_t pack_launch_environment(AstraLaunchArguments *arguments,
                                        astra_shell_words_t *words)
{
    uint32_t count = 0u;

    for (size_t index = 0u; ; ++index) {
        const char *name;
        const char *value;

        if (!astra_shell_variable_at(&shell.variables, index, &name, &value))
            break;
        if (!astra_shell_variable_exportable(name))
            continue;
        if (count >= ASTRA_LAUNCH_ENVIRONMENT_MAX)
            return ASTRA_SYSCALL_RESOURCE_LIMIT;
        shell.launch_environment_names[count] = name;
        shell.launch_environment_values[count] = value;
        ++count;
    }
    for (int index = 0; index < words->assignments; ++index) {
        char *name = words->assignment[index];
        char *value = name;
        uint32_t found = count;

        while (*value != '\0' && *value != '=')
            ++value;
        if (*value == '\0')
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        *value++ = '\0';
        if (!astra_shell_variable_exportable(name))
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        for (uint32_t at = 0u; at < count; ++at)
            if (strcmp(shell.launch_environment_names[at], name) == 0) {
                found = at;
                break;
            }
        if (found == count) {
            if (count >= ASTRA_LAUNCH_ENVIRONMENT_MAX)
                return ASTRA_SYSCALL_RESOURCE_LIMIT;
            ++count;
        }
        shell.launch_environment_names[found] = name;
        shell.launch_environment_values[found] = value;
    }
    return astra_launch_environment_pack(
        arguments, shell.launch_environment,
        (uint32_t)sizeof(shell.launch_environment), count,
        shell.launch_environment_names, shell.launch_environment_values);
}

static uint32_t command_launch(astra_shell_words_t *words)
{
    AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX];
    AstraLaunchArguments arguments;
    AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
    uint32_t handle = 0u;
    uint32_t child_id = 0u;
    uint32_t exit_status = 0u;
    uint32_t status;
    AstraProcessInfo crash_info = {0};
    AstraTtyState tty_before;
    int have_crash_info = 0;
    uint32_t parent = shell.child;
    uint64_t started = astra_clock_monotonic();
    uint64_t opened;
    uint64_t spawned;

    if (astra_launch_arguments_pack(
            &arguments, shell.launch_arguments,
            (uint32_t)sizeof(shell.launch_arguments),
            ASTRA_LAUNCH_SOURCE_SHELL, (uint32_t)words->argc,
            (const char *const *)words->argv) != ASTRA_SYSCALL_OK ||
        pack_launch_environment(&arguments, words) != ASTRA_SYSCALL_OK) {
        write_line("too many arguments");
        return SHELL_STATUS_NOT_RUN;
    }

    status = open_launch_source(words->argv[0], &source);
    opened = astra_clock_monotonic();
    if (status != ASTRA_VFS_OK) {
        /*
         * "Not a command" is what a genuinely absent name says. Anything
         * else -- a member that refused for its own reason, a device that
         * answered ASTRA_VFS_ERR_IO -- is reported for what it is, the same
         * distinction the Filesystem Kit preserves: absence is not the only
         * reason a lookup can fail.
         */
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            astra_terminal_write(&shell.terminal, words->argv[0]);
            write_line(": not a command");
            return SHELL_STATUS_NOT_FOUND;
        }
        report_status(words->argv[0], status);
        return SHELL_STATUS_NOT_RUN;
    }

    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "launching, %u bytes of image", source.length);
    (void)memset(grants, 0, sizeof(grants));
    console_stream_tty_state(&tty_before);
    status = astra_launch_stream(
        source.length, astra_vfs_read_source_read_at,
        astra_vfs_read_source_close, &source, grants,
        launch_grants(grants, words->argv[0]), &arguments, &handle, &child_id);
    spawned = astra_clock_monotonic();
    if (status != ASTRA_SYSCALL_OK) {
        astra_terminal_write(&shell.terminal, words->argv[0]);
        write_line(": would not start");
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
    shell.child = parent;
    if (status == ASTRA_SYSCALL_PEER_DEAD &&
        astra_process_info(handle, &crash_info) == ASTRA_SYSCALL_OK)
        have_crash_info = 1;
    (void)astra_close(handle);
    /*
     * The child's last words before this shell's account of the child. Its
     * exit was noticed by the wait above, which leaves whatever it wrote on
     * the way out still queued on the sink -- so without this the shell's
     * "exited 13" prints above the line that says what 13 meant.
     */
    (void)console_stream_drain();
    shell.pending_key_valid = 0u;
    console_stream_tty_restore(&tty_before);

    /*
     * **The status is answered, not narrated.** A shell that printed
     * "ls: exited 0" after every line was telling a person something they did
     * not ask for and could not use; `$?` is where a status belongs, and it is
     * a value a later command can act on rather than a line on a screen. What
     * a *failing* command has to say, it says itself on STDERR.
     *
     * A faulted child carries the kernel's verdict in `$?` and gets one crash
     * line with the fault site. Only a failed wait has no child status; that
     * gets the shell's own 126 rather than a stale zero.
     */
    if (status == ASTRA_SYSCALL_PEER_DEAD) {
        astra_terminal_write(&shell.terminal, words->argv[0]);
        astra_terminal_write(&shell.terminal, ": crashed");
        if (have_crash_info) {
            astra_terminal_write(&shell.terminal, ": pc ");
            write_hex32(crash_info.fault_pc);
            astra_terminal_write(&shell.terminal, ", address ");
            write_hex32(crash_info.fault_address);
            astra_terminal_write(&shell.terminal, ", vector ");
            write_number(crash_info.fault_vector);
        }
        astra_terminal_putc(&shell.terminal, '\n');
    } else if (status != ASTRA_SYSCALL_OK) {
        astra_terminal_write(&shell.terminal, words->argv[0]);
        write_line(": wait failed");
        exit_status = SHELL_STATUS_NOT_RUN;
    }
    ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_INFO,
                 "child %u finished with status %u", child_id, exit_status);
    ASTRA_EVENT3(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_NOTICE,
                 "command stages: open %u load %u run %u us",
                 astra_elapsed_microseconds(started, opened),
                 astra_elapsed_microseconds(opened, spawned),
                 astra_elapsed_microseconds(spawned,
                                            astra_clock_monotonic()));
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
        if (strcmp(shell_builtins[index].name, word) == 0)
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
typedef struct RedirectState {
    AstraFile file;
    uint32_t status;     /* the first write that refused */
    int stalled;         /* a write that took less than it was given */
    uint32_t bytes;
    uint32_t activity;
} RedirectState;

static RedirectState redirect;

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

static uint32_t run_line(const char *line)
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
        return SHELL_STATUS_NOT_RUN;
    }
    if (parsed != ASTRA_SHELL_OK)
        return SHELL_STATUS_NOT_RUN;
    if (words.argc == 0 && words.assignments == 0)
        return ASTRA_VFS_OK;

    /*
     * A typed line is a unit of work, so it is where a story begins. Every
     * event emitted from here until the next line -- by the shell, by the Kit,
     * by the service and by the backend, because they are all downstream of
     * this thread -- carries this activity, and reading one command's account
     * is then reading one number.
     */
    (void)astra_activity_begin();
    astra_process_vfs_set_activity(astra_activity_current());
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
        return status;
    }

    builtin = shell_builtin(words.argv[0]);
    /*
     * A builtin writes to the terminal directly and has no stream to move and
     * no environment to be handed, so a redirect or an assignment on one is
     * refused before it runs rather than ignored after. The builtins left here
     * are on their way out for exactly this reason: `ls > out.txt` works
     * because `ls` is a program.
     */
    if (builtin != NULL &&
        (words.redirect != NULL || words.assignments != 0)) {
        astra_terminal_write(&shell.terminal, words.argv[0]);
        write_line(": a builtin cannot be redirected or given an environment");
        status = SHELL_STATUS_NOT_RUN;
    } else if (builtin != NULL)
        status = (uint32_t)builtin->function(NULL, words.argc, words.argv);
    else if (words.redirect == NULL)
        /*
         * Not a builtin, so it is a program. There is no third case: a word
         * the machine does not recognise is a file it has not got, and saying
         * that is the whole of the answer.
         */
        status = command_launch(&words);
    else {
        RedirectState outer_redirect = redirect;
        uint32_t outer_stdout = shell.launch_streams[0];
        int had_outer_redirect = console_stream_redirected();

        /* A system() command is a subshell and may redirect inside one. */
        if (had_outer_redirect) {
            (void)console_stream_drain();
            (void)console_stream_redirect(NULL, NULL);
        }
        if (redirect_begin(words.redirect, words.redirect_append)) {
            /* The redirect is the STDOUT capability granted at launch. */
            if (shell.launch_stream_override)
                shell.launch_streams[0] = console_stream_stdout();
            status = command_launch(&words);
            shell.launch_streams[0] = outer_stdout;
            redirect_end(words.redirect);
        } else {
            status = SHELL_STATUS_NOT_RUN;
        }
        if (had_outer_redirect) {
            redirect = outer_redirect;
            if (!console_stream_redirect(redirect_render, NULL))
                status = SHELL_STATUS_NOT_RUN;
        }
    }
    shell_set_status(status);
    return status;
}

static int
shell_request_environment(astra_shell_variables_t *variables,
                          const char *environment, uint32_t length,
                          uint16_t count)
{
    char pair[ASTRA_LAUNCH_ENVIRONMENT_BYTES];
    uint32_t at = 0u;

    astra_shell_variables_init(variables);
    for (uint16_t entry = 0u; entry < count; ++entry) {
        uint32_t end = at;
        uint32_t equals = UINT32_MAX;

        while (end < length && environment[end] != '\0') {
            if (environment[end] == '=' && end != at &&
                equals == UINT32_MAX)
                equals = end;
            ++end;
        }
        if (end == length || equals == UINT32_MAX ||
            end - at + 1u > sizeof(pair))
            return 0;
        (void)memcpy(pair, environment + at, end - at + 1u);
        pair[equals - at] = '\0';
        if (!astra_shell_variable_exportable(pair) ||
            astra_shell_variable_set(variables, pair,
                                     pair + equals - at + 1u) !=
                ASTRA_SHELL_OK)
            return 0;
        at = end + 1u;
    }
    return at == length;
}

static void
shell_execute_reply(uint32_t handle, uint32_t transaction, uint32_t status,
                    uint32_t command_status)
{
    AstraShellExecuteReply reply = {0};

    if (handle == 0u)
        return;
    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_SHELL_SERVICE_PROTOCOL,
                             ASTRA_SHELL_SERVICE_VERSION,
                             ASTRA_SHELL_EXECUTED, transaction);
    reply.status = status;
    reply.command_status = command_status;
    (void)astra_port_send(handle, &reply, sizeof(reply), NULL, 0u);
}

static void
pump_shell_request(void)
{
    AstraShellExecuteRequest request = {0};
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0u};
    uint32_t size = 0u;
    uint32_t handle_count = 0u;
    uint32_t status;
    uint32_t command_status = SHELL_STATUS_NOT_RUN;
    uint32_t reply_handle = 0u;
    void *area = NULL;
    uint32_t area_size = 0u;

    if (shell.command_receive == 0u)
        return;
    status = astra_port_receive(shell.command_receive, &request,
                                sizeof(request), handles,
                                ASTRA_MESSAGE_HANDLES_MAX, &size,
                                &handle_count);
    if (status == ASTRA_SYSCALL_WOULD_BLOCK)
        return;
    if (status != ASTRA_SYSCALL_OK)
        return;
    if (handle_count != 0u)
        reply_handle = handles[0];
    status = ASTRA_STATUS_PROTOCOL;
    if (size != sizeof(request) ||
        request.header.total_size != sizeof(request) ||
        request.header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        request.header.flags != 0u || request.header.reserved != 0u ||
        request.header.protocol != ASTRA_SHELL_SERVICE_PROTOCOL ||
        request.header.protocol_version != ASTRA_SHELL_SERVICE_VERSION ||
        request.header.operation != ASTRA_SHELL_EXECUTE ||
        request.reserved != 0u || request.handle_count != handle_count ||
        handle_count < 2u ||
        request.environment_length > ASTRA_LAUNCH_ENVIRONMENT_BYTES ||
        request.environment_count > ASTRA_LAUNCH_ENVIRONMENT_MAX ||
        (request.environment_count == 0u) !=
            (request.environment_length == 0u))
        goto finish;
    if ((request.stdin_index != ASTRA_SHELL_HANDLE_NONE &&
         request.stdin_index >= handle_count) ||
        (request.stdout_index != ASTRA_SHELL_HANDLE_NONE &&
         request.stdout_index >= handle_count) ||
        (request.stderr_index != ASTRA_SHELL_HANDLE_NONE &&
         request.stderr_index >= handle_count))
        goto finish;
    if (astra_rt_area_map(handles[1], ASTRA_AREA_MAP_READ, &area,
                          &area_size) != ASTRA_SYSCALL_OK) {
        status = ASTRA_STATUS_INVALID;
        goto finish;
    }
    if (request.command_length >= area_size ||
        request.environment_length >
            area_size - request.command_length - 1u ||
        ((const char *)area)[request.command_length] != '\0')
        goto finish;
    {
        astra_shell_variables_t saved_variables = shell.variables;
        astra_shell_variables_t requested_variables;
        uint32_t saved_streams[3];
        char saved_assign[ASTRA_CAPABILITY_NAME_MAX];
        char saved_directory[SHELL_PATH_MAX];
        uint8_t saved_override = shell.launch_stream_override;

        if (!shell_request_environment(
                &requested_variables,
                (const char *)area + request.command_length + 1u,
                request.environment_length, request.environment_count))
            goto finish;
        shell.variables = requested_variables;
        (void)astra_shell_variable_set(&shell.variables, "?", "0");
        (void)memcpy(saved_streams, shell.launch_streams,
                     sizeof(saved_streams));
        (void)memcpy(saved_assign, shell.assign, sizeof(saved_assign));
        (void)memcpy(saved_directory, shell.directory,
                     sizeof(saved_directory));
        shell.launch_streams[0] =
            request.stdout_index == ASTRA_SHELL_HANDLE_NONE ? 0u :
                handles[request.stdout_index];
        shell.launch_streams[1] =
            request.stderr_index == ASTRA_SHELL_HANDLE_NONE ? 0u :
                handles[request.stderr_index];
        shell.launch_streams[2] =
            request.stdin_index == ASTRA_SHELL_HANDLE_NONE ? 0u :
                handles[request.stdin_index];
        shell.launch_stream_override = 1u;
        command_status = run_line((const char *)area);
        shell.variables = saved_variables;
        (void)memcpy(shell.launch_streams, saved_streams,
                     sizeof(saved_streams));
        shell.launch_stream_override = saved_override;
        (void)memcpy(shell.assign, saved_assign, sizeof(saved_assign));
        (void)memcpy(shell.directory, saved_directory,
                     sizeof(saved_directory));
        status = ASTRA_STATUS_OK;
    }

finish:
    shell_execute_reply(reply_handle, request.header.transaction_id, status,
                        command_status);
    if (area != NULL)
        (void)astra_rt_area_unmap(area);
    for (uint32_t index = 0u; index < handle_count; ++index)
        (void)astra_close(handles[index]);
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
    size_t old_cursor = shell.editor.cursor;
    size_t old_length = shell.editor.length;
    size_t erase;

    if (shell.child != 0u) {
        int accepted = console_stream_key(code);

        if (accepted == 0) {
            shell.pending_key = code;
            shell.pending_key_valid = 1u;
        }
        return;
    }

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
        run_line(shell.editor.line);
        astra_shell_editor_commit(&shell.editor);
        prompt();
        return;
    }
    if (result != ASTRA_SHELL_CHANGED)
        return;

    if (event.key == ASTRA_SHELL_KEY_CHARACTER &&
        old_cursor == old_length && shell.editor.cursor == old_cursor + 1u) {
        astra_terminal_putc(&shell.terminal, event.character);
        return;
    }
    if (event.key == ASTRA_SHELL_KEY_BACKSPACE &&
        old_cursor == old_length && shell.editor.length + 1u == old_length) {
        astra_terminal_putc(&shell.terminal, '\b');
        astra_terminal_putc(&shell.terminal, ' ');
        astra_terminal_putc(&shell.terminal, '\b');
        return;
    }

    /*
     * Return to the logical start with backspaces rather than carriage return:
     * a long command can wrap, so its start need not be on the cursor's row.
     */
    erase = old_length > shell.editor.length ?
        old_length - shell.editor.length : 0u;
    old_cursor += (uint32_t)strlen(shell.assign) +
                  (uint32_t)strlen(shell.directory) + 3u;
    for (size_t index = 0u; index < old_cursor; ++index)
        astra_terminal_putc(&shell.terminal, '\b');
    prompt();
    astra_terminal_write(&shell.terminal, shell.editor.line);
    for (size_t index = 0u; index < erase; ++index)
        astra_terminal_putc(&shell.terminal, ' ');
    for (size_t index = shell.editor.cursor;
         index < shell.editor.length + erase;
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
    pump_shell_request();
    rendered = console_stream_pump();
    /*
     * A machine with no keyboard still has a screen, and a child still writes
     * to it. This used to return here, which meant the flush at the bottom --
     * the only thing that paints -- was reachable only by way of a keystroke.
     */
    if (shell.pending_key_valid) {
        int accepted = console_stream_key(shell.pending_key);

        if (accepted > 0) {
            shell.pending_key_valid = 0u;
            had_key = 1;
        }
    }
    do {
        if (shell.pending_key_valid)
            break;
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
        return 0;
    }
    /*
     * Flushed whenever anything might have changed the cells, which is not the
     * same as "a key arrived": a child's output reaches the model through the
     * sink and nobody typed anything at all.
     */
    {
        uint64_t now = astra_clock_monotonic();

        if (shell.present_deadline == 0u && (rendered != 0u || had_key))
            shell.present_deadline = now + CONSOLE_PRESENT_NS;
        if (shell.present_deadline == 0u || now >= shell.present_deadline) {
            if (!flush_terminal()) {
                return 0;
            }
            shell.present_deadline = 0u;
        }
    }
    if (!had_key) {
        uint32_t waits[6];
        uint32_t wait_count = 0u;
        uint32_t child_index = ASTRA_WAIT_INDEX_NONE;
        uint32_t sink = console_stream_wait_handle();
        uint32_t source = console_stream_input_wait_handle();
        uint32_t redirected = console_stream_redirect_wait_handle();

        if (sink != 0u)
            waits[wait_count++] = sink;
        if (source != 0u)
            waits[wait_count++] = source;
        /*
         * And the file's port, while a child is writing into one. STDERR still
         * arrives on the terminal's sink, so this is a second port and not the
         * same one moved -- and a wait that named only the first would sleep
         * through a child that had filled the second and stopped.
         */
        if (redirected != 0u)
            waits[wait_count++] = redirected;
        if (shell.command_receive != 0u)
            waits[wait_count++] = shell.command_receive;
        if (shell.child != 0u) {
            child_index = wait_count;
            waits[wait_count++] = shell.child;
        }
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
            uint32_t ready = ASTRA_WAIT_INDEX_NONE;
            uint32_t wait_status = astra_wait_multiple(
                waits, wait_count, deadline, &ready, NULL);

            if (wait_status != ASTRA_SYSCALL_OK &&
                wait_status != ASTRA_SYSCALL_TIMED_OUT &&
                !(wait_status == ASTRA_SYSCALL_PEER_DEAD &&
                  ready == child_index)) {
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

void console_shell_run_backend(const ConsoleShellBackend *backend)
{
    if (backend == NULL || backend->columns == 0u || backend->rows == 0u ||
        backend->terminal_storage == NULL ||
        backend->terminal_storage_size == 0u || backend->render == NULL ||
        backend->process_filesystem == NULL ||
        backend->process_filesystem->library == NULL) {
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
    if (filesystem_library()->assign_lookup(astra_process_vfs_assigns(), "WORK") !=
        NULL) {
        (void)memcpy(shell.assign, "WORK", 5u);
    } else if (filesystem_library()->assign_lookup(
                   astra_process_vfs_assigns(), "SYS") != NULL) {
        (void)memcpy(shell.assign, "SYS", 4u);
    }
    if (astra_terminal_init_capacity(
            &shell.terminal, backend->columns, backend->rows,
            backend->terminal_capacity_columns != 0u ?
                backend->terminal_capacity_columns : backend->columns,
            backend->terminal_capacity_rows != 0u ?
                backend->terminal_capacity_rows : backend->rows,
            backend->terminal_storage, backend->terminal_storage_size,
            backend->render, backend->context) !=
        ASTRA_TERMINAL_OK) {
        return;
    }
    astra_terminal_set_scroll(&shell.terminal, backend->scroll);
    astra_terminal_set_echo(&shell.terminal, echo_line, NULL);
    astra_shell_editor_init(&shell.editor);
    astra_shell_variables_init(&shell.variables);
    (void)astra_shell_variable_set(&shell.variables, "TERM",
                                   "astra-256color");
    /*
     * `?` exists from the first prompt. A shell whose `$?` was empty until
     * something had run would make `echo $?` a different thing on line one
     * than on line two, and nothing about a fresh shell has failed.
     */
    (void)astra_shell_variable_set(&shell.variables, "?", "0");
    if (astra_rt_port_create(4u,
                             4u * ASTRA_SHELL_EXECUTE_REQUEST_SIZE,
                             &shell.command_receive,
                             &shell.command_send) != ASTRA_SYSCALL_OK) {
        shell.command_receive = 0u;
        shell.command_send = 0u;
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "shell execution service unavailable");
    }
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
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "console streams unavailable, launched programs "
                     "cannot write");
    } else {
        console_stream_resize(backend->columns, backend->rows,
                              backend->pixel_width, backend->pixel_height);
        astra_terminal_set_reply(&shell.terminal,
                                 console_stream_terminal_reply, NULL);
    }

    astra_terminal_clear(&shell.terminal);
    write_line("Astra 68");
    write_line("namespace: WORK: writable");
    command_help();
    prompt();
    if (!flush_terminal()) {
        return;
    }

    while (shell.running) {
        if (!pump_once()) {
            return;
        }
    }
}
