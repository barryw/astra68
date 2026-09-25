#!/bin/sh
set -eu

project=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
derived=${1:-$(mktemp -d "${TMPDIR:-/tmp}/astra-remote-rebuild.XXXXXX")}
app="$derived/Build/Products/Release/AstraRemote.app"

build() {
    xcodebuild -quiet -project "$project/AstraRemote.xcodeproj" \
        -scheme AstraRemote -configuration Release \
        -destination 'platform=macOS,arch=arm64' \
        -derivedDataPath "$derived" CODE_SIGNING_ALLOWED=NO build
}

build
test -f "$app/Contents/Frameworks/libpixman-1.0.dylib"
test -f "$app/Contents/Frameworks/libjpeg.8.dylib"
chmod 444 "$app/Contents/Frameworks/libpixman-1.0.dylib" \
    "$app/Contents/Frameworks/libjpeg.8.dylib"
build
otool -L "$app/Contents/MacOS/AstraRemote" | grep -q '@rpath/libpixman-1.0.dylib'
otool -L "$app/Contents/MacOS/AstraRemote" | grep -q '@rpath/libjpeg.8.dylib'
echo 'ASTRA_REMOTE_REBUILD PASS'
