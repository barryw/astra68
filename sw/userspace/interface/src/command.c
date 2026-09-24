#include <astra/command.h>

#include <stddef.h>

static int same_id(const AstraCommand *command, const char *id,
                   uint32_t length)
{
    if (command->id == NULL || command->id_length != length)
        return 0;
    for (uint32_t at = 0u; at < length; ++at)
        if (command->id[at] != id[at])
            return 0;
    return 1;
}

static AstraResult find_command(const AstraCommandScope *scope,
                                const char *id, uint32_t length,
                                const AstraCommand **found)
{
    const AstraCommandScope *slow = scope;
    const AstraCommandScope *fast = scope;

    *found = NULL;
    if (scope == NULL || id == NULL || length == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    /* A malformed caller-owned chain must not trap menu/key dispatch forever. */
    while (fast != NULL && fast->parent != NULL) {
        slow = slow->parent;
        fast = fast->parent->parent;
        if (slow == fast)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    for (; scope != NULL; scope = scope->parent) {
        if (scope->command_count != 0u && scope->commands == NULL)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        for (uint32_t at = 0u; at < scope->command_count; ++at)
            if (same_id(&scope->commands[at], id, length)) {
                *found = &scope->commands[at];
                return ASTRA_OK;
            }
    }
    return ASTRA_ERROR_NOT_PRESENT;
}

static AstraResult command_state(const AstraCommand *command,
                                 AstraCommandState *state)
{
    AstraResult result;

    *state = (AstraCommandState){0};
    if (command->query == NULL || command->invoke == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = command->query(command->context, state);
    if (result != ASTRA_OK)
        *state = (AstraCommandState){0};
    else if (state->label_length != 0u && state->label == NULL) {
        *state = (AstraCommandState){0};
        return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    return result;
}

AstraResult astra_interface_command_query(const AstraCommandScope *scope,
                                           const char *id,
                                           uint32_t id_length,
                                           AstraCommandState *state)
{
    const AstraCommand *command;
    AstraResult result;

    if (state == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *state = (AstraCommandState){0};
    result = find_command(scope, id, id_length, &command);
    if (result != ASTRA_OK)
        return result;
    return command_state(command, state);
}

AstraResult astra_interface_command_invoke(
    const AstraCommandScope *scope, const char *id, uint32_t id_length,
    const AstraCommandArguments *args)
{
    const AstraCommand *command;
    AstraCommandState state;
    AstraResult result;

    if (args != NULL && args->length != 0u && args->bytes == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = find_command(scope, id, id_length, &command);
    if (result != ASTRA_OK)
        return result;
    if (command->argument_type == 0u) {
        if (command->argument_version != 0u ||
            (args != NULL && (args->type != 0u || args->version != 0u ||
                              args->length != 0u)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    } else if (command->argument_version == 0u || args == NULL ||
               args->type != command->argument_type ||
               args->version != command->argument_version)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = command_state(command, &state);
    if (result != ASTRA_OK)
        return result;
    if (state.enabled == 0u)
        return ASTRA_ERROR_PERMISSION;
    return command->invoke(command->context, args);
}
