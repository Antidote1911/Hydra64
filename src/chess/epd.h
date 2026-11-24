#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// EPD (Extended Position Description, PGN standard by S. J. Edwards, 1994, open format): one
// position per line, the first 4 fields of a FEN then operations "code operands;"
// (bm, am, id, pv, dm, ce, acd, c0…c9, hmvc, fmvn…). A full FEN line (6 fields) is
// accepted too.
namespace chess {
class Game;
}

namespace chess::epd {

using Operations = std::vector<std::pair<std::string, std::string>>;

struct Record {
    std::string fen; // Full FEN (counters: hmvc / fmvn, otherwise those of the line, otherwise 0 1)
    Operations ops; // code, operands (quotes removed)
    const std::string* op(std::string_view code) const;    // first operand of this code, or nullptr
};

// False for an empty line, a comment ("#", ";", "%") or an invalid position.
bool parseLine(std::string_view line, Record& out);

// "code operands; code operands; …"; a ";" inside quotes is part of the string.
Operations parseOperations(std::string_view text);
// Operation codes of the standard (acd, am, bm, c0…c9, ce, dm, id, pv, v0…v9…).
bool isStandardOpcode(std::string_view code);

// EPD file → PGN text: one game per position ([SetUp] / [FEN], Event = id or
// "Position n", Round = n). Main line: the `pv` variation, otherwise the first `bm` move;
// other `bm` moves as variations, `am` moves as variations marked "?" (solution test
// convention). Other operations go into a comment. `positions`: games written,
// `skipped`: non-empty lines rejected.
std::string toPgn(std::string_view text, std::string_view source, int* positions = nullptr, int* skipped = nullptr);

// Game → EPD line (reverse of toPgn): starting position, `bm` = first move of the
// main line and other starting moves not marked "?", `am` = starting moves marked
// "?", "??" or "?!", `pv` = main line (more than one move), `id` = Event (except "?",
// empty or "Position n"). The starting comment gives back the other operations (`dm`, `ce`,
// `c0`…); free text becomes `c0`. The rest of the game (other headers, move
// comments, sub-variations) has no place in EPD and is not kept.
std::string fromGame(const Game& game);
// PGN text → EPD file (one line per game). `games`: lines written.
std::string fromPgn(std::string_view pgn, int* games = nullptr);

} // namespace chess::epd
