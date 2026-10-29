#!/bin/sh
# Disposable DE25 RFB test helper. Run on the board after focusing Terminal.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 STORY_FILENAME" >&2
    exit 2
fi

case "$1" in
    Zork1.z3|Trinity.z4|Sherlock.z5|Zork0.z6|oneroom.z8|font3-display.z6) ;;
    *) echo "unrecognized test story: $1" >&2; exit 2 ;;
esac

python3 /var/lib/astra/current/bin/de25-rfb.py \
    --password-file /etc/astra/remote-desktop.password \
    --keys "open /apps/AstraZ.app /work/astraz-validation-20261029/$1" \
    --enter "/var/tmp/astraz-$1-open.raw"
