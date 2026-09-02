#!/bin/sh
set -eu

CROSS=${CROSS:-m68k-astra-}
CC=${CC:-${CROSS}gcc}
CXX=${CXX:-${CROSS}g++}
READELF=${READELF:-${CROSS}readelf}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/astra-gcc-driver.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

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
"$CC" -nostdlib -shared -Wl,-Bsymbolic "$WORK/shared.o" -lgcc \
    -o "$WORK/shared.so"

"$READELF" -hW "$WORK/shared.so" | grep -q 'Type:.*DYN'
"$READELF" -lW "$WORK/shared.so" | grep -q ' DYNAMIC '
"$READELF" -dW "$WORK/shared.so" >/dev/null
! "$READELF" -dW "$WORK/shared.so" | grep -q TEXTREL
"$READELF" -rW "$WORK/shared.so" | grep -q 'R_68K_RELATIVE'

cat >"$WORK/constructor.cpp" <<'EOF'
struct Constructor {
    Constructor();
};
Constructor::Constructor() {}
Constructor constructor;
EOF

"$CXX" -c "$WORK/constructor.cpp" -o "$WORK/constructor.o"
"$READELF" -SW "$WORK/constructor.o" | grep -q '\.init_array'
for startup in crtbegin.o crtend.o; do
    startup_path=$($CC -print-file-name="$startup")
    test "$startup_path" != "$startup"
    ! "$READELF" -SW "$startup_path" | grep -Eq '\.(ctors|dtors)([[:space:]]|$)'
done

echo "Astra GCC driver contract: PASS"
