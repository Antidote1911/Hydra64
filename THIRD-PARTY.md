# Third-party components

Hydra64 itself is free software under the GNU General Public License, version 3: see
[`LICENSE`](LICENSE). This file lists what Hydra64 embeds, links to or ships that comes from
someone else, with the author and the licence of each. The full texts of the licences are in the
[`licenses/`](licenses) folder, shipped with every package next to this file.

| Licence | Text |
|---|---|
| GNU General Public License 3.0 | [`LICENSE`](LICENSE) |
| GNU General Public License 2.0 | [`licenses/GPL-2.0.txt`](licenses/GPL-2.0.txt) |
| GNU Affero General Public License 3.0 | [`licenses/AGPL-3.0.txt`](licenses/AGPL-3.0.txt) |
| GNU Lesser General Public License 3.0 | [`licenses/LGPL-3.0.txt`](licenses/LGPL-3.0.txt) |
| GNU Lesser General Public License 2.1 | [`licenses/LGPL-2.1.txt`](licenses/LGPL-2.1.txt) |
| MIT | [`licenses/MIT.txt`](licenses/MIT.txt), with the copyright lines given below |
| ISC | [`licenses/ISC.txt`](licenses/ISC.txt), with the copyright lines given below |
| Creative Commons Attribution 4.0 | [`licenses/CC-BY-4.0.txt`](licenses/CC-BY-4.0.txt) |
| Creative Commons CC0 1.0 | [`licenses/CC0-1.0.txt`](licenses/CC0-1.0.txt) |

## Programs shipped next to Hydra64

