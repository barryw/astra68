#include <astra/log_file.h>
#include <astra/log_format.h>
#include <astra/event.h>
#include <astra/log_ring.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* In-memory directory of up to 8 files. */
static struct { char name[32]; char data[4096]; uint32_t length; int used; }
    files[8];

/* Rename-failure injection for test_rotate_failure(): fails the Nth call to
 * fs_rename() (1-based), or never when 0. */
static int fail_rename_at = 0;
static int rename_call_count = 0;

/* Size-failure injection for test_open_retry(): fs_size_flaky() fails its
 * next `fail_size_remaining` calls, then behaves like fs_size(). */
static int fail_size_remaining = 0;

static int find(const char *name)
{
    for (int i = 0; i < 8; ++i)
        if (files[i].used && strcmp(files[i].name, name) == 0) return i;
    return -1;
}
static int fs_size(void *c, const char *n, uint64_t *b)
{ (void)c; int i = find(n); *b = i < 0 ? 0u : files[i].length; return 1; }
static int fs_size_flaky(void *c, const char *n, uint64_t *b)
{
    if (fail_size_remaining > 0) {
        --fail_size_remaining;
        *b = 0u;
        return 0;
    }
    return fs_size(c, n, b);
}
static int fs_append(void *c, const char *n, const void *b, uint32_t l)
{
    (void)c; int i = find(n);
    if (i < 0) { for (i = 0; files[i].used; ++i) {} files[i].used = 1;
                 strcpy(files[i].name, n); files[i].length = 0; }
    assert(files[i].length + l <= sizeof(files[i].data));
    memcpy(files[i].data + files[i].length, b, l); files[i].length += l;
    return 1;
}
static int fs_rename(void *c, const char *f, const char *t)
{
    (void)c;
    ++rename_call_count;
    if (fail_rename_at != 0 && rename_call_count == fail_rename_at)
        return 0;
    {
        /* An absent source is success: nothing was there to rename, which is
         * exactly what a shift of a not-yet-existing generation expects. */
        int i = find(f), j = find(t);
        if (i < 0) return 1;
        if (j >= 0) files[j].used = 0;
        strcpy(files[i].name, t);
        return 1;
    }
}
static const AstraLogFileOps ops = { NULL, fs_size, fs_append, fs_rename };
static const AstraLogFileOps ops_flaky_size = { NULL, fs_size_flaky, fs_append,
                                                fs_rename };

/* Test-only: seed `name` with exactly `data`, overwriting any prior content.
 * fs_append() already creates an absent file; reset length first so a
 * pre-existing one is replaced instead of appended to. */
static void seed(const char *name, const char *data)
{
    int i = find(name);
    if (i >= 0) files[i].length = 0;
    fs_append(NULL, name, data, (uint32_t)strlen(data));
}

static int content_is(const char *name, const char *expected)
{
    int i = find(name);
    return i >= 0 && files[i].length == strlen(expected) &&
           memcmp(files[i].data, expected, files[i].length) == 0;
}

static void
test_format(void)
{
    char out[512];
    AstraLogLine line = {
        .monotonic_ns = 12345000000ull,
        /* 2026-10-05T18:03:22.117Z */
        .wall_ns = 1791223402117000000ull,
        .utc_offset = -14400, .zone = 0x45445400u, .wall_valid = 1,
        .level = ASTRA_EVENT_LEVEL_NOTICE, .stream = ASTRA_LOG_STREAM_STDOUT,
        .pid = 0x10000011u, .ident = "chocolate-doom",
        .text = "V_Init: allocate screens.", .length = 25u };
    uint32_t n = astra_log_format(out, sizeof(out), &line);

    out[n] = '\0';
    assert(strcmp(out, "2026-10-05T14:03:22.117-04:00 notice "
                       "chocolate-doom[10000011] stdout: "
                       "V_Init: allocate screens.\n") == 0);
    assert(astra_log_local_day(&line) == 20261005u);

    line.text = "bell\a and\nnewline"; line.length = 17u;
    line.level = ASTRA_EVENT_LEVEL_ERROR; line.stream = ASTRA_LOG_STREAM_STDERR;
    n = astra_log_format(out, sizeof(out), &line); out[n] = '\0';
    assert(strstr(out, " error chocolate-doom[10000011] stderr: "
                       "bell\\x07 and\\x0anewline\n") != NULL);

    /* No wall clock: boot-relative time. */
    line.wall_valid = 0;
    n = astra_log_format(out, sizeof(out), &line); out[n] = '\0';
    assert(strncmp(out, "boot+12.345 ", 12) == 0);
    assert(astra_log_local_day(&line) == 0u);

    /* Truncation marker and a tiny buffer never overflow. */
    line.truncated = 1; line.text = "x"; line.length = 1u;
    n = astra_log_format(out, sizeof(out), &line); out[n] = '\0';
    assert(strstr(out, "stderr: x [truncated]\n") != NULL);
    assert(astra_log_format(out, 8u, &line) <= 8u);
}

