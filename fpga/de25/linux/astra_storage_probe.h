#ifndef ASTRA_STORAGE_PROBE_H
#define ASTRA_STORAGE_PROBE_H

#include "astra_storage_catalog.h"

#include <sys/types.h>

typedef struct AstraStorageProbeResult {
    uint64_t source;
    uint64_t generation;
    uint64_t completed_ns;
    uint32_t format;
    int32_t error;
    uint32_t ambiguous;
    char uuid[128];
    char label[256];
    char detected_type[64];
} AstraStorageProbeResult;

/* Single event-loop owner. The caller bounds admitted jobs; a timed-out child
 * remains charged until waitpid confirms exit, including uninterruptible I/O. */
typedef struct AstraStorageProbe {
    pid_t pid;
    int result_fd;
    uint64_t deadline_ns;
    uint64_t source;
    uint64_t generation;
    int cancelled;
} AstraStorageProbe;

int astra_storage_probe_start(AstraStorageProbe *probe, int source_fd,
                              uint64_t source, uint64_t generation,
                              uid_t uid, gid_t gid, uint32_t timeout_ms,
                              uint64_t offset, uint64_t length);
/* Zero: pending. One: exited and result ready. Negative: invalid job. */
int astra_storage_probe_poll(AstraStorageProbe *probe,
                             AstraStorageProbeResult *result);
void astra_storage_probe_cancel(AstraStorageProbe *probe);

#endif
