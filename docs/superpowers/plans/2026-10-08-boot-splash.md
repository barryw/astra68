# DE25 Boot Splash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the DE25, show the 1920x1080 Astra splash with a thin progress
bar from power-on until the guest presents its first frame. The bar moves by
counted boot steps, never by time. The text console appears only on failure.

**Architecture:**

- **Record.** One 8-byte progress record lives in the unused tail of the
  shared 4 KiB text page: guest address `0xFFF22000`, the file
  `/run/astra/post-text.bin` on the board. Its layout is defined in
  `sw/include/astra/boot_progress.h`, which the ROM, the kernel and the ARM
  helper all include.
- **Two phases, as Haiku does it.** Haiku's `boot_splash_set_stage`
  (`~/Git/haiku/src/system/kernel/boot_splash.cpp:67-94`) lights a fixed
  slice `width * stage / MAX` for each stage of a fixed enum
  (`headers/private/kernel/boot_splash.h:14-24`). Astra does the same with
  two phases:
  - **Fixed phase, first 30% of the bar.** 21 steps known at compile time:
    the ROM's 10 (9 POST checks and the kernel image load), the kernel's 4,
    and supervisor stages 1-7. Each step is one equal increment.
  - **Services phase, last 70%.** It is divided evenly into N parts once the
    supervisor announces N. Stage 8 (DONE) is 100%.
- **Record fields.** The record keeps the counts, not a fill:
  - fixed step;
  - an `announced` flag;
  - service count N;
  - services done.

  The helper computes the fill with `astra_boot_progress_fill`, a function
  in the shared header. Every transition leaves the 32-bit value no smaller
  than it was. DONE is exact: all 21 fixed steps, an announcement, and
  exactly N service reports.
- **Who writes.** The ROM creates the record and counts its steps. It stops
  writing at handoff, and from then on the kernel is the only writer.
- **The helper.** The ARM helper `astra-terminal-display` draws the RGB565
  splash and the bar. It stops at the guest's first frame through the
  existing `display_owned` handoff. It shows the text page instead when the
  record says FAILED, after a panic, or when the record version is unknown.

**Deviation from Haiku, with the reason.** Haiku's whole stage count is a
compile-time enum. Astra's service count is data: the manifest on the volume
plus the user's `/config/services`. So only the 70% services share is
divided at run time.

Design: `/Users/barry/Git/astra68/.superpowers/sdd/boot-splash-design.md`.
Facts: `/Users/barry/Git/astra68/.superpowers/sdd/splash-investigation.md`.

**Tech Stack:** C11 freestanding (ROM, kernel), m68k-astra-gcc on `beast`;
C11 Linux (the aarch64 helper and its host-built self-test); Python 3 gates
on QEMU 9.2.4 `astra68`.

## Global Constraints

- **Scope.** DE25 only. QEMU-on-host stays headless and runs no helper.
  Arty is untouched: the helper's splash argument is optional, and the Arty
  release ships no splash.
- **Look.**
  - Art: `sw/boot/assets/astra_boot_splash_1920x1080_blank.png`, packed by
    `sw/boot/pack_arty_splash.py` to big-endian RGB565 (4,147,200 bytes). No
    new art.
  - Bar: thin, horizontal, inside the art's lower panel. Position x 420,
    y 834, size 880 x 6 px. Track color RGB565 `0x1187`, fill `0x4F3E`.
    These are tuned on the board in Task 8.
- **Text console only on failure:**
  - the record's state is FAILED;
  - a kernel panic (`ASTRA_DISPLAY_PANIC_TEXT`);
  - a record version the helper does not know.

  A failure that happens after the guest's first frame stays in the record
  and the log only. That is accepted; `DEBUGGING.md` says so. Serial output
  is unchanged except for one new line, `Boot services ....... N to start`.
- **Progress is counted, never timed.** No clock moves the bar. The bar
  never moves backwards.
- **Record ABI (`sw/include/astra/boot_progress.h`), verbatim:**
  - Header word at text-page offset `0xFE0`, value word at `0xFE4`. Both are
    big-endian, naturally aligned, and sit in the unused bytes 2700..4087.
    The cells are 0..2699 and the cursor block is 4088..4095.
  - Header = `0x42500001`: magic `"BP"` (`0x4250`) in the high half,
    version 1 in the low half.
  - Value bit layout:

    | Bits | Field |
    |---|---|
    | 31..28 | state: 0 BOOTING, 1 DONE, 2 FAILED |
    | 27 | announced |
    | 26..21 | fixed step (0..21) |
    | 20 | zero |
    | 19..10 | service count N (0..1023) |
    | 9..0 | services done |

  - Fixed-phase steps: `ROM_STEPS` 10, `KERNEL_STEPS` 4,
    `FIXED_STEPS` = 10 + 4 + `ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED` (7)
    = 21.
  - Fill in pixels of bar width W: W when DONE; otherwise
    `W*300*fixed/(1000*21)` + (once announced and N > 0)
    `W*700*done/(1000*N)`, capped at W.
  - DONE at stage 8 if and only if announced, `fixed == 21` and
    `done == N`. Anything else at stage 8 is FAILED.
  - Every legal transition leaves the 32-bit value greater than or equal to
    its previous value.
- **One writer at a time.**
  - The ROM writes from `screen_init` until `boot_kernel_handoff`.
  - The kernel adopts the record only if the header is exactly
    `0x42500001`, and marks it FAILED if the ROM did not count exactly 10
    steps. From then on the kernel is the sole writer.
  - Writes happen only when `SYS_VIDEO_READY` and `VEGA_CAP_POST_TEXT` are
    set, the same condition text output already uses.
- **Memory ordering.**
  - The writer publishes the value with one aligned 32-bit store: an m68k
    `move.l` becomes one host store into QEMU's `RAM_SHARED` file mapping.
    An aligned 32-bit access is single-copy atomic on Armv8 and x86.
  - The ROM writes the value (0) before the header. The header never changes
    after that, and `run-arty.sh` zeroes the page before every QEMU start.
  - The helper reads each word with one
    `__atomic_load_n(..., __ATOMIC_RELAXED)` on a `volatile const uint32_t *`
    and converts from big-endian. Acquire is not needed: nothing else is read
    on the strength of this word, and the text cells keep their own sequence
    protocol (`display.h:261-285`).
  - The Python gate reads each word twice and accepts it only when both reads
    agree.
- **Syscall ABI.**
  - `ASTRA_SYSCALL_PROGRESS` gains boot commands, encoded in `data[1]` by
    prefix the same way the qualification commands are
    (`sw/kernel/qualification.h:83-84`):
    - mask `0xffff0000`, prefix `0x42530000` (`"BS"`);
    - operation in bits 15..12: `0x1000` SERVICES (argument N), `0x2000`
      SERVICE (argument 0 READY, 1 FAILED, 2 SKIPPED);
    - argument in bits 11..0.
  - Only the registered initial image may send them; any other process gets
    `ASTRA_SYSCALL_ACCESS_DENIED`.
  - The change is additive: `ASTRA_SYSCALL_ABI_VERSION` goes
    `0x00010040u` to `0x00010041u`, following the precedent `1707179a` set
    (3F to 40 for new syscalls). Consumers check for exact equality, so
    everything is rebuilt together. Commit with `!`.
- **Launch order and behavior stay unchanged.** Storage starts first, then
  the manifest in file order (minus `trusted` and policy-skipped entries),
  then `/config/services` definitions in scan order. The existing gates
  prove it.
- **Kernel arithmetic.** The kernel links no libgcc. The record code uses
  only constant shifts, masks and 32-bit adds (`CLAUDE.md`). The fill's
  divisions run only in the helper and the tests.
- **beast: isolation.** The splash work never builds in
  `beast:~/Git/astra68`, where a filesystem branch builds concurrently.
  - It uses its own tree, `~/Git/astra68-splash`, its own images in
    `~/astra-mg/splash/`, its own logs under `/tmp/splash/mg-`, and its own
    QEMU cache `~/.cache/astra68/qemu-9.2.4-splash`.
  - Every command runs through `~/astra-mg/splash-env` (Task 0). It sets
    those paths and takes the shared build lock, so only one build runs at a
    time across both branches.
  - Mac sync, from `/Users/barry/Git/astra68-splash`:
    `rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/`
    Never sync while one of this branch's own builds is running.
  - Build userspace before `sw/boot`; `build-all.sh` does this.
  - Run `make -C sw/kernel clean` between the m68k build and the kernel host
    tests.
- **QEMU.** This plan does not change `astra68.c`. The splash QEMU host
  binary is named by the last line of `$ASTRA_QEMU_BUILD_LOG`, which is
  `/tmp/splash/mg-qemu-host-build.log` under `splash-env`.
- **Board.** The DE25 runs Ubuntu 22.04 (`CLAUDE.md:59`). It is reached as
  `root@192.168.1.52` from beast.
- **Never commit with failing tests.** Every task ends green on beast.
- **Commit trailer.** Every commit ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
- **Perturbation.** Every behavior change gets a step that breaks it on
  purpose, sees the expected failure, and then reverts.

---

## File Structure

| File | Responsibility |
|---|---|
| `sw/include/astra/boot_progress.h` (new) | Record layout, phase constants, the pure transitions and `astra_boot_progress_fill`, shared by ROM, kernel and helper |
| `sw/include/astra/syscall.h` | `ASTRA_PROGRESS_BOOT_*` commands; ABI `0x00010041u` |
| `sw/kernel/platform.c`, `platform.h` | `kernel_platform_post_text_read32/write32` |
| `sw/kernel/boot_progress.c`, `boot_progress.h` (new) | Kernel side of the record: adopt, step, stage, command, fail |
| `sw/kernel/kernel.c` | Kernel steps; panic, degraded exit and idle recovery mark FAILED; stage hook; the boot-command callback and its console line |
| `sw/kernel/process.c`, `process.h` | Route boot commands from the initial image |
| `sw/kernel/tests/test_boot_progress.c` (new), `tests/test_platform.c`, `tests/test_process.c`, `Makefile` | Host tests |
| `sw/boot/boot.c`, `sw/boot/Makefile` | ROM creates the record, counts its 10 steps, marks POST failure |
| `sw/userspace/supervisor/src/loader.c` | Resolve the boot launch list up front, announce N, report each launch |
| `sw/userspace/services/fault-probe/main.c` | `--fail-ready`: report READY with a failure status |
| `emu/qemu/astra_image.py` | `--ready-fail` profile; `configuration=` parameter |
| `emu/qemu/test-boot-progress.py` (new) | The gate |
| `fpga/arty/linux/astra_terminal_display.c`, `Makefile` | Splash scene, bar, scene choice, self-test |
| `emu/qemu/run-arty.sh`, `emu/qemu/test-run-arty.py` | Pass the splash when the release ships one |
| `emu/qemu/create-de25-release.sh`, `emu/qemu/test-create-de25-release.py` | Ship `share/astra_boot_splash.rgb565` |
| Docs | `docs/ABI.md`, `docs/DEBUGGING.md`, `docs/CURRENT_STATE.md`, `sw/boot/assets/README.md` |
| beast only (not in git) | New: `~/astra-mg/tree-env.sh`, `splash-env`, `splash-dryrun.sh`. Edited, each backed up as `<name>.pre-splash`: `build-all.sh`, `images.sh`, `gates.sh`, `suite.sh`, `t9-verify.sh`, `verify-nopc.sh`, `verify-then-publish.sh` |

---

### Task 0: Isolate the splash tree on beast, then take the baseline

**Shared state.** Each item below is fixed by the variable shown. Every
default is today's literal value.

| Shared today | Read or written by | Per tree with |
|---|---|---|
| `~/Git/astra68` (`cd`) | all seven scripts (`gates.sh` spells it `/home/barry/Git/astra68`) | `ASTRA_TREE` |
| `~/astra-mg/*.img` (`I=~/astra-mg`) | `images.sh`, `gates.sh`, `suite.sh`, `t9-verify.sh`, `verify-nopc.sh` | `ASTRA_IMG_DIR` |
| `/tmp/mg-*.log` | `build-all.sh`, `images.sh`, `gates.sh` | `ASTRA_LOG_PREFIX` |
| `/tmp/qemu-host-build.log` | written by `build-all.sh`; read by `gates.sh`, `suite.sh`, `t9-verify.sh`, `verify-nopc.sh` | `ASTRA_QEMU_BUILD_LOG` (derived) |
| `/tmp/su-*.log` | `suite.sh`, `verify-nopc.sh` | `ASTRA_SUITE_PREFIX` (derived) |
| `/tmp/v-*` (logs, `/tmp/v-fs`, `/tmp/v-mb.img`, `/tmp/v-s5.log`) | `verify-nopc.sh`, `verify-then-publish.sh` | `ASTRA_VERIFY_PREFIX` (derived) |
| `~/.cache/astra68/qemu-9.2.4` | `build-all.sh`, `verify-then-publish.sh` | `ASTRA_QEMU_WORK_ROOT` |
| `/tmp/astra-build.lock` | `build-lock.sh` | stays shared on purpose: one build at a time across branches |

The three derived values equal today's literals whenever
`ASTRA_LOG_PREFIX` is unset or `/tmp/mg-`. Under `splash-env` they become
`/tmp/splash/mg-qemu-host-build.log`, `/tmp/splash/mg-su-` and
`/tmp/splash/mg-v-`.

What stays shared, read-only:

- the m68k toolchain;
- the DE25 sysroot `~/.cache/astra68/de25-jammy-arm64`;
- `~/Git/SDL2*`.

`t9-verify.sh` uses `$I` for both scripts and images. The edit gives the
scripts their own fixed path, `~/astra-mg`.

**Files:**
- Create on beast: `~/astra-mg/tree-env.sh`, `~/astra-mg/splash-env`,
  `~/astra-mg/splash-dryrun.sh`
- Modify on beast: the seven scripts above, after `cp X X.pre-splash`

**Interfaces:**
- Produces `~/astra-mg/splash-env COMMAND...`. It runs COMMAND in
  `~/Git/astra68-splash` with the splash paths exported, under the build
  lock. Every later task uses it.
- `bash ~/astra-mg/<script>.sh` keeps working unchanged for the other
  branch.

- [ ] **Step 1: Back up the seven scripts**

```bash
rtk proxy ssh beast 'cd ~/astra-mg && for f in build-all.sh images.sh gates.sh suite.sh t9-verify.sh verify-nopc.sh verify-then-publish.sh; do [ -e $f.pre-splash ] || cp -p $f $f.pre-splash; done; ls -l *.pre-splash'
```

Expected: seven backups. Never overwrite an existing `.pre-splash`.

- [ ] **Step 2: `tree-env.sh` and `splash-env`**

```bash
rtk proxy ssh beast 'cat > ~/astra-mg/tree-env.sh <<'"'"'EOF'"'"'
# Sourced by the ~/astra-mg entry scripts, after build-lock.sh. Unset, every
# value is what the scripts hard-coded before, so the canonical tree's runs
# are unchanged byte for byte (splash-dryrun.sh proves it). Set by
# splash-env for ~/Git/astra68-splash.
ASTRA_TREE=${ASTRA_TREE:-$HOME/Git/astra68}
ASTRA_IMG_DIR=${ASTRA_IMG_DIR:-$HOME/astra-mg}
ASTRA_LOG_PREFIX=${ASTRA_LOG_PREFIX:-/tmp/mg-}
if [ "$ASTRA_LOG_PREFIX" = /tmp/mg- ]; then
    ASTRA_QEMU_BUILD_LOG=/tmp/qemu-host-build.log
    ASTRA_SUITE_PREFIX=/tmp/su-
    ASTRA_VERIFY_PREFIX=/tmp/v-
else
    ASTRA_QEMU_BUILD_LOG=${ASTRA_LOG_PREFIX}qemu-host-build.log
    ASTRA_SUITE_PREFIX=${ASTRA_LOG_PREFIX}su-
    ASTRA_VERIFY_PREFIX=${ASTRA_LOG_PREFIX}v-
fi
export ASTRA_TREE ASTRA_IMG_DIR ASTRA_LOG_PREFIX ASTRA_QEMU_BUILD_LOG \
    ASTRA_SUITE_PREFIX ASTRA_VERIFY_PREFIX
EOF
cat > ~/astra-mg/splash-env <<'"'"'EOF'"'"'
#!/bin/bash
# ~/astra-mg/splash-env COMMAND...
# The boot-splash branch: its own tree, images, logs and QEMU cache, under
# the build lock every branch shares (one build at a time on beast).
export ASTRA_TREE=$HOME/Git/astra68-splash
export ASTRA_IMG_DIR=$HOME/astra-mg/splash
export ASTRA_LOG_PREFIX=/tmp/splash/mg-
export ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4-splash
mkdir -p "$ASTRA_IMG_DIR" /tmp/splash
source ~/astra-mg/build-lock.sh
source ~/astra-mg/tree-env.sh
cd "$ASTRA_TREE" || exit 1
exec "$@"
EOF
chmod +x ~/astra-mg/splash-env'
```

- [ ] **Step 3: Parameterize the seven scripts**

The edit is mechanical. Every replacement must match the expected number of
times, or nothing is written:

