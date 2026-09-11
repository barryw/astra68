#include <astra/undo.h>

#include <astra/bytes.h>
#include <astra/utf8.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UNDO_GROUP_MAGIC UINT32_C(0x554e444f)
#define UNDO_ALIGNMENT 4u
#define UNDO_MODE_IDLE 0u
#define UNDO_MODE_APPLYING 1u
#define UNDO_CLEAN_INVALID UINT32_MAX

typedef struct UndoGroupHeader {
    uint32_t magic;
    uint32_t total_bytes;
    uint32_t action_count;
    uint32_t name_length;
    uint32_t coalesce_id;
    uint32_t reserved;
    uint32_t timestamp_low;
    uint32_t timestamp_high;
} UndoGroupHeader;

typedef struct UndoActionHeader {
    uint32_t total_bytes;
    uint32_t operation;
    uint32_t payload_bytes;
    uint32_t reserved;
} UndoActionHeader;

typedef struct UndoPlan {
    uint32_t group_bytes;
    uint32_t action_bytes;
    uint32_t coalesce_start;
    uint32_t coalesce;
} UndoPlan;

static uint32_t next_generation(uint32_t generation)
{
    ++generation;
    return generation == 0u ? 1u : generation;
}

static uint64_t group_timestamp(const UndoGroupHeader *group)
{
    return (uint64_t)group->timestamp_high << 32u | group->timestamp_low;
}

static void set_group_timestamp(UndoGroupHeader *group, uint64_t timestamp)
{
    group->timestamp_low = (uint32_t)timestamp;
    group->timestamp_high = (uint32_t)(timestamp >> 32u);
}

static int add_aligned(uint64_t *total, uint32_t bytes)
{
    uint64_t aligned = ((uint64_t)bytes + UNDO_ALIGNMENT - 1u) &
                       ~(uint64_t)(UNDO_ALIGNMENT - 1u);

    if (aligned > UINT32_MAX || *total > UINT32_MAX - aligned)
        return 0;
    *total += aligned;
    return 1;
}

static int manager_valid(const AstraUndoManager *manager)
{
    return manager != NULL &&
           manager->_private_structure_size == sizeof(*manager) &&
           manager->_private_arena != NULL &&
           ((uintptr_t)manager->_private_arena &
            (UNDO_ALIGNMENT - 1u)) == 0u &&
           manager->_private_capacity != 0u &&
           manager->_private_used <= manager->_private_capacity &&
           manager->_private_cursor <= manager->_private_used &&
           (manager->_private_clean_cursor == UNDO_CLEAN_INVALID ||
            manager->_private_clean_cursor <= manager->_private_used) &&
           manager->_private_applied_group_count <=
               manager->_private_group_count &&
           manager->_private_mode <= UNDO_MODE_APPLYING &&
           manager->_private_poisoned <= 1u &&
           manager->_private_apply != NULL &&
           astra_words_zero(manager->_private_reserved, 4u);
}

static int manager_empty(const AstraUndoManager *manager)
{
    return manager != NULL &&
           manager->_private_structure_size == sizeof(*manager) &&
           manager->_private_arena == NULL &&
           manager->_private_capacity == 0u &&
           manager->_private_used == 0u &&
           manager->_private_cursor == 0u &&
           manager->_private_clean_cursor == 0u &&
           manager->_private_generation == 0u &&
           manager->_private_group_count == 0u &&
           manager->_private_applied_group_count == 0u &&
           manager->_private_mode == 0u && manager->_private_poisoned == 0u &&
           manager->_private_coalesce_interval_ns == 0u &&
           manager->_private_apply == NULL &&
           manager->_private_context == NULL &&
           astra_words_zero(manager->_private_reserved, 4u);
}

static int overlaps_arena(const AstraUndoManager *manager, const void *data,
                          uint32_t bytes)
{
    uintptr_t start;
    uintptr_t end;
    uintptr_t arena;
    uintptr_t arena_end;

    if (bytes == 0u)
        return 0;
    start = (uintptr_t)data;
    if (start > UINTPTR_MAX - bytes)
        return 1;
    end = start + bytes;
    arena = (uintptr_t)manager->_private_arena;
    if (arena > UINTPTR_MAX - manager->_private_capacity)
        return 1;
    arena_end = arena + manager->_private_capacity;
    return start < arena_end && end > arena;
}

