# Astra software ownership and reuse audit

Status: retained and executable as of 2026-08-25.

This audit covers all target-resident Astra software: firmware handoff, Axiom,
the shared ABI, runtime, NDK, Kits/libraries, Supervisor, services,
applications, and commands. It also checks their build edges into vendored
code. QEMU and FPGA implementations remain governed by their own machine and
timing documents; they may implement a published device contract, but they do
not own target software policy.

The audit rule is simple: wire formats live with the protocol, reusable
behavior lives with one library owner, process entry points contain only glue,
and kernel-private state never crosses into user mode. A local implementation
is retained only when its contract actually differs.

## Enforced ownership

| Owner | Responsibility | May depend on |
|---|---|---|
| `sw/include/astra` | wire ABI, resource limits, and freestanding value primitives | no private kernel, NDK, application, or service headers |
| `sw/kernel` | Axiom objects, scheduling, VM, handles, syscalls, and devices | shared ABI and kernel-private headers |
| `sw/userspace/runtime` | program startup, arguments, syscalls, service readiness, shared-library loading, diagnostics, and runtime clocks | shared ABI only |
| `ndk` | stable application-facing native API | shared ABI and runtime entry points; its `src/internal` headers stay private |
| userspace libraries | reusable storage, VFS, events, input, streams, shell, terminal, graphics, interface, messaging, POSIX, allocation, and metrics behavior | public ABI plus explicitly linked owner archives |
| Supervisor and services | process policy and device/application wiring | public owner headers and archives; never another service's implementation |
| commands and applications | command-specific presentation and orchestration | runtime, NDK, and public libraries |

`sw/userspace/tests/test_boundaries.py` makes these rules executable. It
rejects kernel-private imports in user mode, NDK-private imports outside the
NDK, public headers reached through another subsystem's directory, GUI wire
types owned by the client API, filesystem private-state reach-through,
Terminal compiling Supervisor code, local startup/service-ready parsing,
foreign library implementation sources in production archives, incomplete
analyzer source lists, and local copies of the shared primitives extracted by
this audit.

## Consolidations retained

The following responsibilities now have one implementation and regression
coverage:

- Terminal process glue lives in `services/terminal`; Supervisor no longer
  owns or exports Terminal implementation headers or sources.
- program startup, capability lookup, argument access, service-ready
  publication, failure diagnostics, and saturating elapsed-time conversion
  live in the runtime;
- bundle parsing, handles/results, shared-area access, and application-facing
  resource operations live in the NDK;
- VFS path qualification, union selection, process file loading, transport,
  immutable synthetic-backend operations, and library-private state live in
  the VFS owner;
- stream writes and sinks, terminal cells/keymaps, shell parsing, input
  dispatch, graphics surfaces/builders, and event storage/catalog handling
  live in their respective libraries rather than command or service entry
  points;
- GUI wire events live in the system protocol; the NDK client API consumes
  them instead of defining types that the system includes;
- endian loads/stores, byte/string operations, checked integer operations,
  generation/handle encoding, ASCII case conversion, compiler barriers,
  message-header serialization, and the libgcc-free 64-bit slot bit live in
  shared headers or their existing common owner;
- rounded-corner geometry is shared inside Graphics, while PROC: and EVENTS:
  share the same read-only VFS operation implementations;
- runtime, NDK, and Graphics PIC objects are built once by their owner and
  consumed as archives; production consumers do not recompile their source;
- `sw/userspace/program.mk` owns the build-before-restat-before-link sequence
  for commands, Supervisor, and all services. Each program declares its owner
  directories, so a clean standalone build no longer relies on top-level
  ordering or silently links stale archives;
- lwext4 patch 0013 places its extent-only local under the existing extent
  feature guard, removing the no-extents target warning without adding a new
  policy or code path;
- incremental ELF acceptance lives in Axiom, transaction driving lives in the
  runtime, and borrowed random-access executable files live in VFS. Terminal
  and Supervisor use those owners and retain no whole-image loader or fallback;
- product kernel and ROM outputs live only under owner `build/` directories,
  so source synchronization cannot replace a remote build with a stale local
  binary or generated splash payload.

The Axiom hot-path changes were checked in generated MC68030 code. The shared
64-bit bit helper and compiler barrier are inlined, introduce no helper calls,
and do not pull in `__ashldi3`. The one intentionally forced-inline saturating
IRQ increment remains measured policy, not a blanket inline convention.

## Local code intentionally retained

These similar-looking functions do not currently share one useful contract:

- the memory, VM, and area physical-memory bind functions are module-specific
  host-test hooks and are absent from production;
- Supervisor and storage clock callbacks adapt different owner contexts to the
  same runtime clock;
- display and interface color helpers convert between different type layers;
- command-local `say` wrappers encode each command's output/newline policy;
- `ps` and `ls` text appenders have different capacity and formatting
  contracts, and the wider numeric formatting family includes signed,
  unsigned, padded, duration, percentage, and stream-emission semantics.

Extracting any of these today would add a union of unrelated policies. They
should move only when a tested common contract replaces the differences.

Host tests may compile a real foreign implementation as a fixture—for example
Display against Graphics, NDK application tests against runtime argument
decoding, and Metrics against storage/allocation probes. Production targets
link owner archives; the architecture gate distinguishes the two.

## Remaining findings

These are visible gaps, not silent acceptance:

1. Supervisor and Terminal emit compiled event descriptors. Other resident
   services currently have analyzer coverage but do not yet contribute their
   own structured event catalogs to the merged system catalog.
2. Glue-only services and bundle aggregators retain empty `test`/`sanitize`
   aliases because their process entry points cannot execute on the host.
   Their production sources are cross-compiled and analyzer-checked, and their
   behavior owners have real host and sanitizer tests. A future test should be
   an actual process/service integration test, not a syscall mock created only
   to make the target print activity.

## Verification record

All target builds ran on Beast from the synchronized workspace:

- clean full userspace behavior matrix, ASan/UBSan matrix, analyzer matrix,
  and MC68030 build: pass;
- commands built standalone from clean owner and NDK state, including Lua
  5.5.1 and the 2,157,012-byte stripped Vim file: pass;
- all six services built standalone from clean owner and NDK state: pass;
- NDK host tests, ASan/UBSan, analyzer, normal archive, and PIC archive: pass;
- complete Axiom host suite plus MC68030 `all verify`: pass; final kernel
  payload is 155,320 bytes;
- firmware LZ4 test, 46 Python payload/layout tests, complete ROM build, and firmware
  verification: pass;
- focused storage tests, ASan/UBSan, analyzer, and MC68030 build after lwext4
  patch 0013: pass with the prior unused-variable warning removed;
- transactional streaming rollback, sparse-offset, source-ownership, and VFS
  reader tests: pass; the complete 73-command QEMU gate passes Lua, POSIX, and
  the named-file Vim edit.

No NUC, framebuffer capture, FPGA synthesis, flashing, or physical hardware
claim is part of this software-only audit.
