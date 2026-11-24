#pragma once

#include "chess/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// Where the pieces stand, over a set of games (ChessBase: "Probabilité des pièces",
// "Statistiques du premier déplacement de la pièce"): for each kind of piece, the number of
// positions of the main lines in which it stands on each square, by slice of the game; and, for
// each piece of the starting position, where its first move takes it and when.
namespace hy64db {

class Database;

struct PieceHeat {
    // Slices of the game, by move number: 1–10, 11–20, 21–30, 31–40, 41 and beyond.
    static constexpr int Slices = 5;
    static int sliceOfPly(int ply) { return ply <= 0 ? 0 : (ply - 1) / 20 >= Slices ? Slices - 1 : (ply - 1) / 20; }

    uint32_t games = 0;   // games counted
    uint32_t skipped = 0; // left out: not from the initial position, or unreadable
    // Positions counted in each slice: the position before each move (the final one is not counted).
    std::array<uint64_t, Slices> positions{};
    // occupancy[slice][piece][square]: positions of the slice in which `piece` (chess::Piece, 0–15) stands on the square.
    std::array<std::array<std::array<uint64_t, 64>, 16>, Slices> occupancy{};
    // First move of the piece standing on `origin` in the starting position:
    // firstTo[origin][target] games (castling: the king to g or c, its rook to f or d);
    // firstPly[origin]: sum of the half-move numbers of those first moves.
    std::array<std::array<uint32_t, 64>, 64> firstTo{};
    std::array<uint64_t, 64> firstPly{};

    // Share of the positions (slice -1: the whole game) with `piece` on each square, in millionths.
    std::array<int, 64> shares(chess::Piece piece, int slice = -1) const;
    uint64_t positionCount(int slice = -1) const;
    uint32_t moved(chess::Square origin) const; // games in which that piece moved
    void add(const PieceHeat& other);
};

// `ids`: the games (deleted ones are skipped). `progress(done, total)`; false cancels (`cancelled` set).
PieceHeat pieceHeat(const Database& db, const std::vector<uint32_t>& ids, const std::function<bool(uint64_t, uint64_t)>& progress = {}, bool* cancelled = nullptr);

} // namespace hy64db
