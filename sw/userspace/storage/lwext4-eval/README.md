# lwext4 big-endian evaluation

Status: candidate qualified far enough to keep; not adopted, not vendored

`docs/STORAGE_AND_VFS.md` names a constrained 4 KiB-block ext4 profile as the
leading native writable volume and `lwext4` as one candidate implementation.
That document also recorded the two open risks: upstream states big-endian
behaviour is coded but untested, and journalling/extents were believed to pull
GPLv2 code into the service. This rig answers both with measurements instead of
assumptions.

lwext4 is deliberately **not** vendored. Point `LWEXT4_DIR` at a checkout of
`https://github.com/gkostka/lwext4` at
`58bcf89a121b72d4fb66334f1693d3b30e4cb9c5` (2022-09-22, still upstream HEAD),
run `make patch`, then build.

## What the rig is

`src/probe.c` runs one fixed, deterministic sequence against a file-backed
image: mkfs (optional), mount, recover, journal start, three directories, a
37-byte file, a 192 KiB file, 200 small files in one directory (enough to force
an htree index), a rename, an unlink, then a full read-back with byte-exact
content verification and a directory enumeration count.

The same source builds three ways:

| Build | Purpose |
|---|---|
| host `gcc` | little-endian reference behaviour |
| `m68k-linux-gnu-gcc -m68030`, run under `qemu-m68k` | real big-endian, 32-bit execution |
| same, with `-DASTRA_FORCE_LE` | m68k control that disables the endian macros |

The third build is the control that separates "big-endian defect" from "32-bit
or m68k ABI defect". `include/generated/ext4_config.h` is the Astra build
profile; lwext4 never derives `CONFIG_BIG_ENDIAN` itself and no upstream build
sets it, so the profile derives it from `__BYTE_ORDER__`.

Correctness is judged by tools that share no code with lwext4: `e2fsck` and the
Linux ext4 driver, over an image `mke2fs` formatted offline. That matches the
intended Astra path, where Linux creates and sizes the image and never services
guest path or file requests.

## Result

Measured on Beast (Ubuntu 24.04, x86_64), 2026-08-04, with
`m68k-linux-gnu-gcc` 13 and a `qemu-m68k` 9.2.4 user-mode build.

Unpatched lwext4 on big-endian MC68030 aborts during a rename, and cannot mount
an `mke2fs`-created ext4 volume at all. Three one-line defects account for all
of it:

| Patch | Site | Defect |
|---|---|---|
| 0001 | `src/ext4.c:1189` | `ext4_create_hardlink` reads `result.dentry->inode` raw; every other caller uses `ext4_dir_en_get_inode()`. On big-endian the byte-swapped inode number aborts the rename path. |
| 0002 | `src/ext4_super.c:104` | `s->checksum_type` is a `uint8_t` compared against `to_le32(EXT4_CHECKSUM_CRC32C)`. On big-endian the constant becomes `0x01000000`, so every `metadata_csum` volume fails `ext4_sb_check` and mount returns `ENOTSUP`. |
| 0003 | `src/ext4_hash.c:270` | `s_hash_seed` is an on-disk little-endian array `memcpy`'d straight into the host hash state, so every htree hash is wrong on big-endian. `e2fsck` reports "bad max hash"/"bad min hash" and invalidates the index. |

Defect 0003 is invisible when lwext4 formats the volume itself, because its
`mkfs` leaves `s_hash_seed` zero. It only appears against an `mke2fs` image,
which is the profile Astra intends to use.

With the three patches applied, on the same fixed workload:

| Check | Result |
|---|---|
| big-endian m68k formats, populates and re-reads its own image | pass |
| big-endian m68k populates an `mke2fs -t ext4 -b 4096 -I 256 -O ^64bit` image | pass |
| `e2fsck -fn` on the big-endian written image | clean |
| Linux loop-mount of the big-endian written image, byte-exact content of all 201 files, 199 directory entries | pass |
| big-endian m68k reads an image written by Linux | pass |
| GPL-free profile (no extents, no xattr) over `-O ^extent,^ext_attr`, same checks | pass |
| `Case.dat`, `case.dat`, and `CASE.DAT` coexist as three distinct files with distinct content; `cAsE.dat` returns `ENOENT` | pass |
| a `-O casefold` volume is refused at mount with `ENOTSUP` | pass |

Case sensitivity is not left to the default. The frozen profile states
`^casefold`, and `lwext4` has no notion of the feature at all: it is absent
from `EXT4_SUPPORTED_FINCOM`, so `ext4_fs_check_features` rejects any casefolded
volume outright. `docs/STORAGE_AND_VFS.md` states the byte-exact naming rule the
VFS layer must also honour.

## Licensing

`ext4_extent.c` and `ext4_xattr.c` carry "GNU General Public License ... or (at
your option) any later version". Every other file under `src/` and `include/`,
including `ext4_journal.c`, is BSD-2-clause. Journalling therefore does **not**
pull GPLv2 code in; `docs/STORAGE_AND_VFS.md` was wrong on that point.

