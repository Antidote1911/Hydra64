# cbvault 0.1.7, modified copy for Hydra64

Source: crate `cbvault` 0.1.7 (MIT licence, https://github.com/itshak/cbvault), `src/` folder
copied as it is, then modified. The crate's examples, integration tests and benchmarks are not
copied. The `gigachess` crate still comes from crates.io; `cbvault-format` and `cbvault-chess` are
modified local copies too (see their own `HYDRA64.md`).

Plugged in by: `[patch.crates-io]` in `../../Cargo.toml`.

## Modifications

All are marked "Hydra64" in the code.

- `src/pgn/mod.rs`: `PgnWriter::write_game_or_main_line` and `…_v2`, and the `MainLine` sink. For a
  game without variation or annotation, only the tags are written and the main line is returned as
  `moves2` words: no notation is computed.
- `src/bridge/mod.rs`: `Database::game_for_encoding`, which calls them (access to the move and
  annotation files is private to the crate, hence the copy).

- `src/pgn/comments.rs`: function `cyrillic` and the corresponding branch of `clean_into`. A
  comment that is not UTF-8 and contains a Cyrillic word (four bytes ≥ 0xC0 in a row, more of those
  bytes than Latin letters) is read as Windows-1251, without the export's symbol table. The
  original cbvault reads it as Windows-1252, like ChessBase's export, which makes Russian
  unreadable and loses letters. **This change modifies the output**: 166 lines of the PGN export of
  the Mega Database 2025, all Russian.

- `src/pgn/comments.rs`: Russian context (`set_cyrillic_context`, re-exported by `src/pgn/mod.rs`),
  set by the bridge for the current thread for the duration of a rendering. In this context, any
  comment that is not UTF-8 and carries a letter 0xC0…0xFF is read as Windows-1251, even if too
  short to be recognised on its own ("или", "и"). The bridge (`../../src/lib.rs`) sets it for a
  Russian database (sample of 400 games) or for a game that already has recognised Russian. Outside
  the context, nothing changes.

- `src/pgn/comments.rs`, `clean_into`: the diagram byte `9E` of a text that is not UTF-8 becomes
  the private diagram character (rendered "[#]"), including in a mixed text. (Tried then removed:
  rendering as "[#]" the byte `04` of an "all languages" text — the export of ChessBase '26 does
  not do it either.)
- `src/pgn/mod.rs`, `Tree::play`: a pawn changing file onto an empty square outside en passant
  (irregular move kept by the copy of cbvault-chess) is written "cxb6" and not "b6".

- `src/pgn/comments.rs`, end of the file and `Commentary::before`: **training questions**
  (annotation of type 09, which cbvault did not decode). Rendered in front of the move to find, in
  its own comment, in the form of ChessBase's export:
  `{[%tqu "En","question","hint 1","hint 2","f6h7","feedback",6,"De",…]}`. Byte layout described at
  the top of the code; calibrated on the PGN export of ChessBase '26.

- `src/pgn/comments.rs`, `times`: `[%emt h:mm:ss]` without hundredths, like the export of
  ChessBase '26.

## Updating to a new version of cbvault

Copy `src/` of the new version again, carry these eight modifications over, update the version in
`Cargo.toml` (here and in `../../Cargo.toml`), then check that the conversion of the Mega gives a
byte-for-byte identical database (apart from the Cyrillic comments: compare the PGN export before
and after, `hydra64-convert base.cbh output.pgn`, about 28 s and 7.7 GB).

Additions of 22 November 2026 (same files, all marked "Hydra64"): time control and clocks as
tags (`time_tags`), clock per move (`move_clock`), multiple-choice questions (`[%tmc]`), `[%emt]`
rounded to the second, every diagram marker rendered "[#]", census of the annotations (`census`,
variable `CBB_CENSUS`).
