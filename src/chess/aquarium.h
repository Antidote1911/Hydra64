#pragma once

#include "position.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Aquarium / Chess Assistant opening trees ("Direct Tree", .hsh + .elm, KPAL0005), read
// directly, read-only. Format established by reverse engineering for interoperability
// purposes (Directive 2009/24/EC, art. 6), from the user's files and their
// AquariumBookAdapter program:
//
// - Position key: "CPS" packing by category (pawn structure, major and minor
//   pieces, pawns per file, queens, king and rooks, bishops, knights, en
//   passant, side to move), variable-length codes from Aquarium's tables (addonce1.dll, embedded
//   in the code: aquarium_codes.inc), preceded by a 1 bit, least significant bits first.
// - .hsh: byte-by-byte search tree on the key (8-byte entries; internal nodes
//   and .elm blocks). Exact entry: the byte is consumed; range entry: it is not.
// - .elm: blocks of sorted records; a record = end of the key, then (next
//   byte) moves, statistics… and, as trailing bytes, Elo and years.
// - Move: piece type (short code) then rank among Aquarium's candidate moves for that
//   type (pieces sorted by square, directions in a fixed order, castling first for the king on
//   e1/e8, fixed slots for pawns).
namespace chess::aquarium {

struct BookMove {
    Move move;
    bool hasStats = false;
    // From the point of view of the side playing the move
    uint32_t wins = 0, draws = 0, losses = 0;
    uint16_t maxElo = 0; // highest Elo of the players of the move (0: unknown)
    uint16_t year = 0;   // last year it was played (0: unknown)
    uint64_t games() const { return uint64_t(wins) + draws + losses; }
};

class Book {
public:
    // `treePath`: the .hsh or the .elm (the other one beside it).
    bool open(const std::string& treePath, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_elm.is_open(); }
    const std::string& path() const { return m_path; } // .hsh file
    bool hasStatistics() const { return (m_mask & 0x2040) != 0; }
    bool hasRatings() const { return (m_mask & 0x100000) != 0; }
    bool hasYears() const { return (m_mask & 0x200000) != 0; }
    uint64_t blockCount() const { return m_blocks; }

    // Moves of the position (legal), most played first if the tree has statistics.
    std::vector<BookMove> moves(const Position& pos);

    // CPS key of a position (leading bit included), least significant bits first; empty if the
    // position cannot be represented (more than two knights…). Exposed for the tests.
    std::vector<uint8_t> keyBits(const Position& pos) const;

private:
    struct Entry {
        uint32_t offset;
        uint8_t b[4];
        bool isNode() const { return (b[1] & 0xe0) == 0xa0; }
    };
    bool findRecord(const std::vector<uint8_t>& key, std::vector<uint8_t>& record);

    std::vector<Entry> m_entries;
    std::ifstream m_elm;
    std::string m_path;
    uint32_t m_mask = 0;
    uint64_t m_blocks = 0;
};

} // namespace chess::aquarium
