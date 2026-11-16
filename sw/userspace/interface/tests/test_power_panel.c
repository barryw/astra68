#include <astra/interface_library.h>
#include <astra/messaging.h>
#include <astra/port.h>
#include <astra/runtime.h>
#include <astra/service_manager.h>
#include <astra/system_panels.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AstraSystemPowerStatus power;
static AstraResult query_result, request_result, unsubscribe_result, alert_result;
static uint32_t subscribed, confirmed, requests, cancels, choice, enabled, opened;
static uint32_t pending_event, wrong_publisher, last_cancel, allocations;
static char message[1600], title[32];

void *astra_runtime_callocate(size_t count, size_t bytes)
{ ++allocations; return calloc(count, bytes); }
void astra_runtime_deallocate(void *pointer)
{ if (pointer != NULL) --allocations; free(pointer); }
AstraResult astra_port_create(uint32_t messages, uint32_t bytes, AstraPort *port)
{
    assert(messages == 1u && bytes == sizeof(AstraMessagingEvent));
    port->receive = 4u; port->send = 5u; return ASTRA_OK;
}
AstraResult astra_port_close(AstraPort *port)
{ *port = (AstraPort)ASTRA_PORT_INIT; return ASTRA_OK; }
AstraResult astra_messaging_subscribe(AstraHandle broker, uint32_t kind,
    const char *name, uint32_t pid, const char *topic, AstraHandle *send, uint64_t *subscription)
{
    assert(broker == 3u && kind == ASTRA_MESSAGING_SERVICE && pid == 1u);
    assert(strcmp(name, "supervisor") == 0 && strcmp(topic, ASTRA_SYSTEM_POWER_TOPIC) == 0);
    assert(*send == 5u); *send = 0u; *subscription = 19u; subscribed = 1u;
    return ASTRA_OK;
}
AstraResult astra_messaging_unsubscribe(AstraHandle broker, uint64_t subscription)
{
    assert(broker == 3u && subscription == 19u);
    if (unsubscribe_result != ASTRA_OK) return unsubscribe_result;
    subscribed = 0u; return ASTRA_OK;
}
AstraResult astra_messaging_event_try(AstraHandle receive, AstraMessagingEvent *event, AstraHandle *reply)
{
    assert(receive == 4u && reply == NULL);
    if (!pending_event) return ASTRA_ERROR_WOULD_BLOCK;
    pending_event = 0u; memset(event, 0, sizeof(*event));
    event->kind = ASTRA_MESSAGING_SERVICE; event->process_id = wrong_publisher ? 2u : 1u;
    event->subscription = 19u;
    strcpy(event->name, "supervisor"); strcpy(event->topic, ASTRA_SYSTEM_POWER_TOPIC);
    return ASTRA_OK;
}
AstraResult astra_system_power_status(AstraHandle manager, AstraSystemPowerStatus *status)
{
    assert(manager == 2u && subscribed); /* Subscription precedes first read. */
    if (query_result != ASTRA_OK) return query_result;
    *status = power; return ASTRA_OK;
}
AstraResult astra_system_power_cancel(AstraHandle manager, uint32_t transaction)
{
    assert(manager == 2u); ++cancels; last_cancel = transaction;
    power.flags |= ASTRA_SYSTEM_POWER_CANCEL_REQUESTED; return ASTRA_OK;
}
static AstraResult request(uint32_t operation)
{
    ++requests;
    if (request_result != ASTRA_OK) return request_result;
    power = (AstraSystemPowerStatus)ASTRA_SYSTEM_POWER_STATUS_INIT;
    power.transaction = 23u; power.operation = operation;
    power.phase = ASTRA_SYSTEM_POWER_APPLICATIONS;
    power.step = ASTRA_SYSTEM_POWER_STEP_REPLY;
    power.process_id = 77u; strcpy(power.name, "Terminal\nprivate\\name");
    return ASTRA_OK;
}
AstraResult astra_system_shutdown_request(AstraHandle manager)
{ assert(manager == 2u); return request(ASTRA_SERVICE_MANAGER_SHUTDOWN); }
AstraResult astra_system_restart_request(AstraHandle manager)
{ assert(manager == 2u); return request(ASTRA_SERVICE_MANAGER_SYSTEM_RESTART); }
AstraResult astra_interface_confirm(AstraHandle gui, const AstraAlertInfo *info,
    const char *cancel, uint16_t length, uint32_t *accepted)
{
    assert(gui == 1u && info->message_length != 0u);
    assert(strcmp(cancel, "Cancel") == 0 && length == 6u);
    *accepted = confirmed; return ASTRA_OK;
}
static void capture(const AstraAlertInfo *info)
{
    assert(info->message_length < sizeof(message) && info->title_length < sizeof(title));
    memcpy(message, info->message, info->message_length); message[info->message_length] = '\0';
    memcpy(title, info->title, info->title_length); title[info->title_length] = '\0';
}
AstraResult astra_interface_alert_open(AstraHandle gui, const AstraAlertInfo *info,
    const char *cancel, uint16_t length, uint32_t flags, AstraAlert *alert)
{
    assert(gui == 1u && cancel == NULL && length == 0u && flags == 0u);
    if (alert_result != ASTRA_OK) return alert_result;
    assert(alert->_private_state == NULL); alert->_private_state = &opened; ++opened;
    capture(info); return ASTRA_OK;
}
AstraResult astra_interface_alert_update(AstraAlert *alert, const AstraAlertInfo *info)
{
    assert(alert->_private_state != NULL);
    if (alert_result != ASTRA_OK) return alert_result;
    capture(info); return ASTRA_OK;
}
AstraResult astra_interface_alert_set_enabled(AstraAlert *alert, uint32_t value)
{ assert(alert->_private_state != NULL); enabled = value; return ASTRA_OK; }
AstraHandle astra_interface_alert_wait_handle(const AstraAlert *alert)
{ return alert->_private_state == NULL ? ASTRA_INVALID_HANDLE : 6u; }
AstraResult astra_interface_alert_poll(AstraAlert *alert, uint32_t *accepted)
{
    assert(alert->_private_state != NULL); *accepted = choice; choice = ASTRA_ALERT_PENDING;
    return ASTRA_OK;
}
AstraResult astra_interface_alert_close(AstraAlert *alert)
{ alert->_private_state = NULL; return ASTRA_OK; }

