#ifndef ASTRA_SUPERVISOR_SERVICE_DEFINITION_STORE_H
#define ASTRA_SUPERVISOR_SERVICE_DEFINITION_STORE_H

#include <stdint.h>

#include <astra/service_manager_abi.h>

#include <loader.h>

#define SUPERVISOR_SERVICE_DEFINITION_SCHEMA 1u

uint32_t supervisor_service_definition_parse(
    const char *text, uint32_t length, AstraServiceDefinition *definition,
    uint32_t *error_line);
uint32_t supervisor_service_definition_serialize(
    const AstraServiceDefinition *definition, char *out, uint32_t capacity,
    uint32_t *required);
uint32_t supervisor_service_definition_from_manifest(
    const SupervisorManifestEntry *entry, AstraServiceDefinition *definition);
/* A user's policy.conf for a manifest service, over its manifest default.
 * Only NOT_FOUND admits the defaults; read/parse failures leave them untouched
 * and refuse the operation with the actual error. */
uint32_t supervisor_service_policy_apply(uint32_t read_status, const char *text, uint32_t length,
                                         AstraServiceDefinition *definition);
uint32_t supervisor_service_policy_serialize(
    const AstraServiceDefinition *definition, char *out, uint32_t capacity,
    uint32_t *required);
uint32_t supervisor_service_definition_to_manifest(
    const AstraServiceDefinition *definition, SupervisorManifestEntry *entry);

#endif
