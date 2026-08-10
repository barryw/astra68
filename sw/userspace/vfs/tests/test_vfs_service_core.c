/*
 * The storage service core, driven directly with no transport.
 *
 * The backend here is a fixed in-memory tree, not lwext4, and that is the
 * point twice over: it proves the core has no filesystem knowledge in it, and
 * it lets the misuse cases -- stale handles, one session reaching for
 * another's file, malformed records -- be provoked exactly, which is awkward
 * against a real volume and is where the security-relevant behaviour lives.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/vfs_local_transport.h>
#include <astra/vfs_service_core.h>

#define FAKE_NODE_MAX 8u
#define FAKE_BYTES_MAX 512u

typedef struct FakeNode {
    char path[ASTRA_VFS_PATH_MAX];
    uint8_t bytes[FAKE_BYTES_MAX];
    uint32_t size;
    uint16_t kind;
    int used;
    int open_count;
} FakeNode;

typedef struct FakeFs {
    FakeNode nodes[FAKE_NODE_MAX];
    uint32_t closes;
} FakeFs;

static FakeFs fake;

static FakeNode *
fake_find(const char *path)
{
    uint32_t index;

    for (index = 0u; index < FAKE_NODE_MAX; ++index) {
        if (fake.nodes[index].used &&
            strcmp(fake.nodes[index].path, path) == 0) {
            return &fake.nodes[index];
        }
    }
    return NULL;
}

static FakeNode *
fake_create(const char *path, uint16_t kind)
{
    uint32_t index;

    for (index = 0u; index < FAKE_NODE_MAX; ++index) {
        if (!fake.nodes[index].used) {
            fake.nodes[index].used = 1;
            fake.nodes[index].kind = kind;
            fake.nodes[index].size = 0u;
            fake.nodes[index].open_count = 0;
            snprintf(fake.nodes[index].path, sizeof(fake.nodes[index].path),
                     "%s", path);
            return &fake.nodes[index];
        }
    }
    return NULL;
}

static uint32_t
fake_open(void *context, const char *path, uint32_t flags, uintptr_t *node,
          AstraVfsNodeInfo *info)
{
    FakeNode *found = fake_find(path);

    (void)context;
    if (found == NULL) {
        if ((flags & ASTRA_VFS_OPEN_CREATE) == 0u) {
            return ASTRA_VFS_ERR_NOT_FOUND;
        }
        found = fake_create(path, ASTRA_VFS_KIND_FILE);
        if (found == NULL) {
            return ASTRA_VFS_ERR_NO_SPACE;
        }
    }
    if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u) {
        found->size = 0u;
    }
    ++found->open_count;
    *node = (uintptr_t)found;
    info->size = found->size;
    info->kind = found->kind;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_close(void *context, uintptr_t node)
{
    FakeNode *found = (FakeNode *)node;

    (void)context;
    ++fake.closes;
    if (found != NULL && found->open_count > 0) {
        --found->open_count;
    }
    return ASTRA_VFS_OK;
}

static uint32_t
fake_read(void *context, uintptr_t node, uint64_t offset, void *buffer,
          uint32_t length, uint32_t *moved)
{
    FakeNode *found = (FakeNode *)node;
    uint32_t available;

    (void)context;
    *moved = 0u;
    if (offset > found->size) {
        return ASTRA_VFS_ERR_INVALID;
    }
    available = found->size - (uint32_t)offset;
    if (length > available) {
        length = available;
    }
    memcpy(buffer, &found->bytes[offset], length);
    *moved = length;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_write(void *context, uintptr_t node, uint64_t offset, const void *buffer,
           uint32_t length, uint32_t *moved)
{
    FakeNode *found = (FakeNode *)node;

    (void)context;
    *moved = 0u;
    if (offset + length > FAKE_BYTES_MAX) {
        return ASTRA_VFS_ERR_NO_SPACE;
    }
    memcpy(&found->bytes[offset], buffer, length);
    if (offset + length > found->size) {
        found->size = (uint32_t)(offset + length);
    }
    *moved = length;
    return ASTRA_VFS_OK;
}

static uint32_t
fake_stat(void *context, const char *path, AstraVfsNodeInfo *info)
{
    FakeNode *found = fake_find(path);

    (void)context;
    if (found == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    info->size = found->size;
    info->kind = found->kind;
    return ASTRA_VFS_OK;
}

/*
 * The cookie is the slot after the one returned, so a scan resumes where it
 * stopped without counting from the first node. `fake_readdir_scans` is how a
 * test sees that a listing costs one visit per entry rather than one walk.
 */
static uint32_t fake_readdir_visits;

