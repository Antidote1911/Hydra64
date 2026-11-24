#!/usr/bin/env bash
# Builds Hydra64, for Linux and for Windows.
#
#   ./build.sh               Linux: Release build (incremental) in build/
#   ./build.sh --linux       the same thing, stated explicitly
#   ./build.sh --win         64-bit Windows, built from Linux in build-win/;
#                            produces the archive build-win/Hydra64-windows.zip (to unzip, nothing to install)
#                            and, if NSIS is installed, the installer build-win/Hydra64-windows-setup.exe
#   ./build.sh --linux --win both
#   ./build.sh --clean       starts from an empty build folder (--win: downloaded Qt kept)
#   ./build.sh --test        also runs the tests after the build (Linux)
#   ./build.sh debug         Linux: Debug build in build-debug/
#   ./build.sh asan          Linux: ASan + UBSan in build-asan/ (without Stockfish)
#
# Options can be combined: ./build.sh --win --clean, ./build.sh debug --clean --test
#
# Windows: you need the system MinGW (x86_64-w64-mingw32-g++, UCRT), cargo with the target
# x86_64-pc-windows-gnu, and the machine's Qt at the version MSYS2 offers (its moc, rcc
# and lrelease tools are used for the build). Qt for Windows is downloaded once from MSYS2
# (tools/windows/fetch_sysroot.py). Only Stockfish and Shuffle are embedded; no tests.
set -euo pipefail
cd "$(dirname "$0")"

type=Release
clean=0
test=0
asan=0
linux=0
win=0
for arg in "$@"; do
    case "$arg" in
        release|Release)  type=Release ;;
        debug|Debug)      type=Debug ;;
        asan)             type=RelWithDebInfo; asan=1 ;;
        -l|--linux)       linux=1 ;;
        -w|--win|--windows) win=1 ;;
        -c|--clean)       clean=1 ;;
        -t|--test)        test=1 ;;
        -h|--help)        sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Option inconnue : $arg (voir ./build.sh --help)" >&2; exit 1 ;;
    esac
done
(( linux || win )) || linux=1 # unspecified: Linux

build_linux() {
    local dir=build
    [[ $type == Debug ]] && dir=build-debug
    local extra=()
    if (( asan )); then
        dir=build-asan
        extra=(-DHYDRA64_SANITIZE=ON -DBUNDLE_STOCKFISH=OFF)
    fi

    if (( clean )); then
        echo "==> Nettoyage de $dir/"
        rm -rf "$dir"
    fi

    local generator=()
    if [[ ! -f $dir/CMakeCache.txt ]] && command -v ninja >/dev/null; then
        generator=(-G Ninja)
    fi

    echo "==> Configuration ($type)"
    cmake -S . -B "$dir" "${generator[@]}" -DCMAKE_BUILD_TYPE="$type" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "${extra[@]}" >/dev/null

    echo "==> Compilation"
    cmake --build "$dir" --parallel "$(nproc)"

    # compile_commands.json at the root for clangd
    ln -sf "$dir/compile_commands.json" compile_commands.json

    if (( test )); then
        echo "==> Tests"
        ctest --test-dir "$dir" --output-on-failure
    fi

    echo "==> OK : $dir/hydra64"
}

build_windows() {
    local dir=build-win
    local sysroot="$PWD/$dir/sysroot"
    if (( clean )) && [[ -d $dir ]]; then
        echo "==> Nettoyage de $dir/ (Qt téléchargé conservé)"
        find "$dir" -mindepth 1 -maxdepth 1 ! -name sysroot -exec rm -rf {} +
    fi
    mkdir -p "$dir"
    local qt_version
    qt_version=$(qmake6 -query QT_VERSION)
    if [[ ! -d "$sysroot/ucrt64/lib/cmake/Qt6" ]]; then
        echo "==> Qt $qt_version pour Windows (paquets MSYS2, une seule fois)"
        python3 tools/windows/fetch_sysroot.py "$sysroot" "$qt_version"
    fi

    export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=x86_64-w64-mingw32-gcc
    echo "==> Configuration (Windows, Release)"
    cmake -S . -B "$dir" \
        -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/mingw-w64.cmake" -DHYDRA64_WIN_SYSROOT="$sysroot" \
        -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/cmake \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF -DHYDRA64_TESTS=OFF \
        -DSTOCKFISH_ARCH=x86-64-avx2 >/dev/null

    echo "==> Compilation (Windows)"
    cmake --build "$dir" --parallel "$(nproc)"

    echo "==> Archive"
    python3 tools/windows/package.py "$dir" "$sysroot/ucrt64" "$dir/Hydra64-windows.zip"
    echo "==> OK : $dir/Hydra64-windows.zip (dossier décompressé : $dir/Hydra64/)"

    # Installer (the same folder), if NSIS is there: makensis
    if command -v makensis >/dev/null; then
        echo "==> Installateur"
        local version
        version=$(sed -n 's/^project(Hydra64 VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
        printf '\xEF\xBB\xBF' > "$dir/LICENSE.txt" && cat LICENSE >> "$dir/LICENSE.txt" # UTF-8 mark: see the script
        makensis -V2 -DVERSION="$version" -DROOT="$PWD" -DSTAGE="$PWD/$dir/Hydra64" \
            -DLICENSE="$PWD/$dir/LICENSE.txt" -DOUTFILE="$PWD/$dir/Hydra64-windows-setup.exe" tools/windows/installer.nsi
        echo "==> OK : $dir/Hydra64-windows-setup.exe"
    else
        echo "    (makensis absent : pas d'installateur — paquet « nsis »)"
    fi
}

if (( linux )); then build_linux; fi
if (( win )); then
    if (( asan )) || [[ $type == Debug ]]; then
        echo "--win : seulement en Release (debug et asan concernent Linux)" >&2
        exit 1
    fi
    build_windows
fi
