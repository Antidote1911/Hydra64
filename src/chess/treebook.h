#pragma once

#include "position.h"

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

// Hydra64 opening book (.hy64book): statistics tree converted from a
// text export of an Aquarium / Chess Assistant tree (script tools/aquarium/Hydra64_ExportTree.tsc)
// or from a ChessBase book (.ctg).
//
// File: 32-byte header then 32-byte entries (little-endian), sorted by the position's
// Polyglot key then by decreasing number of games — binary search
// in the file, without loading it. Entry: key (0–7), move (8–9), year (10–11), highest
// Elo (12–13), annotation (14), marks (15: bit 0 "statistics present" [version 2],
// 64 to avoid, 128 recommended), wins / draws / losses (16–27), average Elo (28–29),
// performance (30–31). Version 1: bytes 14–15 and 28–31 are zero (statistics always present).
// Mark 8 ("evaluation"): instead of the average Elo and the performance, the move's evaluation
// by an engine (28–29, int16, centipawns from the point of view of the side playing; mate ±(30000 − n))
// and depth (30–31) — tree of a deep analysis.
namespace chess::treebook {

struct BookMove {
    Move move;
    // From the point of view of the side playing the move
    uint32_t wins = 0, draws = 0, losses = 0;
    uint16_t maxElo = 0; // highest Elo of the players of the move (0: unknown)
    uint16_t year = 0;   // last year the move was played (0: unknown)
    // Coming from a ChessBase book (0 otherwise):
    uint8_t annotation = 0;     // chess::ctg::Annotation ("!", "?!"…)
    uint8_t recommendation = 0; // chess::ctg::Recommendation (64 to avoid, 128 recommended)
    uint16_t averageElo = 0, performance = 0;
    bool hasStats = true; // false: book move whose resulting position has no statistics
    uint64_t gameCount = 0; // number of games if it differs from wins + draws + losses (ChessBase)
    // Deep analysis: evaluation of the move (side playing it) and depth
    bool hasEval = false;
    int16_t eval = 0;
    uint16_t evalDepth = 0;
    uint64_t games() const { return gameCount ? gameCount : uint64_t(wins) + draws + losses; }
};

class Book {
public:
    bool open(const std::string& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_file.is_open(); }
    const std::string& path() const { return m_path; }
    uint64_t entryCount() const { return m_count; }

    // Moves of the position, most played first.
    std::vector<BookMove> moves(const Position& pos);

private:
    std::ifstream m_file;
    std::string m_path;
    uint64_t m_count = 0;
    uint32_t m_version = 0;
};

// Aquarium text export: one line per position,
//   EPD <TAB> move <TAB> move…   with move = uci|san|wins|draws|losses|elo|year|…
// The header is recognised on the first line.
bool looksLikeAquariumExport(const std::string& path);

struct ConvertResult {
    uint64_t positions = 0; // lines read (Aquarium), positions visited (ChessBase)
    uint64_t moves = 0;     // moves written
    uint64_t skipped = 0;   // unreadable / illegal lines or moves
    uint64_t inFile = 0;    // ChessBase: positions present in the file
};

// Converts the export into a .hy64book book (external sort: little memory even for
// hundreds of millions of moves). `progress(read, total)` is called regularly (bytes of the
// source file); if it returns false, the conversion is cancelled and nothing is written.
bool convertAquariumExport(const std::string& input, const std::string& output, ConvertResult* result = nullptr,
                           std::string* error = nullptr, const std::function<bool(uint64_t, uint64_t)>& progress = {});

// Target format of a tree conversion: Hydra64 (everything is kept) or Polyglot (open
// format: moves and weights only — weight = number of games, 0 for moves to avoid or
// annotated "?" / "??").
enum class TreeFormat { Hy64Book, Polyglot };

// Format of a tree file from its extension (and its content for .txt).
enum class TreeSource { Unknown, ChessBase, Aquarium, AquariumExport, Polyglot, Hy64Book };
TreeSource treeSourceOf(const std::string& path);

// Converts any readable tree (ChessBase .ctg, Aquarium / ChessOK .hsh/.elm or text
// export, Polyglot .bin, Hydra64 .hy64book) into the `target` format. Positions are
// visited from the initial position by following the moves (transpositions merged), in
// parallel, with an external sort (little memory). `progress(positions done, estimate)`.
bool convertTree(const std::string& input, const std::string& output, TreeFormat target, ConvertResult* result = nullptr,
                 std::string* error = nullptr, const std::function<bool(uint64_t, uint64_t)>& progress = {});

// Writes any tree: `reader(position)` gives the moves of a position (called from
// several threads), walked from `starts` by following the moves.
using MoveReader = std::function<std::vector<BookMove>(const Position&)>;
// `progress(positions done, estimate)`: false cancels (error "cancelled"); `estimate`: expected
// number of positions, if known.
bool writeTree(const MoveReader& reader, const std::vector<Position>& starts, const std::string& output, TreeFormat target,
               ConvertResult* result = nullptr, std::string* error = nullptr, const std::function<bool(uint64_t, uint64_t)>& progress = {},
               uint64_t estimate = 0);

// Writes a book from moves given in any order, one call per position and move (external
// sort: little memory). Not thread-safe. Temporary files next to `output`, removed at the end.
class BookWriter {
public:
    BookWriter(std::string output, TreeFormat format);
    ~BookWriter();
    BookWriter(const BookWriter&) = delete;
    BookWriter& operator=(const BookWriter&) = delete;
    // `key`: Polyglot key of the position; `castling`: the move is a castling move in it.
    bool add(uint64_t key, const BookMove& move, bool castling);
    bool finish(uint64_t* written = nullptr); // false: write failure
private:
    struct Private;
    Private* d;
};

// Converts a ChessBase book (.ctg/.cto/.ctb) to .hy64book, with everything it
// contains: moves, annotations, marks (recommended / to avoid), statistics (games, wins,
// draws, losses, average Elo, performance). Positions are visited from the initial
// position by following the book's moves (transpositions merged), in parallel.
// `progress(positions visited, positions in the file)`.
bool convertCtg(const std::string& ctgPath, const std::string& output, ConvertResult* result = nullptr, std::string* error = nullptr,
                const std::function<bool(uint64_t, uint64_t)>& progress = {});

} // namespace chess::treebook
