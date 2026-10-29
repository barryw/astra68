# Fonts application

Fonts uses the shared Interface Kit File Open Panel to select an import source.
The app obtains mounted Places from Volume Kit and loads directory entries with
the shared `sw/userspace/common/directory_snapshot` helper. The panel handles
navigation, search, view selection, and file selection; Fonts owns source I/O,
format and encoding validation, conversion, and installation. Mounted Places
refresh on Volume Kit notifications.

The implementation was reviewed against Haiku's
`src/kits/tracker/FilePanel.cpp`: its panel owns browsing and selection while
the caller receives a selected path and performs the operation. Astra adapts
that pattern to native VFS snapshots and capability-scoped Volume Kit access.
The import source is never inferred from its extension.

The shared Font Panel is a font *chooser*: it exposes face, strike, optional
role, and preview, but no source/license information or uninstall action.
Fonts retains its manager view so installed fonts remain inspectable and
removable, including fonts ineligible for system selection. Composing the
chooser into a separate system font selection action is possible once the
manager's selected face can be handed to the chooser without losing that
management context.
