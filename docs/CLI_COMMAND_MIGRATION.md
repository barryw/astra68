# Command-line utility migration

Status: audit and staged migration. A command is replaced only after its
upstream binary passes Astra's positive and negative behavior tests and the
same target image installs it. Until then the existing command remains; a
source compiling is not proof that it runs correctly on Astra.

`ls` and `mkdir` are now primary sbase-built candidates in QEMU. The full
83-command terminal gate passes with them, including synthetic `/`, EVENTS,
and PROC directory reads. They are not DE25/release-qualified. In particular,
`ls -i` and recursive cycle detection still require stable VFS device/inode
identity; do not publish this candidate as a release until that is fixed.

## Inventory and ownership

| Shipped command | Direction | Reason |
| --- | --- | --- |
| `cat`, `ls`, `mkdir`, `rm`, `date`, `echo`, `which` | Replace with portable upstream utilities. | Their behavior is general-purpose CLI policy, not Astra mechanism. |
| `ps` | Replace UI with an upstream implementation when its process reader consumes the canonical `/proc` snapshot. | The current binary snapshot already has PID, generation, state, CPU time, memory, and command name. |
| `status`, `events`, `devices`, `open`, `service`, `metrics` | Retain as native controls. | No POSIX utility has their Astra capability or service semantics. |
| `heapbench`, `fsstress` | Retain as test tools, outside the normal user command set. | They are workload generators, not standard user utilities. |
| `ntp`, `ping` | Audit separately. | `ping` is already a Toybox-derived port; `ntp` controls Astra's network/clock service. Neither is POSIX. |
| `lua`, `vim`, `zsh` | Retain upstream ports. | They are not homegrown commands. |

The common suite should also ship `pwd`, `ln`, `cp`, `mv`, `rmdir`, `chmod`,
`chown`, `touch`, `head`, `tail`, `grep`, `sed`, `sort`, `uniq`, `wc`, `env`,
`basename`, `dirname`, `cut`, `tee`, `test`, and `find` once their POSIX API
dependencies pass. `top` and `df` are explicit goals, not silently omitted:
`top` needs the same `/proc` data-source adapter as `ps`; `df` needs one
rights-checked, alias-aware mounted-volume inventory and existing `statvfs`,
not an invented Linux mount table.

## Upstream baseline and shared blockers

The portable baseline is unmodified [sbase](../third_party/sbase/ASTRA_VENDOR.md)
at its pinned commit. Its source is portable across UNIX-like systems and
supports the standard file/text tools; it does not contain `ps`, `top`, or
`df`. Build each chosen utility against Astra's POSIX startup and libc; do not
patch its source for Astra-specific behavior. Its built-in test suite is a
host-side supplement, not a target acceptance gate.

The cross-compiler syntax audit of the requested/common commands found:

| Shared boundary | Affected examples | Required solution |
| --- | --- | --- |
| `PATH_MAX` absent | `pwd`, `rm`, `cp`, `mv`, `chmod`, `chown`, `du` | POSIX headers now expose the existing VFS ABI bound of 192 bytes, including NUL. Lift that underlying ABI before advertising a larger value. |
| Upstream XSI/BSD feature flags omitted by Astra build | `env`, `sed` | Use sbase's own feature-test definitions in the shared build rule; the positive/negative compile contract now guards this. Picolibc already implements and exports `putenv`. |
| `_POSIX_ARG_MAX` absent | `find`, `xargs` | Expose actual launch argument contract or rework the ABI before claiming POSIX semantics. |
| `regex.h` missing `size_t` prerequisite | `grep`, `sed` | Fix the toolchain/header contract, not each utility. |
| `sys/sysmacros.h` absent | `ls` | Public device-number macros now follow Picolibc's existing 64-bit `dev_t` layout and have positive/negative cross-field tests; `ls` still needs real `st_dev`/`st_ino` metadata before release. |
| `stat` lacks `st_dev`/`st_ino` identity | `ls -i`, `pwd -L`, recursive tools | Extend the one VFS metadata path with stable volume/node IDs before accepting these modes. Do not hash a pathname into a fake inode. |
| directory-relative link creation absent | `ln` | POSIX `linkat`/`symlinkat` need real VFS directory-handle operations. Do not resolve a directory fd back to a possibly stale pathname. |
| directory-relative metadata/access absent | `which`, `ln` | POSIX `fstatat`/`faccessat` need Filesystem Kit operations that preserve open-directory identity and follow-link semantics. |
| `UTIME_NOW` absent | `touch` | Complete the POSIX timestamp adapter and header contract. |
| `sys/utsname.h` absent | `uname` | Add a real Astra identity query and POSIX adapter. |

The target build stages 42 unmodified sbase executables through one shared
rule; they include common file, text, environment, identity, and process-control
tools. `ls` and `mkdir` are the primary QEMU candidates; the others retain
their `sbase-*` test names. The broader link audit
confirms that cp, mv, rm,
chmod, chown, which, ln, and test share missing directory-relative filesystem
operations. Cp and mv additionally need timestamp, ownership, and device-node
operations. Du needs the same directory-relative metadata operation; readlink
needs realpath; sync needs a system-wide durability operation; tar needs
device-node creation. Astra's sbase build supplies the public device-number
macros to tar because Picolibc exposes major/minor from sys/stat.h without
makedev; a positive/negative compile test covers that header mismatch. The
remaining items are shared libc/VFS contract gaps, not reasons to patch sbase.

The production ext4 backend now implements open-at, stat-at, stat-by-handle,
unlink-at, chmod-by-handle, chmod-at, and filesystem-capacity requests through
the backend-neutral VFS contract. Hostfs implements the same stat-by-handle
operation; neither the VFS wire protocol nor Filesystem Kit knows ext4 inode
layout. Its raw and partitioned image tests prove an open directory survives
rename, reject invalid and missing paths, and check the result with `e2fsck`.
Filesystem Kit uses live handle metadata for `fstat` and `SEEK_END`, so a
second writer or truncator cannot leave those operations using an open-time
size. This is native-backend and QEMU qualification, not a claim that the
staged commands work on the DE25. Stable volume/node identity,
directory-handle symlink resolution, and the remaining POSIX adapters are
still required.

`/proc` already provides a fixed, generation-aware snapshot and per-process
leaves. Do not duplicate process state in libc or the kernel. Choose an
upstream `ps`/`top` frontend with a narrow Astra data-source port, or expose
an ordinary POSIX read-only view over the same snapshot if that proves simpler.
Linux `/proc/<pid>/stat` is not part of POSIX and must not be assumed to be
Astra's native protocol. Any new native field must be justified by a concrete
upstream reader requirement.

## Acceptance gates

1. Run upstream behavior tests on the host, then target positive and negative
   tests for files, missing paths, permission denial, option errors, exit
   statuses, and the `/` synthetic POSIX root.
2. Check that native and POSIX file operations accept `/dh0/file` and reject
   `DH0:file` without per-command patches; `getcwd`, `PWD`, `HOME`, and `PATH`
   remain slash-form.
3. Measure startup and `ls -l` directory listing on the DE25 before replacing
   current tools. If POSIX metadata needs batching, optimize the shared
   adapter without changing upstream code.
4. Verify image contents are rebuilt from the selected source and no stale
   homegrown binary can shadow its replacement. Remove replaced sources and
   docs only after the gate passes.
