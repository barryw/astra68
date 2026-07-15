# Vesta — System / IRQ / Timers / I/O Register Map (v0.1)

Vesta is the Astra 68 system-glue chip (Gary/Gayle analog): the interrupt
controller every other chip routes into, timers, FTDI diagnostic console,
AstraHost boot state, input, machine identity, and reset control.

> Process translation and protection use the CPU's built-in MC68030 PMMU. The
> former Vesta region-MMU aperture and contract are retired. Their published
> numeric identifiers remain reserved but no active build implements them.

Authoritative contract; `sw/include/vesta.h` is the hand-maintained C mirror.

---

## 1. Addressing & conventions

- **Base:** `VESTA_BASE = 0xFFF00000` (also the copper `MOVE` offset origin).
- 32-bit registers, 4-byte stride, big-endian; RO / RW / RW1C.
- System registers are supervisor-only. The isolated front-panel page may be
  mapped to user software by the operating system.

```
Block map (VESTA_BASE +):
  0x0000  system control (id / machine / reset / scratch)
  0x00D0  SDRAM power-on self-test
  0x0100  retired region-MMU / diagnostics / AstraHost boot state
  0x0200  retired region-MMU table aperture
  0x0300  interrupt controller
  0x0380  per-source IRQ config (32 x 4 bytes)
  0x0400  timers (2 x 16 bytes)
  0x0500  UART
  0x0600  SPI / SD
  0x0700  input (gamepads + keyboard)
  0x1000  front-panel GPIO (separate 4 KiB PMMU page)
```

---

## 2. System control (0x0000)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0000 | `ID` | RO | `0x56535441` | "VSTA" |
| 0x0004 | `VERSION` | RO | `0x00010000` | major/minor |
| 0x0008 | `MACHINE_ID` | RO | — | machine / board revision |
| 0x000C | `SYS_CTRL` | RW | 0 | `[0]SOFT_RESET` (write 1 = reboot) |
| 0x0010 | `SYS_STATUS` | RO | — | general status |
| 0x0014 | `RESET_REASON` | RO | — | `0=power-on 1=soft 2=watchdog` |
| 0x0018 | `SCRATCH` | RW | 0 | persists across soft reset (boot handoff) |
| 0x001C | `CPU_MODEL` | RO | `0x00068030` | MC68030 architectural target |
| 0x0020 | `CPU_IMPL` | RO | `TGM2` | repaired TG68K.C 68030/PMMU implementation |
| 0x0024 | `CPU_FEATURES` | RO | `0x0000000D` | `[0]PMMU [1]FPU [2]DATA32 [3]ADDR32` |
| 0x0028 | `CPU_HZ` | RO | — | configured CPU/bus clock in Hz |
| 0x002C | `RAM_BASE` | RO | — | physical SDRAM base advertised to boot software |
| 0x0030 | `RAM_SIZE` | RO | — | usable SDRAM bytes |
| 0x0034 | `ROM_BASE` | RO | — | physical boot ROM base |
| 0x0038 | `ROM_SIZE` | RO | — | boot ROM aperture bytes |
| 0x003C | `BUILD_ID` | RO | — | reproducible hardware build identifier |
| 0x0040 | `PERSONALITY_COUNT` | RO | — | valid hardware descriptors at `0x0050` |
| 0x0044 | `PERSONALITY_STRIDE` | RO | 16 | bytes per descriptor |
| 0x0048 | `NVRAM_CAPS` | RO | 0 | persistent-settings backend capabilities |

`SYS_STATUS` currently reports `[0]SDRAM_PRESENT [1]SDRAM_READY
[2]BOOT_OVERLAY [3]VIDEO_PLL_LOCKED [4]SD_CONTROLLER [5]ASTRA_HOST`.

Each personality descriptor is four read-only words: `ID` FourCC, `VERSION`
(16.16 major/minor), physical `BASE`, and MMIO aperture `SIZE`. The boot ROM
prints this table rather than claiming chips that are not instantiated in the
current bitstream.

### SDRAM power-on self-test (0x00D0)

