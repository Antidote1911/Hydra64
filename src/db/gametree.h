#pragma once

#include "chess/treebook.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Opening tree built from the games of a database: for every position of the first half-moves
// of the main lines, the moves played there with their statistics (wins, draws and losses of the
// side playing the move, its highest Elo, last year). Statistics are those of the games in which
// the move was played in that position, whatever the move order that led to it — as in
// ChessOK / Aquarium trees (the games reaching the same position through another move are not added).
namespace hy64db {

class Database;

struct GameTreeOptions {
    int maxPlies = 50;          // half-moves of each game that go into the tree, at most
    // Each game is followed up to its last position visited at least this many times in the
    // database, plus one move: what it is alone to play after that is left out (1: everything up
    // to maxPlies). 2 and 50 half-moves give the trees ChessOK ships with its databases.
    uint32_t minGames = 2;
    const std::vector<uint32_t>* games = nullptr; // only these games (nullptr: the whole database)
};

struct GameTreeResult {
    uint64_t games = 0;     // games that went into the tree
    uint64_t positions = 0; // positions with at least one move written
    uint64_t moves = 0;     // moves written
};

// Writes the tree of `db` to `output` (.hy64book or Polyglot .bin). Deleted games, games without
// moves and games not starting from the initial position are left out. `progress(done, total)`;
// returning false cancels (error "cancelled"). Temporary files are written next to `output`.
bool buildGameTree(const Database& db, const std::string& output, chess::treebook::TreeFormat format, const GameTreeOptions& options,
                   GameTreeResult* result = nullptr, std::string* error = nullptr, const std::function<bool(uint64_t, uint64_t)>& progress = {});

} // namespace hy64db
