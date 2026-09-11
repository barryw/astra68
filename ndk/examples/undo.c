#include <astra/interface_kit.h>

enum { EXAMPLE_SET_ZOOM = 1u };

typedef struct ExampleZoomChange {
    uint32_t before;
    uint32_t after;
} ExampleZoomChange;

typedef struct ExampleDocument {
    AstraUndoManager undo;
    uint32_t zoom_percent;
} ExampleDocument;

static AstraResult apply_document_change(
    void *context, uint32_t operation, uint32_t direction,
    const void *payload, uint32_t payload_bytes)
{
    ExampleDocument *document = context;
    const ExampleZoomChange *change = payload;

    if (document == 0 || operation != EXAMPLE_SET_ZOOM || change == 0 ||
        payload_bytes != sizeof(*change) ||
        (direction != ASTRA_UNDO_DIRECTION_UNDO &&
         direction != ASTRA_UNDO_DIRECTION_REDO))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    document->zoom_percent = direction == ASTRA_UNDO_DIRECTION_UNDO ?
        change->before : change->after;
    return ASTRA_OK;
}

/* Initialize one document with an application-chosen history allocation. */
AstraResult astra_example_document_init(
    const AstraInterfaceLibraryV2 *interface, ExampleDocument *document,
    void *arena, uint32_t arena_bytes)
{
    AstraUndoManagerInfo info = ASTRA_UNDO_MANAGER_INFO_INIT;

    if (document == 0 || !astra_interface_library_supports(
            interface, 5u, ASTRA_INTERFACE_LIBRARY_2_5_SIZE))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    document->undo = (AstraUndoManager)ASTRA_UNDO_MANAGER_INIT;
    document->zoom_percent = 100u;
    info.arena = arena;
    info.arena_bytes = arena_bytes;
    info.coalesce_interval_ns = UINT64_C(500000000);
    info.apply = apply_document_change;
    info.context = document;
    return interface->undo_init(&document->undo, &info);
}

/* Apply and retain one named user transaction atomically. */
AstraResult astra_example_set_zoom(
    const AstraInterfaceLibraryV2 *interface, ExampleDocument *document,
    uint32_t percent, uint64_t timestamp_ns)
{
    ExampleZoomChange change;
    AstraUndoAction action = ASTRA_UNDO_ACTION_INIT;
    AstraUndoGroupInfo group = ASTRA_UNDO_GROUP_INFO_INIT;

    if (interface == 0 || document == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    change.before = document->zoom_percent;
    change.after = percent;
    action.operation = EXAMPLE_SET_ZOOM;
    action.payload = &change;
    action.payload_bytes = sizeof(change);
    group.name = "Zoom";
    group.name_length = sizeof("Zoom") - 1u;
    group.actions = &action;
    group.action_count = 1u;
    group.coalesce_id = EXAMPLE_SET_ZOOM;
    group.timestamp_ns = timestamp_ns;
    return interface->undo_perform_group(&document->undo, &group);
}
