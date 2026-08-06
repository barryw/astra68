/*
 * The terminal the machine boots into.
 *
 * This is the glue the design conversation called glue: it pumps keys from the
 * input device into the line editor, runs the command the editor hands back,
 * and pushes the resulting cells at the display lease. The parts worth keeping
 * are underneath it -- the cell model, the keymap, the line editor -- and the
 * commands here call lwext4 directly because the supervisor already holds the
 * mounted volume. A VFS service takes that over when there is one, and the
 * dispatch below is what changes.
 *
 * Paths are absolute inside the mount point. A working directory is kept as a
 * string and joined here rather than in lwext4, which has no notion of one.
 */

#include <console_shell.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/keymap.h>
#include <astra/runtime.h>
#include <astra/shell.h>
#include <astra/supervisor.h>
#include <astra/syscall.h>
#include <astra/terminal.h>

#include <ext4.h>

#define SHELL_MOUNT_POINT "/vol/"
#define SHELL_PATH_MAX 256u
#define SHELL_READ_CHUNK 128u

typedef struct ConsoleShell {
    AstraTerminal terminal;
    astra_shell_editor_t editor;
    uint32_t display;
    uint32_t input;
    uint32_t modifiers;
    char directory[SHELL_PATH_MAX];   /* relative to the mount, no leading / */
    int running;
} ConsoleShell;

static ConsoleShell shell;

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

/* Appends with truncation reported, so a long path fails rather than overruns. */
static int shell_append(char *destination, uint32_t capacity, const char *text)
{
    uint32_t length = shell_strlen(destination);
    uint32_t index = 0u;

    while (text[index] != '\0') {
        if (length + 1u >= capacity)
            return 0;
        destination[length++] = text[index++];
    }
    destination[length] = '\0';
    return 1;
}

/*
 * Builds the absolute lwext4 path for a name typed at the prompt. A leading
 * slash is absolute from the mount; "." and ".." are resolved here because
 * lwext4 does not.
 */
