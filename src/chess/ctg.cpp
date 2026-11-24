#include "ctg.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace chess::ctg {

namespace {

#include "ctg_tables.inc"

uint32_t readBigEndian(const uint8_t* p, int bytes)
{
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
    return v;
}

// The book always sees the position from the side to move ("White"):
// board flipped if Black is to move, and mirrored (files) when the king of the side
// to move is on the queenside (files a to d) and no castling is possible any more.
struct View {
    bool flip = false;
    bool mirror = false;
};

View viewOf(const Position& pos)
{
    View v;
    v.flip = pos.sideToMove() == Black;
    const Square king = pos.kingSquare(pos.sideToMove());
    v.mirror = king != NoSquare && fileOf(king) < 4 && pos.castlingRights() == 0;
    return v;
}

Square realSquare(View v, int file, int rank)
{
    if (v.flip) rank = 7 - rank;
    if (v.mirror) file = 7 - file;
    return makeSquare(file, rank);
}

// Piece on square (file, rank) seen from the side to move: its pieces are "white".
Piece seenPiece(const Position& pos, View v, int file, int rank)
{
    const Piece p = pos.at(realSquare(v, file, rank));
    if (p == NoPiece || !v.flip) return p;
    return makePiece(~colorOf(p), typeOf(p));
}

// En passant counted only if a pawn of the side to move can play it.
bool epCapturable(const Position& pos)
{
    const Square ep = pos.epSquare();
    if (ep == NoSquare) return false;
    const Color us = pos.sideToMove();
    const int rank = rankOf(ep) + (us == White ? -1 : 1);
    if (rank < 0 || rank > 7) return false;
    for (int df : {-1, 1}) {
        const int f = fileOf(ep) + df;
        if (f >= 0 && f < 8 && pos.at(makeSquare(f, rank)) == makePiece(us, Pawn)) return true;
    }
    return false;
}

// Appends `count` bits of `bits` (least significant bit first), from the most significant bit of each byte.
void appendBits(Signature& sig, uint32_t bits, int& bitPos, int count)
{
    for (int i = 0; i < count; ++i, bits >>= 1, ++bitPos)
        if (bits & 1) sig.bytes[size_t(bitPos / 8)] |= uint8_t(1 << (7 - bitPos % 8));
}

PieceType pieceTypeOfCode(char c)
{
    switch (c) {
    case 'P': return Pawn;
    case 'N': return Knight;
    case 'B': return Bishop;
    case 'R': return Rook;
    case 'Q': return Queen;
    case 'K': return King;
    default: return NoType;
    }
}

constexpr uint8_t ShortCastle = 107, LongCastle = 246;
constexpr int PageSize = 4096;
constexpr int StatsSize = 33; // statistics bytes after the moves of an entry

} // namespace

std::string annotationSymbol(uint8_t a)
{
    switch (a) {
    case Good: return "!";
    case Mistake: return "?";
    case Excellent: return "!!";
    case Blunder: return "??";
    case Interesting: return "!?";
    case Dubious: return "?!";
    case OnlyMove: return "□";
    case Zugzwang: return "⨀";
    default: return {};
    }
}

double Stats::score() const
{
    const int n = wins + draws + losses;
    return n > 0 ? 100.0 * (wins + 0.5 * draws) / n : -1.0;
}

Signature signature(const Position& pos)
{
    Signature sig;
    const View v = viewOf(pos);
    int bitPos = 8; // byte 0: length and flags

    for (int file = 0; file < 8; ++file)
        for (int rank = 0; rank < 8; ++rank) {
            const Piece p = seenPiece(pos, v, file, rank);
            uint32_t bits = 0;
            int count = 1; // empty square: one 0 bit
            if (p != NoPiece) {
                const bool white = colorOf(p) == White;
                switch (typeOf(p)) {
                case Pawn: bits = white ? 0x03 : 0x07, count = 3; break;
                case Knight: bits = white ? 0x09 : 0x19, count = 5; break;
                case Bishop: bits = white ? 0x05 : 0x15, count = 5; break;
                case Rook: bits = white ? 0x0D : 0x1D, count = 5; break;
                case Queen: bits = white ? 0x11 : 0x31, count = 6; break;
                case King: bits = white ? 0x01 : 0x21, count = 6; break;
                default: break;
                }
            }
            appendBits(sig, bits, bitPos, count);
        }

    // Flags, aligned at the end of the last byte: castling rights (4 bits) then en passant file (3 bits).
    int ep = -1, flagBits = 0;
    if (epCapturable(pos)) {
        ep = fileOf(pos.epSquare());
        if (v.mirror) ep = 7 - ep;
        flagBits = 3;
    }
    const uint8_t rights = pos.castlingRights(); // 1 = K, 2 = Q, 4 = k, 8 = q
    bool usShort = rights & 1, usLong = rights & 2, themShort = rights & 4, themLong = rights & 8;
    if (v.flip) {
        std::swap(usShort, themShort);
        std::swap(usLong, themLong);
    }
    const int castle = (usShort ? 4 : 0) | (usLong ? 8 : 0) | (themShort ? 1 : 0) | (themLong ? 2 : 0);
    if (castle) flagBits += 4;
    uint32_t flags = uint32_t(castle);
    if (ep != -1) {
        flags <<= 3;
        for (int i = 0; i < 3; ++i, ep >>= 1)
            if (ep & 1) flags |= 1u << (2 - i);
    }
    if (8 - bitPos % 8 < flagBits) bitPos += 8 - bitPos % 8; // no room: next byte
    int pad = 8 - bitPos % 8 - flagBits;
    if (pad < 0) pad += 8;
    bitPos += pad;
    appendBits(sig, flags, bitPos, flagBits);

    sig.length = (bitPos + 7) / 8;
    sig.bytes[0] = uint8_t(sig.length);
    if (epCapturable(pos)) sig.bytes[0] |= 1 << 5;
    if (castle) sig.bytes[0] |= 1 << 6;
    return sig;
}

