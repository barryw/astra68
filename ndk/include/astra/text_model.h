#ifndef ASTRA_TEXT_MODEL_H
#define ASTRA_TEXT_MODEL_H

/**
 * @file text_model.h
 * @brief Shared allocation-free UTF-8 piece-table document model.
 *
 * A model belongs to one document-owning thread. Text and metadata live in
 * caller-owned arenas and can be moved into larger arenas without changing
 * the document. There is no independent byte, piece, line, or edit ceiling.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Half-open UTF-8 byte selection. Both offsets are scalar boundaries. */
typedef struct AstraTextSelection {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Fixed selection end as a document byte offset. */
    uint32_t anchor;
    /** Moving selection end as a document byte offset. */
    uint32_t focus;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraTextSelection;

/** Empty selection at the start of a document. */
#define ASTRA_TEXT_SELECTION_INIT \
    { sizeof(AstraTextSelection), 0u, 0u, { 0u, 0u, 0u, 0u } }

/** Construction data for a caller-owned UTF-8 text model. */
typedef struct AstraTextModelInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Initial UTF-8 bytes copied during initialization; null when empty. */
    const char *text;
    /** Number of initial bytes at @ref text. */
    uint32_t text_bytes;
    /** Caller-owned arena retaining immutable text chunks. */
    void *content_arena;
    /** Bytes available at @ref content_arena. */
    uint32_t content_arena_bytes;
    /** Four-byte-aligned caller-owned piece and line-index storage. */
    void *metadata_arena;
    /** Bytes available at @ref metadata_arena. */
    uint32_t metadata_arena_bytes;
    /** Initial scalar-boundary selection. */
    AstraTextSelection selection;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraTextModelInfo;

/** Empty text-model construction data. */
#define ASTRA_TEXT_MODEL_INFO_INIT {                                      \
    sizeof(AstraTextModelInfo), 0, 0u, 0, 0u, 0, 0u,                     \
    ASTRA_TEXT_SELECTION_INIT, { 0u, 0u, 0u, 0u }                        \
}

/** Exact retained capacities required by one replacement. */
typedef struct AstraTextModelRequirements {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Content-arena bytes required after the replacement. */
    uint32_t content_arena_bytes;
    /** Metadata-arena bytes required after the replacement. */
    uint32_t metadata_arena_bytes;
    /** Piece count after adjacent chunks are coalesced. */
    uint32_t piece_count;
    /** Indexed line count after the replacement. */
    uint32_t line_count;
    /** Resulting document byte length. */
    uint32_t text_bytes;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraTextModelRequirements;

/** Empty replacement-requirements result. */
#define ASTRA_TEXT_MODEL_REQUIREMENTS_INIT {                              \
    sizeof(AstraTextModelRequirements), 0u, 0u, 0u, 0u, 0u,              \
    { 0u, 0u, 0u, 0u }                                                   \
}

/** One indexed logical line. */
typedef struct AstraTextLine {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Inclusive UTF-8 byte offset of the line's first byte. */
    uint32_t start;
    /** Exclusive content end, before a terminating U+000A when present. */
    uint32_t content_end;
    /** Exclusive end including a terminating U+000A when present. */
    uint32_t end;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraTextLine;

/** Empty logical-line result. */
#define ASTRA_TEXT_LINE_INIT \
    { sizeof(AstraTextLine), 0u, 0u, 0u, { 0u, 0u, 0u, 0u } }

/** Observable text-model state and arena accounting. */
typedef struct AstraTextModelState {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Monotonic nonzero generation changed by every successful mutation. */
    uint32_t generation;
    /** Current document length in UTF-8 bytes. */
    uint32_t text_bytes;
    /** Number of indexed logical lines; an empty document has one line. */
    uint32_t line_count;
    /** Number of retained piece-table entries. */
    uint32_t piece_count;
    /** Occupied bytes in the content arena, including unreachable history. */
    uint32_t content_bytes_used;
    /** Total content-arena capacity. */
    uint32_t content_arena_bytes;
    /** Occupied piece and line-index bytes. */
    uint32_t metadata_bytes_used;
    /** Total metadata-arena capacity. */
    uint32_t metadata_arena_bytes;
    /** Current scalar-boundary selection. */
    AstraTextSelection selection;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraTextModelState;

/** Empty text-model state result. */
#define ASTRA_TEXT_MODEL_STATE_INIT {                                     \
    sizeof(AstraTextModelState), 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,         \
    ASTRA_TEXT_SELECTION_INIT, { 0u, 0u, 0u, 0u }                        \
}

/** Caller-owned UTF-8 piece table. Private fields must not be modified. */
typedef struct AstraTextModel {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_structure_size;
    uint8_t *_private_content;
    uint32_t _private_content_capacity;
    uint32_t _private_content_used;
    uint8_t *_private_metadata;
    uint32_t _private_metadata_capacity;
    uint32_t _private_piece_count;
    uint32_t _private_line_count;
    uint32_t _private_text_bytes;
    uint32_t _private_anchor;
    uint32_t _private_focus;
    uint32_t _private_generation;
    uint32_t _private_reserved[4];
    /** @endcond */
} AstraTextModel;

/** Empty caller-owned text model. */
#define ASTRA_TEXT_MODEL_INIT {                                           \
    sizeof(AstraTextModel), 0, 0u, 0u, 0, 0u, 0u, 0u, 0u, 0u, 0u, 0u,   \
    { 0u, 0u, 0u, 0u }                                                   \
}

