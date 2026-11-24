# Decoding of Chess Assistant / ChessOK Aquarium databases — progress log

Study databases: `/mnt/medias/save/Logiciels/Chess/corr2012/` ("Chess Data Pro format 1.3", 595,572
correspondence games) and `/mnt/medias/save/Logiciels/Chess/hugebase/` (7,974,906 games, 6 of them
deleted by the user: nos. 1, 2, 3, 15, 17, 19). Ground truth: complete PGN export made by Aquarium,
`~/Documents/export corr database/595572.pgn` (games in the order of the database).
Scripts: `tools/chessassistant/` (see its `README.md`). Reference software: Aquarium 2024 under
Wine (`~/Games/aquarium/drive_c/Aquarium2024`, library `ca70db.dll`).

Legend: ✅ validated against the export · 🟡 hypothesis supported by data · ❓ unknown.

## Summary

| Element | Validation |
|---|---|
| Main lines | ✅ 595,569 games out of 595,572 identical to the export |
| Variations | ✅ 15,449 annotated games out of 15,449: same move tree |
| Comments (text) | ✅ 25,533 identical, 96 differences (spaces, encoding — to be looked at) |
| Headers (players, tournament, site, date, round, Elo, ECO, result, classes) | ✅ 198,524 sampled games out of 198,524; only difference: rounds > 63 |
| Complete reading → PGN | ✅ `tools/chessassistant/cabase.py` |
| HugeBase (without ground truth) | ✅ 7,974,900 non-deleted games replayed by the C++ reader, all moves legal |
| Hydra64's C++ reader (`src/db/chessassistant.*`) | ✅ corr2012 → PGN in 8.5 s: 595,565 trees identical to the export out of 595,569 (4 differences = empty variations); HugeBase → Hydra64 database: 7,974,900 games in 153 s, 0 unreadable |

Since Chess960 was added (20 November 2026), the 595,572 games of corr2012 convert; the 3 Chess960
games only differ from the export by "O-O-O" where Aquarium writes "Rd8".

## In Hydra64

`hy64db::ChessAssistantSource` (`src/db/chessassistant.h`) is a conversion source like ChessBase:
File → Convert… or `build/hydra64-convert <base>.cdp <dest>.hy64i|.hy64|.pgn` (any file of the
database will do). Deleted games (`.dxx`) are skipped. Annotation symbols converted to NAGs when
one exists, otherwise to a comment. Chess960 handled, including castlings written as a single king
or rook move.

## Files of a database

| File | Role | State |
|---|---|---|
| `.cdp` | description: number of games, list of fields | ✅ the essentials, 🟡 u32 at the end of a field |
| `.d00` | headers: 38 bytes per game | ✅ |
| `.a01` / `.d01` | u32 index per game + variable fields (`User Folders`, `~19`, `~22`) | 🟡 structure, ❓ meaning |
| `.a02` / `.d02` | u32 index per game + moves, variations, annotations | ✅ |
| `.axx` / `.dxx` | bit arrays, 1 bit per game, **most significant bit first**: `.dxx` = deleted ✅ (HugeBase); `.axx` = 1 for each existing game, padding bits at 0 ✅ |
| `.emb` | volume of embedded files: dictionaries Players, Events, Places, Sources | ✅ |
| `.st0` | volume of the saved subsets (`Subset_*` of the DLL): folder `Classes6`, files like `~-2` = one bit per game | 🟡 |
| `.bo0` | options of the database, as text | ✅ readable as it is |
| `.hsh` / `.elm` | position tree (KPAL0005) | ✅ already read by Hydra64 (`src/chess/aquarium.*`) |
| `.ti0` / `.td0` | games sorted by their moves, and prefix tree of these games ("Data Pro Tree format 1.1") | ✅ (`tools/chessassistant/td0.py`) |
| `.uh0` / `.ue0` | KPAL0005 container (like `.hsh`/`.elm`): positions reached by several move orders → locations in `.td0` | ✅ structure, checked on examples |

## Format

