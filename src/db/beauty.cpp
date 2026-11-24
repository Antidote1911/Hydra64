#include "beauty.h"

#include <algorithm>

namespace hy64db {

using namespace chess;

namespace {

int valueOf(PieceType t)
{
    switch (t) {
    case Pawn: return 1;
    case Knight:
    case Bishop: return 3;
    case Rook: return 5;
    case Queen: return 9;
    default: return 0;
    }
}

// Points of a sacrifice by the material given up
int sacrificePoints(int given)
{
    if (given >= 8) return 8; // queen
    if (given >= 5) return 5; // rook, queen for a rook and more
    if (given >= 3) return 3; // piece
    return 2;                 // exchange, piece for pawns
}

} // namespace

void BeautyRater::start()
{
    m_plies.clear();
    m_balance = 0;
    m_lastMove = Move();
}

void BeautyRater::move(const Position& before, Move m)
{
    if (m_plies.empty()) {
        m_first = before.sideToMove();
        m_balance = 0;
        for (Square s = 0; s < 64; ++s)
            if (const Piece p = before.at(s)) m_balance += (colorOf(p) == White ? 1 : -1) * valueOf(typeOf(p));
    }
    const int sign = before.sideToMove() == White ? 1 : -1;
    Ply ply;
    if (!before.isCastling(m)) { // (Chess960 castling is written "king captures its rook")
        const Piece moved = before.at(m.from);
        Piece taken = before.at(m.to);
        if (!taken && typeOf(moved) == Pawn && m.to == before.epSquare() && fileOf(m.from) != fileOf(m.to)) taken = makePiece(~before.sideToMove(), Pawn);
        if (taken) {
            ply.capture = true;
            m_balance += sign * valueOf(typeOf(taken));
        }
        if (m.promo != NoType) {
            ply.promotion = true;
            ply.underpromotion = m.promo != Queen;
            m_balance += sign * (valueOf(m.promo) - 1);
        }
    }
    ply.balance = int8_t(std::clamp(m_balance, -120, 120));
    m_plies.push_back(ply);
    m_last = before;
    m_lastMove = m;
}

int BeautyRater::points(Result result) const
{
    const int n = int(m_plies.size());
    if (n < 20) return 0; // ten moves at least: shorter games are opening accidents or fragments
    const Position final = m_last.after(m_lastMove);
    const bool mate = final.isCheckmate();
    Color winner;
    if (result == WhiteWins) winner = White;
    else if (result == BlackWins) winner = Black;
    else if (result == ResultNone && mate) winner = ~final.sideToMove();
    else return 0;
    if (mate && winner == final.sideToMove()) return 0; // result contradicting the board

    const int sign = winner == White ? 1 : -1;
    auto balance = [&](int i) { return sign * m_plies[size_t(std::clamp(i, 0, n - 1))].balance; }; // the winner's, after ply i
    auto mover = [&](int i) { return (i % 2 == 0) ? m_first : ~m_first; };

    int points = 0, sacrifices = 0;
    for (int c = 2; c < n; ++c) {
        // A capture by the loser that the winner does not take back: two moves later, the winner
        // is still at least two points below what it had.
        if (mover(c) == winner || !m_plies[size_t(c)].capture || m_plies[size_t(c)].promotion) continue;
        // What it had before its own last move: an exchange it started (BxN, PxB) or a pawn just
        // promoted and taken are not gifts.
        const int base = balance(c - 2);
        const int given = base - std::max(balance(c + 1), balance(c + 3));
        if (given < 2) continue;
        // Compensation: the game is won within twelve moves, or the material comes back.
        bool compensated = n - 1 - c <= 24;
        for (int k = c + 4; !compensated && k <= c + 24 && k < n; ++k) compensated = balance(k) >= base - 1;
        if (!compensated) continue;
        points += sacrificePoints(given);
        ++sacrifices;
    }
    points = std::min(points, 14);

    if (mate) {
        points += 1;
        if (balance(n - 1) <= -3) points += 2; // mate with less material
    }
    // King hunt: the losing king ends beyond the middle of the board, heavy material still there
    {
        const Square king = final.kingSquare(~winner);
        int material = 0;
        for (Square s = 0; s < 64; ++s) material += valueOf(typeOf(final.at(s)));
        if (king != NoSquare && material >= 30) {
            const int advance = winner == White ? 7 - rankOf(king) : rankOf(king); // ranks away from its back rank
            if (advance >= 4) points += 2;
        }
    }
    for (int i = 0; i < n; ++i)
        if (mover(i) == winner && m_plies[size_t(i)].underpromotion) {
            points += 2;
            break;
        }
    if (sacrifices > 0 && n <= 50) points += 1; // miniature
    return points;
}

int BeautyRater::medals(int points)
{
    return points >= 9 ? 3 : points >= 5 ? 2 : points >= 3 ? 1 : 0;
}

} // namespace hy64db