static uint32_t
fake_readdir(void *context, const char *path, uint64_t cookie, char *name,
             uint32_t capacity, AstraVfsNodeInfo *info, uint64_t *next)
{
    uint32_t slot;

    (void)context;
    (void)path;
    if (cookie > FAKE_NODE_MAX) {
        return ASTRA_VFS_ERR_INVALID;
    }
    for (slot = (uint32_t)cookie; slot < FAKE_NODE_MAX; ++slot) {
        ++fake_readdir_visits;
        if (!fake.nodes[slot].used) {
            continue;
        }
        snprintf(name, capacity, "%s", fake.nodes[slot].path);
        info->size = fake.nodes[slot].size;
        info->kind = fake.nodes[slot].kind;
        *next = slot + 1u;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

static uint32_t
fake_mkdir(void *context, const char *path)
{
    (void)context;
    if (fake_find(path) != NULL) {
        return ASTRA_VFS_ERR_EXISTS;
    }
    return fake_create(path, ASTRA_VFS_KIND_DIRECTORY) != NULL ?
        ASTRA_VFS_OK : ASTRA_VFS_ERR_NO_SPACE;
}

static uint32_t
fake_unlink(void *context, const char *path)
{
    FakeNode *found = fake_find(path);

    (void)context;
    if (found == NULL) {
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
    found->used = 0;
    return ASTRA_VFS_OK;
}

static const AstraVfsBackendOps fake_ops = {
    fake_open, fake_close, fake_read, fake_write,
    fake_stat, fake_readdir, fake_mkdir, fake_unlink
};

static AstraVfsService service;

static void
begin_request(AstraVfsRequest *request, uint32_t session, const char *path)
{
    memset(request, 0, sizeof(*request));
    request->size = (uint16_t)ASTRA_VFS_REQUEST_SIZE;
    request->version = ASTRA_VFS_VERSION;
    request->session = session;
    if (path != NULL) {
        snprintf((char *)request->body.path, ASTRA_VFS_PATH_MAX, "%s", path);
    }
}

static uint32_t
open_session(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;

    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.version == ASTRA_VFS_VERSION);
    return reply.session;
}

static void
reset(void)
{
    memset(&fake, 0, sizeof(fake));
    assert(astra_vfs_service_init(&service, &fake_ops, NULL));
}

/* A session must be agreed before anything else is answered. */
static void
test_session_handshake(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;

    reset();
    /* No session yet: a path operation is refused rather than served. */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, "/a.txt");
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);

    session = open_session();
    assert(session != ASTRA_VFS_SESSION_INVALID);

    /*
     * A client that speaks a newer protocol than the service gets the
     * service's version, not its own. This is the check that keeps an
     * upgraded client from assuming fields the service will never send.
     */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    request.version = (uint16_t)(ASTRA_VFS_VERSION + 5u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.version == ASTRA_VFS_VERSION);

    /* Below the floor is refused outright rather than downgraded. */
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    request.version = (uint16_t)(ASTRA_VFS_VERSION_MIN - 1u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
}

static void
test_file_round_trip(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    AstraVfsFile file;
    static const char payload[] = "astra";

    reset();
    session = open_session();

    begin_request(&request, session, "/hello.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                    ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;
    assert(file != ASTRA_VFS_FILE_INVALID);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = (uint32_t)(sizeof(payload) - 1u);
    memcpy(request.body.payload, payload, sizeof(payload) - 1u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == sizeof(payload) - 1u);

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = 64u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == sizeof(payload) - 1u);
    assert(memcmp(reply.payload, payload, sizeof(payload) - 1u) == 0);

    begin_request(&request, session, "/hello.txt");
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.node_size == sizeof(payload) - 1u);
    assert(reply.kind == ASTRA_VFS_KIND_FILE);

    begin_request(&request, session, NULL);
    request.file = file;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
}

/*
 * The properties the core exists for. A backend cannot enforce any of these
 * because it never sees a session or a handle.
 */
static void
test_handle_discipline(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t first;
    uint32_t second;
    AstraVfsFile file;
    AstraVfsFile reused;

    reset();
    first = open_session();
    second = open_session();
    assert(first != second);

    begin_request(&request, first, "/owned.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    /* Another session may not touch it, even holding the exact handle. */
    begin_request(&request, second, NULL);
    request.file = file;
    request.length = 8u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
    assert(astra_vfs_service_stats(&service)->cross_session_denied == 1u);

    /* Closed, then reused: the old handle must not reach the new file. */
    begin_request(&request, first, NULL);
    request.file = file;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_CLOSE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);

    begin_request(&request, first, "/other.txt");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    reused = reply.file;
    assert(reused != file); /* the generation moved */

    begin_request(&request, first, NULL);
    request.file = file;
    request.length = 8u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_READ, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(astra_vfs_service_stats(&service)->stale_handles == 1u);

    /* A write to a read-only handle is refused by the core, not the backend. */
    begin_request(&request, first, NULL);
    request.file = reused;
    request.length = 4u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_ACCESS);
}

