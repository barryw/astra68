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
#include <volume.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/keymap.h>
#include <astra/runtime.h>
#include <astra/shell.h>
#include <astra/stream.h>
#include <astra/supervisor.h>
#include <astra/syscall.h>
#include <astra/terminal.h>

#include <astra/event_emit.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_client.h>
#include <astra/vfs_path.h>
#include <astra/vfs_union.h>
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
 * The adapter astra_vfs_assign_open needs. The Kit's union loop takes a
 * client_for callback carrying a context pointer, because a launched
 * program's clients and the supervisor's are looked up different ways; this
 * shell has no context to give it, so the adapter just drops it.
 */
static AstraVfsClient *shell_client_for(const AstraAssign *assign,
                                        void *context)
{
    (void)context;
    return supervisor_vfs_client_for(assign);
}

/*
 * The first member of an already-qualified path that actually carries the
 * requested rights -- not necessarily member zero, since a union's first
 * member can be read-only. This is where a name that does not exist yet
 * belongs: mkdir, and a write to a name that turns out to be on no member at
 * all, both create through here, so a new file lands on whichever member is
 * willing to take it rather than wherever join order happened to put a
 * read-only copy.
 *
 * "Not found" is what this returns once every member has said no and none of
 * them said anything more specific; a member that refused because it does not
 * carry the right is remembered instead, the same worst-status rule
 * astra_vfs_assign_open documents (astra/vfs_union.h) -- a namespace with no
 * writable member at all is an access problem, not an absence, and must not
 * be reported as one.
 */
static uint32_t shell_path_primary(const char *typed, uint32_t rights,
                                   char *wire, uint32_t capacity,
                                   AstraVfsClient **client)
{
    uint32_t worst = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = NULL;
        uint32_t status;

        status = astra_assign_resolve(supervisor_assigns(), typed, rights,
                                      member, wire, capacity, &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            return worst;
        if (status != ASTRA_VFS_OK) {
            if (worst == ASTRA_VFS_ERR_NOT_FOUND)
                worst = status;
            continue;
        }
        *client = supervisor_vfs_client_for(assign);
        if (*client != NULL)
            return ASTRA_VFS_OK;
    }
}

/*
 * Where an already-qualified path already is, walked member by member without
 * opening a handle: cd only wants its kind and rm is about to remove it, so
 * neither wants a file left open behind it. Resolution is a string operation
 * and existence is not (astra/vfs_assign.h), so a stat is what actually
 * answers "is it on this member" -- the same reason launch_path's own search
 * stats rather than trusting resolve alone.
 *
 * The same worst-status rule as shell_path_primary, and for rm it is not
 * academic: a name that lives only on a read-only member must come back
 * "access denied", not "not found" -- the file is right there, and telling a
 * person it is absent when it is merely untouchable is the wrong refusal.
 *
 * Existence and permission are different questions, and resolving with the
 * *operation's* rights used to ask them as one. astra_assign_resolve refuses
 * ASTRA_VFS_ERR_ACCESS for a member whose bound rights do not cover what was
 * requested, and it does that before any I/O -- so that refusal has told
 * this function nothing about whether the member actually holds the name.
 * Treating it as this function's own worst status anyway let a member that
 * merely cannot be written outrun every other member's honest NOT_FOUND, so
 * a name sitting on no member at all came back "access denied" instead of
 * "not found": the exact failure this comment is here to keep from coming
 * back. So every member is now resolved for existence only, with READ -- the
 * same right `cd` and `cat` already ask for, since a name has to be readable
 * to be found before anything decides whether it may also be acted on. Only
 * once a stat confirms the name is really there is the caller's `rights`
 * checked against the member that holds it; a member that has the name but
 * not the right is remembered exactly like the read-only-member case above
 * and the walk moves on, while a member that lacks even READ still refuses
 * at resolve, unchanged.
 */
