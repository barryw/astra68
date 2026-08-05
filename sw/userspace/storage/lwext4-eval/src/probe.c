/*
 * Astra 68 lwext4 endian/feasibility probe.
 *
 * Performs a fixed, deterministic sequence of filesystem operations against a
 * file-backed image so the same binary can be built little-endian (host) and
 * big-endian (m68k under qemu-m68k) and the resulting images compared byte for
 * byte, then verified by e2fsck and the Linux ext4 driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <ext4.h>
#include <ext4_mkfs.h>
#include <ext4_fs.h>
#include <ext4_debug.h>
#include <ext4_blockdev.h>
#include "file_dev.h"

#define MOUNT_POINT "/mp/"
#define IMAGE_BYTES (32u * 1024u * 1024u)
#define BIG_FILE_BYTES (192u * 1024u)
#define MANY_FILES 200u

#ifdef ASTRA_TRACK_ALLOC
void astra_alloc_report(const char *tag);
#else
#define astra_alloc_report(tag) ((void)(tag))
#endif

static struct ext4_fs fs;
static struct ext4_blockdev *bd;

static uint8_t pattern_byte(unsigned file_index, unsigned offset)
{
    return (uint8_t)((file_index * 31u) + (offset * 7u) + (offset >> 8));
}

static int fail(const char *what, int rc)
{
    printf("FAIL %s rc=%d\n", what, rc);
    return 1;
}

static int do_mount(void)
{
    int rc = ext4_device_register(bd, "astra");
    if (rc != EOK)
        return fail("ext4_device_register", rc);

    rc = ext4_mount("astra", MOUNT_POINT, false);
    if (rc != EOK)
        return fail("ext4_mount", rc);

    rc = ext4_recover(MOUNT_POINT);
    if (rc != EOK && rc != ENOTSUP)
        return fail("ext4_recover", rc);

    if (!getenv("ASTRA_NO_JOURNAL")) {
        rc = ext4_journal_start(MOUNT_POINT);
        if (rc != EOK)
            return fail("ext4_journal_start", rc);
    }

    if (!getenv("ASTRA_NO_WB"))
        ext4_cache_write_back(MOUNT_POINT, 1);
    return 0;
}

static int do_umount(void)
{
    if (!getenv("ASTRA_NO_WB"))
        ext4_cache_write_back(MOUNT_POINT, 0);

    int rc = EOK;
    if (!getenv("ASTRA_NO_JOURNAL")) {
        rc = ext4_journal_stop(MOUNT_POINT);
        if (rc != EOK)
            return fail("ext4_journal_stop", rc);
    }

    rc = ext4_umount(MOUNT_POINT);
    if (rc != EOK)
        return fail("ext4_umount", rc);

    rc = ext4_device_unregister("astra");
    if (rc != EOK)
        return fail("ext4_device_unregister", rc);

    return 0;
}

static int write_file(const char *path, unsigned index, unsigned bytes)
{
    ext4_file file;
    static uint8_t chunk[4096];
    size_t written = 0;
    unsigned offset = 0;

    int rc = ext4_fopen(&file, path, "wb");
    if (rc != EOK)
        return fail("ext4_fopen(w)", rc);

    while (offset < bytes) {
        unsigned span = bytes - offset;
        if (span > sizeof(chunk))
            span = sizeof(chunk);
        for (unsigned i = 0; i < span; ++i)
            chunk[i] = pattern_byte(index, offset + i);

        rc = ext4_fwrite(&file, chunk, span, &written);
        if (rc != EOK || written != span) {
            ext4_fclose(&file);
            return fail("ext4_fwrite", rc);
        }
        offset += span;
    }

    rc = ext4_fclose(&file);
    if (rc != EOK)
        return fail("ext4_fclose(w)", rc);
    return 0;
}

static int read_verify(const char *path, unsigned index, unsigned bytes)
{
    ext4_file file;
    static uint8_t chunk[4096];
    size_t got = 0;
    unsigned offset = 0;

    int rc = ext4_fopen(&file, path, "rb");
    if (rc != EOK)
        return fail("ext4_fopen(r)", rc);

    if (ext4_fsize(&file) != (uint64_t)bytes) {
        printf("FAIL size %s got=%llu want=%u\n", path,
               (unsigned long long)ext4_fsize(&file), bytes);
        ext4_fclose(&file);
        return 1;
    }

    while (offset < bytes) {
        unsigned span = bytes - offset;
        if (span > sizeof(chunk))
            span = sizeof(chunk);

        rc = ext4_fread(&file, chunk, span, &got);
        if (rc != EOK || got != span) {
            ext4_fclose(&file);
            return fail("ext4_fread", rc);
        }
        for (unsigned i = 0; i < span; ++i) {
            if (chunk[i] != pattern_byte(index, offset + i)) {
                printf("FAIL content %s at %u got=%02x want=%02x\n", path,
                       offset + i, chunk[i], pattern_byte(index, offset + i));
                ext4_fclose(&file);
                return 1;
            }
        }
        offset += span;
    }

    rc = ext4_fclose(&file);
    if (rc != EOK)
        return fail("ext4_fclose(r)", rc);
    return 0;
}

static void many_path(char *out, size_t capacity, unsigned index)
{
    snprintf(out, capacity, MOUNT_POINT "many/entry_%03u.dat", index);
}

static int populate(void)
{
    char path[64];
    int rc;

    printf("  populate: mkdir dir\n");
    rc = ext4_dir_mk(MOUNT_POINT "dir");
    if (rc != EOK)
        return fail("ext4_dir_mk dir", rc);
    printf("  populate: mkdir dir/nested\n");
    rc = ext4_dir_mk(MOUNT_POINT "dir/nested");
    if (rc != EOK)
        return fail("ext4_dir_mk nested", rc);
    printf("  populate: mkdir many\n");
    rc = ext4_dir_mk(MOUNT_POINT "many");
    if (rc != EOK)
        return fail("ext4_dir_mk many", rc);

    printf("  populate: hello.txt\n");
    if (write_file(MOUNT_POINT "hello.txt", 1, 37))
        return 1;
    printf("  populate: big.bin\n");
    if (write_file(MOUNT_POINT "dir/nested/big.bin", 2, BIG_FILE_BYTES))
        return 1;

    printf("  populate: many\n");
    for (unsigned i = 0; i < MANY_FILES; ++i) {
        many_path(path, sizeof(path), i);
        if (write_file(path, 100 + i, 64 + (i % 97)))
            return 1;
    }

    rc = ext4_frename(MOUNT_POINT "hello.txt", MOUNT_POINT "dir/renamed.txt");
    if (rc != EOK)
        return fail("ext4_frename", rc);

    rc = ext4_fremove(MOUNT_POINT "many/entry_007.dat");
    if (rc != EOK)
        return fail("ext4_fremove", rc);

    /*
     * Names differing only in case must be distinct objects. ext4 is
     * case-sensitive unless the volume carries the casefold feature, which
     * this profile never sets and lwext4 cannot mount.
     */
    if (write_file(MOUNT_POINT "dir/Case.dat", 3, 64))
        return 1;
    if (write_file(MOUNT_POINT "dir/case.dat", 4, 128))
        return 1;
    if (write_file(MOUNT_POINT "dir/CASE.DAT", 5, 192))
        return 1;

    return 0;
}

