/*
 * The kernel ring's PROCESS_START chains folded into a pid -> name table,
 * and the log service's resume point (docs/SYSTEM_LOG.md section 5).
 */

#include <astra/log_names.h>

#include <astra/crc32.h>
#include <astra/endian.h>

#include <stddef.h>

#define STATE_MAGIC   0x4C4F4752u /* "LOGR" */
#define STATE_VERSION 2u
#define STATE_HEADER  44u
#define STATE_ENTRY   (12u + ASTRA_LOG_IDENT_MAX)

void
astra_log_names_reset(AstraLogNames *names)
{
    uint8_t *bytes = (uint8_t *)names;

    for (uint32_t at = 0u; at < sizeof(*names); ++at)
        bytes[at] = 0u;
}

static void
names_put(AstraLogNames *names, uint32_t pid, const char *name,
          uint32_t length)
{
    AstraLogName *slot = NULL;
    uint32_t start = 0u;
    uint32_t at = 0u;

    /* The same pid, else a free entry, else the least recently used --
     * unpinned before pinned. */
    for (uint32_t index = 0u; index < ASTRA_LOG_NAMES_MAX; ++index) {
        AstraLogName *entry = &names->entry[index];

        if (entry->pid == pid) {
            slot = entry;
            break;
        }
        if (slot == NULL || (slot->pid != 0u &&
                             (entry->pid == 0u ||
                              entry->pinned < slot->pinned ||
                              (entry->pinned == slot->pinned &&
                               entry->used < slot->used))))
            slot = entry;
    }
    if (slot->pid != pid)
        slot->pinned = 0u;
    for (uint32_t index = 0u; index < length; ++index)
        if (name[index] == '/')
            start = index + 1u;
    while (start + at < length && at + 1u < ASTRA_LOG_IDENT_MAX) {
        slot->name[at] = name[start + at];
        ++at;
    }
    while (at < ASTRA_LOG_IDENT_MAX)
        slot->name[at++] = '\0';
    slot->pid = pid;
    slot->used = ++names->clock;
}

int
astra_log_names_record(AstraLogNames *names, const AstraEventDrained *record)
{
    uint32_t length = record->payload_length;

    if (record->message != ASTRA_EVENT_MESSAGE_PROCESS_START ||
        record->process == 0u || length > ASTRA_EVENT_ARGUMENT_MAX)
        return 0;
    if (record->activity == 0u) {
        names->chain_open = 1u;
        names->chain_pid = record->process;
        names->chain_length = 0u;
    } else if (!names->chain_open || names->chain_pid != record->process ||
               names->chain_length != record->activity) {
        names->chain_open = 0u; /* a tail whose head the ring overwrote */
        return 0;
    }
    if (length > sizeof(names->chain) - names->chain_length) {
        names->chain_open = 0u;
        return 0;
    }
    for (uint32_t at = 0u; at < length; ++at)
        names->chain[names->chain_length + at] = (char)record->payload[at];
    names->chain_length += length;
    if ((record->flags & ASTRA_EVENT_FLAG_CONTINUED) != 0u)
        return 0;
    names->chain_open = 0u;
    names_put(names, names->chain_pid, names->chain, names->chain_length);
    return 1;
}

const char *
astra_log_names_find(AstraLogNames *names, uint32_t pid)
{
    if (pid == 0u)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_LOG_NAMES_MAX; ++index)
        if (names->entry[index].pid == pid) {
            names->entry[index].used = ++names->clock;
            return names->entry[index].name;
        }
    return NULL;
}

void
astra_log_names_pin(AstraLogNames *names)
{
    for (uint32_t index = 0u; index < ASTRA_LOG_NAMES_MAX; ++index)
        if (names->entry[index].pid != 0u)
            names->entry[index].pinned = 1u;
}

AstraLogMark
astra_log_mark(const AstraEventDrained *record)
{
    AstraLogMark mark;

    mark.sequence = record->sequence;
    mark.process = record->process;
    mark.message = record->message;
    mark.stamp = ((uint64_t)record->timestamp_high << 32) |
                 record->timestamp_low;
    return mark;
}

/*
 * magic, version, boot, sequence, mark {sequence, process, message, stamp},
 * count; count entries of {pid, used, pinned, name}; CRC-32 of everything
 * before it. Big-endian.
 */
