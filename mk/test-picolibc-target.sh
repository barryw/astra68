#!/bin/sh
set -eu

HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
CROSS=$HERE/../third_party/picolibc/scripts/cross-m68k-astra.txt
BUILD=$HERE/build-picolibc.sh
READ_TP=$HERE/../third_party/picolibc/libc/machine/m68k/read_tp.S
UNISTD=$HERE/../third_party/picolibc/libc/include/sys/unistd.h
SIGNAL=$HERE/../third_party/picolibc/libc/include/signal.h

grep -Fq "c = ['m68k-astra-gcc', '-m68040', '-msoft-float', '-ffixed-a4', '-fPIC'" \
    "$CROSS"
! grep -Fq -- '-fvisibility=hidden' "$CROSS"
grep -Fq "cpu = '68040'" "$CROSS"
grep -Fq 'm68k-astra-gcc -print-sysroot' "$BUILD"
grep -Fq -- '-Db_staticpic=true' "$BUILD"
grep -Fq -- '-Dtls-model=initial-exec' "$BUILD"
grep -Fq -- '-Dio-c99-formats=true' "$BUILD"
grep -Fq -- '-Dio-long-long=true' "$BUILD"
grep -Fq -- '-Dio-pos-args=true' "$BUILD"
grep -Fq -- '-Dio-long-double=true' "$BUILD"
grep -Fq -- '-Dio-percent-b=true' "$BUILD"
grep -Fq -- '-Dprintf-percent-n=true' "$BUILD"
grep -Fq -- '-Dio-wchar=true' "$BUILD"
grep -Fq -- '-Dstdio-locking=true' "$BUILD"
grep -Fq 'mcpu=68040' "$BUILD"
grep -Fq "grep -qv 'mcpu=68040'" "$BUILD"
grep -Fq 'verify_default_symbol __xpg_strerror_r' "$BUILD"
grep -Fq 'verify_default_symbol nl_langinfo' "$BUILD"
grep -Fq 'verify_default_symbol nl_langinfo_l' "$BUILD"
grep -Fq 'verify_default_symbol ferror_unlocked' "$BUILD"
grep -Fq 'verify_default_symbol fgetwc_unlocked' "$BUILD"
grep -Fq 'verify_default_symbol fputws_unlocked' "$BUILD"
grep -Fq 'verify_default_symbol fputwc_unlocked' "$BUILD"
grep -Fq 'verify_default_symbol getwchar_unlocked' "$BUILD"
grep -Fq 'verify_default_symbol putwchar_unlocked' "$BUILD"
grep -Fq '.type	__m68k_read_tp, @function' "$READ_TP"
! grep -Fq '__m6k_read_tp' "$READ_TP"
grep -Fq '.note.GNU-stack' "$READ_TP"
! grep -Fq '#define _POSIX_VERSION 202405L' "$UNISTD"
grep -Fq 'int setgroups(size_t ngroups, const gid_t *grouplist)' "$UNISTD"
grep -Fq '#ifndef __astra__' "$SIGNAL"
grep -Fq '#define sigaddset(what, sig) __sigaddset(what, sig)' "$SIGNAL"
grep -Fq '#define sigisemptyset(s) __sigisemptyset(s)' "$SIGNAL"

echo "picolibc target contract: PASS"
