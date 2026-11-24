#pragma once

#include "chess/game.h"
#include "chess/types.h"
#include "database.h"

#include <cstdint>
#include <string>
#include <vector>

// Preparation against a player: what he plays with one colour, taken from his games of a
// database. The main lines of his games are gathered in a tree of moves, each move with the
// number of games and the player's score; then the lines in which he scores worst and best, his
// openings by ECO code, and his losses by length.
//
// Transpositions are merged: the figures of a move are those of all the games in which it was
// played in that position, whatever the move order that led to it (en passant rights ignored).
// A position reached by several orders is developed once, under the order met in the most games;
// the other orders end on a node marked `transposition`.
namespace hy64db {

struct PrepOptions {
    int maxPlies = 24;     // half-moves of each game that go into the tree
    uint32_t minGames = 2; // a move played in fewer games is left out of the tree
    int fromYear = 0;      // only the games from this year on (0: all; games without a year are then left out)
};

struct PrepNode {
    std::string san;                // move leading to this node (empty: starting position)
    int ply = 0;                    // half-move number of that move (0: starting position)
    bool byPlayer = false;          // move played by the studied player
    uint32_t games = 0;             // games playing this move in this position, unknown results included
    PlayerStats::Score score;       // from the player's point of view
    int lastYear = 0;               // last year the move was seen (0: unknown)
    std::vector<uint32_t> ids;      // those games, in database order
    std::vector<PrepNode> children; // most played first
    uint32_t dropped = 0;           // games continuing with a move left out (fewer than minGames)
    std::string key;                // position after the move (board, side to move, castling rights)
    bool transposition = false;     // that position is developed elsewhere in the tree (same `key`): no children here
};

struct Preparation {
    chess::Color side = chess::White; // the player's colour
    PrepNode root;                    // root.games: his games with that colour, kept
    uint32_t otherStarts = 0;         // games left out: not from the initial position, or unreadable
    int firstYear = 0, lastYear = 0;
    std::vector<PlayerStats::Opening> openings; // by ECO code, most played first
    // Losses by length: before move 25, moves 25 to 40, after move 40.
    uint32_t lossesShort = 0, lossesMiddle = 0, lossesLong = 0;
    int averageLossMoves = 0, averageWinMoves = 0; // 0: none
    std::vector<uint32_t> quickLosses;             // his shortest losses (25 moves at most), shortest first
    std::vector<uint32_t> losses;                  // all his losses with that colour, in database order
};

Preparation prepareAgainst(const Database& db, uint32_t player, chess::Color side, const PrepOptions& options = {});

// Lines of the tree ranked by the player's score: the weakest first (`weakest`) or the strongest.
// Only lines ending with a move of the opponent (what one can choose to play against him), played
// in at least `minGames` games with a known result, and at least `margin` points of percentage away
// from his overall score; a line and its continuation are not both listed. Each line is the path
// from the first move to its last node.
std::vector<std::vector<const PrepNode*>> rankedLines(const Preparation& prep, bool weakest, uint32_t minGames = 3, size_t max = 8, double margin = 10.0);

// The tree compared with one's own repertoire (a game with variations, for the other colour),
// followed move by move from the starting position.
enum class PrepCover {
    Inside,    // the move is in the repertoire
    Uncovered, // a move of the player that the repertoire does not consider, where it plans others
    Ends,      // the repertoire stops just before this move
    Outside,   // beyond one of the above, or after a move of one's own side that is not the repertoire's
};
struct PrepGap {
    std::vector<const PrepNode*> path; // from the first move to the move concerned
    PrepCover kind = PrepCover::Uncovered;
    std::vector<std::string> known;    // what the repertoire plans in that position (empty: it stops)
};
struct PrepCoverage {
    std::vector<std::pair<const PrepNode*, PrepCover>> states; // every node of the tree
    std::vector<PrepGap> gaps;                                 // Uncovered and Ends, most games first
    PrepCover of(const PrepNode* n) const;
};
PrepCoverage coverage(const Preparation& prep, const chess::Game& repertoire);

} // namespace hy64db