/* A dead client must not strand the files it had open. */
static void
test_session_release_frees_files(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t index;

    reset();
    session = open_session();
    for (index = 0u; index < 4u; ++index) {
        char path[32];

        snprintf(path, sizeof(path), "/f%u", index);
        begin_request(&request, session, path);
        request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_CREATE;
        astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request,
                                   &reply);
        assert(reply.status == ASTRA_VFS_OK);
    }
    assert(astra_vfs_service_stats(&service)->files_opened == 4u);

    astra_vfs_service_release_session(&service, session);
    assert(astra_vfs_service_stats(&service)->files_closed == 4u);
    assert(fake.closes == 4u);
    /* Every backend node was closed exactly as many times as it was opened. */
    for (index = 0u; index < FAKE_NODE_MAX; ++index) {
        assert(fake.nodes[index].open_count == 0);
    }
}

/*
 * Malformed records get a complete reply, not silence. A client cannot tell a
 * dropped message from a dead service, so the service never drops one.
 */
static void
test_malformed_records(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t index;

    reset();
    session = open_session();

    begin_request(&request, session, "/a");
    request.size = (uint16_t)(ASTRA_VFS_REQUEST_SIZE - 1u);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
    assert(reply.size == ASTRA_VFS_REPLY_SIZE);

    /*
     * An activity is the caller's account of what it was doing, so any value
     * is as valid as any other. This field used to be reserved-must-be-zero
     * and a non-zero one was a protocol error; refusing work over a log field
     * would be the service deciding what a caller may call its own story.
     */
    begin_request(&request, session, "/a");
    request.activity = 0x1a2bu;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status != ASTRA_VFS_ERR_PROTOCOL);

    /* An unterminated path is a malformed record, not a long path. */
    begin_request(&request, session, NULL);
    for (index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
        request.body.path[index] = (uint8_t)'x';
    }
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_STAT, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);

    begin_request(&request, session, "/a");
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_MAX + 1u, &request,
                               &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);

    begin_request(&request, session, "/a");
    astra_vfs_service_dispatch(&service, 0u, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_PROTOCOL);
}

/*
 * A full-width binary write must not be rejected for lacking a NUL. The
 * request body is a union, and this is the case that catches anyone who
 * reintroduces path validation on the payload arm.
 */
static void
test_full_width_binary_write(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t session;
    uint32_t index;
    AstraVfsFile file;

    reset();
    session = open_session();
    begin_request(&request, session, "/bin");
    request.flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                    ASTRA_VFS_OPEN_CREATE;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_OPEN, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    file = reply.file;

    begin_request(&request, session, NULL);
    request.file = file;
    request.length = ASTRA_VFS_IO_MAX;
    for (index = 0u; index < ASTRA_VFS_IO_MAX; ++index) {
        request.body.payload[index] = (uint8_t)(index + 1u); /* never zero */
    }
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_OK);
    assert(reply.count == ASTRA_VFS_IO_MAX);

    /* One byte past what a message carries is refused, not truncated. */
    begin_request(&request, session, NULL);
    request.file = file;
    request.length = ASTRA_VFS_IO_MAX + 1u;
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_WRITE, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_INVALID);
}

static void
test_limits(void)
{
    AstraVfsRequest request;
    AstraVfsReply reply;
    uint32_t index;

    reset();
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        assert(open_session() != ASTRA_VFS_SESSION_INVALID);
    }
    begin_request(&request, ASTRA_VFS_SESSION_INVALID, NULL);
    astra_vfs_service_dispatch(&service, ASTRA_VFS_OP_HELLO, &request, &reply);
    assert(reply.status == ASTRA_VFS_ERR_LIMIT);
}

/*
 * The whole chain, the way a caller actually meets it: Kit -> transport ->
 * core -> backend. Everything above only ever touched the core directly, which
 * would not have caught a Kit that marshals a field into the wrong place.
 */
