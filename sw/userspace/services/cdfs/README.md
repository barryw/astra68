# CDFS

The optional native `cdfs` service mounts an optical disc image at `/cd0`.
ODFileSystem's unchanged portable core reads ISO 9660 (including High Sierra
and multi-extent files), Rock Ridge, Joliet, UDF type-1 partitions, and HFS/HFS+
data forks. This is a read-only filesystem behind the ordinary Astra VFS;
native and POSIX applications use the same mount.

The desktop image installs the service disabled. Put an immutable image at
`/home/disc.iso`, or edit `image` in
`/system/config/services/cdfs/settings.conf` to another absolute path beneath
the service's granted roots. Enable and start it with:

```text
service enable cdfs
service start cdfs
```

Open a new Terminal after the service starts so it receives `/cd0` authority.
`ls /cd0`, `cat /cd0/path`, and `cd /cd0/directory` then work normally.
`service stop cdfs` releases sessions, unmounts ODFS, and closes the source
file. Existing client handles fail after the service stops.

The private backend also accepts an `AstraBlockDevice`, splitting 2048-byte
optical reads across the source's sector size and transfer limit. Its owner
must hold the device lease and keep the source alive until unmount. Device
generation or geometry changes permanently invalidate that mount, including
cached reads and metadata. Image size or modification-time changes do the
same. The source must remain immutable: same-size edits within the timestamp
resolution cannot be detected by the existing file metadata contract.

Symbolic links are exposed without following them; the shared Astra namespace
layer follows their original POSIX targets and rechecks authority. Unknown
ownership and link-count fields remain zero because ODFS's public node does
not carry them. Names and paths retain the current VFS limits (63 and 191
UTF-8 bytes respectively); oversized names fail explicitly without truncation.

Physical-drive discovery and hotplug require the native device/mount manager
described in `docs/DEVICE_AND_VOLUME_PLAN.md`. The current sources provide no
TOC, raw-audio, or eject operations: image mounts use session zero, and CDDA
is disabled. UDF 2.50+/Blu-ray and HFS resource forks are outside this vendor
reader's supported formats. Burning discs is outside this read-only service.

Run on Beast:

```sh
~/astra-mg/locked make -C sw/userspace/vfs cdfs-test cdfs-sanitize cdfs-analyze
~/astra-mg/locked make -C sw/userspace/services/cdfs all
~/astra-mg/locked python3 emu/qemu/test-cdfs.py QEMU sw/boot/build/astra_boot.bin --image CARD_IMAGE
```

The QEMU gate republishes userspace. With concurrent checkouts, override both
`NCURSES_BUILD` and `NCURSES_PREFIX`, plus `ZSH_BUILD` and `VIM_SOURCE`, to
private paths before running it. Keep QEMU build caches private with
`ASTRA_QEMU_WORK_ROOT`; images and logs must also use unique paths.

The host gate covers 512/2048-byte block sources, short file reads, metadata,
Rock Ridge links, relative directory operations, cookie iteration, read-only
refusals, corrupt/I/O-failing media, cached-data invalidation, and VFS shutdown.
The QEMU gate generates its own ISO and exercises the service from zsh on a
private copy of the supplied system image. Physical optical drives are not
qualified by these tests.