static uint32_t shell_locate(const char *typed, uint32_t rights, char *wire,
                             uint32_t capacity, AstraVfsClient **client,
                             uint16_t *kind)
{
    uint32_t worst = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = NULL;
        uint32_t status;

        status = astra_assign_resolve(supervisor_assigns(), typed,
                                      ASTRA_RIGHT_READ, member, wire, capacity,
                                      &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            return worst;
        if (status != ASTRA_VFS_OK) {
            if (worst == ASTRA_VFS_ERR_NOT_FOUND)
                worst = status;
            continue;
        }
        *client = supervisor_vfs_client_for(assign);
        if (*client == NULL)
            continue;
        status = astra_vfs_stat(*client, wire, NULL, kind);
        if (status == ASTRA_VFS_OK) {
            if ((assign->rights & rights) != rights) {
                /*
                 * It is here, but not writable (or otherwise usable as asked)
                 * here -- not the same thing as absent, so this must not let
                 * a later member's ordinary NOT_FOUND look like the final
                 * word once none of them turn out to hold the name at all.
                 */
                if (worst == ASTRA_VFS_ERR_NOT_FOUND)
                    worst = ASTRA_VFS_ERR_ACCESS;
                continue;
            }
            return ASTRA_VFS_OK;
        }
        /*
         * Not on this member: the next one still might have it. But the same
         * worst-status rule applies to a stat that failed for its own reason
         * as to a resolve that did (astra/vfs_union.h) -- a member whose
         * device answered ASTRA_VFS_ERR_IO or ASTRA_VFS_ERR_PROTOCOL did not
         * say "absent", it said "I could not tell you", and a later member's
         * ordinary NOT_FOUND must not bury that.
         */
        if (status != ASTRA_VFS_ERR_NOT_FOUND && worst == ASTRA_VFS_ERR_NOT_FOUND)
            worst = status;
    }
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
    char typed[SHELL_PATH_MAX];
    char path[SHELL_PATH_MAX];
    char name[ASTRA_VFS_NAME_MAX];
    const AstraAssign *assign = NULL;
    AstraVfsClient *client = NULL;
    uint32_t shown = 0u;
    uint32_t members = 0u;
    uint32_t worst = ASTRA_VFS_ERR_NOT_FOUND;
    uint32_t status;

    if (storage() == NULL) {
        write_line("ls: no volume");
        return;
    }
    status = astra_path_qualify(shell.assign, shell.directory,
                                argc > 1 ? argv[1] : NULL, typed,
                                sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("ls", status);
        return;
    }
    /*
     * Every member, in order, and nothing is deduplicated: a name on two
     * members is shown twice with the member it came from. A duplicate-name
     * set would be memory proportional to the directory, which every
     * enumeration on this machine refuses -- and hiding the loser would make a
     * listing disagree with what a lookup would do.
     */
    for (uint32_t member = 0u; ; ++member) {
        uint64_t cursor = 0u;
        uint16_t kind = 0u;

        status = astra_assign_resolve(supervisor_assigns(), typed,
                                      ASTRA_RIGHT_READ, member, path,
                                      sizeof(path), &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            break;
        }
        if (status != ASTRA_VFS_OK) {
            /*
             * The same worst-status rule as shell_locate and
             * shell_path_primary (astra/vfs_union.h): a member that refused
             * for a reason other than plain absence must survive to the
             * final report, so a name whose only members lack read rights is
             * told "access denied" rather than the ordinary "not found".
             */
            if (worst == ASTRA_VFS_ERR_NOT_FOUND)
                worst = status;
            continue;
        }
        client = supervisor_vfs_client_for(assign);
        if (client == NULL) {
            continue;
        }
        ++members;
        for (;;) {
            status = astra_vfs_readdir(client, path, cursor, name,
                                       sizeof(name), &kind, &cursor);
            if (status == ASTRA_VFS_ERR_NOT_FOUND)
                break;
            if (status != ASTRA_VFS_OK) {
                report_status("ls", status);
                return;
            }
            astra_terminal_write(&shell.terminal, name);
            if (kind == ASTRA_VFS_KIND_DIRECTORY)
                astra_terminal_putc(&shell.terminal, '/');
            /*
             * The member it came from, so the shadowing a person cannot see in
             * a name is visible in the listing.
             */
            astra_terminal_write(&shell.terminal, "  [");
            write_number(member);
            astra_terminal_write(&shell.terminal, "]");
            astra_terminal_putc(&shell.terminal, '\n');
            ++shown;
        }
    }
    if (members == 0u) {
        report_status("ls", worst);
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
        /*
         * A directory on the second member is a directory that exists: cd
         * used to resolve member zero only, which made a directory that
         * lived solely on a union's later member impossible to enter even
         * though `ls` could already show it sitting there. Verified before
         * it is adopted, so the prompt never lies.
         */
        status = shell_locate(typed, ASTRA_RIGHT_READ, wire, sizeof(wire),
                              &client, &kind);
    }
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
    char typed[SHELL_PATH_MAX];
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
    status = astra_path_qualify(shell.assign, shell.directory, argv[1], typed,
                                sizeof(typed));
    if (status == ASTRA_VFS_OK) {
        /*
         * A file on the second member is a file that exists: cat used to
         * resolve member zero only, so a name shadowed by nothing on the
         * first member but present on the second could not be read at all.
         * The Kit's own union loop is the right tool here, because reading
         * wants exactly what it hands back -- an open handle on whichever
         * member answered.
         */
        status = astra_vfs_assign_open(supervisor_assigns(), typed,
                                       ASTRA_RIGHT_READ, ASTRA_VFS_OPEN_READ,
                                       shell_client_for, NULL, path,
                                       sizeof(path), &file, NULL, NULL,
                                       &client, NULL);
    }
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
    char typed[SHELL_PATH_MAX];
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
    status = astra_path_qualify(shell.assign, shell.directory, argv[1], typed,
                                sizeof(typed));
    if (status == ASTRA_VFS_OK) {
        /*
         * A new directory is a creation, and creation belongs on the
         * primary -- the first member willing to take a write -- rather
         * than on member zero regardless of whether it can.
         */
        status = shell_path_primary(typed, ASTRA_RIGHT_WRITE, path,
                                    sizeof(path), &client);
    }
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
    char typed[SHELL_PATH_MAX];
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
    status = astra_path_qualify(shell.assign, shell.directory, argv[1], typed,
                                sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        report_status("write", status);
        return;
    }
    /*
     * A name that already exists is written where it already is, not on the
     * primary: writing through the union at member zero regardless would let
     * a person "edit" a shipped file on a later member and silently get a
     * new, shadowing copy on the first one instead of the edit they asked
     * for.
     *
     * This used to open with WRITE|TRUNCATE and no CREATE, on the reasoning
     * that a truncate-without-create open is an existence probe: a member
     * that lacks the file would refuse and the walk would move on. That
     * reasoning does not hold on this machine. The ext4 backend's mode_of()
     * (vfs_ext4_backend.c) maps ASTRA_VFS_OPEN_TRUNCATE to lwext4's "wb"
     * whether or not ASTRA_VFS_OPEN_CREATE is set, and "wb" both creates and
     * truncates -- so the probe at the writable primary always succeeds by
     * creating an empty file there, and the walk never reaches the member
     * that actually holds the name. An open is not an existence test here.
     *
     * So the name is located first, with no handle opened, exactly as `rm`
     * already does -- and only once a holder is found, or confirmed absent
     * everywhere, is anything opened.
     */
    status = shell_locate(typed, ASTRA_RIGHT_WRITE, path, sizeof(path),
                          &client, NULL);
    if (status == ASTRA_VFS_OK) {
        /* Found: write in place, on the member that already has it. */
        status = astra_vfs_open(client, path,
                                ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_TRUNCATE,
                                &file, NULL, NULL);
    } else if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        /* Not on any member: a name nobody has yet is created on the primary. */
        status = shell_path_primary(typed, ASTRA_RIGHT_WRITE, path,
                                    sizeof(path), &client);
        if (status == ASTRA_VFS_OK) {
            status = astra_vfs_open(client, path,
                                    ASTRA_VFS_OPEN_WRITE |
                                        ASTRA_VFS_OPEN_CREATE |
                                        ASTRA_VFS_OPEN_TRUNCATE,
                                    &file, NULL, NULL);
        }
    }
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
    char typed[SHELL_PATH_MAX];
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
    status = astra_path_qualify(shell.assign, shell.directory, argv[1], typed,
                                sizeof(typed));
    if (status == ASTRA_VFS_OK) {
        /*
         * Removing acts on the member that actually holds the name, not on
         * the primary: unlinking through the union at member zero regardless
         * would refuse "not found" for a name only a later member has, while
         * leaving that member's own copy sitting there untouched.
         */
        status = shell_locate(typed, ASTRA_RIGHT_WRITE, path, sizeof(path),
                              &client, NULL);
    }
    if (status != ASTRA_VFS_OK) {
        report_status("rm", status);
        return;
    }
    status = astra_vfs_unlink(client, path);
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
    write_line("paths are ASSIGN:path -- there is no root. try ls SYS:");
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
    uint32_t status = ASTRA_VFS_ERR_NOT_FOUND;

    for (uint32_t index = 0u; word[index] != '\0'; ++index) {
        if (word[index] != ':') {
            continue;
        }
        /*
         * Named its own assign: one place, no search -- but the assign it
         * names can still be a union, and `COMMANDS:status` typed out is the
         * same lookup a bare `status` would have done through the COMMANDS:
         * place, so it walks members the same way.
         */
        for (uint32_t member = 0u; ; ++member) {
            const AstraAssign *found = NULL;

            status = astra_assign_resolve(supervisor_assigns(), word,
                                          ASTRA_RIGHT_READ, member, wire,
                                          capacity, &found);
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                return status;    /* past the last member: nothing named it */
            }
            if (status != ASTRA_VFS_OK) {
                continue;
            }
            *client = supervisor_vfs_client_for(found);
            if (*client == NULL) {
                continue;
            }
            {
                uint16_t kind = 0u;

                if (astra_vfs_stat(*client, wire, NULL, &kind) !=
                    ASTRA_VFS_OK) {
                    continue;   /* not on this member: try the next */
                }
                if (kind == ASTRA_VFS_KIND_DIRECTORY) {
                    continue;   /* a directory is not a command */
                }
            }
            ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SHELL,
                         ASTRA_EVENT_LEVEL_NOTICE,
                         "launching named directly, member %u", member);
            return ASTRA_VFS_OK;
        }
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
        /*
         * Every member of the place, in order, and the first that opens wins.
         * The index is the answer to "which one ran": a path variable cannot
         * answer that question, and a union is holding it by the time the
         * child starts.
         */
        for (uint32_t member = 0u; ; ++member) {
            const AstraAssign *found = NULL;

            status = astra_assign_resolve(supervisor_assigns(), typed,
                                          ASTRA_RIGHT_READ, member, wire,
                                          capacity, &found);
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                break;      /* past the last member: try the next place */
            }
            if (status != ASTRA_VFS_OK) {
                continue;
            }
            *client = supervisor_vfs_client_for(found);
            if (*client == NULL) {
                continue;
            }
            {
                uint16_t kind = 0u;

                if (astra_vfs_stat(*client, wire, NULL, &kind) !=
                    ASTRA_VFS_OK) {
                    continue;   /* not on this member: try the next */
                }
                if (kind == ASTRA_VFS_KIND_DIRECTORY) {
                    continue;   /* a directory is not a command */
                }
            }
            ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SHELL,
                         ASTRA_EVENT_LEVEL_NOTICE,
                         "launching from place %u member %u", place, member);
            return ASTRA_VFS_OK;
        }
    }
    return status;
}

