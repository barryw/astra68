#ifndef ASTRA_CONFIG_DOCUMENT_H
#define ASTRA_CONFIG_DOCUMENT_H

#include <stdint.h>

#include <astra/config_library.h>

typedef struct AstraConfigDocumentError {
    uint32_t line;
    uint32_t version;
} AstraConfigDocumentError;

enum {
    ASTRA_CONFIG_OWNER_SYSTEM = 1,
    ASTRA_CONFIG_OWNER_SERVICE = 2,
    ASTRA_CONFIG_OWNER_COMMAND = 3,
    ASTRA_CONFIG_OWNER_APPLICATION = 4
};

#define ASTRA_CONFIG_COMMANDS_CAPABILITY "CONFIG_COMMANDS"

uint32_t astra_config_scope_root(const char *parent, uint32_t owner_kind,
                                 char *out, uint32_t capacity);
uint32_t astra_config_owner_root(const char *parent, const char *owner,
                                 char *out, uint32_t capacity);
uint32_t astra_config_capability_root(const char *parent, uint32_t owner_kind,
                                      const char *owner, char *out,
                                      uint32_t capacity);

uint32_t astra_config_document_validate(const char *text, uint32_t length,
                                        uint32_t schema_version,
                                        AstraConfigDocumentError *error);
uint32_t astra_config_document_count(const char *text, uint32_t length,
                                     const char *key, uint32_t *count);
uint32_t astra_config_document_get(const char *text, uint32_t length,
                                   const char *key, uint32_t index,
                                   char *out, uint32_t capacity,
                                   uint32_t *value_length);
uint32_t astra_config_document_get_i64(const char *text, uint32_t length,
                                       const char *key, uint32_t index,
                                       int64_t *value);
uint32_t astra_config_document_get_u64(const char *text, uint32_t length,
                                       const char *key, uint32_t index,
                                       uint64_t *value);
uint32_t astra_config_document_get_bool(const char *text, uint32_t length,
                                        const char *key, uint32_t index,
                                        int *value);
uint32_t astra_config_document_replace(
    const char *text, uint32_t length, const char *key, uint32_t index,
    const char *value, char *out, uint32_t capacity, uint32_t *required);
uint32_t astra_config_document_remove(
    const char *text, uint32_t length, const char *key, uint32_t index,
    char *out, uint32_t capacity, uint32_t *required);

#endif
