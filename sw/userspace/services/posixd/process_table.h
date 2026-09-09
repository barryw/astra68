#ifndef ASTRA_POSIX_PROCESS_TABLE_H
#define ASTRA_POSIX_PROCESS_TABLE_H

#include <stdint.h>

typedef struct PosixProcessEntry {
    uint32_t handle;
    int32_t process;
    int32_t parent;
    int32_t group;
    int32_t session;
} PosixProcessEntry;

typedef struct PosixSessionEntry {
    int32_t session;
    int32_t foreground;
    uint8_t terminal;
    uint8_t reserved[3];
} PosixSessionEntry;

typedef struct PosixProcessTable {
    PosixProcessEntry *entries;
    PosixSessionEntry *sessions;
    uint32_t capacity;
} PosixProcessTable;

typedef uint32_t (*PosixProcessVisit)(void *context, uint32_t handle,
                                      int32_t process);

uint32_t posix_process_table_init(PosixProcessTable *table,
                                  PosixProcessEntry *entries,
                                  PosixSessionEntry *sessions,
                                  uint32_t capacity);
uint32_t posix_process_register(PosixProcessTable *table, int32_t sender,
                                int32_t process, uint32_t handle,
                                uint32_t flags);
uint32_t posix_process_remove(PosixProcessTable *table, int32_t process,
                              uint32_t *handle);
uint32_t posix_process_query(const PosixProcessTable *table, int32_t process,
                             PosixProcessEntry *entry);
uint32_t posix_process_setpgid(PosixProcessTable *table, int32_t sender,
                               int32_t process, int32_t group);
uint32_t posix_process_setsid(PosixProcessTable *table, int32_t sender,
                              PosixProcessEntry *entry);
uint32_t posix_process_visit(const PosixProcessTable *table, int32_t sender,
                             int32_t selector, PosixProcessVisit visit,
                             void *context, uint32_t *matched);
uint32_t posix_process_tty_attach(PosixProcessTable *table, int32_t sender);
uint32_t posix_process_tty_foreground(const PosixProcessTable *table,
                                      int32_t sender, int32_t *session,
                                      int32_t *group);
uint32_t posix_process_tty_set_foreground(PosixProcessTable *table,
                                          int32_t sender, int32_t group);
uint32_t posix_process_visit_session_group(
    const PosixProcessTable *table, int32_t session, int32_t group,
    PosixProcessVisit visit, void *context, uint32_t *matched);

#endif
