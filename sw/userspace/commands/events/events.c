/*
 * `events` -- the machine's account of itself, read from a file in COMMANDS:.
 *
 * This was a builtin in the shell until the day a program could be launched,
 * and moving it is the point of the milestone rather than a tidy-up. Everything
 * it needs now arrives as a capability: `EVENTS:` is a port handle to a service
 * in the supervisor, `STDOUT` is a port handle to whatever is rendering, and
 * neither is a thing this program can reach without having been granted it.
 *
 * What it lost in the move is the shell's terminal and the shell's client, and
 * what it gained is that nothing about it is special. It builds a path, opens
 * it, and pages what comes back -- which is what any program reading a tree
 * would do, because `EVENTS:` is a tree and that was the whole claim.
 *
 * It also asks how tall the screen is rather than assuming. A program that
 * assumed 80x24 is wrong on this machine's 90x30 plane, and would be wrong
 * again on the next one.
 */

#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/event.h>
#include <astra/event_control.h>
#include <astra/stream.h>
#include <astra/vfs_process.h>

ASTRA_PROGRAM("events", 1, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

/*
 * How many line starts the tail keeps. A screen's worth and then some: the
 * ring is of offsets rather than of text, because re-reading is cheaper than
 * holding a copy of something the store is still appending to.
 */
#define EVENTS_TAIL_MAX 64u
#define EVENTS_READ_CHUNK 128u
#define EVENTS_PATH_MAX 128u

/* Statically allocated, because a user thread gets one 4 KiB stack. */
static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;
static uint32_t out;
static uint8_t chunk[EVENTS_READ_CHUNK];

static int
close_with(uint32_t status)
{
    astra_process_filesystem_close(&process_filesystem);
    return (int)status;
}

static uint32_t
equal(const char *left, const char *right)
{
    uint32_t index = 0u;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0u;
        }
        ++index;
    }
    return left[index] == right[index] ? 1u : 0u;
}

/* Appends at `at`, and returns where the next append starts. */
static uint32_t
append(char *out_path, uint32_t capacity, uint32_t at, const char *text)
{
    while (*text != '\0' && at + 1u < capacity) {
        out_path[at++] = *text++;
    }
    out_path[at] = '\0';
    return at;
}

static void
say(const char *text)
{
    (void)astra_print(out, text);
    (void)astra_print(out, "\n");
}

/*
 * Eight hex digits, lowercase, because that is what the tree's activity
 * directory names its entries and a path is byte-exact after the colon.
 */
static int
activity_name(const char *typed, char *out_name)
{
    static const char digits[] = "0123456789abcdef";
    uint32_t value = 0u;
    uint32_t seen = 0u;

    while (typed[seen] != '\0') {
        char at = typed[seen];
        uint32_t nibble;

        if (seen >= 8u) {
            return 0;
        }
        if (at >= '0' && at <= '9') {
            nibble = (uint32_t)(at - '0');
        } else if (at >= 'a' && at <= 'f') {
            nibble = (uint32_t)(at - 'a') + 10u;
        } else if (at >= 'A' && at <= 'F') {
            nibble = (uint32_t)(at - 'A') + 10u;
        } else {
            return 0;
        }
        value = (value << 4) | nibble;
        ++seen;
    }
    if (seen == 0u) {
        return 0;
    }
    for (uint32_t index = 0u; index < 8u; ++index) {
        out_name[index] = digits[(value >> ((7u - index) * 4u)) & 0xFu];
    }
    out_name[8] = '\0';
    return 1;
}

/* Prints from `offset` to the end, and returns where it stopped. */
static uint64_t
print_from(AstraFile *file, uint64_t offset)
{
    for (;;) {
        uint32_t moved = 0u;

        if (process_filesystem.library->read_at(
                file, offset, chunk, sizeof(chunk), &moved) !=
                ASTRA_VFS_OK || moved == 0u) {
            break;
        }
        {
            uint32_t written = 0u;

            /*
             * Retried until it lands. A page of history that stopped halfway
             * because the sink was momentarily full would be a report a person
             * reads as complete.
             */
            while (written < moved) {
                uint32_t sent = 0u;
                uint32_t status = astra_stream_write(out, chunk + written,
                                                     moved - written, &sent);

                written += sent;
                if (written >= moved) {
                    break;
                }
                if (status != ASTRA_SYSCALL_WOULD_BLOCK) {
                    return offset + written;
                }
                (void)astra_yield();
            }
        }
        offset += moved;
    }
    return offset;
}

/*
 * Where the last `lines` lines begin. A ring of offsets: the screen decides how
 * many, and the file is read twice rather than held once, because the store is
 * still appending to it while this runs.
 */
