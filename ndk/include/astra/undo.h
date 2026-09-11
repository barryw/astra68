#ifndef ASTRA_UNDO_H
#define ASTRA_UNDO_H

/**
 * @file undo.h
 * @brief Allocation-free, named application undo histories.
 *
 * A manager belongs to one document and is driven by that document's owning
 * thread. Calls on the same manager are not concurrent; callbacks may update
 * the document but may not re-enter the manager. There is no transaction or
 * action-count ceiling: retained history is limited only by the caller-owned
 * arena, which can be replaced with a larger arena without losing history.
 */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

enum {
    /** Apply an action's inverse representation. */
    ASTRA_UNDO_DIRECTION_UNDO = 1u,
    /** Apply an action's forward representation. */
    ASTRA_UNDO_DIRECTION_REDO = 2u
};

enum {
    /** At least one applied group can be undone. */
    ASTRA_UNDO_STATE_CAN_UNDO = 1u << 0,
    /** At least one reverted group can be redone. */
    ASTRA_UNDO_STATE_CAN_REDO = 1u << 1,
    /** The current history position is the last marked save point. */
    ASTRA_UNDO_STATE_CLEAN = 1u << 2,
    /** An action and its compensation both failed; history is unusable. */
    ASTRA_UNDO_STATE_POISONED = 1u << 3
};

/**
 * Apply one application-defined, serializable action payload.
 *
 * An individual call must either complete or leave the document unchanged.
 * The manager compensates already completed calls when another action in the
 * same group fails.
 */
typedef AstraResult (*AstraUndoApply)(void *context, uint32_t operation,
                                      uint32_t direction,
                                      const void *payload,
                                      uint32_t payload_bytes);

