#ifndef ASTRA_PATH_RESULT_TABLE_H
#define ASTRA_PATH_RESULT_TABLE_H

#include <astra/interface_library.h>

/** Borrowed path lookup for one source snapshot. Paths must outlive rows. */
typedef const char *(*AstraPathResultAt)(const void *source, uint32_t index);

/** Adapter storage for existing shared Table and Collection controls. */
typedef struct AstraPathResultRows {
    AstraCollectionItem *rows;
    AstraTableCell *cells;
} AstraPathResultRows;

#define ASTRA_PATH_RESULT_ROWS_INIT {NULL, NULL}

/** Replace Name/Location rows. The caller retains source paths until next
 * replacement. Selection is cleared before replacing rows. */
AstraResult astra_path_result_table_set(AstraUIContext *context,
    AstraControl *table, AstraPathResultRows *storage,
    const void *source, uint32_t count, AstraPathResultAt path_at);

void astra_path_result_table_dispose(AstraPathResultRows *storage);

#endif
