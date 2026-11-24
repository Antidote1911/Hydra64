#include "aquarium.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace chess::aquarium {

namespace {

#include "aquarium_codes.inc"

// CPS code tables (32-bit entries: code length << 27 | code), rebuilt once.
const std::vector<uint32_t>& codeTable()
{
    static const std::vector<uint32_t> table = [] {
        std::vector<uint32_t> t(CodeTableEntries, 0);
        size_t k = 0;
        for (const auto& seg : CodeSegments)
            for (uint32_t i = 0; i < seg[1]; ++i) t[seg[0] + i] = CodeValues[k++];
        return t;
    }();
    return table;
}
constexpr size_t HshHeader = 136;       // .hsh entries from this byte on
constexpr uint32_t ElmMaskOffset = 0x20; // mask of the record fields

// ---------------------------------------------------------------- bits

struct BitWriter {
    std::vector<uint8_t> bits; // one bit per element
    void put(uint32_t value, int n)
    {
        for (int i = 0; i < n; ++i) bits.push_back(uint8_t((value >> i) & 1));
    }
};

struct BitReader {
    const std::vector<uint8_t>& data;
    size_t pos = 0;
    bool overflow = false;
    uint32_t read(int n)
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i, ++pos) {
            if (pos / 8 >= data.size()) {
                overflow = true;
                return v;
            }
            v |= uint32_t((data[pos / 8] >> (pos % 8)) & 1) << i;
        }
        return v;
    }
    void align() { pos = (pos + 7) / 8 * 8; }
};

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// ---------------------------------------------------------------- CPS key

int sq88(Square s) { return rankOf(s) * 16 + fileOf(s); }

// Pair of squares (0x88): the larger in the low bits, the other shifted by 6 bits.
uint32_t pairIndex(const std::vector<int>& squares)
{
    const int a = squares.size() > 0 ? squares[0] : 0, c = squares.size() > 1 ? squares[1] : 0;
    auto idx = [](int s) { return uint32_t((s >> 4) * 8 + (s & 7)); };
    return a > c ? idx(a) | idx(c) << 6 : idx(c) | idx(a) << 6;
}

int popcount6(int m)
{
    int n = 0;
    for (int i = 0; i < 6; ++i) n += (m >> i) & 1;
    return n;
}

// Pawns of a file (mask of ranks 2 to 7) → type (0 to 3) and index within the type.
int pawnType(int mask) { return std::min(popcount6(mask), 3); }
int pawnKey(int mask)
{
    const int n = popcount6(mask);
    if (n == 1) {
        for (int i = 0; i < 6; ++i)
            if (mask >> i & 1) return i;
    }
    if (n == 2) {
        int lo = -1, hi = -1;
        for (int i = 0; i < 6; ++i)
            if (mask >> i & 1) (lo < 0 ? lo : hi) = i;
        return hi * (hi - 1) / 2 + lo;
    }
    return mask; // three pawns or more: raw 6-bit mask
}

// Code of a 0x88 square of the king-and-rooks table: [number of rooks << 2 | castling rights].
constexpr uint8_t RookCode[12] = {0, 2, 3, 7, 1, 5, 6, 8, 4, 8, 8, 8};

// ---------------------------------------------------------------- Aquarium's generator

// Directions: knight (9-16), bishop (1-4), rook (5-8), queen and king (1-8).
constexpr int Delta[17] = {0, 17, 15, -17, -15, 16, -16, 1, -1, -14, 14, -18, 18, -31, 31, -33, 33};
struct Dirs { int first, last; bool slide; };
constexpr Dirs PieceDirs[7] = {{0, 0, false}, {0, 0, false}, {9, 16, false}, {1, 4, true}, {5, 8, true}, {1, 8, true}, {1, 8, false}};
// Slots per pawn by [rank * 2 + side] (4: starting rank, 12: before promotion).
constexpr int PawnSlots[16] = {0, 0, 4, 12, 3, 3, 3, 3, 3, 3, 3, 3, 12, 4, 0, 0};
// Predefined statistics (wins, draws, losses).
constexpr std::array<uint32_t, 3> Preset[15] = {{0, 0, 0}, {0, 2, 1}, {2, 0, 0}, {1, 2, 1}, {1, 1, 0}, {2, 1, 0}, {0, 2, 0}, {1, 2, 0},
                                                 {2, 2, 0}, {2, 2, 1}, {1, 0, 1}, {2, 0, 1}, {0, 1, 1}, {1, 1, 1}, {2, 1, 1}};