The destructive full-range test runs from the 75 MHz SDRAM clock domain over
the controller's pipelined 32-bit DMA port. It keeps up to 16 requests in
flight, avoiding both the CPU clock-domain round trip and the former per-byte
request path. Boot software must keep its code, stack, and live data outside
SDRAM until the test completes.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x00D0 | `MEMTEST_CTRL` | WO | write `[0]START=1` to start or restart |
| 0x00D4 | `MEMTEST_STATUS` | RO | `[0]BUSY [1]DONE [2]FAILED [10:8]PHASE` |
| 0x00D8 | `MEMTEST_PROGRESS` | RO | current byte offset |
| 0x00DC | `MEMTEST_ERRORS` | RO | saturating mismatch count |
| 0x00E0 | `MEMTEST_FIRST_FAIL` | RO | first failing byte offset |
| 0x00E4 | `MEMTEST_EXPECTED` | RO | expected byte for first failure |
| 0x00E8 | `MEMTEST_ACTUAL` | RO | actual byte for first failure |
| 0x00EC | `CPU_CYCLES_LO` | RO | low word of free-running CPU/bus clock count |
| 0x00F0 | `CPU_CYCLES_HI` | RO | high word of free-running CPU/bus clock count |
| 0x00F4 | `ICACHE_HITS` | RO | TG wrapper instruction-cache hit count |
| 0x00F8 | `ICACHE_MISSES` | RO | TG wrapper instruction-cache miss count |
| 0x00FC | `DCACHE_HITS` | RO | TG wrapper data-cache hit count |

CPU memory diagnostics continue in the MMU-control aperture so the identity
block remains ABI-compatible:

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0110 | `DCACHE_MISSES` | RO | TG wrapper data-cache miss count |
| 0x0114 | `CPU_SDRAM_READS` | RO | CPU SDRAM read requests launched |
| 0x0118 | `CPU_SDRAM_WRITES` | RO | CPU SDRAM write requests launched |
| 0x011C | `CPU_SDRAM_WAIT` | RO | CPU clocks spent in the SDRAM wait state |
| 0x0120 | `SDRAM_LINE_HITS` | RO | external 16-byte line-buffer hits |
| 0x0124 | `SDRAM_LINE_MISSES` | RO | external 16-byte line fills |
| 0x0128 | `SDRAM_POSTED_WRITES` | RO | writes acknowledged before completion |

### AstraHost boot state (0x0130)

Production storage and ESP32 services use the SPI-only AstraHost transport
specified in `docs/ASTRAHOST.md`. These registers expose boot-engine state to
stage 0; they are not an alternate CPU-to-ESP byte transport.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0130 | `HOST_CTRL` | RO | `[0]BOOT_REQUESTED` |
| 0x0134 | `HOST_STATUS` | RO | `[0]REQUESTED [1]BUSY [2]DONE [3]ERROR [7]LINK_SEEN` |
| 0x0138 | `HOST_ROM_SIZE` | RO | validated payload bytes |
| 0x013C | `HOST_ROM_CRC32` | RO | validated payload CRC32 |
| 0x0140 | `HOST_INITIAL_SP` | RO | captured stage-2 reset SP |
| 0x0144 | `HOST_INITIAL_PC` | RO | captured stage-2 reset PC |
| 0x0148 | `HOST_BYTES_RECEIVED` | RO | streaming progress in bytes |
| 0x014C | `HOST_ERROR` | RO | AstraHost protocol status code |

Phases are `0=idle`, `1=write`, `2=read/verify`, and `3=done`. Starting the
test clears the prior result. POST writes and verifies an address-derived
pattern, then repeats with its complement. Every address line contributes to
stored data, and every bit in every byte must store both zero and one.

---

## 3. Retired region-MMU aperture (0x0100-0x02FF)

This section documents a retired interface for historical builds only. New
hardware reserves these offsets until the performance counters currently mixed
into this range are assigned a permanent home. New software uses the MC68030
PMMU and must not probe or program the former region table.

<!-- Historical interface retained temporarily for migration. -->

A region-based protection unit — **not** a paged MMU. The *active* process's
region table lives in these 16 hardware slots; the kernel reloads them on a
process switch (64 register writes). Every CPU access is checked in parallel.

