# Astra68 QEMU TCG backend

This directory contains the Astra-owned delta against QEMU 9.2.4.  It exists
to run the unchanged Astra boot ROM and Axiom K1-K10 kernel suite through
QEMU's native ARMv7 TCG backend on the Arty Z7-20.

The existing Musashi machine remains the behavioral oracle.  A QEMU result is
accepted only when the exact ROM reaches the same kernel markers and the shared
MC68030 PMMU/restart tests pass.

The initial overlay adds the physical machine map and device mechanisms.  The
MC68030 PMMU target changes will be kept here as patches rather than depending
on an untracked QEMU checkout on a build host.