```bash
rtk proxy ssh beast 'python3 - <<'"'"'EOF'"'"'
import pathlib, re
root = pathlib.Path.home() / "astra-mg"
LOCK = "source ~/astra-mg/build-lock.sh\n"
scripts = ["build-all.sh", "images.sh", "gates.sh", "suite.sh",
           "t9-verify.sh", "verify-nopc.sh", "verify-then-publish.sh"]
edited = {}
for name in scripts:
    s = (root / name).read_text()
    assert s.count(LOCK) == 1, (name, "lock line")
    assert "tree-env.sh" not in s, (name, "already edited")
    s = s.replace(LOCK, LOCK + "source ~/astra-mg/tree-env.sh\n")
    # t9 runs sibling scripts through $I; scripts stay in ~/astra-mg.
    for script in ("build-all.sh", "images.sh", "suite.sh"):
        s = s.replace("$I/" + script, "~/astra-mg/" + script)
    s = s.replace("cd /home/barry/Git/astra68\n", "cd \"$ASTRA_TREE\"\n")
    s = s.replace("cd ~/Git/astra68", "cd \"$ASTRA_TREE\"")
    s = s.replace("I=~/astra-mg", "I=$ASTRA_IMG_DIR")
    s = s.replace("/tmp/qemu-host-build.log", "$ASTRA_QEMU_BUILD_LOG")
    s = s.replace("/tmp/mg-", "${ASTRA_LOG_PREFIX}")
    s = s.replace("/tmp/su-", "${ASTRA_SUITE_PREFIX}")
    s = s.replace("/tmp/v-", "${ASTRA_VERIFY_PREFIX}")
    s = s.replace("ASTRA_QEMU_WORK_ROOT=$HOME/.cache/astra68/qemu-9.2.4",
                  "ASTRA_QEMU_WORK_ROOT=${ASTRA_QEMU_WORK_ROOT:-$HOME/.cache/astra68/qemu-9.2.4}")
    for leftover in ("Git/astra68", "/tmp/mg-", "/tmp/su-", "/tmp/v-",
                     "qemu-host-build.log", "I=~/astra-mg"):
        assert leftover not in s, (name, leftover)
    edited[name] = s
for name, s in edited.items():
    (root / name).write_text(s)
print("EDITED", len(edited))
EOF
cd ~/astra-mg && for f in build-all.sh images.sh gates.sh suite.sh t9-verify.sh verify-nopc.sh verify-then-publish.sh; do bash -n $f || echo SYNTAX-$f; done; diff build-all.sh.pre-splash build-all.sh'
```

Expected: `EDITED 7`, no `SYNTAX-` lines, and a `build-all.sh` diff that
shows only the substitutions. If an assertion fires, the script on beast
differs from the one this plan was written against (`81fe6e03`, read
2026-10-08). Read it and adapt the edit. Never force it.

- [ ] **Step 4: The dry-run harness**

`~/astra-mg/splash-dryrun.sh MODE` runs `t9-verify.sh` and
`verify-then-publish.sh` with every build and test command stubbed, in a
private `/tmp` (`unshare -rm`, checked available on beast). It prints the
scripts' output, then every file they left in `/tmp`, then the sorted
command trace (cwd, command, arguments, and QEMU's work root). Modes:

- `old`: the `.pre-splash` scripts;
- `new`: the edited scripts with nothing set;
- `splash`: the edited scripts under `splash-env`.

```bash
rtk proxy ssh beast 'cat > ~/astra-mg/splash-dryrun.sh <<'"'"'EOF'"'"'
#!/bin/bash
# ~/astra-mg/splash-dryrun.sh old|new|splash -- what the entry scripts would
# run, with builds and tests stubbed and /tmp private. old and new must match.
set -eu
mode=$1
real=$HOME/astra-mg
dry=$HOME/splash-dry
rm -rf "$dry"
mkdir -p "$dry/home/astra-mg" "$dry/stub"
for script in build-all.sh images.sh gates.sh suite.sh t9-verify.sh \
              verify-nopc.sh verify-then-publish.sh; do
    if [ "$mode" = old ]; then
        cp "$real/$script.pre-splash" "$dry/home/astra-mg/$script"
    else
        cp "$real/$script" "$dry/home/astra-mg/$script"
    fi
done
cp "$real/build-lock.sh" "$real/tree-env.sh" "$real/splash-env" \
   "$dry/home/astra-mg/"
cat > "$dry/stub/record" <<'"'"'STUB'"'"'
#!/bin/bash
name=$(basename "$0")
printf "%s\t%s\t%s\n" "$PWD" "$name" "$*" >> "$HOME/trace"
if [ "$name" = build.sh ]; then
    printf "%s\tbuild.sh-env\tASTRA_QEMU_WORK_ROOT=%s\n" "$PWD" \
        "${ASTRA_QEMU_WORK_ROOT:-}" >> "$HOME/trace"
    echo /fake/qemu-system-m68k
fi
exit 0
STUB
chmod +x "$dry/stub/record"
for name in make python3 cp rm; do ln -s record "$dry/stub/$name"; done
for tree in astra68 astra68-splash; do
    mkdir -p "$dry/home/Git/$tree/emu/qemu"
    ln -s "$dry/stub/record" "$dry/home/Git/$tree/emu/qemu/build.sh"
    ln -s "$dry/stub/record" "$dry/home/Git/$tree/emu/qemu/fs-gates.sh"
done
cat > "$dry/run" <<'"'"'RUN'"'"'
#!/bin/bash
mount -t tmpfs tmpfs /tmp
export HOME=$DRY_HOME PATH=$DRY_STUB:$PATH ASTRA_BUILD_LOCK_HELD=1
cd "$HOME"
for script in t9-verify.sh verify-then-publish.sh; do
    echo "== $script"
    if [ "$DRY_MODE" = splash ]; then
        ~/astra-mg/splash-env bash ~/astra-mg/$script
    else
        bash ~/astra-mg/$script
    fi
done
echo "== /tmp"; find /tmp -mindepth 1 | sort
echo "== trace"; sort "$HOME/trace"
RUN
DRY_HOME=$dry/home DRY_STUB=$dry/stub DRY_MODE=$mode \
    unshare -rm --propagation private bash "$dry/run" 2>&1
EOF
chmod +x ~/astra-mg/splash-dryrun.sh'
```

- [ ] **Step 5: Prove the defaults are unchanged and the splash run is isolated**

```bash
rtk proxy ssh beast '~/astra-mg/locked bash -c "~/astra-mg/splash-dryrun.sh old > ~/splash-dry-old.txt; ~/astra-mg/splash-dryrun.sh new > ~/splash-dry-new.txt; ~/astra-mg/splash-dryrun.sh splash > ~/splash-dry-splash.txt"; wc -l ~/splash-dry-old.txt; diff ~/splash-dry-old.txt ~/splash-dry-new.txt && echo DEFAULTS-IDENTICAL; grep -nE "Git/astra68([/[:space:]]|$)|/tmp/(mg|su|v)-|/tmp/qemu-host-build|astra-mg/[a-z0-9-]+\.img|qemu-9\.2\.4([[:space:]]|$)" ~/splash-dry-splash.txt || echo SPLASH-ISOLATED; grep -c "astra68-splash" ~/splash-dry-splash.txt'
```

Expected:

- `old` has several hundred lines, including `make`, `python3` and
  `build.sh` trace lines and `/tmp/mg-*` files;
- `DEFAULTS-IDENTICAL`;
- `SPLASH-ISOLATED`;
- a nonzero `astra68-splash` count.

`/tmp/splash/mg-...` and `~/astra-mg/splash/base.img` do not match the
grep, by construction.

Perturb: in `~/astra-mg/tree-env.sh`, change the default
`ASTRA_LOG_PREFIX` to `/tmp/mgx-`. Rerun the `new` dry run and the diff.
Expected: the diff shows `/tmp/mgx-` lines. Revert and rerun:
`DEFAULTS-IDENTICAL`.

- [ ] **Step 6: Sync the tree and take the baseline**

```bash
cd /Users/barry/Git/astra68-splash && git log --oneline -1
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast 'nohup ~/astra-mg/splash-env bash ~/astra-mg/verify-then-publish.sh > /tmp/splash/baseline.out 2>&1 &'
```

The first build in the new QEMU cache builds QEMU from scratch. Poll until
the run finishes:

```bash
rtk proxy ssh beast 'grep -E "READY-TO-PUBLISH|VERIFY-FAILED" /tmp/splash/mg-v-s5.log; tail -3 /tmp/splash/mg-v-s5.log'
```

Expected: `READY-TO-PUBLISH`. A failure here is on `main` and not caused by
this plan. Stop and report it.

The run takes the shared lock. If it prints `ASTRA-BUILD-BUSY`, the
filesystem branch is building: wait and retry. Never work around the lock.

- [ ] **Step 7: Boot-time baseline**

```bash
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "cp \$ASTRA_IMG_DIR/base.img /tmp/splash/tb.img && python3 emu/qemu/time-boot.py \"\$(tail -n1 \$ASTRA_QEMU_BUILD_LOG)\" sw/boot/build/astra_boot.bin --image /tmp/splash/tb.img --runs 3" | tee /tmp/splash/timeboot-before.txt'
```

No commit: nothing in git changed.

---

### Task 1: The record ABI, its transitions, and the fill

**Files:**
- Create: `sw/include/astra/boot_progress.h`
- Create: `sw/kernel/tests/test_boot_progress.c`. The pure half is written
  here; Task 2 adds the kernel half.
- Modify: `sw/kernel/Makefile` (new host test)

**Interfaces:**
- Produces, all `static inline` and pure:
  - `astra_boot_progress_pack(state, announced, fixed, count, done)`
  - accessors `astra_boot_progress_state/announced/fixed/count/done(uint32_t)`
  - `astra_boot_progress_advance(uint32_t)`: one fixed-phase step
  - `astra_boot_progress_announce(uint32_t, uint32_t services)`
  - `astra_boot_progress_service(uint32_t, uint32_t failed)`
  - `astra_boot_progress_finish(uint32_t)`: stage 8
  - `astra_boot_progress_fail(uint32_t)`
  - `astra_boot_progress_fill(uint32_t value, uint32_t width) -> uint32_t`
- Constants:
  - offsets `ASTRA_BOOT_PROGRESS_HEADER_OFFSET` (0xFE0) and `..._VALUE_OFFSET`
    (0xFE4);
  - `ASTRA_BOOT_PROGRESS_HEADER` (0x42500001);
  - `..._STATE_BOOTING/DONE/FAILED` (0/1/2);
  - `..._ROM_STEPS` (10), `..._KERNEL_STEPS` (4), `..._FIXED_STEPS` (21);
  - `..._SERVICES_MAX` (1023).
- Consumed by Tasks 2, 4 and 6, and by the gate's constants in Task 5.

- [ ] **Step 1: Write the failing test**

`sw/kernel/tests/test_boot_progress.c`:

```c
#include <astra/boot_progress.h>

#include <assert.h>
#include <stdio.h>

#define PACK astra_boot_progress_pack
#define BOOTING ASTRA_BOOT_PROGRESS_STATE_BOOTING
#define DONE ASTRA_BOOT_PROGRESS_STATE_DONE
#define FAILED ASTRA_BOOT_PROGRESS_STATE_FAILED
#define FIXED ASTRA_BOOT_PROGRESS_FIXED_STEPS
#define FILL astra_boot_progress_fill

static void test_transitions(void)
{
    uint32_t value = 0u;
    uint32_t next;

    /* The fields sit where the ABI says. */
    assert(FIXED == 21u);
    assert(PACK(FAILED, 1u, 0x15u, 0x3ffu, 0x155u) == 0x2aaffd55u);
    assert(astra_boot_progress_state(0x2aaffd55u) == FAILED);
    assert(astra_boot_progress_announced(0x2aaffd55u) == 1u);
    assert(astra_boot_progress_fixed(0x2aaffd55u) == 0x15u);
    assert(astra_boot_progress_count(0x2aaffd55u) == 0x3ffu);
    assert(astra_boot_progress_done(0x2aaffd55u) == 0x155u);

    /* The fixed phase: 21 steps, each one larger. */
    for (uint32_t step = 0u; step < FIXED; ++step) {
        next = astra_boot_progress_advance(value);
        assert(next > value);
        value = next;
    }
    assert(value == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    /* A 22nd fixed step is a miscount. */
    assert(astra_boot_progress_advance(value) ==
           PACK(FAILED, 0u, FIXED, 0u, 0u));

    /* The announcement. */
    next = astra_boot_progress_announce(value, 14u);
    assert(next == PACK(BOOTING, 1u, FIXED, 14u, 0u) && next > value);
    value = next;
    /* Once only, and no fixed step after it. */
    assert(astra_boot_progress_announce(value, 3u) ==
           PACK(FAILED, 1u, FIXED, 14u, 0u));
    assert(astra_boot_progress_advance(value) ==
           PACK(FAILED, 1u, FIXED, 14u, 0u));
    /* Announced before the fixed phase is complete: counted, but FAILED. */
    assert(astra_boot_progress_announce(PACK(BOOTING, 0u, 20u, 0u, 0u), 3u) ==
           PACK(FAILED, 1u, 20u, 3u, 0u));
    assert(astra_boot_progress_state(astra_boot_progress_announce(
               PACK(BOOTING, 0u, FIXED, 0u, 0u),
               ASTRA_BOOT_PROGRESS_SERVICES_MAX + 1u)) == FAILED);

    /* Fourteen services, then stage 8. */
    for (uint32_t service = 0u; service < 14u; ++service) {
        next = astra_boot_progress_service(value, 0u);
        assert(next > value);
        value = next;
    }
    assert(value == PACK(BOOTING, 1u, FIXED, 14u, 14u));
    /* A fifteenth report is a miscount. */
    assert(astra_boot_progress_service(value, 0u) ==
           PACK(FAILED, 1u, FIXED, 14u, 14u));
    next = astra_boot_progress_finish(value);
    assert(next == PACK(DONE, 1u, FIXED, 14u, 14u) && next > value);
    value = next;
    /* DONE takes no more steps; only a failure moves it, upward. */
    assert(astra_boot_progress_advance(value) == value);
    assert(astra_boot_progress_service(value, 0u) == value);
    assert(astra_boot_progress_fail(value) ==
           PACK(FAILED, 1u, FIXED, 14u, 14u));
    assert(astra_boot_progress_fail(value) > value);

    /* Stage 8 is DONE only when every step arrived. */
    assert(astra_boot_progress_finish(PACK(BOOTING, 1u, FIXED, 14u, 13u)) ==
           PACK(FAILED, 1u, FIXED, 14u, 13u));
    assert(astra_boot_progress_finish(PACK(BOOTING, 0u, FIXED, 0u, 0u)) ==
           PACK(FAILED, 0u, FIXED, 0u, 0u));
    /* No services at all is a complete boot. */
    assert(astra_boot_progress_finish(astra_boot_progress_announce(
               PACK(BOOTING, 0u, FIXED, 0u, 0u), 0u)) ==
           PACK(DONE, 1u, FIXED, 0u, 0u));

    /* A service that fails: FAILED, and the count still completes. */
    value = astra_boot_progress_service(PACK(BOOTING, 1u, FIXED, 2u, 0u), 1u);
    assert(value == PACK(FAILED, 1u, FIXED, 2u, 1u));
    value = astra_boot_progress_service(value, 0u);
    assert(value == PACK(FAILED, 1u, FIXED, 2u, 2u));
    assert(astra_boot_progress_finish(value) == value);
}

static void test_fill(void)
{
    /* 30% over 21 equal fixed steps, then 70% over N services. */
    assert(FILL(0u, 880u) == 0u);
    assert(FILL(PACK(BOOTING, 0u, 7u, 0u, 0u), 880u) == 88u);
    assert(FILL(PACK(BOOTING, 0u, FIXED, 0u, 0u), 880u) == 264u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 0u), 880u) == 264u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 7u), 880u) == 572u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 14u, 14u), 880u) == 880u);
    assert(FILL(PACK(BOOTING, 1u, FIXED, 0u, 0u), 880u) == 264u);
    assert(FILL(PACK(DONE, 1u, FIXED, 0u, 0u), 880u) == 880u);
    assert(FILL(PACK(DONE, 1u, FIXED, 14u, 14u), 880u) == 880u);
}

int main(void)
{
    test_transitions();
    test_fill();
    puts("KERNEL BOOT PROGRESS PASS");
    return 0;
}
```

In `sw/kernel/Makefile`:

- Add `$(BUILD_DIR)/test_boot_progress` to `KERNEL_HOST_TESTS`, after
  `$(BUILD_DIR)/test_platform`.
- Add `./$(BUILD_DIR)/test_boot_progress` to the `test:` recipe, after
  `./$(BUILD_DIR)/test_platform`.
- Add the rule:

```make
$(BUILD_DIR)/test_boot_progress: tests/test_boot_progress.c \
                                 ../include/astra/boot_progress.h \
                                 ../include/astra/display.h \
                                 ../include/astra/supervisor.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-I. -I../include tests/test_boot_progress.c -o $@
```

- [ ] **Step 2: Run it and see it fail**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env make -C sw/kernel build/test_boot_progress'
```

Expected: `astra/boot_progress.h: No such file or directory`.

- [ ] **Step 3: Write `sw/include/astra/boot_progress.h`**

```c
#ifndef ASTRA_BOOT_PROGRESS_H
#define ASTRA_BOOT_PROGRESS_H

/*
 * The boot progress record: how far the machine has come up, counted, in
 * the unused tail of the shared text page (display.h). The DE25 display
 * helper draws the splash bar from it and shows the text page instead when
 * it says FAILED.
 *
 * Haiku lights a fixed slice of its splash per stage of a fixed enum
 * (src/system/kernel/boot_splash.cpp:67-94). The bar here has two phases:
 * the fixed steps every boot takes before services -- ROM, kernel,
 * supervisor stages 1-7 -- fill the first 30% in equal slices, and the
 * services fill the remaining 70% in N equal slices once the supervisor
 * announces N (Astra's service count is data, not a constant). Stage 8 is
 * 100%.
 *
 * Writers: the ROM from screen_init until handoff, then the kernel alone.
 * Each word is one aligned big-endian 32-bit store and one aligned 32-bit
 * load: no reader sees half of one. The header is written once per boot,
 * after the value is zeroed, and never changes.
 *
 * Value: state 31..28, announced 27, fixed step 26..21, zero 20, service
 * count 19..10, services done 9..0. Every legal transition leaves it no
 * smaller than it was; a reader that sees it decrease has found a bug.
 */

#include <stdint.h>

#include <astra/display.h>
#include <astra/supervisor.h>

#define ASTRA_BOOT_PROGRESS_HEADER_OFFSET 0xFE0u
#define ASTRA_BOOT_PROGRESS_VALUE_OFFSET  0xFE4u
#define ASTRA_BOOT_PROGRESS_MAGIC         0x4250u /* "BP" */
#define ASTRA_BOOT_PROGRESS_VERSION       1u
#define ASTRA_BOOT_PROGRESS_HEADER \
    ((ASTRA_BOOT_PROGRESS_MAGIC << 16) | ASTRA_BOOT_PROGRESS_VERSION)

#define ASTRA_BOOT_PROGRESS_STATE_BOOTING 0u
#define ASTRA_BOOT_PROGRESS_STATE_DONE    1u
#define ASTRA_BOOT_PROGRESS_STATE_FAILED  2u

/* The fixed phase: every step a successful boot takes before services. */
#define ASTRA_BOOT_PROGRESS_ROM_STEPS    10u /* 9 POST checks, kernel image */
#define ASTRA_BOOT_PROGRESS_KERNEL_STEPS 4u
#define ASTRA_BOOT_PROGRESS_FIXED_STEPS \
    (ASTRA_BOOT_PROGRESS_ROM_STEPS + ASTRA_BOOT_PROGRESS_KERNEL_STEPS + \
     ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED)
#define ASTRA_BOOT_PROGRESS_SERVICES_MAX 1023u
/* Bar shares in thousandths. */
#define ASTRA_BOOT_PROGRESS_FIXED_SHARE  300u
#define ASTRA_BOOT_PROGRESS_SHARE_TOTAL  1000u

#define ASTRA_BOOT_PROGRESS_ANNOUNCED   (UINT32_C(1) << 27)
#define ASTRA_BOOT_PROGRESS_FIXED_SHIFT 21
#define ASTRA_BOOT_PROGRESS_FIXED_MASK  0x3Fu
#define ASTRA_BOOT_PROGRESS_COUNT_SHIFT 10
#define ASTRA_BOOT_PROGRESS_COUNT_MASK  0x3FFu

_Static_assert(ASTRA_BOOT_PROGRESS_HEADER_OFFSET >=
                   ASTRA_TEXT_COLUMNS * ASTRA_TEXT_ROWS,
               "the boot progress record overlaps the text cells");
_Static_assert(ASTRA_BOOT_PROGRESS_VALUE_OFFSET + 4u <=
                   ASTRA_TEXT_CURSOR_OFFSET,
               "the boot progress record overlaps the cursor block");
_Static_assert((ASTRA_BOOT_PROGRESS_HEADER_OFFSET & 3u) == 0u &&
                   (ASTRA_BOOT_PROGRESS_VALUE_OFFSET & 3u) == 0u,
               "boot progress words must be naturally aligned");
_Static_assert(ASTRA_BOOT_PROGRESS_FIXED_STEPS <
                   ASTRA_BOOT_PROGRESS_FIXED_MASK,
               "the fixed phase must fit its field");

static inline uint32_t astra_boot_progress_pack(uint32_t state,
                                                uint32_t announced,
                                                uint32_t fixed,
                                                uint32_t count,
                                                uint32_t done)
{
    return (state << 28) |
           (announced != 0u ? ASTRA_BOOT_PROGRESS_ANNOUNCED : 0u) |
           ((fixed & ASTRA_BOOT_PROGRESS_FIXED_MASK)
            << ASTRA_BOOT_PROGRESS_FIXED_SHIFT) |
           ((count & ASTRA_BOOT_PROGRESS_COUNT_MASK)
            << ASTRA_BOOT_PROGRESS_COUNT_SHIFT) |
           (done & ASTRA_BOOT_PROGRESS_COUNT_MASK);
}

static inline uint32_t astra_boot_progress_state(uint32_t value)
{
    return value >> 28;
}

static inline uint32_t astra_boot_progress_announced(uint32_t value)
{
    return (value & ASTRA_BOOT_PROGRESS_ANNOUNCED) != 0u;
}

static inline uint32_t astra_boot_progress_fixed(uint32_t value)
{
    return (value >> ASTRA_BOOT_PROGRESS_FIXED_SHIFT) &
           ASTRA_BOOT_PROGRESS_FIXED_MASK;
}

static inline uint32_t astra_boot_progress_count(uint32_t value)
{
    return (value >> ASTRA_BOOT_PROGRESS_COUNT_SHIFT) &
           ASTRA_BOOT_PROGRESS_COUNT_MASK;
}

static inline uint32_t astra_boot_progress_done(uint32_t value)
{
    return value & ASTRA_BOOT_PROGRESS_COUNT_MASK;
}

/* FAILED from any state: the state nibble only ever rises. */
static inline uint32_t astra_boot_progress_fail(uint32_t value)
{
    return (value & 0x0FFFFFFFu) |
           (ASTRA_BOOT_PROGRESS_STATE_FAILED << 28);
}

/* One fixed-phase step. A step after the announcement or past the fixed
   count is a miscount. DONE takes none. */
static inline uint32_t astra_boot_progress_advance(uint32_t value)
{
    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE)
        return value;
    if (astra_boot_progress_announced(value) ||
        astra_boot_progress_fixed(value) >= ASTRA_BOOT_PROGRESS_FIXED_STEPS)
        return astra_boot_progress_fail(value);
    return value + (UINT32_C(1) << ASTRA_BOOT_PROGRESS_FIXED_SHIFT);
}

