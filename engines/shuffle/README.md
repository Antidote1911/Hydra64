# Shuffle

A UCI chess engine that plays silly moves, almost at random. Written in Rust, with no dependency
at all. Ideal for beginners, for testing an interface or a bot, or for feeling very strong.

## Building

```sh
cargo build --release      # binary: target/release/shuffle
cargo test --release       # perft on 5 reference positions, style, seed…
```

The binary is standalone: copy it wherever you like and declare it as a UCI engine in any interface
(Hydra64, Arena, Cute Chess, BanksiaGUI, lichess-bot…). In Hydra64, it is built by
`./build.sh` and added automatically to the engine manager.

## UCI options

| Option | Values | Effect |
|---|---|---|
| `Style` | `Silly` (default), `Random` | `Silly`: at random, but attracted by captures (even defended ones), checks, promotion to a queen, and mate in one (often seen). `Random`: all legal moves with equal chances. |
| `Seed` | 0 – 2147483647 | 0: different randomness for each game. Other value: reproducible games (seed reused at each `ucinewgame`). |
| `Move Delay` | 0 – 10000 ms (300) | Fake thinking before playing, never more than `movetime` nor than 1/40 of the time left on the clock. |
| `MultiPV` | 1 – 256 | Number of "analysis" lines sent (other moves considered). |

The other options sent by interfaces (`Threads`, `Hash`, `UCI_Elo`…) are ignored.

## Protocol

`uci`, `isready`, `ucinewgame`, `setoption`, `position startpos|fen … moves …`,
`go` (`wtime`/`btime`/`winc`/`binc`, `movetime`, `depth`, `nodes`, `infinite`, `ponder`),
`stop`, `ponderhit`, `quit`. The announced score is simply the material after the move.
With no legal move (mate or stalemate): `bestmove 0000`.

Convenience commands in a terminal: `d` displays the board, `go perft N` counts the positions.

```text
$ ./shuffle
position startpos moves e2e4
go movetime 100
info depth 1 seldepth 1 multipv 1 score cp 0 nodes 20 nps 0 time 0 pv h7h5
bestmove h7h5
```
