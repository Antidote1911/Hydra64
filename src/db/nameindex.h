#pragma once

#include "database.h"

#include <atomic>
#include <string>
#include <vector>

// Browsable index of a database: its players, tournaments, sites, annotators or sources, with the
// number of games of each (deleted games excluded).
namespace hy64db {

struct IndexEntry {
    uint32_t id = NoName;
    std::string name;
    uint32_t games = 0;
    int firstYear = 0, lastYear = 0; // 0: no known date
    int bestElo = 0;                 // players: best Elo recorded
    uint32_t wins = 0, draws = 0, losses = 0; // players: from the player's point of view
    double score() const { return wins + draws + losses ? 100.0 * (wins + 0.5 * draws) / (wins + draws + losses) : 0; }
};

// Entries with at least one game, in alphabetical order.
std::vector<IndexEntry> nameIndex(const Database& db, NameType type);
// Games of an index entry (for a player: with White or Black).
std::vector<uint32_t> gamesOf(const Database& db, NameType type, uint32_t id, const std::atomic<int32_t>* cancel = nullptr);

} // namespace hy64db
