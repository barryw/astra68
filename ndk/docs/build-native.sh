#!/bin/sh
set -eu

target=${1:-all}
case "$target" in
    all|html|pdf|check) ;;
    *)
        echo "usage: $0 [all|html|pdf|check]" >&2
        exit 2
        ;;
esac

mkdir -p build/docs
mkdir -p "${XDG_CACHE_HOME:-/tmp/.cache}/fontconfig"
rm -rf build/docs/doxygen
case "$target" in
    html)
        rm -rf build/docs/html
        ;;
    pdf)
        rm -rf build/docs/sphinx build/docs/astra68-ndk.pdf
        ;;
    all|check)
        rm -rf build/docs/html build/docs/sphinx \
            build/docs/astra68-ndk.pdf
        ;;
esac
doxygen docs/Doxyfile

build_html()
{
    sphinx-build -E -a -W --keep-going -n \
        -b html docs/source build/docs/html
}

build_pdf()
{
    sphinx-build -M latexpdf docs/source build/docs/sphinx \
        -E -a -W --keep-going -n
    cp build/docs/sphinx/latex/astra68-ndk.pdf \
        build/docs/astra68-ndk.pdf
}

case "$target" in
    html)
        build_html
        ;;
    pdf)
        build_pdf
        ;;
    all|check)
        build_html
        build_pdf
        ;;
esac

if [ "$target" = html ]; then
    echo "HTML: build/docs/html/index.html"
elif [ "$target" = pdf ]; then
    echo "PDF: build/docs/astra68-ndk.pdf"
else
    echo "HTML: build/docs/html/index.html"
    echo "PDF:  build/docs/astra68-ndk.pdf"
fi
