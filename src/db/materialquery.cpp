#include "materialquery.h"

using namespace chess;

namespace hy64db {

namespace {

constexpr int Value[7] = {0, 1, 3, 3, 5, 9, 0};

struct Counts {
    int8_t n[2][7] = {};
    uint8_t pawnFiles[2] = {}; // bit f: pawn on file f
    int material[2] = {};
};

Counts countOf(const Position& pos)
{
    Counts c;
    for (int s = 0; s < 64; ++s) {
        const Piece p = pos.at(Square(s));
        if (p == NoPiece) continue;
        const int side = colorOf(p), t = typeOf(p);
        ++c.n[side][t];
        c.material[side] += Value[t];
        if (t == Pawn) c.pawnFiles[side] |= uint8_t(1u << fileOf(Square(s)));
    }
    return c;
}

bool has(const Position& pos, const char* sq, Color c, PieceType t) { return pos.at(parseSquare(sq)) == makePiece(c, t); }

bool pawnOnFile(const Counts& c, Color side, int file) { return file >= 0 && file < 8 && (c.pawnFiles[side] >> file) & 1; }

bool isolatedQueenPawn(const Counts& c, Color side) { return pawnOnFile(c, side, 3) && !pawnOnFile(c, side, 2) && !pawnOnFile(c, side, 4); }

bool hangingPawns(const Counts& c, Color side)
{
    return pawnOnFile(c, side, 2) && pawnOnFile(c, side, 3) && !pawnOnFile(c, side, 1) && !pawnOnFile(c, side, 4);
}

bool passedPawn(const Position& pos, Color side)
{
    for (int s = 0; s < 64; ++s) {
        if (pos.at(Square(s)) != makePiece(side, Pawn)) continue;
        const int f = fileOf(Square(s)), r = rankOf(Square(s));
        bool blocked = false;
        for (int df = -1; df <= 1 && !blocked; ++df) {
            const int ff = f + df;
            if (ff < 0 || ff > 7) continue;
            for (int rr = side == White ? r + 1 : r - 1; rr >= 0 && rr <= 7; rr += side == White ? 1 : -1)
                if (pos.at(makeSquare(ff, rr)) == makePiece(~side, Pawn)) {
                    blocked = true;
                    break;
                }
        }
        if (!blocked) return true;
    }
    return false;
}

bool squareIsLight(Square s) { return (fileOf(s) + rankOf(s)) % 2 == 1; }

Square bishopOf(const Position& pos, Color side)
{
    for (int s = 0; s < 64; ++s)
        if (pos.at(Square(s)) == makePiece(side, Bishop)) return Square(s);
    return NoSquare;
}

uint64_t pawnBits(const Position& pos, Color side)
{
    uint64_t b = 0;
    for (int s = 0; s < 64; ++s)
        if (pos.at(Square(s)) == makePiece(side, Pawn)) b |= 1ull << s;
    return b;
}

} // namespace

bool MaterialQuery::isEmpty() const
{
    for (int side = 0; side < 2; ++side)
        for (int t = Pawn; t <= Queen; ++t)
            if (minCount[side][t] > 0 || maxCount[side][t] < (t == Pawn ? 8 : 9)) return false;
    return !useDiff && bishops == Bishops::Any && !patterns && reference == Reference::None && !plyFrom && !plyTo && minPlies <= 1;
}

const char* patternName(MaterialQuery::Pattern p)
{
    switch (p) {
    case MaterialQuery::WhiteIsolatedQueenPawn: return "Pion d isolé (Blancs)";
    case MaterialQuery::BlackIsolatedQueenPawn: return "Pion d isolé (Noirs)";
    case MaterialQuery::WhiteHangingPawns: return "Pions pendants c/d (Blancs)";
    case MaterialQuery::BlackHangingPawns: return "Pions pendants c/d (Noirs)";
    case MaterialQuery::WhiteMaroczy: return "Centre Maroczy (Blancs)";
    case MaterialQuery::BlackMaroczy: return "Centre Maroczy (Noirs)";
    case MaterialQuery::WhiteKingFianchetto: return "Fianchetto roi (Blancs)";
    case MaterialQuery::BlackKingFianchetto: return "Fianchetto roi (Noirs)";
    case MaterialQuery::WhiteQueenFianchetto: return "Fianchetto dame (Blancs)";
    case MaterialQuery::BlackQueenFianchetto: return "Fianchetto dame (Noirs)";
    case MaterialQuery::OppositeCastling: return "Roques opposés";
    case MaterialQuery::RookSacC3: return "Tour noire prend en c3";
    case MaterialQuery::WhitePassedPawn: return "Pion passé (Blancs)";
    case MaterialQuery::BlackPassedPawn: return "Pion passé (Noirs)";
    }
    return "";
}

namespace {
bool matchesCounts(const MaterialQuery& q, const Position& pos, const Position* prev, const Counts& c);
}

bool MaterialMatcher::positionMatches(const MaterialQuery& q, const Position& pos, const Position* prev)
{
    return matchesCounts(q, pos, prev, countOf(pos));
}

namespace {
bool matchesCounts(const MaterialQuery& q, const Position& pos, const Position* prev, const Counts& c)
{
    for (int side = 0; side < 2; ++side)
        for (int t = Pawn; t <= Queen; ++t)
            if (c.n[side][t] < q.minCount[side][t] || c.n[side][t] > q.maxCount[side][t]) return false;
    if (q.useDiff) {
        const int d = c.material[White] - c.material[Black];
        if (d < q.diffMin || d > q.diffMax) return false;
    }
    if (q.bishops != MaterialQuery::Bishops::Any) {
        if (c.n[White][Bishop] != 1 || c.n[Black][Bishop] != 1) return false;
        const bool same = squareIsLight(bishopOf(pos, White)) == squareIsLight(bishopOf(pos, Black));
        if (same != (q.bishops == MaterialQuery::Bishops::SameColor)) return false;
    }
    using P = MaterialQuery;
    const uint32_t pt = q.patterns;
    if (pt) {
        if ((pt & P::WhiteIsolatedQueenPawn) && !isolatedQueenPawn(c, White)) return false;
        if ((pt & P::BlackIsolatedQueenPawn) && !isolatedQueenPawn(c, Black)) return false;
        if ((pt & P::WhiteHangingPawns) && !hangingPawns(c, White)) return false;
        if ((pt & P::BlackHangingPawns) && !hangingPawns(c, Black)) return false;
        if ((pt & P::WhiteMaroczy) && !(has(pos, "c4", White, Pawn) && has(pos, "e4", White, Pawn) && !pawnOnFile(c, White, 3) && has(pos, "d6", Black, Pawn)))
            return false;
        if ((pt & P::BlackMaroczy) && !(has(pos, "c5", Black, Pawn) && has(pos, "e5", Black, Pawn) && !pawnOnFile(c, Black, 3) && has(pos, "d3", White, Pawn)))
            return false;
        if ((pt & P::WhiteKingFianchetto) && !(has(pos, "g2", White, Bishop) && has(pos, "g3", White, Pawn))) return false;
        if ((pt & P::BlackKingFianchetto) && !(has(pos, "g7", Black, Bishop) && has(pos, "g6", Black, Pawn))) return false;
        if ((pt & P::WhiteQueenFianchetto) && !(has(pos, "b2", White, Bishop) && has(pos, "b3", White, Pawn))) return false;
        if ((pt & P::BlackQueenFianchetto) && !(has(pos, "b7", Black, Bishop) && has(pos, "b6", Black, Pawn))) return false;
        if (pt & P::OppositeCastling) {
            const int wf = fileOf(pos.kingSquare(White)), bf = fileOf(pos.kingSquare(Black));
            const bool wq = wf <= 2, wk = wf >= 6, bq = bf <= 2, bk = bf >= 6;
            if (!((wq && bk) || (wk && bq))) return false;
        }
        if (pt & P::RookSacC3) {
            const Square c3 = parseSquare("c3");
            const bool sac = prev && prev->sideToMove() == Black && pos.at(c3) == makePiece(Black, Rook) && prev->at(c3) != NoPiece
                             && colorOf(prev->at(c3)) == White;
            if (!sac) return false;
        }
        if ((pt & P::WhitePassedPawn) && !passedPawn(pos, White)) return false;
        if ((pt & P::BlackPassedPawn) && !passedPawn(pos, Black)) return false;
    }
    if (q.reference != MaterialQuery::Reference::None && q.refPos) {
        const Position& r = *q.refPos;
        switch (q.reference) {
        case MaterialQuery::Reference::SamePawns:
            if (pawnBits(pos, White) != pawnBits(r, White) || pawnBits(pos, Black) != pawnBits(r, Black)) return false;
            break;
        case MaterialQuery::Reference::SamePawnFiles: {
            // same number of pawns on each file, for each side
            for (int side = 0; side < 2; ++side)
                for (int f = 0; f < 8; ++f) {
                    int a = 0, b = 0;
                    for (int rk = 0; rk < 8; ++rk) {
                        a += pos.at(makeSquare(f, rk)) == makePiece(Color(side), Pawn);
                        b += r.at(makeSquare(f, rk)) == makePiece(Color(side), Pawn);
                    }
                    if (a != b) return false;
                }
            break;
        }
        case MaterialQuery::Reference::SameMaterial: {
            const Counts rc = countOf(r);
            for (int side = 0; side < 2; ++side)
                for (int t = Pawn; t <= Queen; ++t)
                    if (c.n[side][t] != rc.n[side][t]) return false;
            break;
        }
        default: break;
        }
    }
    return true;
}
} // namespace

MaterialMatcher::MaterialMatcher(const MaterialQuery& q)
    : m_q(q)
{
}

void MaterialMatcher::reset()
{
    m_hasPrev = false;
    m_run = 0;
    m_matched = false;
}

bool MaterialMatcher::feed(const Position& pos, int ply)
{
    if (m_matched) return false;
    if (m_q.plyTo && ply > m_q.plyTo) return false;
    // Material does not come back (except by promotion): an out-of-reach minimum stops the game.
    const Counts c = countOf(pos);
    for (int side = 0; side < 2; ++side) {
        if (c.n[side][Pawn] < m_q.minCount[side][Pawn]) return false;
        for (int t = Knight; t <= Queen; ++t)
            if (c.n[side][t] + c.n[side][Pawn] < m_q.minCount[side][t]) return false;
    }
    const bool inRange = ply >= m_q.plyFrom;
    const bool ok = inRange && matchesCounts(m_q, pos, m_hasPrev ? &m_prev : nullptr, c);
    m_run = ok ? m_run + 1 : 0;
    if (m_run >= std::max(1, m_q.minPlies)) m_matched = true;
    if (m_q.patterns & MaterialQuery::RookSacC3) {
        m_prev = pos;
        m_hasPrev = true;
    }
    return !m_matched;
}

} // namespace hy64db