/* Fix 1: ident comes from argv[0]'s basename, which can hold any byte. An
 * unescaped newline or "] stdout: " could split a line or forge the next
 * one, so ident must go through the same escape as text. */
static void
test_ident_escape(void)
{
    char out[256];
    AstraLogLine line = {
        .monotonic_ns = 0ull, .wall_valid = 0,
        .level = ASTRA_EVENT_LEVEL_INFO, .stream = ASTRA_LOG_STREAM_LOG,
        .pid = 1u, .ident = "evil\nX", .text = "ok", .length = 2u };
    uint32_t n = astra_log_format(out, sizeof(out), &line);

    out[n] = '\0';
    /* Exactly one line: the ident's raw newline must never appear as one. */
    for (uint32_t i = 0u; i + 1u < n; ++i)
        assert(out[i] != '\n');
    assert(n > 0u && out[n - 1u] == '\n');
    assert(strstr(out, "evil\\x0aX") != NULL);

    line.ident = "evil\\dir";
    n = astra_log_format(out, sizeof(out), &line);
    out[n] = '\0';
    for (uint32_t i = 0u; i + 1u < n; ++i)
        assert(out[i] != '\n');
    assert(strstr(out, "evil\\\\dir") != NULL);
}

/* Fix 2: only control bytes, DEL and backslash are escaped. A byte from 0x80
 * up -- a UTF-8 continuation byte among them -- must pass through
 * byte-identical, or multi-byte text would come out corrupted. */
static void
test_utf8_passthrough(void)
{
    static const char cafe[] = { 'c', 'a', 'f', (char)0xc3, (char)0xa9 };
    static const char needle[] =
        { 'c', 'a', 'f', (char)0xc3, (char)0xa9, '\0' };
    char out[256];
    AstraLogLine line = {
        .monotonic_ns = 0ull, .wall_valid = 0,
        .level = ASTRA_EVENT_LEVEL_INFO, .stream = ASTRA_LOG_STREAM_LOG,
        .pid = 1u, .ident = "ok", .text = cafe, .length = sizeof(cafe) };
    uint32_t n = astra_log_format(out, sizeof(out), &line);

    out[n] = '\0';
    assert(strstr(out, needle) != NULL);
}

/* Fix 6: a buffer too small for a whole `\xNN` (or `\\`) must stop before
 * writing any of it, never a prefix -- a dangling "\x0" read back looks like
 * a different, shorter escape, not like truncated text. */
static void
test_escape_truncation(void)
{
    char out[512];
    AstraLogLine line = {
        .monotonic_ns = 12345000000ull, .wall_valid = 0,
        .level = ASTRA_EVENT_LEVEL_ERROR, .stream = ASTRA_LOG_STREAM_STDERR,
        .pid = 0x10000011u, .ident = "chocolate-doom",
        .text = "\a", .length = 1u };
    uint32_t escape_at;
    uint32_t capacity;

    astra_log_format(out, sizeof(out), &line);
    for (escape_at = 0u; out[escape_at] != '\\'; ++escape_at) {}

    /* escape_at .. escape_at+5 sweeps through every point at which capacity
     * ends exactly inside, or exactly after, the "\a" -> "\x07" escape. */
    for (capacity = escape_at; capacity <= escape_at + 5u; ++capacity) {
        uint32_t n = astra_log_format(out, capacity, &line);
        uint32_t end = n;

        assert(n <= capacity);
        if (end > 0u && out[end - 1u] == '\n')
            --end;
        assert(!(end >= 1u && out[end - 1u] == '\\'));
        assert(!(end >= 2u && out[end - 2u] == '\\' && out[end - 1u] == 'x'));
        assert(!(end >= 3u && out[end - 3u] == '\\' && out[end - 2u] == 'x'));
    }
}