/* The supervisor's service count. Announced before the fixed phase is
   complete, it is recorded and FAILED. */
static inline uint32_t astra_boot_progress_announce(uint32_t value,
                                                    uint32_t services)
{
    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE ||
        astra_boot_progress_announced(value) ||
        services > ASTRA_BOOT_PROGRESS_SERVICES_MAX)
        return astra_boot_progress_fail(value);
    value |= ASTRA_BOOT_PROGRESS_ANNOUNCED |
             (services << ASTRA_BOOT_PROGRESS_COUNT_SHIFT);
    return astra_boot_progress_fixed(value) ==
                   ASTRA_BOOT_PROGRESS_FIXED_STEPS ?
               value : astra_boot_progress_fail(value);
}

/* One announced service finished launching; @p failed marks the boot. */
static inline uint32_t astra_boot_progress_service(uint32_t value,
                                                   uint32_t failed)
{
    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE)
        return value;
    if (!astra_boot_progress_announced(value) ||
        astra_boot_progress_done(value) >= astra_boot_progress_count(value))
        return astra_boot_progress_fail(value);
    value += 1u;
    return failed != 0u ? astra_boot_progress_fail(value) : value;
}

/* Stage 8. DONE only if every fixed step and every announced service
   arrived. */
static inline uint32_t astra_boot_progress_finish(uint32_t value)
{
    if (astra_boot_progress_state(value) != ASTRA_BOOT_PROGRESS_STATE_BOOTING)
        return value;
    return astra_boot_progress_announced(value) &&
                   astra_boot_progress_fixed(value) ==
                       ASTRA_BOOT_PROGRESS_FIXED_STEPS &&
                   astra_boot_progress_done(value) ==
                       astra_boot_progress_count(value) ?
               value | (ASTRA_BOOT_PROGRESS_STATE_DONE << 28) :
               astra_boot_progress_fail(value);
}

/* The bar's fill in pixels of @p width (width <= 4096). Never smaller for a
   larger value of the same boot: each phase only grows, DONE is all. */
static inline uint32_t astra_boot_progress_fill(uint32_t value,
                                                uint32_t width)
{
    uint32_t fixed = astra_boot_progress_fixed(value);
    uint32_t count = astra_boot_progress_count(value);
    uint32_t done = astra_boot_progress_done(value);
    uint32_t fill;

    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE)
        return width;
    if (fixed > ASTRA_BOOT_PROGRESS_FIXED_STEPS)
        fixed = ASTRA_BOOT_PROGRESS_FIXED_STEPS;
    fill = width * ASTRA_BOOT_PROGRESS_FIXED_SHARE * fixed /
           (ASTRA_BOOT_PROGRESS_SHARE_TOTAL * ASTRA_BOOT_PROGRESS_FIXED_STEPS);
    if (astra_boot_progress_announced(value) && count != 0u)
        fill += width *
                (ASTRA_BOOT_PROGRESS_SHARE_TOTAL -
                 ASTRA_BOOT_PROGRESS_FIXED_SHARE) *
                (done < count ? done : count) /
                (ASTRA_BOOT_PROGRESS_SHARE_TOTAL * count);
    return fill < width ? fill : width;
}

#endif
```

Overflow check: the largest product is
`4096 * 700 * 1023 = 2,933,145,600`, which is below 2^32. `fill` is never
called in the kernel.

- [ ] **Step 4: Pass**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "make -C sw/kernel build/test_boot_progress && sw/kernel/build/test_boot_progress"'
```

Expected: `KERNEL BOOT PROGRESS PASS`.

- [ ] **Step 5: Perturb**

1. In `astra_boot_progress_finish`, drop the `done == count` term.
   Expected: the `14u, 13u` assertion fails. Revert.
2. In `astra_boot_progress_fill`, change `FIXED_SHARE` to `250u`. Expected:
   `== 88u` fails. Revert.
3. In `astra_boot_progress_advance`, drop the
   `fixed >= ASTRA_BOOT_PROGRESS_FIXED_STEPS` test. Expected: the
   "22nd fixed step" assertion fails. Revert, then rerun Step 4: PASS.

- [ ] **Step 6: Commit**

```bash
git add sw/include/astra/boot_progress.h sw/kernel/tests/test_boot_progress.c sw/kernel/Makefile
git commit -m "feat(boot): boot progress record ABI, two-phase transitions and fill

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: The kernel writes the record

**Files:**
- Create: `sw/kernel/boot_progress.c`, `sw/kernel/boot_progress.h`
- Modify:
  - `sw/kernel/platform.c` and `platform.h`: word access;
  - `sw/kernel/kernel.c`: steps, stage hook, FAILED marks;
  - `sw/kernel/Makefile`;
  - `sw/include/astra/syscall.h`: the commands and the ABI bump.
- Test: `sw/kernel/tests/test_boot_progress.c`, `sw/kernel/tests/test_platform.c`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `bool kernel_platform_post_text_read32(uint32_t offset, uint32_t *value)`
  - `bool kernel_platform_post_text_write32(uint32_t offset, uint32_t value)`.
    The offset must be 4-aligned and at most `ASTRA_TEXT_PLANE_BYTES - 4`.
  - `void kernel_boot_progress_init(void)`
  - `uint32_t kernel_boot_progress_value(void)`
  - `void kernel_boot_progress_step(void)`
  - `void kernel_boot_progress_stage(uint32_t stage)`
  - `void kernel_boot_progress_fail(void)`
  - `uint32_t kernel_boot_progress_command(uint32_t command)`, which returns
    an `ASTRA_SYSCALL_*` status. Task 3 uses it.
  - `ASTRA_PROGRESS_BOOT_*` in `syscall.h`.

- [ ] **Step 1: Failing platform test**

In `sw/kernel/tests/test_platform.c`, after
`assert(!kernel_platform_post_text_write(VEGA_POST_COLS * VEGA_POST_ROWS, 0u));`
(about line 176), add:

```c
    {
        uint32_t word = 0u;

        assert(kernel_platform_post_text_write32(0xFE4u, 0x01400000u));
        assert(kernel_platform_post_text_read32(0xFE4u, &word));
        assert(word == 0x01400000u);
        /* Unaligned, past the page, or no destination: refused. */
        assert(!kernel_platform_post_text_write32(0xFE2u, 0u));
        assert(!kernel_platform_post_text_write32(ASTRA_TEXT_PLANE_BYTES - 2u,
                                                  0u));
        assert(!kernel_platform_post_text_read32(ASTRA_TEXT_PLANE_BYTES, &word));
        assert(!kernel_platform_post_text_read32(0xFE4u, NULL));
    }
```

Add `#include <astra/display.h>` to the test if
`grep -n "astra/display.h" sw/kernel/tests/test_platform.c` finds nothing.

- [ ] **Step 2: Failing kernel-module test**

In `sw/kernel/tests/test_boot_progress.c`, add after
`#include <astra/boot_progress.h>`:

```c
#include "boot_progress.h"
#include "platform.h"

#include <astra/syscall.h>
#include <vega.h>
#include <vesta.h>
```

Before `main`, add:

```c
static uint32_t record(void)
{
    uint32_t value = 0u;

    assert(kernel_platform_post_text_read32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                            &value));
    return value;
}

static void rom_wrote(uint32_t value)
{
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                             value));
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_HEADER_OFFSET,
                                             ASTRA_BOOT_PROGRESS_HEADER));
}

static uint32_t command(uint32_t operation, uint32_t argument)
{
    return kernel_boot_progress_command(operation | argument);
}

/* What a boot does between handoff and the announcement. */
static void kernel_and_stages(void)
{
    for (uint32_t step = 0u; step < ASTRA_BOOT_PROGRESS_KERNEL_STEPS; ++step)
        kernel_boot_progress_step();
    for (uint32_t stage = ASTRA_SUPERVISOR_STAGE_SELF_VERIFIED;
         stage <= ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED; ++stage)
        kernel_boot_progress_stage(stage);
}

static void test_kernel(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    VegaRegs *video = kernel_platform_test_vega_registers();
    const uint32_t rom = PACK(BOOTING, 0u, ASTRA_BOOT_PROGRESS_ROM_STEPS, 0u,
                              0u);
    uint32_t last;

    registers->SYS_STATUS = SYS_VIDEO_READY;
    video->ID = VEGA_ID_MAGIC;
    video->CAPS = VEGA_CAP_POST_TEXT;

    /* No ROM record: the kernel adopts nothing and writes nothing. */
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_HEADER_OFFSET,
                                             0u));
    assert(kernel_platform_post_text_write32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                             0u));
    kernel_boot_progress_init();
    kernel_boot_progress_step();
    assert(record() == 0u);

    /* The ROM's record is adopted and continued, never decreasing. */
    rom_wrote(rom);
    kernel_boot_progress_init();
    assert(record() == rom);
    last = record();
    kernel_boot_progress_step();
    assert(record() > last);
    for (uint32_t step = 1u; step < ASTRA_BOOT_PROGRESS_KERNEL_STEPS; ++step)
        kernel_boot_progress_step();
    for (uint32_t stage = ASTRA_SUPERVISOR_STAGE_SELF_VERIFIED;
         stage <= ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED; ++stage) {
        last = record();
        kernel_boot_progress_stage(stage);
        assert(record() > last);
    }
    assert(record() == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    /* A service report before the announcement is refused, unchanged. */
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(record() == PACK(BOOTING, 0u, FIXED, 0u, 0u));
    assert(command(ASTRA_PROGRESS_BOOT_SERVICES, 3u) == ASTRA_SYSCALL_OK);
    assert(record() == PACK(BOOTING, 1u, FIXED, 3u, 0u));
    assert(command(ASTRA_PROGRESS_BOOT_SERVICES, 3u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, 7u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(command(ASTRA_PROGRESS_BOOT_COMMAND_PREFIX | 0x3000u, 0u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(record() == PACK(BOOTING, 1u, FIXED, 3u, 0u));
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_OK);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE,
                   ASTRA_PROGRESS_SERVICE_SKIPPED) == ASTRA_SYSCALL_OK);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_OK);
    kernel_boot_progress_stage(ASTRA_SUPERVISOR_STAGE_TERMINAL);
    assert(record() == PACK(DONE, 1u, FIXED, 3u, 3u));
    assert(kernel_boot_progress_value() == record());
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    /* The console failing after boot is still a failure. */
    kernel_boot_progress_stage(ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED);
    assert(record() == PACK(FAILED, 1u, FIXED, 3u, 3u));

    /* A ROM that miscounted is caught at handoff. */
    rom_wrote(PACK(BOOTING, 0u, ASTRA_BOOT_PROGRESS_ROM_STEPS - 1u, 0u, 0u));
    kernel_boot_progress_init();
    assert(record() == PACK(FAILED, 0u, ASTRA_BOOT_PROGRESS_ROM_STEPS - 1u, 0u,
                            0u));

    /* A service that fails READY: FAILED, and the count still completes. */
    rom_wrote(rom);
    kernel_boot_progress_init();
    kernel_and_stages();
    assert(command(ASTRA_PROGRESS_BOOT_SERVICES, 2u) == ASTRA_SYSCALL_OK);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE,
                   ASTRA_PROGRESS_SERVICE_FAILED) == ASTRA_SYSCALL_OK);
    assert(astra_boot_progress_state(record()) == FAILED);
    assert(command(ASTRA_PROGRESS_BOOT_SERVICE, ASTRA_PROGRESS_SERVICE_READY) ==
           ASTRA_SYSCALL_OK);
    kernel_boot_progress_stage(ASTRA_SUPERVISOR_STAGE_TERMINAL);
    assert(record() == PACK(FAILED, 1u, FIXED, 2u, 2u));

    /* A panic or degraded exit marks FAILED from any state. */
    rom_wrote(rom);
    kernel_boot_progress_init();
    kernel_boot_progress_fail();
    assert(record() == astra_boot_progress_fail(rom));

    /* Without the text page the kernel tracks but never writes. */
    rom_wrote(rom);
    registers->SYS_STATUS = 0u;
    kernel_boot_progress_init();
    kernel_boot_progress_step();
    assert(kernel_boot_progress_value() == PACK(BOOTING, 0u, 1u, 0u, 0u));
    registers->SYS_STATUS = SYS_VIDEO_READY;
    assert(record() == rom);
}
```

