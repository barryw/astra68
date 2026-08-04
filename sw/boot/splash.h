#ifndef ASTRA_BOOT_SPLASH_H
#define ASTRA_BOOT_SPLASH_H

#include <stdint.h>

#define ASTRA_BOOT_SPLASH_ADDRESS       0x03e40000u
#define ASTRA_BOOT_SPLASH_RESERVED_SIZE 0x000b0000u

typedef enum {
    ASTRA_SPLASH_STATUS_POST = 0,
    ASTRA_SPLASH_STATUS_GRAPHICS,
    ASTRA_SPLASH_STATUS_MEMORY,
    ASTRA_SPLASH_STATUS_KERNEL,
    ASTRA_SPLASH_STATUS_COUNT
} AstraSplashStatus;

typedef enum {
    ASTRA_SPLASH_ERROR_NONE = 0,
    ASTRA_SPLASH_ERROR_UNSUPPORTED,
    ASTRA_SPLASH_ERROR_DECODE,
    ASTRA_SPLASH_ERROR_FRAME_COPY,
    ASTRA_SPLASH_ERROR_INITIAL_GLYPHS,
    ASTRA_SPLASH_ERROR_INITIAL_PRESENT,
    ASTRA_SPLASH_ERROR_STATUS_GLYPHS,
    ASTRA_SPLASH_ERROR_STATUS_PRESENT,
    ASTRA_SPLASH_ERROR_STATUS_MIRROR,
    ASTRA_SPLASH_ERROR_STOP_PRESENT
} AstraSplashError;

int astra_boot_splash_start(void);
int astra_boot_splash_mark_ok(AstraSplashStatus status);
int astra_boot_splash_mark_fail(AstraSplashStatus status);
int astra_boot_splash_stop(void);
int astra_boot_splash_active(void);
AstraSplashError astra_boot_splash_error(void);
const char *astra_boot_splash_error_text(void);

#endif
