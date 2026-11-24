#pragma once

#include "chess/position.h"
#include "chess/treebook.h"
#include "mappedfile.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Position index of a database (.hy64p): for each position of the first D half-moves of the
// main line, the games going through it.
//
//   header (64 bytes): magic, version, number of keys (16), depth D (32), threshold K (36),
//                         games indexed (40), start of the lists (48), start of the state at D (56)
//   key table: 16 bytes per distinct position, sorted — Polyglot key (u64) then
//                    either (bit 63) the number of the single game, or the offset of its list
//   lists: varint number of games; if number ≥ K: white wins, draws, black wins,
//            highest Elo, last year (varints); then game numbers (deltas, varints)
//   state at D: 6 bytes per game — starting pawns already gone (u16) and material (u32) at
//              half-move D: a later position can only come after them (pawns do not
//              come back, pieces do not reappear without promotion).
namespace hy64db {

class Database;

class PositionIndex {
public:
    struct Stats {
        uint32_t games = 0, whiteWins = 0, draws = 0, blackWins = 0;
        uint16_t maxElo = 0, year = 0;
        bool known = false; // statistics stored (otherwise to be computed from the games)
    };

    bool open(const std::string& path, std::string* error);
    // Index stored in a single-file database: `length` bytes from `offset`.
    bool openRange(const std::string& path, uint64_t offset, uint64_t length, std::string* error);
    int depth() const { return m_depth; }
    uint64_t gamesIndexed() const { return m_gamesIndexed; }
    uint64_t positions() const { return m_keys; }

    // Games going through position `key` (within the indexed depth); false if none.
    bool lookup(uint64_t key, Stats* stats, std::vector<uint32_t>* games) const;
    // State of the game at half-move D (starting pawns gone, material); false if unknown.
    bool stateAtDepth(uint32_t game, uint16_t* missing, uint32_t* material) const;

private:
    MappedFile m_file;
    uint64_t m_keys = 0, m_gamesIndexed = 0, m_postings = 0, m_states = 0;
    int m_depth = 0;
    uint32_t m_threshold = 0;
};

// Builds (or rebuilds) the position index of the database. `progress(done, total)`; false = cancelled.
bool buildPositionIndex(Database& db, int depth, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress = {});

// Games of the database going through `pos` (main line): index for the first D
// half-moves, replay of the games beyond (after material and pawn filters) and of the games
// added since the index was built. Increasing numbers.
std::vector<uint32_t> searchPosition(const Database& db, const chess::Position& pos, const std::atomic<int32_t>* cancel = nullptr);

// Tree: moves played from `pos` in the database, most played first (statistics from the
// point of view of the side playing the move). Empty outside the indexed depth.
std::vector<chess::treebook::BookMove> positionTree(const Database& db, const chess::Position& pos);

constexpr int DefaultPositionDepth = 24;

} // namespace hy64db
