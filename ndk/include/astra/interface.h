#ifndef ASTRA_INTERFACE_H
#define ASTRA_INTERFACE_H

/** @file interface.h @brief Shared, themed application-interface primitives. */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

enum {
    ASTRA_ALERT_INFORMATION = 1,
    ASTRA_ALERT_WARNING = 2,
    ASTRA_ALERT_ERROR = 3
};

typedef struct AstraAlertInfo {
    uint32_t size;
    uint32_t kind;
    const char *title;
    uint16_t title_length;
    uint16_t reserved16;
    const char *message;
    uint16_t message_length;
    uint16_t reserved16_2;
    const char *button;
    uint16_t button_length;
    uint16_t reserved16_3;
    uint32_t reserved[4];
} AstraAlertInfo;

#define ASTRA_ALERT_INFO_INIT { \
    sizeof(AstraAlertInfo), ASTRA_ALERT_INFORMATION, 0, 0, 0, 0, 0, 0, \
    0, 0, 0, { 0, 0, 0, 0 } \
}

ASTRA_EXTERN_C_END

#endif
