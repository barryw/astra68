# Astra 68 SDRAM subsystem

## Goals

The ULX3S MT48LC16M16A2 is a 32 MiB, 16-bit SDR SDRAM. At the initial
75 MHz controller clock its raw transfer rate is 150 MB/s. The memory system
must preserve CPU-visible 68030 ordering and byte semantics while leaving most
of that bandwidth available to future chip DMA.

Acceptance targets at 75 MHz:

- sustained aligned read and write streams: at least 120 MB/s each;
- complementary four-sweep POST over 32 MiB: less than 1.5 seconds;
- one memory-controller request for an 8-bit or 16-bit TG68K bus cycle;
- two native 16-bit CPU cycles for a 32-bit TG68K operation, without any
  additional byte serialization;
- exact masked-write behavior at every byte lane and across unaligned CPU
  operations;
- bounded DMA bursts so the CPU and refresh engine cannot be starved.

After the 75 MHz implementation is stable on hardware, 100 MHz is the stretch
clock. That raises raw bandwidth to 200 MB/s; it is not accepted unless timing,
long memory stress, and all CPU regressions remain clean.

## Native request contract

The SDRAM clock domain uses an aligned, canonical big-endian 32-bit request:

```
valid, ready, write, address[24:0], byte_enable[3:0], write_data[31:0]
response_valid, read_data[31:0]
```

`address[1:0]` is zero. `byte_enable[3]` and `write_data[31:24]` correspond to
the lowest byte address; lane zero corresponds to the highest byte address.
The physical controller uses conventional little-endian write strobes, so the
board wrapper reverses bytes and strobes at exactly one boundary.

A two-entry FIFO separates client arbitration from the physical command
engine. Client readiness depends only on registered occupancy, while the FIFO
head alone drives the controller. The depth absorbs the controller's alternating
command phases and preserves its maximum sustained 32-bit transfer rate without
a combinational accept path crossing the boundary.

The TG68K candidate has a 16-bit external bus. Its wrapper therefore emits one
native request for a byte or word cycle. A longword is two ordered word cycles.
The PMMU walker can emit a full 32-bit request. A separate CPU lock signal
crosses the clock boundary with the requests and remains live for the complete
68030 read-modify-write (`RMC`) sequence.

The CPU bridge uses a bundled-data toggle handshake with two synchronizer
stages in each direction. The SoC launches it on the SDRAM decode edge, and a
synchronized completion presents its already-stable data and `DSACK` directly
to the CPU sampling edge. Reads, locked cycles, PMMU walks, and uncached writes
wait for physical completion. One ordinary cacheable SDRAM write may retire
after the bridge has captured it; the bridge remains busy until that write has
completed and cannot accept a second request in the meantime.

## Controller and arbitration

The physical command engine is the vendored `ultraembedded/core_sdram_axi4`
core, used without its AXI adapter. It is verified upstream against the exact
MT48LC16M16A2, keeps one row open in each of four banks, and pipelines masked
32-bit transfers as two SDRAM halfwords.

The ULX3S ECP5 implementation uses `SDRAM_READ_LATENCY=3` at 75 MHz. A
controller-only hardware sweep established the sampling point independently of
the CPU and caches: latency 2 returned `0x00010000` after writing `0x00000001`,
latency 1 returned `0x00010001`, and latency 3 passed 38 directed operations,
including all 15 nonzero byte-enable masks. The pin-level SDRAM model presents
CAS-2 data on the second SDRAM edge and therefore enforces the same setting in
simulation.

Astra owns the request arbiter. Implemented clients are:

1. CPU/PMMU single transactions through a toggle-based CDC bridge.
2. The destructive POST engine, which takes an exclusive maintenance grant
   while the ROM and stack remain in block RAM.
3. Vega framebuffer, tile, and sprite scanline builders through the real-time
   video port.
4. Astraea blitter/draw engines through the opportunistic DMA port.

An active CPU `RMC` lock has first priority, followed by Vega video, DMA, and
ordinary CPU traffic. Video's three internal clients rotate after bounded
32-word bursts and hold ownership only until their accepted responses retire.
DMA grants are also chunked; an engine must release its lock at its configured
maximum burst boundary. Refresh is internal to the physical controller and
always takes precedence. A CPU `RMC` lock retains CPU ownership between the
locked read and write and prevents DMA traffic from interleaving until the CPU
releases it.

The first non-POST DMA client is the Astraea blitter. It performs aligned
COPY/FILL work in 16-word chunks and falls back to masked byte transactions for
unaligned rectangles. Controller-level simulation sustains 51.44 MB/s copy
(one read plus one write per payload byte) and 119.81 MB/s fill. Astraea and the
destructive POST engine share a registered DMA owner; POST has priority when
both request ownership.

## CPU caches

