# Build and Test Artifact Policy

Git contains source files, authored documentation, deterministic generators,
and intentional design assets. It does not contain generated build products,
test output, FPGA checkpoints or reports, packed firmware payloads, generated
protocol bindings, or hardware captures.

Release and certification evidence is retained on the shared durable store
under `/mnt/Documents/astra68/`, grouped by source identity and checkpoint.
Timing and status documents record the exact durable path, tool version,
source hashes, measurements, and disposition needed to audit a result.

The local `docs/evidence/` directory is ignored. It may be used as a temporary
working view of retained evidence, but its contents must never be staged.

Before committing, verify the candidate index with:

```sh
git diff --cached --name-status
git diff --cached --stat
```

Build from a source-only export before release. A successful incremental build
from a developer worktree is not proof that every generated prerequisite can
be reproduced.
