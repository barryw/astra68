/*
 * Serving EVENTS: through the storage protocol's backend seam.
 *
 * The whole file is a path parser, a merge across three rings, and a renderer.
 * There is no protocol here and no client: `ls` and `cat` are the interface,
 * which is the point of making this a tree rather than a bespoke query service.
 *
 * Nothing here emits an event. Structural, per the spec's §8.5: a reader that
 * logs about reading is how a logging subsystem takes a machine down.
 */

#include <astra/event_backend.h>

#include <stddef.h>

#include <astra/event_descriptor.h>

#define KIND_ROOT        1u
#define KIND_BOOT        2u
#define KIND_BOOT_CURRENT 3u
#define KIND_ACTIVITY_DIR 4u
#define KIND_SUBSYSTEM_DIR 5u
#define KIND_MERGED      6u   /* a leaf over the three level tiers */
#define KIND_EARLIEST    7u   /* a leaf over the boot ring */
#define KIND_SUBSYSTEM_ONE 8u /* one subsystem, and the levels under it */

#define LINE_MAX 160u

static const char *const subsystem_name[ASTRA_EVENT_SUBSYSTEM_MAX] = {
    "kernel", "runtime", "supervisor", "storage", "vfs", "shell", "input",
    "display"
};

static const char *const level_name[] = {
    "debug", "info", "notice", "warning", "error"
};

/* The leaves under boot/current, and the lowest level each one holds. */
static const struct {
    const char *name;
    uint8_t level_min;
    uint8_t kind;
} boot_leaf[] = {
    {"all", ASTRA_EVENT_LEVEL_DEBUG, KIND_MERGED},
    {"notice", ASTRA_EVENT_LEVEL_NOTICE, KIND_MERGED},
    {"warning", ASTRA_EVENT_LEVEL_WARNING, KIND_MERGED},
    {"error", ASTRA_EVENT_LEVEL_ERROR, KIND_MERGED},
    {"earliest", ASTRA_EVENT_LEVEL_DEBUG, KIND_EARLIEST}
};
#define BOOT_LEAF_COUNT (sizeof(boot_leaf) / sizeof(boot_leaf[0]))
/*
 * The same names under a subsystem, minus `earliest`: the boot ring is the
 * boot's, not a subsystem's. Two dimensions at once is the one thing a single
 * directory could not name, and it is a path rather than a query because
 * anything `events` can ask for has to be something `cat` could have asked for.
 */
#define LEVEL_LEAF_COUNT (BOOT_LEAF_COUNT - 1u)

static const uint32_t merged_tier[] = {
    ASTRA_EVENT_TIER_PRESENTED, ASTRA_EVENT_TIER_RECORD,
    ASTRA_EVENT_TIER_DETAIL
};
#define MERGED_TIER_COUNT (sizeof(merged_tier) / sizeof(merged_tier[0]))

static int
equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

/* The path with `prefix` removed, or NULL when it does not start with it. */
static const char *
after(const char *path, const char *prefix)
{
    while (*prefix != '\0') {
        if (*path != *prefix) {
            return NULL;
        }
        ++path;
        ++prefix;
    }
    return path;
}

static int
hex_value(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return 10 + (digit - 'a');
    }
    return -1;
}

/*
 * Exactly eight lowercase hex digits. Not "whatever strtoul accepts": an
 * activity directory's names are what this backend printed, and accepting a
 * looser spelling would mean two paths naming one story.
 */
