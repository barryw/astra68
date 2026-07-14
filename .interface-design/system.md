# AstraVM Interface System

## Direction

AstraVM is a purpose-built reference instrument for the person designing,
debugging, and eventually using Astra 68. It should feel like black-anodized
laboratory hardware with unusually careful typography: dense, calm, exact, and
native to this one machine. It is not a generic emulator dashboard and does not
imitate a green-phosphor terminal.

Domain vocabulary: boot ROMs, FPGA front panels, exception traces, star charts,
machine inventories, raster displays, address buses, and logic analyzers.

The signature is the **boot constellation**: the software-visible power-on path
drawn as a vertical linked sequence beside the actual Vega display. It is the
primary pattern for ordered machine state, not a generic progress stepper.

## Color world

The palette comes from black anodized chassis, graphite PCB solder mask,
starlight lettering, ion-cyan instrument light, amber cautions, and muted red
fault lamps. Color communicates machine state; it is never decorative.

| Token | RGB | Use |
| --- | --- | --- |
| `VOID` | `8, 11, 15` | deepest display and canvas void |
| `CHASSIS` | `13, 17, 23` | application chassis |
| `PANEL` | `17, 23, 31` | primary instrument surfaces |
| `PANEL_RAISED` | `21, 28, 37` | controls and raised surfaces |
| `INSET` | `5, 8, 11` | displays and content receivers |
| `BORDER_SOFT` | `38, 48, 59` | normal separation |
| `BORDER_EMPHASIS` | `57, 72, 84` | hover and important boundaries |
| `STARLIGHT` | `216, 225, 232` | primary text |
| `TEXT_SECONDARY` | `157, 172, 184` | supporting text |
| `TEXT_TERTIARY` | `111, 126, 139` | metadata |
| `TEXT_MUTED` | `73, 87, 99` | disabled and quiet context |
| `ION` | `105, 209, 216` | active, passed, selected, focus |
| `ION_DIM` | `37, 87, 94` | active control fill |
| `AMBER` | `205, 164, 91` | warnings and pending attention |
| `FAULT` | `211, 104, 100` | errors and failed hardware |

## Depth and geometry

- Borders-only depth. Do not add drop shadows, gradients, glow, or glass.
- Higher surfaces become only slightly lighter; inputs and displays are inset.
- Standard boundaries use a 1 px soft border. Focus and active boundaries use
  a 1 px ion or emphasis border.
- Corners stay technical: 2 px for panels and controls. Avoid pill-shaped
  containers except when a tiny status label truly needs one.
- The spacing base unit is 4 px. Common steps are 4, 8, 12, 16, 24, and 32 px.

## Typography

- Proportional text names controls, sections, and human-readable state.
- Monospace text represents machine data: addresses, cycles, versions,
  register values, console cells, and build identifiers.
- Maintain four text levels: starlight primary, secondary, tertiary, and muted.
- Default sizes: 20 px heading, 14 px body, 13 px button/monospace, 11 px small.

## Reusable patterns

### Vega display

The emulated display is the visual center of gravity and preserves the guest
aspect ratio. It sits inside a quiet bezel and inset raster surface. Guest text
comes from machine state; host UI labels never intrude into the raster.

### Boot constellation

Use for ordered boot stages and other causal machine sequences. Nodes share a
thin vertical bus. Passed/active state uses ion cyan, pending state is muted,
warnings use amber, and failures use fault red. Each node has a proportional
label and a monospace hardware detail line.

### Machine ledger

Use a single horizontal ledger beneath the display for stable identity facts
such as CPU, PMMU, memory, and build ID. Labels are tertiary proportional text;
values are secondary monospace text. Do not split these into dashboard cards.

### Chassis header and footer

The header establishes machine identity, model status, and immediate controls.
The footer carries low-priority live instrumentation such as virtual cycles and
renderer backend. Both belong to the chassis and use borders for separation.

### State controls

Controls are compact rectangular instrument switches. Every control has normal,
hover, active, focus, and disabled treatment. Ion cyan means an affirmative
machine state—not generic decoration.

## Rejected defaults

- Generic sidebar navigation: use the display/constellation instrument layout.
- Metric-card grids: use the machine ledger and data placed near its cause.
- Fake CRT nostalgia: show the actual Vega raster with restrained scanlines.
- Rainbow subsystem colors: use one ion accent plus semantic amber/fault states.
- UI-driven emulation: machine time stays in a dedicated worker and crosses
  bounded snapshot channels.