/** One typed action copied into a history group. */
typedef struct AstraUndoAction {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Nonzero application-defined operation identifier. */
    uint32_t operation;
    /** Opaque serializable data understood by the registered apply function. */
    const void *payload;
    /** Number of bytes at @ref payload; zero permits a null payload. */
    uint32_t payload_bytes;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraUndoAction;

/** Empty initializer for ::AstraUndoAction. */
#define ASTRA_UNDO_ACTION_INIT \
    { sizeof(AstraUndoAction), 0u, 0, 0u, { 0u, 0u, 0u, 0u } }

/** One user-visible transaction containing one or more actions. */
typedef struct AstraUndoGroupInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** UTF-8 action name used by menu labels; may be null when length is zero. */
    const char *name;
    /** Counted bytes at @ref name, excluding any terminator. */
    uint32_t name_length;
    /** Ordered forward actions. */
    const AstraUndoAction *actions;
    /** Number of actions; there is no independent count ceiling. */
    uint32_t action_count;
    /** Nonzero identity eligible to join the immediately preceding group. */
    uint32_t coalesce_id;
    /** Monotonic time of the newest action in this group. */
    uint64_t timestamp_ns;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraUndoGroupInfo;

/** Empty initializer for ::AstraUndoGroupInfo. */
#define ASTRA_UNDO_GROUP_INFO_INIT \
    { sizeof(AstraUndoGroupInfo), 0, 0u, 0, 0u, 0u, 0u, \
      { 0u, 0u, 0u, 0u } }

/** Construction data for a caller-owned manager and arena. */
typedef struct AstraUndoManagerInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Four-byte-aligned caller-owned storage retained for the manager lifetime. */
    void *arena;
    /** Bytes available at @ref arena. */
    uint32_t arena_bytes;
    /** Maximum pause between equal nonzero coalesce IDs; zero disables it. */
    uint64_t coalesce_interval_ns;
    /** Application operation dispatcher. */
    AstraUndoApply apply;
    /** Opaque document context passed to @ref apply. */
    void *context;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraUndoManagerInfo;

/** Empty initializer for ::AstraUndoManagerInfo. */
#define ASTRA_UNDO_MANAGER_INFO_INIT \
    { sizeof(AstraUndoManagerInfo), 0, 0u, 0u, 0, 0, \
      { 0u, 0u, 0u, 0u } }

/** Caller-owned undo state. Private fields must not be modified. */
typedef struct AstraUndoManager {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_structure_size;
    void *_private_arena;
    uint32_t _private_capacity;
    uint32_t _private_used;
    uint32_t _private_cursor;
    uint32_t _private_clean_cursor;
    uint32_t _private_generation;
    uint32_t _private_group_count;
    uint32_t _private_applied_group_count;
    uint32_t _private_mode;
    uint32_t _private_poisoned;
    uint64_t _private_coalesce_interval_ns;
    AstraUndoApply _private_apply;
    void *_private_context;
    uint32_t _private_reserved[4];
    /** @endcond */
} AstraUndoManager;

/** Empty initializer for ::AstraUndoManager. */
#define ASTRA_UNDO_MANAGER_INIT \
    { sizeof(AstraUndoManager), 0, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, \
      0u, 0, 0, { 0u, 0u, 0u, 0u } }

/** Snapshot used to drive Edit menu and document-dirty state. */
typedef struct AstraUndoState {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** Monotonic manager state generation. */
    uint32_t generation;
    /** Combination of ASTRA_UNDO_STATE_* flags. */
    uint32_t flags;
    /** Bytes occupied by undo and redo history. */
    uint32_t history_bytes;
    /** Byte boundary separating applied history from redo history. */
    uint32_t applied_bytes;
    /** Total retained transaction groups. */
    uint32_t group_count;
    /** Number of groups before the current history cursor. */
    uint32_t applied_group_count;
    /** Arena-backed UTF-8 name of the next undo group, or null. */
    const char *undo_name;
    /** Counted bytes at @ref undo_name. */
    uint32_t undo_name_length;
    /** Arena-backed UTF-8 name of the next redo group, or null. */
    const char *redo_name;
    /** Counted bytes at @ref redo_name. */
    uint32_t redo_name_length;
    /** Must be zero on input and output. */
    uint32_t reserved[4];
} AstraUndoState;

/** Empty initializer for ::AstraUndoState. */
#define ASTRA_UNDO_STATE_INIT \
    { sizeof(AstraUndoState), 0u, 0u, 0u, 0u, 0u, 0u, 0, 0u, 0, 0u, \
      { 0u, 0u, 0u, 0u } }

/**
 * Initialize an empty, clean history over caller-owned storage.
 * @param manager Empty caller-owned manager initialized with
 *        ::ASTRA_UNDO_MANAGER_INIT.
 * @param info Arena, coalescing, and callback configuration.
 * @return ::ASTRA_OK, or a negative ::AstraResult validation error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_init(
    AstraUndoManager *manager, const AstraUndoManagerInfo *info);

/**
 * Validate a complete group and return its exact arena charge.
 * @param group Complete transaction description.
 * @param bytes Receives the number of arena bytes the group requires.
 * @return ::ASTRA_OK, or a negative ::AstraResult validation or size error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_group_size(
    const AstraUndoGroupInfo *group, uint32_t *bytes);

/**
 * Record a group whose forward mutation has already completed.
 *
 * Preflight with ::astra_undo_group_size or retain enough arena space: unlike
 * ::astra_undo_perform_group, failure cannot roll back a mutation the caller
 * performed before entering this function.
 * @param manager Initialized document history.
 * @param group Transaction to copy into retained history.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL when the arena must grow,
 *         or another negative ::AstraResult.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_record_group(
    AstraUndoManager *manager, const AstraUndoGroupInfo *group);

/**
 * Apply a group's forward actions and record it as one transaction.
 * Group descriptors and payloads must remain unchanged until this synchronous
 * call returns.
 * @param manager Initialized document history.
 * @param group Transaction to apply and retain.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_BUFFER_TOO_SMALL without applying any
 *         action, or the callback's error after successful compensation.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_perform_group(
    AstraUndoManager *manager, const AstraUndoGroupInfo *group);

/**
 * Apply the previous group's inverse actions in reverse order.
 * @param manager Initialized document history.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_NOT_PRESENT at the oldest state, or a
 *         negative callback/history error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_undo(AstraUndoManager *manager);

/**
 * Reapply the next group's forward actions in original order.
 * @param manager Initialized document history.
 * @return ::ASTRA_OK, ::ASTRA_ERROR_NOT_PRESENT at the newest state, or a
 *         negative callback/history error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_redo(AstraUndoManager *manager);

/**
 * Forget all history, wipe its bytes, and recover a poisoned manager.
 * @param manager Initialized document history.
 * @return ::ASTRA_OK or a negative ::AstraResult validation/state error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_clear(AstraUndoManager *manager);

/**
 * Mark the current cursor as the document's saved state.
 * @param manager Initialized document history.
 * @return ::ASTRA_OK or a negative ::AstraResult validation/state error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_mark_clean(AstraUndoManager *manager);

/**
 * Read menu names, history usage, availability, and dirty state.
 * @param manager Initialized document history.
 * @param state Caller-owned snapshot initialized with ::ASTRA_UNDO_STATE_INIT.
 * @return ::ASTRA_OK or a negative ::AstraResult validation/history error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_get_state(
    const AstraUndoManager *manager, AstraUndoState *state);

/**
 * Move retained history into a larger non-overlapping caller-owned arena.
 * The old occupied bytes are wiped after the copy.
 * @param manager Initialized document history.
 * @param arena Four-byte-aligned replacement storage.
 * @param arena_bytes Replacement capacity, at least the retained byte count.
 * @return ::ASTRA_OK or a negative ::AstraResult validation/state error.
 * @since Interface Kit 2.5.0.
 */
ASTRA_NODISCARD AstraResult astra_undo_move_arena(
    AstraUndoManager *manager, void *arena, uint32_t arena_bytes);

ASTRA_EXTERN_C_END

#endif
