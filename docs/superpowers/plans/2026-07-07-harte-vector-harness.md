# Harte Vector Test Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stream Tom Harte 68000 register-only data-processing vectors into the WF68K30L core on the ULX3S and verify final regs+CCR against expected, as a permanent silicon regression net.

**Architecture:** A resident 68k test-executive in ROM polls a new UART RX port, decodes one case (regs, CCR, instruction bytes), loads state via `MOVEM` + `MOVE #ccr,CCR`, single-steps the relocated instruction (instruction + trailing `JMP dump`), dumps final regs+CCR back over UART TX. A Python host on the Mac parses Harte JSON, filters to the in-scope subset, streams cases, and compares.

**Tech Stack:** SystemVerilog (SoC + uart_rx), yosys/nextpnr/ecppack (oss-cad-suite, local Mac ECP5 flow), GNU m68k assembly (`m68k-linux-gnu-` on beast), Python 3 + pyserial (host, on Mac), Questa (beast) / iverilog (Mac) for HDL sim.

## Global Constraints

- Serial: **115740 baud** on `/dev/cu.usbserial-D01457` (3.125 MHz core / 27; macOS FTDI VCP rejects exactly 115200). 8N1.
- SoC clock = 25 MHz / 8 = **3.125 MHz**; `uart_tx`/`uart_rx` params `CLK_HZ=3125000, BAUD=115200`.
- Memory map: ROM `0xFFE00000` (`ROM_WORDS=2048`, 8 KB), RAM `0x01FF8000..0x01FFFFFF` (32 KB, `sel_ram = cpu_adr[31:15]==17'h03FF`), UART `0xFFF00500` (`sel_uart = cpu_adr[31:8]==24'hFFF005`).
- 68k is **big-endian**. All multi-byte values on the wire are big-endian (MSB first).
- Build flow (Mac): `fpga/soc/oss_flow/mkbit.sh <rom.hex> <tag>` → `astra.bit`; flash `openFPGALoader --board ulx3s -f astra.bit`; **power-cycle** to reset the FT231X to UART mode before reading.
- Firmware build (beast `~/astra_st/`): `m68k-linux-gnu-gcc -m68020 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib -I include -T astra_st.ld -o X.elf crt0.S X.c ...` → `objcopy -O binary` → `python3 bin2hex.py X.bin rom_init.hex`.
- Instruction relocation base `harness_pc = 0x01FFC000` (in RAM, executable). Case buffers below it.
- Scope: register-only DP cases; **exclude** memory addressing modes, A7-as-operand, privileged, exception-generating, `initial.sr` T-bit set, and undefined CCR flags (per-opcode mask).
- No new host dependencies beyond `pyserial` (already installed).

---

## Wire Protocol (shared contract — referenced by host and harness tasks)

Big-endian fields. `CKSUM` = (sum of all bytes from CMD through last payload byte) mod 256.

**RUN (host→FPGA):**
```
0x55  LEN  0x01  d0..d7[32]  a0..a6[28]  ccr[1]  ilen[1]  instr[ilen]  CKSUM[1]
```
- `d0..d7`: 8 × uint32 BE. `a0..a6`: 7 × uint32 BE. `ccr`: low byte of initial SR. `ilen`: instruction length in bytes (2–10). `instr`: raw instruction bytes (already reconstructed from Harte prefetch+ram by the host).
- `LEN` = number of bytes after LEN, through CKSUM inclusive.

**RESULT (FPGA→host):**
```
0xAA  LEN  0x81  d0..d7[32]  a0..a6[28]  ccr[1]  CKSUM[1]
```

**PING (host→FPGA):** `0x55 0x02 0x00 <byte> CKSUM` → **PONG:** `0xAA 0x02 0x80 <byte> CKSUM` (echo).

Register order (matches `MOVEM.L …,d0-d7/a0-a6`): d0,d1,…,d7,a0,…,a6.

---

## File Structure

- `fpga/soc/uart_rx.sv` (new) — 8N1 UART receiver. One responsibility: deserialize rx line → byte + strobe.
- `fpga/soc/astra_soc.sv` (modify) — instantiate `uart_rx`; add RX data/status registers to the UART block.
- `fpga/soc/oss_flow/astra_soc.sv` (modify, build copy) — mirror the SoC change; the build reads this copy.
- `sw/harte/harness.S` (new) — resident test-executive (RX poll, decode, setup, single-step, dump, TX).
- `sw/harte/harness.ld` (new) — linker script (ROM 8 KB @ 0xFFE00000, RAM @ 0x01FF8000, defines buffers + `harness_pc`).
- `sw/harte/host/proto.py` (new) — frame encode/decode + checksum (pure, unit-tested).
- `sw/harte/host/harte_case.py` (new) — Harte JSON → filter + reconstruct instruction bytes (pure, unit-tested).
- `sw/harte/host/flags.py` (new) — per-opcode defined-CCR-flags mask.
- `sw/harte/host/harte_run.py` (new) — driver: serial I/O, stream cases, compare, report.
- `sw/harte/host/tests/` (new) — pytest for proto.py, harte_case.py.

---

## Phase 0 — UART RX + streaming skeleton