The TG030 wrapper implements separate 256-byte instruction and data caches,
controlled by the 68030 CACR enable/freeze signals. Each direct-mapped entry
holds one aligned 32-bit controller word, and each cache has a 16-byte stream
buffer populated by an SDRAM line fetch. A miss remains an ordered external
transaction. On a hit, the selected 16-bit halfword follows a full
rising-edge-to-rising-edge path and retires on the next CPU edge without an
external 68k bus cycle. TG's documented bus-state output, rather than its
MOVES-overridable function-code output, classifies instruction and data cycles;
this avoids a combinational `cache_ack`/`clkena`/FC feedback loop.

CPU writes are write-through and invalidate matching instruction and data
entries only after the external transaction completes. PMMU cache-inhibited
accesses and locked `RMC` cycles always bypass both caches. Cache-maintenance
requests and DMA maintenance ownership conservatively invalidate all entries.
Future concurrent DMA clients must retain this invalidation contract or add
address snooping before they can share memory with cached CPU traffic.

## Ordering and coherence

The wrapper combines the two 16-bit beats of an aligned longword store into one
masked 32-bit controller request. The bridge permits one posted cacheable SDRAM
write and acknowledges it only after capturing its address, data, and byte
enables. It does not contain a write FIFO, so a second external request cannot
overtake the in-flight write. Matching instruction and data entries are
invalidated when the bridge accepts the write, preventing a following cache hit
from returning the old value. Uncached/MMIO requests, locked `RMC` cycles, PMMU
walks, and NOP synchronization wait behind the outstanding write.

PMMU cache-inhibit propagation bypasses both line and processor caches. CACR
maintenance operations invalidate the selected processor-cache scope, while
DMA maintenance ownership conservatively invalidates all entries. Future
concurrent DMA clients must preserve this invalidation contract or add address
snooping before sharing memory with cached CPU traffic.

## Measured simulation baseline

At 75 MHz, the current pin-level controller test sustains 145.30 MB/s writes
and 143.64 MB/s reads while refresh is active. The integrated complementary
POST path sustains 144.46 MB/s, projecting 0.929 seconds for four complete
sweeps of 32 MiB. Run `sw/boot/run_sdram_sim.sh` to rebuild the TG030 ROM and
core netlist and execute this complete pin-level gate.

The full graphics gate pipelines framebuffer requests and overlaps tile/sprite
BRAM work. With both tile layers and unrelated-row sprites active, INDEX8 plus
1024 admitted sprite pixels completes its worst scanline in 2274 memory clocks;
RGB565 plus the hardware-limited 512 pixels completes in 2022. The 720x480 line
deadline is 2383 clocks. `sw/graphics_demo/run_sim.sh all` locks these cases.

## Hardware acceptance

Build `0xA0086302` passed three consecutive SRAM reconfiguration boots on the
ULX3S attached to `nuc`. Every run completed the CPU data/byte-lane, unaligned
word/long, address, cache-coherence, Astraea fill/copy, and complementary
full-range 32 MiB BIST checks in 1.127-1.128 seconds. The accepted seed-2
bitstream SHA-256 is
`a2ff4bd888ab130c00c94822c9b2618489c965a6155a2c7ed4ae2d46a403d8ea`.

Post-route Fmax is 80.13 MHz for the 75 MHz SDRAM domain and 13.16 MHz for the
12.5 MHz CPU domain. The complete TG030+PMMU+HDMI+Astraea SoC uses 36,935
packed LUTs, 9,147 FFs, 136 DP16KD blocks, and 13 multipliers. Yosys reports
zero SCCs, and nextpnr reports no combinational loops.

The native controller and BIST paths remain close to the 150 MB/s physical
limit. On hardware, the software-visible Astraea interface sustains 118.36
MB/s fill and 51.40 MB/s copy, including command launch, completion polling,
and cache-maintenance boundaries.

Application-visible pointer-loop rates at 12.5 MHz are intentionally reported
separately from native bandwidth. The hardware benchmark moves a 16 KiB
payload per phase:

| Width | Write | Read |
|---|---:|---:|
| 8-bit | 1.388 MB/s | 1.183 MB/s |
| 16-bit | 2.777 MB/s | 2.247 MB/s |
| 32-bit | 4.542 MB/s | 4.935 MB/s |

These loops include TG030 instruction execution, the core's 16-bit external bus,
The 32-bit phase takes 45,094 CPU clocks for writes and 41,498-41,512 clocks
for reads across the accepted boots. Writes report zero SDRAM wait clocks. A
separate cache-resident 256-byte simulation takes 602 clocks for a 32-bit hot
read after fill and issues no SDRAM requests. The 16 KiB hardware read exceeds
the 256-byte processor cache and remains a streaming test. Remaining
cache-resident pointer-loop cost is therefore TG030 instruction execution and
its 16-bit internal bus sequencing, not SDRAM latency.

The earlier build `0x7FB5A559` remains a useful pre-Astraea baseline. It passed
five cold boots in 1.098-1.100 seconds with bitstream SHA-256
`f1c5d22955797e4a98e58401627823d59fdc3bd3b16f4bf48e1a104633928075`.
