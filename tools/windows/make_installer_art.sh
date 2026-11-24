#!/usr/bin/env bash
# Draws the picture of the first and last pages of the Windows installer
# (resources/windows/installer-side.bmp, 164 × 314, 24-bit BMP as NSIS wants it): the logo on the
# dark background of the application. To run again if the logo changes; needs ImageMagick and
# rsvg-convert.
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
rsvg-convert -w 124 -h 124 resources/hydra64.svg -o "$tmp/logo.png"
magick -size 164x314 gradient:'#2e2e33-#141416' \
    "$tmp/logo.png" -gravity north -geometry +0+34 -composite \
    -font DejaVu-Sans-Bold -pointsize 21 -fill '#ececef' -gravity north -annotate +0+176 'Hydra' \
    -fill '#e68a35' -annotate +0+204 '64' \
    -type TrueColor BMP3:resources/windows/installer-side.bmp
echo "==> OK : resources/windows/installer-side.bmp"
