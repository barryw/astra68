#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/utf8.h>

/* One canonical layout: aligned argv, aligned environment, then their strings.
 * Validate every pointer and NUL before consumers may use an unbounded strlen.
 * The immutable kernel payload never aliases capabilities or opaque handoff. */
static int
startup_vectors_valid(const AstraStartupInfo *startup)
{
    uint32_t base = startup->vector_data_address;
    uint32_t size = startup->vector_data_size;
    uint32_t argument_slots;
    uint32_t environment_slots;
    uint32_t vector_bytes;
    uint32_t end;
    uint32_t text;
    uint32_t words;

    if (startup->argc > ASTRA_LAUNCH_ARGUMENT_MAX ||
        startup->environment_count > ASTRA_LAUNCH_ENVIRONMENT_MAX ||
        startup->argc + 3u * startup->environment_count >
            ASTRA_LAUNCH_STRING_BYTES)
        return 0;
    if (startup->argc == 0u && startup->environment_count == 0u)
        return base == 0u && size == 0u && startup->argv_address == 0u &&
               startup->environment_address == 0u;
    if (base == 0u || (base & 3u) != 0u || size == 0u ||
        size > ASTRA_STARTUP_VECTOR_DATA_MAX || size > UINT32_MAX - base)
        return 0;
    end = base + size;
    {
        uint32_t header = (uint32_t)(uintptr_t)startup;

        if (ASTRA_STARTUP_BLOCK_SIZE > UINT32_MAX - header ||
            (base < header + ASTRA_STARTUP_BLOCK_SIZE && end > header))
            return 0;
    }
    argument_slots = startup->argc != 0u ? startup->argc + 1u : 0u;
    environment_slots = startup->environment_count != 0u ?
        startup->environment_count + 1u : 0u;
    vector_bytes = (argument_slots + environment_slots) * 4u;
    if (vector_bytes >= size || size - vector_bytes > ASTRA_LAUNCH_STRING_BYTES ||
        (startup->argc != 0u ? startup->argv_address != base :
                              startup->argv_address != 0u) ||
        (startup->environment_count != 0u ?
            startup->environment_address != base + argument_slots * 4u :
            startup->environment_address != 0u))
        return 0;
    if (startup->handoff_size != 0u &&
        base < startup->handoff_address + startup->handoff_size &&
        end > startup->handoff_address)
        return 0;
    text = base + vector_bytes;
    words = base;
    for (uint32_t list = 0u; list < 2u; ++list) {
        uint32_t count = list == 0u ? startup->argc : startup->environment_count;

        for (uint32_t index = 0u; index < count; ++index) {
            const char *value;
            uint32_t length = 0u;
            uint32_t equals = UINT32_MAX;

            if (*(const uint32_t *)(uintptr_t)words != text || text >= end)
                return 0;
            words += 4u;
            value = (const char *)(uintptr_t)text;
            while (length < end - text && value[length] != '\0') {
                if (value[length] == '=' && equals == UINT32_MAX)
                    equals = length;
                ++length;
            }
            if (length == end - text || !astra_utf8_validate(value, length, 0u) ||
                (list != 0u && (equals == UINT32_MAX || equals == 0u)))
                return 0;
            text += length + 1u;
        }
        if (count != 0u) {
            if (*(const uint32_t *)(uintptr_t)words != 0u)
                return 0;
            words += 4u;
        }
    }
    return text == end;
}

int
astra_startup_validate(const AstraStartupInfo *startup)
{
    uint32_t header;

    if (startup == NULL || startup->magic != ASTRA_STARTUP_MAGIC ||
        startup->abi_version != ASTRA_STARTUP_ABI_VERSION ||
        startup->header_size != ASTRA_STARTUP_INFO_SIZE ||
        startup->total_size != ASTRA_STARTUP_INFO_SIZE +
            startup->capability_count * ASTRA_STARTUP_CAPABILITY_SIZE ||
        startup->total_size > ASTRA_STARTUP_BLOCK_SIZE ||
        startup->syscall_abi_version != ASTRA_SYSCALL_ABI_VERSION ||
        (startup->flags & ~ASTRA_STARTUP_FLAG_MASK) != 0u ||
        startup->capability_count > ASTRA_STARTUP_CAPABILITY_MAX ||
        startup->launch_source > ASTRA_LAUNCH_SOURCE_DESKTOP ||
        startup->program_entry == 0u) {
        return 0;
    }
    header = (uint32_t)(uintptr_t)startup;
    if (ASTRA_STARTUP_BLOCK_SIZE > UINT32_MAX - header ||
        startup->capabilities_address != (startup->capability_count != 0u ?
            header + ASTRA_STARTUP_INFO_SIZE : 0u)) {
        return 0;
    }

    if ((startup->handoff_address == 0u) !=
        (startup->handoff_size == 0u) ||
        startup->handoff_size > UINT32_MAX - startup->handoff_address) {
        return 0;
    }
    if ((startup->flags & ASTRA_STARTUP_FLAG_INTERPRETED) == 0u) {
        if (startup->interpreter_base != 0u ||
            startup->interpreter_span != 0u ||
            startup->interpreter_entry != 0u)
            return 0;
    } else if (startup->interpreter_base == 0u ||
               startup->interpreter_span == 0u ||
               startup->interpreter_entry < startup->interpreter_base ||
               startup->interpreter_entry - startup->interpreter_base >=
                   startup->interpreter_span) {
        return 0;
    }
    /*
     * The diagnostic channel is bound here because this is the one call every
     * program makes before it does anything else, and because a handle that
     * has not been validated is not one to log through.
     */
    return startup_vectors_valid(startup);
}

AstraLaunchSource
astra_startup_launch_source(const AstraStartupInfo *startup)
{
    return astra_startup_validate(startup) ?
        (AstraLaunchSource)startup->launch_source :
        ASTRA_LAUNCH_SOURCE_SYSTEM;
}

const char *
astra_startup_argument(const AstraStartupInfo *startup, uint32_t index)
{
    const uint32_t *arguments;

    if (!astra_startup_validate(startup) || index >= startup->argc)
        return NULL;
    arguments = (const uint32_t *)(uintptr_t)startup->argv_address;
    return (const char *)(uintptr_t)arguments[index];
}

const AstraStartupCapability *
astra_startup_capability(const AstraStartupInfo *startup, const char *name)
{
    const AstraStartupCapability *capabilities;

    if (!astra_startup_validate(startup) || name == NULL)
        return NULL;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index)
        if (astra_capability_name_equal(capabilities[index].name, name))
            return &capabilities[index];
    return NULL;
}