static AstraResult group_measure(const AstraUndoGroupInfo *group,
                                 const AstraUndoManager *manager,
                                 uint32_t *group_bytes,
                                 uint32_t *action_bytes)
{
    uint64_t total = sizeof(UndoGroupHeader);
    uint64_t actions_total = 0u;
    uint64_t action_table_bytes;

    if (group == NULL || group->size < sizeof(*group) ||
        group->action_count == 0u || group->actions == NULL ||
        (group->name == NULL && group->name_length != 0u) ||
        (group->name_length != 0u &&
         !astra_utf8_validate(group->name, group->name_length, 0u)) ||
        !astra_words_zero(group->reserved, 4u) || group_bytes == NULL ||
        action_bytes == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    action_table_bytes = (uint64_t)group->action_count *
                         sizeof(AstraUndoAction);
    if (action_table_bytes > UINT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    if (manager != NULL &&
        (overlaps_arena(manager, group, sizeof(*group)) ||
         overlaps_arena(manager, group->name, group->name_length) ||
         overlaps_arena(manager, group->actions,
                        (uint32_t)action_table_bytes)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (!add_aligned(&total, group->name_length))
        return ASTRA_ERROR_NO_RESOURCES;
    for (uint32_t index = 0u; index < group->action_count; ++index) {
        const AstraUndoAction *action = &group->actions[index];
        uint64_t one = sizeof(UndoActionHeader);

        if (action->size < sizeof(*action) || action->operation == 0u ||
            (action->payload == NULL && action->payload_bytes != 0u) ||
            !astra_words_zero(action->reserved, 4u) ||
            (manager != NULL && overlaps_arena(
                manager, action->payload, action->payload_bytes)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        if (!add_aligned(&one, action->payload_bytes) ||
            one > UINT32_MAX - sizeof(uint32_t))
            return ASTRA_ERROR_NO_RESOURCES;
        one += sizeof(uint32_t);
        if (actions_total > UINT32_MAX - one || total > UINT32_MAX - one)
            return ASTRA_ERROR_NO_RESOURCES;
        actions_total += one;
        total += one;
    }
    if (total > UINT32_MAX - sizeof(uint32_t))
        return ASTRA_ERROR_NO_RESOURCES;
    total += sizeof(uint32_t);
    *group_bytes = (uint32_t)total;
    *action_bytes = (uint32_t)actions_total;
    return ASTRA_OK;
}

AstraResult astra_undo_group_size(const AstraUndoGroupInfo *group,
                                  uint32_t *bytes)
{
    uint32_t action_bytes;

    return group_measure(group, NULL, bytes, &action_bytes);
}

AstraResult astra_undo_init(AstraUndoManager *manager,
                            const AstraUndoManagerInfo *info)
{
    if (!manager_empty(manager) || info == NULL ||
        info->size < sizeof(*info) || info->arena == NULL ||
        info->arena_bytes == 0u ||
        ((uintptr_t)info->arena & (UNDO_ALIGNMENT - 1u)) != 0u ||
        info->apply == NULL || !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    manager->_private_arena = info->arena;
    manager->_private_capacity = info->arena_bytes;
    manager->_private_generation = 1u;
    manager->_private_coalesce_interval_ns = info->coalesce_interval_ns;
    manager->_private_apply = info->apply;
    manager->_private_context = info->context;
    return ASTRA_OK;
}

static const UndoGroupHeader *group_at(const AstraUndoManager *manager,
                                       uint32_t start, uint32_t expected_end)
{
    const uint8_t *arena = manager->_private_arena;
    const UndoGroupHeader *group;
    uint32_t cursor;

    if (start > expected_end || expected_end > manager->_private_used ||
        expected_end - start < sizeof(UndoGroupHeader) + sizeof(uint32_t))
        return NULL;
    group = (const UndoGroupHeader *)(const void *)(arena + start);
    if (group->magic != UNDO_GROUP_MAGIC || group->reserved != 0u ||
        group->action_count == 0u || group->total_bytes != expected_end - start ||
        group->name_length > group->total_bytes - sizeof(*group) ||
        group->name_length > UINT32_MAX - (UNDO_ALIGNMENT - 1u) ||
        !astra_utf8_validate(arena + start + sizeof(*group),
                             group->name_length, 0u))
        return NULL;
    cursor = start + sizeof(*group) +
             ((group->name_length + UNDO_ALIGNMENT - 1u) &
              ~(UNDO_ALIGNMENT - 1u));
    for (uint32_t index = 0u; index < group->action_count; ++index) {
        const UndoActionHeader *action;
        uint64_t minimum;
        uint32_t recorded;

        if (cursor > expected_end ||
            expected_end - cursor < sizeof(UndoActionHeader) +
                                    sizeof(uint32_t))
            return NULL;
        action = (const UndoActionHeader *)(const void *)(arena + cursor);
        minimum = sizeof(*action) +
                  (((uint64_t)action->payload_bytes + UNDO_ALIGNMENT - 1u) &
                   ~(uint64_t)(UNDO_ALIGNMENT - 1u)) + sizeof(uint32_t);
        if (action->operation == 0u || action->reserved != 0u ||
            minimum > UINT32_MAX || action->total_bytes != minimum ||
            action->total_bytes > expected_end - cursor)
            return NULL;
        memcpy(&recorded, arena + cursor + action->total_bytes -
                              sizeof(recorded), sizeof(recorded));
        if (recorded != action->total_bytes)
            return NULL;
        cursor += action->total_bytes;
    }
    {
        uint32_t recorded;

        if (cursor != expected_end - sizeof(recorded))
            return NULL;
        memcpy(&recorded, arena + cursor, sizeof(recorded));
        if (recorded != group->total_bytes)
            return NULL;
    }
    return group;
}

static const UndoGroupHeader *previous_group(
    const AstraUndoManager *manager, uint32_t end, uint32_t *start)
{
    const uint8_t *arena = manager->_private_arena;
    uint32_t bytes;

    if (end < sizeof(bytes) || end > manager->_private_used)
        return NULL;
    memcpy(&bytes, arena + end - sizeof(bytes), sizeof(bytes));
    if (bytes > end)
        return NULL;
    *start = end - bytes;
    return group_at(manager, *start, end);
}

static uint32_t group_actions_start(uint32_t start,
                                    const UndoGroupHeader *group)
{
    return start + sizeof(*group) +
           ((group->name_length + UNDO_ALIGNMENT - 1u) &
            ~(UNDO_ALIGNMENT - 1u));
}

static int group_name_equal(const AstraUndoManager *manager, uint32_t start,
                            const UndoGroupHeader *left,
                            const AstraUndoGroupInfo *right)
{
    return left->name_length == right->name_length &&
           (left->name_length == 0u ||
            memcmp((const uint8_t *)manager->_private_arena + start +
                       sizeof(*left), right->name,
                   left->name_length) == 0);
}

static AstraResult make_plan(const AstraUndoManager *manager,
                             const AstraUndoGroupInfo *group,
                             UndoPlan *plan)
{
    const UndoGroupHeader *previous = NULL;
    uint32_t previous_start = 0u;
    AstraResult result;

    result = group_measure(group, manager, &plan->group_bytes,
                           &plan->action_bytes);
    if (result != ASTRA_OK)
        return result;
    plan->coalesce = 0u;
    plan->coalesce_start = 0u;
    if (group->coalesce_id != 0u &&
        manager->_private_coalesce_interval_ns != 0u &&
        manager->_private_cursor == manager->_private_used &&
        manager->_private_cursor != 0u &&
        manager->_private_clean_cursor != manager->_private_cursor) {
        previous = previous_group(manager, manager->_private_cursor,
                                  &previous_start);
        if (previous == NULL)
            return ASTRA_ERROR_IO;
        if (previous->coalesce_id == group->coalesce_id &&
            group->timestamp_ns >= group_timestamp(previous) &&
            group->timestamp_ns - group_timestamp(previous) <=
                manager->_private_coalesce_interval_ns &&
            group_name_equal(manager, previous_start, previous, group)) {
            plan->coalesce = 1u;
            plan->coalesce_start = previous_start;
        }
    }
    if ((plan->coalesce != 0u &&
         plan->action_bytes > manager->_private_capacity -
                                  manager->_private_cursor) ||
        (plan->coalesce == 0u &&
         plan->group_bytes > manager->_private_capacity -
                                 manager->_private_cursor))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    return ASTRA_OK;
}

static uint32_t write_actions(uint8_t *destination,
                              const AstraUndoGroupInfo *group)
{
    uint32_t cursor = 0u;

    for (uint32_t index = 0u; index < group->action_count; ++index) {
        const AstraUndoAction *input = &group->actions[index];
        UndoActionHeader action = {0};
        uint32_t payload_aligned =
            (input->payload_bytes + UNDO_ALIGNMENT - 1u) &
            ~(UNDO_ALIGNMENT - 1u);

        action.total_bytes = sizeof(action) + payload_aligned +
                             sizeof(uint32_t);
        action.operation = input->operation;
        action.payload_bytes = input->payload_bytes;
        memcpy(destination + cursor, &action, sizeof(action));
        if (input->payload_bytes != 0u)
            memcpy(destination + cursor + sizeof(action), input->payload,
                   input->payload_bytes);
        if (payload_aligned != input->payload_bytes)
            memset(destination + cursor + sizeof(action) +
                       input->payload_bytes, 0,
                   payload_aligned - input->payload_bytes);
        memcpy(destination + cursor + action.total_bytes - sizeof(uint32_t),
               &action.total_bytes, sizeof(uint32_t));
        cursor += action.total_bytes;
    }
    return cursor;
}

static void commit_group(AstraUndoManager *manager,
                         const AstraUndoGroupInfo *input,
                         const UndoPlan *plan)
{
    uint8_t *arena = manager->_private_arena;

    if (plan->coalesce != 0u) {
        UndoGroupHeader *group = (UndoGroupHeader *)(void *)(
            arena + plan->coalesce_start);
        uint32_t at = manager->_private_cursor - sizeof(uint32_t);

        at += write_actions(arena + at, input);
        group->total_bytes += plan->action_bytes;
        group->action_count += input->action_count;
        set_group_timestamp(group, input->timestamp_ns);
        memcpy(arena + at, &group->total_bytes, sizeof(group->total_bytes));
        manager->_private_cursor += plan->action_bytes;
        manager->_private_used = manager->_private_cursor;
    } else {
        UndoGroupHeader group = {0};
        uint32_t start = manager->_private_cursor;
        uint32_t name_aligned =
            (input->name_length + UNDO_ALIGNMENT - 1u) &
            ~(UNDO_ALIGNMENT - 1u);
        uint32_t at;

        if (manager->_private_cursor != manager->_private_used)
            memset(arena + manager->_private_cursor, 0,
                   manager->_private_used - manager->_private_cursor);
        group.magic = UNDO_GROUP_MAGIC;
        group.total_bytes = plan->group_bytes;
        group.action_count = input->action_count;
        group.name_length = input->name_length;
        group.coalesce_id = input->coalesce_id;
        set_group_timestamp(&group, input->timestamp_ns);
        memcpy(arena + start, &group, sizeof(group));
        if (input->name_length != 0u)
            memcpy(arena + start + sizeof(group), input->name,
                   input->name_length);
        if (name_aligned != input->name_length)
            memset(arena + start + sizeof(group) + input->name_length, 0,
                   name_aligned - input->name_length);
        at = start + sizeof(group) + name_aligned;
        at += write_actions(arena + at, input);
        memcpy(arena + at, &group.total_bytes, sizeof(group.total_bytes));
        manager->_private_cursor = start + group.total_bytes;
        manager->_private_used = manager->_private_cursor;
        manager->_private_group_count =
            manager->_private_applied_group_count + 1u;
        ++manager->_private_applied_group_count;
    }
    manager->_private_generation = next_generation(
        manager->_private_generation);
}

AstraResult astra_undo_record_group(AstraUndoManager *manager,
                                    const AstraUndoGroupInfo *group)
{
    UndoPlan plan;
    AstraResult result;

    if (!manager_valid(manager))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (manager->_private_mode != UNDO_MODE_IDLE)
        return ASTRA_ERROR_BUSY;
    if (manager->_private_poisoned != 0u)
        return ASTRA_ERROR_IO;
    result = make_plan(manager, group, &plan);
    if (result != ASTRA_OK)
        return result;
    if (manager->_private_clean_cursor > manager->_private_cursor &&
        manager->_private_clean_cursor != UNDO_CLEAN_INVALID)
        manager->_private_clean_cursor = UNDO_CLEAN_INVALID;
    commit_group(manager, group, &plan);
    return ASTRA_OK;
}

static const UndoActionHeader *forward_action(const uint8_t *arena,
                                              uint32_t *cursor)
{
    const UndoActionHeader *action =
        (const UndoActionHeader *)(const void *)(arena + *cursor);

    *cursor += action->total_bytes;
    return action;
}

static const UndoActionHeader *reverse_action(const uint8_t *arena,
                                              uint32_t *cursor)
{
    uint32_t bytes;

    memcpy(&bytes, arena + *cursor - sizeof(bytes), sizeof(bytes));
    *cursor -= bytes;
    return (const UndoActionHeader *)(const void *)(arena + *cursor);
}

static AstraResult dispatch(const AstraUndoManager *manager,
                            const UndoActionHeader *action,
                            uint32_t direction)
{
    return manager->_private_apply(
        manager->_private_context, action->operation, direction,
        (const uint8_t *)(const void *)action + sizeof(*action),
        action->payload_bytes);
}

static AstraResult compensate_redo(const AstraUndoManager *manager,
                                   const UndoGroupHeader *group,
                                   uint32_t start, uint32_t first)
{
    const uint8_t *arena = manager->_private_arena;
    uint32_t cursor = group_actions_start(start, group);

    for (uint32_t index = 0u; index < first; ++index)
        (void)forward_action(arena, &cursor);
    for (uint32_t index = first; index < group->action_count; ++index)
        if (dispatch(manager, forward_action(arena, &cursor),
                     ASTRA_UNDO_DIRECTION_REDO) != ASTRA_OK)
            return ASTRA_ERROR_IO;
    return ASTRA_OK;
}

static AstraResult compensate_undo(const AstraUndoManager *manager,
                                   const UndoGroupHeader *group,
                                   uint32_t start, uint32_t count)
{
    const uint8_t *arena = manager->_private_arena;
    uint32_t cursor = group_actions_start(start, group);

    for (uint32_t index = 0u; index < count; ++index)
        (void)forward_action(arena, &cursor);
    for (uint32_t index = 0u; index < count; ++index)
        if (dispatch(manager, reverse_action(arena, &cursor),
                     ASTRA_UNDO_DIRECTION_UNDO) != ASTRA_OK)
            return ASTRA_ERROR_IO;
    return ASTRA_OK;
}

static AstraResult apply_serialized(AstraUndoManager *manager,
                                    const UndoGroupHeader *group,
                                    uint32_t start, uint32_t direction)
{
    const uint8_t *arena = manager->_private_arena;
    uint32_t cursor;

    if (direction == ASTRA_UNDO_DIRECTION_REDO) {
        cursor = group_actions_start(start, group);
        for (uint32_t index = 0u; index < group->action_count; ++index) {
            const UndoActionHeader *action = forward_action(arena, &cursor);
            AstraResult result = dispatch(manager, action, direction);

            if (result != ASTRA_OK) {
                if (compensate_undo(manager, group, start, index) != ASTRA_OK)
                    manager->_private_poisoned = 1u;
                return manager->_private_poisoned != 0u ?
                       ASTRA_ERROR_IO : result;
            }
        }
    } else {
        cursor = start + group->total_bytes - sizeof(uint32_t);
        for (uint32_t index = group->action_count; index != 0u; --index) {
            const UndoActionHeader *action = reverse_action(arena, &cursor);
            AstraResult result = dispatch(manager, action, direction);

            if (result != ASTRA_OK) {
                if (compensate_redo(manager, group, start, index) != ASTRA_OK)
                    manager->_private_poisoned = 1u;
                return manager->_private_poisoned != 0u ?
                       ASTRA_ERROR_IO : result;
            }
        }
    }
    return ASTRA_OK;
}

static AstraResult apply_input(AstraUndoManager *manager,
                               const AstraUndoGroupInfo *group)
{
    for (uint32_t index = 0u; index < group->action_count; ++index) {
        const AstraUndoAction *action = &group->actions[index];
        AstraResult result = manager->_private_apply(
            manager->_private_context, action->operation,
            ASTRA_UNDO_DIRECTION_REDO, action->payload,
            action->payload_bytes);

        if (result != ASTRA_OK) {
            while (index != 0u) {
                --index;
                action = &group->actions[index];
                if (manager->_private_apply(
                        manager->_private_context, action->operation,
                        ASTRA_UNDO_DIRECTION_UNDO, action->payload,
                        action->payload_bytes) != ASTRA_OK) {
                    manager->_private_poisoned = 1u;
                    return ASTRA_ERROR_IO;
                }
            }
            return result;
        }
    }
    return ASTRA_OK;
}

AstraResult astra_undo_perform_group(AstraUndoManager *manager,
                                     const AstraUndoGroupInfo *group)
{
    UndoPlan plan;
    AstraResult result;

    if (!manager_valid(manager))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (manager->_private_mode != UNDO_MODE_IDLE)
        return ASTRA_ERROR_BUSY;
    if (manager->_private_poisoned != 0u)
        return ASTRA_ERROR_IO;
    result = make_plan(manager, group, &plan);
    if (result != ASTRA_OK)
        return result;
    manager->_private_mode = UNDO_MODE_APPLYING;
    result = apply_input(manager, group);
    manager->_private_mode = UNDO_MODE_IDLE;
    if (result != ASTRA_OK)
        return result;
    if (manager->_private_clean_cursor > manager->_private_cursor &&
        manager->_private_clean_cursor != UNDO_CLEAN_INVALID)
        manager->_private_clean_cursor = UNDO_CLEAN_INVALID;
    commit_group(manager, group, &plan);
    return ASTRA_OK;
}

static AstraResult move_cursor(AstraUndoManager *manager, uint32_t direction)
{
    const UndoGroupHeader *group;
    uint32_t start;
    uint32_t end;
    AstraResult result;

    if (!manager_valid(manager))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (manager->_private_mode != UNDO_MODE_IDLE)
        return ASTRA_ERROR_BUSY;
    if (manager->_private_poisoned != 0u)
        return ASTRA_ERROR_IO;
    if (direction == ASTRA_UNDO_DIRECTION_UNDO) {
        if (manager->_private_cursor == 0u)
            return ASTRA_ERROR_NOT_PRESENT;
        end = manager->_private_cursor;
        group = previous_group(manager, end, &start);
    } else {
        if (manager->_private_cursor == manager->_private_used)
            return ASTRA_ERROR_NOT_PRESENT;
        start = manager->_private_cursor;
        if (manager->_private_used - start < sizeof(*group)) {
            group = NULL;
        } else {
            group = (const UndoGroupHeader *)(const void *)(
                (const uint8_t *)manager->_private_arena + start);
            if (group->total_bytes > manager->_private_used - start)
                group = NULL;
        }
        end = group == NULL ? start : start + group->total_bytes;
        if (group != NULL)
            group = group_at(manager, start, end);
    }
    if (group == NULL) {
        manager->_private_poisoned = 1u;
        manager->_private_generation = next_generation(
            manager->_private_generation);
        return ASTRA_ERROR_IO;
    }
    manager->_private_mode = UNDO_MODE_APPLYING;
    result = apply_serialized(manager, group, start, direction);
    manager->_private_mode = UNDO_MODE_IDLE;
    if (result != ASTRA_OK) {
        if (manager->_private_poisoned != 0u)
            manager->_private_generation = next_generation(
                manager->_private_generation);
        return result;
    }
    if (direction == ASTRA_UNDO_DIRECTION_UNDO) {
        manager->_private_cursor = start;
        --manager->_private_applied_group_count;
    } else {
        manager->_private_cursor = end;
        ++manager->_private_applied_group_count;
    }
    manager->_private_generation = next_generation(
        manager->_private_generation);
    return ASTRA_OK;
}

AstraResult astra_undo_undo(AstraUndoManager *manager)
{
    return move_cursor(manager, ASTRA_UNDO_DIRECTION_UNDO);
}

AstraResult astra_undo_redo(AstraUndoManager *manager)
{
    return move_cursor(manager, ASTRA_UNDO_DIRECTION_REDO);
}

AstraResult astra_undo_clear(AstraUndoManager *manager)
{
    if (!manager_valid(manager))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (manager->_private_mode != UNDO_MODE_IDLE)
        return ASTRA_ERROR_BUSY;
    memset(manager->_private_arena, 0, manager->_private_used);
    manager->_private_used = 0u;
    manager->_private_cursor = 0u;
    manager->_private_clean_cursor = 0u;
    manager->_private_group_count = 0u;
    manager->_private_applied_group_count = 0u;
    manager->_private_poisoned = 0u;
    manager->_private_generation = next_generation(
        manager->_private_generation);
    return ASTRA_OK;
}

AstraResult astra_undo_mark_clean(AstraUndoManager *manager)
{
    if (!manager_valid(manager))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (manager->_private_mode != UNDO_MODE_IDLE)
        return ASTRA_ERROR_BUSY;
    if (manager->_private_poisoned != 0u)
        return ASTRA_ERROR_IO;
    manager->_private_clean_cursor = manager->_private_cursor;
    manager->_private_generation = next_generation(
        manager->_private_generation);
    return ASTRA_OK;
}

AstraResult astra_undo_get_state(const AstraUndoManager *manager,
                                 AstraUndoState *state)
{
    const uint8_t *arena;

    if (!manager_valid(manager) || state == NULL ||
        state->size < sizeof(*state) ||
        !astra_words_zero(state->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    arena = manager->_private_arena;
    state->generation = manager->_private_generation;
    state->flags = 0u;
    if (manager->_private_cursor != 0u)
        state->flags |= ASTRA_UNDO_STATE_CAN_UNDO;
    if (manager->_private_cursor != manager->_private_used)
        state->flags |= ASTRA_UNDO_STATE_CAN_REDO;
    if (manager->_private_clean_cursor != UNDO_CLEAN_INVALID &&
        manager->_private_cursor == manager->_private_clean_cursor)
        state->flags |= ASTRA_UNDO_STATE_CLEAN;
    if (manager->_private_poisoned != 0u)
        state->flags |= ASTRA_UNDO_STATE_POISONED;
    state->history_bytes = manager->_private_used;
    state->applied_bytes = manager->_private_cursor;
    state->group_count = manager->_private_group_count;
    state->applied_group_count = manager->_private_applied_group_count;
    state->undo_name = NULL;
    state->undo_name_length = 0u;
    state->redo_name = NULL;
    state->redo_name_length = 0u;
    if (manager->_private_cursor != 0u) {
        uint32_t start;
        const UndoGroupHeader *group = previous_group(
            manager, manager->_private_cursor, &start);

        if (group == NULL)
            return ASTRA_ERROR_IO;
        state->undo_name = group->name_length == 0u ? NULL :
            (const char *)(const void *)(arena + start + sizeof(*group));
        state->undo_name_length = group->name_length;
    }
    if (manager->_private_cursor != manager->_private_used) {
        uint32_t start = manager->_private_cursor;
        const UndoGroupHeader *candidate;
        const UndoGroupHeader *group = NULL;

        if (manager->_private_used - start >= sizeof(*candidate)) {
            candidate = (const UndoGroupHeader *)(const void *)(arena + start);
            if (candidate->total_bytes <= manager->_private_used - start)
                group = group_at(manager, start,
                                 start + candidate->total_bytes);
        }

        if (group == NULL)
            return ASTRA_ERROR_IO;
        state->redo_name = group->name_length == 0u ? NULL :
            (const char *)(const void *)(arena + start + sizeof(*group));
        state->redo_name_length = group->name_length;
    }
    memset(state->reserved, 0, sizeof(state->reserved));
    return ASTRA_OK;
}

AstraResult astra_undo_move_arena(AstraUndoManager *manager, void *arena,
                                  uint32_t arena_bytes)
{
    uintptr_t old_start;
    uintptr_t old_end;
    uintptr_t new_start;
    uintptr_t new_end;

    if (!manager_valid(manager) || arena == NULL ||
        ((uintptr_t)arena & (UNDO_ALIGNMENT - 1u)) != 0u ||
        arena_bytes < manager->_private_used)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (manager->_private_mode != UNDO_MODE_IDLE)
        return ASTRA_ERROR_BUSY;
    if (arena == manager->_private_arena) {
        return arena_bytes == manager->_private_capacity ? ASTRA_OK :
               ASTRA_ERROR_INVALID_ARGUMENT;
    }
    old_start = (uintptr_t)manager->_private_arena;
    new_start = (uintptr_t)arena;
    if (old_start > UINTPTR_MAX - manager->_private_capacity ||
        new_start > UINTPTR_MAX - arena_bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    old_end = old_start + manager->_private_capacity;
    new_end = new_start + arena_bytes;
    if (old_start < new_end && old_end > new_start)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    memcpy(arena, manager->_private_arena, manager->_private_used);
    memset(manager->_private_arena, 0, manager->_private_used);
    manager->_private_arena = arena;
    manager->_private_capacity = arena_bytes;
    manager->_private_generation = next_generation(
        manager->_private_generation);
    return ASTRA_OK;
}
