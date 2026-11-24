# Hydra64 game database format (version 1)

Hydra64's format for large game databases: instant opening, sorting and searching in a fraction
of a second over millions of games, editable, with position search and opening tree. It is inspired
by Scid (si4/si5) and ChessBase, with a few improvements (see the end). Code: `src/db/` (C++
without Qt).

## Files

A database `Name` consists of:

| File | Content | Writing |
|---|---|---|
| `Name.hy64i` | index: one 64-byte row per game | append at the end, rewrite of a row in place |
| `Name.hy64n` | names: players, tournaments, sites, rounds, annotators, sources | append at the end |
| `Name.hy64g` | games: moves, variations, comments, extra headers | append at the end |
| `Name.hy64p` | position index (optional, recomputable) | rebuilt as a whole |

Everything is **little-endian**. The files are read by **memory mapping**: nothing is loaded, the
system reads the useful pages.

## Common header (64 bytes)

| Bytes | Content |
|---|---|
| 0–7 | magic: `B64DB-I`, `B64DB-N`, `B64DB-G` or `B64DB-P` followed by a zero |
| 8–11 | format version (1) |
| 12–15 | header size (64) |
| 16–23 | number of elements (index rows, names, games, positions) |
| 24–31 | `.hy64n` and `.hy64g`: end of the valid data (bytes) |

An interrupted write only leaves orphan bytes after the announced end: the database stays readable.
Order of the writes for an added game: game block, new names, index row, then only the index's
counter (commit).

## `.hy64i` — index (64 bytes per game)

Game no. *i* (from 0) is at byte `64 + 64·i`.

| Bytes | Field |
|---|---|
| 0–7 | position of the block in `.hy64g` (40 bits) and its size (24 bits: 16 MB at most) |
| 8–11, 12–15 | White, Black (player numbers) |
| 16–19, 20–23, 24–27 | tournament, site, round |
| 28–31 | annotator (`FFFFFFFF`: none) |
| 32–35 | date: year·512 + month·32 + day (0 = unknown, part by part) |
| 36–37, 38–39 | White Elo, Black Elo (0 = unknown) |
| 40–41 | ECO: 1 = A00 … 500 = E99 (0 = none or non-standard) |
| 42–43 | number of half-moves of the main line |
| 44 | result: 0 `*`, 1 `1-0`, 2 `1/2-1/2`, 3 `0-1` |
| 45 | variant: 0 = standard chess |
| 46–47 | flags: 0 deleted, 1 starting position, 2 variations, 3 comments, 4 NAG, 5 promotion; 10–15 free for the user |
| 48–51 | final material: for each side (12 bits) queens, rooks, bishops, knights (2 bits, capped at 3) and pawns (4 bits) |
| 52–55 | source (`FFFFFFFF`: none) |
| 56–63 | home pawns: number of departures (4 bits) then the first 12 home pawns (a2…h2 = 0–7, a7…h7 = 8–15) in the order in which they left their square (5 bits each) |

A deleted game stays in the files (flag) until **compaction**, which rewrites the database without
it and without the room lost by replaced games.

## `.hy64n` — names

Sequence of records `varint(length·8 + type)` then the name in UTF-8. Types: 0 player, 1
tournament, 2 site, 3 round, 4 annotator, 5 source. Within each type, names are numbered 0, 1, 2…
in file order. A name is stored only once.

On opening, Hydra64 computes on demand:
- the **alphabetical ranks** (folded text: lower case, Latin accents removed; names starting with a
  letter first) — sorting by name comes down to sorting integers;
- the search "a word of the name starts with…" on these folded texts.

## `.hy64g` — games

One block per game:

```
u8      flags: 1 = starting position (FEN) follows; 2 = compressed text (zlib)
[FEN]   varint length + text
varint  length of the moves, then the moves (see below)
varint  length of the uncompressed text, varint stored length, then the text
```

