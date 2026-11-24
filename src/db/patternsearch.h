#pragma once

#include "chess/position.h"
#include "database.h"

#include <atomic>
#include <functional>
#include <vector>

// Searches that replay the games of a database: manoeuvre of a piece, pawn structure,
// material. All walk the database on several threads; a non-null `cancel` interrupts them,
// `progress(done, total)` follows the progress (in games).
namespace hy64db {

// A piece goes from `from` to `to` (through `via` if given) in at most `maxMoves` moves of
// that piece.
struct ManeuverQuery {
    chess::PieceType piece = chess::Knight;
    int color = 2; // 0: White, 1: Black, 2: either
    chess::Square from = chess::NoSquare, to = chess::NoSquare, via = chess::NoSquare;
    int maxMoves = 4;
    bool valid() const { return from != chess::NoSquare && to != chess::NoSquare && from != to && maxMoves >= 1; }
};
using ScanProgress = std::function<void(uint64_t, uint64_t)>;

std::vector<uint32_t> searchManeuver(const Database& db, const ManeuverQuery& q, const std::atomic<int32_t>* cancel = nullptr, const ScanProgress& progress = {});
// Games where the pawns of both sides occupy, at some point, exactly the squares of `pos`.
std::vector<uint32_t> searchPawnStructure(const Database& db, const chess::Position& pos, const std::atomic<int32_t>* cancel = nullptr, const ScanProgress& progress = {});
// Games that go through the same material as `pos` (same pieces for each side, wherever they are).
std::vector<uint32_t> searchSameMaterial(const Database& db, const chess::Position& pos, const std::atomic<int32_t>* cancel = nullptr, const ScanProgress& progress = {});

// Is the manoeuvre in this sequence of positions? (exposed for the tests)
class ManeuverTracker {
public:
    explicit ManeuverTracker(const ManeuverQuery& q);
    bool feed(const chess::Position& pos); // next position of the game; true as soon as the manoeuvre is done

private:
    struct Track {
        chess::Square at;
        int moves;
        bool viaSeen;
    };
    ManeuverQuery m_q;
    bool m_first = true;
    uint64_t m_prev[2] = {0, 0};      // squares of the pieces of the searched type, per side
    std::vector<Track> m_tracks[2];
};

} // namespace hy64db
