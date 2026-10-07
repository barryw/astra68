#include "../log/log_filter.h"
#include <assert.h>
#include <stdio.h>

static const char line[] =
    "2026-10-05T14:03:22.117-04:00 warning chocolate-doom[10000011] stderr: "
    "MIDI device not found";

int main(void)
{
    LogFilter f = {0};
    int64_t s;

    assert(log_filter_line_time(line, &s) && s == 1791223402);
    assert(log_filter_duration("10m", &s) && s == 600);
    assert(log_filter_duration("2h", &s) && s == 7200);
    assert(log_filter_duration("30s", &s) && s == 30);
    assert(log_filter_duration("1d", &s) && s == 86400);
    assert(!log_filter_duration("10x", &s) && !log_filter_duration("", &s));
    assert(!log_filter_duration("m", &s) && !log_filter_duration("10mm", &s));
    assert(!log_filter_line_time("boot+12.345 notice x[00000001] log: y", &s));
    assert(!log_filter_line_time("2026-13-05T14:03:22.117-04:00 x", &s));
    assert(log_filter_line_time("1970-01-01T00:00:00.000+00:00 x", &s) &&
           s == 0);
    assert(log_filter_line_time("2024-02-29T23:59:59.999+05:30 x", &s) &&
           s == 1709231399);
    assert(log_filter_level("debug") == 0 && log_filter_level("error") == 4 &&
           log_filter_level("loud") == -1);
    assert(log_filter_match(&f, line));
    f.has_level = 1; f.level = 4; assert(!log_filter_match(&f, line));
    f.level = 3; assert(log_filter_match(&f, line));
    f.program = "chocolate-doom"; assert(log_filter_match(&f, line));
    f.program = "chocolate"; assert(!log_filter_match(&f, line));
    f.program = NULL; f.has_pid = 1; f.pid = 0x10000011u;
    assert(log_filter_match(&f, line));
    f.pid = 0x10000012u; assert(!log_filter_match(&f, line));
    f.has_pid = 0; f.stream = "stderr"; assert(log_filter_match(&f, line));
    f.stream = "stdout"; assert(!log_filter_match(&f, line));
    f.stream = NULL; f.grep = "MIDI"; assert(log_filter_match(&f, line));
    f.grep = "midi"; assert(!log_filter_match(&f, line)); /* case-sensitive */
    f.grep = "stderr"; assert(!log_filter_match(&f, line)); /* text only */
    f.grep = NULL; f.has_since = 1; f.since_unix_s = 1791223402;
    assert(log_filter_match(&f, line));
    f.since_unix_s = 1791223403; assert(!log_filter_match(&f, line));
    assert(log_filter_is_boot_marker("--- boot 2026-10-05T14:03:20-04:00 build 1a2b3c4d ---"));
    assert(!log_filter_is_boot_marker("--- events service restarted x ---"));
    assert(!log_filter_is_boot_marker(line));
    /* A boot-relative line has no wall time: --last cannot exclude it. */
    f.since_unix_s = 1791223403;
    f.has_level = 0;    /* still "warning" from above; this line is notice */
    assert(log_filter_match(&f, "boot+12.345 notice x[00000001] log: y"));
    /* A marker is not a record: only an empty filter passes it. */
    assert(!log_filter_match(&f, "--- boot 2026-10-05T14:03:20.000-04:00 ---"));
    {
        LogFilter none = {0};

        assert(log_filter_match(&none,
                                "--- boot 2026-10-05T14:03:20.000-04:00 ---"));
        /* An ident with brackets of its own: the pid is the last pair. */
        none.has_pid = 1; none.pid = 0x2au;
        none.program = "a[b]";
        assert(log_filter_match(&none,
                                "boot+1.000 info a[b][0000002a] log: z"));
        /* Malformed records pass no filter. */
        assert(!log_filter_match(&none, "boot+1.000 info a[zz] log: z"));
        assert(!log_filter_match(&none, "boot+1.000 info"));
    }
    puts("LOG FILTER PASS");
    return 0;
}