### `.cdp`
- Header text; at `0x68` number of records (u32); then the description of the database (u32 length
  + text: empty in corr2012, "Games on 1.01.2023" in HugeBase); then the number of fields (u32) and
  a zero u32.
- Fields next: name (u32 length + text), u32, name of the dictionary or of the type (u32 length +
  text), then u32s including the size in bytes. The exact split of the last u32s (order number,
  fixed/variable kind, volume) remains 🟡.

The names `~N` are numbered "meanings" (table of `ca70db.dll`): 0 Other, 1 White, 2 Black, 3 Name,
4 Birthday, 5 ELO, 6 Sex, 7 Chess Title, 8 FIDE ID, 9 White Elo, 10 Black Elo, 11 Site, 12 City,
13 Country, 14 Result, 15 Date, 16 Event, 17 Round, 18 ECO, 19 Remark, 20 Annotator, 21 Source,
22 Moves. A field named in clear (`Classes`, `GameID`, `User Folders`) is an "Other" field specific
to the application. Field types: dictionary reference, variable-length string, integer (1, 2 or 4
bytes, signed or not: `2u`, `4`…), date (`F` = complete date; also year alone, year and month),
game notation, result, round.

### `.d00` — 38 bytes per game, little-endian

| Bytes | `.cdp` field | Content |
|---|---|---|
| 0–2 | `~1` Players | White: player number |
| 3–5 | `~2` Players | Black |
| 6–7, 8–9 | `~9`, `~10` | White Elo, Black Elo (0 = absent) |
| 10 | `~14` | result: 1 draw, 2 `1-0`, 3 `0-1`, any other value `*` ✅ (this is what `ca70db.dll` does, and what Aquarium displays for 5, 6 and 11) |
| 11–13 | `~15` | date: year × 416 + month × 32 + day (0 = unknown, part by part) |
| 14–16 | `~16` Events | tournament |
| 17–19 | `~11` Places | site |
| 20–23 | `Classes` | bit mask: bit n = class n of Aquarium's `Data/classes6.ini` (0 My own games, 1 Historic game, 2 Model game, 3 Annotated games from books, 4 Computer games, 5 Opening theory/plans, 6 Middlegames, 7 Endgames, 8 Strategy, 9 Tactics, 10 Tests, 11 Blunders, 12 Kingside attack, 13 Queenside attack, 14 Counter attack, 15 Typical pawn structures, 16 Piece Play, 17 Positional Sacrifice, 18 Material imbalance, 19 Endgame transition, 20 Opposite colored bishops, 21 Space, 22 Defense, 23 Prophylaxis, 24 Drawing methods, 25 My Analysis, 26 Computer Analysis, 27 Tablebase Analysis, 28 Psychology, 29 Miscellaneous) 🟡 |
| 24–27 | `GameID` | small integer ❓ (213 values in HugeBase, 57 the most frequent) |
| 28–29 | `~17` | round × 512 + game number within the round; the DLL reads it as **signed** 16 bits (`RoundField_GetRoundN` = value / 512), hence rounds > 63 negative in Aquarium |
| 30–31 | `~18` | ECO: letter × 100 + number (A00 = 0 … E99 = 499), `FFFF` = absent |
| 32–34 | `~20` Players | annotator ✅ (meaning 20) |
| 35–37 | `~21` Sources | source ✅ (meaning 21) |

`FFFFFF` = no value.

### `.a0N` / `.d0N`
- `.a0N`: one u32 per game = position of its record in `.d0N`.
- Variable record (`.d01`, and `.d01` of the dictionaries): first a length (varint) per field, then
  the texts one after the other. Database: 2 fields — `User Folders` (list
  `1589355567=ZE5Y611,1589355567=ZE5Y571,…`) and `~19` = **remark** (short text: `001`, `XII`,
  `IX-X`, `I(XII)`). The field `~22` is the game itself (`.d02`). Players: name then title (`c`,
  `cim`, `cgm`, `clm`, `csm`, `clg`).
- `.d02`: length as a varint (7 bits per byte, least significant first), then the game.