uint32_t hash(const Signature& sig)
{
    uint32_t h = 0;
    int16_t tmp = 0;
    for (int i = 0; i < sig.length; ++i) {
        const auto b = int8_t(sig.bytes[size_t(i)]);
        tmp = int16_t(tmp + ((0x0f - (b & 0x0f)) << 2) + 1);
        h += HashBits[tmp & 0x3f];
        tmp = int16_t(tmp + ((0xf0 - (b & 0xf0)) >> 2) + 1);
        h += HashBits[tmp & 0x3f];
    }
    return h;
}

Move decodeMove(const Position& pos, uint8_t code)
{
    const View v = viewOf(pos);
    const int homeRank = v.flip ? 7 : 0;
    if (code == ShortCastle || code == LongCastle) {
        const Move m{makeSquare(4, homeRank), makeSquare(code == ShortCastle ? 6 : 2, homeRank), NoType};
        return pos.isLegal(m) ? m : Move{};
    }
    const PieceType type = pieceTypeOfCode(PieceCode[code]);
    if (type == NoType) return {};

    // n-th piece of this type, file by file from a1 (seen from the side to move)
    const int nth = PieceIndex[code];
    int count = 0, fileFrom = -1, rankFrom = -1;
    for (int file = 0; file < 8 && fileFrom < 0; ++file)
        for (int rank = 0; rank < 8; ++rank)
            if (seenPiece(pos, v, file, rank) == makePiece(White, type) && ++count == nth) {
                fileFrom = file;
                rankFrom = rank;
                break;
            }
    if (fileFrom < 0) return {};

    const int fileTo = (fileFrom - Left[code] + 8) % 8;
    const int rankTo = (rankFrom + Forward[code] + 8) % 8;
    Move m{realSquare(v, fileFrom, rankFrom), realSquare(v, fileTo, rankTo), NoType};
    if (type == Pawn && (rankOf(m.to) == 0 || rankOf(m.to) == 7)) m.promo = Queen; // the only promotion encoded
    return pos.isLegal(m) ? m : Move{};
}

int encodeMove(const Position& pos, Move m)
{
    for (int code = 0; code < 256; ++code)
        if (decodeMove(pos, uint8_t(code)) == m) return code;
    return -1;
}

// ---------------------------------------------------------------- Book

bool Book::open(const std::string& path, std::string* error)
{
    close();
    std::string base = path;
    if (base.size() > 4) {
        std::string ext = base.substr(base.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".ctg" || ext == ".cto" || ext == ".ctb") base.resize(base.size() - 4);
    }
    // Extension in lower or upper case (books copied from Windows)
    auto openPart = [&](std::ifstream& f, const char* ext) {
        f.open(base + "." + ext, std::ios::binary);
        if (!f) {
            std::string upper = ext;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            f.clear();
            f.open(base + "." + upper, std::ios::binary);
        }
        return bool(f);
    };
    std::ifstream ctb;
    if (!openPart(m_ctg, "ctg") || !openPart(m_cto, "cto") || !openPart(ctb, "ctb")) {
        close();
        if (error) *error = "missing";
        return false;
    }
    uint8_t header[12];
    if (!ctb.read(reinterpret_cast<char*>(header), 12)) {
        close();
        if (error) *error = "invalid";
        return false;
    }
    m_low = readBigEndian(header + 4, 4);
    m_high = readBigEndian(header + 8, 4);
    m_ctg.seekg(0, std::ios::end);
    const auto size = static_cast<uint64_t>(m_ctg.tellg());
    m_pages = uint32_t(size / PageSize);
    if (m_low > m_high || m_pages < 2) {
        close();
        if (error) *error = "invalid";
        return false;
    }
    m_path = base + ".ctg";
    return true;
}