`CONFIG_EXTENTS_ENABLE=0` and `CONFIG_XATTR_ENABLE=0` yield a build that omits
both GPLv2 files. `CONFIG_XATTR_ENABLE=0` empties `ext4_xattr.c` but does not
guard the public xattr entry points in `ext4.c`, so `src/xattr_stub.c` supplies
the six missing symbols as `ENOTSUP`. Dropping extents means indirect block
mapping, i.e. an ext3-shaped volume that the Linux ext4 driver still mounts.

## Measurements

MC68030 object text, `-Os -ffreestanding -ffunction-sections -fdata-sections`,
debug printf and assert off, before section garbage collection:

| Profile | text | data | bss |
|---|---|---|---|
| extents + xattr | 79,891 | 0 | 4,652 |
| no extents, no xattr | 66,395 | 0 | 4,652 |

Dynamic allocation over the fixed workload, measured big-endian on m68k through
`CONFIG_USE_USER_MALLOC` with an accounting allocator, 32 MiB volume,
`CONFIG_BLOCK_DEV_CACHE_SIZE 16`:

| Block size | calls | peak bytes | peak live blocks | largest | leaked at unmount |
|---|---|---|---|---|---|
| 4096 | 15,475 | 110,592 | 888 | 4,096 | 0 |
| 1024 | 13,510 | 90,960 | 1,574 | 1,060 | 0 |

Nothing leaks, and the shape is not a heap workload: the small class holds
33..64-byte descriptors, and the 4 KiB class peaks at exactly
`CONFIG_BLOCK_DEV_CACHE_SIZE + 1`.

`make astra-alloc` reruns the same big-endian workload with `libastraalloc`
(`sw/userspace/alloc`) in place of the host heap, using the class table in
`src/port_alloc.c` derived from the table above:

```
astra_alloc[populate]: allocations=15475 frees=15475 failures=0 rejections=0
                       live=0 peak_live=888 peak_bytes=126144 valid=1
  class 64    count=900   peak_live=855 failures=0
  class 128   count=32    peak_live=17  failures=0
  class 2048  count=4     peak_live=1   failures=0
  class 4096  count=20    peak_live=17  failures=0
```

Charged bytes exceed the 110,592-byte request peak because a bounded allocator
charges whole slots. The arena the class table needs is 151,936 bytes.

These figures are host-emulated. No MC68030 cycle count, no real block backend,
and no Astra service measurement exists yet. The journal scales with volume
size, so the class table must be re-measured against the real volume before any
service ships with it.

## C library surface

lwext4 does not need a C library. The external symbols left undefined by the
freestanding MC68030 build are:

```
libc:    malloc free qsort memcmp memcpy memmove memset strcmp strlen strncmp strncpy
libgcc:  __udivdi3 __umoddi3 __lshrdi3
fortify: __memcpy_chk __strcpy_chk   (absent with -U_FORTIFY_SOURCE)
```

`libastrart` already provides the `mem*` and `str*` entries. `malloc`/`free`
map to `libastraalloc` through `CONFIG_USE_USER_MALLOC`. That leaves `qsort`,
used only by `ext4_dir_idx.c` and present in both profiles, plus the three
64-bit libgcc helpers. Porting a C library is therefore not on this path.

## Defects that are not endian-related

`ext4_mkfs` mis-accounts free blocks whenever the last block group is short.
`create_bg_desc` sets `bg_free_blk = blocks_per_group - inode_table_blocks`
without clamping to the blocks the group actually has, so a 32 MiB 4 KiB-block
volume claims 31,352 free of 8,192 total and `e2fsck` reports "Free blocks count
wrong" plus a wrapped 64-bit total. This reproduces identically on
little-endian, and is hidden at lwext4's default 1024-byte block size because
the groups come out full.

Astra formats offline with `mke2fs`, so this is not on the first boot path, but
lwext4's own `mkfs` cannot be trusted at 4 KiB blocks as it stands.

## Reproducing

```sh
export LWEXT4_DIR=/path/to/lwext4
make patch
make interop          # mke2fs image, big-endian populate, e2fsck
make alloc            # allocation accounting through a tracking host heap
make astra-alloc      # same workload carried by libastraalloc
make size             # MC68030 object sizes for both profiles
sudo mount -o loop,ro build/probe.img /mnt/x && python3 verify_mount.py /mnt/x
python3 populate_linux.py /mnt/x   # reverse direction: Linux writes, m68k reads
```

`qemu-m68k` user mode is required. The astra68 QEMU fork does not build a
`m68k-linux-user` target: `target/m68k/translate.c` registers `INSN(pmmu030,
...)` unconditionally while `disas_pmmu030` is behind `!CONFIG_USER_ONLY`. Build
the user-mode emulator from an unmodified 9.2.4 tree, or add that guard.

## What is still missing

The gates in `docs/STORAGE_AND_VFS.md` that this rig does not touch: an exact
frozen mkfs feature profile, power-cut and recovery testing after every block
write/flush transition, a malformed and adversarial image corpus with fuzzing,
model-based random operation testing against an oracle, parallel client and
queue saturation, and real block-backend and MC68030 performance baselines.
The three patches also need to go upstream or be carried as a recorded fork.
