#pragma once

#include "game.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chess::pgn {

// Parses a PGN game. Returns false (with a message) if a move is unreadable;
// the moves read so far stay in `game`.
bool parseGame(std::string_view text, Game& game, std::string* error = nullptr);

// Reading for a game that will only be encoded into a database (conversion).
struct ParseOptions {
    bool withSan = true; // false: Node::san stays empty (see Game::addMove)
    // Main line supplied in binary: only the tags of `text` are read, the moves come
    // from `mainline` (16-bit words: from | to << 6 | promotion << 12, promotion 1 = N,
    // 2 = B, 3 = R, 4 = Q; 0xFFFF = null move; castling = the king onto its rook's square).
    const uint16_t* mainline = nullptr;
    size_t mainlineSize = 0;
    bool hasMainline = false;
};
bool parseGame(std::string_view text, Game& game, std::string* error, const ParseOptions& options);

std::string writeGame(const Game& game);
// True if a pre-move comment placed on `n` keeps its place when the PGN is read back: at the head
// of a variation, after the previous move's comment or after its variations. Elsewhere it would be
// confused with the previous move's comment (same text, same place for the reader).
bool keepsPreComment(const Node* n);

std::string nagSymbol(int nag);
// ChessBase evaluation profile, in the pre-game comment:
// "[%evp first,last,v0,v1,…]", one engine evaluation in centipawns (White's side) per
// half-move. On a move: "[%eval centipawns,depth]" and "[%emt h:mm:ss]" (time spent).
// These texts stay in the comments (nothing is lost when saving); they are
// only removed from the display, and the evaluations carried over to the moves for the graph.
// Comment without any "[%name …]" command (they are not meant to be read).
std::string withoutCommands(const std::string& comment);
// Time spent on the move, in seconds, read from "[%emt h:mm:ss]" (or "m:ss", or seconds); -1 if there is none.
int elapsedSeconds(const std::string& comment);
// Carries the profile, then the "[%eval]", over to the moves that have no evaluation; number of moves served.
int applyEvalProfile(Game& game);
constexpr int DiagramNag = 220; // "diagram after this move" ($220; "[#]" in a ChessBase comment)
// ChessBase medals (bits of [%mdl n]): names in French, in bit order.
std::vector<std::string> medalNames(unsigned medals);

// Fast index of a PGN file containing several games.
struct GameEntry {
    size_t offset = 0;
    size_t length = 0;
    std::string white, black, whiteElo, blackElo, result, date, event, site, round, eco;
};

std::vector<GameEntry> indexDatabase(std::string_view data);

} // namespace chess::pgn
