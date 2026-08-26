#ifndef ASTRA_POSIX_DESCRIPTOR_H
#define ASTRA_POSIX_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <astra/process.h>
#include <astra/vfs_process.h>

/*
 * The descriptor table, and the seam that keeps printf cheap.
 *
 * One table, because a descriptor has to be able to change what it is: a shell
 * redirecting output makes fd 1 a file while stdio goes on writing to fd 1, and
 * a table that kept streams and files apart could not express that.
 *
 * But a program that only prints must not drag the VFS client in behind
 * printf -- `hello` is 23 KiB and `ls` is 43 KiB, and the difference is the
 * filesystem. So the table knows nothing about files: it holds a kind and a
 * number, and a file's operations arrive as a vector that the file half
 * registers when something actually opens one. `status`, which prints nothing
 * and opens nothing, links neither.
 */

typedef struct AstraPosixFileOps {
    ssize_t (*read)(uint32_t slot, void *bytes, size_t length);
    ssize_t (*write)(uint32_t slot, const void *bytes, size_t length);
    int (*close)(uint32_t slot);
    off_t (*seek)(uint32_t slot, off_t offset, int whence);
    uint32_t (*exec_size)(void);
    int (*exec_export)(void *state, uint32_t capacity, uint32_t *used);
    int (*exec_import)(const AstraStartupInfo *startup, const void *state,
                       uint32_t size);
    int (*file_export)(uint32_t slot, void *state, uint32_t size);
    int (*file_import)(const void *state, uint32_t size, uint32_t *slot);
} AstraPosixFileOps;

#define ASTRA_POSIX_FILE_EXEC_STATE_SIZE ASTRA_PROCESS_FILE_STATE_SIZE

typedef AstraProcessFileState AstraPosixFileExecState;

_Static_assert(sizeof(AstraPosixFileExecState) ==
                   ASTRA_POSIX_FILE_EXEC_STATE_SIZE,
               "POSIX file exec state changed");

/* Installed by the file half the first time a program opens something. */
void astra_posix_file_bind(const AstraPosixFileOps *ops);

/*
 * Claims the lowest free descriptor for a file slot, or -1 with errno set.
 * POSIX promises "lowest available", and a shell about to dup2 onto fd 1
 * depends on it.
 */
int astra_posix_descriptor_file(uint32_t slot, int flags);

/* The file slot behind a descriptor, or -1 if it is not a file at all. */
int astra_posix_descriptor_slot(int fd);
/* The kernel handle behind a stream descriptor, or zero for files/closed. */
uint32_t astra_posix_descriptor_handle(int fd);

/* Builds the native wait set for one poll descriptor and reports anything
 * already ready.  At most two handles are returned: input and output. */
int astra_posix_descriptor_poll(int fd, short events, short *revents,
                                uint32_t handles[2], uint32_t *count);

/* The startup block `astra_posix_start` was given, for the file half's use. */
const AstraStartupInfo *astra_posix_startup(void);

int astra_posix_exec_export(void **state, uint32_t *size);
void astra_posix_file_prepare(void);

#endif