### Control (0x0100)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0100 | `MMU_CTRL` | RW | `[0]ENABLE [1]SUPER_BYPASS` |
| 0x0104 | `MMU_FAULT_ADDR` | RO | logical address of the last fault |
| 0x0108 | `MMU_FAULT_STAT` | RO | fault reason + region (below) |
| 0x010C | `MMU_FAULT_ACK` | RW1C | write 1 to clear the latched fault |

- `ENABLE=0` → identity map, all access allowed (boot / MVP).
- `SUPER_BYPASS=1` (default) → supervisor accesses bypass the MMU (kernel runs
  unmapped with full physical access); user-mode accesses are region-checked.
  Set 0 to region-check the kernel too.

**`MMU_FAULT_STAT`** `[0]FAULT [1]NO_REGION [2]PERM_R [3]PERM_W [4]PERM_X
[5]USER_VIOLATION [12:8]matched region#`.

### Region table (0x0200) — 16 regions × 16 bytes

Region `i` at `0x0200 + i*0x10`:

| +Off | Name | Description |
|---|---|---|
| 0x00 | `RGN_BASE` | logical base address |
| 0x04 | `RGN_SIZE` | size in bytes; region covers `[base, base+size)` |
| 0x08 | `RGN_PHYS` | physical base address |
| 0x0C | `RGN_ATTR` | `[0]ENABLE [1]R [2]W [3]X [4]USER` |

