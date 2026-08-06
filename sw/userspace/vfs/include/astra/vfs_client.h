#ifndef ASTRA_VFS_CLIENT_H
#define ASTRA_VFS_CLIENT_H

#include <stdint.h>

#include <astra/vfs_service.h>

/*
 * The client side of the storage protocol: the Kit half.
 *
 * This exists so a caller writes astra_vfs_open() instead of building a
 * record, and so that the day the service moves into its own process the
 * caller does not change at all. Everything it does is marshal a request,
 * hand it to a transport, and unpack the reply.
 *
 * The transport is a callback rather than a port handle because that is the
 * whole point of the exercise. Today the service is a call away in the same
 * process; tomorrow it is a port away in another one. Only the function behind
 * this pointer changes, and no caller of the Kit is recompiled for it.
 *
 * See docs/DRIVER_AND_SERVICE_ARCHITECTURE.md 9.2: this file's version and the
 * protocol's version are different numbers on purpose.
 */

typedef uint32_t (*AstraVfsTransport)(void *context, uint32_t operation,
                                      const AstraVfsRequest *request,
                                      AstraVfsReply *reply);

typedef struct AstraVfsClient {
    AstraVfsTransport transport;
    void *context;
    uint32_t session;
    uint16_t version;       /* the version agreed at connect */
    /*
     * What the owner of this client is currently doing. Stamped on every
     * request so one request is one story across every process it touches.
     * Zero until somebody sets it, which reads as no activity.
     */
    uint32_t activity;
    /*
     * The in-flight records live here rather than on the caller's stack, and
     * that is a requirement rather than a preference: a user thread gets one
     * 4 KiB stack, and at 224 bytes each a request/reply pair per frame sank
     * the shell -> Kit -> service -> backend -> lwext4 chain into a fault at
     * the first command. Holding them in the client costs the same memory once
     * instead of once per call depth, and makes it caller-owned and countable
     * like every other buffer in this system.
     *
     * The protocol is synchronous, so one client has at most one request
     * outstanding. Two threads sharing a client would corrupt these; a thread
     * that needs its own gets its own client, which is what a session is for.
     */
    AstraVfsRequest request;
    AstraVfsReply reply;
} AstraVfsClient;

/*
 * Performs the HELLO handshake and records the agreed version. Returns an
 * ASTRA_VFS_* status; the client is unusable unless this returns ASTRA_VFS_OK.
 */
uint32_t astra_vfs_connect(AstraVfsClient *client, AstraVfsTransport transport,
                           void *context);
uint32_t astra_vfs_disconnect(AstraVfsClient *client);

uint32_t astra_vfs_open(AstraVfsClient *client, const char *path,
                        uint32_t flags, AstraVfsFile *file,
                        uint64_t *size, uint16_t *kind);
uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file);

/*
 * Reads and writes are short by design: one message carries at most
 * ASTRA_VFS_IO_MAX bytes. `moved` always reports what actually happened, and a
 * caller that needs more loops. Writing that loop now is not wasted work --
 * when bulk transfer moves to shared rings a transfer can still be short, and
 * a caller that already loops does not change.
 */
uint32_t astra_vfs_read(AstraVfsClient *client, AstraVfsFile file,
                        uint64_t offset, void *buffer, uint32_t length,
                        uint32_t *moved);
uint32_t astra_vfs_write(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, const void *buffer, uint32_t length,
                         uint32_t *moved);

uint32_t astra_vfs_stat(AstraVfsClient *client, const char *path,
                        uint64_t *size, uint16_t *kind);
/*
 * One entry, resuming from `cursor`; zero begins a scan and `*next` is what to
 * pass for the entry after this one. Returns ASTRA_VFS_ERR_NOT_FOUND once a
 * scan passes the last entry, which is how a listing ends rather than a
 * failure.
 */
uint32_t astra_vfs_readdir(AstraVfsClient *client, const char *path,
                           uint64_t cursor, char *name, uint32_t capacity,
                           uint16_t *kind, uint64_t *next);
uint32_t astra_vfs_mkdir(AstraVfsClient *client, const char *path);
uint32_t astra_vfs_unlink(AstraVfsClient *client, const char *path);

#endif
