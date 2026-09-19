#!/bin/sh
set -eu

CROSS=${CROSS:-m68k-astra-}
CC=${CC:-${CROSS}gcc}
CXX=${CXX:-${CROSS}g++}
READELF=${READELF:-${CROSS}readelf}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/astra-gcc-driver.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

"$CC" -dM -E - </dev/null >"$WORK/macros"
grep -q '^#define __mc68040__ 1$' "$WORK/macros"

for archive in "$($CC -print-libgcc-file-name)" \
        "$($CXX -print-file-name=libstdc++.a)" \
        "$($CXX -print-file-name=libsupc++.a)"; do
    test -f "$archive"
    grep -aq 'mcpu=68040' "$archive"
    ! strings "$archive" | grep 'mcpu=' | grep -qv 'mcpu=68040'
done

cat >"$WORK/shared.c" <<'EOF'
int astra_driver_value;
int *astra_driver_pointer = &astra_driver_value;
unsigned long long astra_driver_divide(unsigned long long left,
                                       unsigned long long right)
{
    return left / right;
}
EOF

"$CC" -fPIC -c "$WORK/shared.c" -o "$WORK/shared.o"
"$CC" -nostdlib -shared -Wl,-Bsymbolic -Wl,-soname,driver.library.1 \
    "$WORK/shared.o" -lgcc -o "$WORK/driver.library"

"$READELF" -hW "$WORK/driver.library" | grep -q 'Type:.*DYN'
"$READELF" -lW "$WORK/driver.library" | grep -q ' DYNAMIC '
"$READELF" -dW "$WORK/driver.library" >/dev/null
! "$READELF" -dW "$WORK/driver.library" | grep -q TEXTREL
"$READELF" -rW "$WORK/driver.library" | grep -q 'R_68K_RELATIVE'

cat >"$WORK/dynamic.c" <<'EOF'
#include <astra/program.h>

ASTRA_PROGRAM("driver-test", 1, 0, 0, "Astra68",
              "Copyright 2026 Astra68 contributors");

extern int *astra_driver_pointer;
void _start(void)
{
    *astra_driver_pointer = 42;
}
EOF

"$CC" -I"$SOURCE_ROOT/sw/include" -fPIC -ftls-model=initial-exec \
    -c "$WORK/dynamic.c" \
    -o "$WORK/dynamic.o"
"$CC" -nostdlib -Wl,--no-as-needed \
    -Wl,-z,now "$WORK/dynamic.o" "$WORK/driver.library" \
    -o "$WORK/dynamic.elf"
"$READELF" -hW "$WORK/dynamic.elf" | grep -q 'Type:.*EXEC'
"$READELF" -lW "$WORK/dynamic.elf" | grep -q ' DYNAMIC '
"$READELF" -lW "$WORK/dynamic.elf" | grep -q ' INTERP '
"$READELF" -p .interp "$WORK/dynamic.elf" | grep -q 'loader.library.1'
"$READELF" -dW "$WORK/dynamic.elf" | grep -q 'NEEDED.*driver.library.1'
"$READELF" -dW "$WORK/dynamic.elf" | grep -q BIND_NOW
"$READELF" -rW "$WORK/dynamic.elf" | grep -Eq 'R_68K_(GLOB_DAT|JMP_SLOT)'

cat >"$WORK/static.c" <<'EOF'
#include <astra/program.h>

ASTRA_PROGRAM("static-driver-test", 1, 0, 0, "Astra68",
              "Copyright 2026 Astra68 contributors");

void _start(void) {}
EOF
"$CC" -I"$SOURCE_ROOT/sw/include" -nostdlib -static "$WORK/static.c" \
    -o "$WORK/static.elf"
"$READELF" -hW "$WORK/static.elf" | grep -q 'Type:.*EXEC'
! "$READELF" -lW "$WORK/static.elf" | grep -q ' INTERP '
! "$READELF" -lW "$WORK/static.elf" | grep -q ' DYNAMIC '

cat >"$WORK/constructor.cpp" <<'EOF'
struct Constructor {
    Constructor();
};
Constructor::Constructor() {}
Constructor constructor;
extern "C" void _start() {}
EOF

"$CXX" -c "$WORK/constructor.cpp" -o "$WORK/constructor.o"
"$READELF" -SW "$WORK/constructor.o" | grep -q '\.init_array'
for startup in crtbegin.o crtend.o crtbeginS.o crtendS.o; do
    startup_path=$($CC -print-file-name="$startup")
    test "$startup_path" != "$startup"
    ! "$READELF" -SW "$startup_path" | grep -Eq '\.(ctors|dtors)([[:space:]]|$)'
done

dynamic_begin=$($CC -print-file-name=crtbeginS.o)
dynamic_end=$($CC -print-file-name=crtendS.o)
"$CXX" -fPIC -c "$WORK/constructor.cpp" -o "$WORK/constructor.o"
"$CXX" -nostdlib -pie -Wl,-z,now -Wl,-z,relro \
    "$dynamic_begin" "$WORK/constructor.o" "$dynamic_end" \
    "$($CC -print-file-name=libgcc_unwind_shared.a)" \
    -o "$WORK/constructor.elf"
! "$READELF" -dW "$WORK/constructor.elf" | grep -q TEXTREL

echo "Astra GCC driver contract: PASS"