int ceilLog2(int n)
{
    int b = 0;
    while ((1 << b) < n) ++b;
    return b;
}

struct Candidate { int from, to; PieceType promo; };

// Aquarium's candidate (pseudo-legal) moves for a piece type, in its order.
std::vector<Candidate> candidates(const Position& pos, int type)
{
    std::array<int, 128> board{}; // 0: empty; type | 0x10 if black
    for (Square s = 0; s < 64; ++s)
        if (const Piece p = pos.at(s); p != NoPiece) board[size_t(sq88(s))] = typeOf(p) | (colorOf(p) == Black ? 0x10 : 0);
    const int side = pos.sideToMove() == White ? 0 : 1;
    std::vector<int> squares;
    for (int s = 0; s < 128; ++s)
        if (!(s & 0x88) && board[size_t(s)] && (board[size_t(s)] >> 4) == side && (board[size_t(s)] & 15) == type) squares.push_back(s);

    std::vector<Candidate> out;
    if (type == Pawn) {
        const int fwd = side == 0 ? 16 : -16;
        for (int s : squares) {
            const int n = PawnSlots[(s >> 4) * 2 + side];
            for (int local = 0; local < n; ++local) {
                PieceType promo = NoType;
                int l = local;
                if (n == 12) {
                    static constexpr PieceType promos[4] = {Queen, Rook, Bishop, Knight};
                    promo = promos[local & 3];
                    l = local >> 2;
                }
                const int d = l == 0 ? fwd : l == 1 ? fwd + 1 : l == 2 ? fwd - 1 : fwd * 2;
                out.push_back({s, s + d, promo});
            }
        }
        return out;
    }
    const int enemy = (1 - side) << 4;
    const Dirs dirs = PieceDirs[type];
    for (int s : squares) {
        if (type == King && (s == 0x04 || s == 0x74)) { // castling moves, always offered first
            out.push_back({s, s - 2, NoType});
            out.push_back({s, s + 2, NoType});
        }
        for (int di = dirs.first; di <= dirs.last; ++di) {
            int t = s;
            for (;;) {
                t += Delta[di];
                if (t & 0x88) break;
                if (board[size_t(t)]) {
                    if ((board[size_t(t)] & 0x10) == enemy) out.push_back({s, t, NoType});
                    break;
                }
                out.push_back({s, t, NoType});
                if (!dirs.slide) break;
            }
        }
    }
    return out;
}

int readPieceType(BitReader& r)
{
    const uint32_t v = r.read(2);
    if (v < 3) return int(v) + 1; // pawn, knight, bishop
    if (r.read(1) == 0) return Queen;
    return r.read(1) == 0 ? Rook : King;
}

uint32_t readCount(BitReader& r)
{
    const uint32_t v = r.read(2);
    if (v != 3) return v;
    const uint32_t s = r.read(4);
    if (s == 0) return r.read(8) + 16;
    if (s == 1) return r.read(16);
    if (s == 2) return r.read(24);
    return s;
}

std::array<uint32_t, 3> readStats(BitReader& r)
{
    switch (r.read(2)) {
    case 1: return {0, 1, 0};
    case 2: return {1, 0, 0};
    case 3: return {0, 0, 1};
    default: {
        const uint32_t i = r.read(4);
        if (i == 15) {
            const uint32_t w = readCount(r), d = readCount(r), l = readCount(r);
            return {w, d, l};
        }
        return Preset[i];
    }
    }
}

} // namespace