Change `main` to call `test_transitions(); test_fill(); test_kernel();`.
`<astra/supervisor.h>` already comes in through `boot_progress.h`.

Replace the test's Makefile rule from Task 1 with this one, which links the
module:

```make
$(BUILD_DIR)/test_boot_progress: tests/test_boot_progress.c boot_progress.c \
                                 boot_progress.h platform.c platform.h \
                                 mmio.c mmio.h \
                                 ../include/astra/boot_progress.h \
                                 ../include/astra/display.h \
                                 ../include/astra/supervisor.h \
                                 ../include/astra/syscall.h \
                                 ../include/vega.h ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PLATFORM_HOST_TEST=1 -DKERNEL_MMIO_HOST_TEST=1 \
		-I. -I../include mmio.c platform.c boot_progress.c \
		tests/test_boot_progress.c -o $@
```

Run as in Task 1 Step 2. Expected: compile errors for the missing
`boot_progress.h`, `kernel_platform_post_text_read32` and
`ASTRA_PROGRESS_BOOT_SERVICE`.

- [ ] **Step 3: The syscall constants**

In `sw/include/astra/syscall.h`, replace the `ASTRA_SYSCALL_PROGRESS`
comment and define (lines 50-58) with:

```c
/**
 * Reports the calling process's own startup/boot progress. data[1] is the
 * new progress value; it must be no less than the value already recorded,
 * or the call answers ::ASTRA_SYSCALL_INVALID_ARGUMENT. The initial user
 * image's progress additionally drives the kernel's own boot-milestone
 * accounting and the boot progress record (astra/boot_progress.h).
 *
 * A data[1] whose high half is ::ASTRA_PROGRESS_BOOT_COMMAND_PREFIX is a boot
 * command, not a progress value, and those values are reserved from every
 * process's counter. Only the initial image may send one; anything else gets
 * ::ASTRA_SYSCALL_ACCESS_DENIED.
 */
#define ASTRA_SYSCALL_PROGRESS  1
#define ASTRA_PROGRESS_BOOT_COMMAND_MASK   0xffff0000u
#define ASTRA_PROGRESS_BOOT_COMMAND_PREFIX 0x42530000u /* "BS" */
#define ASTRA_PROGRESS_BOOT_ARGUMENT_MASK  0x00000fffu
/** SERVICES | N: N boot services will each report once before stage 8. Once. */
#define ASTRA_PROGRESS_BOOT_SERVICES (ASTRA_PROGRESS_BOOT_COMMAND_PREFIX | 0x1000u)
/** SERVICE | result: one announced boot service finished launching. */
#define ASTRA_PROGRESS_BOOT_SERVICE  (ASTRA_PROGRESS_BOOT_COMMAND_PREFIX | 0x2000u)
#define ASTRA_PROGRESS_SERVICE_READY   0u
#define ASTRA_PROGRESS_SERVICE_FAILED  1u
#define ASTRA_PROGRESS_SERVICE_SKIPPED 2u
```

Bump `#define ASTRA_SYSCALL_ABI_VERSION 0x00010040u` to `0x00010041u`.

`sw/kernel/user_test.S:663`'s `0x00010040` is the bulk-ring header version:
`ASTRA_BULK_RING_MAGIC` follows it. Do not touch it.

- [ ] **Step 4: Platform word access**

In `sw/kernel/platform.h`, after the `kernel_platform_post_text_write`
declaration (line 131), add:

```c
/* One aligned word of the text page, by byte offset (boot_progress.h). */
bool kernel_platform_post_text_read32(uint32_t offset, uint32_t *value);
bool kernel_platform_post_text_write32(uint32_t offset, uint32_t value);
```

In `sw/kernel/platform.c`, after `kernel_platform_post_text_write`, add:

```c
static bool post_text_word_valid(uint32_t offset)
{
    return (offset & 3u) == 0u && offset <= ASTRA_TEXT_PLANE_BYTES - 4u;
}

bool kernel_platform_post_text_read32(uint32_t offset, uint32_t *value)
{
    if (value == NULL || !post_text_word_valid(offset))
        return false;
    *value = kernel_mmio_read32(VEGA_POST_TEXT_BASE + offset);
    return true;
}

bool kernel_platform_post_text_write32(uint32_t offset, uint32_t value)
{
    if (!post_text_word_valid(offset))
        return false;
    kernel_mmio_write32(VEGA_POST_TEXT_BASE + offset, value);
    return true;
}
```

- [ ] **Step 5: The kernel module**

`sw/kernel/boot_progress.h`:

```c
#ifndef ASTRA_KERNEL_BOOT_PROGRESS_H
#define ASTRA_KERNEL_BOOT_PROGRESS_H

#include <stdint.h>

/*
 * The kernel's half of the boot progress record (astra/boot_progress.h).
 * The ROM creates it and counts its own steps; from handoff the kernel is
 * its only writer. Without a ROM record or a text page nothing is written,
 * but the value is still tracked so commands answer the same.
 */
void kernel_boot_progress_init(void);
uint32_t kernel_boot_progress_value(void);
/* One of the kernel's ASTRA_BOOT_PROGRESS_KERNEL_STEPS. */
void kernel_boot_progress_step(void);
/* A supervisor stage reached: 1-7 are fixed steps, 8 finishes, 9 fails. */
void kernel_boot_progress_stage(uint32_t stage);
void kernel_boot_progress_fail(void);
/* An ASTRA_PROGRESS_BOOT_* command; returns an ASTRA_SYSCALL_* status. */
uint32_t kernel_boot_progress_command(uint32_t command);

#endif
```

`sw/kernel/boot_progress.c`:

```c
#include "boot_progress.h"

#include "platform.h"

#include <astra/boot_progress.h>
#include <astra/supervisor.h>
#include <astra/syscall.h>

#include <stdbool.h>

static bool boot_progress_enabled;
static uint32_t boot_progress;

static void publish(uint32_t value)
{
    boot_progress = value;
    if (boot_progress_enabled)
        (void)kernel_platform_post_text_write32(
            ASTRA_BOOT_PROGRESS_VALUE_OFFSET, value);
}

void kernel_boot_progress_init(void)
{
    uint32_t header = 0u;
    uint32_t value = 0u;

    boot_progress_enabled =
        kernel_platform_post_text_present() &&
        kernel_platform_post_text_read32(ASTRA_BOOT_PROGRESS_HEADER_OFFSET,
                                         &header) &&
        header == ASTRA_BOOT_PROGRESS_HEADER &&
        kernel_platform_post_text_read32(ASTRA_BOOT_PROGRESS_VALUE_OFFSET,
                                         &value);
    boot_progress = boot_progress_enabled ? value : 0u;
    /* The ROM's count is part of the exact total: a miscount is a fault. */
    if (boot_progress_enabled &&
        astra_boot_progress_fixed(value) != ASTRA_BOOT_PROGRESS_ROM_STEPS)
        publish(astra_boot_progress_fail(value));
}

uint32_t kernel_boot_progress_value(void)
{
    return boot_progress;
}

void kernel_boot_progress_step(void)
{
    publish(astra_boot_progress_advance(boot_progress));
}

void kernel_boot_progress_fail(void)
{
    publish(astra_boot_progress_fail(boot_progress));
}

void kernel_boot_progress_stage(uint32_t stage)
{
    if (stage == ASTRA_SUPERVISOR_STAGE_TERMINAL)
        publish(astra_boot_progress_finish(boot_progress));
    else if (stage == ASTRA_SUPERVISOR_STAGE_CONSOLE_FAILED)
        kernel_boot_progress_fail();
    else
        kernel_boot_progress_step();
}

uint32_t kernel_boot_progress_command(uint32_t command)
{
    uint32_t argument = command & ASTRA_PROGRESS_BOOT_ARGUMENT_MASK;
    uint32_t announced = astra_boot_progress_announced(boot_progress);

    if (astra_boot_progress_state(boot_progress) ==
        ASTRA_BOOT_PROGRESS_STATE_DONE)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    switch (command & ~ASTRA_PROGRESS_BOOT_ARGUMENT_MASK) {
    case ASTRA_PROGRESS_BOOT_SERVICES:
        if (announced)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        publish(astra_boot_progress_announce(boot_progress, argument));
        return ASTRA_SYSCALL_OK;
    case ASTRA_PROGRESS_BOOT_SERVICE:
        if (!announced || argument > ASTRA_PROGRESS_SERVICE_SKIPPED)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        publish(astra_boot_progress_service(
            boot_progress, argument == ASTRA_PROGRESS_SERVICE_FAILED));
        return ASTRA_SYSCALL_OK;
    default:
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
}
```

In `sw/kernel/Makefile`:

- Add `$(BUILD_DIR)/boot_progress.o` to `OBJECTS` after
  `$(BUILD_DIR)/platform.o`.
- Add the rule:

```make
$(BUILD_DIR)/boot_progress.o: boot_progress.c boot_progress.h platform.h \
                              ../include/astra/boot_progress.h \
                              ../include/astra/display.h \
                              ../include/astra/supervisor.h \
                              ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
```

- Add `boot_progress.h` and `../include/astra/boot_progress.h` to the
  `$(BUILD_DIR)/kernel.o` prerequisites.

- [ ] **Step 6: Kernel hooks in `kernel.c`**

Add `#include "boot_progress.h"` after `#include "block.h"`.

1. **Adopt.** In `kernel_main`, immediately after `console_init();`:
   ```c
       kernel_boot_progress_init();
   ```
2. **Steps.** Add `kernel_boot_progress_step();` in four places. These are
   the `ASTRA_BOOT_PROGRESS_KERNEL_STEPS` (4); a fifth or a missing one
   makes the announcement FAILED.
   - Immediately after the whole
     `if (kernel_allocation_phase() != ... ) kernel_panic("runtime allocator validation failed");`
     statement: memory, VM and devices are up.
   - Immediately after
     `console_puts("Vesta timer ........ OK, one-shot 5 ms\n");`.
   - Immediately after the closing `}` of the
     `if ((kernel_platform_system_status() & SYS_ASTRA_HOST) != 0u) {...} else {...}`
     block: the host runtime has been probed.
   - Immediately after `start_initial_user_image();` inside the `#else` of
     `#if ASTRA_KERNEL_K1_QUALIFICATION`.
3. **Panic.** In `panic_begin`, after `panic_reason = reason;`:
   `kernel_boot_progress_fail();`
4. **Degraded exit.** In `kernel_process_initial_image_exited`, immediately
   before `console_puts("Initial image ....... EXITED, reason ");`:
   `kernel_boot_progress_fail();`. A clean shutdown or restart returns
   earlier and is unchanged.
5. **Idle recovery.** In `kernel_main`, immediately before
   `console_puts("Recovery ............ idle; interrupts and diagnostics "`:
   `kernel_boot_progress_fail();`
6. **Stage hook.** As the first statement of
   `kernel_process_initial_image_progress`, before the `#if`:
   ```c
       /* Before the line: whoever reads "stage 8" reads the final record. */
       kernel_boot_progress_stage(stage);
   ```
   This function runs only when the counter rises
   (`process.c:11450-11454`), so a stage repeated by `volume.c:378,401`
   counts once.

- [ ] **Step 7: Host tests pass**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "make -C sw/kernel clean && make -C sw/kernel -j32 test 2>&1 | tail -5"'
```

Expected: exit 0, with `KERNEL BOOT PROGRESS PASS` among the results.

- [ ] **Step 8: m68k build, float-free check and size**

```bash
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "bash ~/astra-mg/build-all.sh && make -C sw/kernel clean"'
```

Expected: `BUILD-OK`. The kernel link runs
`tools/check_kernel_float_free.py` and the `<= 0x80000` payload check.

- [ ] **Step 9: Perturb**

1. In `kernel_boot_progress_init`, drop the ROM-count check. Rerun Step 7.
   Expected: the "ROM that miscounted" assertion fails. Revert.
2. In the SERVICES case, drop `if (announced)`. Expected: the second
   announcement's `INVALID_ARGUMENT` assertion fails. Revert, then rerun:
   PASS.

- [ ] **Step 10: Commit**

```bash
git add sw/include/astra/syscall.h sw/kernel/boot_progress.c sw/kernel/boot_progress.h \
        sw/kernel/platform.c sw/kernel/platform.h sw/kernel/kernel.c sw/kernel/Makefile \
        sw/kernel/tests/test_boot_progress.c sw/kernel/tests/test_platform.c
git commit -m "feat(kernel)!: the kernel continues the boot progress record

Syscall ABI 0x00010041: PROGRESS boot commands are reserved values.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: The initial image's boot commands reach the record

**Files:**
- Modify: `sw/kernel/process.c` (PROGRESS handler, about line 11426);
  `sw/kernel/process.h` (callback declaration, after line 508);
  `sw/kernel/kernel.c` (the callback and its console line)
- Test: `sw/kernel/tests/test_process.c` (stub at line 1195; reset at
  line 1538; new cases after line 12385)
- Modify: `docs/ABI.md:126-128`

**Interfaces:**
- Consumes: `kernel_boot_progress_command` (Task 2).
- Produces: `uint32_t kernel_process_initial_image_boot_command(uint32_t command)`.
  It is declared in `process.h`, implemented in `kernel.c`, and stubbed in
  `test_process.c`.

- [ ] **Step 1: Failing test**

In `sw/kernel/tests/test_process.c`:

- Next to `initial_image_progress_reports` (line 87) add:

```c
static uint32_t boot_commands;
static uint32_t last_boot_command;
```

- After the `kernel_process_initial_image_progress` stub (line 1195) add:

```c
uint32_t kernel_process_initial_image_boot_command(uint32_t command)
{
    ++boot_commands;
    last_boot_command = command;
    return ASTRA_SYSCALL_OK;
}
```

- In `initialize_test`, after `last_initial_image_stage = 0u;` (line 1539),
  add `boot_commands = 0u; last_boot_command = 0u;`.
- After `assert(last_initial_image_stage == 3u);` (about line 12385) add:

```c
    /*
     * A boot command from the initial image goes to the record and is not a
     * progress value: the stage counter does not move.
     */
    registers[1] = ASTRA_PROGRESS_BOOT_SERVICES | 14u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(boot_commands == 1u);
    assert(last_boot_command == (ASTRA_PROGRESS_BOOT_SERVICES | 14u));
    assert(initial_image_progress_reports == 2u);
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = 4u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(initial_image_progress_reports == 3u);
```

- In the "unregistered process" block that follows (process created, no
  `kernel_process_register_initial_image`), before its
  `registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;`, add:

```c
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = ASTRA_PROGRESS_BOOT_SERVICE | ASTRA_PROGRESS_SERVICE_READY;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);
    assert(boot_commands == 0u);
    memset(registers, 0, sizeof(registers));
```

Before relying on `next->data[0]`, confirm that `next` is the calling
thread's context after a non-blocking PROGRESS. The existing qualification
cases at lines 3959-3968 assert `next->data[0]` the same way.

Run:

```bash
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "make -C sw/kernel clean && make -C sw/kernel build/test_process && sw/kernel/build/test_process"'
```

Expected: the build fails if `kernel_process_initial_image_boot_command` is
undeclared. Otherwise it fails at `assert(boot_commands == 1u)`, because
today the command is treated as progress value `0x42531...`.

- [ ] **Step 2: Route the command**

In `sw/kernel/process.h`, after the `kernel_process_initial_image_progress`
declaration:

```c
/*
 * An ASTRA_PROGRESS_BOOT_* command from the initial image (astra/syscall.h).
 * Returns the ASTRA_SYSCALL_* status the caller receives.
 */
uint32_t kernel_process_initial_image_boot_command(uint32_t command);
```

In `sw/kernel/process.c`, near the other file-scope asserts or just before
`kernel_process_on_syscall`, add:

```c
_Static_assert((KERNEL_QUALIFICATION_COMMAND_PREFIX &
                ASTRA_PROGRESS_BOOT_COMMAND_MASK) !=
                   ASTRA_PROGRESS_BOOT_COMMAND_PREFIX,
               "boot and qualification PROGRESS commands overlap");
```

In the `case ASTRA_SYSCALL_PROGRESS:` block, immediately after the
`if (qualification_handled) { ... break; }` statement and before
`if (thread->context.data[1] < current->progress)`, add:

```c
            if ((thread->context.data[1] &
                 ASTRA_PROGRESS_BOOT_COMMAND_MASK) ==
                ASTRA_PROGRESS_BOOT_COMMAND_PREFIX) {
                result = initial_image_process_id != 0u &&
                         current->id == initial_image_process_id ?
                    kernel_process_initial_image_boot_command(
                        thread->context.data[1]) :
                    ASTRA_SYSCALL_ACCESS_DENIED;
                break;
            }
```

In `sw/kernel/kernel.c`, after `kernel_process_initial_image_progress`, add:

```c
uint32_t kernel_process_initial_image_boot_command(uint32_t command)
{
    uint32_t result = kernel_boot_progress_command(command);

    if (result == ASTRA_SYSCALL_OK &&
        (command & ~ASTRA_PROGRESS_BOOT_ARGUMENT_MASK) ==
            ASTRA_PROGRESS_BOOT_SERVICES)
        console_printf("Boot services ....... %u to start\n",
                       command & ASTRA_PROGRESS_BOOT_ARGUMENT_MASK);
    return result;
}
```

Add `#include <astra/syscall.h>` to `kernel.c` if
`grep -n "astra/syscall.h" sw/kernel/kernel.c` finds nothing. If it is
already reached through `process.h`, the explicit include is still correct.

- [ ] **Step 3: Tests pass**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "make -C sw/kernel clean && make -C sw/kernel -j32 test 2>&1 | tail -3; make -C sw/kernel clean && bash ~/astra-mg/build-all.sh"'
```

Expected: kernel tests exit 0 and `BUILD-OK`.

- [ ] **Step 4: Perturb**

Change `current->id == initial_image_process_id` to `true`. Rerun Step 3's
first command. Expected: `assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED)`
fails. Revert. Rerun: PASS.

- [ ] **Step 5: ABI doc**

In `docs/ABI.md`, the syscall table:

- Row 0 `QUERY_ABI` reads `D1=0x00010036`, which is stale. Change it to
  `D1=0x00010041`.
- Row 1 becomes:

```
| 1 | `PROGRESS` | CURRENT | `D1` = monotonic progress value; or, from the initial image only, a boot command `0x4253xyyy` (`x`=1 SERVICES with N=`yyy`, `x`=2 SERVICE with result 0 ready / 1 failed / 2 skipped) that drives the boot progress record (`sw/include/astra/boot_progress.h`) |
```

- [ ] **Step 6: Commit**

```bash
git add sw/kernel/process.c sw/kernel/process.h sw/kernel/kernel.c sw/kernel/tests/test_process.c docs/ABI.md
git commit -m "feat(kernel): boot commands from the initial image drive the record

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: The ROM creates the record and counts POST

**Files:**
- Modify: `sw/boot/boot.c` (`screen_init` 115-121, `run_post` 925-990,
  `idle_forever` 1258, `kmain` 1273-1317)
- Modify: `sw/boot/Makefile` (`ID_SRCS` and `$(BOOT_ELF)` prerequisites gain
  `../include/astra/boot_progress.h`)

**Interfaces:**
- Consumes: `astra_boot_progress_advance/fail`, offsets and header (Task 1).
- Produces: at handoff, a record with header `0x42500001` and value
  `PACK(BOOTING, 0, 10, 0, 0)` = `0x01400000`. The kernel continues from it
  and marks it FAILED if the fixed count is not exactly
  `ASTRA_BOOT_PROGRESS_ROM_STEPS` (Task 2).

- [ ] **Step 1: Failing check, in QEMU**

There is no ROM host test for this path. The proof is the record that the
next ROM leaves in a file-backed text page. Create the probe once, in
`/tmp/splash/rom-record.py` on beast. It is a verification command, not a repo
file:

```bash
rtk proxy ssh beast 'cat > /tmp/splash/rom-record.py <<'"'"'EOF'"'"'
import os, struct, subprocess, sys, tempfile, time
qemu, rom = sys.argv[1], sys.argv[2]
page = tempfile.NamedTemporaryFile(delete=False); page.write(bytes(4096)); page.close()
env = dict(os.environ, ASTRA_TEXT_PLANE_PATH=page.name)
proc = subprocess.Popen([qemu, "-M", "astra68", "-m", "512M", "-bios", rom,
    "-display", "none", "-monitor", "none", "-serial", "stdio", "-no-reboot"],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
end = time.monotonic() + 60
for line in proc.stdout:
    if "Starting Axiom kernel" in line or time.monotonic() > end:
        break
time.sleep(0.5); proc.kill(); proc.wait()
data = open(page.name, "rb").read()
header, value = struct.unpack_from(">II", data, 0xFE0)
print("header=%08x value=%08x state=%u announced=%u fixed=%u count=%u done=%u" % (
    header, value, value >> 28, (value >> 27) & 1, (value >> 21) & 0x3f,
    (value >> 10) & 0x3ff, value & 0x3ff))
EOF'
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "python3 /tmp/splash/rom-record.py \"\$(tail -n1 \$ASTRA_QEMU_BUILD_LOG)\" sw/boot/build/astra_boot.bin"'
```

Expected on the Task 3 ROM: `header=00000000`. The kernel adopts nothing,
because no ROM record exists yet.

- [ ] **Step 2: Write the ROM side**

In `sw/boot/boot.c`, add `#include <astra/boot_progress.h>` after
`#include <astra/boot.h>`. Add, after `screen_puts`:

```c
/*
 * The boot progress record (astra/boot_progress.h). The ROM makes it and
 * counts POST; at handoff the kernel takes it over and the ROM never writes
 * again. One aligned 32-bit store per update, the value before the header,
 * so a reader that sees the header never sees a previous boot's value.
 */
static uint32_t rom_progress;

static void rom_progress_publish(uint32_t value)
{
    rom_progress = value;
    if (screen_enabled)
        *(volatile uint32_t *)(VEGA_POST_TEXT_BASE +
                               ASTRA_BOOT_PROGRESS_VALUE_OFFSET) = value;
}

static void rom_progress_step(void)
{
    rom_progress_publish(astra_boot_progress_advance(rom_progress));
}
```

In `screen_init`, after `screen_clear();`:

```c
    rom_progress_publish(0u);
    if (screen_enabled)
        *(volatile uint32_t *)(VEGA_POST_TEXT_BASE +
                               ASTRA_BOOT_PROGRESS_HEADER_OFFSET) =
            ASTRA_BOOT_PROGRESS_HEADER;
```

Placement: `screen_init` (line 115) calls `rom_progress_publish`, so the
helpers go between `screen_puts` (ends line 113) and `screen_init`.
`screen_enabled` is declared above both.

In `idle_forever`, as the first statement:

```c
    rom_progress_publish(astra_boot_progress_fail(rom_progress));
```

In `run_post`, add `rom_progress_step();` on the line after each of these
nine success prints:

- `uart_puts("OK\n");` after `wait_for_sdram()`
- `uart_puts("OK\n");` after `test_front_panel()`
- `uart_puts("OK\n");` after `test_access_widths(ram_base)`
- `uart_puts("OK\n");` after `test_address_bus(base, words)`
- `uart_puts("OK\n");` after `test_cache_coherence(ram_base)`
- `screen_puts("OK\n");` after `benchmark_local_access_widths()`
- `screen_puts("OK\n");` after `benchmark_access_widths(ram_base)`
- `screen_puts("OK\n");` after `benchmark_astraea(ram_base)`
- `screen_puts("OK\n");` after `test_full_range(ram_base, ram_size)`

The `OK\n` strings are not unique. Edit each one by its preceding test call,
which is unique.

In `kmain`, after the
`if (!prepare_kernel_handoff()) idle_forever(...);` statement:

```c
    rom_progress_step();
```

In `sw/boot/Makefile`, add `../include/astra/boot_progress.h` to `ID_SRCS`
(after `../include/astra/boot.h`) and to the `$(BOOT_ELF)` prerequisite
list (after `boot.c`).

- [ ] **Step 3: Build and see the record**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "bash ~/astra-mg/build-all.sh && python3 /tmp/splash/rom-record.py \"\$(tail -n1 \$ASTRA_QEMU_BUILD_LOG)\" sw/boot/build/astra_boot.bin"'
```

Expected: `BUILD-OK`, then
`header=42500001 value=01400000 state=0 announced=0 fixed=10 count=0 done=0`
(9 POST steps plus the kernel load).

If the probe kills QEMU after the kernel has started, `fixed` is between 11
and 21 and the kernel has taken over. That is correct too. The requirement
is `header=42500001` and `state=0`. With `state=2` and `fixed=10`, the
kernel found the ROM count right but something later failed. With
`state=2` and `fixed` not 10, the ROM miscounted: check the nine
`rom_progress_step()` sites.

- [ ] **Step 4: Perturb**

1. Comment out the header store in `screen_init`. Rebuild and rerun Step 3.
   Expected: `header=00000000`. Revert.
2. Delete one `rom_progress_step();`, for example the one after
   `test_front_panel()`. Rebuild and rerun Step 3. Expected: `state=2` and
   `fixed=9` or more: the kernel caught the miscount at handoff. Revert.
3. In `kmain`, change `if (!run_post())` to `if (1)`. Rebuild and rerun the
   probe with its marker changed to `HALTED: POST FAILURE`: edit
   `/tmp/splash/rom-record.py`'s `"Starting Axiom kernel"` to that string. Expected:
   `state=2`. Revert both. Rebuild, rerun Step 3: `state=0`.

- [ ] **Step 5: Commit**

```bash
git add sw/boot/boot.c sw/boot/Makefile
git commit -m "feat(rom): the ROM creates the boot progress record and counts POST

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: The supervisor announces an exact launch list; the gate

**Files:**
- Modify: `sw/userspace/supervisor/src/loader.c` (`supervisor_loader_start`,
  lines 2729-2808; new statics near `definition_scratch`, line 94)
- Modify: `sw/userspace/services/fault-probe/main.c` (`--fail-ready`)
- Modify: `emu/qemu/astra_image.py` (`configuration=` on `install` and
  `_install_built`; `--ready-fail` profile)
- Create: `emu/qemu/test-boot-progress.py`
- Modify on beast: `~/astra-mg/gates.sh`

**Interfaces:**
- Consumes: the `ASTRA_PROGRESS_BOOT_*` commands (Task 2/3) and the record
  (Tasks 1-4).
- Produces: the gate `test-boot-progress.py QEMU ROM BASE_IMAGE`, which runs
  both scenarios. On success it prints `ASTRA BOOT PROGRESS PASS` and
  `ASTRA BOOT PROGRESS FAILED-SERVICE PASS`.

- [ ] **Step 1: The failure fixture**

In `sw/userspace/services/fault-probe/main.c`:

- In the top comment, after the first paragraph, add the sentence:
  `With --fail-ready it refuses READY instead, so a gate can drive a boot
  service that fails to start.`
- Replace the body after `bootstrap == NULL` with:

```c
    if (astra_startup_argument(startup, 1u) != NULL &&
        strcmp(astra_startup_argument(startup, 1u), "--fail-ready") == 0) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_IO, NULL,
                                  0u);
        (void)astra_close(bootstrap->handle);
        return ASTRA_STATUS_IO;
    }
    if (astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK, NULL, 0u) !=
        ASTRA_SYSCALL_OK)
        return ASTRA_STATUS_PEER_DEAD;
    (void)astra_close(bootstrap->handle);
    (void)astra_log("fault probe dying");
    return FAULT_PROBE_STATUS;
```

- Add `#include <string.h>`.

In `emu/qemu/astra_image.py`:

1. After `FAULT_CRITICAL_STARTUP_MANIFEST` (line 210), add:

```python
# A boot service that refuses READY, from /config/services so the definition
# scan counts it: the boot completes, and the boot progress record says
# FAILED (test-boot-progress.py).
READY_FAIL_CONFIGURATION = dict(CONFIGURATION, **{
    "services/ready-fail/service.conf": (
        "astra-config 1\n"
        "schema 1\n"
        "name \"ready-fail\"\n"
        "executable \"/services/fault-probe\"\n"
        "runs astra\n"
        "enabled true\n"
        "delegates false\n"
        "start boot\n"
        "restart never\n"
        "argument \"/services/fault-probe\"\n"
        "argument \"--fail-ready\"\n"),
})
```

2. Add `configuration=CONFIGURATION` as the last keyword parameter of
   `install` (line 775) and `_install_built` (line 811). Pass it through in
   `install`'s call to `_install_built(...)`. In `_install_built`, change
   `for path, contents in CONFIGURATION.items():` (line 949) to
   `for path, contents in configuration.items():`.
   - Check every other `_install_built(` caller with
     `grep -n "_install_built(" emu/qemu/astra_image.py`. Each must still
     work through the default.
3. In `__main__`, after the `--fault-critical` branch:

```python
    elif len(sys.argv) == 3 and sys.argv[1] == "--ready-fail":
        install(sys.argv[2], service_names=FAULT_PROBE_SERVICES,
                manifest_text=DISPLAY_STARTUP_MANIFEST,
                configuration=READY_FAIL_CONFIGURATION)
```

   Add `--ready-fail` to the usage string's profile list.

- [ ] **Step 2: Write the gate**

`emu/qemu/test-boot-progress.py`:

```python
#!/usr/bin/env python3
"""The boot progress record: monotonic, exact, and honest about failure.

Boots with the text page backed by a file, as run-arty.sh does on the DE25,
and samples the record the splash helper reads
(sw/include/astra/boot_progress.h) every 2 ms until the kernel prints
stage 8.

Normal boot (a copy of BASE_IMAGE):
  - the 32-bit value never decreases;
  - at stage 8 it is DONE: all 21 fixed steps, announced, done == count;
  - the kernel's announced service count equals what the image says boots:
    the manifest minus storage and `trusted`, plus the enabled start=boot
    definitions under /config/services.
A service that refuses READY (the copy given `astra_image.py --ready-fail`):
  - at stage 8 the record is FAILED with every fixed step and done == count,
    and the boot still reached stage 8.

Usage: test-boot-progress.py QEMU ROM BASE_IMAGE
"""

import argparse
import importlib.util
import mmap
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import astra_image  # noqa: E402

SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)

PAGE_BYTES = 4096
HEADER_OFFSET = 0xFE0
VALUE_OFFSET = 0xFE4
HEADER = 0x42500001
BOOTING, DONE, FAILED = 0, 1, 2
SERVICES = re.compile(r"Boot services \.+ (\d+) to start")
BOOT_DEADLINE = 240.0


FIXED_STEPS = 21  # ASTRA_BOOT_PROGRESS_FIXED_STEPS (boot_progress.h)


def fields(value):
    """(state, announced, fixed, count, done) -- boot_progress.h's layout."""
    return (value >> 28, (value >> 27) & 1, (value >> 21) & 0x3F,
            (value >> 10) & 0x3FF, value & 0x3FF)


def stable_word(page, offset):
    """One word, read until two reads agree: never half of a store."""
    while True:
        first = struct.unpack_from(">I", page, offset)[0]
        if struct.unpack_from(">I", page, offset)[0] == first:
            return first


def expected_services(configuration):
    """What the supervisor must announce for an image built from these."""
    count = 0
    for line in astra_image.DISPLAY_STARTUP_MANIFEST.splitlines():
        words = line.split()
        if (words and words[0] in ("service", "application") and
                words[1] != "/services/storage" and
                "start=manual" not in words):
            count += 1
    for path, text in configuration.items():
        if (path.startswith("services/") and path.endswith("/service.conf")
                and "\nenabled true\n" in text and "\nstart boot\n" in text):
            count += 1
    return count


class Sampler(threading.Thread):
    def __init__(self, page):
        super().__init__(daemon=True)
        self.page = page
        self.values = []
        self.stop = threading.Event()

    def run(self):
        while not self.stop.is_set():
            if stable_word(self.page, HEADER_OFFSET) == HEADER:
                value = stable_word(self.page, VALUE_OFFSET)
                if not self.values or self.values[-1] != value:
                    self.values.append(value)
            time.sleep(0.002)


def boot(qemu, rom, image, expect_failed, services):
    work = tempfile.mkdtemp(prefix="astra-boot-progress-")
    page_path = os.path.join(work, "post-text.bin")
    with open(page_path, "wb") as handle:
        handle.write(bytes(PAGE_BYTES))
    os.environ["ASTRA_TEXT_PLANE_PATH"] = page_path
    machine = None
    sampler = None
    try:
        machine = terminal.Machine(qemu, rom, image, work)
        with open(page_path, "rb") as handle:
            page = mmap.mmap(handle.fileno(), PAGE_BYTES,
                             access=mmap.ACCESS_READ)
        sampler = Sampler(page)
        sampler.start()
        if not machine.wait_for_serial("stage 8", BOOT_DEADLINE):
            raise RuntimeError("no stage 8:\n" +
                               "\n".join(machine.recent_serial(60)))
        time.sleep(0.05)
        sampler.stop.set()
        sampler.join()
        header = stable_word(page, HEADER_OFFSET)
        final = stable_word(page, VALUE_OFFSET)
        values = sampler.values
        print("samples=%d first=%08x final=%08x" %
              (len(values), values[0] if values else 0, final))
        if header != HEADER:
            raise RuntimeError("no boot progress header: %08x" % header)
        for before, after in zip(values, values[1:]):
            if after < before:
                raise RuntimeError("record went backwards: %08x -> %08x" %
                                   (before, after))
        state, announced, fixed, count, done = fields(final)
        if not announced or fixed != FIXED_STEPS or done != count:
            raise RuntimeError(
                "stage 8 with announced=%d fixed=%d/%d done=%d/%d" %
                (announced, fixed, FIXED_STEPS, done, count))
        said = [int(match.group(1)) for match in
                (SERVICES.search(line) for line in machine.log) if match]
        if said != [services] or count != services:
            raise RuntimeError("announced %r (record %d), image boots %d" %
                               (said, count, services))
        if expect_failed:
            if state != FAILED:
                raise RuntimeError("a failed service left state %d" % state)
        else:
            if state != DONE:
                raise RuntimeError("a clean boot ended in state %d" % state)
            if any(fields(value)[0] == FAILED for value in values):
                raise RuntimeError("a clean boot passed through FAILED")
        # Informational: compared by hand with the pre-change baseline.
        # said_with_syslog rejoins the ring's 20-byte records.
        lines, _ = machine.said_with_syslog(0)
        print("ASTRA BOOT LAUNCH ORDER: " + " | ".join(
            match.group(1) for match in
            (re.search(r"\blaunch (/\S+)", line) for line in lines)
            if match))
    finally:
        if sampler is not None:
            sampler.stop.set()
        if machine is not None:
            machine.close()
        os.environ.pop("ASTRA_TEXT_PLANE_PATH", None)
        shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    arguments = parser.parse_args()
    scratch = tempfile.mkdtemp(prefix="astra-boot-progress-images-")
    try:
        clean = os.path.join(scratch, "clean.img")
        shutil.copyfile(arguments.image, clean)
        boot(arguments.qemu, arguments.rom, clean, False,
             expected_services(astra_image.CONFIGURATION))
        print("ASTRA BOOT PROGRESS PASS")
        failing = os.path.join(scratch, "ready-fail.img")
        shutil.copyfile(arguments.image, failing)
        subprocess.run([sys.executable,
                        os.path.join(HERE, "astra_image.py"),
                        "--ready-fail", failing], check=True,
                       stdout=subprocess.DEVNULL)
        boot(arguments.qemu, arguments.rom, failing, True,
             expected_services(astra_image.READY_FAIL_CONFIGURATION))
        print("ASTRA BOOT PROGRESS FAILED-SERVICE PASS")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
```

