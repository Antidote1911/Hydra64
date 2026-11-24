#pragma once

// Syzygy endgame tablebases (.rtbw / .rtbz files), read with Fathom (third_party/fathom, MIT).

#include "position.h"

#include <string>
#include <vector>

namespace chess::tb {

// Result from the point of view of the side to move: -2 loss, -1 loss saved by the 50-move
// rule, 0 draw, 1 win cancelled by the 50-move rule, 2 win.
struct MoveResult {
    Move move;
    int wdl = 0;
    int dtz = 0; // distance to the next pawn move or capture (half-moves), 0 if unknown
};
struct Probe {
    bool ok = false;
    int wdl = 0;
    int dtz = 0;
    std::vector<MoveResult> moves; // from best to worst
};

// Opens the tables of the folder (several folders separated by ":"). Returns the largest number of
// pieces covered (0: no table).
int init(const std::string& path);
int largest();
// Position with at most largest() pieces, with no castling possible.
Probe probe(const Position& pos);
// Result only, 50-move rule included, to adjudicate a game: -2, -1, 0, 1 or 2 from the point of
// view of the side to move (±1: win or loss cancelled by the 50-move rule, hence a draw).
// False if the position is not covered. Right after a capture or a pawn move, the result
// tables (.rtbw) are enough; otherwise the distance tables (.rtbz) are needed too.
bool result(const Position& pos, int& wdl);
int pieceCount(const Position& pos);

} // namespace chess::tb