// ---------------------------------------------------------------- Book

bool Book::open(const std::string& treePath, std::string* error)
{
    close();
    auto fail = [&](const char* why) {
        close();
        if (error) *error = why;
        return false;
    };
    std::string base = treePath;
    if (base.size() > 4) {
        std::string ext = base.substr(base.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".hsh" || ext == ".elm") base.resize(base.size() - 4);
    }
    auto openPart = [&](std::ifstream& f, const char* ext) {
        for (const std::string& e : {std::string(ext), [&] { std::string u = ext; for (char& c : u) c = char(std::toupper((unsigned char)c)); return u; }()}) {
            f.clear();
            f.open(base + "." + e, std::ios::binary);
            if (f) return true;
        }
        return false;
    };
    std::ifstream hsh;
    if (!openPart(hsh, "hsh") || !openPart(m_elm, "elm")) return fail("missing");
    std::vector<uint8_t> h((std::istreambuf_iterator<char>(hsh)), {});
    if (h.size() < HshHeader + 8 || std::memcmp(&h[24], "KPAL0005", 8) != 0) return fail("invalid");
    for (size_t o = HshHeader; o + 8 <= h.size(); o += 8) {
        Entry e;
        e.offset = le32(&h[o]);
        std::memcpy(e.b, &h[o + 4], 4);
        if (!e.isNode()) ++m_blocks;
        m_entries.push_back(e);
    }
    uint8_t header[0x24];
    if (!m_elm.read(reinterpret_cast<char*>(header), sizeof header) || std::memcmp(&header[24], "KPAL0005", 8) != 0) return fail("invalid");
    m_mask = le32(&header[ElmMaskOffset]);
    if (!(m_mask & 0x3aa0c1)) return fail("invalid"); // tree without moves
    m_path = base + ".hsh";
    return true;
}

void Book::close()
{
    if (m_elm.is_open()) m_elm.close();
    m_elm.clear();
    m_entries.clear();
    m_path.clear();
    m_mask = 0;
    m_blocks = 0;
}

