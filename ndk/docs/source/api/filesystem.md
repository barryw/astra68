# Filesystem Kit

Volumes, assigns, paths, files, unions, process namespaces, disk administration,
and streams (`filesystem.library`, `dos.library`, `streams.library`).

`dos.library` combines coherent volume and usage inventory with shared disk
edit policy. Callers need `VOLUMES` and `VOLUME_USAGE` for inspection, plus
`DISK_ADMIN` for edits. Disk jobs are asynchronous: retain returned exact job
reference and poll status. Mounting, files, and provider transport remain in
`filesystem.library`.

```{doxygenfile} dos_library.h
:project: astra-ndk
```

```{doxygenfile} volume.h
:project: astra-ndk
```

```{doxygenfile} volume_abi.h
:project: astra-ndk
```

```{doxygenfile} filesystem_kit.h
:project: astra-ndk
```

```{doxygenfile} filesystem_library.h
:project: astra-ndk
```

```{doxygenfile} vfs_client.h
:project: astra-ndk
```

```{doxygenfile} vfs_assign.h
:project: astra-ndk
```

```{doxygenfile} vfs_path.h
:project: astra-ndk
```

```{doxygenfile} vfs_union.h
:project: astra-ndk
```

```{doxygenfile} vfs_process.h
:project: astra-ndk
```

```{doxygenfile} vfs_reader.h
:project: astra-ndk
```

```{doxygenfile} stream.h
:project: astra-ndk
```