**Text** (compressed by zlib beyond 96 bytes when that is shorter): `varint n` then *n* headers
`name` (`varint` + bytes), `kind` (0: value as text following; 1: value taken from the index — Elo,
ECO, annotator, source), then the comments in the order in which the moves call them. The seven
tag roster entries (Event, Site, Date, Round, White, Black, Result) come from the index; a value the
index would not reproduce identically is kept as text. The headers thus come back exactly, in their
original order.

**Moves**: one byte = `(piece number << 4) | code`. The pieces of the side to move are numbered
**king first (0), then the others by increasing square** (a1 = 0 … h8 = 63). The order is
recomputed on the board: no piece-list history, and no need to generate the legal moves to read a
game.

| Piece | Codes |
|---|---|
| king | 0–7 directions (−1,−1) (0,−1) (+1,−1) (−1,0) (+1,0) (−1,+1) (0,+1) (+1,+1); 8 kingside castling; 9 queenside castling |
| knight | 0–7 jumps (+1,+2) (+2,+1) (+2,−1) (+1,−2) (−1,−2) (−2,−1) (−2,+1) (−1,+2) |
| pawn | 0 one square, 1 two squares, 2 capture towards the a-file, 3 towards the h-file; 4–7 / 8–11 / 12–15 promotion to queen, rook, bishop, knight when advancing / capturing towards a / towards h |
| rook | 0–7 target file (same rank); 8–15 target rank (same file) |
| bishop | 0–7 target file on the rising diagonal; 8–15 on the falling one |
| queen | like the rook; the code "target file = starting file", impossible for a rook, announces a diagonal move: the next byte is **64 + target square** |

Since the king is always piece 0 and only uses codes 0–9, the bytes `0x0A`–`0x0F` are **markers**:

| Byte | Meaning |
|---|---|
| `0A` | null move (`--`) |
| `0B` | NAG (the next byte) |
| `0C` | comment (the next one of the text) |
| `0D` | start of a variation (alternative to the last move) |
| `0E` | end of a variation |
| `0F` | extension (the next byte): `00` end of game, `01` clock (`varint` milliseconds), `02` arrows and squares (`varint` count, then from, to or 255, colour), `03` medals (`varint`), `04` training question (`varint` length, wording), `05` pre-move comment (the next one of the text, like `0C`), `06` complete ChessBase question (arguments of its `[%tqu …]`: the next one of the text; a `04` follows) |

The comment `0C` follows the move; the pre-move comment `0F 05` precedes it on display ("(or
8.Nbd2)": ChessBase distinguishes the two). Both texts are stored in the text section, in the order
of their markers. A reader from before 21 November 2026 refuses a game carrying extension `05`
("unknown extension"); databases written before stay readable.

The second byte of a diagonal queen move (64 + square) can never be taken for a marker: a
**variation is skipped without decoding it** (fast replay of the main line).

## `.hy64p` — position index

Built on conversion (or by "Rebuild the position index") for the **first D half-moves** of the main
line of each game (D = 24 by default, adjustable).

Header: number of distinct positions (16), D (32), threshold K (36), indexed games (40), start of
the lists (48), start of the state at D (56).

- **Key table**: 16 bytes per distinct position, sorted: Polyglot key (u64), then either (bit 63)
  the number of the single game, or the position of its list. Binary search.
- **Lists**: `varint` number of games; if this number ≥ K (16): White wins, draws, Black wins,
  highest Elo, last year (`varint`) — the tree is immediate; then the game numbers (gaps,
  `varint`). Below K, the statistics are computed on the index rows.
- **State at D**: 6 bytes per game, the home pawns already gone (u16) and the material (u32) at
  half-move D.

**Searching a position**: the index gives the games passing through it in the first D half-moves;
the others are replayed after D, except those discarded without reading them. Pawns do not come
back to their home square and pieces do not reappear without promotion: a game one of whose pawns
had already moved at D, or which was already missing a piece, cannot pass again through a position
where that pawn or piece is still there. Games added after the index was built are replayed
entirely.

## Measurements (Mega Database 2025, 11.15 million games; 32-core machine)