static void
test_rotation(void)
{
    AstraLogFile file;
    char line[100];

    memset(files, 0, sizeof(files));
    fail_rename_at = 0;
    rename_call_count = 0;
    memset(line, 'a', sizeof(line) - 1u); line[sizeof(line) - 1u] = '\n';
    assert(astra_log_file_open(&file, &ops, "syslog", 250u, 3u));
    assert(astra_log_file_append(&file, line, 100u, 20261005u) ==
          ASTRA_LOG_FILE_OK);
    assert(astra_log_file_append(&file, line, 100u, 20261005u) ==
          ASTRA_LOG_FILE_OK);
    assert(find("syslog.1") < 0);
    /* Third line would cross 250 bytes: rotate first. */
    assert(astra_log_file_append(&file, line, 100u, 20261005u) ==
          ASTRA_LOG_FILE_OK);
    assert(find("syslog.1") >= 0 && files[find("syslog.1")].length == 200u);
    assert(files[find("syslog")].length == 100u);
    /* New local day rotates even when small. */
    assert(astra_log_file_append(&file, line, 100u, 20261006u) ==
          ASTRA_LOG_FILE_OK);
    assert(files[find("syslog.2")].length == 200u);
    assert(files[find("syslog.1")].length == 100u);
    /* Keep only 3: the oldest falls off. */
    assert(astra_log_file_append(&file, line, 100u, 20261007u) ==
          ASTRA_LOG_FILE_OK);
    assert(astra_log_file_append(&file, line, 100u, 20261008u) ==
          ASTRA_LOG_FILE_OK);
    assert(find("syslog.4") < 0 && find("syslog.3") >= 0);
    /* A batch larger than max_bytes is written whole into a fresh file. */
    {
        char big[300]; memset(big, 'b', sizeof(big));
        assert(astra_log_file_append(&file, big, 300u, 20261008u) ==
              ASTRA_LOG_FILE_OK);
        assert(files[find("syslog")].length == 300u);
    }
    /* Reopen picks up the existing size. */
    assert(astra_log_file_open(&file, &ops, "syslog", 250u, 3u));
    assert(file.bytes == 300u);
}

/* Fix 3: a fresh boot volume has no "syslog" yet. size() must still report
 * success, with bytes == 0, not "absent" as failure. */
static void
test_open_first_boot(void)
{
    AstraLogFile file;

    memset(files, 0, sizeof(files));
    assert(astra_log_file_open(&file, &ops, "syslog", 250u, 3u));
    assert(file.ready);
    assert(file.bytes == 0u);
}

/* Fix 3: a transient size() failure at open must not disable logging for the
 * rest of the boot -- each append retries establishing the file. */
static void
test_open_retry(void)
{
    AstraLogFile file;
    static const char line[] = "hello\n";

    memset(files, 0, sizeof(files));
    /* Fails the call inside open() and the retry inside the first append. */
    fail_size_remaining = 2;

    assert(!astra_log_file_open(&file, &ops_flaky_size, "syslog", 250u, 3u));
    assert(!file.ready);

    /* First append retries size(); still failing, so the batch is not
     * written and the boot is not yet logging -- but it is not disabled. */
    assert(astra_log_file_append(&file, line, sizeof(line) - 1u,
                                 20261005u) == ASTRA_LOG_FILE_FAILED);
    assert(find("syslog") < 0);

    /* Second append retries again; size() now succeeds, so the file becomes
     * ready and this batch is written. */
    assert(astra_log_file_append(&file, line, sizeof(line) - 1u,
                                 20261005u) == ASTRA_LOG_FILE_OK);
    assert(find("syslog") >= 0 &&
          files[find("syslog")].length == sizeof(line) - 1u);
}

/* Fix 4 / fix 5: a real rename failure partway through a shift must stop
 * immediately, and the batch still lands in the current base file instead of
 * being dropped. The shift is three real renames here (syslog.2 -> syslog.3,
 * syslog.1 -> syslog.2, syslog -> syslog.1); failing the first means nothing
 * has moved yet, so every generation is untouched. */
static void
test_rotate_failure_first(void)
{
    AstraLogFile file;

    memset(files, 0, sizeof(files));
    seed("syslog", "BASE");
    seed("syslog.1", "ONE");
    seed("syslog.2", "TWO");
    seed("syslog.3", "THREE");

    assert(astra_log_file_open(&file, &ops, "syslog", 1000u, 3u));
    /* Records today's day without rotating (file->day starts at 0, so no
     * write ever rotates on the day of the very first append). */
    assert(astra_log_file_append(&file, "x", 1u, 20261005u) ==
          ASTRA_LOG_FILE_OK);

    /* A new day forces rotation. Fail the first real rename. */
    rename_call_count = 0;
    fail_rename_at = 1;
    assert(astra_log_file_append(&file, "BATCH", 5u, 20261006u) ==
          ASTRA_LOG_FILE_ROTATE_FAILED);
    fail_rename_at = 0;

    /* Nothing moved: every generation holds its original content. */
    assert(content_is("syslog.1", "ONE"));
    assert(content_is("syslog.2", "TWO"));
    assert(content_is("syslog.3", "THREE"));
    /* The batch still landed in the base file instead of being dropped. */
    assert(content_is("syslog", "BASExBATCH"));
}

