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

Eighteen upstream changes are applied **in-tree**. The patches are retained
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
| `0008-creation-modes.patch` | `src/ext4.c:924`, `include/ext4.h:329` | Adds mode-aware file and directory creation entry points while preserving the old APIs as default-mode wrappers. Permission bits are installed before the inode is linked, avoiding a create-then-chmod race. |
| `0009-read-concurrency.patch` | `src/ext4.c:71`, `src/ext4_blockdev.c:65` | Splits read-only file I/O from the exclusive mount/write lock, protects cache metadata with a short-held lock, and coalesces cache misses through the physical fill lane. The cache lock is released before a device wait. |
| `0010-dir-leaf-checksum-order.patch` | `src/ext4_dir_idx.c:387` | Indexed-directory initialization computed the first leaf checksum before zeroing the entry inode, invalidating the checksum immediately. All bytes are now initialized before the checksum. |
| `0011-durable-write-barriers.patch` | `include/ext4_blockdev.h:67`, `src/ext4_blockdev.c:113`, `src/ext4_journal.c:2181` | The block interface had no durability callback, so journal commit and `fsync` could return after writes reached only volatile device state. Adds an optional flush operation, orders journal records before the commit record with device barriers, and makes cache flush finish with a device barrier. |
| `0012-coalesced-read-cache.patch` | `src/ext4.c:1899`, `src/ext4_blockdev.c:383` | Contiguous full-block file reads bypassed the coherent cache, so every external command launch reread its executable blocks. Keeps one coalesced device read on a cold miss, publishes the run into the existing cache, and returns warm runs without device I/O. |
| `0013-extent-guard-local.patch` | `src/ext4_fs.c:805` | The no-extents Astra profile left an extent-only local outside its feature guard, producing an unused-variable warning on every target build. The declaration now has the same feature lifetime as its only uses. |
| `0014-readlink-lock-recursion.patch` | `src/ext4.c:1759`, `src/ext4.c:2798` | `ext4_readlink` held the exclusive mount lock and called public `ext4_fread`, which recursively acquired the read lock and aborted or deadlocked. The shared read body now has an already-locked entry used by `readlink`, while public reads retain their read-lock wrapper. |
| `0015-atomic-sparse-truncate-extension.patch` | `src/ext4.c:1674`, `src/ext4.c:2045` | Growing `ext4_ftruncate` returned success without changing the inode. Astra's backend compensated with repeated 64-byte writes, making one truncate slow and non-atomic. lwext4 now grows the inode inside its existing transaction, zeroes any previously allocated partial tail block, keeps whole-block gaps sparse, preserves the file position, timestamps size changes, and leaves the backend with one shared call. The patch also removes an erroneous unlock from the already-unlocked failure path and fixes `fwrite`/`ftruncate` access checks that tested `flags & O_RDONLY` even though `O_RDONLY` is zero. |
| `0016-write-into-sparse-hole.patch` | `src/ext4_fs.c:1473` | The non-extent implementation ignored the create half of `ext4_fs_init_inode_dblk_idx`, so a write inside a sparse hole passed physical block zero into the cache and aborted while releasing it. It now allocates the requested logical block, zeroes it before publishing its inode mapping, and returns the real physical block. |
| `0017-slicing-by-4-crc32c.patch` | `src/ext4_crc32.c:107`, `src/ext4_crc32.c:378` | Metadata and journal CRC32C was the hottest guest loop in the rename profile. The shared implementation now consumes four bytes per iteration with slicing-by-4 tables, retains byte loads for unaligned big-endian buffers, and uses a pointer bound so the MC68030 loop does not recompute the remaining word count on every iteration. |
| `0018-create-final-component-only.patch` | `src/ext4.c:1020` | Upstream interpreted `O_CREAT` as permission to manufacture every missing path component as a directory. File creation now creates only the final component, matching POSIX and the VFS backend contract; a missing parent returns `ENOENT`. |

Defect 0003 is invisible against lwext4's own `mkfs`, which leaves
`s_hash_seed` zero. It appears only against an `mke2fs` image, which is the
profile Astra uses.

Defect 0004 is the first of these that is not endian-specific: it is wrong
everywhere, and `ext4_fread` in the same file already does it correctly. It is
covered by `make ext4-test` mode `full`, which fills a volume and fails against
unpatched upstream with `ext4_fwrite returned EOK having moved 0 of 4096`.

Patch 0007 is a feature rather than a defect fix, and it is the only one of
these that upstream would recognise as its own plan: the TODO naming the
missing clock is upstream's. Patch 0008 is the POSIX creation-mode extension;
the raw and partitioned image tests read back a 0600 file and 0710 directory.
Patch 0009 is covered by a deterministic held-read gate: an unrelated cached
read completes without physical I/O, while a same-block peer sleeps and reuses
the first fill. The same gate runs under ASan/UBSan and TSan.

Patch 0010 became visible when the mount gate finally invoked the independent
checker it had always claimed to invoke: `e2fsck -fn` rejected directory inode
15 block 1 before the fix. Raw, partitioned, full-volume, and fresh-remount
images now pass.

Patch 0011 is covered by a volatile-media crash oracle. The backend keeps
writes separate from its durable image and publishes them only when lwext4
issues the block flush callback. After an unclean process exit, a fresh mount
recovers the journal and verifies the committed file byte-for-byte; three
successive crash/recovery cycles remain `e2fsck -fn` clean. The Astra port maps
that callback to the existing block `FLUSH` request rather than inventing a
second durability path.

Patch 0012 is covered by a contiguous 12 KiB file oracle. Its cold read must
perform physical I/O, while an immediate seek and reread must return identical
bytes with zero physical reads. Raw, partitioned, sanitizer, and TSan runs pass.

Patch 0015 is covered by a persistent sparse-extension oracle. It writes a
three-byte prefix, extends the inode beyond one block, verifies the file offset
did not move and the entire hole reads as zero, then repeats the byte and size
checks from a fresh mount before `e2fsck` judges the image. A second fixture
shrinks `AST` to `A`, re-extends it, and proves the discarded `ST` bytes cannot
reappear from the allocated tail block. The same fixture is opened read-only
and must reject both write and truncate without changing it.

Patch 0016 is covered by appending into the sparse second block created by the
Patch 0015 fixture. The append must materialize that exact logical block, keep
the preceding hole zero, preserve the appended bytes through a fresh mount,
and leave the image clean under independent `e2fsck`.

Patch 0017 is checked against an independent bitwise CRC32C oracle across
unaligned offsets and lengths plus the standard `123456789` vector. The
generated MC68030 loop was inspected before and after the pointer-bound change;
the retained version removes the per-iteration subtract and shift. In the
single-client rename workload that refinement moved the identical seed from
441 to 448 operations per second without changing physical I/O.

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
