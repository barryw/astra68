#!/bin/sh
# Run on Beast to archive a DE25 AstraZ RFB capture by stem.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 STEM" >&2
    exit 2
fi
case "$1" in
    ''|*[!A-Za-z0-9._-]*) echo "invalid capture stem" >&2; exit 2 ;;
esac

archive=/mnt/Documents/astra68/astraz-validation-20261029
mkdir -p "$archive"
scp -q "root@192.168.1.52:/var/tmp/astraz-$1.raw" "$archive/$1.raw"
ffmpeg -hide_banner -loglevel error -f rawvideo -pixel_format rgb24 \
    -video_size 1920x1080 -i "$archive/$1.raw" -frames:v 1 \
    "$archive/$1.png" -y
sha256sum "$archive/$1.raw" "$archive/$1.png"
