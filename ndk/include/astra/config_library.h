#ifndef ASTRA_CONFIG_LIBRARY_H
#define ASTRA_CONFIG_LIBRARY_H

/** @file config_library.h @brief Location-independent configuration API. */

#include <stdint.h>

/** Logical name of config.library. */
#define ASTRA_CONFIG_LIBRARY_NAME "config.library"
/** Config Kit ELF ABI major version. */
#define ASTRA_CONFIG_LIBRARY_ABI_MAJOR 1u
/** Config Kit backward-compatible ABI revision. */
#define ASTRA_CONFIG_LIBRARY_ABI_MINOR 0u
/** Minimum compatible config.library major version. */
#define ASTRA_CONFIG_LIBRARY_VERSION ASTRA_CONFIG_LIBRARY_ABI_MAJOR
/** Launch capability carrying the caller's private configuration root. */
#define ASTRA_CONFIG_CAPABILITY "CONFIG"
/** Launch capability carrying the configuration root for commands. */
#define ASTRA_CONFIG_COMMANDS_CAPABILITY "CONFIG_COMMANDS"

/** Canonical configuration namespace owner classes. */
enum {
    ASTRA_CONFIG_OWNER_SYSTEM = 1,
    ASTRA_CONFIG_OWNER_SERVICE = 2,
    ASTRA_CONFIG_OWNER_COMMAND = 3,
    ASTRA_CONFIG_OWNER_APPLICATION = 4
};

/** Request read access when opening configuration. */
#define ASTRA_CONFIG_OPEN_READ  (1u << 0)
/** Request write access when opening configuration. */
#define ASTRA_CONFIG_OPEN_WRITE (1u << 1)

#include <astra/process.h>

enum {
    ASTRA_CONFIG_OK = 0,
    ASTRA_CONFIG_INVALID = 1,
    ASTRA_CONFIG_NOT_FOUND = 2,
    ASTRA_CONFIG_ACCESS = 3,
    ASTRA_CONFIG_IO = 4,
    ASTRA_CONFIG_NO_MEMORY = 5,
    ASTRA_CONFIG_MALFORMED = 6,
    ASTRA_CONFIG_UNSUPPORTED_VERSION = 7,
    ASTRA_CONFIG_BUFFER_TOO_SMALL = 8,
    ASTRA_CONFIG_CLOSED = 9
};

/** Open configuration document owned by one caller. */
typedef struct AstraConfig {
    /** @cond ASTRA_INTERNAL */
    uint32_t _private_area;
    void *_private_state;
    /** @endcond */
} AstraConfig;

/** Empty configuration-handle initializer. */
#define ASTRA_CONFIG_INIT { 0, 0 }

/**
 * Derive the canonical directory for an owner class below @p parent.
 * @param parent Nonempty NUL-terminated capability-relative parent path.
 * @param owner_kind One ASTRA_CONFIG_OWNER_* value.
 * @param out Receives the canonical NUL-terminated path.
 * @param capacity Bytes available at @p out, including its terminator.
 * @return ASTRA_CONFIG_OK, ASTRA_CONFIG_INVALID for invalid input, or
 * ASTRA_CONFIG_BUFFER_TOO_SMALL when @p out cannot hold the complete path.
 */
uint32_t astra_config_scope_root(const char *parent, uint32_t owner_kind,
                                 char *out, uint32_t capacity);
/**
 * Derive one owner's root below an already scoped @p parent.
 * @param parent Nonempty NUL-terminated scoped parent path.
 * @param owner Nonempty owner name containing only portable path-component
 * characters and not beginning with a period.
 * @param out Receives the canonical NUL-terminated path.
 * @param capacity Bytes available at @p out, including its terminator.
 * @return ASTRA_CONFIG_OK, ASTRA_CONFIG_INVALID for invalid input, or
 * ASTRA_CONFIG_BUFFER_TOO_SMALL when @p out cannot hold the complete path.
 */
uint32_t astra_config_owner_root(const char *parent, const char *owner,
                                 char *out, uint32_t capacity);
