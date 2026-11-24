# cbvault-format 0.1.7, modified copy for Hydra64

Source: crate `cbvault-format` 0.1.7 (MIT licence, https://github.com/itshak/cbvault), `src/`
folder copied as it is, then modified. The examples and integration tests are not copied.

Plugged in by: `[patch.crates-io]` in `../../Cargo.toml`.

## Modifications

Marked "Hydra64" in the code.

- `src/twocbh/annotations.rs`, annotation `09`: a multiple-choice training question (field
  following the first two bytes equal to 2) does not have the shape of a one-move question — each
  choice carries its points and texts instead of a move. Its size was computed like that of a
  one-move question, and since a .2cbh database does not record the size of its annotations, the
  whole game became unreadable ("annotation runs past content", 31 games of the Mega Database
  2026).

- `src/game/annotations/timing.rs`, `evaluation_pair`: a mate announced by an engine (`[%eval]`)
  is written in half-moves, like the export of ChessBase '26, and not in moves.

- `src/cbh/annotations.rs`, `check_positions`: annotations set on a move of a game without moves
  are ignored instead of making the game unreadable (ChessBase exports it empty).

## Updating to a new version

Copy `src/` of the new version again, carry these modifications over, update the version in
`Cargo.toml` (here and in `../../Cargo.toml`), remove `source` and `checksum` from the
`cbvault-format` entry in `../../Cargo.lock`.