std::vector<uint8_t> Book::keyBits(const Position& pos) const
{
    const std::vector<uint32_t>& table = codeTable();
    // Pieces of the position (0x88 squares)
    std::array<int, 8> wPawns{}, bPawns{};
    std::vector<int> wN, bN, wR, bR, wQ, bQ;
    int wK = 0, bK = 0, wbL = 0, wbD = 0, bbL = 0, bbD = 0, wbf = 0, bbf = 0, wcf = 0, bcf = 0;
    const uint8_t rights = pos.castlingRights(); // 1 = K, 2 = Q, 4 = k, 8 = q
    for (Square s = 0; s < 64; ++s) {
        const Piece p = pos.at(s);
        if (p == NoPiece) continue;
        const int q = sq88(s), f = fileOf(s), r = rankOf(s);
        const bool white = colorOf(p) == White;
        switch (typeOf(p)) {
        case Pawn:
            if (r < 1 || r > 6) return {};
            (white ? wPawns : bPawns)[size_t(f)] |= 1 << (r - 1);
            break;
        case Knight: (white ? wN : bN).push_back(q); break;
        case Bishop: {
            const bool light = (f + r) & 1;
            int& flags = white ? wbf : bbf;
            const int bit = light ? 1 : 2;
            if (flags & bit) return {}; // two bishops of the same colour: cannot be represented
            flags |= bit;
            (white ? (light ? wbL : wbD) : (light ? bbL : bbD)) = q;
            break;
        }
        case Rook:
            if (white && q == 0x07 && (rights & 1)) wcf |= 1;
            else if (white && q == 0x00 && (rights & 2)) wcf |= 2;
            else if (!white && q == 0x77 && (rights & 4)) bcf |= 1;
            else if (!white && q == 0x70 && (rights & 8)) bcf |= 2;
            else (white ? wR : bR).push_back(q);
            break;
        case Queen: (white ? wQ : bQ).push_back(q); break;
        case King: (white ? wK : bK) = q; break;
        default: break;
        }
    }
    for (const auto* v : {&wN, &bN, &wR, &bR, &wQ, &bQ})
        if (v->size() > 2) return {};

    BitWriter w;
    bool ok = true;
    auto code = [&](uint32_t off, uint32_t idx) {
        const size_t o = size_t(off - 8) / 4 + idx;
        const uint32_t v = o < table.size() ? table[o] : 0;
        if (v == 0) ok = false;
        else w.put(v & 0x7ffffff, int(v >> 27));
    };
    w.put(1, 1); // leading bit

    uint32_t wStr = 0, bStr = 0;
    for (int f = 7; f >= 0; --f) {
        wStr = wStr << 2 | uint32_t(pawnType(wPawns[size_t(f)]));
        bStr = bStr << 2 | uint32_t(pawnType(bPawns[size_t(f)]));
    }
    code(0x8, wStr);
    code(0x40008, bStr);
    const uint32_t wrc = RookCode[wR.size() << 2 | size_t(wcf)], brc = RookCode[bR.size() << 2 | size_t(bcf)];
    const uint32_t major = ((((wrc << 2) | uint32_t(wQ.size())) << 3 | brc) << 2) | uint32_t(bQ.size());
    const uint32_t minor = ((((uint32_t(wN.size()) << 2) | uint32_t(wbf)) << 2 | uint32_t(bN.size())) << 2) | uint32_t(bbf);
    code(0x80988, major);
    code(0x80588, minor);
    for (int f = 0; f < 8; ++f) {
        const int wm = wPawns[size_t(f)], bm = bPawns[size_t(f)];
        switch (pawnType(wm)) {
        case 1: code(0x80008 + uint32_t(f) * 24, uint32_t(pawnKey(wm))); break;
        case 2: code(0x80188 + uint32_t(f) * 64, uint32_t(pawnKey(wm))); break;
        case 3: w.put(uint32_t(pawnKey(wm)), 6); break;
        default: break;
        }
        switch (pawnType(bm)) {
        case 1: code(0x800c8 + uint32_t(f) * 24, uint32_t(pawnKey(bm))); break;
        case 2: code(0x80388 + uint32_t(f) * 64, uint32_t(pawnKey(bm))); break;
        case 3: w.put(uint32_t(pawnKey(bm)), 6); break;
        default: break;
        }
    }
    if (wQ.size() == 1) code(0x94588, pairIndex(wQ));
    else if (wQ.size() == 2) code(0x94788, pairIndex(wQ));
    if (bQ.size() == 1) code(0x94688, pairIndex(bQ));
    else if (bQ.size() == 2) code(0x98788, pairIndex(bQ));

    auto idx64 = [](int q) { return uint32_t((q >> 4) * 8 + (q & 7)); };
    switch (wrc) {
    case 0: code(0x81988, idx64(wK)); break;
    case 1: code(0x81988, idx64(wK)); code(0x8bf88, pairIndex(wR)); break;
    case 4: code(0x81988, idx64(wK)); code(0x8c588, pairIndex(wR)); break;
    case 5: code(0x8c088, pairIndex(wR)); break;
    case 6: code(0x8c188, pairIndex(wR)); break;
    case 2: case 3: case 7: break; // king and castling rooks on their squares: nothing to write
    default: return {};
    }
    switch (brc) {
    case 0: code(0x81a88, idx64(bK)); break;
    case 1: code(0x81a88, idx64(bK)); code(0x8c288, pairIndex(bR)); break;
    case 4: code(0x81a88, idx64(bK)); code(0x90588, pairIndex(bR)); break;
    case 5: code(0x8c388, pairIndex(bR)); break;
    case 6: code(0x8c488, pairIndex(bR)); break;
    case 2: case 3: case 7: break;
    default: return {};
    }
    auto bish = [](int q) { return uint32_t(((q >> 4) * 8 + (q & 7)) >> 1); };
    const uint32_t wb = bish(wbL) | bish(wbD) << 5, bb = bish(bbL) | bish(bbD) << 5;
    if (wbf == 1) code(0x89d88, wb);
    else if (wbf == 2) code(0x89e88, wb >> 5);
    else if (wbf == 3) code(0x89f88, wb);
    if (bbf == 1) code(0x89e08, bb);
    else if (bbf == 2) code(0x89f08, bb >> 5);
    else if (bbf == 3) code(0x8af88, bb);
    if (wN.size() == 1) code(0x81b88, pairIndex(wN));
    else if (wN.size() == 2) code(0x81d88, pairIndex(wN));
    if (bN.size() == 1) code(0x81c88, pairIndex(bN));
    else if (bN.size() == 2) code(0x85d88, pairIndex(bN));

    // En passant: only if a pawn of the side to move can play it.
    int epFile = 8;
    if (const Square ep = pos.epSquare(); ep != NoSquare) {
        const Color us = pos.sideToMove();
        const int r = rankOf(ep) + (us == White ? -1 : 1);
        for (int df : {-1, 1}) {
            const int f = fileOf(ep) + df;
            if (f >= 0 && f < 8 && r >= 0 && r < 8 && pos.at(makeSquare(f, r)) == makePiece(us, Pawn)) epFile = fileOf(ep);
        }
    }
    if (epFile == 8) w.put(0, 1);
    else w.put(uint32_t(epFile * 2 | 1), 4);
    w.put(pos.sideToMove() == White ? 0 : 1, 1);
    if (!ok) return {};
    return w.bits;
}