void Book::close()
{
    for (std::ifstream* f : {&m_ctg, &m_cto}) {
        if (f->is_open()) f->close();
        f->clear();
    }
    m_path.clear();
    m_low = m_high = m_pages = 0;
}

bool Book::readPage(uint32_t page, std::array<uint8_t, 4096>& buf)
{
    if (page + 1 >= m_pages) return false; // page 0 of the file is the header
    m_ctg.clear();
    m_ctg.seekg(std::streamoff(page + 1) * PageSize);
    return bool(m_ctg.read(reinterpret_cast<char*>(buf.data()), PageSize));
}

bool Book::findEntry(const Position& pos, Entry& out)
{
    if (!isOpen() || pos.isChess960()) return false; // the format only knows classical castling
    const Signature sig = signature(pos);
    const uint32_t h = hash(sig);

    // Index slot: (hash & mask) + mask, for increasing masks (1, 3, 7…).
    uint32_t key = 0;
    for (uint32_t mask = 1; key <= m_high && mask != 0xFFFFFFFFu; mask = (mask << 1) | 1) {
        key = (h & mask) + mask;
        if (key < m_low) continue;
        uint8_t raw[4];
        m_cto.clear();
        m_cto.seekg(16 + std::streamoff(key) * 4);
        if (!m_cto.read(reinterpret_cast<char*>(raw), 4)) return false;
        const auto page = int32_t(readBigEndian(raw, 4));
        if (page < 0) continue;

        std::array<uint8_t, 4096> buf;
        if (!readPage(uint32_t(page), buf)) return false;
        const int count = buf[0] << 8 | buf[1];
        int p = 4;
        for (int i = 0; i < count; ++i) {
            if (p >= PageSize) return false;
            const int sigLen = buf[size_t(p)] % 32;
            if (p + sigLen >= PageSize) return false;
            const int movesLen = buf[size_t(p + sigLen)];
            const int next = p + sigLen + movesLen + StatsSize;
            if (next > PageSize) return false;
            if (sigLen != sig.length || std::memcmp(&buf[size_t(p)], sig.bytes.data(), size_t(sigLen)) != 0) {
                p = next;
                continue;
            }
            const uint8_t* m = &buf[size_t(p + sigLen)];
            out.moves.assign(m + 1, m + movesLen);
            const uint8_t* s = m + movesLen;
            Stats& st = out.stats;
            st.games = int(readBigEndian(s, 3));
            st.losses = int(readBigEndian(s + 3, 3));
            st.wins = int(readBigEndian(s + 6, 3));
            st.draws = int(readBigEndian(s + 9, 3));
            const uint32_t eloGames = readBigEndian(s + 16, 3), eloSum = readBigEndian(s + 19, 4);
            const uint32_t perfGames = readBigEndian(s + 23, 3), perfSum = readBigEndian(s + 26, 4);
            st.averageElo = eloGames ? int(eloSum / eloGames) : 0;
            st.performance = perfGames ? int(perfSum / perfGames) : 0;
            st.recommendation = s[30];
            return true;
        }
        return false; // a single page per position
    }
    return false;
}

bool Book::positionStats(const Position& pos, Stats& out)
{
    Entry e;
    if (!findEntry(pos, e)) return false;
    out = e.stats;
    return true;
}

uint64_t Book::positionCount()
{
    uint64_t n = 0;
    std::array<uint8_t, 4096> buf;
    for (uint32_t page = 0; page + 1 < m_pages; ++page)
        if (readPage(page, buf)) n += uint64_t(buf[0] << 8 | buf[1]);
    return n;
}

std::vector<BookMove> Book::moves(const Position& pos)
{
    std::vector<BookMove> out;
    Entry e;
    if (!findEntry(pos, e)) return out;
    for (size_t i = 0; i + 1 < e.moves.size(); i += 2) {
        BookMove bm;
        bm.move = decodeMove(pos, e.moves[i]);
        if (bm.move.isNull()) continue;
        if (std::any_of(out.begin(), out.end(), [&](const BookMove& o) { return o.move == bm.move; })) continue;
        bm.annotation = e.moves[i + 1];
        Entry child;
        if (findEntry(pos.after(bm.move), child)) {
            bm.hasStats = true;
            bm.stats = child.stats; // already from the point of view of the side that just moved
        }
        out.push_back(bm);
    }
    std::stable_sort(out.begin(), out.end(), [](const BookMove& a, const BookMove& b) { return a.stats.games > b.stats.games; });
    return out;
}

} // namespace chess::ctg
