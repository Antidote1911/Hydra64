#pragma once

// Hydra64 game database format — see docs/format-hy64db.md.
//
//   <base>.hy64i  index: 64-byte header, then 64 bytes per game (direct access id × 64)
//   <base>.hy64n  names: 64-byte header, then an append-only journal (varint(len·8+type), UTF-8)
//   <base>.hy64g  games: 64-byte header, then one block per game (appended at the end)
//   <base>.hy64p  positions: recomputable index of the Polyglot keys of the first D half-moves
//
//   <base>.hy64   the four files above in a single one, opened as it is: 128-byte header
//                (magic, version, number of sections, offset and size of each section, start
//                and end of the journal), sections i, n, g, p aligned on 4096, then an append-only
//                journal for what is written afterwards: blocks [type 'I'/'N'/'G'][u32 size]
//                [content] (index row, names, game block). Compacting the database, or rebuilding
//                its position index, moves the journal into the sections.
//
// Everything is little-endian.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hy64db {

constexpr uint32_t FormatVersion = 1;
constexpr size_t HeaderSize = 64;
constexpr size_t RecordSize = 64;
constexpr uint32_t NoName = 0xFFFFFFFFu; // missing name field

constexpr char IndexMagic[8] = {'B', '6', '4', 'D', 'B', '-', 'I', 0};
constexpr char NamesMagic[8] = {'B', '6', '4', 'D', 'B', '-', 'N', 0};
constexpr char GamesMagic[8] = {'B', '6', '4', 'D', 'B', '-', 'G', 0};
constexpr char PositionsMagic[8] = {'B', '6', '4', 'D', 'B', '-', 'P', 0};
constexpr char PackMagic[8] = {'B', '6', '4', 'D', 'B', '-', '1', 0}; // single-file database
constexpr size_t PackHeaderSize = 128;
constexpr size_t PackAlign = 4096;
constexpr size_t PackJournalStart = 80; // u64: start of the journal (0: none), then u64: validated end
enum PackSection { PackIndex = 0, PackNames = 1, PackGames = 2, PackPositions = 3, PackSectionCount = 4 };

// Name types (.hy64n). Each type has its own sequence of numbers 0, 1, 2…
enum NameType : uint8_t { Player = 0, Event = 1, Site = 2, Round = 3, Annotator = 4, Source = 5, NameTypeCount = 6 };

// Result (8-bit field)
enum Result : uint8_t { ResultNone = 0, WhiteWins = 1, Draw = 2, BlackWins = 3 };

// Flags (16 bits)
enum Flag : uint16_t {
    FlagDeleted = 1 << 0,     // marked deleted (disappears on compaction)
    FlagStartFen = 1 << 1,    // special starting position
    FlagVariations = 1 << 2,  // contains variations
    FlagComments = 1 << 3,    // contains comments
    FlagNags = 1 << 4,        // contains NAGs
    FlagPromotion = 1 << 5,   // a promotion in the main line (material filter)
    FlagBeautyRated = 1 << 6, // the beauty mark (bits 7–8, 0…3) has been computed: db/beauty.h
    FlagUser1 = 1 << 10,      // 6 free flags for the user (10…15)
};
constexpr int BeautyShift = 7;
constexpr uint16_t BeautyMask = FlagBeautyRated | (3 << BeautyShift);
constexpr int UserFlagShift = 10;
constexpr int UserFlagCount = 6; // opening, middlegame, endgame, tactics, strategy, to review

// One index row (64 bytes). Layout (bytes):
//   0 u64 offset (40 bits) | size (24 bits) in .hy64g
//   8 u32 white   12 u32 black   16 u32 event   20 u32 site   24 u32 round   28 u32 annotator
//  32 u32 date (year·512 + month·32 + day; 0 = unknown)
//  36 u16 white Elo  38 u16 black Elo  40 u16 ECO (0 none, 1 = A00 … 500 = E99)  42 u16 half-moves
//  44 u8 result  45 u8 variant (0 standard chess)  46 u16 flags
//  48 u32 final material  52 u32 source  56 u64 signature of the starting pawns
struct Record {
    uint64_t offset = 0;
    uint32_t size = 0;
    uint32_t white = NoName, black = NoName, event = NoName, site = NoName, round = NoName, annotator = NoName;
    uint32_t date = 0;
    uint16_t whiteElo = 0, blackElo = 0, eco = 0, plies = 0;
    uint8_t result = ResultNone, variant = 0;
    uint16_t flags = 0;
    uint32_t material = 0;
    uint32_t source = NoName;
    uint64_t pawns = 0;

    bool deleted() const { return flags & FlagDeleted; }
    void write(uint8_t* out) const;
    static Record read(const uint8_t* in);
};

// ---------------------------------------------------------------- utilities

inline void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }
inline void put32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline void put64(uint8_t* p, uint64_t v)
{
    for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i));
}
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | uint64_t(get32(p + 4)) << 32; }

inline void putVarint(std::vector<uint8_t>& out, uint64_t v)
{
    while (v >= 0x80) {
        out.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    out.push_back(uint8_t(v));
}
// Reads a varint; false if the buffer ends before it does.
inline bool getVarint(const uint8_t*& p, const uint8_t* end, uint64_t& v)
{
    v = 0;
    for (int shift = 0; shift < 64 && p < end; shift += 7) {
        const uint8_t b = *p++;
        v |= uint64_t(b & 0x7f) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}

// PGN date "YYYY.MM.DD" (with "?") ↔ field; format(parse(x)) == x for well-formed dates.
uint32_t parseDate(const std::string& pgnDate);
std::string formatDate(uint32_t date);
inline int dateYear(uint32_t d) { return int(d >> 9); }
// ECO "B90" ↔ 1…500; 0 if missing or non-standard.
uint16_t parseEco(const std::string& eco);
std::string formatEco(uint16_t eco);
uint8_t parseResult(const std::string& result);
const char* formatResult(uint8_t result);

// File header (64 bytes): magic, version, counter.
void writeHeader(uint8_t* out, const char magic[8], uint64_t count, uint64_t extra = 0);
bool checkHeader(const uint8_t* in, size_t size, const char magic[8], std::string* error);

} // namespace hy64db
