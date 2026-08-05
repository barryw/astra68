#ifndef ASTRA_MBR_H
#define ASTRA_MBR_H

#include <stdint.h>

#include <astra/block_device.h>

/*
 * Read-only master boot record reader.
 *
 * Astra's boot chain is frozen around the MBR: `sw/stage0` lives in FPGA BRAM,
 * so its partition scan cannot change without a bitstream rebuild and a timing
 * closure re-qualification. It looks for a FAT partition in one of the four
 * primary slots and nothing else. Every later layout decision inherits that —
 * a GPT disk presents a protective entry of type 0xEE that stage0 rejects, so
 * the card would not boot.
 *
 * This is deliberately not lwext4's `ext4_mbr_scan`. That one hands back four
 * `ext4_blockdev` structures sharing the parent's interface, which bypasses the
 * port's accounting, transfer splitting and lock; and its companion
 * `ext4_mbr_write` divides the disk by percentage across all four slots, which
 * would take the FAT partition with it. Partition table parsing also does not
 * belong inside a filesystem library when a volume tool needs it too.
 *
 * There is no writer here. Writing a partition table is the one operation that
 * can destroy the boot volume, and it does not belong in the library that
 * mounts filesystems.
 */

#define ASTRA_MBR_ENTRY_COUNT 4u
#define ASTRA_MBR_SECTOR_BYTES 512u

typedef enum AstraMbrKind {
    ASTRA_MBR_EMPTY = 0,
    ASTRA_MBR_FAT = 1,
    ASTRA_MBR_LINUX = 2,
    ASTRA_MBR_EXTENDED = 3,
    ASTRA_MBR_GPT_PROTECTIVE = 4,
    ASTRA_MBR_OTHER = 5
} AstraMbrKind;

typedef struct AstraMbrEntry {
    uint32_t first_sector;
    uint32_t sector_count;
    uint8_t type;
    uint8_t bootable;
    AstraMbrKind kind;
} AstraMbrEntry;

typedef struct AstraMbrTable {
    AstraMbrEntry entry[ASTRA_MBR_ENTRY_COUNT];
    uint32_t used; /* entries with a non-zero sector count */
} AstraMbrTable;

/*
 * Reads and parses sector 0. `sector_buffer` must hold one device sector.
 *
 * Returns ASTRA_BLOCK_CORRUPT when the signature is absent or an entry runs
 * past the end of the device — an entry that does not fit is not a partition
 * this code will hand to anything, whatever the table claims.
 */
AstraBlockStatus astra_mbr_read(AstraBlockDevice *device, void *sector_buffer,
                                uint32_t sector_buffer_bytes,
                                AstraMbrTable *table, uint64_t deadline);

/* Classifies a partition type byte. The FAT set matches sw/stage0 exactly. */
AstraMbrKind astra_mbr_classify(uint8_t type);

/* The first entry of the given kind, or NULL. */
const AstraMbrEntry *astra_mbr_find(const AstraMbrTable *table,
                                    AstraMbrKind kind);

/*
 * True when [first_sector, first_sector + sector_count) touches any occupied
 * entry other than `index`. Sector 0 counts as occupied: it holds the table.
 *
 * This is the primitive any tool that allocates volumes must consult before
 * writing. The boot volume and an Astra volume live on the same card, and the
 * failure mode of getting it wrong is an unbootable machine with the user's
 * files gone. Pass ASTRA_MBR_ENTRY_COUNT for `index` to check a range that
 * owns no entry yet.
 */
int astra_mbr_range_conflicts(const AstraMbrTable *table, uint32_t index,
                              uint32_t first_sector, uint32_t sector_count);

#endif
