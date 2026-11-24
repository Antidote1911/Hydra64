#pragma once

#include "chess/position.h"
#include "format.h"

#include <vector>

// "Beauty" of a game: a mark from 0 to 3 given from its moves alone, without an engine, to find
// the spectacular games of a database at a glance (like ChessBase's "Beauty" column). What counts,
// for the side that wins:
//   - sacrifices: material given up and not taken back within two moves, in a game the side goes
//     on to win or in which it recovers the material within twelve moves (so a piece simply lost,
//     then a late swindle, does not count);
//   - a game ended by checkmate, more so with less material than the opponent;
//   - a king hunt: the losing king ends beyond the middle of the board with heavy material left;
//   - an underpromotion;
//   - a short game with a sacrifice.
// Draws and games without a winner get 0. The mark says "spectacular", not "well played": nothing
// here judges the soundness of a sacrifice.
namespace hy64db {

class BeautyRater {
public:
    void start();
    // Each move of the main line, with the position it is played in. Null moves are to be skipped.
    void move(const chess::Position& before, chess::Move m);
    // Points of the game (0: nothing remarkable). `result`: the game's result; without one, a final
    // checkmate still designates a winner.
    int points(Result result) const;
    // Mark 0…3 for a number of points.
    static int medals(int points);
    int medals(Result result) const { return medals(points(result)); }

private:
    struct Ply {
        int8_t balance = 0;  // material of White minus Black after the move (pawn 1 … queen 9), clamped
        bool capture = false, promotion = false, underpromotion = false;
    };
    std::vector<Ply> m_plies;
    int m_balance = 0;
    chess::Color m_first = chess::White; // side playing the first recorded move
    chess::Position m_last;              // position before the last move
    chess::Move m_lastMove;
};

// The mark as stored in the index flags (FlagBeautyRated + two bits).
inline uint16_t beautyFlags(int medals) { return uint16_t(FlagBeautyRated | ((medals & 3) << BeautyShift)); }
inline int beautyOf(uint16_t flags) { return (flags & FlagBeautyRated) ? (flags >> BeautyShift) & 3 : -1; } // -1: not rated

} // namespace hy64db
