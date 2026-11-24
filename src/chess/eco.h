#pragma once

// Opening classification (ECO codes and names), from the data of
// lichess-org/chess-openings (public domain, resources/eco-opening/*.tsv): one line per opening,
// "eco <TAB> name <TAB> SAN moves". Positions are recognised whatever the move order.

#include "game.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chess::eco {

struct Opening {
    std::string eco;  // « C41 »
    std::string name; // "Philidor Defense"
    int plies = 0;    // length of the reference line
};
// Line of the file (ECO browser), in file order
struct Line {
    std::string eco, name, moves; // SAN moves with numbers ("1. e4 e5 2. Nf3")
};

class Book {
public:
    // Adds the openings of a TSV file ("eco name pgn" header ignored). Returns the number read.
    int load(std::string_view tsv);
    size_t size() const { return m_byKey.size(); }

    // Opening of the position (or nullptr).
    const Opening* lookup(const Position& pos) const;
    // Deepest opening reached by the main line; `ply`: half-move where it is
    // recognised (0: starting position).
    const Opening* classify(const Game& game, int* ply = nullptr) const;
    // Main name of an ECO code (the shortest line of that code), empty if unknown.
    std::string nameOf(const std::string& eco) const;
    const std::vector<Line>& lines() const { return m_lines; }

private:
    std::unordered_map<uint64_t, Opening> m_byKey;
    std::unordered_map<std::string, Opening> m_byCode;
    std::vector<Line> m_lines;
};

} // namespace chess::eco
