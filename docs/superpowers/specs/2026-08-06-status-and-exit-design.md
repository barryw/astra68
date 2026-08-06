# Astra statuses and exit codes

Date: 2026-08-06
Status: design, approved in conversation. The vocabulary, the ranges and the
verdict bit are built (`sw/include/astra/status.h`); the shared enumeration is
not yet used at call sites, which is deliberate — see §7.

Scope: what a program says when it is asked how it went, what a service says
over a protocol, and what the system says about a process that never got to
answer. One vocabulary for all three.

---

## 1. The governing rule

> **A status names *which* failure. It never says how bad one is.**

Severity is the caller's judgment, not the callee's. `NOT_FOUND` ends a startup
sequence and is entirely routine inside a loop looking for the first of two
places a command might live. The code that failed does not know how much its
failure matters, and a number that claimed to would be wrong half the time.

Severity belongs on the event record, which carries the context that makes
severity mean something — see `2026-08-06-event-system-design.md`, which already
has five levels. Two severity systems can disagree, and then the machine has no
answer to which one is right.

There is a second reason, and it is the one that bites in practice. A status
ordered by severity freezes its own numbering the moment anyone writes
`status > N` — and someone always does, because an ordering invites it. After
that, a failure cannot be added in the middle of the range without changing what
every existing comparison means. An identity space never has that problem: a new
failure takes the next free number and nothing that already worked changes.

## 2. The ranges

| Range | Who owns it | Who may interpret it |
|---|---|---|
| `0` | success | everyone |
| `1..31` | the system | everyone — one shared meaning |
| `32..0x7FFFFFFF` | the program that returned it | nobody but that program |
| high bit set | the system's verdict on a process | everyone |

`ASTRA_STATUS_SYSTEM_MAX` is 31 rather than 30 so that the boundary test is
`status < 32` — one mask, no decimal constant to mistype — and so that the
sixteen values after the storage protocol's fifteen are a full range rather than
an awkward remainder.

### 2.1 `1..31`, the shared vocabulary

Sixteen are assigned and sixteen are spare:

```
 1 PROTOCOL          malformed, or a version mismatch
 2 NOT_FOUND         9 BAD_HANDLE
 3 EXISTS           10 LIMIT
 4 NOT_DIR          11 IO
 5 IS_DIR           12 NOT_EMPTY
 6 ACCESS           13 UNSUPPORTED
 7 NO_SPACE         14 BUSY
 8 INVALID          15 BUFFER_TOO_SMALL
```

**These are the storage protocol's existing numbers.** They came first, they are
on the wire, and they are not renumbered. The protocol now *uses* this
vocabulary rather than carrying one of its own: `ASTRA_VFS_ERR_NOT_FOUND` is a
spelling of `ASTRA_STATUS_NOT_FOUND` and cannot drift from it, because there is
only one definition of the value. A machine that translated between two lists at
every boundary would eventually have a boundary where the translation was wrong.

The names stay because callers read them and a storage caller thinks in storage
terms. Only the numbers are shared.

### 2.2 `32` and above, the program's own

A program with more than one way to fail says so here rather than reaching for a
shared code that nearly fits. Nothing else may interpret one: a launcher reports
the number and stops.

The supervisor's self-test bitfield (`0x5356____`, "SV" and one bit per failed
check) is a program-defined status under this rule and needs no change. It is
worth noting that its halfword is **full** — all sixteen bits are spent, and the
filesystem check was already squashed into one aggregate bit because of it. The
next check to be added there needs a wider report, not a seventeenth bit.

### 2.3 The high bit, the system's verdict

```
0x80000001 FAULTED      killed by its own fault
0x80000002 NO_STARTUP   its startup block was refused
0x80000003 BAD_EXIT     it returned a status with the verdict bit set
```

This is what keeps **"it failed"** apart from **"it never got to say"**. Unix
loses that distinction and pays for it with conventions like 127, which any
program may also return by accident.

**A program cannot produce one.** `astra_main` returns `int`, so a value
carrying the verdict bit is negative, and `crt0` exits with `BAD_EXIT` instead of
passing it on. The kernel makes the same substitution in `retire_current` for
anything that did not come through `crt0`. Both refuse rather than truncate: a
program returning a negative status is defective, and saying so beats turning it
into a number that looks deliberate.

The rule is scoped to **process** exit. A thread's exit status is read only
inside its own process and carries no verdict.

## 3. The pair is authoritative

The kernel records three things about a dead process, and they answer different
questions:

| Field | Question |
|---|---|
| `exit_status` | what did it say? |
| `exit_reason` | `SYSCALL` / `LAST_THREAD` / `USER_FAULT` — how did it end? |
| `terminal_result` | `OK`, or `PEER_DEAD` if it was killed |

**`exit_reason` is the authority on whether a process returned at all.** The
verdict bit exists so that a reader of the single status value reaches the same
conclusion, which is what a shell prompt and a launcher's error line actually
have in front of them. Nothing that can consult the pair should settle for the
bit.

## 4. The two bugs this fixed

Both were live, both are gone, and both were the same mistake: zero standing in
for an answer that was never given.

**A crashed process reported success.** `prepare_process_death_wait` returned
`exit_status = 0` whenever `terminal_result != OK`, so a process killed by a
fault waited out as `(PEER_DEAD, 0)`. The status alone said it finished cleanly.
The fix is at `retire_current` — the single point every process passes through on
its way out — so the death wait, the process info record, the snapshot and the
kernel's own boot line all report `FAULTED` rather than three readers agreeing by
accident. A kernel test had encoded the old behaviour and now asserts the new
one.

**`crt0` exited with 127** when the startup block was refused. That is Unix's
"command not found" wearing the wrong meaning, and it is indistinguishable from a
program that legitimately returned 127. It is `NO_STARTUP` now.

One more followed from the rule rather than from a defect: the supervisor
returned `0x53560000` for success. The tag was standing in for a proof of life
back when a process that never reached user mode also exited zero — the verdict
statuses carry that now, so success no longer has to be spelled unusually to be
believed. Its *failure* statuses keep the tag and remain valid under §2.2.

## 5. What a launcher does with this

Not built — there is no launch path — but the shape follows from the above and
is written down so the first launcher does not have to invent it:

- `0` → the program succeeded. Say nothing.
- `1..31` → name it from the shared vocabulary. The launcher knows these words.
- `32+` → report the number and the program's name. The launcher must not
  pretend to know what it means.
- verdict → say what the system did to it. Never report this as the program's
  own failure, because it is not one.

## 6. What this does not cover

Syscall results (`ASTRA_SYSCALL_*`) are a separate enumeration and stay separate.
They are answers to *this call*, delivered synchronously to a caller that already
has the context, and several of them — `WOULD_BLOCK`, `PEER_DEAD` — are not
failures at all. Merging them would put values into the exit space that no
program could ever return.

## 7. Deliberately not done yet

**Call sites are not rewritten.** `ASTRA_VFS_ERR_*` keeps working everywhere it
is used today. The values are now defined in one place, which is the property
that matters; changing thousands of spellings would be churn with a regression
risk and no property gained.

**Nothing consumes an exit status yet**, because userspace cannot start a
process. The vocabulary is defined now because it is far cheaper to agree on it
before there are consumers than after — and because the two bugs it exposed were
already costing correctness with no consumers at all.