/**
 * Initialize a model and copy its initial UTF-8 document.
 * @param model Empty state initialized with ::ASTRA_TEXT_MODEL_INIT.
 * @param info Initial text, arenas, and selection.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL, or a validation error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_init(
    AstraTextModel *model, const AstraTextModelInfo *info);

/**
 * Perform a complete invariant and UTF-8 validation pass.
 * @param model Initialized model.
 * @return ::ASTRA_OK or ::ASTRA_ERROR_INVALID_ARGUMENT.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_validate(
    const AstraTextModel *model);

/**
 * Read current length, line index, selection, generation, and arena usage.
 * @param model Initialized model.
 * @param state Result initialized with ::ASTRA_TEXT_MODEL_STATE_INIT.
 * @return ::ASTRA_OK or ::ASTRA_ERROR_INVALID_ARGUMENT.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_get_state(
    const AstraTextModel *model, AstraTextModelState *state);

/**
 * Replace the current selection without changing document bytes.
 * @param model Initialized model.
 * @param selection Scalar-boundary anchor and focus offsets.
 * @return ::ASTRA_OK or ::ASTRA_ERROR_INVALID_ARGUMENT.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_set_selection(
    AstraTextModel *model, const AstraTextSelection *selection);

/**
 * Calculate exact post-edit storage requirements without changing the model.
 * @param model Initialized model.
 * @param start Inclusive scalar-boundary byte offset.
 * @param end Exclusive scalar-boundary byte offset, at least @p start.
 * @param replacement Valid UTF-8 replacement bytes; null only when empty.
 * @param replacement_bytes Byte count at @p replacement.
 * @param requirements Result initialized with
 *        ::ASTRA_TEXT_MODEL_REQUIREMENTS_INIT.
 * @return ::ASTRA_OK or a negative validation or size error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_replace_requirements(
    const AstraTextModel *model, uint32_t start, uint32_t end,
    const char *replacement, uint32_t replacement_bytes,
    AstraTextModelRequirements *requirements);

/**
 * Atomically replace one UTF-8 range and collapse selection after new text.
 * Capacity failure leaves document bytes, selection, and generation unchanged.
 * @param model Initialized model.
 * @param start Inclusive scalar-boundary byte offset.
 * @param end Exclusive scalar-boundary byte offset, at least @p start.
 * @param replacement Valid UTF-8 replacement bytes; null only when empty.
 * @param replacement_bytes Byte count at @p replacement.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL, or another error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_replace(
    AstraTextModel *model, uint32_t start, uint32_t end,
    const char *replacement, uint32_t replacement_bytes);

/**
 * Copy one UTF-8 range atomically; null output measures the required bytes.
 * @param model Initialized model.
 * @param start Inclusive scalar-boundary byte offset.
 * @param end Exclusive scalar-boundary byte offset, at least @p start.
 * @param output Destination outside the model arenas, or null only when
 *        @p capacity is zero.
 * @param capacity Bytes available at @p output.
 * @param bytes Receives required or copied bytes.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL, or another error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_copy(
    const AstraTextModel *model, uint32_t start, uint32_t end,
    char *output, uint32_t capacity, uint32_t *bytes);

/**
 * Borrow the contiguous piece beginning at a document byte offset.
 * The span remains valid until the next mutation or arena move.
 * @param model Initialized model.
 * @param offset Scalar-boundary document byte offset.
 * @param text Receives a borrowed pointer, or null at end of document.
 * @param bytes Receives contiguous bytes available from @p offset.
 * @return ::ASTRA_OK or ::ASTRA_ERROR_INVALID_ARGUMENT.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_read(
    const AstraTextModel *model, uint32_t offset,
    const char **text, uint32_t *bytes);

/**
 * Advance an in/out document offset by one Unicode scalar.
 * Failure at end of document leaves the offset unchanged.
 * @param model Initialized model.
 * @param offset In/out scalar-boundary document byte offset.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_NOT_PRESENT, or a validation error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_scalar_advance(
    const AstraTextModel *model, uint32_t *offset);

/**
 * Retreat an in/out document offset by one Unicode scalar.
 * Failure at the start of document leaves the offset unchanged.
 * @param model Initialized model.
 * @param offset In/out scalar-boundary document byte offset.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_NOT_PRESENT, or a validation error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_scalar_retreat(
    const AstraTextModel *model, uint32_t *offset);

/**
 * Resolve one indexed logical line without scanning preceding document text.
 * @param model Initialized model.
 * @param line_index Zero-based line number.
 * @param line Result initialized with ::ASTRA_TEXT_LINE_INIT.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_NOT_PRESENT, or a validation error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_get_line(
    const AstraTextModel *model, uint32_t line_index, AstraTextLine *line);

/**
 * Compact live text into replacement non-overlapping caller-owned arenas.
 * Old occupied storage is wiped after the atomic move.
 * @param model Initialized model.
 * @param content_arena Replacement content storage, or null when empty and
 *        @p content_arena_bytes is zero.
 * @param content_arena_bytes Replacement content capacity.
 * @param metadata_arena Four-byte-aligned replacement metadata storage.
 * @param metadata_arena_bytes Replacement metadata capacity.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL, or a validation error.
 * @since Interface Kit 2.6.0.
 */
ASTRA_NODISCARD AstraResult astra_text_model_move_arenas(
    AstraTextModel *model, void *content_arena, uint32_t content_arena_bytes,
    void *metadata_arena, uint32_t metadata_arena_bytes);

/**
 * Wipe occupied arenas and return the caller-owned object to its empty state.
 * @param model Initialized model; null is ignored.
 * @since Interface Kit 2.6.0.
 */
void astra_text_model_dispose(AstraTextModel *model);

ASTRA_EXTERN_C_END

#endif