### `.emb` — "Data Pro Volume format 1.1"
- At `0x89`: number of files (u32), number of directory blocks (u32).
- Entries of 0x100 bytes (at multiples of 0x100): u32 0, u32 2, u32 size, u32 position of the
  **extent table**, 12 bytes, name. Extent table: pairs (u32 position, u32 length) until the size
  is covered. Files are fragmented.
- Content: for `Players`, `Events`, `Places`, `Sources`: `.cdp`, `.d00` (players: 10 bytes = Elo
  u16, FIDE identifier u32, birth on 3 bytes like the date, sex u8), `.a01`/`.d01` (texts), `.iii`
  (u32: numbers in alphabetical order), `.axx`, `.dxx`.
- Texts in UTF-8. The original accented letters (Latin-1) were reread as cp1251 when the database
  was made ("é" stored as "й"); they are restored by UTF-8 → cp1251 → cp1252.

### Game (`.d02`)

One byte per move. Each side has 16 numbered pieces: pawns 0–7, knights 8–9, bishops 10–11, rooks
12–13, queen 14, king 15, plus 2 extra slots (16, 17) for promoted pieces. The number follows the
piece for the whole game (a pawn keeps its own when changing file). At the start: pawns by file
a→h, pieces by increasing file.

| Bytes | Meaning |
|---|---|
| `00–1F` | pawn n × 4 + k: k = 0 advance, 1 capture towards the a-file, 2 capture towards the h-file, 3 two-square advance |
| `20–2F` | knights (2 × 8): (−1,−2) (−2,−1) (−2,+1) (−1,+2) (+1,+2) (+2,+1) (+2,−1) (+1,−2), White's frame of reference |
| `30–49` | bishops (2 × 13) |
| `4A–65` | rooks (2 × 14) |
| `66–80` | queen (27) |
| `81–88` | king: (+1,+1) (−1,−1) (−1,+1) (+1,−1) (−1,0) (+1,0) (0,−1) (0,+1) |
| `89–AB`, `AC–CE` | extra piece 0 and 1 (35 codes each: 27 queen codes then 8 knight codes) |
| `D4–E5 d` | long move of piece no. (byte − `D4`), for the 18 slots: `d` = Δrank × 16 + Δfile, modulo 8. Used for promoted pieces that stayed in a pawn slot, and for moves outside the table (`E3 04`: king c1→g1 of a Chess960 castling) |
| `CF` / `D0` | queenside castling / kingside castling |
| `D1 p t` | promotion: `p` = byte of the pawn move, `t` = `40 + 20×kind + number` (kind 0 N, 1 B, 2 R, 3 Q; number = slot the piece takes: a free slot of its kind, otherwise 16 or 17, otherwise the pawn's own number) |
| `D2` | prefix: the next pawn move is an en-passant capture |
| `D3` | null move (`Z0`) |
| `E6` | starting position (see below), at the head of the game |
| `E7 … E8` | variation, placed **before** the move it replaces |
| `E9 g` | empty annotation of kind g |
| `EA g c` | one-character annotation |
| `EB n g text` | annotation, n (u8) = length of the text + 1 |
| `EC n g text` | annotation, n as u32 |

Promoted piece that stayed in a pawn slot (4th promotion and beyond): it keeps the pawn's 4 codes,
read modulo 8 (k = 0: (0,±1), 3: (0,±2), 1: (−1,±1), 2: (+1,±1), "±" according to the side), and
uses `D4–DB` for all its other moves.

Long-range pieces: move **modulo 8** (toroidal board), (Δfile, Δrank):
- diagonal A (7 codes): (k,k), k = 1…7;
- diagonal B (6 codes): (7,1) (6,2) (5,3) (3,5) (2,6) (1,7) — (4,4) already belongs to A;
- rook (14 codes): (7,0)…(1,0) then (0,7)…(0,1).
Bishop = A then B; rook; queen = A, B, then rook.

Annotation kinds — the kind byte is the index into a table of 32 names of `Aquarium.exe` (those it
writes in the PGN in the form `[%t Name]`; "b" = before the move):

| no. | Name | Content observed |
|---|---|---|
| 0, 1 | `Ctrl`, `bCtr` | binary (`FE 00 00 10 00`… sometimes followed by a text: "Current position@", "Autoplay@@shredder3") |
| 2, 3 | `Shrt`, `bShr` | symbol or short word (symbol font: `21` !, `3F` ?, `9D` +−, `B0` −+, `B1` ±, `B5` ∓, `B2` ⩲, `B3` ⩱, `9B` ∞, `99` □, `93` ⊕, `82` →, `83` ↑, `84` ⇄, `87` ⨀, `85` △; `A9`, `B9` ❓) |
| 4, 5 | `Long`, `bLon` | text comment, after / before the move |
| 6 | `Val` | one byte (`P`, `A`, `2`, `05`…) ❓ |
| 7 | `Temp` | text: `1`, `3`, or a negative number (`-2044391642`) ❓ |
| 8, 9 | `Dgrm`, `bDgr` | diagram: `DA` (after), `DB` or empty (before) |
| 10, 11 | `Time`, `bTim` | clocks: 6 or 9 bytes = two or three 3-digit numbers in **base 200**, each digit offset by `21`: White's clock, Black's, time of the move, in **tenths of a second** (`22 C1 21` = 72000 = 2 h). One-byte form (`4`) ❓ |
| 12–19 | `Tab`, `Tabs`, `Mrkr`, `bMrk`, `Tutr`, `bTut`, `1Anl`, `b1An` | not met |
| 20 | `Summ` | `00 00 00 01 00` then a Pascal string: a move (`Ng1e2`, `e6`) |
| 21 | `Fldr` | folder: `1597241095=ZE5Y485` (same syntax as `User Folders`) |
| 22–28 | `3Anl`, `b2An`, `Medi`, `b4An`, `Obst`, `Numb`, `Refr` | not met |
| 29 | `CVal` | engine evaluation: i16 centipawns **from White's point of view** (±(30000 − n) = mate in n), depth (255 = unknown), flags, then sometimes the engine's name (`CAP`) |
| 30 | `Clas` | one byte (0, 1, 2, `1E`…) ❓ |
| 31 | `NTut` | not met |

Aquarium also writes `[%eval …]`, `[%clk …]`, `[%clko …]` on export. The C++ reader keeps kinds 2
to 5 and writes 29 in the form `[%eval 0.38]`; the others are ignored.

Starting position `E6` (77 bytes): 36 × (square, type) — 18 white slots then 18 black; square =
rank × 16 + file, type 1 P, 2 N, 3 B, 4 R, 5 Q, 6 K, `08 00` = empty — then u16 half-move (even =
White to move, move number = half-move / 2 + 1), castling mask (1 K, 2 Q, 4 k, 8 q), one byte
(en-passant square? 🟡).

### `.ti0` and `.td0` — sorted games and move tree

- `.ti0`: 4 bytes per rank — u24 game number, u8 its result. The games are sorted there by their
  moves (main line).
- `.td0`: header of 0x80 bytes ("Data Pro Tree format. Version 1.1"), u32 = maximum depth
  (half-moves + 1), then a stream of **scrambled** bytes:
  `clear byte = INVERSE[byte] − position in the file (mod 256)`, where `INVERSE` is the inverse of
  a 256-byte permutation of `ca70db.dll` (RVA `0xE06B0`, copied into
  `tools/chessassistant/td0_table.bin`).
- Clear stream: `E7 <u32 end of the branch> <u32 number of games>` … `E8` delimits a branch; `ED` =
  a game ends here; any other byte is a move, with the same codes as `.d02` (`D1`, `D2`, `D4–DB`
  included). The two u32s after `E7` are not scrambled.
- The games read in tree order are those of `.ti0`, in the same order.
- Checked: Comment database 15,280 / 15,280 games identical to their main line; corr2012
  595,248 / 595,248 (the 324 games with a special starting position are in the tree without being
  compared).

### `.uh0` and `.ue0` — transpositions

Same container as the `.hsh`/`.elm` tree (byte-trie index, blocks, same CPS position key), with a
zero field mask. One record = rest of the key, then a list of u32s: positions in `.td0` (byte
following the move leading to the position), the last one marked by bit 31. Only positions reached
by **several move orders** appear there (1.e4 e5 2.Nf3 is not there; 1.d4 Nf6 2.c4 e6 3.Nf3 has 12
entries, all checked).

## Edge cases

- Games 1746 and 538413 (Chess960): queenside castling is written as a rook move (a8–d8 over the
  king); game 538412: `E3 04`, long king move (c1→g1), for kingside castling. Chess Assistant does
  not really handle Chess960; the castling mask is `0F` there whereas Aquarium exports "-".
- Aquarium disambiguates more than necessary (`Rdxd5` when the other rook is pinned).

## Examples of unknown fields (to be looked at in Aquarium)

HugeBase, game numbers from 1:

| Field | Value | Games |
|---|---|---|
| result | 0 (140 games) | 9064 Hawes J – Lasker Emanuel, 1903; 16593; 16603; 18813 |
| result | 5 (26 games) | 146000 Abroskin – Golubev, 1964; 146001; 146004; 146005 |
| result | 6 (18 games) | 146009 Chelpanov – Bogdanov, 1964; 146013; 146016; 146028 |
| result | 11 (31 games) | 146007 Bogdanov – Pikunov, 1964; 146010; 146015; 146017 |
| `Classes` | `00000200` = bit 17, Positional Sacrifice | 35951 Ortueta – Sanz Aguado, 1934; 364628 |
| `Classes` | `000a0000` = bits 9 and 11, Tactics + Blunders (468 games) | 405347 Meinsohn – Lecuyer, 1983; 487395 |
| `Classes` | `00080000` (241) | 2234468; 2345132 |
| `Classes` | `01000000` (185) | 6769171 Firouzja – Artemiev, 2020; 7041711 |
| `Classes` | `000e0000` (148) | 2412485; 2778398 |
| `Classes` | `00000004` = bit 26, Computer Analysis (49) | 2749409 Cabrera – Shipov, 2003; 2884715 |
| `Classes` | `80000000`, `04000000`, `00000020`, `00004000` | 386246; 2795071; 2030744; 1287076 |
| `GameID` | 57 (5,470 games) | 1 to 22, 4491 Lasker – Von Bardeleben, 1890 |
| `GameID` | 14 | 2749409 |
| `~19` (text of `.d01`) | `I(XII)` | 4491, 4492, 4493 |
| `~19` | `XII` | 2616471 Belle – Wustefeld, 2003 |
| `User Folders` | `1589355567=ZE5Y611,…` | 2616471, 2616679, 2616727 |
| annotator | Kodinets, Benko, Vasiukov | 19807, 142624, 336049 |
| source | "www.tiviakov.demon.nl" | 2202485, 2202884 |

corr2012: `~19` = `001` for game 24159 (7 ch RSFSR 7879), `002`… afterwards.

Results 5, 6, 11: checked on screen on 20 November 2026 (the user's screenshots) — Aquarium
displays "*" for games 146000, 146009 and 146007, with the same moves, the same ECO, the same
tournament and the same date as the decoding. They are therefore meaningless values (all in the
same 1964 tournament), read as "unknown result". Aquarium numbers the games without counting the
deleted ones: it displays nos. 145994, 146003 and 146001 (6 less).

## Ideas to test / still to do

- [ ] The 96 comments that differ from the export (spaces? characters outside cp1251?).
- [x] `.d01`: `~19` = remark (the DLL's table of meanings).
- [ ] Syntax of `User Folders` (`1589355567=ZE5Y611,…`).
- [ ] Write the remark (`~19`) and the titles in the Hydra64 conversion? (remark not converted today)
- [x] Bits of `Classes`: classes of `classes6.ini` (consistent: Ortueta – Sanz 1934 = "Positional Sacrifice", analyses = "Computer Analysis"). To be confirmed on screen.
- [ ] `GameID`: integer specific to the application (57 for 5,470 old games); no known use.
- [x] `.axx`: mere padding (most significant bits first).
- [x] `Players.d00`: Elo, FIDE identifier, birth, sex (meanings 5, 8, 4, 6) — values not checked on screen.
- [ ] `.cdp`: exact role of the u32s after each field.
- [ ] Final byte of the `E6` block (en passant): make up a game with Aquarium.
- [x] Annotation kinds: table of the 32 names found in `Aquarium.exe`.
- [ ] Exact content of kinds 0, 6, 7, 10, 20, 30; symbols `A9`, `B9` (see table).
- [x] `.ti0` / `.td0` decoded and checked.
- [x] `.uh0` / `.ue0` decoded.
- [x] Clocks (`Time`): tenths of a second, base 200; converted by the C++ reader (clock of the side that has just moved).
- [ ] Old databases (`.cbf` of Chess Assistant ≤ 2): another format, out of scope for now.
- [x] Port to C++ in Hydra64 (conversion source, like ChessBase).
- [x] Chess960 in the C++ reader (3 games of corr2012).
- [ ] Direct reading in the Databases page (without conversion), if useful.
- [x] Disassemble `ca70db.dll`: table of meanings, field types, result, round (see log 7).
- [x] Marker `E3`: long king move (codes `D4–E5` = long moves of the 18 slots, seen in the DLL's move execution).
- [ ] `ca70db.dll`, continued: final byte of `E6`, exact split of the `.cdp` fields.
- [x] Results 5, 6, 11: "*" on screen in Aquarium.

## Log

- 2026-11-20 (1) — Inventory of the 17 files. `.cdp` and `.d00` split up (38 bytes). `.a0N` index
  understood, varint length of `.d02` validated. Pawns and knights identified on the openings;
  toroidal principle of the bishops deduced from 1.e4 e5 2.Nf3 Nc6.
- 2026-11-20 (2) — Bishop/rook/queen/king tables learned by legality (python-chess): 0 illegal
  moves on the sample. Pawns tracked by number and not by file. `D2` en passant, `D1` promotion.
- 2026-11-20 (3) — Complete PGN export provided by the user. Order of the promotion kinds corrected
  (N, B, R, Q), extra slots of 35 codes, `D3` null move, `E6` starting position (18 slots per
  side), `E9`. 595,569 / 595,572 main lines identical.
- 2026-11-20 (4) — Variations and annotations validated. Headers: date, round, ECO, result. `.emb`:
  directory and extent tables; names validated; UTF-8 encoding of text reread as cp1251. Complete
  reader `cabase.py`.
- 2026-11-20 (5) — HugeBase (7,974,906 games). `.dxx`: most significant bits first, the 6 deleted
  games found. `.cdp`: description of the database before the number of fields. Promotions beyond
  two extra pieces: the piece keeps the pawn's number (`A0+n`), pawn codes modulo 8 and long moves
  `D4–DB` (Δ modulo 8). Lengths of `.d01` as varints, `User Folders` and `~19` identified. The
  whole database replays legally.
- 2026-11-20 (6) — C++ reader `src/db/chessassistant.*` plugged into the conversion; validated
  against the export (corr2012) and on HugeBase. Examples of unknown fields noted.
- 2026-11-20 (7) — `ca70db.dll` (x64, 303 exported functions with their names, radare2). Table of
  field meanings (`~19` = remark, `~20` annotator, `~21` source; players: `~3` name, `~4` birth,
  `~5` Elo, `~6` sex, `~7` title, `~8` FIDE identifier). Field types and subtypes. Result: the DLL
  only writes 1, 2, 3, the rest gives "*". Round: signed integer, round = value / 512, game = value
  mod 512. Functions useful for what follows: `Base_BuildTree`, `Base_BuildUTree`, `UTree_*`,
  `Volume_*`, `Record_GetPGNBuffer`, `NW_ReadNotationPGN`.
- 2026-11-20 (8) — Comment (15,280 games) and Elite2008 (5,456) databases, heavily annotated:
  everything reads, 14 annotation kinds met. Table of the 32 kinds found in `Aquarium.exe` (Delphi
  RTTI, near `TmarkersType`). `CVal` = evaluation from White's point of view. The C++ reader no
  longer pours the binary kinds into the comments.
- 2026-11-20 (9) — `.ti0`/`.td0`: decompilation (r2ghidra) of the tree reader of `ca70db.dll`;
  scrambling by substitution table and position; tree checked game by game on two databases.
  `.ue0`: list of the locations of a position in `.td0`, for transpositions only.
- 2026-11-20 (10) — Execution of a move in the DLL (`fcn.180096380`): codes < `CF` by table (one
  per side), `CF`/`D0` castlings, `D1` promotion, `D2` en passant, `D3` null move, `D4–E5` long
  moves of the 18 slots, `E6` starting position (76 bytes, side to move = bit 0 of byte 72), `ED`
  end. The decoder now accepts all long moves.
- 2026-11-20 (11) — Aquarium screenshots on HugeBase: results 5, 6, 11 displayed "*"; moves, ECO,
  tournament and date identical to the decoding for the three games.
- 2026-11-20 (12) — `Classes`: bits = classes of `Data/classes6.ini`. `Time`: base 200, tenths of a
  second (Kasparov – Bareev: 2 h at the start). `.st0`: saved subsets. Aquarium's database
  templates in `Data/Config/Templates/cdp_unicode.cdp.xml` (fields `User Folders`, `Classes`,
  `GameID`, `TimeControl`).
- 2026-11-20 (13) — Chess960 in Hydra64's core: the C++ reader converts all the games of
  corr2012 (595,572), Chess960 castlings included.
- 2026-11-22 (14) — Aquarium 2024 installed in the virtual machine; PGN export of corr2012 by
  Aquarium (Save to database → PGN Base, template `pgn_unicode.pgn.xml`) compared game by game with
  Hydra64's conversion: no move differs over 595,572 games (apart from three Chess960 games that
  Aquarium itself exports with unreadable castlings and no castling rights). 588,048 games without
  any difference. Fixed: engine evaluation written "-0,47" when the system is in French (`%.2f`);
  non-existent day (31 November) returned as "??" like Aquarium; round read as a signed integer
  ("-57", ".2"); figurines and the "with the idea" sign of the annotation font in the middle of a
  text ("[…¥b7]" → "[△♗b7]": bytes A2 ♔, A3 ♕, A4 ♘, A5 ♗, A6 ♖, 85 △ — the 85 of the list of
  unknown symbols). The rest of the differences is due to Aquarium's way of writing, not to the
  decoding: it writes the symbols as comments (`{!}`, `{±}`) where Hydra64 sets NAGs, the
  evaluation as `[%eval -47,127,10]`, and markers `[%t Long]` / `[%t Shrt]` (kind of the
  annotation); it rereads as Windows-1251 the Western names ("Jдger", "Boљtjan") that Hydra64
  returns right ("Jäger", "Boštjan"). It also writes `Classes`, `TimeControl "?"`, `ECO "?"`,
  `WhiteElo "-"`; Hydra64 writes in addition the titles, the annotator and the source.
- 2026-11-22 (15) — PGN export of HugeBase by Aquarium: the file stops at 4.3 GB (5,439,535 games
  out of 7,974,900) and is no longer in the order of the database beyond the first few dozen games
  — Aquarium evidently cannot write a PGN of more than 4 GB. Comparison by matching the headers
  (White, Black, date, result, tournament): 5,315,643 games matched, 5,269,713 without any
  difference. 123,892 of Aquarium's games have no identical header in the conversion (accented
  names it rereads as Windows-1251, damaged areas of the file). The 289 move differences and those
  of ECO and round come from the matching (several games with the same header), not from the
  decoding. Differences of form, as for corr2012: symbols as comments, `[%t Shrt]`, clocks that
  Aquarium truncates to the second (Hydra64 keeps the tenths). For a complete comparison,
  HugeBase would have to be exported as several files of less than 4 GB.
