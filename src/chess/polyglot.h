#pragma once

#include "position.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Opening books in Polyglot format (.bin): 16-byte entries (big-endian)
// sorted by position key — key (64 bits), move (16), weight (16), learning (32).
namespace chess::polyglot {

// Polyglot key of the position (en passant only counts if it is playable).
uint64_t key(const Position& pos);

struct BookMove {
    Move move;
    uint16_t weight = 0;
    uint32_t learn = 0;
};

// Move encoded in an entry → move of `pos`. Polyglot castling (king onto its rook's square,
// e1h1) becomes e1g1. Null move if it is not legal (key collision, damaged file).
Move decodeMove(const Position& pos, uint16_t raw);
// Reverse: move of `pos` → Polyglot code (castling written king onto its rook's square).
uint16_t encodeMove(const Position& pos, Move m);

class Book {
public:
    bool open(const std::string& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_file.is_open(); }
    const std::string& path() const { return m_path; }
    uint64_t entryCount() const { return m_count; }

    // Legal moves of the position, most played first (decreasing weight).
    std::vector<BookMove> moves(const Position& pos);

private:
    bool readEntry(uint64_t index, uint64_t& key, uint16_t& move, uint16_t& weight, uint32_t& learn);

    std::ifstream m_file;
    std::string m_path;
    uint64_t m_count = 0;
};

} // namespace chess::polyglot
