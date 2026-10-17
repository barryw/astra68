/** @file font_encoding.h @brief Character encoding choice for native font import. */
#ifndef ASTRA_FONT_ENCODING_H
#define ASTRA_FONT_ENCODING_H

/** Outline and AFNT sources carry Unicode. Amiga disk fonts need a choice. */
enum {
    /** Detect Unicode sources; invalid for Amiga disk fonts. */
    ASTRA_FONT_ENCODING_AUTO = 0u,
    /** Map Amiga character slots through ISO-8859-1. */
    ASTRA_FONT_ENCODING_ISO_8859_1 = 1u,
    /** Map Amiga character slots through ISO-8859-2. */
    ASTRA_FONT_ENCODING_ISO_8859_2 = 2u
};

#endif
