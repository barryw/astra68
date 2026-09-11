#include <astra/undo.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Change {
    int32_t before;
    int32_t after;
} Change;

typedef struct Model {
    AstraUndoManager *manager;
    int32_t value;
    uint32_t calls;
    uint32_t fail_operation;
    uint32_t fail_direction;
    uint32_t fail_second_operation;
    uint32_t fail_second_direction;
    uint32_t reenter;
    AstraResult reenter_result;
} Model;

static AstraUndoAction action(uint32_t operation, const Change *change)
{
    AstraUndoAction value = ASTRA_UNDO_ACTION_INIT;

    value.operation = operation;
    value.payload = change;
    value.payload_bytes = sizeof(*change);
    return value;
}

static AstraUndoGroupInfo group(const char *name,
                                const AstraUndoAction *actions,
                                uint32_t action_count)
{
    AstraUndoGroupInfo value = ASTRA_UNDO_GROUP_INFO_INIT;

    value.name = name;
    value.name_length = name == NULL ? 0u : (uint32_t)strlen(name);
    value.actions = actions;
    value.action_count = action_count;
    return value;
}

static AstraResult apply(void *context, uint32_t operation,
                         uint32_t direction, const void *payload,
                         uint32_t payload_bytes)
{
    Model *model = context;
    const Change *change = payload;

    assert(payload_bytes == sizeof(*change));
    ++model->calls;
    if ((operation == model->fail_operation &&
         direction == model->fail_direction) ||
        (operation == model->fail_second_operation &&
         direction == model->fail_second_direction))
        return ASTRA_ERROR_IO;
    if (model->reenter != 0u) {
        AstraUndoAction nested_action = action(operation, change);
        AstraUndoGroupInfo nested = group("Nested", &nested_action, 1u);

        model->reenter_result = astra_undo_record_group(model->manager,
                                                        &nested);
    }
    model->value = direction == ASTRA_UNDO_DIRECTION_UNDO ?
                   change->before : change->after;
    return ASTRA_OK;
}

static void init(AstraUndoManager *manager, Model *model, void *arena,
                 uint32_t arena_bytes, uint64_t coalesce_interval_ns)
{
    AstraUndoManagerInfo info = ASTRA_UNDO_MANAGER_INFO_INIT;

    info.arena = arena;
    info.arena_bytes = arena_bytes;
    info.coalesce_interval_ns = coalesce_interval_ns;
    info.apply = apply;
    info.context = model;
    assert(astra_undo_init(manager, &info) == ASTRA_OK);
    model->manager = manager;
}

static AstraUndoState state(const AstraUndoManager *manager)
{
    AstraUndoState value = ASTRA_UNDO_STATE_INIT;

    assert(astra_undo_get_state(manager, &value) == ASTRA_OK);
    return value;
}

