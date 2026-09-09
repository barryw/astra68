#include "../process_table.h"

#include <astra/posix_process.h>
#include <astra/status.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VisitLog {
    uint32_t count;
    uint32_t handle;
    int32_t process;
} VisitLog;

static uint32_t
visit(void *context, uint32_t handle, int32_t process)
{
    VisitLog *log = context;

    ++log->count;
    log->handle = handle;
    log->process = process;
    return ASTRA_STATUS_OK;
}

int
main(void)
{
    PosixProcessEntry entries[4];
    PosixSessionEntry sessions[4];
    PosixProcessEntry entry;
    PosixProcessTable table;
    VisitLog log = {0};
    uint32_t handle = 0u;
    uint32_t matched = 0u;

    assert(posix_process_table_init(&table, entries, sessions, 4u) ==
           ASTRA_STATUS_OK);
    assert(posix_process_register(
               &table, 100, 200, 7u,
               ASTRA_POSIX_PROCESS_NEW_SESSION) == ASTRA_STATUS_OK);
    assert(posix_process_query(&table, 200, &entry) == ASTRA_STATUS_OK);
    assert(entry.parent == 0 && entry.group == 200 && entry.session == 200);
    assert(posix_process_tty_attach(&table, 201) == ASTRA_STATUS_ACCESS);
    assert(posix_process_tty_attach(&table, 200) == ASTRA_STATUS_OK);
    assert(posix_process_tty_foreground(&table, 200, &entry.session,
                                        &entry.group) == ASTRA_STATUS_OK);
    assert(entry.session == 200 && entry.group == 200);
    assert(posix_process_register(&table, 200, 201, 8u, 0u) ==
           ASTRA_STATUS_OK);
    assert(posix_process_query(&table, 201, &entry) == ASTRA_STATUS_OK);
    assert(entry.parent == 200 && entry.group == 200 && entry.session == 200);
    assert(posix_process_register(&table, 999, 202, 9u, 0u) ==
           ASTRA_STATUS_ACCESS);
    assert(posix_process_register(&table, 200, 201, 9u, 0u) ==
           ASTRA_STATUS_EXISTS);

    assert(posix_process_setpgid(&table, 200, 201, 201) == ASTRA_STATUS_OK);
    assert(posix_process_tty_set_foreground(&table, 200, 201) ==
           ASTRA_STATUS_OK);
    assert(posix_process_tty_foreground(&table, 201, &entry.session,
                                        &entry.group) == ASTRA_STATUS_OK);
    assert(entry.session == 200 && entry.group == 201);
    assert(posix_process_tty_set_foreground(&table, 201, 999) ==
           ASTRA_STATUS_ACCESS);
    assert(posix_process_visit(&table, 200, -201, visit, &log, &matched) ==
           ASTRA_STATUS_OK);
    assert(matched == 1u && log.count == 1u && log.handle == 8u &&
           log.process == 201);

    log = (VisitLog){0};
    assert(posix_process_visit(&table, 201, 0, visit, &log, &matched) ==
           ASTRA_STATUS_OK);
    assert(matched == 1u && log.process == 201);
    assert(posix_process_setsid(&table, 201, NULL) == ASTRA_STATUS_BUSY);
    assert(posix_process_setpgid(&table, 201, 0, 200) == ASTRA_STATUS_OK);
    assert(posix_process_setsid(&table, 201, &entry) == ASTRA_STATUS_OK);
    assert(entry.session == 201 && entry.group == 201);
    assert(posix_process_tty_foreground(&table, 201, NULL, NULL) ==
           ASTRA_STATUS_NOT_FOUND);

    assert(posix_process_remove(&table, 200, &handle) == ASTRA_STATUS_OK);
    assert(handle == 7u);
    assert(posix_process_query(&table, 201, &entry) == ASTRA_STATUS_OK);
    assert(entry.parent == 0);
    assert(posix_process_visit(&table, 201, 999, visit, &log, &matched) ==
           ASTRA_STATUS_NOT_FOUND);
    assert(posix_process_remove(&table, 999, &handle) ==
           ASTRA_STATUS_NOT_FOUND);
    return 0;
}
