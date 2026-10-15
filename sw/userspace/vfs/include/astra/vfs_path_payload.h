#ifndef ASTRA_VFS_PATH_PAYLOAD_H
#define ASTRA_VFS_PATH_PAYLOAD_H

#include <astra/vfs_client.h>

/* Fixed STOR control fields address complete NUL-terminated operands in the
 * existing transfer area. Native dispatch borrows only a validated snapshot,
 * never mutable client memory or an inline request-sized pathname. */
typedef struct AstraVfsPathOperands {
    const char *first;
    const char *second;
} AstraVfsPathOperands;

uint32_t astra_vfs_path_pack(const char *first, const char *second,
    void *bytes, uint32_t capacity, AstraVfsPathFields *fields,
    uint32_t *used);
uint32_t astra_vfs_path_measure(const char *path, uint32_t *length);
uint32_t astra_vfs_path_unpack(const AstraVfsPathFields *fields,
    const void *bytes, uint32_t size, uint32_t operands,
    AstraVfsPathOperands *paths);
uint32_t astra_vfs_path_operand_count(uint32_t operation,
    const AstraVfsRequest *request);
uint32_t astra_vfs_path_request(uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer,
    AstraVfsPathOperands *paths);

#endif
