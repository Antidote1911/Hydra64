#include "polyglot.h"

#include <cstdlib>

#include <algorithm>
#include <array>

namespace chess::polyglot {

namespace {

constexpr uint64_t Random64[781] = {
#include "polyglot_random.inc"
};

constexpr int RandomCastle = 768;
constexpr int RandomEnPassant = 772;
constexpr int RandomTurn = 780;

// Polyglot pieces: black pawn 0, white pawn 1, black knight 2… white king 11.
int pieceKind(Piece p) { return (typeOf(p) - 1) * 2 + (colorOf(p) == White ? 1 : 0); }

uint64_t readBigEndian(const unsigned char* p, int bytes)
{
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
    return v;
}

} // namespace

uint64_t key(const Position& pos)
{
    uint64_t k = 0;
    for (Square s = 0; s < 64; ++s)
        if (const Piece p = pos.at(s); p != NoPiece) k ^= Random64[64 * pieceKind(p) + s];

    const uint8_t castling = pos.castlingRights(); // 1 = K, 2 = Q, 4 = k, 8 = q: same order as Polyglot
    for (int i = 0; i < 4; ++i)
        if (castling & (1 << i)) k ^= Random64[RandomCastle + i];

    // En passant: only if a pawn of the side to move can play it.
    if (const Square ep = pos.epSquare(); ep != NoSquare) {
        const Color us = pos.sideToMove();
        const int pawnRank = rankOf(ep) + (us == White ? -1 : 1);
        bool capturable = false;
        for (int df : {-1, 1}) {
            const int f = fileOf(ep) + df;
            if (f >= 0 && f < 8 && pawnRank >= 0 && pawnRank < 8 && pos.at(makeSquare(f, pawnRank)) == makePiece(us, Pawn))
                capturable = true;
        }
        if (capturable) k ^= Random64[RandomEnPassant + fileOf(ep)];
    }

    if (pos.sideToMove() == White) k ^= Random64[RandomTurn];
    return k;
}

Move decodeMove(const Position& pos, uint16_t raw)
{
    Move m;
    m.to = makeSquare(raw & 7, (raw >> 3) & 7);
    m.from = makeSquare((raw >> 6) & 7, (raw >> 9) & 7);
    static constexpr PieceType promos[] = {NoType, Knight, Bishop, Rook, Queen};
    const int promo = (raw >> 12) & 7;
    m.promo = promo <= 4 ? promos[promo] : NoType;

    // Castling: the king "captures" its rook.
    const Piece p = pos.at(m.from);
    if (typeOf(p) == King && fileOf(m.from) == 4 && rankOf(m.from) == rankOf(m.to) && !pos.isChess960()) {
        if (fileOf(m.to) == 7) m.to = makeSquare(6, rankOf(m.to));
        else if (fileOf(m.to) == 0) m.to = makeSquare(2, rankOf(m.to));
    }
    return pos.isLegal(m) ? m : Move{};
}

uint16_t encodeMove(const Position& pos, Move m)
{
    Square to = m.to;
    if (pos.isCastling(m)) // castling: the king onto its rook's square
        to = pos.castlingRook(pos.sideToMove(), pos.isKingsideCastling(m));
    static constexpr int promo[] = {0, 0, 1, 2, 3, 4, 0}; // NoType, Pawn, Knight, Bishop, Rook, Queen, King
    return uint16_t(fileOf(to) | rankOf(to) << 3 | fileOf(m.from) << 6 | rankOf(m.from) << 9 | promo[m.promo] << 12);
}

bool Book::open(const std::string& path, std::string* error)
{
    close();
    m_file.open(path, std::ios::binary);
    if (!m_file) {
        if (error) *error = "fichier illisible";
        return false;
    }
    m_file.seekg(0, std::ios::end);
    const auto size = static_cast<uint64_t>(m_file.tellg());
    if (size == 0 || size % 16 != 0) {
        close();
        if (error) *error = "ce n'est pas une bibliothèque Polyglot (.bin)";
        return false;
    }
    m_count = size / 16;
    m_path = path;
    return true;
}

void Book::close()
{
    if (m_file.is_open()) m_file.close();
    m_file.clear();
    m_path.clear();
    m_count = 0;
}

bool Book::readEntry(uint64_t index, uint64_t& key, uint16_t& move, uint16_t& weight, uint32_t& learn)
{
    std::array<unsigned char, 16> b{};
    m_file.clear();
    m_file.seekg(static_cast<std::streamoff>(index * 16));
    if (!m_file.read(reinterpret_cast<char*>(b.data()), 16)) return false;
    key = readBigEndian(&b[0], 8);
    move = uint16_t(readBigEndian(&b[8], 2));
    weight = uint16_t(readBigEndian(&b[10], 2));
    learn = uint32_t(readBigEndian(&b[12], 4));
    return true;
}

std::vector<BookMove> Book::moves(const Position& pos)
{
    std::vector<BookMove> out;
    if (!isOpen()) return out;
    const uint64_t k = key(pos);

    // First entry with key >= k (binary search in the file).
    uint64_t lo = 0, hi = m_count;
    uint64_t ek;
    uint16_t raw, weight;
    uint32_t learn;
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        if (!readEntry(mid, ek, raw, weight, learn)) return out;
        if (ek < k) lo = mid + 1;
        else hi = mid;
    }
    for (uint64_t i = lo; i < m_count; ++i) {
        if (!readEntry(i, ek, raw, weight, learn) || ek != k) break;
        const Move m = decodeMove(pos, raw);
        if (m.isNull()) continue;
        auto same = std::find_if(out.begin(), out.end(), [&](const BookMove& b) { return b.move == m; });
        if (same != out.end()) same->weight = uint16_t(std::min(0xFFFF, same->weight + weight)); // duplicate: weights summed
        else out.push_back({m, weight, learn});
    }
    std::stable_sort(out.begin(), out.end(), [](const BookMove& a, const BookMove& b) { return a.weight > b.weight; });
    return out;
}

} // namespace chess::polyglot
