#!/usr/bin/env bash
# Installs the Linux build of Hydra64 for the current user (no root needed):
#   ~/.local/opt/hydra64/              program, built-in engines (Stockfish, Shuffle), bundled databases
#   ~/.local/bin/hydra64               launcher (and hydra64-convert)
#   ~/.local/share/applications/       menu entry, with the icon in ~/.local/share/icons
# Builds first (./build.sh). ./install.sh --uninstall removes all of it (not your Documents/hydra64).
set -euo pipefail
cd "$(dirname "$0")"
opt="$HOME/.local/opt/hydra64"
bin="$HOME/.local/bin"
apps="$HOME/.local/share/applications"
icons="$HOME/.local/share/icons/hicolor"

if [[ ${1:-} == --uninstall ]]; then
    rm -rf "$opt" "$bin/hydra64" "$bin/hydra64-convert" "$apps/hydra64.desktop" "$icons/scalable/apps/hydra64.svg" "$icons/256x256/apps/hydra64.png"
    echo "Hydra64 désinstallé (vos fichiers de Documents/hydra64 sont conservés)."
    exit 0
fi

./build.sh release >/dev/null || ./build.sh release
rm -rf "$opt"
mkdir -p "$opt" "$bin" "$apps" "$icons/scalable/apps" "$icons/256x256/apps"
for f in hydra64 hydra64-convert hydra64-tablefs stockfish shuffle; do
    [[ -f build/$f ]] && install -m 755 "build/$f" "$opt/$f"
done
cp -r build/databases "$opt/databases"
ln -sf "$opt/hydra64" "$bin/hydra64"
ln -sf "$opt/hydra64-convert" "$bin/hydra64-convert"
install -m 644 resources/hydra64.svg "$icons/scalable/apps/hydra64.svg"
command -v rsvg-convert >/dev/null && rsvg-convert -w 256 -h 256 resources/hydra64.svg -o "$icons/256x256/apps/hydra64.png"
cat > "$apps/hydra64.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Hydra64
GenericName=Chess database and analysis
GenericName[fr]=Bases, analyse et jeu d'échecs
Comment=Chess databases, engine analysis, play and engine tournaments
Comment[fr]=Bases de parties, analyse avec moteurs, jeu et tournois de moteurs
Exec=$opt/hydra64 %f
Icon=hydra64
Terminal=false
Categories=Game;BoardGame;
MimeType=application/x-chess-pgn;
StartupWMClass=hydra64
DESKTOP
command -v update-desktop-database >/dev/null && update-desktop-database "$apps" >/dev/null 2>&1 || true
echo "Hydra64 installé dans $opt — lanceur : $bin/hydra64, entrée de menu « Hydra64 »."
