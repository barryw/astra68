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
uint32_t supervisor_service_definition_to_manifest(
    const AstraServiceDefinition *definition, SupervisorManifestEntry *entry);

#endif