| | |
|---|---|
| Conversion from ChessBase (with the position index) | 2 min 42 s; 762 unreadable games (mostly Chess960) — since 20 November 2026 (targeted notation, Chess960, moves read in binary): 15 s for the games alone, 9 unreadable |
| Size | index 714 MB, games 1.33 GB, names 10 MB, positions 1.47 GB (D = 24) |
| Distinct positions indexed | 57.4 million |
| Opening | 13 ms |
| Searching a player ("Kasparov") | 42 ms |
| Sorting a column | 113–257 ms |
| Reading a game | 0.2 ms |
| Tree of a position | 0.6–13 ms |
| Position search: opening / middlegame / endgame | 19–170 ms / 18–430 ms / 0.3–1.4 s |
| Verification | 20,000 games drawn at random identical to the original ChessBase database |

Calibration of D (distinct positions of the first D half-moves of the Mega): 16 → 11.6 M;
24 → 57.5 M; 30 → 112 M; 40 → 211 M.

## Differences from Scid si4/si5

- **64-byte** aligned index (one cache line), 4 billion games, 16 MB per game (Scid: 16.7 million
  games in si4, 128 KB per game).
- Piece numbering **without history** (king, then increasing squares): simpler than Scid's piece
  list, always one byte per move except the diagonal queen move.
- **Append-only** names like si5, with the alphabetical ranks in addition (immediate sort like
  si4).
- **Clocks in binary** and text (comments, headers) **compressed** (Scid does not compress).
- **Position index** with precomputed statistics and state at D (Scid scans the games).
- Null moves, variations identical to the move they replace, atypical headers: restored exactly
  (identical PGN round trip).

## Single-file database (`.hy64`)

Added on 20 November 2026. Container of the four files of a database, opened as it is and editable.

| Position | Size | Content |
|---|---|---|
| 0 | 8 | magic `B64DB-1\0` |
| 8 | 4 | format version (1) |
| 12 | 4 | number of sections (4) |
| 16 + 16·k | 8 + 8 | position and size of section k (0 index, 1 names, 2 games, 3 positions) |
| 80 | 8 | start of the journal (0: none) |
| 88 | 8 | committed end of the journal |

The header is 128 bytes; each section starts on a multiple of 4096 and contains the corresponding
file as it is, with its own header. A section of zero size is absent (database without a position
index). `Database::open` maps each section into memory where it is (`MappedFile::openRange`): no
copy, no extraction.

### Writing: the journal

Sections do not grow. What is written after the file was created goes after them, into an
append-only journal made of blocks `[type: 1 byte][size: 32 bits][content]`:

| Type | Content |
|---|---|
| `I` | an index row (64 bytes): an added game |
| `N` | added names, in the format of the `.hy64n` file |
| `G` | a game block, in the format of the `.hy64g` file; its logical position continues that of the games section |

- An existing index row is modified **in place** (deletion, flags, corrected name, ECO, replaced
  game), in its section or in its `I` block of the journal.
- The end of the journal is only written in the header once the blocks are on disk: an interrupted
  write leaves at most ignored bytes at the end of the file.
- On opening, the journal is reread in order: rows and game blocks are kept in memory (like the
  games added to an ordinary database since it was opened).
- `compact()` and `buildPositionIndex()` unpack the file alongside, work on the resulting ordinary
  database and pack it again (`Database::rebuildPacked`): the journal is then tidied into the
  sections.
- `unpackDatabase` appends the journal's blocks to the end of each file and fixes their headers.
- `Database::tidy()` tidies the journal without removing anything (deleted games stay): the
  interface calls it when a database or the application is closed once the journal exceeds 4 MB.
- `Database::sync()` forces the write to disk (`fsync`); the interface calls it at the end of each
  user action, once for a series (copy of games between databases), and `close()` does too.

Measured on a database of 7,974,900 games (2.6 GB): 0.9 s to pack, 0.5 s to unpack, files
byte-for-byte identical, PGN export identical to that of the ordinary database. The tests compare,
operation by operation, a single-file database and an ordinary database (`tests/core_tests.cpp`).

Not compressed, on purpose: compression would require extracting before opening.
