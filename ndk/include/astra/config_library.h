#ifndef ASTRA_CONFIG_LIBRARY_H
#define ASTRA_CONFIG_LIBRARY_H

#include <stdint.h>

#define ASTRA_CONFIG_LIBRARY_NAME "config.library"
#define ASTRA_CONFIG_LIBRARY_VERSION 1u
#define ASTRA_CONFIG_LIBRARY_ABI_MAJOR 1u
#define ASTRA_CONFIG_LIBRARY_ABI_MINOR 0u
#define ASTRA_CONFIG_CAPABILITY "CONFIG"

#define ASTRA_CONFIG_OPEN_READ  (1u << 0)
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

typedef struct AstraConfig {
    uint32_t _private_area;
    void *_private_state;
} AstraConfig;

#define ASTRA_CONFIG_INIT { 0, 0 }

typedef struct AstraConfigError {
    uint32_t line;
    uint32_t version;
} AstraConfigError;

/*
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
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;

    uint32_t (*open)(const AstraStartupInfo *, uint32_t schema_version,
                     uint32_t flags, AstraConfig *, AstraConfigError *);
    void (*close)(AstraConfig *);
    uint32_t (*reload)(AstraConfig *, AstraConfigError *);
    uint32_t (*count)(const AstraConfig *, const char *, uint32_t *);
    uint32_t (*get_string)(const AstraConfig *, const char *, uint32_t,
                           char *, uint32_t, uint32_t *);
    uint32_t (*get_i64)(const AstraConfig *, const char *, uint32_t,
                        int64_t *);
    uint32_t (*get_u64)(const AstraConfig *, const char *, uint32_t,
                        uint64_t *);
    uint32_t (*get_bool)(const AstraConfig *, const char *, uint32_t, int *);
    uint32_t (*set_string)(AstraConfig *, const char *, uint32_t,
                           const char *);
    uint32_t (*set_i64)(AstraConfig *, const char *, uint32_t, int64_t);
    uint32_t (*set_u64)(AstraConfig *, const char *, uint32_t, uint64_t);
    uint32_t (*set_bool)(AstraConfig *, const char *, uint32_t, int);
    uint32_t (*append_string)(AstraConfig *, const char *, const char *);
    uint32_t (*append_i64)(AstraConfig *, const char *, int64_t);
    uint32_t (*append_u64)(AstraConfig *, const char *, uint64_t);
    uint32_t (*append_bool)(AstraConfig *, const char *, int);
    uint32_t (*remove)(AstraConfig *, const char *, uint32_t);
} AstraConfigLibraryV1;

#endif
