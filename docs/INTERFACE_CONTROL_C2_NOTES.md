# C2 collection view notes

Status: Columns implemented over C1; Table, Tree, icon Grid, and Pagination
remain open.

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

## Remaining contracts

- Table needs a typed column/cell descriptor over C1 item IDs, a shared row
  header and sort command policy, and visible-row rendering. The existing List
  stores one label per row and cannot represent additional cells correctly.
- Tree needs parent identity, expansion state, visible-row mapping, and
  collapse/reveal semantics without dropping C1 selected IDs.
- Icon Grid needs `AstraIconTileInfo` in the retained control pipeline with
  selected-item hit testing and two-dimensional keyboard navigation. Current
  `ASTRA_CONTROL_GRID_VIEW` is a fixed-cell text surface and must remain
  distinct.
- Pagination should be a composed group of existing Buttons/Label over a
  C1 collection model, with a caller-owned page state and one shared visible
  range helper. It must not introduce another selection store.
