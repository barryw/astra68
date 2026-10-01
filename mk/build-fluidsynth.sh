#!/bin/sh
# Builds FluidSynth, the SoundFont synthesizer behind the Linux audio host's
# MIDI voices (docs/AUDIO_ARCHITECTURE.md), from the pinned upstream tree,
# unmodified, as a static position-independent library.
#
#   mk/build-fluidsynth.sh host OUT   for this machine (gates, self-tests)
#   mk/build-fluidsynth.sh de25 OUT   aarch64 against the DE25 Jammy sysroot
#
# Everything optional is off: no audio or MIDI drivers, no file output, no
# network, no shell, no OpenMP, no DLS, no libsndfile (so no SF3). The C++11
# OS layer means no glib. What is left is the synthesizer, the SoundFont
# loader and the MIDI file player. The daemon links it with -static-libstdc++
# because the cross compiler's libstdc++ is newer than the board's.
# FluidSynth is LGPL-2.1; the daemon is source-available in this repository,
# which is what the licence asks of a static link.
set -eu

TARGET=${1:?usage: build-fluidsynth.sh host|de25 OUT}
OUT=${2:?usage: build-fluidsynth.sh host|de25 OUT}
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
SOURCE=${FLUIDSYNTH_ROOT:-$HERE/../../fluidsynth}
REVISION=71e85b2ca6bf48641ba7e2261d8f2640ae473773 # v2.6.1
DE25_SYSROOT=${DE25_SYSROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/astra68/de25-jammy-arm64}

if [ "$(git -C "$SOURCE" rev-parse HEAD)" != "$REVISION" ]; then
    echo "FluidSynth at $SOURCE is not the pinned $REVISION" >&2
    exit 1
fi
if [ -n "$(git -C "$SOURCE" status --porcelain --untracked-files=no)" ]; then
    echo "FluidSynth source tree at $SOURCE is modified" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"
case "$TARGET" in
host)
    CROSS=
    ;;
de25)
    TOOLCHAIN=$OUT.toolchain.cmake
    cat >"$TOOLCHAIN" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_SYSROOT $DE25_SYSROOT)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
    CROSS="-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN"
    ;;
*)
    echo "unknown FluidSynth target $TARGET" >&2
    exit 1
    ;;
esac

rm -rf "$OUT"
# shellcheck disable=SC2086 # CROSS is one option or none
cmake -S "$SOURCE" -B "$OUT" -G Ninja $CROSS \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -Denable-alsa=off -Denable-aufile=off -Denable-dbus=off \
    -Denable-ipv6=off -Denable-jack=off -Denable-ladspa=off \
    -Denable-libsndfile=off -Denable-midishare=off -Denable-network=off \
    -Denable-oss=off -Denable-sdl3=off -Denable-pulseaudio=off \
    -Denable-pipewire=off -Denable-readline=off -Denable-openmp=off \
    -Denable-native-dls=off -Denable-signalsmith=off -Denable-systemd=off \
    -Denable-threads=on >"$OUT.configure.log" 2>&1 ||
    { cat "$OUT.configure.log" >&2; exit 1; }
ninja -C "$OUT" libfluidsynth >"$OUT.build.log" 2>&1 ||
    { tail -40 "$OUT.build.log" >&2; exit 1; }
test -f "$OUT/src/libfluidsynth.a"
echo "$OUT/src/libfluidsynth.a"
