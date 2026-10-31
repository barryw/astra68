# Applications and Services

Launching programs, bundles and manifests, the application catalog, services, configuration, and commands.

System library ABI 4 owns each parsed bundle's executable and icon paths.
Initialize manifests before parsing, and release them with
`astra_bundle_manifest_destroy()`. Application catalog entries likewise own
their bundle and icon paths until `astra_application_catalog_destroy()`.

APPL version 5 sends a fixed control message and one readonly shared area of
packed UTF-8 arguments. It supports the native filesystem path bound without
truncation. The launcher takes its own byte snapshot and closes the received
area before checking or launching the request. Startup ABI 8 keeps capability
records in their 4 KiB page and publishes argv/environment vectors and strings
in a separate owned read-only payload. Their combined packed-string budget is
4,096 bytes including NULs. The payload and opaque exec handoff must fit before
the executable's first segment; failures retain the old image and resources.

`astra_application_launch_for_volume()` uses the same launch service and
encodes an exact volume reference as option, epoch, ID, and generation.
Callers should obtain that reference from a current volume snapshot; the
receiver must validate it against a fresh snapshot before acting. Desktop and
Atlas share this helper when opening a volume in another application.

Service manager wire version 7 keeps service identifiers at 63 bytes while
accepting full filesystem executable paths. Definition and runtime-info
objects own their executable strings; initialize them with their `*_INIT`
macros and destroy them, or use their scoped cleanup macros. Pointer-free
definition records and executable strings travel in readonly shared areas.
Malformed payloads fail before stored configuration or caller output changes.

```{doxygenfile} application.h
:project: astra-ndk
```

```{doxygenfile} application_catalog.h
:project: astra-ndk
```

```{doxygenfile} bundle.h
:project: astra-ndk
```

```{doxygenfile} manifest.h
:project: astra-ndk
```

```{doxygenfile} service_manager.h
:project: astra-ndk
```

```{doxygenfile} config_library.h
:project: astra-ndk
```

```{doxygenfile} command.h
:project: astra-ndk
```

```{doxygenfile} program.h
:project: astra-ndk
```

```{doxygenfile} library.h
:project: astra-ndk
```
