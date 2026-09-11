#ifndef ASTRA_CLIPBOARD_H
#define ASTRA_CLIPBOARD_H

/** @file clipboard.h @brief Typed, immutable system clipboard documents. */

#include <stdint.h>

#include <astra/area.h>
#include <astra/attributes.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Canonical MIME type for validated plain UTF-8 text. */
#define ASTRA_CLIPBOARD_TYPE_UTF8 "text/plain;charset=utf-8"

/** Serialized clipboard document magic (`ACLP`). */
#define ASTRA_CLIPBOARD_DOCUMENT_MAGIC UINT32_C(0x41434c50) /* ACLP */
/** Current serialized clipboard document version. */
#define ASTRA_CLIPBOARD_DOCUMENT_VERSION UINT16_C(1)

/** One caller-owned representation used to build a clipboard document. */
typedef struct AstraClipboardRepresentation {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Counted lowercase MIME type. */
    const char *type;
    /** Bytes at @ref type, excluding any terminator. */
    uint32_t type_length;
    /** Immutable representation bytes copied by a write. */
    const void *data;
    /** Bytes at @ref data. */
    uint32_t data_length;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraClipboardRepresentation;

/** Empty clipboard representation descriptor. */
#define ASTRA_CLIPBOARD_REPRESENTATION_INIT \
    { sizeof(AstraClipboardRepresentation), 0, 0u, 0, 0u, { 0, 0, 0, 0 } }

/** Serialized document header at the beginning of a clipboard area. */
typedef struct AstraClipboardDocumentHeader {
    /** ::ASTRA_CLIPBOARD_DOCUMENT_MAGIC. */
    uint32_t magic;
    /** ::ASTRA_CLIPBOARD_DOCUMENT_VERSION. */
    uint16_t version;
    /** Serialized header size. */
    uint16_t structure_size;
    /** Complete serialized document size. */
    uint32_t total_size;
    /** Number of following representation records. */
    uint32_t representation_count;
    /** Document-relative offset of the record table. */
    uint32_t records_offset;
    /** Must be zero. */
    uint32_t reserved[3];
} AstraClipboardDocumentHeader;

/** Serialized representation record; offsets are document-relative. */
typedef struct AstraClipboardDocumentRecord {
    /** Document-relative offset of the MIME type. */
    uint32_t type_offset;
    /** MIME-type byte count. */
    uint32_t type_length;
    /** Document-relative offset of the representation data. */
    uint32_t data_offset;
    /** Representation byte count. */
    uint32_t data_length;
} AstraClipboardDocumentRecord;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraClipboardDocumentHeader) == 32u,
               "clipboard document header ABI changed");
_Static_assert(sizeof(AstraClipboardDocumentRecord) == 16u,
               "clipboard document record ABI changed");
/** @endcond */

/** Owned read-only snapshot returned by ::astra_clipboard_read. */
typedef struct AstraClipboardItem {
    /** @cond ASTRA_INTERNAL */
    AstraArea _private_area;
    /** @endcond */
    /** Clipboard generation captured by this snapshot. */
    uint32_t generation;
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_document_size;
    /** @endcond */
    /** Must be zero. */
    uint32_t reserved[2];
} AstraClipboardItem;

/** Empty clipboard snapshot. */
#define ASTRA_CLIPBOARD_ITEM_INIT \
    { ASTRA_AREA_INIT, 0u, 0u, { 0u, 0u } }

/**
 * Build one canonical multi-representation document atomically.
 * @param document Destination storage, or null for a size query.
 * @param capacity Bytes available at @p document.
 * @param representations Representations to serialize.
 * @param count Number of representations.
 * @param bytes Receives the required or written document size.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL, or another negative
 *         ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_clipboard_document_write(
    void *document, uint32_t capacity,
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t *bytes);

/**
 * Validate an untrusted serialized clipboard document.
 * @param document Complete serialized document bytes.
 * @param bytes Available document bytes.
 * @return ::ASTRA_OK or a negative ::AstraResult validation error.
 */
ASTRA_NODISCARD AstraResult astra_clipboard_document_validate(
    const void *document, uint32_t bytes);

/**
 * Atomically replace the system clipboard with typed representations.
 * @param service Clipboard-service capability.
 * @param representations Representations copied into the new document.
 * @param count Number of representations.
 * @param generation Optional destination for the new generation.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_clipboard_write(
    AstraHandle service,
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t *generation);

/**
 * Acquire an immutable snapshot of the current system clipboard.
 * @param service Clipboard-service capability.
 * @param item Empty caller-owned snapshot initialized with
 *        ::ASTRA_CLIPBOARD_ITEM_INIT.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_clipboard_read(
    AstraHandle service, AstraClipboardItem *item);

/**
 * Find the first representation with the exact counted type.
 * @param item Open immutable clipboard snapshot.
 * @param type Counted MIME type to find.
 * @param type_length Bytes at @p type.
 * @param data Receives an item-backed immutable data pointer.
 * @param data_length Receives its byte count.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_NOT_PRESENT, or another negative
 *         ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_clipboard_item_find(
    const AstraClipboardItem *item, const char *type, uint32_t type_length,
    const void **data, uint32_t *data_length);

/**
 * Close a clipboard snapshot and invalidate its local view.
 * @param item Open snapshot to close.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_clipboard_item_close(
    AstraClipboardItem *item);

/**
 * Atomically clear the system clipboard.
 * @param service Clipboard-service capability.
 * @param generation Optional destination for the new generation.
 * @return ::ASTRA_OK or a negative ::AstraResult.
 */
ASTRA_NODISCARD AstraResult astra_clipboard_clear(
    AstraHandle service, uint32_t *generation);

ASTRA_EXTERN_C_END

#endif