### Task 1: `uart_rx.sv` module

**Files:**
- Create: `fpga/soc/uart_rx.sv`
- Test: `fpga/soc/sim/tb_uart_rx.sv` (new, iverilog)

**Interfaces:**
- Produces: `module uart_rx #(parameter CLK_HZ=3125000, BAUD=115200) (input clk, input rst, input rx, output reg [7:0] data, output reg valid);` — `valid` pulses 1 clk when a byte completes; `data` holds it.

- [ ] **Step 1: Write the failing testbench**

Create `fpga/soc/sim/tb_uart_rx.sv`:
```systemverilog
`timescale 1ns/1ps
module tb_uart_rx;
  localparam CLK_HZ=3125000, BAUD=115200;
  localparam integer BITT = CLK_HZ/BAUD;           // clks per bit = 27
  reg clk=0, rst=1, rx=1; wire [7:0] data; wire valid;
  uart_rx #(.CLK_HZ(CLK_HZ), .BAUD(BAUD)) dut(.clk(clk),.rst(rst),.rx(rx),.data(data),.valid(valid));
  always #16 clk=~clk;                             // ~31.25ns => 32MHz-ish; timing is clk-count based
  integer i; reg [7:0] got; integer ngot=0;
  task send(input [7:0] b);
    integer k;
    begin
      rx=0; repeat(BITT) @(posedge clk);           // start bit
      for(k=0;k<8;k=k+1) begin rx=b[k]; repeat(BITT) @(posedge clk); end
      rx=1; repeat(BITT) @(posedge clk);           // stop bit
      repeat(BITT) @(posedge clk);
    end
  endtask
  always @(posedge clk) if(valid) begin got<=data; ngot=ngot+1; end
  initial begin
    repeat(4) @(posedge clk); rst=0; repeat(4) @(posedge clk);
    send(8'h55);
    if (got!==8'h55) begin $display("FAIL got=%02x want=55", got); $fatal; end
    send(8'hA3);
    if (got!==8'hA3) begin $display("FAIL got=%02x want=A3", got); $fatal; end
    if (ngot!==2) begin $display("FAIL ngot=%0d want=2", ngot); $fatal; end
    $display("PASS uart_rx"); $finish;
  end
endmodule
```

- [ ] **Step 2: Run to verify it fails**

Run: `source /opt/homebrew/oss-cad-suite/environment && cd fpga/soc && iverilog -g2012 -o /tmp/tbrx sim/tb_uart_rx.sv uart_rx.sv && vvp /tmp/tbrx`
Expected: FAIL — `uart_rx.sv` doesn't exist / no such file. (If iverilog absent, use beast Questa: `vlog -sv` + `vsim -c`.)

- [ ] **Step 3: Implement `uart_rx.sv`**

```systemverilog
// 8N1 UART receiver. Oversamples by counting clks/bit; samples at mid-bit.
module uart_rx #(parameter CLK_HZ=3125000, BAUD=115200) (
    input  wire clk, input wire rst, input wire rx,
    output reg [7:0] data, output reg valid);
    localparam integer DIV = CLK_HZ/BAUD;         // 27
    reg rx_s1=1'b1, rx_s2=1'b1;                    // 2-FF synchronizer
    always @(posedge clk) begin rx_s1<=rx; rx_s2<=rx_s1; end
    localparam S_IDLE=2'd0, S_START=2'd1, S_DATA=2'd2, S_STOP=2'd3;
    reg [1:0] st=S_IDLE; reg [15:0] cnt=0; reg [2:0] bit=0; reg [7:0] sh=0;
    always @(posedge clk) begin
        valid <= 1'b0;
        if (rst) begin st<=S_IDLE; cnt<=0; bit<=0; end
        else case (st)
            S_IDLE:  if (!rx_s2) begin st<=S_START; cnt<=DIV/2; end   // detect start, aim mid-bit
            S_START: if (cnt==0) begin
                        if (!rx_s2) begin st<=S_DATA; cnt<=DIV-1; bit<=0; end
                        else st<=S_IDLE;                              // false start
                     end else cnt<=cnt-1'b1;
            S_DATA:  if (cnt==0) begin
                        sh<={rx_s2, sh[7:1]}; cnt<=DIV-1;
                        if (bit==3'd7) st<=S_STOP; else bit<=bit+1'b1;
                     end else cnt<=cnt-1'b1;
            S_STOP:  if (cnt==0) begin data<=sh; valid<=1'b1; st<=S_IDLE; end
                     else cnt<=cnt-1'b1;
        endcase
    end
endmodule
```

- [ ] **Step 4: Run to verify it passes**

Run: `source /opt/homebrew/oss-cad-suite/environment && cd fpga/soc && iverilog -g2012 -o /tmp/tbrx sim/tb_uart_rx.sv uart_rx.sv && vvp /tmp/tbrx`
Expected: `PASS uart_rx`

- [ ] **Step 5: Commit** (if git initialized; else skip commit steps throughout)

```bash
git add fpga/soc/uart_rx.sv fpga/soc/sim/tb_uart_rx.sv && git commit -m "feat(soc): 8N1 uart_rx module + tb"
```

---

### Task 2: Wire UART RX into the SoC

**Files:**
- Modify: `fpga/soc/astra_soc.sv` (UART block near line 148–161, bus read mux near 195–197)
- Modify: `fpga/soc/oss_flow/astra_soc.sv` (identical change — this is the build copy)

**Interfaces:**
- Consumes: `uart_rx` (Task 1).
- Produces: memory-mapped registers in the UART block:
  - `0xFFF00500` write = TX data (existing), read bit0 = TX-ready (`~uart_busy`) (existing at `0xFFF00504`).
  - `0xFFF00508` read = **RX status**: bit0 = `rx_ready` (a byte is waiting).
  - `0xFFF0050C` read = **RX data**: low 8 bits = received byte; reading it clears `rx_ready`.

- [ ] **Step 1: Add the input port + rx logic**

In `fpga/soc/astra_soc.sv`, add `input wire ftdi_txd,` to the port list (FTDI TXD → FPGA RX). After the `uart_tx` instance, add:
```systemverilog
    // UART RX (host -> FPGA). rx_ready set on byte arrival, cleared on data read.
    wire [7:0] rx_byte; wire rx_valid;
    uart_rx #(.CLK_HZ(3125000), .BAUD(115200)) urx (.clk(clk), .rst(rst), .rx(ftdi_txd),
        .data(rx_byte), .valid(rx_valid));
    reg [7:0] rx_data; reg rx_ready;
    wire rx_data_rd = sel_uart & (cpu_adr[3:0]==4'hC) & cpu_data_en & cpu_rw_n & bus_write_stb;
    always @(posedge clk) begin
        if (rst) rx_ready <= 1'b0;
        else begin
            if (rx_valid) begin rx_data <= rx_byte; rx_ready <= 1'b1; end
            if (rx_data_rd) rx_ready <= 1'b0;         // read of 0x...C consumes the byte
        end
    end
```
Note: `bus_write_stb` pulses for both reads and writes at BS_WAIT (see FSM); gate reads with `cpu_rw_n`. (Verify `cpu_rw_n`=1 for reads in this SoC.)

- [ ] **Step 2: Extend the UART read mux**

Change `uart_rdata` (currently only `0x4`) to:
```systemverilog
    wire [31:0] uart_rdata =
        (cpu_adr[3:0]==4'h4) ? {30'd0, uart_busy, ~uart_busy} :   // TX status (existing)
        (cpu_adr[3:0]==4'h8) ? {31'd0, rx_ready}            :     // RX status
        (cpu_adr[3:0]==4'hC) ? {24'd0, rx_data}             :     // RX data
        32'd0;
```

- [ ] **Step 3: Add the pin constraint**

In `fpga/soc/astra_soc.lpf` add (ULX3S ftdi_txd = FPGA input, pin **M1**):
```
LOCATE COMP "ftdi_txd" SITE "M1";
IOBUF  PORT "ftdi_txd" PULLMODE=UP IO_TYPE=LVCMOS33;
```
Copy the same into `fpga/soc/oss_flow/` if a separate lpf is used there (the build uses `fpga/soc/astra_soc.lpf` via mkbit.sh — confirm and mirror).

- [ ] **Step 4: Mirror into the build copy**

Apply Steps 1–2 identically to `fpga/soc/oss_flow/astra_soc.sv`. Copy `uart_rx.sv` into `fpga/soc/oss_flow/` (mkbit.sh reads SV from its own dir): `cp fpga/soc/uart_rx.sv fpga/soc/oss_flow/`. Add `read_verilog -sv uart_rx.sv` to `mkbit.sh`'s yosys line (before `astra_soc.sv`).

- [ ] **Step 5: Verify it builds loop-free**

Run: `cd fpga/soc/oss_flow && bash mkbit.sh rom_selftest.hex rxwire` (needs a rom_init.hex present; reuse the selftest hex).
Expected: `nextpnr rc=0 (0=routed, loop-free)` and `built astra.bit`.

- [ ] **Step 6: Commit**

```bash
git add fpga/soc/astra_soc.sv fpga/soc/astra_soc.lpf fpga/soc/oss_flow/ && git commit -m "feat(soc): map uart_rx RX status/data registers"
```

---

### Task 3: Harness skeleton (PING/PONG echo) + host ping

**Files:**
- Create: `sw/harte/harness.S`, `sw/harte/harness.ld`
- Create: `sw/harte/host/proto.py`, `sw/harte/host/tests/test_proto.py`
- Create: `sw/harte/host/ping.py`

**Interfaces:**
- Produces (proto.py): `frame(cmd:int, payload:bytes)->bytes` (adds SYNC 0x55, LEN, CKSUM); `parse(buf:bytes)->(cmd:int, payload:bytes)|None` (validates SYNC 0xAA + CKSUM); `SYNC_TX=0x55`, `SYNC_RX=0xAA`, `CMD_PING=0x02`, `CMD_RUN=0x01`.

- [ ] **Step 1: Write failing proto test**

`sw/harte/host/tests/test_proto.py`:
```python
from proto import frame, parse, CMD_PING
def test_frame_roundtrip():
    f = frame(CMD_PING, b"\xA3")
    assert f[0] == 0x55 and f[2] == CMD_PING
    # emulate device echo with RX sync
    rx = bytes([0xAA]) + f[1:2] + bytes([0x80]) + b"\xA3"
    rx += bytes([(sum(rx[2:]) & 0xFF)])
    cmd, payload = parse(rx)
    assert cmd == 0x80 and payload == b"\xA3"
def test_bad_checksum_rejected():
    assert parse(b"\xAA\x02\x80\xA3\x00") is None
```

- [ ] **Step 2: Run to verify fail**

Run: `cd sw/harte/host && python3 -m pytest tests/test_proto.py -q`
Expected: FAIL (no module `proto`).

- [ ] **Step 3: Implement proto.py**

```python
SYNC_TX=0x55; SYNC_RX=0xAA; CMD_PING=0x02; CMD_RUN=0x01
def _ck(b): return sum(b) & 0xFF
def frame(cmd, payload):
    body = bytes([cmd]) + payload
    ln = len(body) + 1                       # body + cksum
    return bytes([SYNC_TX, ln]) + body + bytes([_ck(body)])
def parse(buf):
    if len(buf) < 4 or buf[0] != SYNC_RX: return None
    ln = buf[1]; end = 2 + ln
    if len(buf) < end: return None
    body = buf[2:end-1]; ck = buf[end-1]
    if _ck(body) != ck: return None
    return body[0], body[1:]
```

- [ ] **Step 4: Run to verify pass**

Run: `cd sw/harte/host && python3 -m pytest tests/test_proto.py -q`
Expected: PASS (2 passed).

- [ ] **Step 5: Write the harness skeleton**

`sw/harte/harness.ld` (based on `sw/boot/astra_st.ld`; adds symbols):
```
OUTPUT_FORMAT("elf32-m68k") OUTPUT_ARCH(m68k) ENTRY(_start)
MEMORY { ROM (rx):ORIGIN=0xFFE00000,LENGTH=8K  RAM (rwx):ORIGIN=0x01FF8000,LENGTH=32K }
_stack_top = 0x01FFC000;                      /* supervisor stack top; case area above */
_harness_pc = 0x01FFC000;                     /* relocated instruction goes here */
SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > ROM
  .text : { *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); } > ROM
  .bss (NOLOAD) : { . = ALIGN(4); _bss_start=.; *(.bss .bss.*) *(COMMON) . = ALIGN(4); _bss_end=.; } > RAM
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
```
`sw/harte/harness.S` (skeleton — vectors + UART helpers + PING loop; RUN handling added in Task 4):
```gas
    .equ UART, 0xFFF00500
    .equ TXST, 0xFFF00504        | bit0 = tx ready
    .equ RXST, 0xFFF00508        | bit0 = rx ready
    .equ RXDT, 0xFFF0050C        | rx data (read clears ready)
    .section .vectors,"a",@progbits
    .balign 4
    .long _stack_top             | vec0 SSP
    .long _start                 | vec1 PC
    .rept 254
    .long _halt
    .endr
    .section .text
    .globl _start
_start:
    move.l  #_stack_top, %sp
    | ---- main loop: read a frame, dispatch ----
main:
    bsr     rx_byte              | expect SYNC 0x55
    cmp.b   #0x55, %d0
    bne     main
    bsr     rx_byte              | LEN -> d7
    move.b  %d0, %d7
    bsr     rx_byte              | CMD -> d6
    move.b  %d0, %d6
    cmp.b   #0x02, %d6           | PING?
    bne     main                 | (RUN=0x01 handled in Task 4)
    bsr     rx_byte              | ping byte -> d5
    move.b  %d0, %d5
    bsr     rx_byte              | cksum (ignore for skeleton)
    | ---- PONG: 0xAA LEN=2 CMD=0x80 byte cksum ----
    move.b  #0xAA, %d0 ; bsr tx_byte
    move.b  #0x02, %d0 ; bsr tx_byte
    move.b  #0x80, %d1 ; move.b %d1,%d0 ; bsr tx_byte
    move.b  %d5, %d0  ; bsr tx_byte
    move.b  #0x80, %d0 ; add.b %d5,%d0 ; bsr tx_byte    | cksum = 0x80+byte
    bra     main
| ---- helpers ----
rx_byte:                          | -> d0.b
    move.l  #RXST, %a0
1:  move.l  (%a0), %d0 ; btst #0, %d0 ; beq 1b
    move.l  #RXDT, %a0 ; move.l (%a0), %d0 ; and.l #0xFF, %d0
    rts
tx_byte:                          | d0.b ->
    move.l  %d0, -(%sp)
    move.l  #TXST, %a0
1:  move.l  (%a0), %d1 ; btst #0, %d1 ; beq 1b
    move.l  (%sp)+, %d0
    move.l  #UART, %a0 ; move.l %d0, (%a0)
    rts
_halt:  stop #0x2700
    bra _halt
    .section .note.GNU-stack,"",@progbits
```

- [ ] **Step 6: Build harness ROM + write host ping**

On beast: `scp` `harness.S`,`harness.ld` to `~/astra_st/`; build:
`m68k-linux-gnu-gcc -m68020 -nostdlib -ffreestanding -T harness.ld -o harness.elf harness.S && m68k-linux-gnu-objcopy -O binary harness.elf harness.bin && python3 bin2hex.py harness.bin rom_harness.hex`; `scp` `rom_harness.hex` back to `fpga/soc/oss_flow/`.
`sw/harte/host/ping.py`:
```python
import serial, sys, time
sys.path.insert(0,'.')
from proto import frame, parse, CMD_PING
p = serial.Serial('/dev/cu.usbserial-D01457', 115740, timeout=0.5)
p.reset_input_buffer()
p.write(frame(CMD_PING, bytes([0xA3])))
time.sleep(0.2); r = p.read(8); p.close()
print("sent PING 0xA3, got:", r.hex(), "->", parse(r))
```

- [ ] **Step 7: Build bitstream, flash, power-cycle, verify echo**

Run (Mac): `cd fpga/soc/oss_flow && bash mkbit.sh rom_harness.hex harness && openFPGALoader --board ulx3s -f astra.bit`; **ask the user to power-cycle**; then `cd sw/harte/host && python3 ping.py`.
Expected: `got: aa028023 ...` parsing to `cmd=0x80 payload=b'\xa3'` (PONG echoes 0xA3). This is the **Phase 0 exit criterion**: bidirectional streaming works on silicon.

- [ ] **Step 8: Commit**

```bash
git add sw/harte/ && git commit -m "feat(harte): uart_rx SoC wiring + harness PING/PONG skeleton + host proto"
```

---

## Phase 1 — per-case execution for ADD / MOVE / ASL

### Task 4: Host case model — filter + instruction reconstruction

**Files:**
- Create: `sw/harte/host/harte_case.py`, `sw/harte/host/tests/test_harte_case.py`

**Interfaces:**
- Produces: `class Case` with fields `d[8],a[7],ccr:int,ilen:int,instr:bytes` (initial) and `fd[8],fa[7],fccr:int` (final); `load(path)->Iterator[dict]` (streams JSON cases); `is_in_scope(raw:dict)->bool` (register-only, no A7 operand, no trace bit); `build_case(raw:dict)->Case` (reconstructs `instr` from `prefetch`+`ram` at pc..pc+len).
- Instruction reconstruction: words at `pc,pc+2` come from `prefetch[0],prefetch[1]`; words at `pc+4…` come from `ram` (big-endian byte pairs). `ilen` from `raw['length']`.
- Scope filter (Phase 1 heuristic): the opcode files are pre-selected register-only (ADD Dn/Dm, MOVE Dn/Dm, ASL/LSL #imm/Dn). Exclude a case if `initial.sr & 0x8000` (T bit). A7-operand exclusion: skip if the decoded instruction's register field selects A7 in an An operand — for Phase 1's three opcodes, filter by checking the opcode doesn't encode An=7 as a source/dest (documented per opcode in flags.py; ADD/MOVE/ASL register forms don't use An → no A7 cases).

- [ ] **Step 1: Write failing test** using a captured sample case (copy 3 cases from beast `~/astra_soc/harte/nop.json` into `tests/sample_nop.json` for parsing; and hand-write one ADD case):
```python
import json
from harte_case import build_case, is_in_scope
def test_reconstruct_instr_len2():
    raw = {"name":"add","initial":{"d0":1,"d1":2,"d2":0,"d3":0,"d4":0,"d5":0,"d6":0,"d7":0,
            "a0":0,"a1":0,"a2":0,"a3":0,"a4":0,"a5":0,"a6":0,"usp":0,"ssp":0,"sr":0,
            "pc":0x1000,"prefetch":[0xD041,0x4E71],"ram":[]},
           "final":{"d0":3,"d1":2,"d2":0,"d3":0,"d4":0,"d5":0,"d6":0,"d7":0,
            "a0":0,"a1":0,"a2":0,"a3":0,"a4":0,"a5":0,"a6":0,"usp":0,"ssp":0,"sr":0},
           "length":2}
    c = build_case(raw)
    assert c.ilen == 2 and c.instr == b"\xD0\x41"    # ADD.W D1,D0, big-endian
    assert c.d[0]==1 and c.fd[0]==3
def test_trace_case_out_of_scope():
    raw = {"initial":{"sr":0x8000}}                   # T bit set
    assert not is_in_scope(raw)
```

- [ ] **Step 2: Run → fail.** `cd sw/harte/host && python3 -m pytest tests/test_harte_case.py -q` → FAIL (no module).

- [ ] **Step 3: Implement harte_case.py**
```python
import json
from dataclasses import dataclass
@dataclass
class Case:
    d:list; a:list; ccr:int; ilen:int; instr:bytes
    fd:list; fa:list; fccr:int; name:str
def load(path):
    with open(path) as f:
        for raw in json.load(f): yield raw
def is_in_scope(raw):
    if raw["initial"].get("sr",0) & 0x8000: return False   # trace
    return True
def _instr_bytes(init, ilen):
    words = list(init["prefetch"])                          # [w0, w1]
    pc = init["pc"]; ram = {a:v for a,v in init.get("ram",[])}
    off = 4
    while len(words)*2 < ilen:
        hi = ram.get(pc+off,0); lo = ram.get(pc+off+1,0)
        words.append((hi<<8)|lo); off += 2
    b = b"".join(int(w).to_bytes(2,"big") for w in words)
    return b[:ilen]
def build_case(raw):
    i, f = raw["initial"], raw["final"]
    d=[i["d%d"%k] for k in range(8)]; a=[i["a%d"%k] for k in range(7)]
    fd=[f["d%d"%k] for k in range(8)]; fa=[f["a%d"%k] for k in range(7)]
    return Case(d,a,i["sr"]&0xFF,raw["length"],_instr_bytes(i,raw["length"]),
                fd,fa,f["sr"]&0xFF,raw.get("name",""))
```

- [ ] **Step 4: Run → pass.** Expected: 2 passed.

- [ ] **Step 5: Commit.** `git add sw/harte/host/harte_case.py sw/harte/host/tests/ && git commit -m "feat(harte): host case model + instruction reconstruction"`

---

### Task 5: Harness RUN handler (setup → single-step → dump)

**Files:**
- Modify: `sw/harte/harness.S` (replace the `bne main` after PING with RUN dispatch + handler)

**Interfaces:**
- Consumes: RUN frame (protocol), `_harness_pc=0x01FFC000`.
- Produces: RESULT frame after executing one instruction.
- RAM layout (all below 0x01FFC000; supervisor stack grows down from 0x01FFBF00): `regtable` @ `0x01FFB000` (60 bytes: d0..d7,a0..a6), `ccr_in` @ `0x01FFB040`, `harness_pc` @ `0x01FFC000` (instr + JMP dump), `resultbuf` @ `0x01FFB100` (60 bytes).

- [ ] **Step 1: Implement the RUN handler** — add to `harness.S` (dispatch when `d6==0x01`):
```gas
    .equ REGT, 0x01FFB000
    .equ CCRI, 0x01FFB040
    .equ HPC,  0x01FFC000
    .equ RBUF, 0x01FFB100
run_case:                          | d7=LEN already read; read payload
    | read 60 reg bytes into REGT
    move.l  #REGT, %a1 ; move.w #59, %d3
1:  bsr rx_byte ; move.b %d0,(%a1)+ ; dbf %d3,1b
    bsr rx_byte ; move.l #CCRI,%a1 ; move.b %d0,(%a1)   | ccr
    bsr rx_byte ; move.b %d0,%d4                        | ilen
    | copy ilen instr bytes to HPC
    move.l  #HPC, %a1 ; move.b %d4,%d3 ; subq.b #1,%d3
2:  bsr rx_byte ; move.b %d0,(%a1)+ ; dbf %d3,2b
    | append JMP (dump).L  = 0x4EF9 <addr>
    move.w  #0x4EF9,(%a1)+ ; move.l #dump,(%a1)+
    bsr rx_byte                                         | cksum (ignored)
    | ---- load state: set CCR first, then MOVEM all regs (MOVEM does NOT affect CCR) ----
    move.b  CCRI, %d0                                   | ccr byte -> d0.b
    move.w  %d0, %ccr                                   | MOVE to CCR (word op, uses low byte)
    movem.l REGT, %d0-%d7/%a0-%a6                       | load 15 regs; CCR preserved
    jmp     HPC                                          | execute the relocated test instruction
dump:                               | reached via JMP after the test instruction
    movem.l %d0-%d7/%a0-%a6, RBUF                       | save 15 regs (absolute, no clobber)
    move.w  %sr, %d0                                     | full SR (supervisor)
    move.b  %d0, RBUF+60                                 | store CCR byte at RBUF[60]
    | ---- TX RESULT: 0xAA LEN=63 0x81 <60 regs> <ccr> cksum ----
    move.b  #0xAA,%d0 ; bsr tx_byte
    move.b  #63,%d0   ; bsr tx_byte
    clr.l   %d2                                          | cksum accum
    move.b  #0x81,%d0 ; bsr tx_byte ; add.b %d0,%d2
    move.l  #RBUF,%a2 ; move.w #60, %d3                  | 61 bytes: 60 regs + ccr
3:  move.b  (%a2)+,%d0 ; bsr tx_byte ; add.b %d0,%d2 ; dbf %d3,3b
    move.b  %d2,%d0   ; bsr tx_byte                      | cksum
    jmp     main                                          | next case
```
**Correctness note:** CCR cannot be set from a compile-time immediate (the value is per-case data). Use the `MOVE Dn,CCR` form (word op, low byte). Ordering: set CCR, then `MOVEM` the regs (MOVEM leaves CCR untouched), then `JMP`. The `dump` routine saves regs via `MOVEM.L …,(RBUF).L` **before** reading `SR` (so no scratch register clobbers a live value, and CCR is captured intact — `JMP` does not affect CCR).

- [ ] **Step 2: Wire dispatch** — in `main`, after reading CMD into d6: `cmp.b #0x01,%d6 ; beq run_case` (before the PING check).

- [ ] **Step 3: Build the harness ROM and verify one hand-built case on silicon**

Build (beast): `scp harness.S back; m68k-linux-gnu-gcc -m68020 -nostdlib -ffreestanding -T harness.ld -o harness.elf harness.S && objcopy -O binary harness.elf harness.bin && python3 bin2hex.py harness.bin rom_harness.hex`; scp `rom_harness.hex` → `fpga/soc/oss_flow/`. Build+flash: `cd fpga/soc/oss_flow && bash mkbit.sh rom_harness.hex harness && openFPGALoader --board ulx3s -f astra.bit`; **user power-cycles**.

Create `sw/harte/host/runone.py` (sends ADD.W D1,D0 with D0=1,D1=2 → expect D0=3):
```python
import serial, struct, sys
sys.path.insert(0,'.')
from proto import frame, parse, CMD_RUN
d=[1,2,0,0,0,0,0,0]; a=[0]*7; ccr=0; instr=b"\xD0\x41"   # ADD.W D1,D0
pl=b"".join(struct.pack(">I",x) for x in d+a)+bytes([ccr,len(instr)])+instr
p=serial.Serial('/dev/cu.usbserial-D01457',115740,timeout=0.5); p.reset_input_buffer()
p.write(frame(CMD_RUN, pl)); r=p.read(64); p.close()
pr=parse(r); regs=[struct.unpack(">I",pr[1][i*4:i*4+4])[0] for i in range(15)]
print("D0 =", regs[0], "(want 3)", "PASS" if regs[0]==3 else "FAIL")
```
Run: `cd sw/harte/host && python3 runone.py`
Expected: `D0 = 3 (want 3) PASS`. Proves load→single-step→dump on silicon. (If it FAILs on comms, add a one-shot retry; if D0≠3, debug the harness asm against the sequence above.)

- [ ] **Step 4: Commit.** `git add sw/harte/harness.S sw/harte/host/runone.py && git commit -m "feat(harte): harness RUN handler — load/step/dump + single-case HW check"`

---

### Task 6: Host driver + Phase-1 end-to-end on silicon

**Files:**
- Create: `sw/harte/host/harte_run.py`, `sw/harte/host/flags.py`
- Fetch: ADD/MOVE/ASL opcode JSON to `sw/harte/data/`

**Interfaces:**
- Consumes: `proto.frame/parse`, `harte_case.load/build_case/is_in_scope`, RESULT layout (60 regs + ccr).
- Produces: `run_opcode(path, ser, defined_mask)->(passed,failed,mismatches)`; CLI `python3 harte_run.py <file.json> [--limit N]`.

- [ ] **Step 1: Implement flags.py** (Phase-1 opcodes are fully defined):
```python
# CCR bits: X=0x10 N=0x08 Z=0x04 V=0x02 C=0x01
DEFINED = {"ADD":0x1F, "MOVE":0x0F, "ASL":0x1F, "LSL":0x1F, "DEFAULT":0x1F}
def mask_for(name):
    for k,v in DEFINED.items():
        if name.upper().startswith(k): return v
    return DEFINED["DEFAULT"]
```

- [ ] **Step 2: Implement harte_run.py**:
```python
import sys, serial, struct
sys.path.insert(0,'.')
from proto import frame, parse, CMD_RUN
from harte_case import load, build_case, is_in_scope
from flags import mask_for
def encode_run(c):
    pl = b"".join(struct.pack(">I",x&0xFFFFFFFF) for x in c.d+c.a)
    pl += bytes([c.ccr, c.ilen]) + c.instr
    return frame(CMD_RUN, pl)
def decode_result(payload):
    regs = [struct.unpack(">I",payload[i*4:i*4+4])[0] for i in range(15)]
    return regs[:8], regs[8:15], payload[60]
def run_opcode(path, ser, limit=None):
    p=f=0; miss=[]
    for n,raw in enumerate(load(path)):
        if limit and n>=limit: break
        if not is_in_scope(raw): continue
        c = build_case(raw); ser.reset_input_buffer(); ser.write(encode_run(c))
        buf=ser.read(64); pr=parse(buf)
        if not pr: miss.append((c.name,"no/again")); f+=1; continue
        d,a,ccr = decode_result(pr[1]); m=mask_for(c.name)
        ok = d==c.fd and a==c.fa and (ccr&m)==(c.fccr&m)
        if ok: p+=1
        else: f+=1; miss.append((c.name, d,c.fd, ccr,c.fccr,m))
    return p,f,miss
if __name__=="__main__":
    ser=serial.Serial('/dev/cu.usbserial-D01457',115740,timeout=0.4)
    limit=None
    if "--limit" in sys.argv: limit=int(sys.argv[sys.argv.index("--limit")+1])
    p,f,miss=run_opcode(sys.argv[1],ser,limit); ser.close()
    print(f"PASS={p} FAIL={f}")
    for m in miss[:20]: print("  MISMATCH", m)
```

- [ ] **Step 3: Fetch Harte data** to `sw/harte/data/`. Source: `https://github.com/SingleStepTests/ProcessorTests` → `680x0/68000/v1/<opcode>.json.gz` (files are named by opcode word, e.g. `d041.json.gz` = ADD.W D1,D0). Fetch a register-form ADD file + a MOVE Dn,Dm + an ASL, e.g. `for f in d041 3001 e340; do curl -sL "https://raw.githubusercontent.com/SingleStepTests/ProcessorTests/main/680x0/68000/v1/$f.json.gz" | gunzip > sw/harte/data/$f.json; done`. (Or copy from beast `~/astra_soc/harte/` if already fetched.) Add `sw/harte/data/` to `.gitignore` — the JSON is large; don't commit it.

- [ ] **Step 4: Flash the Task-5 harness bitstream** (`mkbit.sh rom_harness.hex` → `openFPGALoader -f`), **user power-cycles**.

- [ ] **Step 5: Run end-to-end**

Run: `cd sw/harte/host && python3 harte_run.py ../data/ADD.json --limit 200`
Expected: `PASS=200 FAIL=0` (all in-scope). Any FAIL prints the exact operands + expected/actual regs+CCR. **Phase-1 exit criterion: 100% pass, or a reproducible core bug surfaced (fix in RTL per systematic-debugging, re-verify).**

- [ ] **Step 6: Commit.** `git add sw/harte/host/ sw/harte/data/.gitignore && git commit -m "feat(harte): host driver + Phase-1 ADD/MOVE/ASL sweep"`

---

## Phase 2 — full register-only DP opcode sweep

### Task 7: Expand scope + defined-flags coverage + full sweep

**Files:**
- Modify: `sw/harte/host/flags.py` (add masks for all register-only DP opcodes), `sw/harte/host/harte_case.py` (tighten `is_in_scope` A7-operand filter if needed)
- Create: `sw/harte/host/sweep.py` (runs a list of opcode files, aggregates)

**Interfaces:**
- Produces: `sweep.py` iterating a curated register-only DP opcode file list, calling `run_opcode`, printing per-opcode PASS/FAIL and a grand total + a mismatch report grouped by opcode.

- [ ] **Step 1: Build the register-only DP opcode list** — enumerate the Harte 68000 opcode files that are register/immediate-form data-processing (ADD/ADDI/ADDQ/SUB/SUBI/SUBQ/AND/ANDI/OR/ORI/EOR/EORI/NOT/NEG/NEGX/CLR/TST/CMP/CMPI/MOVE-reg/MOVEQ/EXT/SWAP/ASL/ASR/LSL/LSR/ROL/ROR/ROXL/ROXR/Scc). Save as `sw/harte/host/opcodes_regonly.txt` (one filename per line). Exclude anything with an EA that can be memory unless the specific file is the Dn-form.

- [ ] **Step 2: Add defined-flags masks** in `flags.py` for each family (e.g., `CMP`/`TST` set NZVC, X unaffected → mask 0x0F; shifts set X → 0x1F; `Scc`/`MOVEQ` per M68000PRM). Reference: Motorola M68000 Programmer's Reference Manual CCR tables. Where the 68000 lists a flag "U" (undefined), drop it from the mask (that's the 68000↔68030 divergence guard).

- [ ] **Step 3: Implement sweep.py**:
```python
import sys, serial
sys.path.insert(0,'.')
from harte_run import run_opcode
files=[l.strip() for l in open("opcodes_regonly.txt") if l.strip() and not l.startswith("#")]
ser=serial.Serial('/dev/cu.usbserial-D01457',115740,timeout=0.4)
tp=tf=0
for fn in files:
    p,f,miss=run_opcode("../data/"+fn, ser)
    print(f"{fn:20s} PASS={p} FAIL={f}"); tp+=p; tf+=f
    for m in miss[:5]: print("   ", m)
ser.close(); print(f"TOTAL PASS={tp} FAIL={tf}")
```

- [ ] **Step 4: Run the sweep** (harness bitstream already flashed; user power-cycles once):

Run: `cd sw/harte/host && python3 sweep.py`
Expected: `TOTAL PASS=<large> FAIL=0`. Triage any FAIL: reproducible mismatch → likely real core bug (fix RTL, re-verify via `check_boot.py` selftest + re-run sweep) OR a genuine 68000↔68030 arch difference → add the flag to the undefined mask / exclude the case, with a one-line comment citing the M68000PRM reason.

- [ ] **Step 5: Commit.** `git add sw/harte/host/ && git commit -m "feat(harte): full register-only DP sweep + defined-flags masks"`

---

## Out of scope (future, separate plan)
- **Phase 3 — memory-operand cases:** requires flat RAM for arbitrary Harte addresses. Unlock via ULX3S 32 MB SDRAM controller bring-up (maps the full 68000 space; also the SoC's next real feature) or a sim testbench with a sparse-associative memory model. Decide after Phase 1 proves the harness.

## Notes for the implementer
- The core, SoC, selftest, and local ECP5 flow are already working (5/5 clean boots; `SELFTEST: PASS sum=574D530E`). Do NOT modify `wf68k30L_*.vhd` except to fix a bug the harness *reproduces*.
- Every RTL change: re-run `sw/harte/host/check_boot.py`-style selftest to confirm no regression, then the harte sweep.
- Serial is flaky: `harte_run.py` should retry a case once on `parse()==None` before counting FAIL (add a single retry in Step 2 of Task 6 if false negatives appear).
- Power-cycle is required after every `openFPGALoader` (FT231X returns to UART mode); reads use 115740 baud.
