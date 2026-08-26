#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

static AstraStartupCapability capabilities[2];
static AstraServiceReady sent_message;
static uint32_t sent_handle;
static uint32_t sent_handles[ASTRA_MESSAGE_HANDLES_MAX];
static uint32_t sent_handle_count;

uint32_t
astra_port_send(uint32_t handle, const void *message, uint32_t size,
                const uint32_t *handles, uint32_t handle_count)
{
    assert(message != NULL && size == sizeof(sent_message));
    assert(handle_count <= ASTRA_MESSAGE_HANDLES_MAX);
    sent_handle = handle;
    memcpy(&sent_message, message, sizeof(sent_message));
    sent_handle_count = handle_count;
    if (handle_count != 0u) {
        assert(handles != NULL);
        memcpy(sent_handles, handles, handle_count * sizeof(handles[0]));
    }
    return ASTRA_SYSCALL_OK;
}

static AstraStartupInfo
startup_info(void)
{
    AstraStartupInfo startup = {0};

    startup.magic = ASTRA_STARTUP_MAGIC;
    startup.abi_version = ASTRA_STARTUP_ABI_VERSION;
    startup.header_size = ASTRA_STARTUP_INFO_SIZE;
    startup.total_size = ASTRA_STARTUP_INFO_SIZE;
    startup.syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION;
    startup.capability_count = 2u;
    startup.capabilities_address = (uint32_t)(uintptr_t)capabilities;
    return startup;
}

int
main(void)
{
    AstraStartupInfo startup = startup_info();
    uint32_t handles[2] = {0x11111111u, 0x22222222u};

    memcpy(capabilities[0].name, "SERVICE_READY", sizeof("SERVICE_READY"));
    capabilities[0].handle = 7u;
    memcpy(capabilities[1].name, "GUI", sizeof("GUI"));
    capabilities[1].handle = 9u;

    assert(astra_startup_capability(&startup, "GUI") == &capabilities[1]);
    assert(astra_startup_capability(&startup, "MISSING") == NULL);
    assert(astra_startup_capability(NULL, "GUI") == NULL);
    assert(astra_startup_capability(&startup, NULL) == NULL);

    assert(astra_service_ready(7u, ASTRA_STATUS_OK, handles, 2u) ==
           ASTRA_SYSCALL_OK);
    assert(sent_handle == 7u);
    assert(sent_message.header.total_size == ASTRA_SERVICE_READY_SIZE);
    assert(sent_message.header.header_size == ASTRA_MESSAGE_HEADER_SIZE);
    assert(sent_message.header.flags == 0u);
    assert(sent_message.header.protocol == ASTRA_SERVICE_PROTOCOL);
    assert(sent_message.header.protocol_version == ASTRA_SERVICE_VERSION);
    assert(sent_message.header.reserved == 0u);
    assert(sent_message.header.operation == ASTRA_SERVICE_READY);
    assert(sent_message.header.transaction_id == 0u);
    assert(sent_message.status == ASTRA_STATUS_OK);
    assert(sent_handle_count == 2u && sent_handles[0] == handles[0] &&
           sent_handles[1] == handles[1]);

    assert(astra_service_ready(7u, ASTRA_STATUS_IO, handles, 2u) ==
           ASTRA_SYSCALL_OK);
    assert(sent_message.status == ASTRA_STATUS_IO);
    assert(sent_handle_count == 0u);
    return 0;
}
