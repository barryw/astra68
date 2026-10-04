#include <astra/vfs_cdfs_backend.h>
#include <astra/memory_block.h>
#include <astra/vfs_local_transport.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t image[32u * ASTRA_CDFS_SECTOR_BYTES];
static uint32_t image_bytes = sizeof(image);
static uint32_t image_failure;
static const AstraVfsBackendOps *ops;

/* The native file transport is the seam: deliberately return short reads. */
uint32_t astra_filesystem_file_info(const AstraFile *file, AstraFileInfo *info)
{
    (void)file;
    info->kind = ASTRA_VFS_KIND_FILE;
    info->open_flags = ASTRA_VFS_OPEN_READ;
    info->byte_size = image_bytes;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read_at(AstraFile *file, uint64_t offset,
    void *buffer, uint32_t length, uint32_t *moved)
{
    (void)file;
    *moved = 0u;
    if (image_failure) return ASTRA_VFS_ERR_IO;
    if (offset >= image_bytes) return ASTRA_VFS_OK;
    if (length > 117u) length = 117u;
    if (length > image_bytes - offset) length = (uint32_t)(image_bytes - offset);
    memcpy(buffer, image + offset, length);
    *moved = length;
    return ASTRA_VFS_OK;
}

static void le32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static void both32(uint8_t *p, uint32_t value)
{
    le32(p, value);
    for (unsigned i = 0; i < 4; ++i) p[4u + i] = (uint8_t)(value >> ((3u - i) * 8));
}

static size_t record(uint8_t *p, const char *name, size_t length,
    uint32_t lba, uint32_t bytes, int directory)
{
    size_t size = 33u + length + (length % 2u == 0u);
    memset(p, 0, size);
    p[0] = (uint8_t)size;
    both32(p + 2, lba);
    both32(p + 10, bytes);
    p[18] = 124; p[19] = 2; p[20] = 29;
    p[21] = 1; p[22] = 30; p[24] = 4; /* UTC+1, leap day */
    p[25] = directory ? 2u : 0u;
    p[28] = 1; p[31] = 1;
    p[32] = (uint8_t)length;
    memcpy(p + 33, name, length);
    return size;
}

static size_t system_use(uint8_t *record_bytes, size_t size,
                         const uint8_t *extra, size_t length)
{
    memcpy(record_bytes + size, extra, length);
    size += length;
    if (size & 1u) record_bytes[size++] = 0;
    assert(size <= UINT8_MAX);
    record_bytes[0] = (uint8_t)size;
    return size;
}

static void fixture(void)
{
    uint8_t *pvd;
    uint8_t *root;
    size_t offset;
    memset(image, 0, sizeof(image));
    pvd = image + 16u * ASTRA_CDFS_SECTOR_BYTES;
    pvd[0] = 1; memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    memset(pvd + 40, ' ', 32); memcpy(pvd + 40, "ASTRA_CDFS", 10);
    both32(pvd + 80, 32u);
    pvd[129] = 8; pvd[130] = 8;
    record(pvd + 156, "\0", 1, 20u, ASTRA_CDFS_SECTOR_BYTES, 1);
    pvd = image + 17u * ASTRA_CDFS_SECTOR_BYTES;
    pvd[0] = 255; memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    root = image + 20u * ASTRA_CDFS_SECTOR_BYTES;
    offset = record(root, "\0", 1, 20u, ASTRA_CDFS_SECTOR_BYTES, 1);
    offset += record(root + offset, "\1", 1, 20u, ASTRA_CDFS_SECTOR_BYTES, 1);
    offset += record(root + offset, "DOCS", 4, 21u, ASTRA_CDFS_SECTOR_BYTES, 1);
    record(root + offset, "HELLO.TXT;1", 11, 22u, 3000u, 0);
    root = image + 21u * ASTRA_CDFS_SECTOR_BYTES;
    offset = record(root, "\0", 1, 21u, ASTRA_CDFS_SECTOR_BYTES, 1);
    offset += record(root + offset, "\1", 1, 20u, ASTRA_CDFS_SECTOR_BYTES, 1);
    record(root + offset, "NOTE.TXT;1", 10, 24u, 4u, 0);
    memset(image + 22u * ASTRA_CDFS_SECTOR_BYTES, 'A', 3000u);
    memcpy(image + 24u * ASTRA_CDFS_SECTOR_BYTES, "note", 4u);
}

static void check_symlinks(void)
{
    static const uint8_t sp[] = {'S','P',7,1,0xbe,0xef,0};
    uint8_t sua[64] = {'P','X',36,1};
    uint8_t *root = image + 20u * ASTRA_CDFS_SECTOR_BYTES;
    size_t offset, size;
    AstraCdfsMedia media;
    AstraVfsCdfsBackend fs;
    AstraFile source = ASTRA_FILE_INIT;
    AstraVfsNodeInfo info;
    uintptr_t file;
    uint32_t length;
    char target[32];

    fixture();
    memset(root, 0, ASTRA_CDFS_SECTOR_BYTES);
    size = record(root, "\0", 1, 20u, ASTRA_CDFS_SECTOR_BYTES, 1);
    offset = system_use(root, size, sp, sizeof(sp));
    offset += record(root + offset, "\1", 1, 20u, ASTRA_CDFS_SECTOR_BYTES, 1);
    offset += record(root + offset, "HELLO.TXT;1", 11, 22u, 3000u, 0);
    both32(sua + 4, 0120777);
    both32(sua + 12, 1u);
    /* SL length is 5-byte header + 2-byte component + 9-byte name. */
    sua[36] = 'S'; sua[37] = 'L'; sua[38] = 16; sua[39] = 1;
    sua[42] = 9;
    memcpy(sua + 43, "hello.txt", 9);
    size = record(root + offset, "LINK;1", 6, 0u, 0u, 0);
    offset += system_use(root + offset, size, sua, 52u);
    /* Root-absolute target must remain byte-exact for the assign layer. */
    sua[38] = 18; sua[41] = 8; sua[42] = 0;
    sua[43] = 0; sua[44] = 9;
    memcpy(sua + 45, "hello.txt", 9);
    size = record(root + offset, "ABS;1", 5, 0u, 0u, 0);
    system_use(root + offset, size, sua, 54u);
    assert(astra_cdfs_media_file(&media, &source) == ASTRA_VFS_OK);
    assert(astra_vfs_cdfs_mount(&fs, &media, NULL, NULL, NULL, NULL) == ASTRA_VFS_OK);
    assert(fs.mount.active_backend == ODFS_BACKEND_ROCK_RIDGE);
    assert(ops->stat(&fs, "/link", &info) == ASTRA_VFS_OK && info.kind == ASTRA_VFS_KIND_SYMLINK);
    assert(info.mode == 0120777u);
    assert(ops->readlink(&fs, "/link", target, 2u, &length) == ASTRA_VFS_ERR_BUFFER_TOO_SMALL && length == 9u);
    assert(ops->readlink(&fs, "/link", target, sizeof(target), &length) == ASTRA_VFS_OK && length == 9u);
    assert(memcmp(target, "hello.txt", length) == 0);
    assert(ops->readlink(&fs, "/abs", target, sizeof(target), &length) == ASTRA_VFS_OK && length == 10u);
    assert(memcmp(target, "/hello.txt", length) == 0);
    assert(ops->open(&fs, "/link", ASTRA_VFS_OPEN_READ, 0, &file, &info) == ASTRA_VFS_ERR_LOOP);
    assert(ops->stat(&fs, "/link/child", &info) == ASTRA_VFS_ERR_NOT_DIR);
    assert(astra_vfs_cdfs_unmount(&fs) == ASTRA_VFS_OK);
}

static void check_volume(AstraVfsCdfsBackend *fs)
{
    AstraVfsNodeInfo info;
    AstraVfsFilesystemInfo capacity;
    uintptr_t root, docs, file;
    uint32_t moved;
    uint64_t cookie = 0u;
    uint64_t position;
    char name[ASTRA_VFS_NAME_MAX];
    char buffer[4000];
    unsigned entries = 0u;
    assert(strcmp(fs->mount.volume_name, "ASTRA_CDFS") == 0);
    assert(ops->stat(fs, "/hello.txt", &info) == ASTRA_VFS_OK);
    assert(info.size == 3000u && info.kind == ASTRA_VFS_KIND_FILE);
    assert(info.mtime == INT64_C(1709166600)); /* 2024-02-29 00:30 UTC */
    assert(ops->stat(fs, "/hello.txt/", &info) == ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->stat(fs, "/hello.txt/..", &info) == ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->stat(fs, "/absent", &info) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(ops->open(fs, "/", ASTRA_VFS_OPEN_DIRECTORY, 0, &root, &info) == ASTRA_VFS_OK);
    assert(ops->open_at(fs, root, "docs", ASTRA_VFS_OPEN_DIRECTORY, 0, &docs, &info) == ASTRA_VFS_OK);
    assert(ops->open_at(fs, docs, "./note.txt", ASTRA_VFS_OPEN_READ, 0, &file, &info) == ASTRA_VFS_OK);
    assert(astra_vfs_cdfs_unmount(fs) == ASTRA_VFS_ERR_BUSY);
    assert(ops->read(fs, file, 1, buffer, 100, &moved) == ASTRA_VFS_OK && moved == 3u);
    assert(memcmp(buffer, "ote", 3) == 0);
    assert(ops->read(fs, file, UINT64_MAX, buffer, 1, &moved) == ASTRA_VFS_OK && moved == 0u);
    assert(ops->read(fs, file, 0, NULL, 0, &moved) == ASTRA_VFS_OK && moved == 0u);
    assert(ops->stat_at(fs, docs, "../hello.txt", &info) == ASTRA_VFS_OK);
    assert(ops->stat_at(fs, docs, "/hello.txt", &info) == ASTRA_VFS_ERR_INVALID);
    assert(ops->stat_node(fs, UINTPTR_MAX, &info) == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(ops->close(fs, file) == ASTRA_VFS_OK);
    assert(ops->close(fs, file) == ASTRA_VFS_ERR_BAD_HANDLE);
    assert(ops->open(fs, "/hello.txt", ASTRA_VFS_OPEN_READ, 0, &file, &info) == ASTRA_VFS_OK);
    assert(ops->read(fs, file, 0, buffer, sizeof(buffer), &moved) == ASTRA_VFS_OK && moved == 3000u);
    for (unsigned i = 0; i < moved; ++i) assert(buffer[i] == 'A');
    while (ops->readdir(fs, root, NULL, cookie, name, sizeof(name), &info, &cookie) == ASTRA_VFS_OK)
        ++entries;
    assert(entries == 2u);
    assert(ops->readdir(fs, root, NULL, UINT64_MAX, name, sizeof(name), &info, &cookie) == ASTRA_VFS_ERR_INVALID);
    assert(ops->readdir(fs, root, NULL, 0, name, 1, &info, &cookie) == ASTRA_VFS_ERR_BUFFER_TOO_SMALL);
    assert(ops->filesystem_info(fs, root, NULL, &capacity) == ASTRA_VFS_OK);
    assert(capacity.blocks == 32u && capacity.blocks_free == 0u && capacity.block_size == 2048u);
    assert(ops->write(fs, file, 0, 0, "x", 1, &moved, &position) == ASTRA_VFS_ERR_READ_ONLY && moved == 0u);
    assert(ops->truncate(fs, file, 0) == ASTRA_VFS_ERR_READ_ONLY);
    assert(ops->mkdir(fs, "/new", 0755) == ASTRA_VFS_ERR_READ_ONLY);
    assert(ops->unlink(fs, "/hello.txt") == ASTRA_VFS_ERR_READ_ONLY);
    assert(ops->rename(fs, "/hello.txt", "/new") == ASTRA_VFS_ERR_READ_ONLY);
    assert(ops->chmod(fs, "/hello.txt", 0777) == ASTRA_VFS_ERR_READ_ONLY);
    assert(ops->sync(fs, file) == ASTRA_VFS_OK);
    assert(ops->close(fs, file) == ASTRA_VFS_OK);
    assert(ops->close(fs, docs) == ASTRA_VFS_OK);
    assert(ops->close(fs, root) == ASTRA_VFS_OK);
}

static void check_block(uint32_t sector_bytes)
{
    AstraMemoryBlock memory;
    AstraBlockDevice block;
    AstraCdfsMedia media;
    AstraVfsCdfsBackend fs;
    AstraVfsNodeInfo info;
    uintptr_t file;
    uint32_t moved;
    char buffer[8];
    astra_memory_block_init(&memory, image, sizeof(image), sector_bytes, 1u,
        ASTRA_BLOCK_FLAG_PRESENT | ASTRA_BLOCK_FLAG_READ_ONLY);
    astra_block_device_init(&block, &astra_memory_block_backend, &memory, NULL, NULL);
    assert(astra_cdfs_media_block(&media, &block) == ASTRA_VFS_OK);
    assert(astra_vfs_cdfs_mount(&fs, &media, NULL, NULL, NULL, NULL) == ASTRA_VFS_OK);
    check_volume(&fs);
    assert(ops->open(&fs, "/hello.txt", ASTRA_VFS_OPEN_READ, 0, &file, &info) == ASTRA_VFS_OK);
    assert(ops->read(&fs, file, 0, buffer, sizeof(buffer), &moved) == ASTRA_VFS_OK);
    ++memory.media_generation; /* Cache hits must still reject changed media. */
    assert(ops->read(&fs, file, 0, buffer, sizeof(buffer), &moved) == ASTRA_VFS_ERR_PEER && moved == 0u);
    assert(ops->stat(&fs, "/", &info) == ASTRA_VFS_ERR_PEER);
    assert(ops->close(&fs, file) == ASTRA_VFS_OK);
    assert(astra_vfs_cdfs_unmount(&fs) == ASTRA_VFS_OK);
}

static void check_protocol(AstraVfsCdfsBackend *fs)
{
    AstraVfsService service;
    AstraVfsSessionSlot sessions[2];
    AstraVfsOpenFile files[4];
    AstraVfsClient client;
    AstraVfsFile file;
    uint64_t size;
    uint16_t kind;
    uint32_t moved;
    char buffer[4];
    const AstraVfsMountOps mount = {NULL, astra_vfs_cdfs_unmount};

    assert(astra_vfs_service_init(&service, ops, fs, sessions, 2, files, 4));
    assert(astra_vfs_connect(&client, astra_vfs_local_transport, &service) == ASTRA_VFS_OK);
    assert(astra_vfs_open(&client, "/docs/note.txt", ASTRA_VFS_OPEN_READ,
                          &file, &size, &kind) == ASTRA_VFS_OK);
    assert(size == 4u && kind == ASTRA_VFS_KIND_FILE);
    assert(astra_vfs_read(&client, file, 0, buffer, sizeof(buffer), &moved) == ASTRA_VFS_OK);
    assert(moved == 4u && memcmp(buffer, "note", 4) == 0);
    assert(astra_vfs_open(&client, "/hello.txt", ASTRA_VFS_OPEN_WRITE,
                          &file, NULL, NULL) == ASTRA_VFS_ERR_READ_ONLY);
    /* Shutdown releases the still-open file before its no-flush unmount. */
    assert(astra_vfs_service_shutdown(&service, &mount, fs) == ASTRA_VFS_OK);
    assert(fs->files == NULL && !fs->mounted);
}

int main(int argc, char **argv)
{
    AstraCdfsMedia media;
    AstraVfsCdfsBackend fs;
    AstraFile file = ASTRA_FILE_INIT;
    ops = astra_vfs_cdfs_ops();
    fixture();
    if (argc == 3 && strcmp(argv[1], "--write-iso") == 0) {
        FILE *out = fopen(argv[2], "wb");
        assert(out != NULL);
        assert(fwrite(image, 1, sizeof(image), out) == sizeof(image));
        assert(fclose(out) == 0);
        return 0;
    }
    check_block(512u);
    check_block(2048u);
    assert(astra_cdfs_media_file(&media, &file) == ASTRA_VFS_OK);
    assert(astra_vfs_cdfs_mount(&fs, &media, NULL, NULL, NULL, NULL) == ASTRA_VFS_OK);
    check_volume(&fs);
    check_protocol(&fs);
    check_symlinks();
    image_bytes = 1u;
    assert(astra_cdfs_media_file(&media, &file) == ASTRA_VFS_ERR_INVALID);
    image_bytes = sizeof(image);
    image_failure = 1u;
    assert(astra_cdfs_media_file(&media, &file) == ASTRA_VFS_OK);
    assert(astra_vfs_cdfs_mount(&fs, &media, NULL, NULL, NULL, NULL) != ASTRA_VFS_OK);
    image_failure = 0u;
    memset(image, 0, sizeof(image));
    assert(astra_vfs_cdfs_mount(&fs, &media, NULL, NULL, NULL, NULL) != ASTRA_VFS_OK);
    puts("CDFS: block and image mounts, reads, metadata, cookies, read-only and media-change checks pass");
    return 0;
}
