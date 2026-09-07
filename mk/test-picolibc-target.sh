#!/bin/sh
set -eu

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
CROSS=$HERE/../third_party/picolibc/scripts/cross-m68k-astra.txt
BUILD=$HERE/build-picolibc.sh

grep -Fq "c = ['m68k-astra-gcc', '-m68040', '-msoft-float', '-ffixed-a4'" \
    "$CROSS"
grep -Fq "cpu = '68040'" "$CROSS"
grep -Fq 'm68k-astra-gcc -print-sysroot' "$BUILD"
grep -Fq 'mcpu=68040' "$BUILD"
grep -Fq "grep -qv 'mcpu=68040'" "$BUILD"

echo "picolibc target contract: PASS"
