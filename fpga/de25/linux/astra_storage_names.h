#ifndef ASTRA_STORAGE_NAMES_H
#define ASTRA_STORAGE_NAMES_H

#include "astra_storage_catalog.h"

#define ASTRA_STORAGE_NAME_KEY_BYTES 192u

typedef struct AstraStorageNameRow {
    uint32_t slot;
    char key[ASTRA_STORAGE_NAME_KEY_BYTES];
} AstraStorageNameRow;

typedef struct AstraStorageNames {
    AstraStorageNameRow *rows;
    uint32_t count;
    int directory;
    int error;
    int primary_valid;
} AstraStorageNames;

#define ASTRA_STORAGE_NAMES_INIT { .directory = -1 }

/* State directory belongs to the trusted root launcher. Corrupt maps fence
 * USB admission while the system volume and other classes remain available. */
int astra_storage_names_open(AstraStorageNames *names, const char *state_directory);
void astra_storage_names_close(AstraStorageNames *names);
/* A durable key keeps its slot across provider epochs; no key gets a temporary
 * slot outside all persisted reservations. */
int astra_storage_names_select(AstraStorageNames *names, const char *key,
    const AstraStorageCatalog *catalog, uint32_t *slot);

#endif
