#ifndef ASTRA_CONFIG_LIBRARY_H
#define ASTRA_CONFIG_LIBRARY_H

/** @file config_library.h @brief Typed, location-independent configuration. */

#include <stdint.h>

/** Logical name of config.library. */
#define ASTRA_CONFIG_LIBRARY_NAME "config.library"
/** Minimum compatible config.library major version. */
#define ASTRA_CONFIG_LIBRARY_VERSION 1u
/** Config Kit export-table ABI major version. */
#define ASTRA_CONFIG_LIBRARY_ABI_MAJOR 1u
/** Config Kit export-table ABI minor version. */
#define ASTRA_CONFIG_LIBRARY_ABI_MINOR 0u
/** Launch capability carrying the caller's private configuration root. */
#define ASTRA_CONFIG_CAPABILITY "CONFIG"

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

/** Actionable configuration parse or schema error. */
typedef struct AstraConfigError {
    uint32_t line; /**< One-based malformed line, or zero. */
    uint32_t version; /**< Unsupported schema version, or zero. */
} AstraConfigError;

/**
 * A key owns an ordered sequence of typed values.  A scalar is the common
 * one-value case and uses index zero; repeating a key creates a list without
 * introducing a second data model.  count() reports zero for a missing key.
 * get_string() returns the value length even when the supplied buffer is too
 * small, so callers can allocate exactly what the stored value requires.
 *
 * Programs never name a file or parse a format.  open() resolves the caller's
 * private CONFIG capability and schema through the library.
 */
typedef struct AstraConfigLibraryV1 {
    uint16_t abi_major; /**< ASTRA_CONFIG_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_CONFIG_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */

    /** Open the calling program's configuration. */
    uint32_t (*open)(const AstraStartupInfo *, uint32_t schema_version,
                     uint32_t flags, AstraConfig *, AstraConfigError *);
    /** Close an open configuration. */
    void (*close)(AstraConfig *);
    /** Reload committed values from storage. */
    uint32_t (*reload)(AstraConfig *, AstraConfigError *);
    /** Count values stored under one key. */
    uint32_t (*count)(const AstraConfig *, const char *, uint32_t *);
    /** Read a string value by key and index. */
    uint32_t (*get_string)(const AstraConfig *, const char *, uint32_t,
                           char *, uint32_t, uint32_t *);
    /** Read a signed integer value by key and index. */
    uint32_t (*get_i64)(const AstraConfig *, const char *, uint32_t,
                        int64_t *);
    /** Read an unsigned integer value by key and index. */
    uint32_t (*get_u64)(const AstraConfig *, const char *, uint32_t,
                        uint64_t *);
    /** Read a Boolean value by key and index. */
    uint32_t (*get_bool)(const AstraConfig *, const char *, uint32_t, int *);
    /** Replace or create a string value. */
    uint32_t (*set_string)(AstraConfig *, const char *, uint32_t,
                           const char *);
    /** Replace or create a signed integer value. */
    uint32_t (*set_i64)(AstraConfig *, const char *, uint32_t, int64_t);
    /** Replace or create an unsigned integer value. */
    uint32_t (*set_u64)(AstraConfig *, const char *, uint32_t, uint64_t);
    /** Replace or create a Boolean value. */
    uint32_t (*set_bool)(AstraConfig *, const char *, uint32_t, int);
    /** Append a string to a key's value list. */
    uint32_t (*append_string)(AstraConfig *, const char *, const char *);
    /** Append a signed integer to a key's value list. */
    uint32_t (*append_i64)(AstraConfig *, const char *, int64_t);
    /** Append an unsigned integer to a key's value list. */
    uint32_t (*append_u64)(AstraConfig *, const char *, uint64_t);
    /** Append a Boolean to a key's value list. */
    uint32_t (*append_bool)(AstraConfig *, const char *, int);
    /** Remove one indexed value. */
    uint32_t (*remove)(AstraConfig *, const char *, uint32_t);
} AstraConfigLibraryV1;

#endif
