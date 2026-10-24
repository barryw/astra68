#ifndef ASTRA_VOLUME_SERVICE_H
#define ASTRA_VOLUME_SERVICE_H

#include <astra/message_abi.h>
#include <astra/volume.h>

/* Native metadata control. Bulk pages use caller-owned output areas; the
 * service never reads authority or policy from those areas. */
#define ASTRA_VOLUME_PROTOCOL UINT32_C(0x564f4c53)
#define ASTRA_VOLUME_PROTOCOL_VERSION 4u
enum {
    ASTRA_VOLUME_QUERY = 1u,
    ASTRA_VOLUME_SUBSCRIBE,
    ASTRA_VOLUME_UNSUBSCRIBE,
    ASTRA_VOLUME_REPLY,
    ASTRA_VOLUME_CHANGED,
    ASTRA_VOLUME_ROOT,
    ASTRA_VOLUME_CONTROL,
    ASTRA_VOLUME_USAGE,
    ASTRA_VOLUME_BROKER_INFO,
    ASTRA_VOLUME_FORMAT,
    ASTRA_VOLUME_FORMAT_STATUS,
    ASTRA_VOLUME_PARTITION,
    ASTRA_VOLUME_PARTITION_STATUS
};
typedef struct AstraVolumePartitionRequest {
    AstraMessageHeader header;
    AstraVolumeReference device;
    AstraVolumePartitionEdit edit;
} AstraVolumePartitionRequest;
typedef struct AstraVolumeFormatRequest {
    AstraMessageHeader header;
    AstraVolumeReference source;
    uint32_t format;
    uint32_t reserved;
    char label[12];
    uint32_t reserved2;
} AstraVolumeFormatRequest;
typedef struct AstraVolumeUsageRequest {
    AstraMessageHeader header;
    AstraVolumeUsageQuery query;
} AstraVolumeUsageRequest;
typedef struct AstraVolumeControlRequest {
    AstraMessageHeader header;
    AstraVolumeControl control;
} AstraVolumeControlRequest;
typedef struct AstraVolumeRootRequest {
    AstraMessageHeader header;
    AstraVolumeReference reference;
    uint32_t rights;
    uint32_t reserved;
} AstraVolumeRootRequest;
typedef struct AstraVolumeRequest {
    AstraMessageHeader header;
    AstraVolumeCatalogQuery query;
    uint64_t subscription;
} AstraVolumeRequest;
typedef struct AstraVolumeReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t reserved;
    uint64_t subscription;
} AstraVolumeReply;
typedef struct AstraVolumeFormatReply {
    AstraVolumeReply reply;
    uint32_t outcome;
    uint32_t reserved;
} AstraVolumeFormatReply;
typedef struct AstraVolumePartitionReply {
    AstraVolumeReply reply;
    uint32_t outcome;
    uint32_t reserved;
} AstraVolumePartitionReply;
typedef struct AstraVolumeControlReply {
    AstraVolumeReply reply;
    AstraVolumeReference reference;
} AstraVolumeControlReply;
typedef struct AstraVolumeBrokerInfoReply {
    AstraVolumeReply reply;
    AstraVolumeBrokerInfo info;
} AstraVolumeBrokerInfoReply;
typedef struct AstraVolumeNotification {
    AstraMessageHeader header;
    uint64_t subscription;
    AstraVolumeChange change;
} AstraVolumeNotification;

_Static_assert(sizeof(AstraVolumeRequest) == 64u, "volume request ABI");
_Static_assert(sizeof(AstraVolumeRootRequest) == 56u, "volume root request ABI");
_Static_assert(sizeof(AstraVolumeReply) == 40u, "volume reply ABI");
_Static_assert(sizeof(AstraVolumeControlRequest) == 80u, "volume control request ABI");
_Static_assert(sizeof(AstraVolumeControlReply) == 64u, "volume control reply ABI");
_Static_assert(sizeof(AstraVolumeNotification) == 56u, "volume notification ABI");
_Static_assert(sizeof(AstraVolumeUsageRequest) == 88u, "volume usage request ABI");
_Static_assert(sizeof(AstraVolumeBrokerInfo) == 32u, "volume broker info ABI");
_Static_assert(sizeof(AstraVolumeBrokerInfoReply) == 72u, "volume broker reply ABI");
_Static_assert(sizeof(AstraVolumeFormatRequest) == 72u, "volume format request ABI");
_Static_assert(sizeof(AstraVolumeFormatReply) == 48u, "volume format reply ABI");
_Static_assert(sizeof(AstraVolumePartitionRequest) == 80u, "volume partition request ABI");
_Static_assert(sizeof(AstraVolumePartitionReply) == 48u, "volume partition reply ABI");
#endif