bool Book::findRecord(const std::vector<uint8_t>& key, std::vector<uint8_t>& record)
{
    if (m_entries.empty()) return false;
    auto keyByte = [&](size_t k) {
        uint32_t v = 0;
        for (size_t i = 0; i < 8; ++i)
            if (8 * k + i < key.size()) v |= uint32_t(key[8 * k + i]) << i;
        return uint8_t(v);
    };
    // Index walk: the root is the last entry.
    size_t node = m_entries.size() - 1, consumed = 0;
    for (int guard = 0; guard < 64; ++guard) {
        const Entry& n = m_entries[node];
        const size_t group = (size_t(n.offset) - 8) / 8, total = size_t(n.b[3]) + 1, ranges = n.b[2];
        if (group + total > m_entries.size() || ranges > total) return false;
        const uint8_t byte = keyByte(consumed);
        size_t chosen = SIZE_MAX;
        for (size_t i = group; i < group + total - ranges; ++i)
            if (m_entries[i].b[0] == byte) {
                chosen = i;
                break;
            }
        size_t next = consumed;
        if (chosen != SIZE_MAX) next = consumed + 1;
        else {
            for (size_t i = group + total - ranges; i < group + total; ++i)
                if (m_entries[i].b[0] >= byte) {
                    chosen = i;
                    break;
                }
            if (chosen == SIZE_MAX) chosen = group + total - 1;
        }
        const Entry& e = m_entries[chosen];
        consumed = next;
        if (e.isNode()) {
            node = chosen;
            continue;
        }
        // .elm block
        const uint32_t size = ((uint32_t(e.b[1] & 0x3f) << 4) | (e.b[3] >> 4)) * 64;
        if (size == 0 || size > (1u << 20)) return false;
        std::vector<uint8_t> block(size);
        m_elm.clear();
        m_elm.seekg(std::streamoff(e.offset));
        if (!m_elm.read(reinterpret_cast<char*>(block.data()), std::streamsize(size))) return false;
        const size_t count = block[0], pages = (size + 255) / 256;
        if (2 * count + 2 + pages > size) return false;
        std::vector<uint32_t> offs;
        size_t j = 0;
        for (size_t p = 0; p < pages; ++p)
            for (int c = 0; c < block[2 * count + 2 + p] && j <= count; ++c) offs.push_back(uint32_t(p * 256 + block[count + j++]));
        if (offs.size() != count + 1) return false;
        const size_t remBits = key.size() - 8 * consumed;
        for (size_t r = 0; r < count; ++r) {
            const uint32_t a = offs[r], z = offs[r + 1];
            if (z <= a || z > size || (z - a) * 8 < remBits) continue;
            bool match = true;
            for (size_t i = 0; i < remBits && match; ++i)
                match = ((block[a + i / 8] >> (i % 8)) & 1) == key[8 * consumed + i];
            if (match) {
                record.assign(block.begin() + a, block.begin() + z);
                // start of the body: the byte following the end of the key
                record.insert(record.begin(), uint8_t((remBits + 7) / 8));
                return true;
            }
        }
        return false;
    }
    return false;
}

