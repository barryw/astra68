#ifndef ASTRA_VOLUME_PORT_H
#define ASTRA_VOLUME_PORT_H

#include <astra/volume_service.h>

/* One validation contract shared by the provider adapter and public client. */
int astra_volume_query_valid(const AstraVolumeCatalogQuery *query);
int astra_volume_control_valid(const AstraVolumeControl *control);
int astra_volume_format_valid(const AstraVolumeFormatRequest *request);
int astra_volume_partition_valid(const AstraVolumePartitionRequest *request);
int astra_volume_usage_query_valid(const AstraVolumeUsageQuery *query);
int astra_volume_usage_page_valid(const AstraVolumeUsageQuery *query, const AstraVolumeUsagePage *page);
int astra_volume_page_valid(const AstraVolumeCatalogQuery *query,
                            const AstraVolumeCatalogPage *page);
typedef uint32_t (*AstraVolumeReadCatalog)(void *context,
    const AstraVolumeCatalogQuery *query, AstraVolumeCatalogPage *page);
typedef uint32_t (*AstraVolumeGrantRoot)(void *context, uint32_t owner,
    const AstraVolumeReference *reference, uint32_t rights,
    const AstraVolumeReference *source, int delegates, uint32_t *root);
typedef uint32_t (*AstraVolumeApplyControl)(void *context, uint32_t owner,
    const AstraVolumeControl *control, AstraVolumeReference *reference);
typedef uint32_t (*AstraVolumeApplyFormat)(void *context, uint32_t owner,
    const AstraVolumeFormatRequest *request, uint32_t *outcome);
typedef uint32_t (*AstraVolumeApplyPartition)(void *context, uint32_t owner,
    const AstraVolumePartitionRequest *request, uint32_t *outcome);
typedef uint32_t (*AstraVolumeReadUsage)(void *context,
    const AstraVolumeUsageQuery *query, AstraVolumeUsagePage *page);
typedef struct AstraVolumeSubscriber AstraVolumeSubscriber;
typedef struct AstraVolumePort {
    uint32_t receive;
    uint32_t owner_limit;
    uint64_t next_subscription;
    AstraVolumeReadCatalog read;
    AstraVolumeGrantRoot root;
    AstraVolumeApplyControl control;
    AstraVolumeApplyFormat format;
    AstraVolumeApplyPartition partition;
    AstraVolumeReadUsage usage;
    void *context;
    AstraVolumeSubscriber *subscribers;
    AstraVolumeCatalogPage page;
    AstraVolumeUsagePage usage_page;
    AstraVolumeChange observed;
} AstraVolumePort;
int astra_volume_port_init(AstraVolumePort *port, uint32_t receive,
    uint32_t owner_limit, AstraVolumeReadCatalog read, void *context);
uint32_t astra_volume_port_pump(AstraVolumePort *port, uint32_t budget);
/* Broker authority comes from the selected receive endpoint, not the packet. */
uint32_t astra_volume_port_root_pump(AstraVolumePort *port, uint32_t receive,
    uint32_t rights, const AstraVolumeReference *source, int delegates,
    uint32_t budget);
uint32_t astra_volume_port_control_pump(AstraVolumePort *port, uint32_t receive,
    uint32_t budget);
uint32_t astra_volume_port_format_pump(AstraVolumePort *port, uint32_t receive,
    uint32_t budget);
uint32_t astra_volume_port_usage_pump(AstraVolumePort *port, uint32_t receive,
    uint32_t budget);
/* Sample and invalidate; also reap closed subscribers when nothing changed. */
void astra_volume_port_poll(AstraVolumePort *port);
void astra_volume_port_destroy(AstraVolumePort *port);
#endif
