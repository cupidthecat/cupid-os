#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <source.jpg> <output.o>" >&2
    exit 2
fi

src=$1
out=$2
tmp="${out}.baseline"

cleanup() {
    rm -f "$tmp"
}
trap cleanup EXIT HUP INT TERM

symbol_name() {
    printf '%s' "$1" | sed 's|[^A-Za-z0-9]|_|g'
}

old=$(symbol_name "$tmp")
new=$(symbol_name "$src")

rm -f "$tmp"
if command -v jpegtran >/dev/null 2>&1 &&
   jpegtran -copy none -optimize -outfile "$tmp" "$src" >/dev/null 2>&1; then
    printf 'JPEG baseline embed %s\n' "$src"
elif command -v djpeg >/dev/null 2>&1 &&
     command -v cjpeg >/dev/null 2>&1 &&
     djpeg -rgb "$src" 2>/dev/null |
        cjpeg -quality 90 -baseline -optimize > "$tmp" 2>/dev/null; then
    printf 'JPEG baseline embed %s\n' "$src"
elif command -v ffmpeg >/dev/null 2>&1 &&
     ffmpeg -y -hide_banner -loglevel error -i "$src" -frames:v 1 -q:v 2 "$tmp"; then
    printf 'JPEG baseline embed %s\n' "$src"
else
    printf 'JPEG raw embed %s (install jpegtran, djpeg+cjpeg, or ffmpeg for progressive JPEG conversion)\n' "$src"
    cp "$src" "$tmp"
fi

objcopy -I binary -O elf32-i386 -B i386 "$tmp" "$out"
objcopy \
    --redefine-sym "_binary_${old}_start=_binary_${new}_start" \
    --redefine-sym "_binary_${old}_end=_binary_${new}_end" \
    --redefine-sym "_binary_${old}_size=_binary_${new}_size" \
    "$out"