static uint64_t
tail_from(AstraFile *file, uint32_t lines)
{
    uint64_t starts[EVENTS_TAIL_MAX];
    uint64_t offset = 0u;
    uint32_t count = 1u;
    uint32_t oldest = 0u;

    if (lines == 0u || lines > EVENTS_TAIL_MAX) {
        lines = EVENTS_TAIL_MAX;
    }
    starts[0] = 0u;
    for (;;) {
        uint32_t moved = 0u;

        if (process_filesystem.library->read_at(
                file, offset, chunk, sizeof(chunk), &moved) !=
                ASTRA_VFS_OK || moved == 0u) {
            break;
        }
        for (uint32_t index = 0u; index < moved; ++index) {
            if (chunk[index] != '\n') {
                continue;
            }
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
    return count == lines ? starts[oldest] : starts[0];
}

/*
 * Live, until a line is entered. It was "press any key" as a builtin, when the
 * shell owned the keyboard and could see a keystroke; a program sees STDIN,
 * which is lines, so it is "press return" and the message says so rather than
 * telling somebody to do something that will not work.
 */
static void
follow(uint32_t stdin_handle, AstraFile *file, uint64_t offset)
{
    say("-- following, press return --");
    for (;;) {
        uint8_t typed[1];
        uint32_t length = 0u;
        uint64_t moved_to = print_from(file, offset);

        offset = moved_to;
        if (stdin_handle == 0u) {
            return;
        }
        if (astra_stream_read(stdin_handle, typed, sizeof(typed), &length) !=
                ASTRA_SYSCALL_OK ||
            length != 0u) {
            break;
        }
        (void)astra_yield();
    }
    say("");
}

int
astra_main(const AstraStartupInfo *startup)
{
    static const char *const level_name[] = {"all", "notice", "warning",
                                             "error"};
    static const char *const set_level_name[] = {
        "debug", "info", "notice", "warning", "error"
    };
    static const char *const subsystem_name[] = {
        "kernel", "runtime", "supervisor", "storage", "vfs", "shell",
        "input", "display"
    };
    const AstraStartupCapability *capabilities;
    const uint32_t *argv = NULL;
    char path[EVENTS_PATH_MAX];
    char activity[9];
    const char *level = "notice";
    const char *subsystem = NULL;
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t stdin_handle = 0u;
    uint32_t control_handle = 0u;
    uint32_t columns = 0u;
    uint32_t rows = 0u;
    uint64_t offset;
    uint32_t at = 0u;
    uint32_t status;
    int following = 0;
    int by_activity = 0;
    int previous_boot = 0;
    int level_set = 0;
    uint32_t set_subsystem = 0u;
    uint32_t set_level = 0u;

    if (startup == NULL || startup->capabilities_address == 0u) {
        return ASTRA_STATUS_INVALID;
    }
    capabilities =
        (const AstraStartupCapability *)(uintptr_t)
            startup->capabilities_address;
    if (startup->argc != 0u && startup->argv_address != 0u) {
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    }

    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT")) {
            out = capabilities[index].handle;
        } else if (astra_capability_name_equal(capabilities[index].name,
                                               "STDIN")) {
            stdin_handle = capabilities[index].handle;
        } else if (astra_capability_name_equal(
                       capabilities[index].name,
                       ASTRA_CAPABILITY_EVENT_CONTROL)) {
            control_handle = capabilities[index].handle;
        }
    }
    if (out == 0u) {
        /* Nowhere to write is not a failure this program can report. */
        return ASTRA_STATUS_ACCESS;
    }
    for (uint32_t index = 1u; index < startup->argc; ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];
        const char *value = index + 1u < startup->argc ?
            (const char *)(uintptr_t)argv[index + 1u] : NULL;

        if (equal(word, "--all")) {
            level = "all";
        } else if (equal(word, "--follow")) {
            following = 1;
        } else if (equal(word, "--level")) {
            int known = 0;

            if (value == NULL) {
                say("events: --level needs a name");
                return ASTRA_STATUS_INVALID;
            }
            /*
             * debug is not a level the store has. It is a live subscription,
             * dropped at drain, and saying so is better than showing `all` and
             * letting somebody conclude that nothing was emitted.
             */
            if (equal(value, "debug")) {
                say("events: debug is never stored, showing all");
                level = "all";
                known = 1;
            }
            for (uint32_t name = 0u;
                 !known && name < sizeof(level_name) / sizeof(level_name[0]);
                 ++name) {
                if (equal(value, level_name[name])) {
                    level = level_name[name];
                    known = 1;
                }
            }
            if (!known) {
                say("events: levels are all, notice, warning, error");
                return ASTRA_STATUS_INVALID;
            }
            ++index;
        } else if (equal(word, "--subsystem")) {
            if (value == NULL) {
                say("events: --subsystem needs a name");
                return ASTRA_STATUS_INVALID;
            }
            subsystem = value;
            ++index;
        } else if (equal(word, "--activity")) {
            if (value == NULL || !activity_name(value, activity)) {
                say("events: --activity takes up to eight hex digits");
                return ASTRA_STATUS_INVALID;
            }
            by_activity = 1;
            ++index;
        } else if (equal(word, "--boot")) {
            if (value == NULL || !equal(value, "-1")) {
                say("events: --boot currently takes -1");
                return ASTRA_STATUS_INVALID;
            }
            previous_boot = 1;
            ++index;
        } else if (equal(word, "--since")) {
            say("events: no wall clock yet, so no time range");
            return ASTRA_STATUS_UNSUPPORTED;
        } else if (equal(word, "--level-set")) {
            if (index != 1u || startup->argc != 4u) {
                say("events: --level-set takes a subsystem and level alone");
                return ASTRA_STATUS_INVALID;
            }
            level_set = 1;
            for (set_subsystem = 0u;
                 set_subsystem < ASTRA_EVENT_SUBSYSTEM_MAX;
                 ++set_subsystem) {
                if (equal(value, subsystem_name[set_subsystem]))
                    break;
            }
            for (set_level = 0u; set_level <= ASTRA_EVENT_LEVEL_ERROR;
                 ++set_level) {
                if (equal((const char *)(uintptr_t)argv[index + 2u],
                          set_level_name[set_level]))
                    break;
            }
            if (set_subsystem == ASTRA_EVENT_SUBSYSTEM_MAX ||
                set_level > ASTRA_EVENT_LEVEL_ERROR) {
                say("events: unknown subsystem or level");
                return ASTRA_STATUS_INVALID;
            }
            index += 2u;
        } else {
            say("events: unknown option");
            return ASTRA_STATUS_INVALID;
        }
    }

    if (level_set) {
        if (control_handle == 0u) {
            say("events: this program was not granted event control");
            return ASTRA_STATUS_ACCESS;
        }
        status = astra_event_control_set(control_handle, set_subsystem,
                                         set_level);
        if (status != ASTRA_STATUS_OK) {
            say("events: level change refused");
            return (int)status;
        }
        say("events: temporary level set for this boot");
        return ASTRA_STATUS_OK;
    }

    if (by_activity && subsystem != NULL) {
        /* An activity is already a slice through every other dimension. */
        say("events: --activity is every subsystem, by definition");
        return ASTRA_STATUS_INVALID;
    }
    if (previous_boot && (by_activity || subsystem != NULL)) {
        say("events: --boot cannot be combined with another history");
        return ASTRA_STATUS_INVALID;
    }
    if (previous_boot && following) {
        say("events: a previous boot cannot grow");
        return ASTRA_STATUS_INVALID;
    }
    if (following && stdin_handle == 0u) {
        say("events: no input, so nothing could end a follow");
        return ASTRA_STATUS_INVALID;
    }

    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        say("events: filesystem unavailable");
        return (int)status;
    }

    at = append(path, sizeof(path), at, "EVENTS:");
    if (by_activity) {
        at = append(path, sizeof(path), at, "activity/");
        at = append(path, sizeof(path), at, activity);
    } else if (subsystem != NULL) {
        at = append(path, sizeof(path), at, "subsystem/");
        at = append(path, sizeof(path), at, subsystem);
        at = append(path, sizeof(path), at, "/");
        at = append(path, sizeof(path), at, level);
    } else {
        at = append(path, sizeof(path), at,
                    previous_boot ? "boot/-1/" : "boot/current/");
        at = append(path, sizeof(path), at, level);
    }

    status = process_filesystem.library->open(
        &process_filesystem.filesystem, path, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) {
        say(previous_boot ? "events: no previous boot is stored" :
                            "events: no such view");
        return close_with(status);
    }

    /*
     * How tall the screen is, asked rather than assumed. A sink with no
     * geometry answers zero, and zero means do not page -- which is what a
     * redirected `events` should do.
     */
    (void)astra_stream_size(out, &columns, &rows);
    offset = rows > 1u ? tail_from(&file, rows - 1u) : 0u;
    offset = print_from(&file, offset);
    if (offset == 0u) {
        say("(nothing at that level)");
    }
    if (following) {
        follow(stdin_handle, &file, offset);
    }
    (void)process_filesystem.library->close(&file);
    return close_with(ASTRA_STATUS_OK);
}