Before relying on these helpers, confirm three things in
`emu/qemu/test-terminal.py`:

- `Machine.wait_for_serial` appends to `machine.log` (lines 777-789);
- `said_with_syslog(after)` returns `(lines, highest)` the way `said` does
  (lines 668-697 and 810; `test-desktop-apps.py:45` uses it that way);
- `Machine.__init__`'s `qemu_environment(...)` copies `os.environ`
  (`qemu_runtime.py:9-10`).

These were true at `81fe6e03`.

- [ ] **Step 3: Run the gate and see it fail**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "bash ~/astra-mg/build-all.sh && bash ~/astra-mg/images.sh && python3 emu/qemu/test-boot-progress.py \"\$(tail -n1 \$ASTRA_QEMU_BUILD_LOG)\" sw/boot/build/astra_boot.bin \$ASTRA_IMG_DIR/base.img" 2>&1 | tail -8' | tee /tmp/splash-gate-before.txt
```

Expected: `RuntimeError: stage 8 with announced=0 fixed=21/21 done=0/0`.
Nothing is announced yet, so `finish` leaves FAILED. The line
`ASTRA BOOT LAUNCH ORDER:` is not printed, because the check raises first.
To capture the baseline order, temporarily move the `said_with_syslog`/`print` block
above the `if header != HEADER` check. Run once, save the printed order to
`/tmp/splash-order-before.txt` on the Mac, then move the block back.

- [ ] **Step 4: Resolve the list up front, announce, report**

In `sw/userspace/supervisor/src/loader.c`, after
`static AstraServiceDefinition definition_scratch[2];` (line 94), add:

```c
/*
 * The boot launch list, resolved in full before the first launch so the
 * kernel knows its length (astra/boot_progress.h): the manifest minus
 * trusted ceilings and services the user set not to start at boot, then the
 * enabled start=boot definitions under /config/services -- the same choices,
 * in the same order, the launch loops always made one at a time.
 * ponytail: one full definition per entry (~5 KiB each) for the length of
 * the boot loop; index-only entries if a boot ever starts hundreds.
 */
typedef struct SupervisorBootLaunch {
    /* The manifest line, or NULL for a /config/services definition. */
    const SupervisorManifestEntry *entry;
    /* 0 for a manifest line without a definition (an `application`). */
    uint32_t has_definition;
    AstraServiceDefinition definition;
} SupervisorBootLaunch;

static SupervisorBootLaunch *boot_plan;
static uint32_t boot_plan_count;
```

Before `uint32_t supervisor_loader_start(` add:

```c
static void boot_plan_release(void)
{
    astra_runtime_deallocate(boot_plan);
    boot_plan = NULL;
    boot_plan_count = 0u;
}

static uint32_t boot_plan_append(const SupervisorManifestEntry *entry,
                                 const AstraServiceDefinition *definition)
{
    SupervisorBootLaunch *grown = astra_runtime_reallocate(
        boot_plan, ((size_t)boot_plan_count + 1u) * sizeof(*boot_plan));

    if (grown == NULL)
        return ASTRA_STATUS_LIMIT;
    boot_plan = grown;
    grown[boot_plan_count].entry = entry;
    grown[boot_plan_count].has_definition = definition != NULL;
    if (definition != NULL)
        grown[boot_plan_count].definition = *definition;
    ++boot_plan_count;
    return ASTRA_STATUS_OK;
}

static uint32_t boot_plan_resolve(const SupervisorManifest *manifest)
{
    AstraServiceListCursor cursor = {
        0u, ASTRA_SERVICE_LIST_SOURCE_DYNAMIC, 0u
    };
    uint32_t status;

    for (uint32_t index = 1u; index < manifest->count; ++index) {
        const SupervisorManifestEntry *entry = &manifest->entries[index];
        AstraServiceDefinition *service = &definition_scratch[0];
        const AstraServiceDefinition *definition = NULL;

        if (entry->trusted != 0u)
            continue;
        if (entry->resident != 0u &&
            supervisor_service_definition_from_manifest(entry, service) ==
                ASTRA_STATUS_OK) {
            /* The user may have chosen to start this one themselves. */
            policy_apply(service);
            if (service->start_policy != ASTRA_SERVICE_START_BOOT)
                continue;
            definition = service;
        }
        status = boot_plan_append(entry, definition);
        if (status != ASTRA_STATUS_OK)
            return status;
    }
    for (;;) {
        AstraServiceListCursor next = ASTRA_SERVICE_LIST_CURSOR_INIT;
        AstraServiceDefinition *service = &definition_scratch[0];

        status = list_service(&cursor, service, &next);
        if (status == ASTRA_STATUS_NOT_FOUND)
            break;
        if (status != ASTRA_STATUS_OK) {
            (void)astra_log_failure("service definition scan", status);
            break;
        }
        if ((service->flags & ASTRA_SERVICE_ENABLED) != 0u &&
            service->start_policy == ASTRA_SERVICE_START_BOOT) {
            status = boot_plan_append(NULL, service);
            if (status != ASTRA_STATUS_OK)
                return status;
        }
        if (next.source == ASTRA_SERVICE_LIST_SOURCE_DONE)
            break;
        cursor = next;
    }
    return ASTRA_STATUS_OK;
}

/* The old manifest loop body, unchanged: one manifest service. */
static uint32_t boot_manifest_launch(const AstraStartupInfo *startup,
                                     const SupervisorManifestEntry *entry,
                                     const AstraServiceDefinition *service)
{
    char entry_path[ASTRA_VFS_PATH_MAX];
    char bundle_root[ASTRA_VFS_PATH_MAX];
    uint32_t open_us = 0u;
    uint32_t status = resolve_entry_image(entry, entry_path,
                                          sizeof(entry_path), bundle_root,
                                          sizeof(bundle_root), NULL);

    if (status == ASTRA_STATUS_OK) {
        AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
        uint64_t read_start = astra_clock_monotonic();

        status = astra_vfs_read_source_open(
            &source, supervisor_assigns(), entry_path,
            supervisor_vfs_assign_client, NULL);
        open_us = astra_elapsed_microseconds(read_start,
                                             astra_clock_monotonic());
        if (status == ASTRA_VFS_OK)
            status = launch_entry(startup, entry, service, bundle_root, NULL,
                                  source.length, open_us,
                                  astra_vfs_read_source_read_at,
                                  astra_vfs_read_source_close, &source,
                                  NULL, NULL);
        else
            status = launch_open_status(status);
    }
    if (status != ASTRA_STATUS_OK && service != NULL)
        mark_service_failed(service->name, 1u);
    if (status != ASTRA_STATUS_OK && entry->critical != 0u)
        system_fail(service != NULL ? service->name : entry->path,
                    "did not start", status);
    return status;
}

static void boot_service_report(uint32_t result)
{
    (void)astra_progress(ASTRA_PROGRESS_BOOT_SERVICE | result);
}
```

In `supervisor_loader_start`, replace everything from the line after
`proc_tree_start();` through the closing `}` of the definition-scan block
(the block that ends just before the long comment
`These receivers are services implemented by this process`) with:

```c
    status = boot_plan_resolve(manifest);
    if (status != ASTRA_STATUS_OK) {
        boot_plan_release();
        return status;
    }
    (void)astra_progress(ASTRA_PROGRESS_BOOT_SERVICES | boot_plan_count);
    for (uint32_t index = 0u; index < boot_plan_count; ++index) {
        SupervisorBootLaunch *launch = &boot_plan[index];

        if (launch->entry == NULL) {
            /* A /config/services definition: after every manifest service. */
            int running = service_process_slot(launch->definition.name) !=
                          UINT32_MAX;

            status = launch_definition(startup, &launch->definition, 0u);
            if (status != ASTRA_STATUS_OK)
                (void)astra_log_failure(launch->definition.name, status);
            boot_service_report(running ? ASTRA_PROGRESS_SERVICE_SKIPPED :
                                status == ASTRA_STATUS_OK ?
                                    ASTRA_PROGRESS_SERVICE_READY :
                                    ASTRA_PROGRESS_SERVICE_FAILED);
            continue;
        }
        status = boot_manifest_launch(
            startup, launch->entry,
            launch->has_definition != 0u ? &launch->definition : NULL);
        boot_service_report(status == ASTRA_STATUS_OK ?
                            ASTRA_PROGRESS_SERVICE_READY :
                            ASTRA_PROGRESS_SERVICE_FAILED);
        if (status != ASTRA_STATUS_OK && launch->entry->required) {
            boot_plan_release();
            return status;
        }
    }
    boot_plan_release();
```

Behavior notes. Each one must hold:

- Policy reads (`policy_apply`) now all happen before the first boot launch
  instead of interleaved with launches. They read through the storage client
  (`supervisor_vfs_client`, live since storage published `SYSTEM`,
  `loader.c:1213`). No boot service writes `/config/services` before the
  watch loop pumps the service manager, so the same files are read either
  way.
- `launch_definition` copies nothing it is given into the process table that
  outlives the call: names are `astra_string_concat`'d. So freeing the plan
  afterwards is safe.
- The local `open_us` replaces the reuse of the outer variable. The outer one
  is still used for storage only.

- [ ] **Step 5: Build, gate, and the existing gates**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "bash ~/astra-mg/build-all.sh && bash ~/astra-mg/images.sh && python3 emu/qemu/test-boot-progress.py \"\$(tail -n1 \$ASTRA_QEMU_BUILD_LOG)\" sw/boot/build/astra_boot.bin \$ASTRA_IMG_DIR/base.img" 2>&1 | tail -8'
```

Expected:

- `ASTRA BOOT PROGRESS PASS` and `ASTRA BOOT PROGRESS FAILED-SERVICE PASS`.
- The clean boot announces 14: posixd, hostfs, entropy, network, ntpd,
  events, input, clipboard, display, media, sampler, desktop, plus ramfs and
  remote-desktop. The failure boot announces 15.
- Compare the clean run's `ASTRA BOOT LAUNCH ORDER:` line with
  `/tmp/splash-order-before.txt`. The manifest part must match exactly. The
  `/config/services` tail is readdir order, so it is unchanged by this task;
  if it differs, run the baseline twice to see whether readdir order varies
  between images.

Then run the gates that read `stage 8` and the service paths this task
touched:

```bash
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "Q=\$(tail -n1 \$ASTRA_QEMU_BUILD_LOG); R=sw/boot/build/astra_boot.bin; I=\$ASTRA_IMG_DIR; bash ~/astra-mg/gates.sh 2>&1 | grep -E \"^==|PASS|FAIL|EXIT\"; for p in fault-probe fault-critical; do cp \$I/base.img \$I/\$p.img; python3 emu/qemu/astra_image.py --\$p \$I/\$p.img >/dev/null; done; python3 emu/qemu/test-service-policy.py \$Q \$R \$I/fault-probe.img | tail -1; python3 emu/qemu/test-service-policy.py \$Q \$R \$I/fault-critical.img --critical | tail -1; cp \$I/draw2.img /tmp/splash/v-mb.img; python3 emu/qemu/test-display-mailbox.py \$Q \$R /tmp/splash/v-mb.img | tail -1; cp \$I/base.img \$I/ev.img; python3 emu/qemu/test-events.py \$Q \$R --image \$I/ev.img 2>&1 | tail -1"'
```

Expected: every `EXIT=0`, `display` and `terminal` PASS, both service-policy
lines PASS, display-mailbox PASS, events PASS. These are the unchanged
`stage 8` gates (`test-display.py:21`, `test-events.py:78`,
`test-display-mailbox.py:45`, `test-terminal.py:79`).

- [ ] **Step 6: Perturb**

1. Delete the `boot_service_report(...)` call for manifest entries. Rebuild
   and rerun the gate. Expected: the clean boot raises
   `stage 8 with announced=1 fixed=21/21 done=2/14`, because only the two
   definitions reported. Revert.
2. Map FAILED to READY: change the manifest report's
   `ASTRA_PROGRESS_SERVICE_FAILED` and the definition report's to
   `..._READY`. Expected: the clean scenario passes and the failure scenario
   raises `a failed service left state 1`. Revert.
3. Announce `boot_plan_count + 1u`. Expected: the clean boot raises
   `stage 8 with ... done=14/15`. Revert.

Rebuild and rerun Step 5's gate: both PASS.

- [ ] **Step 7: Register the gate on beast**

`~/astra-mg/gates.sh` is shared with the filesystem branch. The new lines
are therefore silent when `emu/qemu/test-boot-progress.py` is absent: no
command runs, no file is written, and the summary loop's list is unchanged.
They go in the sequential tail after the terminal gate, not in the parallel
group. The parallel gates boot `$I/base.img` themselves, and copying an
image another QEMU is writing gives a dirty journal (CLAUDE.md).

`gates.sh.pre-splash` already exists from Task 0; do not overwrite it.

```bash
rtk proxy ssh beast 'python3 - <<'"'"'EOF'"'"'
import pathlib
p = pathlib.Path.home() / "astra-mg/gates.sh"
s = p.read_text()
anchor = "echo EXIT=$? >> ${ASTRA_LOG_PREFIX}terminal.log)\n"
line = ("if [ -f emu/qemu/test-boot-progress.py ]; then (python3 emu/qemu/test-boot-progress.py $Q $R $I/base.img "
        "> ${ASTRA_LOG_PREFIX}boot-progress.log 2>&1; echo EXIT=$? >> ${ASTRA_LOG_PREFIX}boot-progress.log); fi\n")
loop = "display terminal; do"
assert s.count(anchor) == 1 and s.count(loop) == 1 and "boot-progress" not in s
s = s.replace(anchor, anchor + line, 1)
s = s.replace(loop, "display terminal $([ -f emu/qemu/test-boot-progress.py ] && echo boot-progress); do", 1)
p.write_text(s)
EOF
bash -n ~/astra-mg/gates.sh && grep -c "boot-progress" ~/astra-mg/gates.sh'
```

Expected: `3`. Prove the filesystem branch sees no change: the dry run's
fake canonical tree has no `test-boot-progress.py`.

```bash
rtk proxy ssh beast '~/astra-mg/locked bash -c "~/astra-mg/splash-dryrun.sh old > ~/splash-dry-old.txt; ~/astra-mg/splash-dryrun.sh new > ~/splash-dry-new.txt"; diff ~/splash-dry-old.txt ~/splash-dry-new.txt && echo DEFAULTS-IDENTICAL'
```

Expected: `DEFAULTS-IDENTICAL`. Then run the gate through the suite:

```bash
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "Q=\$(tail -n1 \$ASTRA_QEMU_BUILD_LOG) R=sw/boot/build/astra_boot.bin bash ~/astra-mg/gates.sh 2>&1 | grep -A3 \"== boot-progress\""'
```

Expected: the `== boot-progress` block ends with
`ASTRA BOOT PROGRESS FAILED-SERVICE PASS` and `EXIT=0`.

- [ ] **Step 8: Commit**

