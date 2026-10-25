#ifndef ASTRA_FOLDER_OPS_H
#define ASTRA_FOLDER_OPS_H

#include <astra/filesystem_library.h>

int astra_folder_name_valid(const char *name);
const char *astra_folder_leaf(const char *path);
uint32_t astra_folder_path(const char *parent, const char *name,
    char *path, uint32_t capacity);
uint32_t astra_folder_create_numbered(AstraFilesystem *filesystem,
    const char *parent, const char *prefix, char *path, uint32_t capacity);
uint32_t astra_folder_rename_safe(AstraFilesystem *filesystem,
    const char *source, const char *parent, const char *name,
    char *path, uint32_t capacity);
uint32_t astra_folder_move_to_trash(AstraFilesystem *filesystem,
    const char *source, const char *trash, char *path, uint32_t capacity);

#endif
