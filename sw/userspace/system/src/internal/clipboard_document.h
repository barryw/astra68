#ifndef ASTRA_SYSTEM_CLIPBOARD_DOCUMENT_H
#define ASTRA_SYSTEM_CLIPBOARD_DOCUMENT_H

#include <astra/clipboard.h>

/* Shared immutable ACLP snapshot builder for Clipboard and pointer drags.
 * Caller closes both outputs, even after a later transport failure. */
AstraResult astra_system_document_snapshot(
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t byte_limit, AstraArea *area, AstraHandle *immutable,
    uint32_t *document_bytes);

#endif
