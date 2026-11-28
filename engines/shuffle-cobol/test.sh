#!/usr/bin/env bash
# Checks Shuffle-COBOL against the same perft references as the Rust Shuffle,
# then plays a short UCI exchange. Usage: ./test.sh [path/to/shuffle-cobol]
set -u
BIN=${1:-./shuffle-cobol}
fail=0

perft() { # fen depth expected
    local out
    out=$(printf 'position fen %s\ngo perft %s\nquit\n' "$1" "$2" | "$BIN")
    if grep -q "perft $2: $3 nodes" <<<"$out"; then
        echo "ok    perft $2 = $3  ($1)"
    else
        echo "FAIL  perft $2, expected $3 ($1): $out"
        fail=1
    fi
}

perft "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 4 197281
perft "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 3 97862
perft "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1" 4 43238
perft "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1" 3 9467
perft "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8" 3 62379

# Back-rank mate: Silly almost always plays Rd8#
mates=0
for seed in $(seq 1 20); do
    out=$(printf 'setoption name Seed value %s\nsetoption name Move Delay value 0\nposition fen 6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1\ngo movetime 10\nquit\n' "$seed" | "$BIN")
    grep -q '^bestmove d1d8' <<<"$out" && mates=$((mates + 1))
done
if [ "$mates" -ge 12 ]; then echo "ok    mate in one found $mates times out of 20"; else echo "FAIL  mate in one found $mates times out of 20"; fail=1; fi

# Same seed, same game
a=$(printf 'setoption name Seed value 42\nucinewgame\nposition startpos\ngo depth 1\nquit\n' | "$BIN" | grep bestmove)
b=$(printf 'setoption name Seed value 42\nucinewgame\nposition startpos\ngo depth 1\nquit\n' | "$BIN" | grep bestmove)
if [ "$a" = "$b" ]; then echo "ok    same seed, same move ($a)"; else echo "FAIL  seed: $a / $b"; fail=1; fi

# A plain UCI exchange
printf 'uci\nisready\nposition startpos moves e2e4 e7e5 g1f3\nsetoption name MultiPV value 3\ngo wtime 60000 btime 60000\nd\nquit\n' | "$BIN"

exit $fail