/* Same shift, but the first rename (syslog.2 -> syslog.3) succeeds before
 * the second (syslog.1 -> syslog.2) fails. Per the documented guarantee,
 * exactly one generation -- the oldest kept one -- is lost: syslog.3's
 * original content is gone, overwritten by what was syslog.2's. */
static void
test_rotate_failure_second(void)
{
    AstraLogFile file;

    memset(files, 0, sizeof(files));
    seed("syslog", "BASE");
    seed("syslog.1", "ONE");
    seed("syslog.2", "TWO");
    seed("syslog.3", "THREE");

    assert(astra_log_file_open(&file, &ops, "syslog", 1000u, 3u));
    assert(astra_log_file_append(&file, "x", 1u, 20261005u) ==
          ASTRA_LOG_FILE_OK);

    rename_call_count = 0;
    fail_rename_at = 2;
    assert(astra_log_file_append(&file, "BATCH", 5u, 20261006u) ==
          ASTRA_LOG_FILE_ROTATE_FAILED);
    fail_rename_at = 0;

    /* Named exactly: syslog.1 untouched (its rename is the one that failed);
     * syslog.2 no longer exists (its content already moved to slot 3);
     * syslog.3 holds syslog.2's old content, having lost its own -- the one
     * generation a partial shift costs, never more than one. */
    assert(content_is("syslog.1", "ONE"));
    assert(find("syslog.2") < 0);
    assert(content_is("syslog.3", "TWO"));
    /* The batch still landed in the base file instead of being dropped. */
    assert(content_is("syslog", "BASExBATCH"));
}

/* ASTRA_LOG_LINE_BYTES is a bound the drain reserves against, so no input a
 * process can put in a record -- every byte a control byte, the longest
 * ident, the widest timestamp and offset, the longest level and stream names
 * -- may format longer than it. The widest case must hit it exactly, or the
 * constant is not derived from the formatter. */
static void
test_line_bound(void)
{
    static char text[ASTRA_LOG_TEXT_MAX];
    static char out[ASTRA_LOG_LINE_BYTES(ASTRA_LOG_TEXT_MAX) + 64u];
    char ident[ASTRA_LOG_IDENT_MAX];
    static const int32_t offsets[] = { 0, 2147483647, -2147483647 - 1, -18000 };
    static const uint64_t walls[] = { 0u, 1000000000u, UINT64_MAX };

    memset(text, 0x01, sizeof(text));
    memset(ident, 0x7f, sizeof(ident) - 1u);
    ident[sizeof(ident) - 1u] = '\0';
    for (uint32_t w = 0u; w < 3u; ++w)
        for (uint32_t z = 0u; z < 4u; ++z)
            for (uint32_t valid = 0u; valid < 2u; ++valid)
                for (uint32_t level = 0u; level <= 255u; level += 51u)
                    for (uint32_t stream = 0u; stream < 5u; ++stream) {
        AstraLogLine line = {
            .monotonic_ns = UINT64_MAX, .wall_ns = walls[w],
            .utc_offset = offsets[z], .zone = 0x41424344u,
            .wall_valid = (uint8_t)valid, .level = (uint8_t)level,
            .stream = (uint8_t)stream, .truncated = 1u, .pid = 0xffffffffu,
            .ident = ident, .text = text, .length = ASTRA_LOG_TEXT_MAX };
        uint32_t n = astra_log_format(out, sizeof(out), &line);

        assert(n <= ASTRA_LOG_LINE_BYTES(ASTRA_LOG_TEXT_MAX));
        assert(out[n - 1u] == '\n');
    }
    {
        /* The widest line: max wall time, max positive offset, "warning",
         * "stdout", every ident and text byte escaped, truncated. */
        AstraLogLine line = {
            .wall_ns = UINT64_MAX, .utc_offset = 2147483647,
            .zone = 0x41424344u, .wall_valid = 1u,
            .level = ASTRA_EVENT_LEVEL_WARNING,
            .stream = ASTRA_LOG_STREAM_STDOUT, .truncated = 1u, .pid = 1u,
            .ident = ident, .text = text, .length = ASTRA_LOG_TEXT_MAX };

        assert(astra_log_format(out, sizeof(out), &line) ==
               ASTRA_LOG_LINE_BYTES(ASTRA_LOG_TEXT_MAX));
        line.length = 0u;
        assert(astra_log_format(out, sizeof(out), &line) ==
               ASTRA_LOG_LINE_BYTES(0u));
    }
}

int
main(void)
{
    test_line_bound();
    test_format();
    test_ident_escape();
    test_utf8_passthrough();
    test_escape_truncation();
    test_rotation();
    test_open_first_boot();
    test_open_retry();
    test_rotate_failure_first();
    test_rotate_failure_second();
    puts("LOG FILE PASS");
    return 0;
}