```bash
git add sw/userspace/supervisor/src/loader.c sw/userspace/services/fault-probe/main.c \
        emu/qemu/astra_image.py emu/qemu/test-boot-progress.py
git commit -m "feat(supervisor): resolve the boot launch list up front and report each launch

The kernel's boot progress total is exact before the first service starts.
test-boot-progress.py gates a clean boot (DONE: every fixed step and every
announced service, monotonic) and a boot service that refuses READY (FAILED).

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: The helper draws the splash and the bar

**Files:**
- Modify: `fpga/arty/linux/astra_terminal_display.c`:
  - constants after line 64;
  - new functions before `self_test` (line 2024);
  - self-test additions before `puts("ASTRA_TERMINAL_DISPLAY_SELF_TEST PASS")`
    (line 2663);
  - `main` at 2713-3160.
- Modify: `fpga/arty/linux/Makefile`. `$(TERMINAL_TARGET)` (line 182) and
  `$(OUT)/test-astra-terminal-display` (line 250) gain
  `$(ASTRA_INCLUDE)/astra/boot_progress.h`.
- Modify: `emu/qemu/run-arty.sh` (lines 33-38, 203, 267);
  `emu/qemu/test-run-arty.py`.

**Interfaces:**
- Consumes: `astra/boot_progress.h` (Task 1).
- Produces:
  - Helper usage `<text-page> <mailbox> <payload> [<splash.rgb565>]`.
  - `run-arty.sh` passes `$ASTRA_ROOT/share/astra_boot_splash.rgb565` as the
    fourth argument when it is readable. `ASTRA_DISPLAY_SPLASH` overrides the
    path.

- [ ] **Step 1: Failing self-test**

Add to `self_test`, immediately before
`puts("ASTRA_TERMINAL_DISPLAY_SELF_TEST PASS");`:

```c
    /* The boot splash: which scene, how much bar, which pixels. */
    {
        static uint8_t frame[ASTRA_FRAMEBUFFER_BYTES];
        _Alignas(4) uint8_t page[TEXT_PAGE_BYTES] = {0};
        const uint32_t fixed = ASTRA_BOOT_PROGRESS_FIXED_STEPS;
        /* Half the services of 14 started: 30% + 35% of 880 = 572. */
        const uint32_t booting = astra_boot_progress_pack(
            ASTRA_BOOT_PROGRESS_STATE_BOOTING, 1u, fixed, 14u, 7u);
        const uint32_t done = astra_boot_progress_pack(
            ASTRA_BOOT_PROGRESS_STATE_DONE, 1u, fixed, 14u, 14u);
        uint32_t value = 0u;
        uint32_t last_fill = 0u;
        const uint32_t failed = astra_boot_progress_fail(booting);
        const uint32_t header = ASTRA_BOOT_PROGRESS_HEADER;
        const uint8_t *pixel;

        /* Before the guest writes anything, and through a clean boot. */
        if (boot_scene(true, false, 0u, 0u) != BOOT_SCENE_SPLASH ||
            boot_scene(true, false, header, booting) != BOOT_SCENE_SPLASH ||
            /* A helper restarted after boot shows the splash, not text. */
            boot_scene(true, false, header, done) != BOOT_SCENE_SPLASH ||
            /* Failure, a panic, no splash, or an unknown record: text. */
            boot_scene(true, false, header, failed) != BOOT_SCENE_TEXT ||
            boot_scene(true, true, header, booting) != BOOT_SCENE_TEXT ||
            boot_scene(false, false, header, booting) != BOOT_SCENE_TEXT ||
            boot_scene(true, false, header + 1u, booting) != BOOT_SCENE_TEXT)
            return EXIT_FAILURE;
        if (splash_bar_fill(0u) != 0u ||
            splash_bar_fill(booting) != 572u ||
            splash_bar_fill(done) != SPLASH_BAR_WIDTH)
            return EXIT_FAILURE;
        /* A whole boot, step by step: the bar never moves backwards, the
           fixed phase ends at 30%, and DONE is the full bar. */
        for (uint32_t step = 0u; step < fixed; ++step) {
            value = astra_boot_progress_advance(value);
            if (splash_bar_fill(value) < last_fill)
                return EXIT_FAILURE;
            last_fill = splash_bar_fill(value);
        }
        if (last_fill != SPLASH_BAR_WIDTH * 3u / 10u)
            return EXIT_FAILURE;
        value = astra_boot_progress_announce(value, 14u);
        for (uint32_t service = 0u; service < 14u; ++service) {
            value = astra_boot_progress_service(value, 0u);
            if (splash_bar_fill(value) < last_fill)
                return EXIT_FAILURE;
            last_fill = splash_bar_fill(value);
        }
        if (splash_bar_fill(astra_boot_progress_finish(value)) !=
            SPLASH_BAR_WIDTH)
            return EXIT_FAILURE;
        /* The guest stores big-endian; the helper reads one word. */
        page[ASTRA_BOOT_PROGRESS_VALUE_OFFSET + 0u] = (uint8_t)(booting >> 24);
        page[ASTRA_BOOT_PROGRESS_VALUE_OFFSET + 1u] = (uint8_t)(booting >> 16);
        page[ASTRA_BOOT_PROGRESS_VALUE_OFFSET + 2u] = (uint8_t)(booting >> 8);
        page[ASTRA_BOOT_PROGRESS_VALUE_OFFSET + 3u] = (uint8_t)booting;
        if (plane_load_be32(page, ASTRA_BOOT_PROGRESS_VALUE_OFFSET) != booting)
            return EXIT_FAILURE;
        (void)memset(frame, 0x5a, sizeof(frame));
        splash_draw_bar(frame, booting);
#define SPLASH_PIXEL(x, y) \
    (frame + (size_t)(y) * ASTRA_FRAMEBUFFER_PITCH + (size_t)(x) * 2u)
        pixel = SPLASH_PIXEL(SPLASH_BAR_X, SPLASH_BAR_Y);
        if (pixel[0] != (uint8_t)(SPLASH_BAR_FILL >> 8) ||
            pixel[1] != (uint8_t)SPLASH_BAR_FILL)
            return EXIT_FAILURE;
        pixel = SPLASH_PIXEL(SPLASH_BAR_X + 571u,
                             SPLASH_BAR_Y + SPLASH_BAR_HEIGHT - 1u);
        if (pixel[0] != (uint8_t)(SPLASH_BAR_FILL >> 8))
            return EXIT_FAILURE;
        pixel = SPLASH_PIXEL(SPLASH_BAR_X + 572u, SPLASH_BAR_Y);
        if (pixel[0] != (uint8_t)(SPLASH_BAR_TRACK >> 8) ||
            pixel[1] != (uint8_t)SPLASH_BAR_TRACK)
            return EXIT_FAILURE;
        /* Nothing outside the bar is touched. */
        if (*SPLASH_PIXEL(SPLASH_BAR_X - 1u, SPLASH_BAR_Y) != 0x5au ||
            *SPLASH_PIXEL(SPLASH_BAR_X + SPLASH_BAR_WIDTH, SPLASH_BAR_Y) !=
                0x5au ||
            *SPLASH_PIXEL(SPLASH_BAR_X, SPLASH_BAR_Y - 1u) != 0x5au ||
            *SPLASH_PIXEL(SPLASH_BAR_X, SPLASH_BAR_Y + SPLASH_BAR_HEIGHT) !=
                0x5au)
            return EXIT_FAILURE;
#undef SPLASH_PIXEL
    }
```

Add `#include <astra/boot_progress.h>` after `#include <astra/display.h>`.
Add `$(ASTRA_INCLUDE)/astra/boot_progress.h` to both Makefile prerequisite
lists named above.

Run:

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "make -C fpga/arty/linux ../../../build/arty-graphics/linux/test-astra-terminal-display && build/arty-graphics/linux/test-astra-terminal-display --self-test"'
```

Expected: the compile fails with `boot_scene` undeclared.

- [ ] **Step 2: Scene, bar and splash functions**

After the enum's closing `};` (line 64) add:

```c
/*
 * The boot splash bar, inside the empty lower panel of
 * astra_boot_splash_1920x1080_blank.png (x 360-1560, y 717-955), left of its
 * emblem. ponytail: placement and colours are a calibration knob -- tune on
 * the board, the art is the reference.
 */
enum {
    SPLASH_BAR_X = 420u,
    SPLASH_BAR_Y = 834u,
    SPLASH_BAR_WIDTH = 880u,
    SPLASH_BAR_HEIGHT = 6u,
    SPLASH_BAR_TRACK = 0x1187u,
    SPLASH_BAR_FILL = 0x4f3eu,
};
_Static_assert(SPLASH_BAR_X + SPLASH_BAR_WIDTH <= ASTRA_DISPLAY_WIDTH &&
                   SPLASH_BAR_Y + SPLASH_BAR_HEIGHT <= ASTRA_DISPLAY_HEIGHT,
               "the splash bar must fit the scanout");

enum boot_scene { BOOT_SCENE_TEXT, BOOT_SCENE_SPLASH };
```

Before `static int self_test(void)` add:

```c
/* One word of the text page as the m68k stored it: one load, never torn.
   Relaxed: nothing else is read on the strength of it (boot_progress.h). */
static uint32_t plane_load_be32(volatile const uint8_t *plane, uint32_t offset)
{
    uint32_t word = __atomic_load_n(
        (volatile const uint32_t *)(volatile const void *)(plane + offset),
        __ATOMIC_RELAXED);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    word = __builtin_bswap32(word);
#endif
    return word;
}

/*
 * Splash unless something is wrong. A zero header is a guest that has not
 * started: the helper runs before QEMU. A header it cannot read is a guest
 * it does not understand, and the text page is how such a guest explains
 * itself.
 */
static enum boot_scene boot_scene(bool have_splash, bool panicked,
                                  uint32_t header, uint32_t value)
{
    if (!have_splash || panicked)
        return BOOT_SCENE_TEXT;
    if (header == 0u)
        return BOOT_SCENE_SPLASH;
    if (header != ASTRA_BOOT_PROGRESS_HEADER)
        return BOOT_SCENE_TEXT;
    return astra_boot_progress_state(value) ==
                   ASTRA_BOOT_PROGRESS_STATE_FAILED ?
               BOOT_SCENE_TEXT : BOOT_SCENE_SPLASH;
}

/* Haiku's slices (boot_splash.cpp:83), two phases: boot_progress.h. */
static uint32_t splash_bar_fill(uint32_t value)
{
    return astra_boot_progress_fill(value, SPLASH_BAR_WIDTH);
}

/* The bar into a big-endian RGB565 frame; nothing else is touched. */
static void splash_draw_bar(uint8_t *frame, uint32_t value)
{
    uint32_t fill = splash_bar_fill(value);

    for (uint32_t y = 0u; y < SPLASH_BAR_HEIGHT; ++y) {
        uint8_t *row = frame + (size_t)(SPLASH_BAR_Y + y) *
                                   ASTRA_FRAMEBUFFER_PITCH +
                       (size_t)SPLASH_BAR_X * 2u;

        for (uint32_t x = 0u; x < SPLASH_BAR_WIDTH; ++x) {
            uint16_t color = x < fill ? SPLASH_BAR_FILL : SPLASH_BAR_TRACK;

            row[2u * x] = (uint8_t)(color >> 8);
            row[2u * x + 1u] = (uint8_t)color;
        }
    }
}

/* The release's packed splash (pack_arty_splash.py), or NULL: text as before. */
static uint8_t *splash_load(const char *path)
{
    struct stat info;
    uint8_t *frame;
    size_t loaded = 0u;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        perror("open boot splash");
        return NULL;
    }
    if (fstat(fd, &info) != 0 ||
        info.st_size != (off_t)ASTRA_FRAMEBUFFER_BYTES) {
        fprintf(stderr, "boot splash must be exactly %u bytes\n",
                ASTRA_FRAMEBUFFER_BYTES);
        (void)close(fd);
        return NULL;
    }
    frame = malloc(ASTRA_FRAMEBUFFER_BYTES);
    while (frame != NULL && loaded < ASTRA_FRAMEBUFFER_BYTES) {
        ssize_t received = read(fd, frame + loaded,
                                ASTRA_FRAMEBUFFER_BYTES - loaded);

        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0) {
            perror("read boot splash");
            free(frame);
            frame = NULL;
            break;
        }
        loaded += (size_t)received;
    }
    (void)close(fd);
    return frame;
}

/* ponytail: a full 4 MB frame per step (~40 per boot); bar rows only if the
   copy ever shows in astra-top. */
static int present_splash(const struct astra_graphics_device *device,
                          uint8_t *frame, uint32_t value,
                          uint32_t *generation, uint32_t *active_scanout)
{
    splash_draw_bar(frame, value);
    return present_rgb565_frame(device, frame, generation, active_scanout);
}
```

- [ ] **Step 3: `main` uses them**

In `main`:

1. Add the locals:

```c
    uint8_t *splash = NULL;
    bool panicked = false;
    bool splash_shown = false;
    uint32_t splash_value = 0u;
