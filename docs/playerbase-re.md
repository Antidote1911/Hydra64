# ChessBase players encyclopedia ("Playbase") — format and log

Decoding of 20 November 2026, on the user's copy dated 13 October 2017
(`…/Documents/ChessBase/Playbase`). Nothing was disassembled: ChessBase's program is not in this
folder, everything comes from examining the data.

Readers: `tools/playerbase/playerbase.py` (Python) and `src/db/playerbase.*` (C++, used by
Hydra64's player profile).

## Content of the folder

| File | Size | Content |
|---|---|---|
| `Playbase.player` | 90 MB | 525,166 player records, in clear |
| `PlayBase.rating` | 220 MB | 11,574,081 Elo ratings (one per player and per FIDE list), scrambled |
| `PlayBase.media` | 4.9 MB | 54,558 photo records, scrambled |
| `PlayBase.MMA` … `MMZ` | 536 MB | 37,653 `.jpg` photos, stored by initial |

The case of file and folder names varies: they must be looked up ignoring it.

## `Playbase.player`

8-byte header: number of records (32 bits, **big-endian**), then `00 00 00 32` (50, probably a
version). Then, for each record: its length (32 bits, big-endian) and its content, itself
little-endian:

| Position | Size | Field |
|---|---|---|
| 0 | 4 + n | family name: length (32 bits) then Windows-1252 text |
| … | 4 + n | first name, in the same way |
| +0 | 4 | date of birth (see "Dates") |
| +4 | 2 | rank among namesakes (0 to 10) |
| +6 | 4 | player identifier (2 to 571,986, unique): it is the one found in the ratings |
| +10 | 4 | date of death, 0 if absent |
| +14 | 2 | titles (see below) |
| +16 | 2 | federation (ChessBase number, see below) |
| +18 | 2 | previous federation (0 most often; Kasparov: URS) |
| +20 | 4 | bit 0: male. Rare high bits (`0x40000400`, `0x40001001`… 37 players): unknown |
| +23 | 3 | non-zero for 38 players only (the same high bits) |
| +24 | 91 | zeros |
| +115 | 4 | first photo record in `.media`, −1 if none (16,995 players have one) |
| +119 | 4 | −1, 0, 32,758… or `0xCCCCCCCC`: uninitialised memory, no use found |
| +123 | 4 | same |
| +127 | 4 | 0 almost always |
| +131 | 4 | 55 (523,611 records) or 47 (1,541): unknown |
| +135 | 2 | 0 to 100; frequent values 2, 1, 20, 21, 22: unknown |
| +137 | 4 | date, often 0: unknown (Fischer: 23 November 2012) |
| +141 | 4 | date: 13 October 2017 for 290,428 records (date of the encyclopedia) — last update? |

The fixed part after the two names is always 145 bytes.

### Dates

As in ChessBase databases: `year × 512 + month × 32 + day`, month and day at 0 if they are unknown
(Carlsen: 1990 only; Fischer: 9 March 1943 – 17 January 2008).

### Titles (position +14)

| Bits | Meaning | Checked on |
|---|---|---|
| 2–3 | women's title: 1 WFM, 2 WIM, 3 WGM | counts (1,774 / 919 / 360), J. Polgar |
| 4–5 | title: 1 FM, 2 IM, 3 GM | counts (8,301 / 4,180 / 1,771) |
| 6 (`0x40`) | women's world champion | Menchik, Bykova, Chiburdanidze, Stefanova |
| 7 (`0x80`) | world champion | Lasker, Alekhine, Fischer, Kasparov |
| 8 (`0x100`) | reigning at the date of the encyclopedia | Carlsen, Tan Zhongyi |
| 1 (`0x02`) | honorary title? | Böök, H. Kramer (6 players) |
| 0 (`0x01`) | unknown | Marco Bode (9 players) |

### Federations

ChessBase numbers (0 = unknown). The table of `PlayerBase::nationCode` was established by looking
at the strongest players of each number: 1 AFG, 2 ALB, 3 ALG … 7 ARG, 8 ARM, 9 AUS … 42 ENG,
43 ESP … 49 FRA … 53 GER … 108 NOR … 121 RUS … 147 USA, 148 URU, 149 URS … 156 YUG … 160 GDR,
161 Czechoslovakia, 192 Saar, 249 MNE, 250 SRB, 253 KOS. Unidentified numbers (returned empty):
18, 27, 159, 162–166, 168, 173, 175–177, 180–183, 186–190, 193, 196, 198, 199, 201, 204–211,
213–215, 217–220, 222, 223, 225–233, 235, 236, 239–248, 251, 252 — all very sparsely populated.

## `PlayBase.rating`

4 header bytes (`C3 9A B0 00`, that is 11,574,979: close to the number of ratings without being
equal to it), then ratings of **19 bytes**. Each byte is combined (exclusive OR) with the byte of
the same rank of the key:

    F7 FD FF 55 C7 ED 4F C5 97 DD 9E 34 67 CD EF A5 37 BD 3F

| Position | Size | Field |
|---|---|---|
| 0 | 4 | player identifier |
| 4 | 2 | 0 |
| 6 | 2 | Elo, **most significant byte first** (the only field in that order) |
| 8 | 2 | 0 |
| 10 | 1 | 0, except 35 ratings (1 or 5): unknown |
| 11 | 4 | date of the list (same encoding as the dates of the records, day = 1) |
| 15 | 4 | world rank in this list, −1 if unranked (2,246,862 ratings) |

The first three ratings of the file are empty (identifier 0). A player's ratings almost always
follow one another (302,993 groups for 302,865 rated players) but are not sorted by date. The lists
run from 1976 to October 2017.

The key was found by cross-checking: constant bytes (zeros), −1 values of the rank, then known Elo
and dates of Kasparov (2595 in January 1980, 2625 in January 1981…) and of Carlsen (2882 in 2014,
2826 and no. 1 in October 2017).

## `PlayBase.media`

54,558 records of **89 bytes**, without a header (record 0 only contains padding). Key: the
sequence of 32 bytes below, taken from its 26th byte and restarted at the beginning of each record,
each byte being moreover combined with `CC`:

    EE 7A 5E 4A 4E 1A 3E 6A 2E 3A 9E 0A 8E DA 7E 2A 6E FA DE CA CE 9A BE EA AE BA 1E 8A 0E 5A FE AA

that is `key[j] = sequence[(j + 25) mod 32] xor CC`. (`CC` is the byte Visual C++ fills
uninitialised memory with: the unused areas of the records are full of it.)

| Position | Size | Field |
|---|---|---|
| 0 | 4 | file prefix (3 letters + zero): "Car", "car", "aab"… |
| 4 | 40 | photographer (zero-terminated text) |
| 44 | 30 | place |
| 74 | 4 | date of the photo (often the year alone) |
| 78 | 4 | number of the photo |
| 82 | 4 | next record of the same player, −1 at the end |
| 86 | 3 | `01 00 01` or `01 00 00`: unknown |

The record designated by the player (position +115 of their record) is an empty **anchor record**:
only its "next" link is used, it leads to the first photo. A photo's file is
`PlayBase.MM<initial>/<prefix><number on 5 digits>.jpg` (`PlayBase.MMC/Car13104.jpg`). Of 37,560
photos referenced, 8 are missing on disk; 93 files are referenced by nobody.

## Checks

- Carlsen: GM, NOR, born in 1990, reigning champion; 92 ratings from 2001 to October 2017, maximum
  2882 (2014), world no. 1; 57 photos, all present.
- Fischer: 9 March 1943 – 17 January 2008, USA, world champion. Kasparov: RUS, previously URS.
- Anand: maximum 2817 in March 2011, world no. 1.
- Tests: `HYDRA64_PLAYBASE=<folder> build/core_tests …` and `ui_tests`.

## Log

- 2026-11-20 (1) — Records: big-endian lengths, two prefixed names, 145 fixed bytes. Dates,
  identifier, titles, federations, sex.
- 2026-11-20 (2) — Ratings: 19-byte records, key found byte by byte; errors corrected along the way
  (low bit of the year, most significant byte of the Elo).
- 2026-11-20 (3) — Photos: 89-byte records, shifted cyclic key, chaining per player, file names;
  37,552 photos found out of 37,560.
- 2026-11-20 (4) — C++ reader `hy64db::PlayerBase` and Hydra64's player profile.

## Ideas to test

- [ ] Unknown fields of the records: +131 (55 / 47), +135, dates +137 and +141, high bits of +20.
- [ ] Bits 0 and 1 of the titles; bytes 86–88 of the photo records; byte 10 of the ratings.
- [ ] Meaning of the 4 header bytes of `PlayBase.rating`.
- [ ] Unidentified federation numbers (listed above).
- [ ] The 128 players whose ratings are in several groups: why?
