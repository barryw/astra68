/*
 * Protocol statuses as words, in one place.
 *
 * These are not errno and they are not a filesystem's error numbers: they are
 * the values every client of the storage protocol sees, whatever answered.
 * The table lived in the shell while the shell was the only thing that had to
 * say them out loud. It stopped being that when `cat`, `mkdir` and `rm` became
 * programs -- four copies of one table is four chances for a machine to call
 * the same refusal two different things, which is exactly the confusion a
 * person reading a refusal cannot afford.
 */

#include <stddef.h>

#include <astra/vfs_service.h>

const char *
astra_vfs_status_text(uint32_t status)
{
    static const char *const text[] = {
        "ok", "protocol error", "not found", "already exists",
        "not a directory", "is a directory", "access denied", "no space",
        "invalid", "bad handle", "limit reached", "I/O error", "not empty",
        "unsupported", "busy", "buffer too small",
        /*
         * 16, and the first status here that a transport produces rather than
         * a filesystem. "not found" is a volume answering; this is nobody
         * answering, and a person needs to be able to tell those apart at the
         * prompt because only one of them is worth retrying.
         */
        "the service is gone"
    };

    if (status >= (uint32_t)(sizeof(text) / sizeof(text[0])))
        return NULL;
    return text[status];
}
