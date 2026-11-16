#include <astra/interface_library.h>
#include <astra/messaging.h>
#include <astra/port.h>
#include <astra/runtime.h>
#include <astra/service_manager.h>
#include <astra/string.h>
#include <astra/system_panels.h>
#include <string.h>

typedef struct PowerPanel {
    AstraHandle gui, manager, broker;
    AstraPort events;
    uint64_t subscription;
    AstraAlert alert;
    AstraSystemPowerStatus shown;
    uint32_t terminal;
    char message[sizeof(((AstraSystemPowerStatus *)0)->name) * 4u + 512u];
} PowerPanel;

static const char *power_title(uint32_t operation)
{
    return operation == ASTRA_SERVICE_MANAGER_SYSTEM_RESTART ? "Restart" : "Shut Down";
}

static AstraResult power_show(PowerPanel *state, uint32_t operation,
    uint32_t terminal, uint32_t enabled)
{
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    info.title = power_title(operation);
    info.title_length = (uint16_t)strlen(info.title);
    info.message = state->message;
    info.message_length = (uint16_t)strlen(state->message);
    info.button = terminal ? "Close" : enabled ? "Cancel" : "Please wait";
    info.button_length = (uint16_t)strlen(info.button);
    info.kind = terminal ? ASTRA_ALERT_ERROR : ASTRA_ALERT_INFORMATION;
    AstraResult result = state->alert._private_state == NULL ?
        astra_interface_alert_open(state->gui, &info, NULL, 0u, 0u, &state->alert) :
        astra_interface_alert_update(&state->alert, &info);
    if (result == ASTRA_OK)
        result = astra_interface_alert_set_enabled(&state->alert, enabled);
    if (result == ASTRA_OK) state->terminal = terminal;
    return result;
}

static AstraResult power_refresh(PowerPanel *state)
{
    static const char *const steps[] = {"None", "Prepare", "Request consent",
        "Await reply", "Consent refused", "Await clean exit", "Terminate",
        "Await exit", "Check dependencies", "Restore services", "System power",
        "Abort", "User cancellation"};
    AstraSystemPowerStatus power = ASTRA_SYSTEM_POWER_STATUS_INIT;
    AstraResult result = astra_system_power_status(state->manager, &power);
    if (result != ASTRA_OK) return result;
    if (memcmp(&power, &state->shown, sizeof(power)) == 0) return ASTRA_OK;
    if (power.phase == ASTRA_SYSTEM_POWER_IDLE ||
        (power.phase == ASTRA_SYSTEM_POWER_CANCELLED &&
         power.step == ASTRA_SYSTEM_POWER_STEP_USER_CANCEL &&
         power.recovery_failures == 0u)) {
        result = astra_interface_alert_close(&state->alert);
    } else {
        AstraString text;
        astra_string_init(&text, state->message, sizeof(state->message));
        (void)astra_string_append(&text,
            power.phase == ASTRA_SYSTEM_POWER_RECOVERING ? "Restoring services after cancellation.\n" :
            power.phase == ASTRA_SYSTEM_POWER_CANCELLED ? "Power operation stopped.\n" :
            power.flags & ASTRA_SYSTEM_POWER_CANCEL_REQUESTED ? "Cancellation requested; waiting for a safe stop.\n" :
            power.phase == ASTRA_SYSTEM_POWER_APPLICATIONS ? "Waiting for applications to close.\n" :
                "Stopping services safely.\n");
        if (power.name[0] != '\0') {
            (void)astra_string_append_display(&text, power.name, (uint32_t)strlen(power.name));
            (void)astra_string_append(&text, " — PID ");
            (void)astra_string_append_u64(&text, power.process_id);
            (void)astra_string_append_char(&text, '\n');
        }
        (void)astra_string_append(&text, "Step: ");
        (void)astra_string_append(&text, steps[power.step]);
        (void)astra_string_append(&text, "\nStatus: ");
        (void)astra_string_append_u64(&text, power.status);
        (void)astra_string_append(&text, " — reason: ");
        (void)astra_string_append_u64(&text, power.reason);
        if (power.recovery_failures != 0u) {
            (void)astra_string_append(&text, "\nServices not restored: ");
            (void)astra_string_append_u64(&text, power.recovery_failures);
        }
        if (text.truncated) return ASTRA_ERROR_BUFFER_TOO_SMALL;
        uint32_t terminal = power.phase == ASTRA_SYSTEM_POWER_CANCELLED;
        uint32_t enabled = terminal ||
            (power.phase != ASTRA_SYSTEM_POWER_RECOVERING &&
             (power.flags & ASTRA_SYSTEM_POWER_CANCEL_REQUESTED) == 0u);
        result = power_show(state, power.operation, terminal, enabled);
        if (result != ASTRA_OK &&
            (power.phase == ASTRA_SYSTEM_POWER_APPLICATIONS ||
             power.phase == ASTRA_SYSTEM_POWER_SERVICES) &&
            (power.flags & ASTRA_SYSTEM_POWER_CANCEL_REQUESTED) == 0u) {
            AstraResult ignored = astra_system_power_cancel(state->manager, power.transaction);
            (void)ignored;
        }
    }
    if (result == ASTRA_OK) state->shown = power;
    return result;
}

