Hydra64 is a chess workbench: game databases, engine analysis, play against an engine and engine tournaments in one interface. Website and manual: <https://antidote1911.github.io/Hydra64/>.

## Which file?

| System | File | How |
|---|---|---|
| Windows 10 (1903) / 11, 64-bit | `Hydra64-{TAG}-windows-x64-setup.exe` | Installer: Start menu entry, uninstaller. |
| Windows, without installing | `Hydra64-{TAG}-windows-x64.zip` | Unzip anywhere and run `hydra64.exe`. |
| Linux x86-64, any recent distribution | `Hydra64-{TAG}-linux-x86_64.AppImage` | `chmod +x` the file, then run it. |
| Debian 13, Ubuntu 24.04 and later, Mint 22 | `hydra64_{VERSION}_amd64.deb` | `sudo apt install ./hydra64_{VERSION}_amd64.deb` |
| Linux with Flatpak | `Hydra64-{TAG}-linux-x86_64.flatpak` | `flatpak install Hydra64-{TAG}-linux-x86_64.flatpak` (the KDE runtime comes from Flathub) |
| Linux, without installing | `Hydra64-{TAG}-linux-x86_64.tar.gz` | Extract anywhere and run `Hydra64/hydra64`. |
| Arch Linux | AUR: [`hydra64-bin`](https://aur.archlinux.org/packages/hydra64-bin) (this AppImage) or [`hydra64`](https://aur.archlinux.org/packages/hydra64) (built from the sources) | `yay -S hydra64-bin` |

The AppImage, the `.deb` and the `.tar.gz` need glibc 2.39 or later. All the files need a processor with AVX2 and contain Stockfish, Shuffle and the bundled databases. `SHA256SUMS` lists the checksums of the files: `sha256sum -c SHA256SUMS`.

The Windows files are not signed: Windows SmartScreen may ask for a confirmation ("More info" → "Run anyway").

## Licences

Hydra64 is free software under the GNU General Public License, version 3. The files above embed [Stockfish](https://stockfishchess.org/) {STOCKFISH} (GPL-3.0), built unmodified from its official sources: <https://github.com/official-stockfish/Stockfish/tree/{STOCKFISH}>. The other components and their licences are listed in the [README](https://github.com/Antidote1911/Hydra64#licence).
