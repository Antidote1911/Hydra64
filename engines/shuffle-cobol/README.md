# Shuffle-COBOL (bonus)

Shuffle, the UCI engine that plays silly moves almost at random, rewritten in COBOL for the fun
of it. The engine bundled with Hydra64 is still the Rust one (`../shuffle`). Nothing builds or
installs this version; it lives here as a curiosity.

It follows the same rules as the Rust Shuffle: the same move weights (captures ×3, checks ×2,
mate in one ×40 on top of that, queen promotion ×4, knight ×0.6, other underpromotions ×0.3),
the same UCI options (`Style`, `Seed`, `Move Delay`, `MultiPV`) and the same answers. It just
shouts in capital letters, calls its board `THE-CHECKERED-THING`, gets excited through
`OOH-SOMETHING-SHINY` and `HE-IS-TOTALLY-BROKEN`, and thinks by performing `SCRATCH-CHIN` once
per millisecond.

## Building

With [GnuCOBOL](https://gnucobol.sourceforge.io/) 3.x (the `gnucobol` package on Arch/AUR,
Debian, Fedora…):

```sh
cobc -x -O2 -o shuffle-cobol shuffle.cob
./test.sh            # perft on the 5 reference positions, mate in one, seed, UCI exchange
```

Then declare `shuffle-cobol` as a UCI engine in Hydra64 (or any other interface).

## Differences from the Rust version

- COBOL has no threads, so the engine listens to nobody while it fakes its thinking. A `stop`
  sent during that time is only read afterwards (the move comes at the end of `Move Delay`
  anyway). `go infinite` and `go ponder` work: the move is sent on `stop` or `ponderhit`.
- The randomness comes from `FUNCTION RANDOM`, so a given `Seed` does not produce the same games
  as the Rust Shuffle with that seed. It does produce the same games from one run to the next.
- `go perft` is capped at depth 6. It is slow. It is COBOL.
