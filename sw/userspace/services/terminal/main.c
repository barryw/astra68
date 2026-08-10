#include <console_shell.h>
#include <loader.h>
#include <vfs_host.h>
#include <volume.h>

#include <astra/bytes.h>
#include <astra/display.h>
#include <astra/event.h>
#include <astra/event_control.h>
#include <astra/event_descriptor.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_assign.h>
#include <astra/vfs_port_transport.h>

#define TERMINAL_VFS_CLIENT_MAX 4u
#define TERMINAL_EVENT_PREFIX_RECORDS 5u

ASTRA_PROGRAM("terminal", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

/*
 * Message ids are descriptor addresses and the installed catalog is still the
 * supervisor catalog. Five supervisor descriptors precede console_shell.c in
 * that catalog, so reserve their addresses here. The Makefile compares the
 * remaining bytes and refuses a build if either layout drifts.
 *
 * ponytail: retain this prefix until catalogs become process-aware; that
 * larger event-format change is not needed to make the terminal a process.
 */
static const uint8_t terminal_event_prefix[
    TERMINAL_EVENT_PREFIX_RECORDS * ASTRA_EVENT_DESCRIPTOR_SIZE]
    __attribute__((section(".astra_events"), used, aligned(4))) = {0u};

static AstraAssignTable assigns;
static struct {
    AstraVfsClient client;
    uint32_t handle;
} clients[TERMINAL_VFS_CLIENT_MAX];
static uint32_t client_count;
static uint32_t event_control;

static const AstraStartupCapability *
capability(const AstraStartupInfo *startup,
           const AstraStartupCapability *capabilities, const char *name)
{
    for (uint32_t index = 0u; index < startup->capability_count; ++index)
        if (astra_capability_name_equal(capabilities[index].name, name))
            return &capabilities[index];
    return NULL;
}

static uint32_t connect_namespaces(void)
{
    for (uint32_t index = 0u; index < assigns.count; ++index) {
        uint32_t handle = assigns.entries[index].handle;
        uint32_t slot;

        for (slot = 0u; slot < client_count; ++slot)
            if (clients[slot].handle == handle)
                break;
        if (slot != client_count)
            continue;
        if (client_count == TERMINAL_VFS_CLIENT_MAX)
            return ASTRA_STATUS_LIMIT;
        clients[client_count].handle = handle;
        if (astra_vfs_port_connect(&clients[client_count].client,
                                   clients[client_count].handle) !=
                ASTRA_VFS_OK)
            return ASTRA_STATUS_PROTOCOL;
        ++client_count;
    }
    return client_count != 0u ? ASTRA_STATUS_OK : ASTRA_STATUS_NOT_FOUND;
}

AstraVfsClient *supervisor_vfs_client(void)
{
    return client_count != 0u ? &clients[0].client : NULL;
}

AstraAssignTable *supervisor_assigns(void)
{
    return &assigns;
}

AstraVfsClient *supervisor_vfs_client_for(const AstraAssign *assign)
{
    if (assign == NULL)
        return supervisor_vfs_client();
    for (uint32_t index = 0u; index < client_count; ++index)
        if (clients[index].handle == assign->handle)
            return &clients[index].client;
    return NULL;
}

void supervisor_vfs_set_activity(uint32_t activity)
{
    for (uint32_t index = 0u; index < client_count; ++index)
        clients[index].client.activity = activity;
}

uint32_t supervisor_loader_event_control(void)
{
    return event_control;
}

void supervisor_loader_pump_event_control(void)
{
    /* The boot-global target remains in the resident supervisor. */
}

uint32_t supervisor_volume_device_status(void)
{
    return 0u;
}

uint32_t supervisor_volume_device_failure(void)
{
    return 0u;
}

static void ready(uint32_t handle, uint32_t status)
{
    AstraServiceReady message;

    (void)memset(&message, 0, sizeof(message));
    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = ASTRA_SERVICE_PROTOCOL;
    message.header.protocol_version = ASTRA_SERVICE_VERSION;
    message.header.operation = ASTRA_SERVICE_READY;
    message.status = status;
    (void)astra_port_send(handle, &message, sizeof(message), NULL, 0u);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *display;
    const AstraStartupCapability *input;
    const AstraStartupCapability *control;
    uint32_t status;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    bootstrap = capability(startup, capabilities,
                           ASTRA_CAPABILITY_SERVICE_READY);
    display = capability(startup, capabilities,
                         ASTRA_CAPABILITY_DISPLAY_DEVICE);
    input = capability(startup, capabilities, ASTRA_CAPABILITY_INPUT_DEVICE);
    control = capability(startup, capabilities,
                         ASTRA_CAPABILITY_EVENT_CONTROL);
    if (bootstrap == NULL || display == NULL || input == NULL ||
        control == NULL)
        return ASTRA_STATUS_BAD_HANDLE;

    status = astra_assign_seed(&assigns, capabilities,
                               startup->capability_count) == ASTRA_VFS_OK ?
        connect_namespaces() : ASTRA_STATUS_INVALID;
    event_control = control->handle;

    ready(bootstrap->handle, status);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK)
        return (int)status;
    console_shell_run(display->handle, input->handle, 1);
    return ASTRA_STATUS_PEER_DEAD;
}
