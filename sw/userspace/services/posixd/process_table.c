#include "process_table.h"

#include <astra/posix_process.h>
#include <astra/status.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

static PosixProcessEntry *
find(const PosixProcessTable *table, int32_t process)
{
    if (table == NULL || table->entries == NULL || process <= 0)
        return NULL;
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->entries[index].process == process)
            return &table->entries[index];
    return NULL;
}

static PosixSessionEntry *
find_session(const PosixProcessTable *table, int32_t session)
{
    if (table == NULL || table->sessions == NULL || session <= 0)
        return NULL;
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->sessions[index].session == session)
            return &table->sessions[index];
    return NULL;
}

static PosixSessionEntry *
new_session(PosixProcessTable *table, int32_t session)
{
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->sessions[index].session == 0) {
            table->sessions[index].session = session;
            return &table->sessions[index];
        }
    return NULL;
}

static void
remove_empty_session(PosixProcessTable *table, int32_t session)
{
    PosixSessionEntry *entry;

    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->entries[index].process != 0 &&
            table->entries[index].session == session)
            return;
    entry = find_session(table, session);
    if (entry != NULL)
        memset(entry, 0, sizeof(*entry));
}

static int
group_in_session(const PosixProcessTable *table, int32_t session,
                 int32_t group)
{
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->entries[index].process != 0 &&
            table->entries[index].session == session &&
            table->entries[index].group == group)
            return 1;
    return 0;
}

