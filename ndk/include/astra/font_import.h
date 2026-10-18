/** @file font_import.h @brief Native font import lifecycle states. */
#ifndef ASTRA_FONT_IMPORT_H
#define ASTRA_FONT_IMPORT_H

enum {
    ASTRA_FONT_IMPORT_RUNNING = 1u, /**< Conversion or staging is in progress. */
    ASTRA_FONT_IMPORT_CANCELLING, /**< Awaiting worker retirement and rollback. */
    ASTRA_FONT_IMPORT_SUCCEEDED, /**< Complete inventory was published. */
    ASTRA_FONT_IMPORT_FAILED, /**< Failure and cleanup have completed. */
    ASTRA_FONT_IMPORT_CANCELLED /**< Worker and unpublished objects are gone. */
};

#endif
