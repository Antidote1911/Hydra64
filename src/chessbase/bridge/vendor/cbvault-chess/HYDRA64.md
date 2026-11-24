# cbvault-chess 0.1.7, modified copy for Hydra64

Source: crate `cbvault-chess` 0.1.7 (MIT licence, https://github.com/itshak/cbvault), `src/` folder
copied as it is, then modified. The crate's integration tests are not copied.

Plugged in by: `[patch.crates-io]` in `../../Cargo.toml`.

## Modifications

Marked "Hydra64" in the code.

- `src/decode.rs`, `play`: a move the chess engine judges illegal no longer stops the reading of
  the game if it can be played mechanically (a piece of the side to move leaves its square and
  lands neither on a piece of its own side nor on a king). ChessBase records and displays such
  moves: a king moving into check in a badly entered game (Dereviagin – Strenzwilk, US op 1993,
  39...Kg6 under fire from the h5 pawn), a pawn capture onto an empty square in a transposition
  note ("10.cxb6 - 9...b6"). Found by comparing the Opening Encyclopaedia 2024 with the PGN export
  of ChessBase '26 (21 November 2026).

- `src/decode.rs`, end of the reading of a game: a move stream without an end marker is no longer
  an error as soon as at least one move has been read (ChessBase reads these games up to their last
  move).

## Updating to a new version

Copy `src/` of the new version again, carry these modifications over, update the version in
`Cargo.toml` (here and in `../../Cargo.toml`), remove `source` and `checksum` from the
`cbvault-chess` entry in `../../Cargo.lock`.