static int
parse_activity(const char *text, uint32_t *activity)
{
    uint32_t value = 0u;
    uint32_t index;

    for (index = 0u; index < 8u; ++index) {
        int digit = hex_value(text[index]);

        if (digit < 0) {
            return 0;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    *activity = value;
    return text[8] == '\0';
}

/* ---------------------------------------------------------------- merging */

/*
 * The next record at or after `index[]`, by sequence, across the level tiers.
 * Four rings and one ordering: the store files an event by level, and a reader
 * asked for a story, so the merge belongs here rather than in the store.
 */
static const AstraEventStored *
merged_next(const AstraEventStore *store, uint32_t *index)
{
    const AstraEventStored *best = NULL;
    uint32_t best_tier = 0u;
    uint32_t at;

    for (at = 0u; at < MERGED_TIER_COUNT; ++at) {
        const AstraEventStored *record =
            astra_event_store_at(store, merged_tier[at], index[at]);

        if (record == NULL) {
            continue;
        }
        if (best == NULL || record->event.sequence < best->event.sequence) {
            best = record;
            best_tier = at;
        }
    }
    if (best != NULL) {
        ++index[best_tier];
    }
    return best;
}

static int
matches(const AstraEventCatalog *catalog, const AstraEventsNode *node,
        const AstraEventStored *record)
{
    const AstraEventDescriptor *descriptor;

    if ((record->event.flags & ASTRA_EVENT_LEVEL_MASK) < node->level_min) {
        return 0;
    }
    if (node->activity_filter != 0u &&
        record->event.activity != node->activity) {
        return 0;
    }
    if (node->subsystem >= ASTRA_EVENT_SUBSYSTEM_MAX) {
        return 1;
    }
    descriptor = astra_event_catalog_lookup(catalog, record->event.message);
    return descriptor != NULL && descriptor->subsystem == node->subsystem;
}

/* --------------------------------------------------------------- rendering */

static uint32_t
put(char *out, uint32_t capacity, uint32_t at, const char *text)
{
    while (*text != '\0' && at < capacity) {
        out[at++] = *text++;
    }
    return at;
}

/* `width` pads with leading zeroes; zero means as many digits as it takes. */
static uint32_t
put_number(char *out, uint32_t capacity, uint32_t at, uint32_t value,
           uint32_t radix, uint32_t width)
{
    static const char digits[] = "0123456789abcdef";
    char reversed[10];
    uint32_t count = 0u;

    if (value == 0u) {
        reversed[count++] = '0';
    }
    while (value != 0u && count < sizeof(reversed)) {
        reversed[count++] = digits[value % radix];
        value /= radix;
    }
    for (uint32_t pad = count; pad < width && at < capacity; ++pad) {
        out[at++] = '0';
    }
    while (count != 0u && at < capacity) {
        out[at++] = reversed[--count];
    }
    return at;
}

/*
 * One event, one line, in the shape tools/trace_decode.py already prints off
 * the machine. Two readers of one stream showing it two ways would be two
 * things to learn for no reason.
 */
static uint32_t
render_line(const AstraEventCatalog *catalog, const AstraEventStored *record,
            char *out, uint32_t capacity)
{
    const AstraEventDescriptor *descriptor;
    uint32_t level = record->event.flags & ASTRA_EVENT_LEVEL_MASK;
    uint32_t at = 0u;

    at = put(out, capacity, at, "seq ");
    at = put_number(out, capacity, at, record->event.sequence, 10u, 0u);
    at = put(out, capacity, at, "  ");
    at = put(out, capacity, at,
             level < (sizeof(level_name) / sizeof(level_name[0])) ?
                 level_name[level] : "?");
    at = put(out, capacity, at, "  ");
    at = put_number(out, capacity, at, record->event.process, 16u, 8u);
    at = put(out, capacity, at, "/");
    at = put_number(out, capacity, at, record->event.thread, 10u, 0u);
    if (record->event.activity != 0u) {
        at = put(out, capacity, at, " act ");
        at = put_number(out, capacity, at, record->event.activity, 16u, 8u);
    }
    at = put(out, capacity, at, "  ");
    if (at < capacity) {
        at += astra_event_catalog_render(catalog,
                                         record->event.message,
                                         record->event.flags,
                                         record->event.payload,
                                         record->event.payload_length,
                                         &out[at], capacity - at);
    }
    descriptor = astra_event_catalog_lookup(catalog, record->event.message);
    if (descriptor != NULL) {
        at = put(out, capacity, at, " (");
        at = put(out, capacity, at, descriptor->file);
        at = put(out, capacity, at, ":");
        at = put_number(out, capacity, at, descriptor->line, 10u, 0u);
        at = put(out, capacity, at, ")");
    }
    if (at < capacity) {
        out[at++] = '\n';
    }
    return at;
}

/* ------------------------------------------------------------------ nodes */

static AstraEventsBackend *
backend_of(void *context)
{
    return (AstraEventsBackend *)context;
}

static AstraEventsNode *
claim(AstraEventsBackend *backend, uintptr_t *token)
{
    uint32_t index;

    for (index = 0u; index < ASTRA_EVENTS_NODE_MAX; ++index) {
        if (backend->nodes[index].used == 0u) {
            AstraEventsNode *node = &backend->nodes[index];

            node->used = 1u;
            node->kind = 0u;
            node->level_min = ASTRA_EVENT_LEVEL_DEBUG;
            node->subsystem = ASTRA_EVENT_SUBSYSTEM_MAX;
            node->activity = 0u;
            node->activity_filter = 0u;
            node->memo_offset = 0u;
            node->memo_index[0] = 0u;
            node->memo_index[1] = 0u;
            node->memo_index[2] = 0u;
            *token = (uintptr_t)(index + 1u);
            return node;
        }
    }
    return NULL;
}

static AstraEventsNode *
resolve_token(AstraEventsBackend *backend, uintptr_t token)
{
    if (token == 0u || token > ASTRA_EVENTS_NODE_MAX) {
        return NULL;
    }
    return backend->nodes[token - 1u].used != 0u ?
        &backend->nodes[token - 1u] : NULL;
}

/*
 * A path becomes a filter. `describe` fills the node without claiming one, so
 * stat and open share one parser -- two parsers for one namespace is how a
 * `stat` starts disagreeing with an `open`.
 */
static uint32_t
describe(const AstraEventsBackend *backend, AstraEventsNode *node,
         const char *path)
{
    const char *rest;
    uint32_t index;

    node->kind = 0u;
    node->level_min = ASTRA_EVENT_LEVEL_DEBUG;
    node->subsystem = ASTRA_EVENT_SUBSYSTEM_MAX;
    node->activity = 0u;
    node->activity_filter = 0u;
    node->previous_boot = 0u;

    if (equal(path, "/") || equal(path, "")) {
        node->kind = KIND_ROOT;
        return ASTRA_VFS_OK;
    }
    if (equal(path, "/boot")) {
        node->kind = KIND_BOOT;
        return ASTRA_VFS_OK;
    }
    if (equal(path, "/boot/current")) {
        node->kind = KIND_BOOT_CURRENT;
        return ASTRA_VFS_OK;
    }
    if (equal(path, "/boot/-1")) {
        if (backend->previous == NULL) {
            return ASTRA_VFS_ERR_NOT_FOUND;
        }
        node->kind = KIND_BOOT_CURRENT;
        node->previous_boot = 1u;
        return ASTRA_VFS_OK;
    }
    if (equal(path, "/activity")) {
        node->kind = KIND_ACTIVITY_DIR;
        return ASTRA_VFS_OK;
    }
    if (equal(path, "/subsystem")) {
        node->kind = KIND_SUBSYSTEM_DIR;
        return ASTRA_VFS_OK;
    }
    rest = after(path, "/boot/current/");
    if (rest == NULL && backend->previous != NULL) {
        rest = after(path, "/boot/-1/");
        node->previous_boot = rest != NULL;
    }
    if (rest != NULL) {
        for (index = 0u; index < BOOT_LEAF_COUNT; ++index) {
            if (equal(rest, boot_leaf[index].name)) {
                node->kind = boot_leaf[index].kind;
                node->level_min = boot_leaf[index].level_min;
                return ASTRA_VFS_OK;
            }
        }
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    rest = after(path, "/activity/");
    if (rest != NULL) {
        if (!parse_activity(rest, &node->activity)) {
            return ASTRA_VFS_ERR_NOT_FOUND;
        }
        node->kind = KIND_MERGED;
        node->activity_filter = 1u;
        return ASTRA_VFS_OK;
    }
    rest = after(path, "/subsystem/");
    if (rest != NULL) {
        for (index = 0u; index < ASTRA_EVENT_SUBSYSTEM_MAX; ++index) {
            const char *level = after(rest, subsystem_name[index]);

            if (level == NULL) {
                continue;
            }
            node->subsystem = (uint8_t)index;
            /*
             * A subsystem is a directory of levels, and `all` is the one that
             * means every level of it. A node that were both a file and a
             * directory would have to answer `ls` and `cat` with two different
             * kinds, and the protocol refuses a read on a directory for good
             * reasons that are not worth arguing with here.
             */
            if (*level == '\0') {
                node->kind = KIND_SUBSYSTEM_ONE;
                return ASTRA_VFS_OK;
            }
            if (*level != '/') {
                continue;   /* a longer name that merely starts the same way */
            }
            ++level;
            for (uint32_t leaf = 0u; leaf < LEVEL_LEAF_COUNT; ++leaf) {
                if (equal(level, boot_leaf[leaf].name)) {
                    node->kind = KIND_MERGED;
                    node->level_min = boot_leaf[leaf].level_min;
                    return ASTRA_VFS_OK;
                }
            }
            return ASTRA_VFS_ERR_NOT_FOUND;
        }
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

static int
is_directory(uint8_t kind)
{
    return kind == KIND_ROOT || kind == KIND_BOOT ||
           kind == KIND_BOOT_CURRENT || kind == KIND_ACTIVITY_DIR ||
           kind == KIND_SUBSYSTEM_DIR || kind == KIND_SUBSYSTEM_ONE;
}

/* ------------------------------------------------------------------- verbs */

static uint32_t
events_open(void *context, const char *path, uint32_t flags, uintptr_t *node,
            AstraVfsNodeInfo *info)
{
    AstraEventsBackend *backend = backend_of(context);
    AstraEventsNode described;
    AstraEventsNode *held;
    uint32_t status;

    if ((flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                  ASTRA_VFS_OPEN_TRUNCATE)) != 0u) {
        return ASTRA_VFS_ERR_ACCESS;
    }
    status = describe(backend, &described, path);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    held = claim(backend, node);
    if (held == NULL) {
        return ASTRA_VFS_ERR_LIMIT;
    }
    held->kind = described.kind;
    held->level_min = described.level_min;
    held->subsystem = described.subsystem;
    held->activity = described.activity;
    held->activity_filter = described.activity_filter;
    held->previous_boot = described.previous_boot;
    /*
     * A leaf has no size until it is read: the text is rendered from records
     * that are still arriving. Reporting zero is the honest answer the handler
     * contract was written to allow, and a reader that trusts a size here
     * would be trusting a number that was true a moment ago.
     */
    info->size = 0u;
    info->kind = is_directory(held->kind) ? ASTRA_VFS_KIND_DIRECTORY :
        ASTRA_VFS_KIND_FILE;
    return ASTRA_VFS_OK;
}

static uint32_t
events_close(void *context, uintptr_t token)
{
    AstraEventsNode *node = resolve_token(backend_of(context), token);

    if (node == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    node->used = 0u;
    return ASTRA_VFS_OK;
}

static uint32_t
events_read(void *context, uintptr_t token, uint64_t offset, void *buffer,
            uint32_t length, uint32_t *moved)
{
    AstraEventsBackend *backend = backend_of(context);
    AstraEventsNode *node = resolve_token(backend, token);
    const AstraEventStore *store;
    uint8_t *out = buffer;
    char line[LINE_MAX];
    uint32_t index[MERGED_TIER_COUNT] = {0u, 0u, 0u};
    uint32_t boot_index = 0u;
    uint32_t produced = 0u;
    uint64_t at = 0u;

    *moved = 0u;
    if (node == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (is_directory(node->kind)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    store = node->previous_boot != 0u ? backend->previous : backend->store;
    if (store == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    /*
     * Resume from the last line boundary this node reached, when the caller is
     * asking for something at or after it. `cat` reads forwards, and
     * re-rendering the file from the beginning for every page would make a
     * read quadratic in exactly the way readdir used to be. A caller that seeks
     * backwards pays the walk, which is the rare case and the correct cost.
     *
     * The memo is always a line boundary. Memoising mid-line would resume with
     * the record already consumed and silently drop the rest of that line.
     */
    if (node->memo_offset != 0u && offset >= node->memo_offset) {
        at = node->memo_offset;
        if (node->previous_boot != 0u || node->kind == KIND_EARLIEST) {
            boot_index = node->memo_index[0];
        } else {
            index[0] = node->memo_index[0];
            index[1] = node->memo_index[1];
            index[2] = node->memo_index[2];
        }
    }

    for (;;) {
        const AstraEventStored *record;
        uint32_t rendered;
        uint32_t take;

        if (node->previous_boot != 0u || node->kind == KIND_EARLIEST) {
            record = astra_event_store_at(store,
                                          ASTRA_EVENT_TIER_BOOT, boot_index);
            if (record != NULL) {
                ++boot_index;
            }
        } else {
            record = merged_next(store, index);
        }
        if (record == NULL) {
            break;
        }
        if (!matches(store->catalog, node, record)) {
            continue;
        }
        rendered = render_line(store->catalog, record, line, sizeof(line));
        if (at + rendered <= offset) {
            at += rendered;
            continue;
        }
        take = rendered;
        if (at < offset) {
            uint32_t skip = (uint32_t)(offset - at);

            take -= skip;
            for (uint32_t copy = 0u; copy < take && produced < length; ++copy) {
                out[produced++] = (uint8_t)line[skip + copy];
            }
        } else {
            for (uint32_t copy = 0u; copy < take && produced < length; ++copy) {
                out[produced++] = (uint8_t)line[copy];
            }
        }
        at += rendered;
        /*
         * A line boundary, and therefore somewhere a later read can resume --
         * but only once the whole line has been delivered. A page that ends
         * mid-line would otherwise memoise past bytes the caller has not seen,
         * and the next read would resume after them.
         */
        if (at <= offset + produced) {
            node->memo_offset = (uint32_t)at;
            if (node->previous_boot != 0u || node->kind == KIND_EARLIEST) {
                node->memo_index[0] = boot_index;
            } else {
                node->memo_index[0] = index[0];
                node->memo_index[1] = index[1];
                node->memo_index[2] = index[2];
            }
        }
        if (produced == length) {
            break;
        }
    }

    *moved = produced;
    return ASTRA_VFS_OK;
}

static uint32_t
events_write(void *context, uintptr_t node, uint64_t offset,
             uint32_t flags, const void *buffer, uint32_t length,
             uint32_t *moved, uint64_t *position)
{
    (void)context;
    (void)node;
    (void)offset;
    (void)flags;
    (void)buffer;
    (void)length;
    *moved = 0u;
    *position = offset;
    return ASTRA_VFS_ERR_ACCESS;
}

static uint32_t events_sync(void *context, uintptr_t node)
{
    (void)context;
    (void)node;
    return ASTRA_VFS_ERR_ACCESS;
}

static uint32_t events_truncate(void *context, uintptr_t node, uint64_t size)
{
    (void)context;
    (void)node;
    (void)size;
    return ASTRA_VFS_ERR_ACCESS;
}

static uint32_t
events_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    AstraEventsNode described;
    uint32_t status;

    status = describe(backend_of(context), &described, path);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    info->size = 0u;
    info->kind = is_directory(described.kind) ? ASTRA_VFS_KIND_DIRECTORY :
        ASTRA_VFS_KIND_FILE;
    return ASTRA_VFS_OK;
}

static uint32_t
copy_name(char *name, uint32_t capacity, const char *text)
{
    uint32_t at = 0u;

    while (text[at] != '\0') {
        if (at + 1u >= capacity) {
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        }
        name[at] = text[at];
        ++at;
    }
    name[at] = '\0';
    return ASTRA_VFS_OK;
}

/*
 * One entry per distinct activity the store still holds, named by its first
 * appearance and in the order the stories began. The cookie is the record
 * position the scan stopped at.
 *
 * ponytail: an entry costs a rescan of everything before it, so listing the
 * directory is quadratic in the store's size. Bounded -- the store is a few
 * hundred records -- and the upgrade is a small ring of distinct activities
 * maintained in the store on append, which turns this into a lookup. Do that
 * when a listing measures slow, not before.
 */
static uint32_t
readdir_activity(AstraEventsBackend *backend, uint64_t cookie, char *name,
                 uint32_t capacity, AstraVfsNodeInfo *info, uint64_t *next)
{
    uint32_t index[MERGED_TIER_COUNT] = {0u, 0u, 0u};
    uint32_t position = 0u;

    for (;;) {
        const AstraEventStored *record = merged_next(backend->store, index);
        uint32_t scan[MERGED_TIER_COUNT] = {0u, 0u, 0u};
        int duplicate = 0;

        if (record == NULL) {
            return ASTRA_VFS_ERR_NOT_FOUND;
        }
        ++position;
        if (record->event.activity == 0u || position <= cookie) {
            continue;
        }
        for (uint32_t before = 0u; before + 1u < position; ++before) {
            const AstraEventStored *earlier = merged_next(backend->store, scan);

            if (earlier != NULL &&
                earlier->event.activity == record->event.activity) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (capacity < 9u) {
            return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        }
        (void)put_number(name, capacity, 0u, record->event.activity, 16u, 8u);
        name[8] = '\0';
        info->size = 0u;
        info->kind = ASTRA_VFS_KIND_FILE;
        *next = position;
        return ASTRA_VFS_OK;
    }
}

static uint32_t
events_readdir(void *context, const char *path, uint64_t cookie, char *name,
               uint32_t capacity, AstraVfsNodeInfo *info, uint64_t *next)
{
    AstraEventsBackend *backend = backend_of(context);
    AstraEventsNode described;
    const char *entry = NULL;
    uint32_t status;

    status = describe(backend, &described, path);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (!is_directory(described.kind)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    info->size = 0u;
    info->kind = ASTRA_VFS_KIND_DIRECTORY;

    switch (described.kind) {
    case KIND_ROOT:
        if (cookie == 0u) {
            entry = "boot";
        } else if (cookie == 1u) {
            entry = "activity";
        } else if (cookie == 2u) {
            entry = "subsystem";
        }
        break;
    case KIND_BOOT:
        if (cookie == 0u) {
            entry = "current";
        } else if (cookie == 1u && backend->previous != NULL) {
            entry = "-1";
        }
        break;
    case KIND_BOOT_CURRENT:
        if (cookie < BOOT_LEAF_COUNT) {
            entry = boot_leaf[cookie].name;
            info->kind = ASTRA_VFS_KIND_FILE;
        }
        break;
    case KIND_SUBSYSTEM_DIR:
        if (cookie < ASTRA_EVENT_SUBSYSTEM_MAX) {
            entry = subsystem_name[cookie];
        }
        break;
    case KIND_SUBSYSTEM_ONE:
        if (cookie < LEVEL_LEAF_COUNT) {
            entry = boot_leaf[cookie].name;
            info->kind = ASTRA_VFS_KIND_FILE;
        }
        break;
    case KIND_ACTIVITY_DIR:
        return readdir_activity(backend, cookie, name, capacity, info, next);
    default:
        return ASTRA_VFS_ERR_INVALID;
    }
    if (entry == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    status = copy_name(name, capacity, entry);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    *next = cookie + 1u;
    return ASTRA_VFS_OK;
}

static uint32_t
events_mkdir(void *context, const char *path)
{
    (void)context;
    (void)path;
    return ASTRA_VFS_ERR_ACCESS;
}

static uint32_t
events_unlink(void *context, const char *path)
{
    (void)context;
    (void)path;
    return ASTRA_VFS_ERR_ACCESS;
}

static uint32_t
events_rename(void *context, const char *from, const char *to)
{
    (void)context;
    (void)from;
    (void)to;
    return ASTRA_VFS_ERR_ACCESS;
}

static const AstraVfsBackendOps events_ops = {
    events_open,
    events_close,
    events_read,
    events_write,
    events_sync,
    events_truncate,
    events_stat,
    events_readdir,
    events_mkdir,
    events_unlink,
    events_rename
};

int
astra_events_backend_init(AstraEventsBackend *backend,
                          const AstraEventStore *store,
                          const AstraEventStore *previous,
                          const AstraEventCatalog *catalog)
{
    uint32_t index;

    if (backend == NULL || store == NULL) {
        return 0;
    }
    backend->store = store;
    backend->previous = previous;
    backend->catalog = catalog;
    for (index = 0u; index < ASTRA_EVENTS_NODE_MAX; ++index) {
        backend->nodes[index].used = 0u;
    }
    return 1;
}

const AstraVfsBackendOps *
astra_events_backend_ops(void)
{
    return &events_ops;
}
