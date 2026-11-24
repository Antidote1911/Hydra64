#!/usr/bin/env bash
# Removes everything the builds generate, back to a clean source tree.
#
#   ./clean.sh             build/, build-debug/, build-asan/, build-win/ (downloaded Qt kept),
#                          compile_commands.json, .cache/ (clangd) and the __pycache__ folders
#   ./clean.sh --all       the same, plus the Qt for Windows downloaded in build-win/sysroot/
#   ./clean.sh --dry-run   only lists what would be removed
#
# Does not touch what ./install.sh installed (see ./install.sh --uninstall).
set -euo pipefail
cd "$(dirname "$0")"

all=0
dry=0
for arg in "$@"; do
    case "$arg" in
        -a|--all)     all=1 ;;
        -n|--dry-run) dry=1 ;;
        -h|--help)    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Option inconnue : $arg (voir ./clean.sh --help)" >&2; exit 1 ;;
    esac
done

targets=()
for path in build build-debug build-asan compile_commands.json .cache; do
    [[ -e $path || -L $path ]] && targets+=("$path")
done

if [[ -d build-win ]]; then
    if (( all )) || [[ ! -d build-win/sysroot ]]; then
        targets+=(build-win)
    else
        while IFS= read -r -d '' path; do
            targets+=("$path")
        done < <(find build-win -mindepth 1 -maxdepth 1 ! -name sysroot -print0)
    fi
fi

while IFS= read -r -d '' path; do
    targets+=("$path")
done < <(find . -path ./build -prune -o -path ./build-debug -prune -o -path ./build-asan -prune \
              -o -path ./build-win -prune -o -type d -name __pycache__ -print0 -prune)

if (( ${#targets[@]} == 0 )); then
    echo "Rien à nettoyer."
    exit 0
fi

for path in "${targets[@]}"; do
    if (( dry )); then
        echo "À supprimer : $path"
    else
        echo "==> Suppression de $path"
        rm -rf -- "$path"
    fi
done

if (( ! dry )); then
    if (( ! all )) && [[ -d build-win/sysroot ]]; then
        echo "==> OK (Qt pour Windows conservé dans build-win/sysroot/ ; --all pour le supprimer aussi)"
    else
        echo "==> OK"
    fi
fi
