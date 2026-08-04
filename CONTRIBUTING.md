# Contributing to Astra 68

Astra 68 is under active architectural development. Issues, measurements,
test cases, documentation corrections, and focused patches are welcome.

Before changing code:

1. Read `docs/CURRENT_STATE.md` and the authoritative document for the
   subsystem.
2. Search for an existing implementation, state machine, helper, or test with
   the same responsibility.
3. Establish the relevant functional, performance, timing, or resource
   baseline.
4. Keep the change scoped and add failure-path tests as well as success tests.

CPU behavior must follow Motorola's MC68030 documentation. A test, emulator,
or FPGA implementation may reveal an error but does not define an
Astra-specific instruction semantic.

FPGA pull requests should include the exact device, tool version, clock
constraints, source identity, utilization, timing result, and focused test
results. Placement estimates and unconstrained builds are not release
evidence.

Kernel pull requests should preserve bounded allocation and queue limits,
explicit ownership, failure behavior, and existing performance gates. Avoid
speculative portability layers: Axiom is intentionally MC68030-specific.

Do not include credentials, private keys, proprietary tool installations,
vendor-generated build trees, test corpora, bitstreams, test output, hardware
captures, or local machine configuration. Generated release evidence belongs
on the durable build store, not in Git. See
`docs/ARTIFACT_POLICY.md` for the repository boundary and release check.
Vendored code must retain its upstream license and provenance.

Because public licensing and external contribution terms are still being
formalized, open an issue before submitting a substantial implementation.
