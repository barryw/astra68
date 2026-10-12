#define main hostfs_unused_main
#include "../main.c"
#undef main

#include <assert.h>

static uint64_t values[3];
static uint32_t counts[3], closed, getters;
static uint32_t init_status, open_status, value_status;

uint32_t astra_process_vfs_init(const AstraStartupInfo *startup)
{
    (void)startup;
    return init_status;
}

uint32_t astra_config_open(const AstraStartupInfo *startup, uint32_t schema,
    uint32_t flags, AstraConfig *config, AstraConfigError *error)
{
    (void)startup; (void)config; (void)error;
    assert(schema == 1u && flags == ASTRA_CONFIG_OPEN_READ);
    return open_status;
}

static unsigned key(const char *name)
{
    const char *const names[] = {"owner_files", "owner_sessions", "queue_messages"};
    for (unsigned index = 0u; index < 3u; ++index)
        if (strcmp(name, names[index]) == 0)
            return index;
    assert(0);
    return 0u;
}

uint32_t astra_config_count(const AstraConfig *config, const char *name, uint32_t *count)
{
    (void)config;
    *count = counts[key(name)];
    return ASTRA_CONFIG_OK;
}

uint32_t astra_config_get_u64(const AstraConfig *config, const char *name,
    uint32_t index, uint64_t *value)
{
    (void)config;
    assert(index == 0u);
    ++getters;
    *value = values[key(name)];
    return value_status;
}

void astra_config_close(AstraConfig *config)
{
    (void)config;
    ++closed;
}

static void reset(void)
{
    values[0] = 1024u; values[1] = 64u; values[2] = 16u;
    counts[0] = counts[1] = counts[2] = 1u;
    closed = getters = 0u;
    init_status = ASTRA_VFS_OK;
    open_status = value_status = ASTRA_CONFIG_OK;
}

int main(void)
{
    reset();
    assert(configured_limits(NULL) == ASTRA_STATUS_OK);
    assert(owner_files == 1024u && owner_sessions == 64u && queue_messages == 16u && closed == 1u);
    for (unsigned index = 0u; index < 3u; ++index) {
        reset(); values[index] = 0u;
        assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 1u);
        reset(); counts[index] = 0u;
        assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 1u && getters == index);
        reset(); counts[index] = 2u;
        assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 1u && getters == index);
        reset(); values[index] = UINT64_C(1) << 32;
        assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 1u);
    }
    reset(); values[2] = ASTRA_PORT_MESSAGES_MAX;
    assert(configured_limits(NULL) == ASTRA_STATUS_OK);
    reset(); values[2] = ASTRA_PORT_MESSAGES_MAX + 1u;
    assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 1u);
    reset(); value_status = ASTRA_CONFIG_INVALID;
    assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 1u);
    reset(); open_status = ASTRA_CONFIG_INVALID;
    assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 0u);
    reset(); init_status = ASTRA_VFS_ERR_PEER;
    assert(configured_limits(NULL) == ASTRA_STATUS_INVALID && closed == 0u);
    return 0;
}
