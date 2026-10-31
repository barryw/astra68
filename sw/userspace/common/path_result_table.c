#include "path_result_table.h"
#include "folder_ops.h"

#include <astra/runtime.h>

#include <string.h>

void astra_path_result_table_dispose(AstraPathResultRows *storage)
{
    if (storage == NULL) return;
    astra_runtime_deallocate(storage->rows);
    astra_runtime_deallocate(storage->cells);
    *storage = (AstraPathResultRows)ASTRA_PATH_RESULT_ROWS_INIT;
}

AstraResult astra_path_result_table_set(AstraUIContext *context,
    AstraControl *table, AstraPathResultRows *storage,
    const void *source, uint32_t count, AstraPathResultAt path_at)
{
    if (context == NULL || table == NULL || storage == NULL ||
        (count != 0u && (source == NULL || path_at == NULL)) ||
        count > UINT32_MAX / sizeof(AstraCollectionItem) ||
        count > UINT32_MAX / sizeof(AstraTableCell))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraCollectionItem *rows = NULL;
    AstraTableCell *cells = NULL;
    if (count != 0u) {
        rows = astra_runtime_allocate((size_t)count * sizeof(*rows));
        cells = astra_runtime_allocate((size_t)count * sizeof(*cells));
        if (rows == NULL || cells == NULL) {
            astra_runtime_deallocate(rows);
            astra_runtime_deallocate(cells);
            return ASTRA_ERROR_NO_RESOURCES;
        }
        for (uint32_t at = 0u; at < count; ++at) {
            const char *path = path_at(source, at);
            if (path == NULL || path[0] != '/') {
                astra_runtime_deallocate(rows);
                astra_runtime_deallocate(cells);
                return ASTRA_ERROR_INVALID_ARGUMENT;
            }
            const char *leaf = astra_folder_leaf(path);
            if (leaf == NULL) leaf = path;
            rows[at] = (AstraCollectionItem){
                (uint64_t)at + 1u, leaf, (uint32_t)strlen(leaf), {0u, 0u}};
            cells[at] = (AstraTableCell){path,
                (uint32_t)strlen(path), 0u};
        }
    }
    AstraResult result = astra_interface_control_set_value(context,
        table, -1);
    if (result == ASTRA_OK)
        result = astra_interface_table_set_rows(context, table,
            rows, count, cells, count);
    if (result != ASTRA_OK) {
        astra_runtime_deallocate(rows);
        astra_runtime_deallocate(cells);
        return result;
    }
    astra_path_result_table_dispose(storage);
    storage->rows = rows;
    storage->cells = cells;
    return ASTRA_OK;
}
