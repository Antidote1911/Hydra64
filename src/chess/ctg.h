#pragma once

#include "position.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// ChessBase opening books (.ctg + .cto + .ctb), read-only.
//
// The format is unpublished; it is known through reverse engineering (free readers by Brutus,
// ctgreader, jja). A position is encoded (Huffman code, always seen from the side to move),
// its signature is hashed, and the hash points to a slot of the .cto index giving the page
// (4 KB) of the .ctg to search. A position's entry lists its moves (1 byte + annotation)
// and the statistics of the games leading to it.
namespace chess::ctg {

// Annotation of a move in the book.
enum Annotation : uint8_t {
    NoAnnotation = 0x00,
    Good = 0x01,        // !
    Mistake = 0x02,     // ?
    Excellent = 0x03,   // !!
    Blunder = 0x04,     // ??
    Interesting = 0x05, // !?
    Dubious = 0x06,     // ?!
    OnlyMove = 0x08,    // □
    Zugzwang = 0x16,    // ⨀
};
std::string annotationSymbol(uint8_t annotation); // "!", "?!"…; empty if none

// Mark of the position (colour of the move leading to it in ChessBase).
enum Recommendation : uint8_t { Neutral = 0, Avoid = 64, Recommended = 128 };

// Statistics of a position, from the point of view of the side that just moved.
struct Stats {
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    int averageElo = 0;     // average Elo of the players of the move (0: unknown)
    int performance = 0;    // Elo performance (0: unknown)
    uint8_t recommendation = Neutral;
    // Score in % (wins + ½ draws); -1 with no known result.
    double score() const;
};

struct BookMove {
    Move move;
    uint8_t annotation = NoAnnotation;
    bool hasStats = false; // position after the move present in the book
    Stats stats;           // from the point of view of the side playing the move
};

// Signature of a position (byte 0: length, en passant, castling rights).
struct Signature {
    std::array<uint8_t, 64> bytes{};
    int length = 0;
};
Signature signature(const Position& pos);
uint32_t hash(const Signature& sig);

// Move encoded in one byte → move of `pos`; null move if it is not legal.
Move decodeMove(const Position& pos, uint8_t code);
// Reverse (useful to write test books): -1 if the move has no code.
int encodeMove(const Position& pos, Move m);

class Book {
public:
    // `path`: any of the three files (.ctg, .cto or .ctb); the other two must be beside it.
    bool open(const std::string& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_ctg.is_open(); }
    const std::string& path() const { return m_path; } // .ctg file
    uint32_t pageCount() const { return m_pages; }
    // Number of positions stored in the file (reads every page).
    uint64_t positionCount();

    // Statistics of the position itself; false if it is not in the book.
    bool positionStats(const Position& pos, Stats& out);
    // Moves of the position, most played first.
    std::vector<BookMove> moves(const Position& pos);

private:
    struct Entry {
        std::vector<uint8_t> moves; // (move, annotation) pairs
        Stats stats;
    };
    bool findEntry(const Position& pos, Entry& out);
    bool readPage(uint32_t page, std::array<uint8_t, 4096>& buf);

    std::ifstream m_ctg;
    std::ifstream m_cto;
    std::string m_path;
    uint32_t m_low = 0, m_high = 0; // bounds of the slots used in the index
    uint32_t m_pages = 0;
};

} // namespace chess::ctg
