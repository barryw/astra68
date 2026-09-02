#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if [ -z "${MAKE:-}" ]; then
    if command -v gmake >/dev/null 2>&1; then MAKE=gmake; else MAKE=make; fi
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/astra-toolchain-id.XXXXXX")
WORK=$(CDPATH= cd -- "$WORK" && pwd)
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$WORK/sysroot/include" "$WORK/sysroot/lib"

for tool in gcc cc1 cc1plus as ld; do
    printf '%s\n' "$tool-v1" >"$WORK/fake-$tool"
done

cat >"$WORK/fake-gcc" <<'EOF'
#!/bin/sh
base=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
case "$1" in
    -print-prog-name=*) printf '%s/fake-%s\n' "$base" "${1#*=}" ;;
    -print-sysroot) printf '%s/sysroot\n' "$base" ;;
    -dumpmachine) printf '%s\n' m68k-astra ;;
    -dumpspecs) printf '%s\n' fake-specs ;;
esac
EOF
chmod +x "$WORK/fake-gcc"
test "$("$WORK/fake-gcc" -dumpmachine)" = m68k-astra
test "$("$WORK/fake-gcc" -print-sysroot)" = "$WORK/sysroot"

cat >"$WORK/Makefile" <<EOF
CROSS := $WORK/fake-
include $ROOT/mk/m68k-cross.mk

all: build/test.o
.PHONY: all identity sysroot

identity:
	@printf '%s\n' '\$(ASTRA_TOOLCHAIN_ID)'

sysroot:
	@printf '%s|%s|%s\n' '\$(ASTRA_TOOLCHAIN_TRIPLE)' \
		'\$(origin PICOLIBC)' '\$(PICOLIBC)'

build/test.o:
	@mkdir -p \$(@D)
	@date +%s >\$@
EOF

"$MAKE" -s -C "$WORK"
test "$("$MAKE" -s -C "$WORK" sysroot)" = \
    "m68k-astra|file|$WORK/sysroot"
first_identity=$("$MAKE" -s -C "$WORK" identity)
"$MAKE" -s -C "$WORK"
first=$(cat "$WORK/build/test.o")
"$MAKE" -s -C "$WORK"
test "$(cat "$WORK/build/test.o")" = "$first"

sleep 1
printf '%s\n' cc1-v2 >"$WORK/fake-cc1"
second_identity=$("$MAKE" -s -C "$WORK" identity)
test "$second_identity" != "$first_identity"
"$MAKE" -s -C "$WORK"
test "$(cat "$WORK/build/test.o")" != "$first"
second=$(cat "$WORK/build/test.o")

sleep 1
printf '%s\n' '#define ASTRA_SYSROOT_TEST 1' >"$WORK/sysroot/include/test.h"
third_identity=$("$MAKE" -s -C "$WORK" identity)
test "$third_identity" != "$second_identity"
"$MAKE" -s -C "$WORK"
test "$(cat "$WORK/build/test.o")" != "$second"
third=$(cat "$WORK/build/test.o")

sleep 1
"$MAKE" -s -C "$WORK" CFLAGS=-DASTRA_FLAGS_TEST
test "$(cat "$WORK/build/test.o")" != "$third"

echo "m68k toolchain identity: PASS"