static void
test_client_through_transport(void)
{
    AstraVfsClient client;
    AstraVfsFile file;
    uint64_t size = 0u;
    uint16_t kind = 0u;
    uint32_t moved = 0u;
    uint32_t status;
    char name[ASTRA_VFS_NAME_MAX];
    uint8_t buffer[32];
    static const char text[] = "pluggable";

    reset();
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) ==
           ASTRA_VFS_OK);
    assert(client.session != ASTRA_VFS_SESSION_INVALID);
    assert(client.version == ASTRA_VFS_VERSION);

    assert(astra_vfs_mkdir(&client, "/dir") == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/dir/note.txt",
                          ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                              ASTRA_VFS_OPEN_CREATE,
                          &file, &size, &kind) == ASTRA_VFS_OK);
    assert(astra_vfs_write(&client, file, 0u, text, sizeof(text) - 1u,
                           &moved) == ASTRA_VFS_OK);
    assert(moved == sizeof(text) - 1u);

    memset(buffer, 0, sizeof(buffer));
    assert(astra_vfs_read(&client, file, 0u, buffer, sizeof(buffer), &moved) ==
           ASTRA_VFS_OK);
    assert(moved == sizeof(text) - 1u);
    assert(memcmp(buffer, text, sizeof(text) - 1u) == 0);
    assert(astra_vfs_close(&client, file) == ASTRA_VFS_OK);

    assert(astra_vfs_stat(&client, "/dir/note.txt", &size, &kind) ==
           ASTRA_VFS_OK);
    assert(size == sizeof(text) - 1u);
    assert(kind == ASTRA_VFS_KIND_FILE);

    {
        /*
         * A scan visits every entry exactly once and costs one visit per
         * entry. Index-addressed, this walked from the first node for every
         * entry, which is a listing that gets slower the longer it is -- and
         * on a 30 MHz machine that is the difference between a directory you
         * can list and one you cannot.
         */
        char seen[FAKE_NODE_MAX][ASTRA_VFS_NAME_MAX];
        uint64_t cursor = 0u;
        uint64_t previous;
        uint32_t entries = 0u;
        uint32_t at;

        fake_readdir_visits = 0u;
        for (;;) {
            previous = cursor;
            status = astra_vfs_readdir(&client, "/", cursor, name,
                                       sizeof(name), &kind, &cursor);
            if (status == ASTRA_VFS_ERR_NOT_FOUND) {
                break;
            }
            assert(status == ASTRA_VFS_OK);
            assert(name[0] != '\0');
            /* A cursor that did not move is a listing that never ends. */
            assert(cursor > previous);
            assert(entries < FAKE_NODE_MAX);
            for (at = 0u; at < entries; ++at) {
                assert(strcmp(seen[at], name) != 0);
            }
            snprintf(seen[entries], sizeof(seen[entries]), "%s", name);
            ++entries;
        }
        assert(entries >= 2u);
        assert(fake_readdir_visits <= FAKE_NODE_MAX + entries);

        /* Past the last entry is a clean end, not an error to guess at. */
        assert(astra_vfs_readdir(&client, "/", cursor, name, sizeof(name),
                                 &kind, &cursor) == ASTRA_VFS_ERR_NOT_FOUND);
        /* A cursor nothing issued is refused rather than answered. */
        assert(astra_vfs_readdir(&client, "/", FAKE_NODE_MAX + 1u, name,
                                 sizeof(name), &kind,
                                 &cursor) == ASTRA_VFS_ERR_INVALID);

        {
            AstraVfsDirEntry entries[FAKE_NODE_MAX];
            const AstraVfsServiceStats *stats =
                astra_vfs_service_stats(&service);
            uint32_t requests = stats->requests;
            uint32_t count = 0u;

            cursor = 0u;
            assert(astra_vfs_readdir_batch(
                       &client, "/", cursor, entries, FAKE_NODE_MAX, &count,
                       &cursor) == ASTRA_VFS_OK);
            assert(count == 2u);
            assert(cursor == 0u);
            assert(stats->requests == requests + 1u);
            assert(entries[0].name[0] != '\0');
            assert(entries[1].name[0] != '\0');
        }

        /* A negotiated v3 peer keeps the one-entry API without a new op. */
        client.version = UINT16_C(3);
        cursor = 0u;
        {
            AstraVfsDirEntry entry;
            uint32_t count = 0u;

            assert(astra_vfs_readdir_batch(&client, "/", cursor, &entry, 1u,
                                            &count, &cursor) == ASTRA_VFS_OK);
            assert(count == 1u);
            assert(cursor != 0u);
        }
        client.version = ASTRA_VFS_VERSION;
    }

    assert(astra_vfs_unlink(&client, "/dir/note.txt") == ASTRA_VFS_OK);
    assert(astra_vfs_stat(&client, "/dir/note.txt", &size, &kind) ==
           ASTRA_VFS_ERR_NOT_FOUND);

    /* A path that cannot fit the record is refused, never truncated: a
     * truncated path names a different file. */
    {
        char huge[ASTRA_VFS_PATH_MAX + 16];

        memset(huge, 'a', sizeof(huge) - 1u);
        huge[sizeof(huge) - 1u] = '\0';
        assert(astra_vfs_stat(&client, huge, &size, &kind) ==
               ASTRA_VFS_ERR_INVALID);
    }

    assert(astra_vfs_disconnect(&client) == ASTRA_VFS_OK);
    assert(client.session == ASTRA_VFS_SESSION_INVALID);
}

int
main(void)
{
    test_session_handshake();
    test_file_round_trip();
    test_handle_discipline();
    test_session_release_frees_files();
    test_malformed_records();
    test_full_width_binary_write();
    test_limits();
    test_client_through_transport();
    puts("astra vfs service core: PASS");
    return 0;
}