static int shell_resolve(const char *name, char *out, uint32_t capacity)
{
    out[0] = '\0';
    if (!shell_append(out, capacity, SHELL_MOUNT_POINT))
        return 0;
    if (name == NULL || name[0] == '\0') {
        return shell_append(out, capacity, shell.directory);
    }
    if (name[0] == '/') {
        return shell_append(out, capacity, name + 1);
    }
    if (!shell_append(out, capacity, shell.directory))
        return 0;
    if (shell.directory[0] != '\0' && !shell_append(out, capacity, "/"))
        return 0;
    return shell_append(out, capacity, name);
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

static void report_errno(const char *what, int code)
{
    astra_terminal_write(&shell.terminal, what);
    astra_terminal_write(&shell.terminal, ": failed, error ");
    write_number((uint32_t)code);
    astra_terminal_putc(&shell.terminal, '\n');
}

static void command_ls(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    ext4_dir directory;
    const ext4_direntry *entry;
    uint32_t shown = 0u;
    int rc;

    if (!shell_resolve(argc > 1 ? argv[1] : NULL, path, sizeof(path))) {
        write_line("ls: path too long");
        return;
    }
    rc = ext4_dir_open(&directory, path);
    if (rc != EOK) {
        report_errno("ls", rc);
        return;
    }
    for (;;) {
        entry = ext4_dir_entry_next(&directory);
        if (entry == NULL)
            break;
        if (entry->name_length == 0u)
            continue;
        /* "." and ".." are real entries; showing them is honest. */
        astra_terminal_write_bytes(&shell.terminal, entry->name,
                                   entry->name_length);
        if (entry->inode_type == EXT4_DE_DIR)
            astra_terminal_putc(&shell.terminal, '/');
        astra_terminal_putc(&shell.terminal, '\n');
        ++shown;
    }
    (void)ext4_dir_close(&directory);
    if (shown == 0u)
        write_line("(empty)");
}

static void command_cd(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    char candidate[SHELL_PATH_MAX];
    ext4_dir directory;
    int rc;

    if (argc < 2 || shell_equal(argv[1], "/")) {
        shell.directory[0] = '\0';
        return;
    }
    if (shell_equal(argv[1], ".."))  {
        uint32_t length = shell_strlen(shell.directory);

        while (length != 0u && shell.directory[length - 1u] != '/')
            --length;
        if (length != 0u)
            --length;
        shell.directory[length] = '\0';
        return;
    }
    if (shell_equal(argv[1], "."))
        return;

    /* Verified before it is adopted, so the prompt never lies. */
    candidate[0] = '\0';
    if (argv[1][0] == '/') {
        if (!shell_append(candidate, sizeof(candidate), argv[1] + 1)) {
            write_line("cd: path too long");
            return;
        }
    } else {
        if (!shell_append(candidate, sizeof(candidate), shell.directory) ||
            (shell.directory[0] != '\0' &&
             !shell_append(candidate, sizeof(candidate), "/")) ||
            !shell_append(candidate, sizeof(candidate), argv[1])) {
            write_line("cd: path too long");
            return;
        }
    }
    if (!shell_resolve(candidate, path, sizeof(path))) {
        write_line("cd: path too long");
        return;
    }
    rc = ext4_dir_open(&directory, path);
    if (rc != EOK) {
        report_errno("cd", rc);
        return;
    }
    (void)ext4_dir_close(&directory);
    (void)memcpy(shell.directory, candidate, sizeof(shell.directory));
}

static void command_cat(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    uint8_t chunk[SHELL_READ_CHUNK];
    ext4_file file;
    size_t moved = 0u;
    int rc;

    if (argc < 2) {
        write_line("cat: needs a file");
        return;
    }
    if (!shell_resolve(argv[1], path, sizeof(path))) {
        write_line("cat: path too long");
        return;
    }
    rc = ext4_fopen(&file, path, "rb");
    if (rc != EOK) {
        report_errno("cat", rc);
        return;
    }
    for (;;) {
        rc = ext4_fread(&file, chunk, sizeof(chunk), &moved);
        if (rc != EOK) {
            report_errno("cat", rc);
            break;
        }
        if (moved == 0u)
            break;
        astra_terminal_write_bytes(&shell.terminal, chunk, moved);
    }
    (void)ext4_fclose(&file);
    astra_terminal_putc(&shell.terminal, '\n');
}

static void command_mkdir(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    int rc;

    if (argc < 2) {
        write_line("mkdir: needs a name");
        return;
    }
    if (!shell_resolve(argv[1], path, sizeof(path))) {
        write_line("mkdir: path too long");
        return;
    }
    rc = ext4_dir_mk(path);
    if (rc != EOK)
        report_errno("mkdir", rc);
}

/* write NAME TEXT... -- creates or truncates, then writes the rest of the line. */
static void command_write(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    ext4_file file;
    size_t moved = 0u;
    int rc;
    int index;

    if (argc < 2) {
        write_line("write: needs a name");
        return;
    }
    if (!shell_resolve(argv[1], path, sizeof(path))) {
        write_line("write: path too long");
        return;
    }
    rc = ext4_fopen(&file, path, "wb");
    if (rc != EOK) {
        report_errno("write", rc);
        return;
    }
    for (index = 2; index < argc; ++index) {
        uint32_t length = shell_strlen(argv[index]);

        rc = ext4_fwrite(&file, argv[index], length, &moved);
        if (rc != EOK) {
            report_errno("write", rc);
            break;
        }
        if (moved != length) {
            /* A short write is not an error code, so say what happened. */
            astra_terminal_write(&shell.terminal, "write: short, ");
            write_number((uint32_t)moved);
            astra_terminal_write(&shell.terminal, " of ");
            write_number(length);
            astra_terminal_putc(&shell.terminal, '\n');
            break;
        }
        rc = ext4_fwrite(&file, index + 1 < argc ? " " : "\n", 1u, &moved);
        if (rc != EOK) {
            report_errno("write", rc);
            break;
        }
    }
    rc = ext4_fclose(&file);
    if (rc != EOK)
        report_errno("write", rc);
}

static void command_rm(int argc, char *const *argv)
{
    char path[SHELL_PATH_MAX];
    int rc;

    if (argc < 2) {
        write_line("rm: needs a name");
        return;
    }
    if (!shell_resolve(argv[1], path, sizeof(path))) {
        write_line("rm: path too long");
        return;
    }
    rc = ext4_fremove(path);
    if (rc != EOK)
        report_errno("rm", rc);
}

static void command_help(void)
{
    write_line("commands: ls [dir], cd [dir], cat FILE, mkdir DIR,");
    write_line("          write FILE TEXT..., rm FILE, pwd, clear, help");
}

static void prompt(void)
{
    astra_terminal_write(&shell.terminal, "astra:/");
    astra_terminal_write(&shell.terminal, shell.directory);
    astra_terminal_write(&shell.terminal, "> ");
}

static void run_line(const char *line)
{
    astra_shell_words_t words;

    if (astra_shell_parse(line, &words) != ASTRA_SHELL_OK || words.argc == 0)
        return;

    if (shell_equal(words.argv[0], "help"))
        command_help();
    else if (shell_equal(words.argv[0], "clear"))
        astra_terminal_clear(&shell.terminal);
    else if (shell_equal(words.argv[0], "pwd")) {
        astra_terminal_putc(&shell.terminal, '/');
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
    else {
        astra_terminal_write(&shell.terminal, words.argv[0]);
        write_line(": not a command");
    }
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

void console_shell_run(uint32_t display, uint32_t input,
                       int volume_ready)
{
    AstraInputEvent events[ASTRA_INPUT_READ_BATCH_MAX];
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
    if (astra_terminal_init(&shell.terminal, columns, rows, render_cells,
                            &shell) != ASTRA_TERMINAL_OK) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return;
    }
    astra_shell_editor_init(&shell.editor);

    astra_terminal_clear(&shell.terminal);
    write_line("Astra 68");
    write_line(volume_ready ? "volume: mounted at /" :
                             "volume: not mounted, file commands will fail");
    command_help();
    prompt();
    if (astra_terminal_flush(&shell.terminal) != ASTRA_TERMINAL_OK) {
        (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
        return;
    }
    (void)astra_progress(ASTRA_SUPERVISOR_STAGE_TERMINAL);

    while (shell.running) {
        uint32_t count = 0u;
        uint32_t flags = 0u;
        uint32_t index;

        uint32_t status;

        if (input == 0u) {
            (void)astra_yield();
            continue;
        }
        status = astra_input_read(input, events, ASTRA_INPUT_READ_BATCH_MAX,
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
            continue;
        }
        if (status != ASTRA_SYSCALL_OK) {
            (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
            return;
        }
        for (index = 0u; index < count; ++index) {
            uint32_t header = events[index].header;
            uint32_t usage = events[index].value;
            int pressed =
                (ASTRA_INPUT_EVENT_FLAGS(header) & ASTRA_INPUT_FLAG_DOWN) !=
                0u;

            if (ASTRA_INPUT_EVENT_CLASS(header) != ASTRA_INPUT_CLASS_KEYBOARD)
                continue;
            if (astra_keymap_is_modifier(usage)) {
                shell.modifiers = astra_keymap_apply_modifier(
                    shell.modifiers, usage, pressed);
                continue;
            }
            /* Releases move no cursor and type no character. */
            if (!pressed)
                continue;
            feed_key(astra_keymap_translate(usage, shell.modifiers));
        }
        if (count != 0u &&
            astra_terminal_flush(&shell.terminal) != ASTRA_TERMINAL_OK) {
            (void)astra_progress(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
            return;
        }
        if (count == 0u)
            (void)astra_yield();
    }
}