std::vector<BookMove> Book::moves(const Position& pos)
{
    std::vector<BookMove> out;
    if (pos.isChess960()) return out; // the format's position key assumes classical castling
    if (!isOpen()) return out;
    const std::vector<uint8_t> key = keyBits(pos);
    std::vector<uint8_t> raw;
    if (key.empty() || !findRecord(key, raw)) return out;
    const size_t bodyStart = raw[0];
    const std::vector<uint8_t> rec(raw.begin() + 1, raw.end());

    BitReader r{rec, bodyStart * 8};
    const uint32_t flags = r.read(3);
    if (m_mask & 0x100) { // main index (not needed here)
        const uint32_t v = r.read(9);
        if ((m_mask & 0x200) && v < 500) r.read(1);
    }
    const uint32_t count = (flags & 4) ? r.read(8) : r.read(2);
    std::vector<BookMove> list;
    for (uint32_t i = 0; i < count && !r.overflow; ++i) {
        const int type = readPieceType(r);
        const std::vector<Candidate> cand = candidates(pos, type);
        const uint32_t idx = r.read(ceilLog2(int(cand.size())));
        BookMove bm;
        if (idx < cand.size()) {
            const Candidate& c = cand[idx];
            bm.move = {makeSquare(c.from & 7, c.from >> 4), makeSquare(c.to & 7, c.to >> 4), c.promo};
        }
        list.push_back(bm);
    }
    if (r.overflow) return out;
    if (hasStatistics()) {
        for (BookMove& bm : list) {
            const auto s = readStats(r);
            bm.hasStats = true;
            bm.wins = s[0];
            bm.draws = s[1];
            bm.losses = s[2];
        }
        if (r.overflow) return out;
    }
    // Elo (one byte per move) then years (position then moves), at the end of the record.
    const size_t n = list.size();
    size_t end = rec.size();
    // Padding zero bytes after the years (no year equals 1830)
    if (hasYears())
        while (end > 0 && rec[end - 1] == 0) --end;
    if (hasYears() && end >= n + 1) {
        for (size_t i = 0; i < n; ++i)
            if (const uint8_t y = rec[end - n + i]; y != 0xff) list[i].year = uint16_t(1830 + y);
        end -= n + 1;
    }
    if (hasRatings() && end >= n) {
        for (size_t i = 0; i < n; ++i)
            if (const uint8_t e = rec[end - n + i]; e != 0xff) list[i].maxElo = uint16_t(1900 + 5 * e);
    }
    for (const BookMove& bm : list)
        if (!bm.move.isNull() && pos.isLegal(bm.move)) out.push_back(bm);
    if (hasStatistics())
        std::stable_sort(out.begin(), out.end(), [](const BookMove& a, const BookMove& b) { return a.games() > b.games(); });
    return out;
}

} // namespace chess::aquarium
