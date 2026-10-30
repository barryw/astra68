# Interface Gallery coverage

`InterfaceGallery.app` is Interface Kit's running control showcase. Open it from
Applications on Astra. Each tab uses the same public control constructors and
application bundle that other clients use. Sample data in Gallery is local to
the app; Open and Save previews report selected paths but perform no file I/O.

| Tab | Shared controls shown |
| --- | --- |
| Input | Label, Button variants and states, Field and validation |
| Choice | Checkbox, Radio, Switch, Segmented, Disclosure, List, Popup, Toolbar Actions, Split Button, Combo Box, Multi Popup |
| Value | Slider, Dial, Stepper |
| Progress | Progress, Level Meter, Sparkline, Status Bar |
| Text | Text View, fixed-cell Grid View, read-only Field |
| Layout | Container, Scroll View, Scrollbar, Splitter, custom View, Image View, Form Row |
| Collections | Columns, Table, Tree, Pagination, Icon Grid |
| Fields | Secure Field, Search Field, Segmented Field, Tag Input, Colour Well |
| Panels | Colour, Open, Save, and Font panels; Menu Bar, Context Menu, Alert, Sheet, Tooltip, Notification |
| Drag | Drag source and two Drop Targets, including accepted and refused drops |

Every `ASTRA_CONTROL_*` kind in `ndk/include/astra/control.h` has a visible
example. Open, Save, and Font panels are Interface Kit compositions; the file
panels also show shared breadcrumb navigation. Font panel reads the installed
Font Kit catalog and displays actual faces and strikes. Gallery's Menu Bar is
visible whenever its window has focus. Right-click the Panels tab for its
Context Menu; buttons there open other system overlays. Icon Grid uses three
hardware-drawn fixture icons so its tile image path is visible too.

Gallery's `--benchmark` argument exercises target-side layout and interaction
paths and prints `INTERFACE` records. Ordinary desktop launch runs only the
interactive showcase.
