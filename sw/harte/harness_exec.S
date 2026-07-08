| Astra 68 — Harte harness per-case executor (GNU as, m68k syntax).
| C-callable: void harte_exec(void).  Operates on globals defined in harness.c:
|   ccr_in (byte), regtable[15] (d0..d7,a0..a6), codebuf (test instr + a trailing RTS,
|   built by C), resultbuf[15], result_ccr (byte).
|
| Sequence (spec §4): set CCR, MOVEM-load all 15 regs, JSR into the relocated test
| instruction in RAM; the test instruction runs and falls into a trailing RTS that returns
| here; then capture regs+CCR.
|
| WHY jsr+RTS and not jmp+JMP: on this SoC, executing the relocated instruction reached via
| `jmp <RAM>` and returning via a `JMP abs.L` fetched from RAM HANGS the CPU (multi-word
| operand prefetch from RAM), whereas `jsr <RAM>` into a single-word `RTS` fetched from RAM
| runs cleanly (verified on silicon). So we enter with JSR and return with a one-word RTS —
| no multi-word change-of-flow instruction is ever fetched from RAM.
|
| ABI: harte_exec clobbers d0-d7/a0-a6 by design (it loads case data), so it saves and
| restores the callee-saved set (d2-d7/a2-a6). A7 is never touched by in-scope cases
| (register-only, non-A7); the JSR return address sits on the stack across the test
| instruction and the trailing RTS pops it.
|
| Ordering is load-bearing: CCR is set into d0 first, then MOVEM overwrites d0 with the real
| value (MOVEM does not affect CCR; neither does JSR/RTS), so at the test instruction both
| regs and CCR equal the case's initial state. Regs are captured via an absolute MOVEM
| *before* reading SR, so no scratch clobbers a live result and CCR is captured intact.

	.section .text
	.globl  harte_exec
	.type   harte_exec, @function
harte_exec:
	movem.l %d2-%d7/%a2-%a6, -(%sp)          | save callee-saved regs (ABI)
	move.b  ccr_in, %d0                      | initial CCR -> d0.b
	move.w  %d0, %ccr                        | set CCR (word op, low byte)
	movem.l regtable, %d0-%d7/%a0-%a6        | load 15 regs from abs.L; CCR preserved
	jsr     codebuf                          | run relocated test instr -> trailing RTS returns here
	movem.l %d0-%d7/%a0-%a6, resultbuf       | capture 15 result regs to abs.L (no clobber)
	move.w  %sr, %d0                         | full SR (supervisor); low byte = CCR, still intact
	move.b  %d0, result_ccr
	movem.l (%sp)+, %d2-%d7/%a2-%a6          | restore callee-saved regs
	rts                                      | return to C

	.section .note.GNU-stack, "", @progbits
