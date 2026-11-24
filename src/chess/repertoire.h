#pragma once

#include "game.h"

#include <map>
#include <string>
#include <vector>

// Opening repertoire: a tree of moves (a game with variations) for one side, and the
// report of what played games add to it — unforeseen opponent moves, lines that
// stop, deviations of my own.
namespace chess::repertoire {

// Adds to the repertoire the line leading to `upTo` (the whole main line of `game` if null),
// with its comments and symbols. Number of moves added, -1 if the starting position differs.
int addLine(Game& repertoire, const Game& game, const Node* upTo = nullptr);

struct Gap {
    enum Kind {
        Uncovered, // missing opponent move, where the repertoire plans other replies
        LineEnds,  // the repertoire stops, the game goes on
        Deviation, // my move is not the repertoire's
    };
    Kind kind = Uncovered;
    std::string line;  // moves up to the position ("1. e4 c5 2. Nf3"), empty: starting position
    std::string move;  // the move played in the games ("2... d6")
    std::string known; // what the repertoire plans there ("d6, Nc6"), empty if it stops
    int games = 0, whiteWins = 0, draws = 0, blackWins = 0;
    std::vector<int> ids; // identifiers given to add() (database rows, for instance), the first ones
};

// Report of the gaps between a repertoire and games. `me`: the repertoire's side.
class Report {
public:
    Report(const Game& repertoire, Color me);
    // One game; `id` is returned in Gap::ids. False if it does not start like the repertoire
    // (other starting position, or first move outside the repertoire without it being a useful gap).
    bool add(const Game& game, int id = -1);
    int gamesSeen() const { return m_seen; }
    int gamesInside() const { return m_inside; } // games that stayed in the repertoire to the end of their line
    std::vector<Gap> gaps() const;               // most frequent first

private:
    const Game& m_rep;
    Color m_me;
    int m_seen = 0, m_inside = 0;
    std::map<std::pair<int, std::string>, Gap> m_gaps; // (repertoire node, move in notation) → gap
};

} // namespace chess::repertoire