static void test_perform_undo_redo_and_names(void)
{
    _Alignas(8) uint8_t arena[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change change = {0, 42};
    AstraUndoAction actions[] = {action(1u, &change)};
    AstraUndoGroupInfo info = group("Set Value", actions, 1u);
    AstraUndoState observed;

    init(&manager, &model, arena, sizeof(arena), 0u);
    observed = state(&manager);
    assert(observed.flags == ASTRA_UNDO_STATE_CLEAN);
    assert(observed.history_bytes == 0u);
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    assert(model.value == 42);
    observed = state(&manager);
    assert(observed.flags == ASTRA_UNDO_STATE_CAN_UNDO);
    assert(observed.undo_name_length == 9u);
    assert(memcmp(observed.undo_name, "Set Value", 9u) == 0);
    assert(observed.applied_group_count == 1u);
    assert(observed.group_count == 1u);
    assert(astra_undo_undo(&manager) == ASTRA_OK);
    assert(model.value == 0);
    observed = state(&manager);
    assert(observed.flags == (ASTRA_UNDO_STATE_CAN_REDO |
                              ASTRA_UNDO_STATE_CLEAN));
    assert(observed.redo_name_length == 9u);
    assert(memcmp(observed.redo_name, "Set Value", 9u) == 0);
    assert(astra_undo_redo(&manager) == ASTRA_OK);
    assert(model.value == 42);
    assert(model.calls == 3u);
}

static void test_group_order_and_failure_compensation(void)
{
    _Alignas(8) uint8_t arena[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change changes[] = {{0, 1}, {1, 2}, {2, 3}};
    AstraUndoAction actions[] = {
        action(1u, &changes[0]), action(2u, &changes[1]),
        action(3u, &changes[2])};
    AstraUndoGroupInfo info = group("Three Steps", actions, 3u);

    init(&manager, &model, arena, sizeof(arena), 0u);
    model.fail_operation = 3u;
    model.fail_direction = ASTRA_UNDO_DIRECTION_REDO;
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_ERROR_IO);
    assert(model.value == 0);
    assert(state(&manager).group_count == 0u);
    model.fail_operation = 0u;
    model.fail_direction = 0u;
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    assert(model.value == 3);
    model.fail_operation = 2u;
    model.fail_direction = ASTRA_UNDO_DIRECTION_UNDO;
    assert(astra_undo_undo(&manager) == ASTRA_ERROR_IO);
    assert(model.value == 3);
    assert((state(&manager).flags & ASTRA_UNDO_STATE_CAN_UNDO) != 0u);
    model.fail_operation = 0u;
    model.fail_direction = 0u;
    assert(astra_undo_undo(&manager) == ASTRA_OK);
    assert(model.value == 0);
    assert(astra_undo_redo(&manager) == ASTRA_OK);
    assert(model.value == 3);
}

static void test_compensation_failure_poisoning(void)
{
    _Alignas(8) uint8_t arena[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change changes[] = {{0, 1}, {1, 2}, {2, 3}};
    AstraUndoAction actions[] = {
        action(1u, &changes[0]), action(2u, &changes[1]),
        action(3u, &changes[2])};
    AstraUndoGroupInfo info = group("Poison", actions, 3u);

    init(&manager, &model, arena, sizeof(arena), 0u);
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    model.fail_operation = 2u;
    model.fail_direction = ASTRA_UNDO_DIRECTION_UNDO;
    model.fail_second_operation = 3u;
    model.fail_second_direction = ASTRA_UNDO_DIRECTION_REDO;
    assert(astra_undo_undo(&manager) == ASTRA_ERROR_IO);
    assert((state(&manager).flags & ASTRA_UNDO_STATE_POISONED) != 0u);
    assert(astra_undo_undo(&manager) == ASTRA_ERROR_IO);
    assert(astra_undo_redo(&manager) == ASTRA_ERROR_IO);
    assert(astra_undo_clear(&manager) == ASTRA_OK);
    assert(state(&manager).flags == ASTRA_UNDO_STATE_CLEAN);
}

static void test_coalescing_pause_and_save_boundary(void)
{
    _Alignas(8) uint8_t arena[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change changes[] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}};
    AstraUndoAction actions[4];
    AstraUndoGroupInfo info;

    init(&manager, &model, arena, sizeof(arena), 100u);
    for (uint32_t index = 0u; index < 4u; ++index)
        actions[index] = action(index + 1u, &changes[index]);
    info = group("Typing", &actions[0], 1u);
    info.coalesce_id = 7u;
    info.timestamp_ns = 100u;
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    info.actions = &actions[1];
    info.timestamp_ns = 175u;
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    assert(state(&manager).group_count == 1u);
    assert(astra_undo_undo(&manager) == ASTRA_OK);
    assert(model.value == 0);
    assert(astra_undo_redo(&manager) == ASTRA_OK);
    assert(model.value == 2);

    assert(astra_undo_mark_clean(&manager) == ASTRA_OK);
    info.actions = &actions[2];
    info.timestamp_ns = 200u;
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    assert(state(&manager).group_count == 2u);
    assert(astra_undo_undo(&manager) == ASTRA_OK);
    assert(model.value == 2);
    assert((state(&manager).flags & ASTRA_UNDO_STATE_CLEAN) != 0u);

    info.actions = &actions[3];
    info.timestamp_ns = 400u;
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    assert(model.value == 4);
    assert(state(&manager).group_count == 2u);
    assert((state(&manager).flags & ASTRA_UNDO_STATE_CAN_REDO) == 0u);
}

static void test_capacity_growth_and_atomic_failure(void)
{
    _Alignas(8) uint8_t small[32];
    _Alignas(8) uint8_t large[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change change = {0, 1};
    AstraUndoAction value = action(1u, &change);
    AstraUndoGroupInfo info = group("Grow", &value, 1u);
    uint32_t required = 0u;

    assert(astra_undo_group_size(&info, &required) == ASTRA_OK);
    assert(required > sizeof(small));
    init(&manager, &model, small, sizeof(small), 0u);
    assert(astra_undo_perform_group(&manager, &info) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(model.value == 0);
    assert(model.calls == 0u);
    assert(astra_undo_move_arena(&manager, large, sizeof(large)) == ASTRA_OK);
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    assert(astra_undo_undo(&manager) == ASTRA_OK);
    assert(model.value == 0);
}

static void test_reentrancy_is_rejected(void)
{
    _Alignas(8) uint8_t arena[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change change = {0, 1};
    AstraUndoAction value = action(1u, &change);
    AstraUndoGroupInfo info = group("Outer", &value, 1u);

    init(&manager, &model, arena, sizeof(arena), 0u);
    model.reenter = 1u;
    assert(astra_undo_perform_group(&manager, &info) == ASTRA_OK);
    assert(model.reenter_result == ASTRA_ERROR_BUSY);
}

static void test_no_action_count_ceiling(void)
{
    const uint32_t count = 10000u;
    uint8_t *arena = malloc(1024u * 1024u);
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};

    assert(arena != NULL);
    init(&manager, &model, arena, 1024u * 1024u, 0u);
    for (uint32_t index = 0u; index < count; ++index) {
        Change change = {(int32_t)index, (int32_t)index + 1};
        AstraUndoAction value = action(index + 1u, &change);
        AstraUndoGroupInfo info = group(NULL, &value, 1u);

        model.value = change.after;
        assert(astra_undo_record_group(&manager, &info) == ASTRA_OK);
    }
    assert(state(&manager).group_count == count);
    assert(state(&manager).undo_name == NULL);
    for (uint32_t index = 0u; index < count; ++index)
        assert(astra_undo_undo(&manager) == ASTRA_OK);
    assert(model.value == 0);
    free(arena);
}

static void test_discarded_history_is_wiped(void)
{
    _Alignas(8) uint8_t arena[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change changes[] = {{0, 1}, {1, 2}, {1, 3}};
    AstraUndoAction actions[] = {
        action(1u, &changes[0]), action(2u, &changes[1]),
        action(3u, &changes[2])};
    AstraUndoGroupInfo first = group("First", &actions[0], 1u);
    AstraUndoGroupInfo long_redo = group(
        "A deliberately longer discarded redo transaction", &actions[1], 1u);
    AstraUndoGroupInfo replacement = group("New", &actions[2], 1u);
    uint32_t old_used;
    uint32_t new_used;

    memset(arena, 0xa5, sizeof(arena));
    init(&manager, &model, arena, sizeof(arena), 0u);
    assert(astra_undo_perform_group(&manager, &first) == ASTRA_OK);
    assert(astra_undo_perform_group(&manager, &long_redo) == ASTRA_OK);
    old_used = state(&manager).history_bytes;
    assert(astra_undo_undo(&manager) == ASTRA_OK);
    assert(astra_undo_perform_group(&manager, &replacement) == ASTRA_OK);
    new_used = state(&manager).history_bytes;
    assert(new_used < old_used);
    for (uint32_t index = new_used; index < old_used; ++index)
        assert(arena[index] == 0u);
    assert(astra_undo_clear(&manager) == ASTRA_OK);
    for (uint32_t index = 0u; index < new_used; ++index)
        assert(arena[index] == 0u);
}

static void test_corruption_and_arena_resize_are_safe(void)
{
    _Alignas(8) uint8_t arena[4096] = {0};
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};

    init(&manager, &model, arena, sizeof(arena), 0u);
    assert(astra_undo_move_arena(&manager, arena, sizeof(arena) + 4u) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    manager._private_used = sizeof(uint32_t);
    assert(astra_undo_redo(&manager) == ASTRA_ERROR_IO);
    assert(manager._private_poisoned != 0u);
    assert(astra_undo_clear(&manager) == ASTRA_OK);
    assert(state(&manager).flags == ASTRA_UNDO_STATE_CLEAN);
}

static void test_invalid_inputs(void)
{
    _Alignas(8) uint8_t arena[4096];
    AstraUndoManager manager = ASTRA_UNDO_MANAGER_INIT;
    Model model = {0};
    Change change = {0, 1};
    AstraUndoAction value = action(1u, &change);
    AstraUndoGroupInfo info = group("\xc0\x80", &value, 1u);
    uint32_t bytes = 0u;

    assert(astra_undo_group_size(&info, &bytes) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    info = group("Valid", &value, 1u);
    value.reserved[0] = 1u;
    assert(astra_undo_group_size(&info, &bytes) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    value.reserved[0] = 0u;
    init(&manager, &model, arena, sizeof(arena), 0u);
    assert(astra_undo_undo(&manager) == ASTRA_ERROR_NOT_PRESENT);
    assert(astra_undo_redo(&manager) == ASTRA_ERROR_NOT_PRESENT);
}

int main(void)
{
    test_perform_undo_redo_and_names();
    test_group_order_and_failure_compensation();
    test_compensation_failure_poisoning();
    test_coalescing_pause_and_save_boundary();
    test_capacity_growth_and_atomic_failure();
    test_reentrancy_is_rejected();
    test_no_action_count_ceiling();
    test_discarded_history_is_wiped();
    test_corruption_and_arena_resize_are_safe();
    test_invalid_inputs();
    puts("undo manager tests passed");
    return 0;
}
