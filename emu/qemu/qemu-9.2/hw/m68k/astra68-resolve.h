#ifndef ASTRA68_RESOLVE_H
#define ASTRA68_RESOLVE_H

#include <stdint.h>
#include <sys/types.h>

#define ASTRA_RESOLVE_FOLLOW_FINAL 1u
/* Mutations name the final directory entry, even with a trailing slash. */
#define ASTRA_RESOLVE_ENTRY 2u
#define ASTRA_RESOLVE_LINK_MAX 40u
#define ASTRA_RESOLVE_ESCAPE_PARENT 1u
#define ASTRA_RESOLVE_ESCAPE_SYMLINK 2u

typedef struct AstraResolution {
    int parent;                 /* caller closes on success */
    char leaf[256];              /* empty: parent itself */
    uint32_t trailing;
    uint32_t missing;
    uint32_t escape;
    uint32_t consumed;           /* bytes in the original request */
    uint32_t links;              /* links followed, including an escape */
    char continuation[4096];     /* absolute Astra namespace path on escape */
} AstraResolution;

/* Ordered resolution beneath root. An absolute path starts at root; an
 * empty path names base. A root escape is -EXDEV with its original position.
 * A base or result moved outside root is -ESTALE. No host escape is followed. */
int astra_resolve(int root, int base, const char *path, unsigned flags,
                  uint32_t links_left, AstraResolution *out);
/* Descriptor-based confinement, also checked after the final atomic open. */
int astra_resolve_rooted(int root, int fd);
/* Current directory spelling beneath root, revalidated against its inode.
 * An unlinked or moved-out directory has no name and returns -ENOENT. */
int astra_resolve_directory_path(int root, int fd, char *path,
                                  uint32_t capacity, uint32_t *length);
/* Open the resolved entry with RESOLVE_BENEATH and no final link traversal.
 * Truncation happens only after rechecking the held parent's ancestry. */
int astra_resolve_open(int root, const AstraResolution *out, int flags,
                       mode_t mode);
/* Open a path through the same ordered contract. Non-mutating opens can use
 * one confined Linux lookup; links and namespace continuations use the walk.
 * The returned resolution never owns a parent descriptor. */
int astra_resolve_open_path(int root, int base, const char *path, int flags,
                            mode_t mode, unsigned resolve, uint32_t links_left,
                            AstraResolution *out);

#endif