/*
 * The grants a launched command is handed: three streams, and a namespace.
 *
 * `ASTRA_LAUNCH_GRANT_MAX` is eight and this uses all seven -- three streams
 * plus WORK, two COMMANDS members and EVENTS -- which is a fact worth stating
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
    static const char *const mount_names[] = {"WORK", "COMMANDS", "EVENTS"};
    uint32_t streams[3];
    uint32_t count = 0u;

    streams[0] = console_stream_stdout();
    streams[1] = console_stream_stderr();
    streams[2] = console_stream_stdin();
    for (uint32_t index = 0u; index < 3u && count < ASTRA_LAUNCH_GRANT_MAX;
         ++index) {
        if (streams[index] == 0u) {
            continue;
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
    for (uint32_t index = 0u; index < 3u && count < ASTRA_LAUNCH_GRANT_MAX;
         ++index) {
        for (uint32_t member = 0u; count < ASTRA_LAUNCH_GRANT_MAX; ++member) {
            const AstraAssign *assign =
                astra_assign_member(supervisor_assigns(), mount_names[index],
                                    member);

            if (assign == NULL) {
                break;
            }
            astra_capability_name_set(grants[count].name, mount_names[index]);
            grants[count].handle = assign->handle;
            grants[count].rights = ASTRA_RIGHT_SIGNAL;
            grants[count].flags = ASTRA_CAPABILITY_FLAG_NAMESPACE |
                ((assign->rights & ASTRA_RIGHT_READ) != 0u ?
                     ASTRA_CAPABILITY_FLAG_READ : 0u) |
                ((assign->rights & ASTRA_RIGHT_WRITE) != 0u ?
                     ASTRA_CAPABILITY_FLAG_WRITE : 0u);
            astra_capability_root_set(grants[count].root, assign->root);
            ++count;
        }
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
        write_number((uint32_t)size);
        /*
         * And what the device said, because "I/O error" is lwext4's one errno
         * for a timeout, a cancellation, a short transfer and a corrupt reply.
         * Which of those it was decides whether retrying is worth anything.
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
    /*
     * A service that stopped receiving is indistinguishable from one nobody is
     * calling, and that ambiguity is what made the launch of the first program
     * expensive to diagnose. So the stall is reported, and only the stall:
     * counting what was served says nothing once it works.
     */
    if (supervisor_events_stalled() != 0u) {
        astra_terminal_write(&shell.terminal, "  [events service stalled, "
                             "status ");
        write_number(supervisor_events_stalled());
        write_line("]");
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
    /*
     * And the storage service, which a child reaches by a port now. This is
     * the call that makes a launched program able to open a file: it is
     * blocked on a reply that only this loop can produce.
     *
     * The events service answers on its own port inside supervisor_events_pump
     * above, for the same reason.
     */
    supervisor_vfs_pump();
    /*
     * A machine with no keyboard still has a screen, and a child still writes
     * to it. This used to return here, which meant the flush at the bottom --
     * the only thing that paints -- was reachable only by way of a keystroke.
     */
    status = shell.input != 0u ?
        astra_input_read(shell.input, events, ASTRA_INPUT_READ_BATCH_MAX,
                         &count, &flags) :
        ASTRA_SYSCALL_WOULD_BLOCK;
    /*
     * An empty queue is the ordinary case and the only one worth yielding
     * over. Anything else is a refused call, and treating a refusal as
     * "no input" is what turned a rejected buffer address into a terminal
     * that looked hung: the loop spun forever and said nothing. A broken
     * input path is exactly as fatal to a terminal as a broken flush, and
     * is reported the same way.
     *
     * It falls through to the flush rather than returning. **A pass with no
     * key still has cells to paint**: a launched program's output arrives
     * through the sink above and nothing typed it. Returning early here left
     * that output in the model until somebody happened to press a key, so a
     * program that printed one line and exited printed nothing at all -- and
     * a person waiting for that line had to type to see it, which is the one
     * thing they were waiting to be told they could do.
     */
    if (status != ASTRA_SYSCALL_OK && status != ASTRA_SYSCALL_WOULD_BLOCK) {
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
    /*
     * The other invisible input failure. A refused call at least came back
     * with a status; an overflow comes back with ASTRA_SYSCALL_OK and a flag
     * nothing here used to look at, so a keystroke the queue had no room for
     * vanished with no trace anywhere a person or this gate could see. The
     * line a command produces afterward is not the line that was typed, and
     * until now nothing said so. There is no count to give: the emulator
     * knows how many it dropped and no register carries that number out, so
     * this says only that it happened, which is what can honestly be known.
     */
    if ((flags & ASTRA_INPUT_READ_OVERFLOW) != 0u) {
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_WARNING,
                     "input overflowed, some keystrokes never arrived");
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
