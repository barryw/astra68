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

static int find(const char *name)
{
    for (int i = 0; i < 8; ++i)
        if (files[i].used && strcmp(files[i].name, name) == 0) return i;
    return -1;
}
static int fs_size(void *c, const char *n, uint64_t *b)
{ (void)c; int i = find(n); *b = i < 0 ? 0u : files[i].length; return 1; }
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
    (void)c; int i = find(f), j = find(t);
    if (i < 0) return 0;
    if (j >= 0) files[j].used = 0;
    strcpy(files[i].name, t); return 1;
}
static int fs_remove(void *c, const char *n)
{ (void)c; int i = find(n); if (i >= 0) files[i].used = 0; return 1; }

static const AstraLogFileOps ops = { NULL, fs_size, fs_append, fs_rename,
                                     fs_remove };

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

static void
test_rotation(void)
{
    AstraLogFile file;
    char line[100];

    memset(files, 0, sizeof(files));
    memset(line, 'a', sizeof(line) - 1u); line[sizeof(line) - 1u] = '\n';
    assert(astra_log_file_open(&file, &ops, "syslog", 250u, 3u));
    assert(astra_log_file_append(&file, line, 100u, 20261005u));
    assert(astra_log_file_append(&file, line, 100u, 20261005u));
    assert(find("syslog.1") < 0);
    /* Third line would cross 250 bytes: rotate first. */
    assert(astra_log_file_append(&file, line, 100u, 20261005u));
    assert(find("syslog.1") >= 0 && files[find("syslog.1")].length == 200u);
    assert(files[find("syslog")].length == 100u);
    /* New local day rotates even when small. */
    assert(astra_log_file_append(&file, line, 100u, 20261006u));
    assert(files[find("syslog.2")].length == 200u);
    assert(files[find("syslog.1")].length == 100u);
    /* Keep only 3: the oldest falls off. */
    assert(astra_log_file_append(&file, line, 100u, 20261007u));
    assert(astra_log_file_append(&file, line, 100u, 20261008u));
    assert(find("syslog.4") < 0 && find("syslog.3") >= 0);
    /* A batch larger than max_bytes is written whole into a fresh file. */
    {
        char big[300]; memset(big, 'b', sizeof(big));
        assert(astra_log_file_append(&file, big, 300u, 20261008u));
        assert(files[find("syslog")].length == 300u);
    }
    /* Reopen picks up the existing size. */
    assert(astra_log_file_open(&file, &ops, "syslog", 250u, 3u));
    assert(file.bytes == 300u);
}

int
main(void)
{
    test_format();
    test_rotation();
    puts("LOG FILE PASS");
    return 0;
}
