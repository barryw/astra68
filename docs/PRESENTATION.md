# Tear-free presentation contract

## Status

This document defines the implemented production behavior. Vega keeps separate
shadow, committed-baseline, and active scene state; accepts one fenced present;
promotes it atomically at vblank; and swaps the framebuffer descriptor without
copying pixels. The CPU, AstraHost DMA, Astraea draw, and blitter paths enforce
the exported front and pending-surface guards. Copper remains the only path
that can alter active visual registers during scanout. Directed and integrated
graphics tests exercise promotion, fence deferral, rejected writes, scrolling,
and the active-surface guards.

## Ownership model

The protected display service owns Vega and Astraea policy. Applications
describe desired scene changes through NDK resources; they do not wait for the
beam, update live display registers, or choose a front-buffer swap instant.

There are three distinct kinds of work:

1. CPU and NDK updates change shadow display state.
2. Astraea draw and blitter commands render pixels into a non-visible surface.
3. Copper `WAIT` and `MOVE` instructions intentionally change active registers
   during scanout and remain the sole raster-time exception.

These paths must remain distinct in RTL. A CPU write cannot acquire copper's
active-register privilege by using the same MMIO address.

## Shadow display state

CPU writes to visual state update a pending shadow, never the active scanout
copy. Visual state includes mode, framebuffer descriptor, viewport, default
palette and backdrop, colorkey, sprite and virtual-sprite state, and other
frame-scoped presentation controls. Status, interrupt acknowledgement, fault
reporting, and diagnostic controls remain immediate.

At vblank, Vega promotes one complete validated shadow generation before the
line-zero prefetch deadline. The promotion is all-or-nothing. Invalid,
incomplete, late, or over-capacity generations leave the current active state
unchanged and complete their fence with a precise status.

Copper starts each frame from the promoted baseline. Its raster-time changes
affect only active state and never mutate the canonical shadow generation.

## Surfaces and present

Pixel rendering is not restricted to vblank. Vblank is only 45 of 525 output
lines, so such a restriction would discard more than 90 percent of available
draw and blitter time and still could not make an unbounded command atomic.

The display service acquires a hidden back surface, submits validated Astraea
commands, and queues `PRESENT(surface, render_fence, scene_generation)`. Vega
promotes that request at the first vblank for which both the render fence and
the shadow generation are complete. Otherwise it retains the previous frame
and retries at a later vblank.

Vega swaps the active framebuffer descriptor; it does not copy a framebuffer
during vblank. A 720x480 RGB565 image is 691,200 bytes and cannot be copied in
the blanking interval. The old front surface becomes eligible for reuse only
after the presentation completion fence identifies the frame that retired it.

The compositor owns damage tracking and hidden-buffer coherence. Applications
see acquire, drawing, and present operations rather than front/back addresses.

## Active-surface write guard

Vega exports the active physical scanout footprint and frame generation to the
shared memory fabric. Outside vblank, CPU, AstraHost DMA, Astraea draw, and
blitter writes that overlap that footprint must not reach SDRAM. Normal work to
non-visible surfaces continues at full rate.

The default response to an active-surface hazard is to hold or reject the
command before its first write. A privileged vblank-only operation may run only
when hardware can prove the complete bounded operation will retire before
active scanout resumes. It must never expose a partially completed command.

The PMMU and display service also map application-visible presentation surfaces
with suitable rights, but MMU policy does not replace the physical write guard.

## Completion and failure

Every accepted scene generation and present request has a monotonically
increasing identifier. Completion reports the actual frame number, retired
surface, and status. The display service can therefore release old surfaces,
command buffers, palettes, and sprite resources without guessing about beam
timing.

Queue-full, invalid-resource, missed-deadline, and active-surface-hazard results
are explicit. A missed frame is represented by retaining the last complete
frame, not by tearing or displaying partially updated state.

## NDK shape

The final names are not frozen, but the API must express this ownership model:

```c
AstraFrame *astra_display_acquire(AstraDisplay *display);
AstraFence astra_frame_submit(AstraFrame *frame,
                              const AstraSceneUpdate *update);
AstraFence astra_display_present(AstraDisplay *display,
                                 AstraFrame *frame,
                                 AstraFence render_complete);
```

Resource wrappers release unsubmitted frames and wait-free queue ownership
automatically. Ordinary application code never contains a vblank polling loop.
