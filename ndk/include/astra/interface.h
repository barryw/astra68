#ifndef ASTRA_INTERFACE_H
#define ASTRA_INTERFACE_H

/** @file interface.h @brief Shared, themed application-interface primitives. */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Alert severity. */
enum {
    ASTRA_ALERT_INFORMATION = 1,
    ASTRA_ALERT_WARNING = 2,
    ASTRA_ALERT_ERROR = 3
};

/** Content for one modal system alert. */
typedef struct AstraAlertInfo {
    /** Structure size for source-compatible extension. */
    uint32_t size;
    /** ASTRA_ALERT_* severity. */
    uint32_t kind;
    /** Counted validated UTF-8 title. */
    const char *title;
    /** Bytes at @ref title, excluding any terminator. */
    uint16_t title_length;
    /** Must be zero. */
    uint16_t reserved16;
    /** Counted validated UTF-8 message. */
    const char *message;
    /** Bytes at @ref message, excluding any terminator. */
    uint16_t message_length;
    /** Must be zero. */
    uint16_t reserved16_2;
    /** Counted validated UTF-8 button label. */
    const char *button;
    /** Bytes at @ref button, excluding any terminator. */
    uint16_t button_length;
    /** Must be zero. */
    uint16_t reserved16_3;
    /** Must be zero. */
    uint32_t reserved[4];
} AstraAlertInfo;

/** Empty information-alert descriptor. */
#define ASTRA_ALERT_INFO_INIT { \
    sizeof(AstraAlertInfo), ASTRA_ALERT_INFORMATION, 0, 0, 0, 0, 0, 0, \
    0, 0, 0, { 0, 0, 0, 0 } \
}

ASTRA_EXTERN_C_END

#endif
