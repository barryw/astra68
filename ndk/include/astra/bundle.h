#ifndef ASTRA_BUNDLE_H
#define ASTRA_BUNDLE_H

/** @file bundle.h @brief Installed application and Kit bundle metadata. */

#include <stdint.h>

#include <astra/process.h>

/** Supported manifest format version. */
#define ASTRA_BUNDLE_MANIFEST_VERSION 1u
/** Maximum manifest file bytes. */
#define ASTRA_BUNDLE_MANIFEST_MAX 4096u
/** Maximum bundle identifier bytes including NUL. */
#define ASTRA_BUNDLE_ID_MAX 64u
/** Maximum display-name bytes including NUL. */
#define ASTRA_BUNDLE_NAME_MAX 64u
/** Maximum bundle-relative path bytes including NUL. */
#define ASTRA_BUNDLE_PATH_MAX 160u
/** Maximum required or provided libraries. */
#define ASTRA_BUNDLE_LIBRARY_MAX 8u
/* An application cannot use more manifest capabilities than can fit in the
 * launch namespace; keep the parser and launch ABI on the same authority. */
/** Maximum requested launch capabilities. */
#define ASTRA_BUNDLE_CAPABILITY_MAX ASTRA_LAUNCH_GRANT_MAX
/** Maximum logical library-name bytes including NUL. */
#define ASTRA_BUNDLE_LIBRARY_NAME_MAX 48u

enum {
    ASTRA_BUNDLE_APPLICATION = 1,
    ASTRA_BUNDLE_KIT = 2
};

/** Semantic version stored in a bundle manifest. */
typedef struct AstraBundleVersion {
    uint16_t major; /**< Major version. */
    uint16_t minor; /**< Minor version. */
    uint16_t patch; /**< Patch version. */
} AstraBundleVersion;

/** One required or provided shared library. */
typedef struct AstraBundleLibrary {
    char name[ASTRA_BUNDLE_LIBRARY_NAME_MAX]; /**< Logical library name. */
    uint16_t abi; /**< Required export-table ABI major. */
    AstraBundleVersion version; /**< Required/provided semantic version. */
} AstraBundleLibrary;

/** Parsed bundle manifest. */
typedef struct AstraBundleManifest {
    uint16_t format_version; /**< ASTRA_BUNDLE_MANIFEST_VERSION. */
    uint16_t kind; /**< ASTRA_BUNDLE_APPLICATION or ASTRA_BUNDLE_KIT. */
    AstraBundleVersion version; /**< Bundle semantic version. */
    char id[ASTRA_BUNDLE_ID_MAX]; /**< Stable reverse-domain identifier. */
    char name[ASTRA_BUNDLE_NAME_MAX]; /**< Human-readable UTF-8 name. */
    char executable[ASTRA_BUNDLE_PATH_MAX]; /**< Bundle-relative executable. */
    char icon[ASTRA_BUNDLE_PATH_MAX]; /**< Bundle-relative AICON path. */
    AstraBundleLibrary requirements[ASTRA_BUNDLE_LIBRARY_MAX]; /**< Required libraries. */
    AstraBundleLibrary provides[ASTRA_BUNDLE_LIBRARY_MAX]; /**< Provided libraries. */
    char capabilities[ASTRA_BUNDLE_CAPABILITY_MAX][ASTRA_BUNDLE_NAME_MAX]; /**< Requested launch capabilities. */
    uint16_t require_count; /**< Used entries in requirements. */
    uint16_t provide_count; /**< Used entries in provides. */
    uint16_t capability_count; /**< Used entries in capabilities. */
} AstraBundleManifest;

enum {
    ASTRA_BUNDLE_OK = 0,
    ASTRA_BUNDLE_INVALID = 1,
    ASTRA_BUNDLE_LIMIT = 2,
    ASTRA_BUNDLE_MISSING = 3
};

/** Parse a mutable UTF-8 bundle manifest.
 * @param text Manifest bytes; may be modified while parsing.
 * @param length Byte length of @p text.
 * @param manifest Receives the parsed manifest.
 * @param error_line Receives the one-based malformed line, or zero.
 * @return ASTRA_BUNDLE_OK or an ASTRA_BUNDLE_* error.
 */
uint32_t astra_bundle_manifest_parse(char *text, uint32_t length,
                                     AstraBundleManifest *manifest,
                                     uint32_t *error_line);

/** Big-endian `AICO` file magic. */
#define ASTRA_AICON_MAGIC UINT32_C(0x4149434f) /* AICO */
/** Supported AICON file version. */
#define ASTRA_AICON_VERSION 1u
/** Serialized AICON header bytes. */
#define ASTRA_AICON_HEADER_SIZE 32u
/** Serialized AICON strike-record bytes. */
#define ASTRA_AICON_STRIKE_SIZE 16u
/** Required standard strike count. */
#define ASTRA_AICON_REQUIRED_STRIKES 3u

/** Validated zero-copy view of an AICON file. */
typedef struct AstraAicon {
    const uint8_t *bytes; /**< Borrowed complete file bytes. */
    uint32_t length; /**< Complete file length. */
    uint16_t palette_count; /**< Number of RGBA palette entries. */
    uint16_t strike_count; /**< Number of raster strikes. */
    uint32_t palette_offset; /**< Palette byte offset. */
    uint32_t strike_offset; /**< Strike-table byte offset. */
    uint32_t data_offset; /**< Pixel-data byte offset. */
} AstraAicon;

/** One selected AICON raster strike. */
typedef struct AstraAiconStrike {
    uint16_t width; /**< Width in pixels. */
    uint16_t height; /**< Height in pixels. */
    const uint8_t *pixels; /**< Borrowed palette-index bytes. */
    uint32_t length; /**< Bytes in @p pixels. */
} AstraAiconStrike;

/** Validate and adopt an AICON file. @param bytes Complete file bytes. @param length File length. @param icon Receives the view. @return ASTRA_BUNDLE_OK or an error. */
uint32_t astra_aicon_open(const void *bytes, uint32_t length,
                          AstraAicon *icon);
/** Select the best exact-size strike. @param icon Validated icon. @param size Requested square extent. @param strike Receives the strike. @return ASTRA_BUNDLE_OK or an error. */
uint32_t astra_aicon_strike(const AstraAicon *icon, uint16_t size,
                            AstraAiconStrike *strike);
/** Read one palette entry. @param icon Validated icon. @param index Palette index. @param rgba Receives red, green, blue, alpha. @return ASTRA_BUNDLE_OK or an error. */
uint32_t astra_aicon_palette(const AstraAicon *icon, uint16_t index,
                             uint8_t rgba[4]);

#endif
