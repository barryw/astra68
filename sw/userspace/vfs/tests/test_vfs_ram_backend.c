#include <astra/vfs_ram_backend.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const AstraVfsBackendOps *ops;

static void test_tree(void)
{
    AstraVfsRamBackend fs;
    AstraVfsNodeInfo info;
    AstraVfsFilesystemInfo capacity;
    uintptr_t root = 0u;
    uintptr_t directory = 0u;
    uintptr_t file = 0u;
    uintptr_t alias = 0u;
    char name[ASTRA_VFS_NAME_MAX];
    char text[32];
    uint32_t moved = 0u;
    uint32_t length = 0u;
    uint64_t position = 0u;
    uint64_t cookie = 0u;

    assert(astra_vfs_ram_init(&fs, 32768u, malloc, free,
                              NULL, NULL, NULL));
    assert(ops->mkdir(&fs, "/docs", 0750u) == ASTRA_VFS_OK);
    assert(ops->mkdir(&fs, "/docs", 0750u) == ASTRA_VFS_ERR_EXISTS);
    assert(ops->stat(&fs, "/docs", &info) == ASTRA_VFS_OK &&
           info.kind == ASTRA_VFS_KIND_DIRECTORY && info.mode == 0750u);
    assert(ops->open(&fs, "/", ASTRA_VFS_OPEN_DIRECTORY, 0u,
                     &root, &info) == ASTRA_VFS_OK);
    assert(ops->open_at(&fs, root, "docs", ASTRA_VFS_OPEN_DIRECTORY,
                        0u, &directory, &info) == ASTRA_VFS_OK);
    assert(ops->open_at(&fs, directory, "note",
                        ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_WRITE,
                        0640u, &file, &info) == ASTRA_VFS_OK);
    assert(ops->write(&fs, file, 0u, 0u, "abc", 3u, &moved,
                      &position) == ASTRA_VFS_OK && moved == 3u &&
           position == 3u);
    assert(ops->write(&fs, file, 8192u, 0u, "z", 1u, &moved,
                      &position) == ASTRA_VFS_OK && moved == 1u &&
           position == 8193u);
    memset(text, 0xff, sizeof(text));
    assert(ops->read(&fs, file, 4096u, text, sizeof(text), &moved) ==
           ASTRA_VFS_OK && moved == sizeof(text));
    for (size_t i = 0u; i < sizeof(text); ++i) assert(text[i] == 0);
    assert(ops->stat_node(&fs, file, &info) == ASTRA_VFS_OK &&
           info.size == 8193u);
    assert(ops->link(&fs, "/docs/note", "/docs/hard") == ASTRA_VFS_OK);
    assert(ops->stat(&fs, "/docs/hard", &info) == ASTRA_VFS_OK &&
           info.nlink == 2u);
    assert(ops->symlink(&fs, "note", "/docs/soft") == ASTRA_VFS_OK);
    assert(ops->readlink(&fs, "/docs/soft", text, 3u, &length) ==
           ASTRA_VFS_ERR_BUFFER_TOO_SMALL && length == 4u);
    assert(ops->readlink(&fs, "/docs/soft", text, sizeof(text), &length) ==
           ASTRA_VFS_OK && length == 4u && memcmp(text, "note", 4u) == 0);
    assert(ops->readdir(&fs, directory, NULL, cookie, name, sizeof(name),
                        &info, &cookie) == ASTRA_VFS_OK && cookie == 1u);
    assert(ops->readdir(&fs, directory, NULL, cookie, name, sizeof(name),
                        &info, &cookie) == ASTRA_VFS_OK && cookie == 2u);
    assert(ops->readdir(&fs, directory, NULL, cookie, name, sizeof(name),
                        &info, &cookie) == ASTRA_VFS_OK && cookie == 0u);
    assert(ops->rename(&fs, "/docs/note", "/docs/renamed") == ASTRA_VFS_OK);
    assert(ops->stat(&fs, "/docs/note", &info) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(ops->open(&fs, "/docs/hard", ASTRA_VFS_OPEN_READ, 0u,
                     &alias, &info) == ASTRA_VFS_OK);
    assert(ops->read(&fs, alias, 0u, text, 3u, &moved) == ASTRA_VFS_OK &&
           moved == 3u && memcmp(text, "abc", 3u) == 0);
    assert(ops->unlink(&fs, "/docs/hard") == ASTRA_VFS_OK);
    assert(ops->unlink(&fs, "/docs/renamed") == ASTRA_VFS_OK);
    assert(ops->stat_node(&fs, alias, &info) == ASTRA_VFS_OK &&
           info.nlink == 0u);
    assert(ops->read(&fs, alias, 8192u, text, 1u, &moved) == ASTRA_VFS_OK &&
           moved == 1u && text[0] == 'z');
    assert(ops->close(&fs, alias) == ASTRA_VFS_OK);
    assert(ops->close(&fs, file) == ASTRA_VFS_OK);
    assert(ops->unlink_at(&fs, directory, "soft", 0u) == ASTRA_VFS_OK);
    assert(ops->unlink_at(&fs, root, "docs", 0u) == ASTRA_VFS_ERR_IS_DIR);
    assert(ops->unlink_at(&fs, root, "docs",
                          ASTRA_VFS_AT_REMOVE_DIRECTORY) == ASTRA_VFS_OK);
    assert(ops->close(&fs, directory) == ASTRA_VFS_OK);
    assert(ops->filesystem_info(&fs, root, NULL, &capacity) == ASTRA_VFS_OK &&
           capacity.block_size == 4096u && fs.node_count == 1u);
    assert(ops->close(&fs, root) == ASTRA_VFS_OK);
    astra_vfs_ram_destroy(&fs);
    assert(fs.used_bytes == 0u);
}

static void test_refusals(void)
{
    AstraVfsRamBackend fs;
    AstraVfsNodeInfo info;
    uintptr_t file = 0u;
    uint8_t block[4096];
    uint32_t moved = 0u;
    uint64_t position = 0u;
    uint64_t before;

    memset(block, 0x5a, sizeof(block));
    assert(astra_vfs_ram_init(&fs, 9000u, malloc, free,
                              NULL, NULL, NULL));
    assert(ops->open(&fs, "/x", ASTRA_VFS_OPEN_CREATE |
                     ASTRA_VFS_OPEN_WRITE, 0600u, &file, &info) ==
           ASTRA_VFS_OK);
    assert(ops->write(&fs, file, 0u, 0u, block, sizeof(block),
                      &moved, &position) == ASTRA_VFS_OK &&
           moved == sizeof(block));
    assert(ops->write(&fs, file, 4096u, 0u, block, sizeof(block),
                      &moved, &position) == ASTRA_VFS_OK &&
           moved == sizeof(block));
    before = fs.used_bytes;
    assert(ops->write(&fs, file, 8192u, 0u, block, sizeof(block),
                      &moved, &position) == ASTRA_VFS_ERR_NO_SPACE &&
           moved == 0u && fs.used_bytes == before);
    assert(ops->stat_node(&fs, file, &info) == ASTRA_VFS_OK &&
           info.size == 8192u);
    assert(ops->write(&fs, file, UINT64_MAX, 0u, "x", 1u, &moved,
                      &position) == ASTRA_VFS_ERR_INVALID);
    assert(ops->open(&fs, "/x", ASTRA_VFS_OPEN_CREATE |
                     ASTRA_VFS_OPEN_EXCLUSIVE, 0600u, &file, &info) ==
           ASTRA_VFS_ERR_EXISTS);
    assert(ops->mkdir(&fs, "/x/child", 0700u) == ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->unlink(&fs, "/") == ASTRA_VFS_ERR_ACCESS);
    assert(ops->truncate(&fs, file, 0u) == ASTRA_VFS_OK &&
           fs.used_bytes < before);
    assert(ops->write(&fs, file, 8192u, 0u, "x", 1u, &moved,
                      &position) == ASTRA_VFS_OK && moved == 1u);
    assert(ops->close(&fs, file) == ASTRA_VFS_OK);
    assert(ops->unlink(&fs, "/x") == ASTRA_VFS_OK);
    astra_vfs_ram_destroy(&fs);
    assert(fs.used_bytes == 0u);
}

static void test_path_and_rename_boundaries(void)
{
    AstraVfsRamBackend fs;
    AstraVfsNodeInfo info;
    uintptr_t file = 0u;

    assert(astra_vfs_ram_init(&fs, 32768u, malloc, free,
                              NULL, NULL, NULL));
    assert(ops->mkdir(&fs, "/a", 0700u) == ASTRA_VFS_OK);
    assert(ops->mkdir(&fs, "/a/inside", 0700u) == ASTRA_VFS_OK);
    assert(ops->mkdir(&fs, "/b", 0700u) == ASTRA_VFS_OK);
    assert(ops->open(&fs, "/a/f", ASTRA_VFS_OPEN_CREATE |
                     ASTRA_VFS_OPEN_WRITE, 0600u, &file, &info) ==
           ASTRA_VFS_OK);
    assert(ops->stat(&fs, "/a/f//", &info) == ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->unlink(&fs, "/a/f//") == ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->stat(&fs, "/a/f", &info) == ASTRA_VFS_OK);
    assert(ops->rename(&fs, "/a", "/a/inside/cycle") ==
           ASTRA_VFS_ERR_INVALID);
    assert(ops->stat(&fs, "/a/inside", &info) == ASTRA_VFS_OK);
    assert(ops->rename(&fs, "/a/f", "/b/missing/") ==
           ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->rename(&fs, "/a/f", "/a/f/") ==
           ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->link(&fs, "/a/f", "/b/link/") ==
           ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->symlink(&fs, "/a/f", "/b/link/") ==
           ASTRA_VFS_ERR_NOT_DIR);
    assert(ops->symlink(&fs, "", "/b/link") ==
           ASTRA_VFS_ERR_INVALID);
    assert(ops->stat(&fs, "/b/link", &info) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(ops->rename(&fs, "/a/inside", "/b/inside") == ASTRA_VFS_OK);
    assert(ops->stat(&fs, "/b/inside", &info) == ASTRA_VFS_OK);
    assert(ops->stat(&fs, "/a/inside", &info) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(ops->rename(&fs, "/a", "/b") == ASTRA_VFS_ERR_NOT_EMPTY);
    assert(ops->stat(&fs, "/a/f", &info) == ASTRA_VFS_OK);
    assert(ops->stat(&fs, "/b/inside", &info) == ASTRA_VFS_OK);
    assert(ops->close(&fs, file) == ASTRA_VFS_OK);
    astra_vfs_ram_destroy(&fs);
    assert(fs.used_bytes == 0u);
}

int main(void)
{
    ops = astra_vfs_ram_ops();
    test_tree();
    test_refusals();
    test_path_and_rename_boundaries();
    return 0;
}
