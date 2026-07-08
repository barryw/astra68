# Vesta — System / MMU / IRQ / Timers / I/O Register Map (v0.1)

Vesta is the Astra 68 system-glue chip (Gary/Gayle analog): the region-based
protection MMU, the interrupt controller every other chip routes into, timers,
UART console, SD/SPI storage, and input. It also holds machine identity and
reset control.

Authoritative contract; `sw/include/vesta.h` is the hand-maintained C mirror.

---

## 1. Addressing & conventions

- **Base:** `VESTA_BASE = 0xFFF00000` (also the copper `MOVE` offset origin).
- 32-bit registers, 4-byte stride, big-endian; RO / RW / RW1C. Supervisor-only.

```
Block map (VESTA_BASE +):
  0x0000  system control (id / machine / reset / scratch)
  0x0100  MMU control + fault reporting
  0x0200  MMU region table (16 regions x 16 bytes)
  0x0300  interrupt controller
  0x0380  per-source IRQ config (32 x 4 bytes)
  0x0400  timers (2 x 16 bytes)
  0x0500  UART
  0x0600  SPI / SD
  0x0700  input (gamepads + keyboard)
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

---

## 3. Region MMU (0x0100 control, 0x0200 regions)

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

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0500 | `UART_DATA` | RW | read = pop RX byte; write = push TX byte |
| 0x0504 | `UART_STATUS` | RO | `[0]TX_READY [1]RX_VALID [2]TX_BUSY [3]RX_OVERRUN` |
| 0x0508 | `UART_CTRL` | RW | `[0]TX_IRQ_EN [1]RX_IRQ_EN` |
| 0x050C | `UART_BAUD` | RW | `[15:0]` divisor = sysclk / baud |

---

## 7. SPI / SD (0x0600)

Low-level SPI master; software drives the SD card protocol (SPI mode). Block-DMA
to SDRAM is a future enhancement (§10).

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

## 9. Programming sketches

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

## 10. Decisions & open

**Decided:** 16 hardware region slots, kernel-reloaded on switch (no page walk) ·
first-match overlap resolution · supervisor-bypass default · faults = 68k bus
error with latched addr+reason · per-source IRQ level+vector config · aggregated
chip IRQ lines (read the chip's `IRQ_STAT` for detail).

**Open:**
1. Region **count** — 16 vs 8 (LUT / translation-latency tradeoff; confirm after
   WF68K30L timing).
2. Fast process switch — a **second region bank** to double-buffer (load next
   process's table while current runs) vs plain 64-write reload.
3. SD **block-DMA engine** (auto DMA a 512-byte block to SDRAM) vs software SPI.
4. Watchdog timer (for `RESET_REASON=watchdog`).
5. `SCRATCH` persistence across soft reset — how many words for boot handoff.
6. Paging path (future) — regions could gain a "page table pointer" mode later.
