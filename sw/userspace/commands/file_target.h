#ifndef ASTRA_COMMAND_FILE_TARGET_H
#define ASTRA_COMMAND_FILE_TARGET_H

#include <astra/filesystem_library.h>

uint32_t astra_command_file_target(AstraFilesystem *filesystem,
    const char *source, const char *destination, char *joined,
    const char **target);

#endif
