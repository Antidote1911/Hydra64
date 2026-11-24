#pragma once

#include "chess/game.h"
#include "format.h"
#include "names.h"

#include <functional>
#include <string>
#include <vector>

// Encoding of a game (.hy64g block) and of its index fields.
//
// Moves: one byte = (piece number << 4) | code. The pieces of the side to move are
// numbered king first (0), then the others by increasing square (a1 = 0 … h8 = 63) — the order
// is recomputed from the board, with no history. Codes per piece:
//   king     0–7 directions, 8 kingside castling, 9 queenside castling; 10–15 reserved for markers:
//            10 null move, 11 NAG (+1 byte), 12 comment, 13 start of variation,
//            14 end of variation, 15 extension (+1 byte: 0 end of game, 1 clock + varint)
//   knight   0–7 jumps
//   pawn     0 one-square advance, 1 two squares, 2 capture towards the a-file, 3 towards the h-file,
//            4–7 / 8–11 / 12–15 promotions (Q, R, B, N) advancing / capturing towards a / towards h
//   rook     0–7 target file on the same rank, 8–15 target rank on the same file
//   bishop   0–7 target file on the rising diagonal, 8–15 on the falling one
//   queen    like the rook; "target file = source file" (impossible for a rook)
//            announces a diagonal move: the next byte is 64 + target square (never a
//            marker, which allows skipping variations without decoding them).
namespace hy64db {

// Header fields of a game, as they go into the index (names in clear, not numbered yet).
struct GameInfo {
    std::string white, black, event, site, round, annotator, source;
    Record record; // date, Elo, ECO, result, half-moves, flags, material, pawns (name numbers ignored)
};

// Game → block + index fields. False (and `error`) if a move cannot be encoded.
bool encodeGame(const chess::Game& game, std::vector<uint8_t>& blob, GameInfo& info, std::string* error = nullptr);

// Block + index row (+ names) → full game (headers, moves, variations, comments…).
bool decodeGame(const uint8_t* blob, size_t size, const Record& record, const Names& names, chess::Game& out,
                std::string* error = nullptr);

// Replays the main line without building a game (position search, index):
// `visit(position, half-move)` for the starting position (0) then after each move;
// returning false stops. False if the block is damaged.
bool replayMainline(const uint8_t* blob, size_t size, const std::function<bool(const chess::Position&, int)>& visit);
// Same, by moves: `visit(position before the move, move, half-move number from 1)`. Null moves are
// played but not reported.
bool replayMainlineMoves(const uint8_t* blob, size_t size, const std::function<bool(const chess::Position&, chess::Move, int)>& visit);

// Index signatures for a final position / a sequence of positions.
uint32_t materialSignature(const chess::Position& pos);
// Starting pawns (a2…h2, a7…h7) in the order they leave their square (first 12 × 5 bits,
// 4 bits of count): a position is only reachable if the pawns missing from it form
// the beginning of that sequence.
class PawnSignature {
public:
    void start(const chess::Position& pos);
    void update(const chess::Position& pos);
    uint64_t value() const;
    // Starting pawns missing from a position (16 bits); can `sig` go through it?
    static uint16_t missing(const chess::Position& pos);
    static bool compatible(uint64_t sig, uint16_t missingInTarget);

private:
    uint16_t m_missing = 0;
    std::vector<uint8_t> m_order;
};

} // namespace hy64db