AstraResult astra_interface_power_panel_close(AstraPowerPanel *panel)
{
    if (panel == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    PowerPanel *state = panel->_private_state;
    if (state == NULL) return ASTRA_OK;
    AstraResult result = astra_interface_alert_close(&state->alert);
    if (result != ASTRA_OK) return result;
    if (state->subscription != 0u) {
        result = astra_messaging_unsubscribe(state->broker, state->subscription);
        if (result != ASTRA_OK && result != ASTRA_ERROR_PEER_DEAD) return result;
        state->subscription = 0u;
    }
    result = astra_port_close(&state->events);
    if (result != ASTRA_OK) return result;
    astra_runtime_deallocate(state);
    panel->_private_state = NULL;
    return ASTRA_OK;
}

AstraResult astra_interface_power_panel_open(AstraPowerPanel *panel,
    AstraHandle gui, AstraHandle manager, AstraHandle broker)
{
    if (panel == NULL || panel->_private_state != NULL || gui == ASTRA_INVALID_HANDLE ||
        manager == ASTRA_INVALID_HANDLE || broker == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    PowerPanel *state = astra_runtime_callocate(1u, sizeof(*state));
    if (state == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    state->gui = gui; state->manager = manager; state->broker = broker;
    panel->_private_state = state;
    AstraResult result = astra_port_create(1u, sizeof(AstraMessagingEvent), &state->events);
    if (result == ASTRA_OK)
        result = astra_messaging_subscribe(broker, ASTRA_MESSAGING_SERVICE,
            "supervisor", 1u, ASTRA_SYSTEM_POWER_TOPIC,
            &state->events.send, &state->subscription);
    if (result == ASTRA_OK) result = power_refresh(state);
    if (result != ASTRA_OK) {
        AstraResult ignored = astra_interface_power_panel_close(panel); (void)ignored;
    }
    return result;
}

uint32_t astra_interface_power_panel_wait_handles(const AstraPowerPanel *panel,
    AstraHandle handles[2])
{
    PowerPanel *state = panel == NULL ? NULL : panel->_private_state;
    if (state == NULL || handles == NULL) return 0u;
    handles[0] = state->events.receive;
    AstraHandle alert = astra_interface_alert_wait_handle(&state->alert);
    if (alert == ASTRA_INVALID_HANDLE) return 1u;
    handles[1] = alert;
    return 2u;
}

AstraResult astra_interface_power_panel_poll(AstraPowerPanel *panel)
{
    PowerPanel *state = panel == NULL ? NULL : panel->_private_state;
    if (state == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraMessagingEvent event;
    AstraResult result = astra_messaging_event_try(state->events.receive, &event, NULL);
    if (result == ASTRA_OK) {
        if (event.kind != ASTRA_MESSAGING_SERVICE || event.process_id != 1u ||
            event.subscription != state->subscription || event.payload_size != 0u ||
            strcmp(event.name, "supervisor") != 0 ||
            strcmp(event.topic, ASTRA_SYSTEM_POWER_TOPIC) != 0) return ASTRA_ERROR_IO;
        result = power_refresh(state);
    } else if (result == ASTRA_ERROR_WOULD_BLOCK) result = ASTRA_OK;
    if (result != ASTRA_OK || state->alert._private_state == NULL) return result;
    uint32_t choice = ASTRA_ALERT_PENDING;
    result = astra_interface_alert_poll(&state->alert, &choice);
    if (result == ASTRA_ERROR_WOULD_BLOCK) return ASTRA_OK;
    if (result != ASTRA_OK || choice == ASTRA_ALERT_PENDING) return result;
    if (state->terminal) return astra_interface_alert_close(&state->alert);
    if (state->shown.phase == ASTRA_SYSTEM_POWER_RECOVERING ||
        state->shown.flags & ASTRA_SYSTEM_POWER_CANCEL_REQUESTED) return ASTRA_OK;
    result = astra_system_power_cancel(state->manager, state->shown.transaction);
    AstraResult refreshed = power_refresh(state);
    /* The transaction may have finished between the click and admission. */
    return refreshed != ASTRA_OK ? refreshed :
        state->shown.phase == ASTRA_SYSTEM_POWER_CANCELLED ||
        state->shown.phase == ASTRA_SYSTEM_POWER_RECOVERING ? ASTRA_OK : result;
}

AstraResult astra_interface_power_panel_request(AstraPowerPanel *panel,
    uint32_t operation)
{
    PowerPanel *state = panel == NULL ? NULL : panel->_private_state;
    if (state == NULL || (operation != ASTRA_SERVICE_MANAGER_SHUTDOWN &&
        operation != ASTRA_SERVICE_MANAGER_SYSTEM_RESTART)) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = power_refresh(state);
    if (result != ASTRA_OK) return result;
    if (state->shown.phase != ASTRA_SYSTEM_POWER_IDLE &&
        state->shown.phase != ASTRA_SYSTEM_POWER_CANCELLED) return ASTRA_ERROR_BUSY;
    AstraAlertInfo info = ASTRA_ALERT_INFO_INIT;
    info.title = info.button = power_title(operation);
    info.title_length = info.button_length = (uint16_t)strlen(info.title);
    info.message = operation == ASTRA_SERVICE_MANAGER_SYSTEM_RESTART ?
        "Restart Astra? Applications can ask you to save changes first." :
        "Shut down Astra? Applications can ask you to save changes first.";
    info.message_length = (uint16_t)strlen(info.message);
    uint32_t confirmed = 0u;
    result = astra_interface_confirm(state->gui, &info, "Cancel", 6u, &confirmed);
    if (result != ASTRA_OK || !confirmed) return result;
    (void)astra_string_copy(state->message, sizeof(state->message), "Preparing safe shutdown…");
    result = power_show(state, operation, 0u, 1u);
    if (result != ASTRA_OK) return result;
    result = operation == ASTRA_SERVICE_MANAGER_SYSTEM_RESTART ?
        astra_system_restart_request(state->manager) : astra_system_shutdown_request(state->manager);
    if (result == ASTRA_OK) return power_refresh(state);
    AstraString text;
    astra_string_init(&text, state->message, sizeof(state->message));
    (void)astra_string_append(&text, "Power request was refused. Result: ");
    (void)astra_string_append_i64(&text, result);
    AstraResult ignored = power_show(state, operation, 1u, 1u); (void)ignored;
    return result;
}
