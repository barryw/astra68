#include "../../../common/path_result_table.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t allocations, frees, fail_table, selection_calls;
static const AstraCollectionItem *bound_rows;
static const AstraTableCell *bound_cells;
static uint32_t bound_count;

void *astra_runtime_allocate(size_t bytes)
{
    void *memory = malloc(bytes);
    if (memory != NULL) ++allocations;
    return memory;
}

void astra_runtime_deallocate(void *memory)
{
    if (memory != NULL) ++frees;
    free(memory);
}

const char *astra_folder_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash != NULL && slash[1] != '\0' ? slash + 1u : NULL;
}

AstraResult astra_interface_control_set_value(AstraUIContext *context,
    AstraControl *control, int64_t value)
{
    assert(context != NULL && control != NULL && value == -1);
    ++selection_calls;
    return ASTRA_OK;
}

AstraResult astra_interface_table_set_rows(AstraUIContext *context,
    AstraControl *control, const AstraCollectionItem *rows,
    uint32_t row_count, const AstraTableCell *cells, uint32_t cell_count)
{
    assert(context != NULL && control != NULL && row_count == cell_count);
    if (fail_table) return ASTRA_ERROR_INVALID_ARGUMENT;
    bound_rows = rows;
    bound_cells = cells;
    bound_count = row_count;
    return ASTRA_OK;
}

static const char *path_at(const void *source, uint32_t index)
{
    return ((const char *const *)source)[index];
}

int main(void)
{
    AstraUIContext context = ASTRA_UI_CONTEXT_INIT;
    AstraControl table = {0};
    AstraPathResultRows storage = ASTRA_PATH_RESULT_ROWS_INIT;
    const char *first[] = {"/dh0/fonts", "/home/notes.txt"};
    const char *second[] = {"/ram0/second"};
    assert(astra_path_result_table_set(&context, &table, &storage,
        first, 2u, path_at) == ASTRA_OK);
    assert(bound_count == 2u && selection_calls == 1u);
    assert(strcmp(bound_rows[0].text, "fonts") == 0);
    assert(strcmp(bound_cells[1].text, "/home/notes.txt") == 0);
    assert(storage.rows == bound_rows && storage.cells == bound_cells);
    fail_table = 1u;
    assert(astra_path_result_table_set(&context, &table, &storage,
        second, 1u, path_at) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(storage.rows == bound_rows && storage.cells == bound_cells);
    fail_table = 0u;
    assert(astra_path_result_table_set(&context, &table, &storage,
        second, 1u, path_at) == ASTRA_OK);
    assert(bound_count == 1u && strcmp(bound_rows[0].text, "second") == 0);
    astra_path_result_table_dispose(&storage);
    assert(storage.rows == NULL && storage.cells == NULL);
    assert(allocations == frees);
    puts("ATLAS PATH RESULT TABLE PASS");
    return 0;
}