/**
 * Derive the canonical private capability root for one configuration owner.
 * @param parent Nonempty NUL-terminated capability-relative parent path.
 * @param owner_kind One ASTRA_CONFIG_OWNER_* value.
 * @param owner Nonempty owner name containing only portable path-component
 * characters and not beginning with a period.
 * @param out Receives the canonical NUL-terminated path.
 * @param capacity Bytes available at @p out, including its terminator.
 * @return ASTRA_CONFIG_OK, ASTRA_CONFIG_INVALID for invalid input, or
 * ASTRA_CONFIG_BUFFER_TOO_SMALL when @p out cannot hold the complete path.
 */
uint32_t astra_config_capability_root(const char *parent,
                                      uint32_t owner_kind,
                                      const char *owner, char *out,
                                      uint32_t capacity);

/** Actionable configuration parse or schema error. */
typedef struct AstraConfigError {
    uint32_t line; /**< One-based malformed line, or zero. */
    uint32_t version; /**< Unsupported schema version, or zero. */
} AstraConfigError;

/** Open the caller's configuration through its private CONFIG capability. @param startup Valid startup record. @param schema_version Caller schema version. @param flags ASTRA_CONFIG_OPEN_* flags. @param config Receives the handle. @param error Optional parse error. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_open(const AstraStartupInfo *startup,
                           uint32_t schema_version, uint32_t flags,
                           AstraConfig *config, AstraConfigError *error);
/** Close an open configuration. @param config Configuration to close. */
void astra_config_close(AstraConfig *config);
/** Reload committed values. @param config Open configuration. @param error Optional parse error. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_reload(AstraConfig *config, AstraConfigError *error);
/** Count values under a key. @param config Open configuration. @param key UTF-8 key. @param count Receives count. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_count(const AstraConfig *config, const char *key,
                            uint32_t *count);
/** Read a string. @param config Open configuration. @param key Key. @param index Value index. @param value Receives text. @param capacity Buffer bytes. @param length Receives required bytes. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_get_string(const AstraConfig *config, const char *key,
                                 uint32_t index, char *value,
                                 uint32_t capacity, uint32_t *length);
/** Read a signed integer. @param config Open configuration. @param key Key. @param index Value index. @param value Receives value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_get_i64(const AstraConfig *config, const char *key,
                              uint32_t index, int64_t *value);
/** Read an unsigned integer. @param config Open configuration. @param key Key. @param index Value index. @param value Receives value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_get_u64(const AstraConfig *config, const char *key,
                              uint32_t index, uint64_t *value);
/** Read a Boolean. @param config Open configuration. @param key Key. @param index Value index. @param value Receives zero or one. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_get_bool(const AstraConfig *config, const char *key,
                               uint32_t index, int *value);
/** Set a string. @param config Open writable configuration. @param key Key. @param index Value index. @param value UTF-8 value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_set_string(AstraConfig *config, const char *key,
                                 uint32_t index, const char *value);
/** Set a signed integer. @param config Open writable configuration. @param key Key. @param index Value index. @param value Value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_set_i64(AstraConfig *config, const char *key,
                              uint32_t index, int64_t value);
/** Set an unsigned integer. @param config Open writable configuration. @param key Key. @param index Value index. @param value Value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_set_u64(AstraConfig *config, const char *key,
                              uint32_t index, uint64_t value);
/** Set a Boolean. @param config Open writable configuration. @param key Key. @param index Value index. @param value Boolean. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_set_bool(AstraConfig *config, const char *key,
                               uint32_t index, int value);
/** Append a string. @param config Open writable configuration. @param key Key. @param value UTF-8 value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_append_string(AstraConfig *config, const char *key,
                                    const char *value);
/** Append a signed integer. @param config Open writable configuration. @param key Key. @param value Value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_append_i64(AstraConfig *config, const char *key,
                                 int64_t value);
/** Append an unsigned integer. @param config Open writable configuration. @param key Key. @param value Value. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_append_u64(AstraConfig *config, const char *key,
                                 uint64_t value);
/** Append a Boolean. @param config Open writable configuration. @param key Key. @param value Boolean. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_append_bool(AstraConfig *config, const char *key,
                                  int value);
/** Remove one value. @param config Open writable configuration. @param key Key. @param index Value index. @return ASTRA_CONFIG_* status. */
uint32_t astra_config_remove(AstraConfig *config, const char *key,
                             uint32_t index);

#endif
