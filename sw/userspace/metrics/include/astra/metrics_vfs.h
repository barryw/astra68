#ifndef ASTRA_METRICS_VFS_H
#define ASTRA_METRICS_VFS_H

#include <stdint.h>

#include <astra/vfs_backend.h>

typedef uint32_t (*AstraMetricRefresh)(void *context);

typedef struct AstraMetricVfs {
    AstraMetricRefresh refresh;
    void *refresh_context;
} AstraMetricVfs;

int astra_metric_vfs_init(AstraMetricVfs *metrics,
                          AstraMetricRefresh refresh, void *context);
const AstraVfsBackendOps *astra_metric_vfs_ops(void);

#endif
