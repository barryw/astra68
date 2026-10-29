# Interface Field family

The design reference is `~/Downloads/Astra Desktop (standalone).html`,
Controls chapters 12a, 12b, and 12d. Haiku was reviewed read-only:
`~/Git/haiku/src/kits/interface/TextControl.cpp` splits field layout from
the editable text child, while `ColorControl.cpp` composes numeric text
controls around a color model. Astra keeps a single Field/TextModel editing
path and composes Fields, Labels, and Buttons in caller-owned control arrays.
The Colour Well is a retained activatable leaf because a live swatch cannot
be represented by a stock Label or Button without private paint policy.

Secure Field sends mask glyphs to the hardware draw list and suppresses
copy/cut actions. It does not provide protected storage: Astra currently has
no credential service, and the application owns its TextModel memory. A
credential service and secure model ownership remain a separate security
gate before using Secure Field for passwords or tokens.

Search Field adds a shared search glyph and clear affordance. Escape and the
clear hit target use the existing TextModel replacement path. Suggestion
dropdowns should bind the Popup/Combo Box menu lifecycle, not add a new
search-specific popover.

Segmented Field uses a Container with Fields and Labels. Tag Input uses a
wrapped Container, removable Tag buttons, and an inline Field. Caller-owned
models and ordinary `AstraUIAction` events keep validation and persistence
outside the controls. Colour Well stores a hardware RGB565 swatch and a
binding mode; a theme binding resolves on each render. Palette binding stores
the resolved swatch plus slot identity until a shared Colour panel supplies
palette editing.

Gallery's Fields page exercises all five controls. Beast tests cover masked
draw text, blocked copy/cut, search clear, segment/tag composition, Colour
Well drawing and activation. UI commands remain hardware draw-list operations.
