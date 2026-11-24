#pragma once

#include "chess/pgn.h"
#include "materialquery.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace hy64db {

// Search by headers (in the manner of the "General search" of SCID vs PC). Names are
// searched by word start, ignoring case and accents; a bound of 0 (or empty) is ignored.
struct HeaderQuery {
    std::string white, black; // players; ignoreColors: either side
    bool ignoreColors = false;
    std::string event, site, round, annotator;
    int whiteEloMin = 0, whiteEloMax = 0, blackEloMin = 0, blackEloMax = 0;
    int yearFrom = 0, yearTo = 0;
    uint16_t ecoFrom = 0, ecoTo = 0; // parseEco: 1 = A00 … 500 = E99
    enum ResultBits : uint8_t { WhiteWin = 1, DrawResult = 2, BlackWin = 4, Unknown = 8, AllResults = 15 };
    uint8_t results = AllResults;
    int movesMin = 0, movesMax = 0; // moves of the main line
    bool withVariations = false, withComments = false;
    uint8_t userFlags = 0; // required flags (Hydra64 databases; a PGN has none)
    MaterialQuery material; // material, patterns, pawn structure (games replayed)

    bool isEmpty() const;
};

// Game of a PGN file (index and text of the game). The number of moves is counted in the text.
bool matches(const HeaderQuery& q, const chess::pgn::GameEntry& e, std::string_view gameText);

// Number of moves (main line) of a PGN text, without decoding it: move numbers outside
// comments and variations.
bool hasTextComment(std::string_view pgnText); // a comment with text, not just "[%…]" commands
int countMoves(std::string_view gameText);

} // namespace hy64db
