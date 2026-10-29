# C2 collection view notes

Status: Columns, Table, and Tree implemented over C1; icon Grid and
Pagination remain open.

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

## Remaining contracts

- Table sorting remains an application command: callers reorder C1 items and
  matching cells in one `astra_interface_table_set_rows` call. Resizable
  columns, column-specific alignment, and secondary sort indicators remain
  design gaps.
- Icon Grid needs `AstraIconTileInfo` in the retained control pipeline with
  selected-item hit testing and two-dimensional keyboard navigation. Current
  `ASTRA_CONTROL_GRID_VIEW` is a fixed-cell text surface and must remain
  distinct.
- Pagination should be a composed group of existing Buttons/Label over a
  C1 collection model, with a caller-owned page state and one shared visible
  range helper. It must not introduce another selection store.