uint32_t
astra_log_state_encode(const AstraLogResume *resume,
                       const AstraLogNames *names, uint8_t *out,
                       uint32_t capacity)
{
    uint32_t count = 0u;
    uint32_t at = STATE_HEADER;

    if (capacity < ASTRA_LOG_STATE_BYTES_MAX)
        return 0u;
    for (uint32_t index = 0u; index < ASTRA_LOG_NAMES_MAX; ++index) {
        const AstraLogName *entry = &names->entry[index];

        if (entry->pid == 0u)
            continue;
        astra_store_be32(out + at, entry->pid);
        astra_store_be32(out + at + 4u, entry->used);
        astra_store_be32(out + at + 8u, entry->pinned);
        for (uint32_t byte = 0u; byte < ASTRA_LOG_IDENT_MAX; ++byte)
            out[at + 12u + byte] = (uint8_t)entry->name[byte];
        at += STATE_ENTRY;
        ++count;
    }
    astra_store_be32(out, STATE_MAGIC);
    astra_store_be32(out + 4u, STATE_VERSION);
    astra_store_be64(out + 8u, resume->boot);
    astra_store_be32(out + 16u, resume->sequence);
    astra_store_be32(out + 20u, resume->mark.sequence);
    astra_store_be32(out + 24u, resume->mark.process);
    astra_store_be32(out + 28u, resume->mark.message);
    astra_store_be64(out + 32u, resume->mark.stamp);
    astra_store_be32(out + 40u, count);
    astra_store_be32(out + at, astra_crc32(out, at));
    return at + 4u;
}

int
astra_log_state_decode(const uint8_t *bytes, uint32_t length,
                       AstraLogResume *resume, AstraLogNames *names)
{
    uint32_t count;
    uint32_t at = STATE_HEADER;

    astra_log_names_reset(names);
    if (length < STATE_HEADER + 4u ||
        astra_load_be32(bytes) != STATE_MAGIC ||
        astra_load_be32(bytes + 4u) != STATE_VERSION)
        return 0;
    count = astra_load_be32(bytes + 40u);
    if (count > ASTRA_LOG_NAMES_MAX ||
        length != STATE_HEADER + count * STATE_ENTRY + 4u ||
        astra_load_be32(bytes + length - 4u) !=
            astra_crc32(bytes, length - 4u))
        return 0;
    for (uint32_t index = 0u; index < count; ++index, at += STATE_ENTRY) {
        AstraLogName *entry = &names->entry[index];

        entry->pid = astra_load_be32(bytes + at);
        entry->used = astra_load_be32(bytes + at + 4u);
        entry->pinned = astra_load_be32(bytes + at + 8u) != 0u;
        for (uint32_t byte = 0u; byte < ASTRA_LOG_IDENT_MAX; ++byte)
            entry->name[byte] = (char)bytes[at + 12u + byte];
        if (entry->pid == 0u ||
            entry->name[ASTRA_LOG_IDENT_MAX - 1u] != '\0') {
            astra_log_names_reset(names);
            return 0;
        }
        if (entry->used > names->clock)
            names->clock = entry->used;
    }
    resume->boot = astra_load_be64(bytes + 8u);
    resume->sequence = astra_load_be32(bytes + 16u);
    resume->mark.sequence = astra_load_be32(bytes + 20u);
    resume->mark.process = astra_load_be32(bytes + 24u);
    resume->mark.message = astra_load_be32(bytes + 28u);
    resume->mark.stamp = astra_load_be64(bytes + 32u);
    return 1;
}

uint64_t
astra_log_boot_id(uint64_t wall_ns, uint64_t monotonic_ns, int wall_valid)
{
    uint64_t began;

    if (!wall_valid || wall_ns < monotonic_ns)
        return 0u;
    began = wall_ns - monotonic_ns + 500000000ull;
    return began / 1000000000ull;
}

int
astra_log_sequence_after(uint32_t sequence, uint32_t other)
{
    uint32_t distance = sequence - other;

    return distance != 0u && distance < 0x80000000u;
}

void
astra_log_boot_scan(AstraLogBootScan *scan, const AstraEventDrained *record)
{
    AstraLogMark mark = astra_log_mark(record);

    if (scan->newest == 0u || mark.stamp < scan->last) {
        if (scan->newest != 0u)
            scan->first = record->sequence;
        scan->anchor = mark;
    }
    if (scan->want != 0u && record->sequence == scan->want) {
        scan->found = 1u;
        scan->at = mark;
    }
    scan->last = mark.stamp;
    scan->newest = record->sequence;
}

int
astra_log_state_restart(const AstraLogResume *saved, uint64_t boot,
                        const AstraLogBootScan *scan)
{
    uint32_t saved_sequence = saved->sequence;
    uint64_t apart = saved->boot > boot ? saved->boot - boot :
                                          boot - saved->boot;

    if (saved->mark.sequence != 0u && scan->found &&
        scan->at.sequence == saved->mark.sequence)
        return scan->at.stamp == saved->mark.stamp &&
               scan->at.process == saved->mark.process &&
               scan->at.message == saved->mark.message &&
               (scan->first == 0u ||
                !astra_log_sequence_after(scan->first,
                                          saved->mark.sequence));
    if (apart > 1u)
        return 0;
    if (scan->newest == 0u ||
        astra_log_sequence_after(saved_sequence, scan->newest))
        return 0;
    return scan->first == 0u ||
           !astra_log_sequence_after(scan->first, saved_sequence + 1u);
}
