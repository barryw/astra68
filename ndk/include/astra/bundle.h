#ifndef ASTRA_BUNDLE_H
#define ASTRA_BUNDLE_H

#include <stdint.h>

#include <astra/process.h>

#define ASTRA_BUNDLE_MANIFEST_VERSION 1u
#define ASTRA_BUNDLE_MANIFEST_MAX 4096u
#define ASTRA_BUNDLE_ID_MAX 64u
#define ASTRA_BUNDLE_NAME_MAX 64u
#define ASTRA_BUNDLE_PATH_MAX 160u
#define ASTRA_BUNDLE_LIBRARY_MAX 8u
/* An application cannot use more manifest capabilities than can fit in the
 * launch namespace; keep the parser and launch ABI on the same authority. */
#define ASTRA_BUNDLE_CAPABILITY_MAX ASTRA_LAUNCH_GRANT_MAX
#define ASTRA_BUNDLE_LIBRARY_NAME_MAX 48u

enum {
    ASTRA_BUNDLE_APPLICATION = 1,
    ASTRA_BUNDLE_KIT = 2
};

typedef struct AstraBundleVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} AstraBundleVersion;

typedef struct AstraBundleLibrary {
    char name[ASTRA_BUNDLE_LIBRARY_NAME_MAX];
    uint16_t abi;
    AstraBundleVersion version;
} AstraBundleLibrary;

typedef struct AstraBundleManifest {
    uint16_t format_version;
    uint16_t kind;
    AstraBundleVersion version;
    char id[ASTRA_BUNDLE_ID_MAX];
    char name[ASTRA_BUNDLE_NAME_MAX];
    char executable[ASTRA_BUNDLE_PATH_MAX];
    char icon[ASTRA_BUNDLE_PATH_MAX];
    AstraBundleLibrary requires[ASTRA_BUNDLE_LIBRARY_MAX];
    AstraBundleLibrary provides[ASTRA_BUNDLE_LIBRARY_MAX];
    char capabilities[ASTRA_BUNDLE_CAPABILITY_MAX][ASTRA_BUNDLE_NAME_MAX];
    uint16_t require_count;
    uint16_t provide_count;
    uint16_t capability_count;
} AstraBundleManifest;

enum {
    ASTRA_BUNDLE_OK = 0,
    ASTRA_BUNDLE_INVALID = 1,
    ASTRA_BUNDLE_LIMIT = 2,
    ASTRA_BUNDLE_MISSING = 3
};

uint32_t astra_bundle_manifest_parse(char *text, uint32_t length,
                                     AstraBundleManifest *manifest,
                                     uint32_t *error_line);

#define ASTRA_AICON_MAGIC UINT32_C(0x4149434f) /* AICO */
#define ASTRA_AICON_VERSION 1u
#define ASTRA_AICON_HEADER_SIZE 32u
#define ASTRA_AICON_STRIKE_SIZE 16u
#define ASTRA_AICON_REQUIRED_STRIKES 3u

typedef struct AstraAicon {
    const uint8_t *bytes;
    uint32_t length;
    uint16_t palette_count;
    uint16_t strike_count;
    uint32_t palette_offset;
    uint32_t strike_offset;
    uint32_t data_offset;
} AstraAicon;

typedef struct AstraAiconStrike {
    uint16_t width;
    uint16_t height;
    const uint8_t *pixels;
    uint32_t length;
} AstraAiconStrike;

uint32_t astra_aicon_open(const void *bytes, uint32_t length,
                          AstraAicon *icon);
uint32_t astra_aicon_strike(const AstraAicon *icon, uint16_t size,
                            AstraAiconStrike *strike);
uint32_t astra_aicon_palette(const AstraAicon *icon, uint16_t index,
                             uint8_t rgba[4]);

#endif
