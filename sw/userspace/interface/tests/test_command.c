#include <astra/command.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Probe {
    uint32_t calls;
    uint32_t query_calls;
    uint8_t enabled;
    const void *last_bytes;
    uint32_t last_length;
} Probe;

static AstraResult query(void *context, AstraCommandState *state)
{
    Probe *probe = context;

    ++probe->query_calls;
    *state = (AstraCommandState){"New Terminal", 12u,
                                 probe->enabled, 0u};
    return ASTRA_OK;
}

static AstraResult invoke(void *context, const AstraCommandArguments *args)
{
    Probe *probe = context;

    ++probe->calls;
    probe->last_bytes = args != NULL ? args->bytes : NULL;
    probe->last_length = args != NULL ? args->length : 0u;
    return ASTRA_OK;
}

static AstraResult broken_query(void *context, AstraCommandState *state)
{
    (void)context;
    *state = (AstraCommandState){NULL, 4u, 1u, 0u};
    return ASTRA_OK;
}

int main(void)
{
    Probe workspace_probe = {0u, 0u, 1u, NULL, 0u};
    Probe window_probe = {0u, 0u, 1u, NULL, 0u};
    AstraCommand workspace_commands[] = {
        {"workspace.new_terminal", 22u, 1u, 1u, query, invoke,
         &workspace_probe}
    };
    AstraCommand window_commands[] = {
        {"workspace.new_terminal", 22u, 1u, 1u, query, invoke,
         &window_probe}
    };
    AstraCommandScope workspace = {NULL, workspace_commands, 1u};
    AstraCommandScope window = {&workspace, window_commands, 1u};
    AstraCommandState state;
    uint32_t payload = 0x12345678u;
    AstraCommandArguments args = {1u, 1u, &payload, sizeof(payload)};

    assert(astra_interface_command_query(
               &window, "workspace.new_terminal", 22u, &state) == ASTRA_OK);
    assert(state.enabled == 1u && state.label_length == 12u);
    assert(window_probe.query_calls == 1u &&
           workspace_probe.query_calls == 0u);
    assert(astra_interface_command_invoke(
               &window, "workspace.new_terminal", 22u, &args) == ASTRA_OK);
    assert(window_probe.calls == 1u && workspace_probe.calls == 0u);
    assert(window_probe.last_bytes == &payload &&
           window_probe.last_length == sizeof(payload));
    assert(astra_interface_command_invoke(
               &window, "workspace.new_terminal", 22u,
               &(AstraCommandArguments){2u, 1u, &payload,
                                        sizeof(payload)}) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(window_probe.calls == 1u);

    /* A disabled local declaration must not fall through to another action. */
    window_probe.enabled = 0u;
    assert(astra_interface_command_invoke(
               &window, "workspace.new_terminal", 22u, &args) ==
           ASTRA_ERROR_PERMISSION);
    assert(window_probe.calls == 1u && workspace_probe.calls == 0u);
    window.command_count = 0u;
    assert(astra_interface_command_invoke(
               &window, "workspace.new_terminal", 22u, &args) == ASTRA_OK);
    assert(workspace_probe.calls == 1u);

    assert(astra_interface_command_query(
               &window, "workspace.missing", 17u, &state) ==
           ASTRA_ERROR_NOT_PRESENT);
    assert(state.label == NULL && state.enabled == 0u);
    assert(astra_interface_command_invoke(
               &window, "workspace.new_terminal", 22u,
               &(AstraCommandArguments){1u, 1u, NULL, 1u}) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_command_query(
               &window, NULL, 22u, &state) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_command_query(
               &window, "workspace.new_terminal", 22u, NULL) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    workspace_commands[0].argument_type = 0u;
    workspace_commands[0].argument_version = 0u;
    assert(astra_interface_command_invoke(
               &workspace, "workspace.new_terminal", 22u, NULL) == ASTRA_OK);
    assert(workspace_probe.calls == 2u);
    assert(astra_interface_command_invoke(
               &workspace, "workspace.new_terminal", 22u, &args) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    workspace_commands[0].query = broken_query;
    assert(astra_interface_command_invoke(
               &workspace, "workspace.new_terminal", 22u, NULL) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(workspace_probe.calls == 2u);
    window.parent = &window;
    assert(astra_interface_command_invoke(
               &window, "workspace.new_terminal", 22u, NULL) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    return 0;
}
