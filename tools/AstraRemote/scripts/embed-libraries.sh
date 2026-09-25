#!/bin/sh
set -eu

app="${TARGET_BUILD_DIR:?}/${WRAPPER_NAME:?}"
executable="$app/Contents/MacOS/${EXECUTABLE_NAME:?}"
if [ -f "$executable.debug.dylib" ]; then
    executable="$executable.debug.dylib"
fi
frameworks="$app/Contents/Frameworks"
mkdir -p "$frameworks"

for name in pixman-1.0 jpeg.8; do
    case "$name" in
        pixman-1.0) package=pixman-1 ;;
        jpeg.8) package=libjpeg ;;
    esac
    library="$(pkg-config --variable=libdir "$package")/lib$name.dylib"
    install_name="$(otool -D "$library" | sed -n '2s/^[[:space:]]*//p')"
    if [ ! -f "$library" ] || [ -z "$install_name" ]; then
        echo "Missing $name library; install pixman and jpeg-turbo" >&2
        exit 1
    fi
    cp -fL "$library" "$frameworks/lib$name.dylib"
    install_name_tool -change "$install_name" "@rpath/lib$name.dylib" "$executable"
done
