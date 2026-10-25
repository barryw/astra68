#ifndef ASTRA_SERVICE_DEFINITION_PAYLOAD_H
#define ASTRA_SERVICE_DEFINITION_PAYLOAD_H

#include <astra/service_manager.h>

AstraResult astra_service_definition_path_set(AstraServiceDefinition *definition,
    const char *path);
uint32_t astra_service_definition_payload_size(const AstraServiceDefinition *definition);
AstraResult astra_service_definition_encode(const AstraServiceDefinition *definition,
    void *bytes, uint32_t capacity);
AstraResult astra_service_definition_decode(const void *bytes, uint32_t length,
    AstraServiceDefinition *definition);
AstraResult astra_service_info_decode(const AstraServiceInfoRecord *record,
    const char *path, uint32_t length, AstraServiceInfo *info);
void astra_service_info_record(const AstraServiceInfo *info, AstraServiceInfoRecord *record);

#endif
