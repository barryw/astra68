#ifndef ASTRA_DOCUMENT_STORE_H
#define ASTRA_DOCUMENT_STORE_H

#include <astra/filesystem_library.h>

/* Internal Filesystem Kit primitives for complete, private user documents.
 * Callers own the document format and publication policy. */
uint32_t astra_document_path_join(const char *directory, const char *name,
    char *out, uint32_t capacity);
uint32_t astra_document_store_prepare(AstraFilesystem *filesystem,
    const char *directory);
uint32_t astra_document_stage(AstraFilesystem *filesystem, const char *path,
    const uint8_t *bytes, uint32_t length);
uint32_t astra_document_read(AstraFilesystem *filesystem, const char *path,
    uint32_t minimum, uint32_t maximum, uint8_t **bytes, uint32_t *length);

#endif
