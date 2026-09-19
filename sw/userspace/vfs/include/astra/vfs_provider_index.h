#ifndef ASTRA_VFS_PROVIDER_INDEX_H
#define ASTRA_VFS_PROVIDER_INDEX_H

#include <stdint.h>

#include <astra/library.h>

/* Splits the canonical `<library-name>.<abi-major>` ELF identity. */
int astra_vfs_provider_identity_parse(
    const char *identity, char *name, uint32_t capacity, uint16_t *abi);

/**
 * Parse one versioned provider record and qualify its relative payload through
 * `assign`.
 *
 * Provider records deliberately contain no assign name. A restricted loader
 * can resolve the same index through LIBS: while an ordinary process resolves
 * it through LIBS:, without either namespace granting authority to the other.
 * The returned reference carries the canonical ELF identity
 * `<library-name>.<abi-major>`; the base name and ABI remain separate only in
 * the package index so compatible implementations can be selected.
 */
int astra_vfs_provider_index_parse(
    const uint8_t *bytes, uint32_t length, const char *assign,
    const char *name, uint16_t abi, char *path, uint32_t capacity,
    AstraLibraryReference *reference);

#endif
