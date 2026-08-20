# lwext4 vendor record

## Upstream identity

- Project: [lwext4](https://github.com/gkostka/lwext4)
- Upstream commit: `58bcf89a121b72d4fb66334f1693d3b30e4cb9c5`
- Upstream commit date: 2022-09-22 (still upstream HEAD at import)
- Imported: 2026-08-05
- License of the imported subset: **BSD-3-Clause**, carried in the header of
  every imported source and header file

The source was imported without upstream Git metadata.

## License and the GPLv2 exclusion

Upstream ships a root `LICENSE` containing the GNU General Public License
version 2. That aggregate license exists **only** because two files in the tree
are GPLv2-or-later. Upstream's own `README.md` states it directly:

> Some of the source files are licensed under GPLv2. It makes whole lwext4
> GPLv2 licensed. To use library as a BSD3, GPLv2 licensed source files must be
> removed first.

Those two files are **not imported**:

| Excluded file | Reason |
|---|---|
| `src/ext4_extent.c` | GPLv2-or-later |
| `src/ext4_xattr.c` | GPLv2-or-later |

Every remaining file carries the three-clause BSD notice — copyright retention,
binary reproduction, and the non-endorsement clause — and no imported file
contains GPL text. Verified mechanically at import:

```sh
grep -rl "GNU General Public" third_party/lwext4    # no matches
```

Upstream's root `LICENSE` was therefore **not imported**. Shipping the GPLv2
aggregate text alongside a BSD-3-Clause-only subset would misreport the license
of everything in this directory to any audit that reads it. Upstream
`README.md` is retained and states the situation in upstream's own words.

Earlier Astra documents recorded these files as BSD-2-clause. That was wrong;
the notice is BSD-3-Clause. The GPLv2 finding itself was correct.

The corresponding build profile sets `CONFIG_EXTENTS_ENABLE 0` and
`CONFIG_XATTR_ENABLE 0`, so nothing references the excluded implementations.
The public xattr entry points in `ext4.c` survive the config switch and are
satisfied by Astra-authored stubs in `sw/userspace/storage/src/ext4_xattr_stub.c`,
which return `ENOTSUP`.

Consequences of the exclusion, which are load-bearing and not merely
bookkeeping:

- Volumes must be formatted `-O ^extent,^ext_attr`. That is not the default
  `mke2fs -t ext4` shape, so the mkfs profile is not optional.
- File block mapping is indirect, not extent-based.
- Extended attributes are unavailable, so POSIX ACLs and security labels have
  no on-disk home. Adding them later means revisiting this exclusion.

## Other exclusions

Upstream build scaffolding and unused backends were left out because Astra
supplies its own: `CMakeLists.txt`, `Makefile`, `fs_test.mk`, `fs_test/`,
`toolchain/`, `.travis.yml`, `_config.yml`, `.clang-format`, `.gitignore`,
`blockdev/blockdev.[ch]`, and `blockdev/windows/`.

`blockdev/linux/` is retained. It is BSD-3-Clause and it backs the m68k-linux
qualification probe in `sw/userspace/storage/lwext4-eval`, which is the
big-endian regression gate for this tree.

`src/ext4_mkfs.c` and `src/ext4_mbr.c` are imported but are **not in the Astra
built set**. Nothing in the core calls either. `ext4_mkfs` additionally
mis-accounts free blocks whenever the last block group is short — on both
endians, hidden at its default 1024-byte block size and visible at the 4 KiB
size Astra uses. Astra formats offline with `mke2fs`; the file stays vendored
so a future fix has somewhere to land.

## Astra68 changes to upstream files

Seven upstream defect fixes are applied **in-tree**. The patches are retained
verbatim under `astra/patches/` as the audit record and as the re-apply path
for a future upstream bump.

| Patch | Site | Defect |
|---|---|---|
| `0001-be-dir-entry-inode.patch` | `src/ext4.c:1189` | `ext4_create_hardlink` read `result.dentry->inode` raw where every other caller uses `ext4_dir_en_get_inode()`. On big-endian the byte-swapped inode number aborts the rename path. |
| `0002-be-superblock-checksum-type.patch` | `src/ext4_super.c:104` | `s->checksum_type` is a `uint8_t` compared against `to_le32(EXT4_CHECKSUM_CRC32C)`. On big-endian the constant becomes `0x01000000`, so every `metadata_csum` volume fails `ext4_sb_check` and mount returns `ENOTSUP`. |
| `0003-be-htree-hash-seed.patch` | `src/ext4_hash.c:270` | `s_hash_seed` is an on-disk little-endian array `memcpy`'d straight into the host hash state, so every htree hash is wrong on big-endian. |
| `0004-fwrite-error-masked-by-inode-ref-release.patch` | `src/ext4.c:2009` | `ext4_fwrite`'s `Finish:` did `r = ext4_fs_put_inode_ref(&ref)`, discarding the write's own error. ENOSPC and device EIO both returned `EOK`, and the failing write took the `ext4_trans_stop` branch, committing a transaction whose write had not happened. |
| `0005-cache-file-data.patch` | `src/ext4.c:1669` | `ext4_fread` and partial file writes bypassed the block cache, so unchanged file data was reread and partial writes performed uncached read-modify-write cycles. Reads and partial writes now use the coherent cache; direct full-block writes invalidate cached copies. |
| `0007-inode-timestamps.patch` | `src/ext4_fs.c:915`, `src/ext4.c:230`, `src/ext4.c:2070` | Every inode upstream creates carries zero for access, change and modification time, and the code that would set them is a `TODO ... when we have wall-clock time`. Astra has one, so `CONFIG_USE_USER_TIME` and `ext4_user_now()` -- the shape the allocator hooks already use -- and stamps at creation, on a successful write, and on the directory either side of a link or unlink. A port with no clock still gets zero. |
| `0006-dx-lookup-dot-entries.patch` | `src/ext4_dir.c:459` | `ext4_dir_find_entry` answered ENOENT for `.` and `..` in an indexed directory: neither is in the hash tree, and block 0 -- where both live -- is the index root rather than a leaf the hash walk reaches. Every directory lwext4 created was affected, because it indexes every directory it makes; a directory `mke2fs` wrote was not. The two dot names take the linear walk. |

Defect 0003 is invisible against lwext4's own `mkfs`, which leaves
`s_hash_seed` zero. It appears only against an `mke2fs` image, which is the
profile Astra uses.

Defect 0004 is the first of these that is not endian-specific: it is wrong
everywhere, and `ext4_fread` in the same file already does it correctly. It is
covered by `make ext4-test` mode `full`, which fills a volume and fails against
unpatched upstream with `ext4_fwrite returned EOK having moved 0 of 4096`.

Patch 0007 is a feature rather than a defect fix, and it is the only one of
these that upstream would recognise as its own plan: the TODO naming the
missing clock is upstream's.

Defect 0006 is also endian-independent, and it is invisible to a caller that
resolves paths the way Linux does -- the VFS answers `.` and `..` itself and
never asks the filesystem. It appears the moment something walks a path one
component at a time, which is what Astra's ext4 backend does when it stats the
names a listing returned.

These are the only edits. All other imported files are byte-identical to the
pinned upstream commit.

Upstream never derives `CONFIG_BIG_ENDIAN` itself and no upstream build sets
it, so big-endian was never compiled upstream, let alone tested. The Astra
build profile derives it from `__BYTE_ORDER__`; see
`sw/userspace/storage/port/include/generated/ext4_config.h`.

## Reproduction and verification

```sh
git clone https://github.com/gkostka/lwext4 && cd lwext4
git checkout 58bcf89a121b72d4fb66334f1693d3b30e4cb9c5
rsync -a --exclude=.git \
      --exclude=.travis.yml --exclude=_config.yml --exclude=.clang-format \
      --exclude=.gitignore --exclude=CMakeLists.txt --exclude=Makefile \
      --exclude=fs_test.mk --exclude=fs_test/ --exclude=toolchain/ \
      --exclude=blockdev/windows/ --exclude=blockdev/blockdev.c \
      --exclude=blockdev/blockdev.h \
      --exclude=src/ext4_extent.c --exclude=src/ext4_xattr.c \
      ./ third_party/lwext4/
rm third_party/lwext4/LICENSE
cd third_party/lwext4 && for p in astra/patches/*.patch; do patch -p1 -N < "$p"; done
```

The focused verification commands are:

```sh
# freestanding MC68030 library, host port tests, sanitizers, analyzer
cd sw/userspace/storage && make test && make sanitize && make analyze && make all

# big-endian regression against this vendored tree (Beast: needs
# m68k-linux-gnu-gcc, a user-mode qemu-m68k, mke2fs and e2fsck)
cd sw/userspace/storage/lwext4-eval && make interop && make astra-alloc && make size
```

## Future updates

A vendor revision bump must repeat all of it: re-exclude `ext4_extent.c` and
`ext4_xattr.c` and re-check that no third file has gained a GPL header,
re-apply `astra/patches/` (or drop a patch upstream has fixed), re-check the
xattr stub still matches the surviving entry points in `ext4.c`, and re-run
both verification commands above before the pinned commit changes.
