#ifndef ASTRA_STORY_H
#define ASTRA_STORY_H

/** @file story.h @brief Inspect Z-machine, Glulx, and Blorb story files. */

#include <astra/filesystem_library.h>

enum {
    ASTRA_STORY_VM_NONE = 0u,
    ASTRA_STORY_VM_ZMACHINE = 1u,
    ASTRA_STORY_VM_GLULX = 2u
};

enum {
    ASTRA_STORY_CONTAINER_NONE = 0u,
    ASTRA_STORY_CONTAINER_BLORB = 1u
};

/** Executable payload inside a bare story or Blorb. An empty VM with a Blorb
 * container means a resource-only or unsupported-format Blorb. */
typedef struct AstraStoryInfo {
    uint32_t size;                 /**< sizeof(AstraStoryInfo). */
    uint32_t vm;                   /**< ASTRA_STORY_VM_*. */
    uint32_t container;            /**< ASTRA_STORY_CONTAINER_*. */
    uint32_t z_version;            /**< Z-machine header version 1–8, else 0. */
    uint32_t glulx_version;        /**< Full 32-bit Glulx header version. */
    uint64_t executable_offset;    /**< Byte offset of executable payload. */
    uint64_t executable_size;      /**< Payload bytes, zero if no VM. */
} AstraStoryInfo;

/** Initialize a story inspection record before calling astra_story_inspect(). */
#define ASTRA_STORY_INFO_INIT { sizeof(AstraStoryInfo), 0u, 0u, 0u, 0u, 0u, 0u }

/** Inspect file content with bounded reads. Filename never chooses a VM.
 * No metadata is written and no file offset is changed. Unknown data returns
 * ASTRA_VFS_OK with VM and container set to NONE. Caller must check VM before
 * executing; this probe does not validate every instruction in a story.
 * @param filesystem Open Filesystem Kit client.
 * @param path Absolute path visible to that client.
 * @param info Initialized record receiving format and payload bounds.
 * @return ASTRA_VFS_OK or a VFS status for access/transport failure. */
uint32_t astra_story_inspect(AstraFilesystem *filesystem, const char *path,
                             AstraStoryInfo *info);

#endif