```

2. Replace `if (argc != 4) {` with `if (argc != 4 && argc != 5) {`. Change
   the usage text's last line to
   `"<shared-display-payload> [<boot-splash.rgb565>]\n",`.
3. After `payload = display_payload_map(payload_fd); if (payload == MAP_FAILED) goto done;` add:

```c
    if (argc == 5)
        splash = splash_load(argv[4]);
```

4. Replace

```c
    active_scanout = current_scanout(&device);
    if (present_text_on_both_scanouts(
            &device, current, &cursor, cursor.visible, text_states,
            &text_generation, &active_scanout) != 0)
        goto done;
```

with

```c
    active_scanout = current_scanout(&device);
    {
        uint32_t header = plane_load_be32(plane,
                                          ASTRA_BOOT_PROGRESS_HEADER_OFFSET);
        uint32_t value = header == ASTRA_BOOT_PROGRESS_HEADER ?
            plane_load_be32(plane, ASTRA_BOOT_PROGRESS_VALUE_OFFSET) : 0u;

        if (boot_scene(splash != NULL, false, header, value) ==
            BOOT_SCENE_SPLASH) {
            if (present_splash(&device, splash, value, &text_generation,
                               &active_scanout) != 0)
                goto done;
            splash_shown = true;
            splash_value = value;
        } else if (present_text_on_both_scanouts(
                       &device, current, &cursor, cursor.visible,
                       text_states, &text_generation, &active_scanout) != 0) {
            goto done;
        }
    }
```

5. In the `ASTRA_DISPLAY_PANIC_TEXT` branch, inside the success block after
   `display_owned = false;`, add `panicked = true; splash_shown = false;`.
6. Immediately after the `if (display_owned) { ... continue; }` block (before
   `if (!copy_cells(current, plane)) {`), add:

```c
        {
            uint32_t header = plane_load_be32(
                plane, ASTRA_BOOT_PROGRESS_HEADER_OFFSET);
            uint32_t value = header == ASTRA_BOOT_PROGRESS_HEADER ?
                plane_load_be32(plane, ASTRA_BOOT_PROGRESS_VALUE_OFFSET) :
                0u;

            if (boot_scene(splash != NULL, panicked, header, value) ==
                BOOT_SCENE_SPLASH) {
                if (!splash_shown || value != splash_value) {
                    if (present_splash(&device, splash, value,
                                       &text_generation,
                                       &active_scanout) != 0)
                        goto done;
                    splash_shown = true;
                    splash_value = value;
                }
                while (nanosleep(&poll_delay, NULL) != 0 && errno == EINTR &&
                       running) {
                }
                continue;
            }
            if (splash_shown) {
                /* Failure: the text page, redrawn whole on both scanouts. */
                splash_shown = false;
                (void)memset(text_states, 0, sizeof(text_states));
            }
        }
```

7. In `done:`, add `free(splash);` after `astra_graphics_device_close(&device);`.

The guest's first frame sets `display_owned` exactly as before, and the loop
`continue`s before reaching this block. That is the existing handoff.

- [ ] **Step 4: Self-test and the DE25 cross-build pass**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "make -C fpga/arty/linux ../../../build/arty-graphics/linux/test-astra-terminal-display && build/arty-graphics/linux/test-astra-terminal-display --self-test && make -C fpga/arty/linux PLATFORM=de25 CROSS_COMPILE=aarch64-linux-gnu- ../../../build/de25-graphics/linux/astra-terminal-display ../../../build/de25-graphics/linux/astra_boot_splash.rgb565 && ls -l build/de25-graphics/linux/astra_boot_splash.rgb565"'
```

Expected:

- `ASTRA_TERMINAL_DISPLAY_SELF_TEST PASS`;
- the aarch64 helper links;
- the splash file is 4147200 bytes.

- [ ] **Step 5: Perturb**

1. In `boot_scene`, delete the FAILED comparison (always return SPLASH after
   the header check). Expected: the self-test fails (`EXIT_FAILURE`, no PASS
   line). Revert.
2. In `splash_draw_bar`, store little-endian (swap the two bytes). Expected:
   the self-test fails. Revert. Rerun Step 4: PASS.

- [ ] **Step 6: `run-arty.sh` passes the splash**

After `TERMINAL_DISPLAY=...` (line 33) add:

```sh
# The DE25 release ships the boot splash; without one the helper shows the
# text page, as on Arty.
DISPLAY_SPLASH=${ASTRA_DISPLAY_SPLASH:-$ASTRA_ROOT/share/astra_boot_splash.rgb565}
[ -r "$DISPLAY_SPLASH" ] || DISPLAY_SPLASH=
```

In both `start_helper "$DISPLAY_CPU" "$TERMINAL_DISPLAY" "$TEXT_PLANE" \`
calls (lines 203 and 267), change the continuation line
`"$DISPLAY_MAILBOX" "$DISPLAY_PAYLOAD"` to:

```sh
    "$DISPLAY_MAILBOX" "$DISPLAY_PAYLOAD" ${DISPLAY_SPLASH:+"$DISPLAY_SPLASH"}
```

`${VAR:+"$VAR"}` is POSIX. It expands to no word when the variable is empty
and to one quoted word otherwise.

In `emu/qemu/test-run-arty.py`:

- Add a `splash=False` parameter to `fixture`. Before `records = []`, add:

```python
    if splash:
        (root / "share").mkdir()
        (root / "share/astra_boot_splash.rgb565").write_bytes(b"splash")
```

- In `main()`, after the existing scenarios, add:

```python
    with tempfile.TemporaryDirectory() as directory:
        root, observed = fixture(directory, 0.3, splash=True)
        result = subprocess.run([str(RUN_ARTY)],
                                env=environment(root, observed),
                                text=True, capture_output=True, check=False)
        assert result.returncode == 1, result.stderr
        arguments = (observed / "display.args").read_text().splitlines()
        assert len(arguments) == 4, arguments
        assert arguments[3] == os.path.realpath(
            root / "share/astra_boot_splash.rgb565"), arguments
```

The existing scenario still asserts exactly three arguments
(`test-run-arty.py:184-186`). That proves an Arty-shaped release is
unchanged.

Before relying on it, confirm that `main()` skips nothing that this scenario
needs: it needs `flock` and `taskset` (`run-arty.sh:130`). Mirror whatever
guard the existing scenarios use.

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast '~/astra-mg/splash-env python3 emu/qemu/test-run-arty.py'
```

Expected: it exits 0. Perturb: drop `${DISPLAY_SPLASH:+"$DISPLAY_SPLASH"}`
from the first `start_helper` call. Expected: `assert len(arguments) == 4`
fails. Revert and rerun: exit 0.

- [ ] **Step 7: Commit**

```bash
git add fpga/arty/linux/astra_terminal_display.c fpga/arty/linux/Makefile emu/qemu/run-arty.sh emu/qemu/test-run-arty.py
git commit -m "feat(de25): the display helper draws the boot splash and its progress bar

Text only on failure: FAILED, a panic, or a record it cannot read. The
guest's first frame ends it, as before.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: The DE25 release ships the splash

**Files:**
- Modify: `emu/qemu/create-de25-release.sh` (lines 16, 28-31, 46-66)
- Modify: `emu/qemu/test-create-de25-release.py`

**Interfaces:**
- Produces: release path `share/astra_boot_splash.rgb565`. This is the path
  `run-arty.sh` defaults to (Task 6).

- [ ] **Step 1: Failing test**

In `emu/qemu/test-create-de25-release.py`, add to the `required` tuple:

```python
    "build/de25-graphics/linux/astra_boot_splash.rgb565",
    "share/astra_boot_splash.rgb565=",
```

After the loop add:

```python
launcher = Path(__file__).with_name("run-arty.sh").read_text()
assert "$ASTRA_ROOT/share/astra_boot_splash.rgb565" in launcher
```

Run on the Mac (pure Python, no build): `python3 emu/qemu/test-create-de25-release.py`.
Expected: `AssertionError: build/de25-graphics/linux/astra_boot_splash.rgb565`.

- [ ] **Step 2: Ship it**

In `create-de25-release.sh`:

- After `DISPLAY=...` (line 16) add
  `SPLASH=$REPOSITORY/build/de25-graphics/linux/astra_boot_splash.rgb565`.
- In the first `make -B` invocation, add a target line after
  `../../../build/de25-graphics/linux/astra-terminal-display \`:
  `    ../../../build/de25-graphics/linux/astra_boot_splash.rgb565 \`.
- Update the comment above it: `POST, panic and the boot splash are rendered
  by this small host binary.`
- In the `create` list, after `"bin/astra-terminal-display=$DISPLAY" \` add
  `    "share/astra_boot_splash.rgb565=$SPLASH" \`.

- [ ] **Step 3: Pass**

`python3 emu/qemu/test-create-de25-release.py` on the Mac. Expected:
`DE25 release creation contract: PASS`.

Perturb: remove the `share/...` line. Expected: the assertion names
`share/astra_boot_splash.rgb565=`. Revert and rerun: PASS.

- [ ] **Step 4: Commit**

```bash
git add emu/qemu/create-de25-release.sh emu/qemu/test-create-de25-release.py
git commit -m "feat(de25): ship the boot splash in the release

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Full verify, docs, board

**Files:**
- Modify:
  - `docs/DEBUGGING.md` (new section after section 6; the kit table)
  - `docs/CURRENT_STATE.md` (board evidence)
  - `sw/boot/assets/README.md` (the 1920x1080 blank is now the DE25 splash)

- [ ] **Step 1: Full verify on beast**

```bash
rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build --exclude '*/build' ./ beast:/home/barry/Git/astra68-splash/
rtk proxy ssh beast 'nohup ~/astra-mg/splash-env bash ~/astra-mg/verify-then-publish.sh > /tmp/splash/verify.out 2>&1 &'
```

Poll `rtk proxy ssh beast 'grep -E "READY-TO-PUBLISH|VERIFY-FAILED" /tmp/splash/mg-v-s5.log; grep -A3 -E "== boot-progress" /tmp/splash/mg-v-s5.log; grep -E "HELPER=|KTEST=|UTEST=" /tmp/splash/mg-v-s5.log'`.

Expected:

- `READY-TO-PUBLISH`;
- `KTEST=0`, `UTEST=0`, `HELPER=0`;
- the `== boot-progress` block ends `EXIT=0`.

`HELPER` runs `make -C fpga/arty/linux test-host`, which includes the helper
self-test. The two launcher and release contract tests are not in the
verify script, so run them as well:

```bash
rtk proxy ssh beast '~/astra-mg/splash-env bash -c "python3 emu/qemu/test-run-arty.py && python3 emu/qemu/test-create-de25-release.py"'
```

Expected: exit 0 and `DE25 release creation contract: PASS`.

- [ ] **Step 2: Boot time did not regress**

Rerun Task 0 Step 7 into `/tmp/splash/timeboot-after.txt`. Compare each
stage with `/tmp/splash/timeboot-before.txt`. Expected: `terminal up` within
run-to-run noise. The added work is ~40 word stores and ~16 extra small VFS
reads moved earlier, not added. A stage more than 10% slower is investigated
before publishing.

- [ ] **Step 3: Publish to the DE25**

```bash
rtk proxy ssh beast 'nohup ~/astra-mg/splash-env env SDL2_ROOT=$HOME/Git/SDL2 SDL_NET_ROOT=$HOME/Git/SDL2_net SDL_IMAGE_ROOT=$HOME/Git/SDL2_image SDL_MIXER_ROOT=$HOME/Git/SDL2_mixer SDL_TTF_ROOT=$HOME/Git/SDL2_ttf emu/qemu/publish-de25-release.sh > /tmp/splash/publish.log 2>&1 &'
```

Poll `rtk proxy ssh beast 'tail -5 /tmp/splash/publish.log'` until it ends.
Then confirm:

```bash
rtk proxy ssh beast 'ssh root@192.168.1.52 "ls -l /var/lib/astra/current/share/astra_boot_splash.rgb565; systemctl is-active astra.service; strings /var/lib/astra/current/qemu/bin/qemu-system-m68k-astra | grep -c \"Astra68 storage image\""'
```

Expected: a 4147200-byte file, `active`, and a nonzero count. A zero count
is the CLAUDE.md trap: a QEMU too old for the block device.

- [ ] **Step 4: Capture the boot on the board**

On the board, record the progress record and RFB frames from a cold
`astra.service` start. `de25-rfb.py` reconnects for 10 s while the remote
desktop restarts with it (`PartOf=astra.service`).

```bash
rtk proxy ssh beast 'ssh root@192.168.1.52 "mkdir -p /data/splash-evidence && rm -f /data/splash-evidence/* && systemctl restart astra.service && (for n in \$(seq 1 300); do printf \"%s \" \$(date +%s.%N); od -A n -t x1 -j 4064 -N 8 /run/astra/post-text.bin | tr -d \"\\n\"; echo; sleep 0.1; done > /data/splash-evidence/record.txt &) && for n in \$(seq 1 25); do python3 /var/lib/astra/current/bin/de25-rfb.py /data/splash-evidence/frame-\$n.rgb --password-file /etc/astra/remote-desktop.password >/dev/null 2>&1; sleep 1; done; ls /data/splash-evidence | wc -l"'
```

Copy the evidence to beast and convert it to PNG there. `de25-rfb.py` writes
raw RGB24 at 1920x1080 (`de25-rfb.py:96-140`).

```bash
rtk proxy ssh beast 'mkdir -p /tmp/splash-evidence && scp -q root@192.168.1.52:/data/splash-evidence/* /tmp/splash-evidence/ && python3 - <<'"'"'EOF'"'"'
import glob, struct, zlib, pathlib
def png(raw, path, w=1920, h=1080):
    rows = b"".join(b"\0" + raw[y*w*3:(y+1)*w*3] for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    pathlib.Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))
for name in sorted(glob.glob("/tmp/splash-evidence/frame-*.rgb")):
    png(open(name, "rb").read(), name[:-4] + ".png")
EOF
mkdir -p /mnt/Documents/astra68/boot-splash-$(date +%Y%m%d) && cp /tmp/splash-evidence/*.png /tmp/splash-evidence/record.txt /mnt/Documents/astra68/boot-splash-$(date +%Y%m%d)/'
rtk rsync -az beast:/tmp/splash-evidence/ /tmp/splash-evidence/
```

Read the PNGs on the Mac with the Read tool. Expected:

- **Early frames:** the splash, with the bar track empty or partly filled.
  It never shows the text page.
- **Later frames:** the bar is longer, and no frame's fill is shorter than
  the one before it.
- **Handoff:** one frame is the desktop, with no text page between the last
  splash and the desktop.
- **Bar phases:** the bar reaches about 30% (264 px of 880) before the
  first service launches, then grows in equal service slices.
- **`record.txt`:** the 8 bytes are `42 50 00 01` then the value. The value
  never decreases. Its last line before the desktop is state `1` (the first
  nibble of byte 4 is `1`), announced, fixed 21, done == count. Decode a line
  with
  `python3 -c 'v=0x...;print(v>>28,(v>>27)&1,(v>>21)&63,(v>>10)&1023,v&1023)'`.
- **Optional photo:** take one with a camera if the capture misses the very
  first frames. The helper is up before the capture service accepts.
- **Tuning:** if the bar sits wrong against the art's panel, adjust
  `SPLASH_BAR_X/Y/WIDTH/HEIGHT/TRACK/FILL` in `astra_terminal_display.c`.
  Then update the self-test's pixel offsets (572 and 571 scale with the
  width) and its `572u` fill, rerun Task 6 Step 4, republish (Step 3), and
  capture again. Commit the tuned values with this task's docs.

- [ ] **Step 5: Perturb on the board: helper restart and failure text**

1. **Helper restart.** After the desktop is up, run
   `ssh root@192.168.1.52 'kill $(pidof astra-terminal-display)'`.
   `run-arty.sh` restarts the helper within 0.5 s. Capture one frame with
   `de25-rfb.py` as above. Expected: the splash with a full bar (state DONE),
   not the text page. Open any window from the desktop, or type with
   `de25-rfb.py --keys`, so the guest presents. Capture again. Expected: the
   desktop.
2. **The text path on hardware.** During a fresh boot, while the splash
   shows, write a FAILED value into the shared page:
   `printf '\040\000\000\000' | dd of=/run/astra/post-text.bin bs=1 seek=4068 conv=notrunc`
   (state FAILED, all counts zero).
   Capture at once. Expected: the text console. The kernel's next step
   overwrites the value, so the splash returns. That is expected and shows
   the scene is chosen from the live record.

Record both captures with the others on the NAS.

- [ ] **Step 6: Docs**

`docs/DEBUGGING.md`. After section 6, add:

```markdown
## 6a. Boot splash and the progress record

On the DE25 the display helper shows `share/astra_boot_splash.rgb565` with a
bar from power-on to the guest's first frame. The bar is driven by the boot
progress record (`sw/include/astra/boot_progress.h`): two big-endian words at
text-page offset `0xFE0`, written by the ROM during POST and by the kernel
after handoff. Two phases, as Haiku's boot_splash: the 21 fixed steps every
boot takes (10 ROM, 4 kernel, supervisor stages 1-7) fill the first 30% in
equal slices; the boot services fill the remaining 70% in N equal slices once
the supervisor announces N (`Boot services ....... N to start` on serial);
stage 8 is 100%. Stage 8 is DONE only if all 21 fixed steps and exactly N
service reports arrived; anything else is FAILED.

The helper shows the text page instead when the record says FAILED (a POST
failure or ROM miscount, a boot service that failed to start, a degraded or
panicked kernel), after a panic, or when the record has a version it does
not know.

**A failure after the desktop has drawn is not on screen.** Once the guest
presents its first frame the helper hands the display over, and only a panic
takes it back. A boot service that fails after that -- the `/config/services`
definitions such as `ramfs` and `remote-desktop` start after the desktop --
is visible only in the record (state FAILED) and in the system log
(`launch-failed /services/...`). Check both when a service is missing.

Read it on the board: `od -A n -t x1 -j 4064 -N 8 /run/astra/post-text.bin`
(`42 50 00 01`, then state:4 announced:1 fixed:6 0:1 count:10 done:10).
Under QEMU: `xp /2wx 0xFFF22FE0`. Gate: `emu/qemu/test-boot-progress.py`.
```

In the kit table, after the "Reading the character plane" row, add:

```markdown
| Reading the boot progress record | `xp /2wx 0xFFF22FE0` in the QEMU monitor; `od -A n -t x1 -j 4064 -N 8 /run/astra/post-text.bin` on the board |
```

`sw/boot/assets/README.md`: change the 1920x1080 blank entry to say it is
the DE25 boot splash, packed by `pack_arty_splash.py` and shipped as
`share/astra_boot_splash.rgb565`. Confirm the current wording with
`cat sw/boot/assets/README.md` first.

`docs/CURRENT_STATE.md`: add a dated paragraph with:

- the release id (`readlink /var/lib/astra/current` on the board);
- the NAS evidence path;
- the number of boot steps and services observed (from `record.txt`);
- the handoff result;
- the time-boot before/after numbers from Step 2.

- [ ] **Step 7: Commit**

```bash
git add docs/DEBUGGING.md docs/CURRENT_STATE.md sw/boot/assets/README.md
git commit -m "docs: DE25 boot splash, the progress record, and board evidence

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage** (design `boot-splash-design.md`, the task brief, and the
controller's amendments of 2026-10-08):

| Requirement | Task |
|---|---|
| Versioned record in a shared header, fixed offset in the unused tail, checked unused | 1. Static asserts against the cells and the cursor. Every writer of the page was checked: ROM `boot.c:68,81-101`; kernel `kernel.c:272-330` and `platform.c:422-447`; QEMU is plain RAM (`astra68.c:7153-7169`). No gate reads the page (`git grep` of `emu/qemu`, `tools`) |
| ROM steps recorded; single writer | 4 (ROM writes until handoff); 2 (kernel adopts and checks the ROM count) |
| Memory ordering for the ARM reader | Global Constraints; `plane_load_be32` (6); `stable_word` (5) |
| Fixed shares: 30% fixed phase in equal steps, 70% services in N equal parts, DONE = 100%, never backwards | 1 (`astra_boot_progress_fill`, `test_fill`); 6 (whole-boot sweep in the self-test) |
| Record encodes the counts, value never decreases, exact DONE/FAILED | 1 (layout, transitions, tests); 2 (kernel, ROM-count check) |
| Exact N before services launch; full list resolved up front | 5 (`boot_plan_resolve`, announce) |
| Launch order and behavior unchanged, proven by existing gates | 5 Step 5 (gates.sh, service-policy, display-mailbox, events); 8 Step 1 (full verify); order line compared with the baseline |
| PROGRESS sub-call, ABI bump | 2 Step 3, 3 |
| Helper draws splash and bar, stops at first guest frame | 6 (`display_owned` path untouched) |
| Restart shows splash; FAILED or panic shows text | 6 (`boot_scene` and self-test); 2 (panic, degraded exit and idle recovery mark FAILED) |
| Host test of rendering logic | 6 (existing `--self-test`, run by `make test-host`) |
| Ship in DE25 release; release test | 7 |
| Gates: monotonic, DONE at stage 8; READY failure gives FAILED; stage-8 gates unchanged | 5 (`test-boot-progress.py`, both scenarios; registered silently in `gates.sh`) |
| beast isolation: parameterized scripts, byte-for-byte defaults, per-tree QEMU log and images, backups | 0 (`tree-env.sh`, `splash-env`, `splash-dryrun.sh` old/new diff and isolation grep); rechecked after the gates.sh edit in 5 Step 7 |
| Post-desktop failures documented | 8 Step 6 (`DEBUGGING.md` 6a) |
| Board: publish, mid-boot capture, handoff, tune bar | 8 Steps 3-5 |
| Perturbation for every behavior change | 0.5, 1.5, 2.9, 3.4, 4.4, 5.6, 6.5, 6.6, 7.3, 8.5 |
| Haiku first | Header citation; the two-phase rule follows `boot_splash.cpp:83` |

**Placeholder scan.** No TBD or TODO. Three steps tell the implementer to
confirm a fact before relying on it, each with the exact command:

- that `next->data[0]` is the caller after PROGRESS (Task 3);
- the `test-terminal.py` helper names (Task 5);
- the beast scripts matching the edit's assertions (Task 0 Step 3).

**Type consistency.**

- `kernel_boot_progress_command(uint32_t) -> uint32_t` is the same in
  Tasks 2 and 3.
- `kernel_process_initial_image_boot_command` is the same in `process.h`,
  `kernel.c` and the test stub.
- `astra_boot_progress_pack` takes five arguments everywhere: the header,
  the kernel test, the helper self-test, and the gate's `fields()` order.
- `boot_scene`, `splash_bar_fill` (a wrapper of `astra_boot_progress_fill`),
  `splash_draw_bar` and `plane_load_be32` match their uses.
- Offsets: `ASTRA_BOOT_PROGRESS_VALUE_OFFSET` is 0xFE4 = 4068. That is
  `seek=4068` in Task 8 and `od -j 4064 -N 8` (header plus value).

**Arithmetic.**

- Fill at W = 880: 21 fixed steps give 264 (88 at step 7); with 7 of 14
  services done, 264 + 308 = 572; with 14 of 14, 880.
- The largest intermediate product is `4096*700*1023 = 2.93e9`, below 2^32.
- The ROM probe's `0x01400000` is fixed 10 in bits 26..21.
