# C2 collection view notes

Status: Columns, Table, Tree, icon Grid, and Pagination implemented over C1.

## Haiku read-only review

- `~/Git/haiku/src/kits/interface/ListView.cpp`: the list owns row selection
  and keyboard activation. Astra's C1 `AstraCollectionModel` and retained List
  remain the only item identity and selection implementation.
- `~/Git/haiku/src/kits/interface/OutlineListView.cpp`: expansion maps a full
  hierarchy to visible rows; left/right keys collapse or enter a branch. An
  Astra Tree needs explicit parent IDs and expansion state on top of the C1
  model, without replacing its selected item ID.
- `~/Git/haiku/src/kits/interface/ColumnListView.cpp`: columns are an
  arrangement of row fields and shared selection operations. Astra's Columns
  browser composes one C1 List and ScrollView per directory depth; each depth
  has its own caller-owned collection and scroll models.
- `~/Git/haiku/src/kits/interface/GridView.cpp`: this is a general grid
  layout wrapper, not a selectable icon collection. Astra's icon Grid needs
  C1 selection plus the existing `AstraIconTileInfo` painter and visible-cell
  clipping.

## Columns contract

`AstraColumnsInfo` creates one horizontal Container and a ScrollView/List pair
for each depth. Slot and ID layout is deterministic: root 0, ScrollView
`1 + 2*n`, List `2 + 2*n`. It rejects duplicate models or scroll models and
invalid ID/width totals. The caller owns each depth's items and navigates by
replacing a deeper List's items with `astra_interface_list_set_items`.
Selection is preserved by C1's stable item ID. Each ScrollView clips its List
and retains independent vertical position. The Interface Gallery shows a
two-depth browser under the Layout tab.

## Table contract

`AstraTableModel` borrows a C1 collection for row identity and selected ID.
Fixed-width `AstraTableColumn` headings and row-major trailing cells add
presentation without another selection store. Column zero uses the C1 label.
The retained Table uses the List's focus, pointer selection, activation,
keyboard navigation, scroll reveal, and row surface policy. Header hit testing
never selects a row. Only visible rows contribute draw-list commands. Changing
rows and cells is transactional and preserves the selected stable ID. Gallery
shows a two-column table beside the Columns browser; Beast tests cover header
hit, selection, rendering, reorder and invalid update rollback.

## Tree contract

`AstraTreeModel` borrows a C1 collection. Parallel preorder nodes carry stable
parent IDs and mutable expansion flags; the library computes depth and fills
the caller's visible-row workspace. Invalid parents, depth over 63, duplicate
IDs, and insufficient workspace are rejected. Selection stays in the C1
model. Collapsing an ancestor selects the nearest visible ancestor, and the
control's value is always a visible-row index. Arrow up/down and Home/End
move through visible rows; left/right collapse or expand branches and move to
parent/first child when already collapsed/expanded. The disclosure glyph is
pointer clickable, and changes emit `ASTRA_UI_ACTION_EXPANSION_CHANGED` with
the full collection item index. `astra_interface_tree_set_nodes` replaces the
hierarchy and items together, preserving selection by stable ID and restoring
the old state if validation or layout fails. Draw-list estimates and painting
iterate only clipped visible rows.

## Pagination contract

`AstraPaginationModel` borrows the C1 collection and stores only page size and
zero-based page index. `astra_interface_pagination_init` composes Container,
Previous Button, Label, and Next Button in four slots with consecutive IDs.
Applications handle button activation by calling
`astra_interface_pagination_set_page`; the helper updates the label and
disabled edges. `astra_interface_pagination_range` returns a half-open item
range and recalculates page count from the collection's current item count.
An empty collection has one empty page. Page changes do not alter C1 selected
item ID. The label uses alternating caller-owned buffers so a length-changing
update leaves the old text valid while layout runs. Beast host and MC68040
library gates cover the composition and range behavior.

## Icon Grid contract

`AstraIconGridModel` borrows one C1 collection and an equal-length parallel
array of optional 64x64 icon surfaces. Fixed column count and existing
`AstraIconTileInfo` client/desktop metrics determine tile geometry. Pointer
hit testing maps both axes to item indices; keyboard left/right moves one
item, up/down moves one row, and Home/End jumps to the ends. The C1 selected
ID remains the only selection store; `astra_interface_icon_grid_set_items`
replaces items and icons together with ID preservation and layout rollback.
The painter clips tile rows and columns to the viewport and reuses the shared
IconTile rendering path. Icon surfaces become hardware BLIT commands in the
retained view through `astra_surface_draw_blit`. Applications append a view
containing icons with `astra_draw_list_append_view_with_sources`, passing the
same borrowed surfaces as the approved source array. The plain append call
intentionally rejects them. Graphics/System bridge details and handle checks
are documented in `ndk/docs/source/graphics.md`.

## Remaining contracts

- Table sorting remains an application command: callers reorder C1 items and
  matching cells in one `astra_interface_table_set_rows` call. Resizable
  columns, column-specific alignment, and secondary sort indicators remain
  design gaps.