static int verify(void)
{
    char path[64];
    ext4_dir dir;
    const ext4_direntry *entry;
    unsigned counted = 0;
    int rc;

    if (read_verify(MOUNT_POINT "dir/renamed.txt", 1, 37))
        return 1;
    if (read_verify(MOUNT_POINT "dir/nested/big.bin", 2, BIG_FILE_BYTES))
        return 1;

    for (unsigned i = 0; i < MANY_FILES; ++i) {
        if (i == 7)
            continue;
        many_path(path, sizeof(path), i);
        if (read_verify(path, 100 + i, 64 + (i % 97)))
            return 1;
    }

    if (read_verify(MOUNT_POINT "dir/Case.dat", 3, 64))
        return 1;
    if (read_verify(MOUNT_POINT "dir/case.dat", 4, 128))
        return 1;
    if (read_verify(MOUNT_POINT "dir/CASE.DAT", 5, 192))
        return 1;

    {
        ext4_file probe;

        rc = ext4_fopen(&probe, MOUNT_POINT "dir/cAsE.dat", "rb");
        if (rc == EOK) {
            ext4_fclose(&probe);
            printf("FAIL case folding: cAsE.dat resolved\n");
            return 1;
        }
        if (rc != ENOENT)
            return fail("ext4_fopen(case probe)", rc);
    }

    rc = ext4_dir_open(&dir, MOUNT_POINT "many");
    if (rc != EOK)
        return fail("ext4_dir_open", rc);
    while ((entry = ext4_dir_entry_next(&dir)) != NULL) {
        if (entry->name_length == 1 && entry->name[0] == '.')
            continue;
        if (entry->name_length == 2 && entry->name[0] == '.' &&
            entry->name[1] == '.')
            continue;
        ++counted;
    }
    ext4_dir_close(&dir);

    if (counted != MANY_FILES - 1u) {
        printf("FAIL dir count got=%u want=%u\n", counted, MANY_FILES - 1u);
        return 1;
    }

    printf("verify ok: %u entries enumerated\n", counted);
    return 0;
}

int main(int argc, char **argv)
{
    struct ext4_mkfs_info info;
    const char *mode;
    int rc;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("ASTRA_EXT4_DEBUG"))
        ext4_dmask_set(DEBUG_ALL);

    if (argc < 3) {
        printf("usage: %s <image> <format|populate|verify> [blocksize]\n", argv[0]);
        return 2;
    }
    mode = argv[2];

    printf("probe endian=%s ptr=%u\n",
#ifdef CONFIG_BIG_ENDIAN
           "big",
#else
           "little",
#endif
           (unsigned)sizeof(void *));

    file_dev_name_set(argv[1]);
    bd = file_dev_get();
    if (bd == NULL)
        return fail("file_dev_get", 0);

    if (strcmp(mode, "format") == 0) {
        memset(&info, 0, sizeof(info));
        info.block_size = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : 4096u;
        info.journal = getenv("ASTRA_NO_JOURNAL") ? false : true;
        info.len = IMAGE_BYTES;

        printf("stage: mkfs\n");
        rc = ext4_mkfs(&fs, bd, &info, F_SET_EXT4);
        if (rc != EOK)
            return fail("ext4_mkfs", rc);
        printf("mkfs ok block_size=%u\n", info.block_size);
    }

    printf("stage: mount\n");
    if (do_mount())
        return 1;

    if (strcmp(mode, "format") == 0 || strcmp(mode, "populate") == 0) {
        printf("stage: populate\n");
        if (populate()) {
            do_umount();
            return 1;
        }
    }

    astra_alloc_report("mounted");
    printf("stage: verify\n");
    if (verify()) {
        do_umount();
        return 1;
    }

    if (do_umount())
        return 1;

    astra_alloc_report(mode);
    printf("PASS %s\n", mode);
    return 0;
}
