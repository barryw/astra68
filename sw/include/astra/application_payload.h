#ifndef ASTRA_APPLICATION_PAYLOAD_H
#define ASTRA_APPLICATION_PAYLOAD_H

/* Internal APPL wire contract, shared by its native client and Supervisor. */
#include <astra/application_service.h>
#include <astra/types.h>
#include <astra/resource.h>

AstraResult astra_application_arguments_validate(
    const AstraApplicationLaunchArguments *arguments, const char *bytes,
    uint32_t capacity);

/* Consume the transferred area on every outcome. Copy before parsing so a
 * caller retaining another writer cannot mutate an admitted launch. */
AstraResult astra_application_payload_copy(
    const AstraApplicationLaunchArguments *arguments, AstraHandle *payload,
    char *bytes, uint32_t capacity);

#endif
