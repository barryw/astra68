#ifndef ASTRA_CLIPBOARD_H
#define ASTRA_CLIPBOARD_H

/** @file clipboard.h @brief Typed, immutable system clipboard documents. */

#include <stdint.h>

#include <astra/area.h>
#include <astra/attributes.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

#define ASTRA_CLIPBOARD_TYPE_UTF8 "text/plain;charset=utf-8"

#define ASTRA_CLIPBOARD_DOCUMENT_MAGIC UINT32_C(0x41434c50) /* ACLP */
#define ASTRA_CLIPBOARD_DOCUMENT_VERSION UINT16_C(1)

/** One caller-owned representation used to build a clipboard document. */
typedef struct AstraClipboardRepresentation {
    uint32_t size;
    const char *type;
    uint32_t type_length;
    const void *data;
    uint32_t data_length;
    uint32_t reserved[4];
} AstraClipboardRepresentation;

#define ASTRA_CLIPBOARD_REPRESENTATION_INIT \
    { sizeof(AstraClipboardRepresentation), 0, 0u, 0, 0u, { 0, 0, 0, 0 } }

/** Serialized document header at the beginning of a clipboard area. */
typedef struct AstraClipboardDocumentHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t structure_size;
    uint32_t total_size;
    uint32_t representation_count;
    uint32_t records_offset;
    uint32_t reserved[3];
} AstraClipboardDocumentHeader;

/** Serialized representation record; offsets are document-relative. */
typedef struct AstraClipboardDocumentRecord {
    uint32_t type_offset;
    uint32_t type_length;
    uint32_t data_offset;
    uint32_t data_length;
} AstraClipboardDocumentRecord;

_Static_assert(sizeof(AstraClipboardDocumentHeader) == 32u,
               "clipboard document header ABI changed");
_Static_assert(sizeof(AstraClipboardDocumentRecord) == 16u,
               "clipboard document record ABI changed");

/** Owned read-only snapshot returned by ::astra_clipboard_read. */
typedef struct AstraClipboardItem {
    AstraArea _private_area;
    uint32_t generation;
    uint32_t _private_document_size;
    uint32_t reserved[2];
} AstraClipboardItem;

#define ASTRA_CLIPBOARD_ITEM_INIT \
    { ASTRA_AREA_INIT, 0u, 0u, { 0u, 0u } }

/** Build one canonical multi-representation document atomically. */
ASTRA_NODISCARD AstraResult astra_clipboard_document_write(
    void *document, uint32_t capacity,
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t *bytes);

/** Validate an untrusted serialized clipboard document. */
ASTRA_NODISCARD AstraResult astra_clipboard_document_validate(
    const void *document, uint32_t bytes);

/** Atomically replace the system clipboard with typed representations. */
ASTRA_NODISCARD AstraResult astra_clipboard_write(
    AstraHandle service,
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t *generation);

/** Acquire an immutable snapshot of the current system clipboard. */
ASTRA_NODISCARD AstraResult astra_clipboard_read(
    AstraHandle service, AstraClipboardItem *item);

/** Find the first representation with the exact counted type. */
ASTRA_NODISCARD AstraResult astra_clipboard_item_find(
    const AstraClipboardItem *item, const char *type, uint32_t type_length,
    const void **data, uint32_t *data_length);

/** Close a clipboard snapshot and invalidate its local view. */
ASTRA_NODISCARD AstraResult astra_clipboard_item_close(
    AstraClipboardItem *item);

/** Atomically clear the system clipboard. */
ASTRA_NODISCARD AstraResult astra_clipboard_clear(
    AstraHandle service, uint32_t *generation);

ASTRA_EXTERN_C_END

#endif