uint32_t
posix_process_table_init(PosixProcessTable *table,
                         PosixProcessEntry *entries,
                         PosixSessionEntry *sessions, uint32_t capacity)
{
    if (table == NULL || entries == NULL || sessions == NULL || capacity == 0u)
        return ASTRA_STATUS_INVALID;
    memset(entries, 0, capacity * sizeof(*entries));
    memset(sessions, 0, capacity * sizeof(*sessions));
    table->entries = entries;
    table->sessions = sessions;
    table->capacity = capacity;
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_register(PosixProcessTable *table, int32_t sender,
                       int32_t process, uint32_t handle, uint32_t flags)
{
    PosixProcessEntry *parent = NULL;
    PosixProcessEntry *slot = NULL;
    PosixSessionEntry *session = NULL;

    if (table == NULL || table->entries == NULL || sender <= 0 ||
        process <= 0 || sender == process || handle == 0u ||
        (flags & ~ASTRA_POSIX_PROCESS_FLAG_MASK) != 0u)
        return ASTRA_STATUS_INVALID;
    if (find(table, process) != NULL)
        return ASTRA_STATUS_EXISTS;
    if ((flags & ASTRA_POSIX_PROCESS_NEW_SESSION) == 0u) {
        parent = find(table, sender);
        if (parent == NULL)
            return ASTRA_STATUS_ACCESS;
        session = find_session(table, parent->session);
        if (session == NULL)
            return ASTRA_STATUS_INVALID;
    }
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->entries[index].process == 0) {
            slot = &table->entries[index];
            break;
        }
    if (slot == NULL)
        return ASTRA_STATUS_LIMIT;
    if (parent == NULL) {
        session = new_session(table, process);
        if (session == NULL)
            return ASTRA_STATUS_LIMIT;
    }
    slot->handle = handle;
    slot->process = process;
    if (parent == NULL) {
        slot->parent = 0;
        slot->group = process;
        slot->session = process;
    } else {
        slot->parent = parent->process;
        slot->group = parent->group;
        slot->session = parent->session;
    }
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_remove(PosixProcessTable *table, int32_t process,
                     uint32_t *handle)
{
    PosixProcessEntry *entry = find(table, process);
    int32_t session;

    if (handle != NULL)
        *handle = 0u;
    if (entry == NULL)
        return ASTRA_STATUS_NOT_FOUND;
    if (handle != NULL)
        *handle = entry->handle;
    session = entry->session;
    memset(entry, 0, sizeof(*entry));
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->entries[index].parent == process)
            table->entries[index].parent = 0;
    remove_empty_session(table, session);
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_query(const PosixProcessTable *table, int32_t process,
                    PosixProcessEntry *entry)
{
    PosixProcessEntry *found = find(table, process);

    if (found == NULL)
        return ASTRA_STATUS_NOT_FOUND;
    if (entry != NULL)
        *entry = *found;
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_setpgid(PosixProcessTable *table, int32_t sender,
                      int32_t process, int32_t group)
{
    PosixProcessEntry *caller = find(table, sender);
    PosixProcessEntry *target;
    int group_exists = 0;

    if (caller == NULL)
        return ASTRA_STATUS_ACCESS;
    if (process == 0)
        process = sender;
    if (group == 0)
        group = process;
    if (process <= 0 || group <= 0)
        return ASTRA_STATUS_INVALID;
    target = find(table, process);
    if (target == NULL)
        return ASTRA_STATUS_NOT_FOUND;
    if (target != caller && target->parent != sender)
        return ASTRA_STATUS_ACCESS;
    if (target->session != caller->session || target->session == target->process)
        return ASTRA_STATUS_ACCESS;
    if (group == target->process)
        group_exists = 1;
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->entries[index].process != 0 &&
            table->entries[index].group == group &&
            table->entries[index].session == target->session) {
            group_exists = 1;
            break;
        }
    if (!group_exists)
        return ASTRA_STATUS_NOT_FOUND;
    target->group = group;
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_setsid(PosixProcessTable *table, int32_t sender,
                     PosixProcessEntry *entry)
{
    PosixProcessEntry *caller = find(table, sender);
    PosixSessionEntry *session;
    int32_t old_session;

    if (caller == NULL)
        return ASTRA_STATUS_ACCESS;
    for (uint32_t index = 0u; index < table->capacity; ++index)
        if (table->entries[index].process != 0 &&
            table->entries[index].group == sender)
            return ASTRA_STATUS_BUSY;
    session = new_session(table, sender);
    if (session == NULL)
        return ASTRA_STATUS_LIMIT;
    old_session = caller->session;
    caller->group = sender;
    caller->session = sender;
    remove_empty_session(table, old_session);
    if (entry != NULL)
        *entry = *caller;
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_visit(const PosixProcessTable *table, int32_t sender,
                    int32_t selector, PosixProcessVisit visit,
                    void *context, uint32_t *matched)
{
    int32_t group = 0;
    uint32_t count = 0u;

    if (matched != NULL)
        *matched = 0u;
    if (table == NULL || table->entries == NULL || visit == NULL ||
        selector == INT32_MIN)
        return ASTRA_STATUS_INVALID;
    if (selector == 0) {
        PosixProcessEntry *caller = find(table, sender);

        if (caller == NULL)
            return ASTRA_STATUS_ACCESS;
        group = caller->group;
    } else if (selector < -1) {
        group = -selector;
    }
    for (uint32_t index = 0u; index < table->capacity; ++index) {
        const PosixProcessEntry *entry = &table->entries[index];
        uint32_t status;

        if (entry->process == 0 ||
            (selector > 0 && entry->process != selector) ||
            (group != 0 && entry->group != group))
            continue;
        status = visit(context, entry->handle, entry->process);
        ++count;
        if (status != ASTRA_STATUS_OK) {
            if (matched != NULL)
                *matched = count;
            return status;
        }
    }
    if (matched != NULL)
        *matched = count;
    return count != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_NOT_FOUND;
}

uint32_t
posix_process_tty_attach(PosixProcessTable *table, int32_t sender)
{
    PosixProcessEntry *caller = find(table, sender);
    PosixSessionEntry *session;

    if (caller == NULL || caller->session != sender)
        return ASTRA_STATUS_ACCESS;
    session = find_session(table, caller->session);
    if (session == NULL)
        return ASTRA_STATUS_INVALID;
    if (session->terminal != 0u)
        return ASTRA_STATUS_BUSY;
    session->foreground = caller->group;
    session->terminal = 1u;
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_tty_foreground(const PosixProcessTable *table, int32_t sender,
                             int32_t *session_id, int32_t *group)
{
    PosixProcessEntry *caller = find(table, sender);
    PosixSessionEntry *session;

    if (caller == NULL)
        return ASTRA_STATUS_ACCESS;
    session = find_session(table, caller->session);
    if (session == NULL || session->terminal == 0u)
        return ASTRA_STATUS_NOT_FOUND;
    if (session_id != NULL)
        *session_id = session->session;
    if (group != NULL)
        *group = session->foreground;
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_tty_set_foreground(PosixProcessTable *table, int32_t sender,
                                 int32_t group)
{
    PosixProcessEntry *caller = find(table, sender);
    PosixSessionEntry *session;

    if (caller == NULL)
        return ASTRA_STATUS_ACCESS;
    session = find_session(table, caller->session);
    if (session == NULL || session->terminal == 0u)
        return ASTRA_STATUS_NOT_FOUND;
    if (group <= 0)
        return ASTRA_STATUS_INVALID;
    if (!group_in_session(table, caller->session, group))
        return ASTRA_STATUS_ACCESS;
    session->foreground = group;
    return ASTRA_STATUS_OK;
}

uint32_t
posix_process_visit_session_group(const PosixProcessTable *table,
                                  int32_t session, int32_t group,
                                  PosixProcessVisit visit, void *context,
                                  uint32_t *matched)
{
    uint32_t count = 0u;

    if (matched != NULL)
        *matched = 0u;
    if (table == NULL || table->entries == NULL || session <= 0 || group <= 0 ||
        visit == NULL)
        return ASTRA_STATUS_INVALID;
    for (uint32_t index = 0u; index < table->capacity; ++index) {
        const PosixProcessEntry *entry = &table->entries[index];
        uint32_t status;

        if (entry->process == 0 || entry->session != session ||
            entry->group != group)
            continue;
        status = visit(context, entry->handle, entry->process);
        ++count;
        if (status != ASTRA_STATUS_OK) {
            if (matched != NULL)
                *matched = count;
            return status;
        }
    }
    if (matched != NULL)
        *matched = count;
    return count != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_NOT_FOUND;
}