static void notify(AstraPowerPanel *panel)
{ pending_event = 1u; assert(astra_interface_power_panel_poll(panel) == ASTRA_OK); }

int main(void)
{
    AstraPowerPanel panel = ASTRA_POWER_PANEL_INIT;
    AstraHandle waits[2];
    assert(astra_interface_power_panel_wait_handles(&panel, waits) == 0u);
    assert(astra_interface_power_panel_open(&panel, 1u, 2u, 3u) == ASTRA_OK);
    assert(opened == 0u && allocations == 1u);
    assert(astra_interface_power_panel_wait_handles(&panel, waits) == 1u && waits[0] == 4u);
    assert(astra_interface_power_panel_request(&panel, 999u) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_interface_power_panel_request(&panel, ASTRA_SERVICE_MANAGER_SHUTDOWN) == ASTRA_OK);
    assert(requests == 0u); /* Initial Cancel has no native side effect. */
    confirmed = 1u;
    alert_result = ASTRA_ERROR_OUT_OF_MEMORY;
    assert(astra_interface_power_panel_request(&panel, ASTRA_SERVICE_MANAGER_SHUTDOWN) == alert_result);
    assert(requests == 0u);
    alert_result = ASTRA_OK;
    assert(astra_interface_power_panel_request(&panel, ASTRA_SERVICE_MANAGER_SYSTEM_RESTART) == ASTRA_OK);
    assert(requests == 1u && enabled && strcmp(title, "Restart") == 0);
    assert(strstr(message, "Terminal\\x0aprivate\\\\name — PID 77") != NULL);
    assert(astra_interface_power_panel_wait_handles(&panel, waits) == 2u && waits[1] == 6u);
    choice = ASTRA_ALERT_CANCELLED; /* Window close also cancels safely. */
    assert(astra_interface_power_panel_poll(&panel) == ASTRA_OK);
    assert(cancels == 1u && last_cancel == 23u && !enabled);
    choice = ASTRA_ALERT_ACCEPTED;
    assert(astra_interface_power_panel_poll(&panel) == ASTRA_OK && cancels == 1u);
    power.phase = ASTRA_SYSTEM_POWER_RECOVERING; notify(&panel);
    assert(!enabled && strstr(message, "Restoring") != NULL);
    choice = ASTRA_ALERT_CANCELLED;
    assert(astra_interface_power_panel_poll(&panel) == ASTRA_OK && cancels == 1u);
    power.phase = ASTRA_SYSTEM_POWER_CANCELLED; power.step = ASTRA_SYSTEM_POWER_STEP_USER_CANCEL;
    notify(&panel);
    assert(astra_interface_power_panel_wait_handles(&panel, waits) == 1u);
    power.step = ASTRA_SYSTEM_POWER_STEP_DECLINED;
    power.status = 11u; power.reason = 42u; power.recovery_failures = 2u;
    notify(&panel);
    assert(enabled && strstr(message, "Status: 11 — reason: 42") != NULL);
    assert(strstr(message, "Services not restored: 2") != NULL);
    choice = ASTRA_ALERT_ACCEPTED;
    assert(astra_interface_power_panel_poll(&panel) == ASTRA_OK);
    uint32_t previous_opened = opened;
    notify(&panel); assert(opened == previous_opened); /* Same snapshot stays dismissed. */
    wrong_publisher = pending_event = 1u;
    assert(astra_interface_power_panel_poll(&panel) == ASTRA_ERROR_IO);
    wrong_publisher = 0u;
    unsubscribe_result = ASTRA_ERROR_BUSY;
    assert(astra_interface_power_panel_close(&panel) == ASTRA_ERROR_BUSY);
    assert(panel._private_state != NULL && allocations == 1u);
    unsubscribe_result = ASTRA_OK;
    assert(astra_interface_power_panel_close(&panel) == ASTRA_OK && allocations == 0u);
    /* A replacement Desktop shows the retained refusal, including long names. */
    memset(power.name, 'x', sizeof(power.name) - 1u); power.name[255] = '\0';
    assert(astra_interface_power_panel_open(&panel, 1u, 2u, 3u) == ASTRA_OK);
    assert(strstr(message, power.name) != NULL);
    request_result = ASTRA_ERROR_PERMISSION;
    assert(astra_interface_power_panel_request(&panel, ASTRA_SERVICE_MANAGER_SHUTDOWN) == request_result);
    assert(strstr(message, "Power request was refused. Result: -") != NULL);
    request_result = ASTRA_OK;
    assert(astra_interface_power_panel_request(&panel, ASTRA_SERVICE_MANAGER_SHUTDOWN) == ASTRA_OK);
    alert_result = ASTRA_ERROR_OUT_OF_MEMORY;
    power.process_id = 88u;
    pending_event = 1u;
    assert(astra_interface_power_panel_poll(&panel) == ASTRA_ERROR_OUT_OF_MEMORY);
    assert(cancels == 2u && last_cancel == 23u); /* Lost progress UI never authorizes continuation. */
    alert_result = ASTRA_OK;
    assert(astra_interface_power_panel_close(&panel) == ASTRA_OK && allocations == 0u);
    query_result = ASTRA_ERROR_IO;
    assert(astra_interface_power_panel_open(&panel, 1u, 2u, 3u) == ASTRA_ERROR_IO);
    assert(panel._private_state == NULL && allocations == 0u && !subscribed);
    puts("power panel tests passed");
    return 0;
}
