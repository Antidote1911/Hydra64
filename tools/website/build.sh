#!/usr/bin/env bash
# Assembles the website (GitHub Pages: https://antidote1911.github.io/Hydra64/) into a folder.
#
#   tools/website/build.sh           into build/website/
#   tools/website/build.sh <folder>  into <folder> (emptied first)
#
# The site is made of files that already live in the repository, nothing is generated:
#   website/            home page, style            → /
#   Manual.html         the user manual             → /manual/
#   docs/screenshots/   the screenshots             → /screenshots/
#   resources/hydra64.svg  the logo                 → /hydra64.svg
# To look at it: open <folder>/index.html in a browser.
set -euo pipefail
cd "$(dirname "$0")/../.."

out=${1:-build/website}
rm -rf "$out"
mkdir -p "$out/manual" "$out/screenshots"
cp website/* "$out/"
cp Manual.html "$out/manual/index.html"
cp docs/screenshots/*.png "$out/screenshots/"
cp resources/hydra64.svg "$out/hydra64.svg"
touch "$out/.nojekyll" # plain files: GitHub must not run them through Jekyll
echo "==> OK : $out/index.html"