**Translation** per access (logical `A`, mode from the CPU's supervisor/FC lines):
```
if !MMU_CTRL.ENABLE                       -> physical = A (identity), allow
if supervisor and MMU_CTRL.SUPER_BYPASS   -> physical = A, allow
find lowest-index enabled region i with RGN_BASE[i] <= A < RGN_BASE[i]+RGN_SIZE[i]
  none                        -> FAULT (NO_REGION)
  read  and !R                -> FAULT (PERM_R)
  write and !W                -> FAULT (PERM_W)
  fetch and !X                -> FAULT (PERM_X)
  user  and !USER             -> FAULT (USER_VIOLATION)
  else physical = RGN_PHYS[i] + (A - RGN_BASE[i]); bus cycle
```
First-match (lowest index) resolves overlaps → a broad region plus a specific
override. A region with `ENABLE=1` but no `R/W/X` is a **guard** (matches, always
faults) — use between stack and heap. A fault latches `FAULT_ADDR`/`FAULT_STAT`
and asserts the 68k **bus error** (vector 2); the handler inspects and acks.

MMIO/ROM are protected implicitly: the kernel simply never maps them into a user
region (or maps them without `USER`).

---

## 4. Interrupt controller (0x0300)

Aggregates device IRQs and the other chips' IRQ lines, drives the 68k IPL, and
supplies a vector on IACK. (Protection faults are **not** here — they are 68k bus
errors, §3.)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0300 | `IRQ_PENDING` | RO | raw pending bitmap (all sources) |
| 0x0304 | `IRQ_ENABLE` | RW | per-source mask |
| 0x0308 | `IRQ_SOFT` | RW | software-set pending (softirq / IPC) |
| 0x030C | `IRQ_ACK` | RW1C | clear edge-triggered pending |
| 0x0310 | `IRQ_CURRENT` | RO | `[2:0]` active IPL, `[7:0]`... top source, `[15:8]` its vector |
| 0x0380 | `IRQ_CFG[32]` | RW | per-source config: `[2:0]` IPL level (1-7), `[15:8]` vector, `[16]` edge |

**Sources** (bit index in the bitmaps / `IRQ_CFG` index):
```
0 TIMER0   1 TIMER1   2 UART_RX  3 UART_TX  4 SD  5 KEYBOARD  6 GAMEPAD
8 VEGA     9 ASTRAEA  10 LYRA
```
The chip lines (`VEGA`/`ASTRAEA`/`LYRA`) are the OR of that chip's own
`IRQ_STAT & IRQ_EN`; the handler reads the chip's `IRQ_STAT` for the exact event
(vblank vs raster, blit vs copper, …). The controller presents the highest-level
pending+enabled source on the IPL lines; ties break by source index.

---

## 5. Timers (0x0400) — 2 timers

Timer `n` at `0x0400 + n*0x10`. Down-counters from `LOAD` at `sysclk / prescale`.

| +Off | Name | Acc | Description |
|---|---|---|---|
| 0x00 | `TMR_LOAD` | RW | reload value |
| 0x04 | `TMR_VALUE` | RO | current count |
| 0x08 | `TMR_CTRL` | RW | `[0]ENABLE [1]PERIODIC [2]IRQ_EN [7:4]PRESCALE(log2)` |
| 0x0C | `TMR_STATUS` | RW1C | `[0]EXPIRED` |

Expiry raises `TIMER0`/`TIMER1` IRQ and, if `PERIODIC`, reloads. One-shot stops
at 0. Prescale is a power-of-two divide for µs…ms ranges.

---

## 6. UART (0x0500)

8N1 serial console (the memtest already exercises this path at 115200).
This port is wired to FTDI for diagnostics and hardware tests. It is not wired
to the ESP32 and is never an AstraHost transport.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0500 | `UART_DATA` | RW | read = pop RX byte; write = push TX byte |
| 0x0504 | `UART_STATUS` | RO | `[0]TX_READY [1]RX_VALID [2]TX_BUSY [3]RX_OVERRUN` |
| 0x0508 | `UART_CTRL` | RW | `[0]TX_IRQ_EN [1]RX_IRQ_EN` |
| 0x050C | `UART_BAUD` | RW | `[15:0]` divisor = sysclk / baud |

---

## 7. Direct SPI / SD recovery aperture (0x0600)

This low-level FPGA SPI master is present only in direct-recovery and simulation
builds with `ASTRA_HOST_ENABLE=0`. Software may drive the SD card protocol in
that build. Production builds release the shared SD pins to the ESP32 and use
the SPI-only AstraHost service; this aperture must not be used by production
software.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0600 | `SPI_CTRL` | RW | `[0]CS_N` (drive low = selected), `[7:4]CLKDIV` |
| 0x0604 | `SPI_STATUS` | RO | `[0]BUSY` |
| 0x0608 | `SPI_DATA` | RW | write = shift 8 bits (this MOSI byte); read = last MISO byte |

---

## 8. Input (0x0700)

Generic surface; the physical layer (PS/2, USB-HID via ESP32, GPIO pads) is
board-specific and feeds these registers.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0700 | `PAD0` | RO | gamepad 0 button bitmap |
| 0x0704 | `PAD1` | RO | gamepad 1 button bitmap |
| 0x0708 | `KEY_DATA` | RO | `[7:0]` scancode (read pops FIFO), `[8]` valid |
| 0x070C | `KEY_STATUS` | RO | `[0]RX_VALID [7:1]` FIFO count |
| 0x0710 | `INPUT_CTRL` | RW | IRQ enables etc. |

---

## 9. Front-panel GPIO (0x1000)

The ULX3S front panel has six software-visible pushbuttons, four DIP switches,
and eight LEDs. The seventh pushbutton remains the active-low hardware reset
and is not software-visible. Inputs are active-high logical values regardless
of board-level polarity.

This block occupies `0xFFF01000..0xFFF01FFF`, a separate 4 KiB page. Bare-metal
software accesses it through the Astra NDK. A protected OS normally brokers it
through NDK resource leases, but can delegate only this page to an explicitly
privileged client without exposing other Vesta controls.

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x1000 | `PANEL_ID` | RO | `0x504E4C30` | "PNL0" |
| 0x1004 | `PANEL_VERSION` | RO | `0x00010000` | major/minor |
| 0x1008 | `PANEL_CAPS` | RO | `0x0F040608` | `[31:24]features [23:16]switches [15:8]buttons [7:0]LEDs` |
| 0x100C | `PANEL_INPUT` | RO | 0 | debounced switches `[11:8]`, buttons `[5:0]` |
| 0x1010 | `PANEL_RAW_INPUT` | RO | 0 | synchronized, non-debounced input levels |
| 0x1014 | `PANEL_CHANGE` | RW1C | 0 | sticky debounced changes, same bit layout as input |
| 0x1018 | `PANEL_LED_DATA` | RW | 0 | software LED values `[7:0]` |
| 0x101C | `PANEL_LED_OWNERSHIP` | RW | 0 | 1 selects software value; 0 selects diagnostic value |
| 0x1020 | `PANEL_LED_SET` | WO | - | atomically set `PANEL_LED_DATA` bits |
| 0x1024 | `PANEL_LED_CLEAR` | WO | - | atomically clear `PANEL_LED_DATA` bits |
| 0x1028 | `PANEL_LED_TOGGLE` | WO | - | atomically toggle `PANEL_LED_DATA` bits |

Feature bits are `[0]RAW_INPUT`, `[1]CHANGE_LATCH`, `[2]LED_OWNERSHIP`, and
`[3]ATOMIC_LEDS`.

Button bits are `0=FIRE1`, `1=FIRE2`, `2=UP`, `3=DOWN`, `4=LEFT`, and
`5=RIGHT`. Switch bits are `0=SW1` through `3=SW4`.

`PANEL_LED_OWNERSHIP` resets to zero, preserving the hardware liveness and hang
diagnostics. The NDK leases individual bits and masks writes to the caller's
lease; disjoint applications can therefore use different LEDs. The supported
application interface is `astra/front_panel.h`; these register addresses are a
private firmware/hardware ABI.

---

## 10. Programming sketches

**Set up a user process's regions + switch to it**
```c
static void map(int i, uint32_t lbase, uint32_t size, uint32_t pbase, uint32_t attr) {
    VESTA->REGION[i].BASE = lbase;
    VESTA->REGION[i].SIZE = size;
    VESTA->REGION[i].PHYS = pbase;
    VESTA->REGION[i].ATTR = RGN_ENABLE | attr;
}
map(0, 0x00010000, p->text_sz, p->text_phys, RGN_RX);           // text: r-x
map(1, 0x00400000, p->heap_sz, p->heap_phys, RGN_RW);           // heap: rw-
map(2, 0x7FFF0000, 0x10000,    p->stack_phys, RGN_RW);          // stack: rw-
map(3, 0x7FFE0000, 0x10000,    0,             0);               // guard: fault
for (int i = 4; i < 16; i++) VESTA->REGION[i].ATTR = 0;         // disable rest
VESTA->MMU_CTRL = MMU_ENABLE | MMU_SUPER_BYPASS;
// ... rte into user mode ...
```

**Bus-error (protection fault) handler**
```c
void bus_error(void) {
    uint32_t addr = VESTA->MMU_FAULT_ADDR;
    uint32_t why  = VESTA->MMU_FAULT_STAT;
    if ((why & MMUF_PERM_W) && in_stack_guard(addr)) grow_stack(addr);
    else kill_current_process(addr, why);
    VESTA->MMU_FAULT_ACK = MMUF_FAULT;
}
```

**Configure vblank IRQ at level 6, periodic timer at level 4**
```c
VESTA->IRQ_CFG[IRQ_SRC_VEGA]   = IRQ_CFG_LEVEL(6) | IRQ_CFG_VECTOR(80);
VESTA->IRQ_CFG[IRQ_SRC_TIMER0] = IRQ_CFG_LEVEL(4) | IRQ_CFG_VECTOR(81);
VESTA->IRQ_ENABLE = IRQ_BIT(IRQ_SRC_VEGA) | IRQ_BIT(IRQ_SRC_TIMER0);

VESTA->TIMER[0].LOAD = sysclk_hz / 1000;                 // 1 kHz tick
VESTA->TIMER[0].CTRL = TMR_ENABLE | TMR_PERIODIC | TMR_IRQ_EN;
```

**UART putc**
```c
while (!(VESTA->UART_STATUS & UART_TX_READY)) ;
VESTA->UART_DATA = c;
```

---

## 11. Decisions & open

**Decided:** process protection uses the CPU's built-in paged PMMU · the Vesta
region unit is retired · per-source IRQ level+vector config · aggregated chip
IRQ lines (read the chip's `IRQ_STAT` for detail).

**Open:**
1. Permanent location and ABI for CPU/cache/PMMU performance counters currently
   exposed in the retired aperture.
2. AstraHost raw multi-sector SPI service and FPGA DMA interface details.
3. Watchdog timer (for `RESET_REASON=watchdog`).
4. `SCRATCH` persistence across soft reset — how many words for boot handoff.
5. Whether central DMA-fence configuration and fault reporting belong in Vesta
   or Astraea.