| Component | Author | Licence |
|---|---|---|
| [Stockfish](https://stockfishchess.org/), the built-in engine, with its evaluation networks | The Stockfish developers | GPL-3.0-or-later |

Stockfish is built unmodified from its official sources, at the tag named in the release notes of
each version: <https://github.com/official-stockfish/Stockfish>. The `hydra64` package of the AUR
uses the system's Stockfish instead.

## Libraries

| Component | Copyright | Licence |
|---|---|---|
| [Qt 6](https://www.qt.io/) (Core, Gui, Widgets, Svg, Network, PrintSupport) | The Qt Company Ltd. and other contributors | LGPL-3.0-only |
| [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System) 5.1.1, with the fix of `cmake/patch-qtads.cmake` | Uwe Kindler | LGPL-2.1-or-later |
| [Fathom](https://github.com/jdart1/Fathom) (Syzygy tablebase probing), `third_party/fathom` | © 2013-2018 Ronald de Man, © 2015 basil00, © 2016-2025 Jon Dart | MIT |
| [cbvault](https://github.com/itshak/cbvault) 0.1.7, with `cbvault-format` and `cbvault-chess` (reading of ChessBase databases); modified copies in `src/chessbase/bridge/vendor`, the changes are listed in each `HYDRA64.md` | The cbvault authors (itshak) | MIT |
| [gigachess](https://github.com/itshak/gigachess-rs) 0.1.10 | © 2026 GigaChess Contributors | MIT |
| [rayon](https://github.com/rayon-rs/rayon) 1.12.0, rayon-core 1.13.0 | © 2010 The Rust Project Developers | MIT OR Apache-2.0, used under MIT |
| [crossbeam](https://github.com/crossbeam-rs/crossbeam) (deque 0.8.8, epoch 0.9.21, utils 0.8.23) | © 2019 The Crossbeam Project Developers | MIT OR Apache-2.0, used under MIT |
| [either](https://github.com/rayon-rs/either) 1.19.0 | © 2015 the either authors | MIT OR Apache-2.0, used under MIT |
| [memmap2](https://github.com/RazrFalcon/memmap2-rs) 0.9.12 | © 2015 Dan Burkert, © 2020 Yevhenii Reizner | MIT OR Apache-2.0, used under MIT |
| [libc](https://github.com/rust-lang/libc) 0.2.191 | © The Rust Project Developers | MIT OR Apache-2.0, used under MIT |
| [arrayvec](https://github.com/bluss/arrayvec) 0.7.8 | © Ulrik Sverdrup "bluss" 2015-2023 | MIT OR Apache-2.0, used under MIT |
| The Rust standard library, linked into Shuffle and into the ChessBase bridge | The Rust Project contributors | MIT OR Apache-2.0, used under MIT |
| zlib | © 1995-2024 Jean-loup Gailly and Mark Adler | zlib licence |

The versions of the Rust crates are those pinned by `src/chessbase/bridge/Cargo.lock`. Qt and the
docking library are used under the LGPL: Qt is linked dynamically and can be replaced by another
build of the same version; the sources of both are at the addresses above, and the sources of
Hydra64 itself (GPL-3.0) allow the program to be built again against a modified copy.

## Data and tables compiled into the program

| Component | Origin | Licence |
|---|---|---|
| Tables of the ChessBase opening book format (`.ctg`), `src/chess/ctg_tables.inc` | [ctgreader](https://github.com/sshivaji/ctgreader), © 2016 Shivkumar Shivaji, itself based on Stephan Vermeire's CTG code for the Brutus engine | MIT |
| Code tables of the CPS packing of Aquarium / Chess Assistant trees, `src/chess/aquarium_codes.inc` | Extracted from `addonce1.dll` of Aquarium, © ChessOK | Not under a free licence: kept for the sole purpose of reading the files of that program (interoperability) |
| Random numbers of the Polyglot key, `src/chess/polyglot_random.inc` | Public format of the Polyglot opening books (Fabien Letouzey) | Public |
| Icons offered for the databases, `src/ui/baseicons.inc` | [Lucide](https://lucide.dev): © 2022 Lucide Contributors; portions © 2013-2022 Cole Bemis, from Feather | ISC (Lucide), MIT (the Feather portions) |
| ECO codes and opening names, `resources/eco-opening` | [lichess-org/chess-openings](https://github.com/lichess-org/chess-openings) | CC0-1.0 |

`tools/chessassistant/td0_table.bin` (a substitution table extracted from `ca70db.dll` of Chess
Assistant, © ChessOK) is only used by the decoding tools of the repository; it is not in the
packages.

## Piece sets and boards

They are separate files (`resources/sets`), read by the program and embedded in the HTML pages it
exports; the first line of the `CREDITS.txt` of each one is shown in Preferences and in the footer
of those pages. All the sets below except Hydra64's come from Lichess
([lichess-org/lila](https://github.com/lichess-org/lila), `COPYING.md`).

| Set | Author | Licence |
|---|---|---|
| Pieces Hydra64 (default), board Hydra64, board Graphite | antidote1911 | CC0-1.0 |
| Pieces Celtic, Fantasy, Spatial | Maurizio Monge | MIT |
| Pieces Firi | James Faure | CC-BY-4.0 |
| Pieces Papercut | Nikolay Anzarov | CC-BY-4.0 |
| Pieces Merida | Armando Hernandez Marroquin | GPL-2.0-or-later |
| Pieces Pixel | therealqtpi | AGPL-3.0-or-later |
| The 24 other boards (Blue, Brown, Canvas, Green, Leather, Maple, Marble, Metal, Wood…) | The lila authors and pirouetti | AGPL-3.0-or-later |

## Bundled databases

The notice of each folder of `databases/` gives the detail.

| Database | Author or source | Terms |
|---|---|---|
| `training/` (3,565 exercises) | [Lichess puzzle database](https://database.lichess.org/) | CC0-1.0 |
| `tournament-openings/8moves_v3`, `2moves_v2`, `Drawkiller_balanced_big` | [Stockfish test books](https://github.com/official-stockfish/books) | CC0-1.0 |
| `tournament-openings/UHO_2022_*` | Stefan Pohl, [sp-cc.de](https://www.sp-cc.de/) | Distributed free of charge for engine testing; no licence stated |
| `tournament-openings/Nunn_10`, `Nunn2_25` | Positions chosen by John Nunn, from the databases shipped with Fritz (ChessBase) | No licence stated |
| `engine-tests/Tactics_Marathon_210` | Positions of the *Encyclopedia of Chess Middlegames* (Chess Informant), from the Marathon database shipped with Fritz (ChessBase) | No licence stated |
| `games/Masters_1000` | Moves and headers of 1,000 publicly played games, selected from the Mega Database (ChessBase) | The moves of a game are not protected; no comment or annotation is taken |

## In the Windows packages

The installer and the archive contain, as DLL files, Qt 6 (above) and the libraries it needs, taken
from the [MSYS2](https://www.msys2.org/) packages of the same names, where their sources are:

| Library | Licence |
|---|---|
| GCC runtime (`libgcc_s_seh-1`, `libstdc++-6`) | GPL-3.0-or-later with the GCC Runtime Library Exception |
| `libwinpthread-1` (mingw-w64) | MIT and BSD-3-Clause |
| ICU (`libicudt`, `libicuin`, `libicuuc`) | Unicode-3.0 |
| FreeType | FreeType Licence (FTL) |
| HarfBuzz | MIT (Old Style) |
| Graphite2 | LGPL-2.1-or-later |
| GLib | LGPL-2.1-or-later |
| libiconv, libintl (gettext runtime) | LGPL-2.1-or-later |
| PCRE2 | BSD-3-Clause |
| libpng | libpng licence |
| libjpeg-turbo | IJG and BSD-3-Clause |
| zlib | zlib licence |
| Zstandard | BSD-3-Clause |
| Brotli | MIT |
| bzip2 | bzip2 licence |
| double-conversion | BSD-3-Clause |
| MD4C | MIT |
| libb2 | CC0-1.0 |

## In the AppImage, the `.deb` and the `.tar.gz`

They contain Qt 6 (above) and the system libraries it needs, gathered by
[linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) from Ubuntu 24.04, where their sources and
licences are. The Flatpak uses the KDE runtime; the `hydra64` package of the AUR uses the system's
libraries.
