#include "position.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace chess {

namespace {

constexpr int KnightD[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KingD[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int BishopD[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr int RookD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

constexpr uint8_t CastleWK = 1, CastleWQ = 2, CastleBK = 4, CastleBQ = 8;

inline bool onBoard(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

char pieceChar(Piece p)
{
    static const char chars[] = " PNBRQK";
    char c = chars[typeOf(p)];
    return colorOf(p) == Black ? char(std::tolower(c)) : c;
}

Piece pieceFromChar(char c)
{
    Color col = std::islower(static_cast<unsigned char>(c)) ? Black : White;
    switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'P': return makePiece(col, Pawn);
    case 'N': return makePiece(col, Knight);
    case 'B': return makePiece(col, Bishop);
    case 'R': return makePiece(col, Rook);
    case 'Q': return makePiece(col, Queen);
    case 'K': return makePiece(col, King);
    default: return NoPiece;
    }
}

PieceType typeFromLetter(char c)
{
    switch (c) {
    case 'N': return Knight;
    case 'B': return Bishop;
    case 'R': return Rook;
    case 'Q': return Queen;
    case 'K': return King;
    default: return NoType;
    }
}

char letterFromType(PieceType t)
{
    static const char chars[] = " PNBRQK";
    return chars[t];
}

} // namespace

Position::Position() { m_board.fill(NoPiece); }

Position Position::startPos()
{
    Position p;
    p.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    return p;
}

bool Position::setFen(const std::string& fen)
{
    std::istringstream in(fen);
    std::string board, stm, castling = "-", ep = "-";
    int half = 0, full = 1;
    if (!(in >> board >> stm)) return false;
    in >> castling >> ep;
    if (!(in >> half)) half = 0;
    if (!(in >> full)) full = 1;

    Position p;
    int rank = 7, file = 0;
    for (char c : board) {
        if (c == '/') {
            if (file != 8) return false;
            --rank;
            file = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            file += c - '0';
            if (file > 8) return false;
        } else {
            Piece pc = pieceFromChar(c);
            if (pc == NoPiece || file > 7 || rank < 0) return false;
            p.m_board[makeSquare(file, rank)] = pc;
            ++file;
        }
    }
    if (rank != 0 || file != 8) return false;
    p.m_king = {p.findKing(White), p.findKing(Black)};
    if (p.kingSquare(White) == NoSquare || p.kingSquare(Black) == NoSquare) return false;

    if (stm == "w") p.m_stm = White;
    else if (stm == "b") p.m_stm = Black;
    else return false;

    // Castling: KQkq (outermost rook on that side of the king, X-FEN) or the file letter of
    // the rook (Shredder-FEN: "HAha"). A right without a king on its rank or without a rook is ignored.
    p.m_castling = 0;
    for (char c : castling) {
        const Color col = std::isupper(static_cast<unsigned char>(c)) ? White : Black;
        const int rank = col == White ? 0 : 7;
        const Square king = p.kingSquare(col);
        if (king == NoSquare || rankOf(king) != rank) continue;
        const Piece rook = makePiece(col, Rook);
        const char l = char(std::tolower(static_cast<unsigned char>(c)));
        int rookFile = -1;
        if (l == 'k') {
            for (int f = 7; f > fileOf(king); --f)
                if (p.m_board[makeSquare(f, rank)] == rook) {
                    rookFile = f;
                    break;
                }
        } else if (l == 'q') {
            for (int f = 0; f < fileOf(king); ++f)
                if (p.m_board[makeSquare(f, rank)] == rook) {
                    rookFile = f;
                    break;
                }
        } else if (l >= 'a' && l <= 'h' && p.m_board[makeSquare(l - 'a', rank)] == rook && l - 'a' != fileOf(king)) {
            rookFile = l - 'a';
        }
        if (rookFile < 0) continue;
        const int idx = (col == White ? 0 : 2) + (rookFile > fileOf(king) ? 0 : 1);
        p.m_castling |= uint8_t(1 << idx);
        p.m_castleRook[size_t(idx)] = makeSquare(rookFile, rank);
    }
    p.m_ep = ep == "-" ? NoSquare : parseSquare(ep);
    p.m_halfmove = half;
    p.m_fullmove = full < 1 ? 1 : full;
    *this = p;
    return true;
}

std::string Position::fen() const
{
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            Piece p = m_board[makeSquare(f, r)];
            if (p == NoPiece) {
                ++empty;
            } else {
                if (empty) s += char('0' + empty);
                empty = 0;
                s += pieceChar(p);
            }
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += m_stm == White ? " w " : " b ";
    // X-FEN: KQkq when the castling rook is the outermost on its side, otherwise its file.
    std::string c;
    for (int i = 0; i < 4; ++i) {
        if (!(m_castling & (1 << i))) continue;
        const Color col = i < 2 ? White : Black;
        const bool kingside = i % 2 == 0;
        const Square rook = m_castleRook[size_t(i)];
        bool outermost = true;
        for (int f = fileOf(rook) + (kingside ? 1 : -1); f >= 0 && f < 8; f += kingside ? 1 : -1)
            if (m_board[makeSquare(f, rankOf(rook))] == makePiece(col, Rook)) outermost = false;
        const char l = outermost ? (kingside ? 'k' : 'q') : char('a' + fileOf(rook));
        c += col == White ? char(std::toupper(static_cast<unsigned char>(l))) : l;
    }
    s += c.empty() ? "-" : c;
    s += ' ';
    s += m_ep == NoSquare ? "-" : squareName(m_ep);
    s += ' ' + std::to_string(m_halfmove) + ' ' + std::to_string(m_fullmove);
    return s;
}

Square Position::kingSquare(Color c) const { return m_king[c]; }

Square Position::findKing(Color c) const
{
    Piece k = makePiece(c, King);
    for (int s = 0; s < 64; ++s)
        if (m_board[s] == k) return Square(s);
    return NoSquare;
}

bool Position::isAttacked(Square s, Color by) const
{
    const int f = fileOf(s), r = rankOf(s);

    const int pr = by == White ? r - 1 : r + 1;
    for (int df : {-1, 1})
        if (onBoard(f + df, pr) && m_board[makeSquare(f + df, pr)] == makePiece(by, Pawn)) return true;

    for (auto& d : KnightD)
        if (onBoard(f + d[0], r + d[1]) && m_board[makeSquare(f + d[0], r + d[1])] == makePiece(by, Knight))
            return true;

    for (auto& d : KingD)
        if (onBoard(f + d[0], r + d[1]) && m_board[makeSquare(f + d[0], r + d[1])] == makePiece(by, King))
            return true;

    auto slide = [&](const int (*dirs)[2], PieceType t) {
        for (int i = 0; i < 4; ++i) {
            int ff = f + dirs[i][0], rr = r + dirs[i][1];
            while (onBoard(ff, rr)) {
                Piece p = m_board[makeSquare(ff, rr)];
                if (p != NoPiece) {
                    if (colorOf(p) == by && (typeOf(p) == t || typeOf(p) == Queen)) return true;
                    break;
                }
                ff += dirs[i][0];
                rr += dirs[i][1];
            }
        }
        return false;
    };
    return slide(BishopD, Bishop) || slide(RookD, Rook);
}

bool Position::inCheck() const
{
    Square k = kingSquare(m_stm);
    return k != NoSquare && isAttacked(k, ~m_stm);
}

void Position::addPawnMove(std::vector<Move>& out, Square from, Square to) const
{
    const int promoRank = m_stm == White ? 7 : 0;
    if (rankOf(to) == promoRank) {
        for (PieceType t : {Queen, Rook, Bishop, Knight}) out.push_back({from, to, t});
    } else {
        out.push_back({from, to, NoType});
    }
}

void Position::generatePseudo(std::vector<Move>& out) const
{
    const Color us = m_stm;
    for (int s = 0; s < 64; ++s) {
        Piece p = m_board[s];
        if (p == NoPiece || colorOf(p) != us) continue;
        const int f = fileOf(Square(s)), r = rankOf(Square(s));

        switch (typeOf(p)) {
        case Pawn: {
            const int dir = us == White ? 1 : -1;
            const int start = us == White ? 1 : 6;
            if (onBoard(f, r + dir) && m_board[makeSquare(f, r + dir)] == NoPiece) {
                addPawnMove(out, Square(s), makeSquare(f, r + dir));
                if (r == start && m_board[makeSquare(f, r + 2 * dir)] == NoPiece)
                    out.push_back({Square(s), makeSquare(f, r + 2 * dir), NoType});
            }
            for (int df : {-1, 1}) {
                if (!onBoard(f + df, r + dir)) continue;
                Square t = makeSquare(f + df, r + dir);
                Piece target = m_board[t];
                if ((target != NoPiece && colorOf(target) != us) || t == m_ep)
                    addPawnMove(out, Square(s), t);
            }
            break;
        }
        case Knight:
        case King: {
            auto& deltas = typeOf(p) == Knight ? KnightD : KingD;
            for (auto& d : deltas) {
                if (!onBoard(f + d[0], r + d[1])) continue;
                Square t = makeSquare(f + d[0], r + d[1]);
                if (m_board[t] == NoPiece || colorOf(m_board[t]) != us) out.push_back({Square(s), t, NoType});
            }
            break;
        }
        case Bishop:
        case Rook:
        case Queen: {
            auto ray = [&](const int (*dirs)[2]) {
                for (int i = 0; i < 4; ++i) {
                    int ff = f + dirs[i][0], rr = r + dirs[i][1];
                    while (onBoard(ff, rr)) {
                        Square t = makeSquare(ff, rr);
                        if (m_board[t] != NoPiece) {
                            if (colorOf(m_board[t]) != us) out.push_back({Square(s), t, NoType});
                            break;
                        }
                        out.push_back({Square(s), t, NoType});
                        ff += dirs[i][0];
                        rr += dirs[i][1];
                    }
                }
            };
            if (typeOf(p) != Rook) ray(BishopD);
            if (typeOf(p) != Bishop) ray(RookD);
            break;
        }
        default: break;
        }
    }

    // Castling (Chess960 rules, of which classical castling is a special case): the king goes to g
    // or c, the rook to f or d; between their starting and target squares there is nothing but the two of them, and the
    // king does not start from an attacked square, cross one or land on one.
    const int base = us == White ? 0 : 56;
    for (int side = 0; side < 2; ++side) { // 0: kingside castling, 1: queenside castling
        const int idx = (us == White ? 0 : 2) + side;
        if (!(m_castling & (1 << idx))) continue;
        const Square king = kingSquare(us), rook = m_castleRook[size_t(idx)];
        if (king == NoSquare || rankOf(king) != rankOf(Square(base)) || m_board[rook] != makePiece(us, Rook)) continue;
        const int kingTo = base + (side == 0 ? 6 : 2), rookTo = base + (side == 0 ? 5 : 3);
        bool ok = true;
        for (int s = std::min<int>({king, rook, kingTo, rookTo}); s <= std::max<int>({king, rook, kingTo, rookTo}) && ok; ++s)
            if (s != king && s != rook && m_board[s] != NoPiece) ok = false;
        for (int s = std::min<int>(king, kingTo); s <= std::max<int>(king, kingTo) && ok; ++s)
            if (isAttacked(Square(s), ~us)) ok = false;
        if (ok) out.push_back(castlingMove(side == 0));
    }
}

std::vector<Move> Position::legalMoves() const
{
    std::vector<Move> pseudo, legal;
    pseudo.reserve(64);
    generatePseudo(pseudo);
    legal.reserve(pseudo.size());
    for (const Move& m : pseudo) {
        Position p = *this;
        p.makeMove(m);
        Square k = p.kingSquare(m_stm);
        if (!p.isAttacked(k, ~m_stm)) legal.push_back(m);
    }
    return legal;
}

bool Position::isChess960() const
{
    for (int i = 0; i < 4; ++i) {
        if (!(m_castling & (1 << i))) continue;
        const Color c = i < 2 ? White : Black;
        const int rank = c == White ? 0 : 7;
        if (kingSquare(c) != makeSquare(4, rank) || m_castleRook[size_t(i)] != makeSquare(i % 2 == 0 ? 7 : 0, rank)) return true;
    }
    return false;
}

bool Position::isCastling(Move m) const
{
    if (m.from < 0 || m.to < 0) return false;
    const Piece p = m_board[m.from];
    if (typeOf(p) != King || rankOf(m.from) != rankOf(m.to)) return false;
    return std::abs(fileOf(m.to) - fileOf(m.from)) == 2 || m_board[m.to] == makePiece(colorOf(p), Rook);
}

Move Position::castlingMove(bool kingside) const
{
    const int idx = (m_stm == White ? 0 : 2) + (kingside ? 0 : 1);
    if (!(m_castling & (1 << idx))) return {};
    const Square king = kingSquare(m_stm), rook = m_castleRook[size_t(idx)];
    if (king == NoSquare) return {};
    const int rank = m_stm == White ? 0 : 7;
    const bool classical = king == makeSquare(4, rank) && rook == makeSquare(kingside ? 7 : 0, rank);
    return {king, classical ? makeSquare(kingside ? 6 : 2, rank) : rook, NoType};
}

Position Position::chess960Start(int number)
{
    // Scharnagl numbering: light-squared bishop, dark-squared bishop, queen, knights, then R K R.
    int n = ((number % 960) + 960) % 960;
    char row[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    row[(n % 4) * 2 + 1] = 'b';
    n /= 4;
    row[(n % 4) * 2] = 'b';
    n /= 4;
    auto place = [&](int k, char piece) {
        for (int f = 0; f < 8; ++f)
            if (!row[f] && k-- == 0) {
                row[f] = piece;
                return;
            }
    };
    place(n % 6, 'q');
    n /= 6;
    static constexpr int knights[10][2] = {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}};
    place(knights[n][1], 'n'); // rightmost first: the ranks of the free squares do not move for the other one
    place(knights[n][0], 'n');
    place(0, 'r');
    place(0, 'k');
    place(0, 'r');
    std::string back(row, 8), white = back;
    for (char& c : white) c = char(std::toupper(static_cast<unsigned char>(c)));
    Position p;
    p.setFen(back + "/pppppppp/8/8/8/8/PPPPPPPP/" + white + " w KQkq - 0 1");
    return p;
}

bool Position::canReach(Square from, Square to) const
{
    if (from == to) return false;
    const Piece p = m_board[from];
    const Piece target = m_board[to];
    if (target != NoPiece && colorOf(target) == m_stm) return false;
    const int df = fileOf(to) - fileOf(from), dr = rankOf(to) - rankOf(from);
    const int adf = std::abs(df), adr = std::abs(dr);
    switch (typeOf(p)) {
    case Pawn: {
        const int dir = m_stm == White ? 1 : -1;
        if (df == 0) {
            if (target != NoPiece) return false;
            if (dr == dir) return true;
            return dr == 2 * dir && rankOf(from) == (m_stm == White ? 1 : 6) && m_board[from + 8 * dir] == NoPiece;
        }
        return adf == 1 && dr == dir && (target != NoPiece || to == m_ep);
    }
    case Knight: return (adf == 1 && adr == 2) || (adf == 2 && adr == 1);
    case King: return adf <= 1 && adr <= 1;
    case Bishop:
        if (adf != adr) return false;
        break;
    case Rook:
        if (df != 0 && dr != 0) return false;
        break;
    case Queen:
        if (df != 0 && dr != 0 && adf != adr) return false;
        break;
    default: return false;
    }
    const int step = (df > 0) - (df < 0) + 8 * ((dr > 0) - (dr < 0));
    for (int s = from + step; s != to; s += step)
        if (m_board[s] != NoPiece) return false;
    return true;
}

bool Position::leavesKingSafe(Move m) const
{
    Position p = *this;
    p.makeMove(m);
    return !p.isAttacked(p.kingSquare(m_stm), ~m_stm);
}

bool Position::hasLegalMove() const
{
    std::vector<Move> pseudo;
    pseudo.reserve(64);
    generatePseudo(pseudo);
    for (const Move& m : pseudo)
        if (leavesKingSafe(m)) return true;
    return false;
}

bool Position::isPseudoLegal(Move m) const
{
    if (m.from < 0 || m.to < 0 || m.from > 63 || m.to > 63) return false;
    const Piece p = m_board[m.from];
    if (p == NoPiece || colorOf(p) != m_stm) return false;
    if (isCastling(m)) return isLegal(m);
    if (!canReach(m.from, m.to)) return false;
    const bool promotes = typeOf(p) == Pawn && rankOf(m.to) == (m_stm == White ? 7 : 0);
    return promotes ? (m.promo == Queen || m.promo == Rook || m.promo == Bishop || m.promo == Knight) : m.promo == NoType;
}

bool Position::isLegal(Move m) const
{
    if (m.from < 0 || m.to < 0) return false;
    const Piece p = m_board[m.from];
    if (p == NoPiece || colorOf(p) != m_stm) return false;
    if (isCastling(m)) { // castling: through move generation
        for (const Move& l : legalMoves())
            if (l == m) return true;
        return false;
    }
    if (!canReach(m.from, m.to)) return false;
    const bool promotes = typeOf(p) == Pawn && rankOf(m.to) == (m_stm == White ? 7 : 0);
    if (promotes ? (m.promo != Queen && m.promo != Rook && m.promo != Bishop && m.promo != Knight) : m.promo != NoType) return false;
    return leavesKingSafe(m);
}

void Position::makeMove(Move m)
{
    if (m.isNull()) { // null move ("--" of ChessBase analyses): the turn passes to the opponent
        ++m_halfmove;
        m_ep = NoSquare;
        if (m_stm == Black) ++m_fullmove;
        m_stm = m_stm == White ? Black : White;
        return;
    }
    const Piece p = m_board[m.from];
    const PieceType t = typeOf(p);
    const bool capture = m_board[m.to] != NoPiece && colorOf(m_board[m.to]) != m_stm;

    ++m_halfmove;
    if (t == Pawn || capture) m_halfmove = 0;

    if (t == Pawn && m.to == m_ep) // en passant
        m_board[makeSquare(fileOf(m.to), rankOf(m.from))] = NoPiece;

    m_ep = NoSquare;
    if (t == Pawn && std::abs(m.to - m.from) == 16) m_ep = Square((m.from + m.to) / 2);

    // A castling right is lost when its rook moves or is captured, or when the king moves.
    for (int i = 0; i < 4; ++i)
        if ((m_castling & (1 << i)) && (m.from == m_castleRook[size_t(i)] || m.to == m_castleRook[size_t(i)])) m_castling &= uint8_t(~(1 << i));
    if (t == King) m_castling &= uint8_t(m_stm == White ? ~(CastleWK | CastleWQ) : ~(CastleBK | CastleBQ));

    if (isCastling(m)) { // king to g or c, rook to f or d (the starting rook is on m.to or in the classical corner)
        const int base = rankOf(m.from) * 8;
        const bool kingside = isKingsideCastling(m);
        const Piece rookPiece = makePiece(m_stm, Rook);
        Square rook = m.to;
        if (m_board[rook] != rookPiece) rook = Square(base + (kingside ? 7 : 0));
        m_board[m.from] = NoPiece;
        m_board[rook] = NoPiece;
        m_board[base + (kingside ? 6 : 2)] = p;
        m_king[m_stm] = Square(base + (kingside ? 6 : 2));
        m_board[base + (kingside ? 5 : 3)] = rookPiece;
        if (m_stm == Black) ++m_fullmove;
        m_stm = ~m_stm;
        return;
    }

    const bool kingTaken = typeOf(m_board[m.to]) == King; // irregular position: we look for this king
    m_board[m.to] = m.promo != NoType ? makePiece(colorOf(p), m.promo) : p;
    m_board[m.from] = NoPiece;
    if (t == King) m_king[colorOf(p)] = m.to;
    if (kingTaken) m_king[~colorOf(p)] = findKing(~colorOf(p));

    if (m_stm == Black) ++m_fullmove;
    m_stm = ~m_stm;
}

bool Position::isCheckmate() const { return inCheck() && legalMoves().empty(); }
bool Position::isStalemate() const { return !inCheck() && legalMoves().empty(); }

bool Position::isInsufficientMaterial() const
{
    int minors = 0;
    int bishopColors[2] = {0, 0};
    for (int s = 0; s < 64; ++s) {
        Piece p = m_board[s];
        if (p == NoPiece) continue;
        switch (typeOf(p)) {
        case King: break;
        case Knight: ++minors; break;
        case Bishop:
            ++minors;
            ++bishopColors[(fileOf(Square(s)) + rankOf(Square(s))) & 1];
            break;
        default: return false;
        }
    }
    if (minors <= 1) return true;
    // Bishops only, all on the same square colour
    int knights = minors - bishopColors[0] - bishopColors[1];
    return knights == 0 && (bishopColors[0] == 0 || bishopColors[1] == 0);
}

std::string Position::toSan(Move m) const
{
    if (m.isNull()) return "--";
    const Piece p = m_board[m.from];
    const PieceType t = typeOf(p);
    std::string san;

    if (isCastling(m)) {
        san = isKingsideCastling(m) ? "O-O" : "O-O-O";
    } else {
        // A pawn changing file is always written as a capture, even onto an empty square
        // other than en passant (irregular move kept from a ChessBase database, see parseSanLenient).
        const bool capture = m_board[m.to] != NoPiece || (t == Pawn && fileOf(m.from) != fileOf(m.to));
        if (t == Pawn) {
            if (capture) san += char('a' + fileOf(m.from));
        } else {
            san += letterFromType(t);
            // Other identical pieces that can legally go to the same square (without generating
            // every move: only pieces of the same kind are examined).
            bool ambiguous = false, sameFile = false, sameRank = false;
            if (t != King)
                for (int s = 0; s < 64; ++s) {
                    if (m_board[s] != p || s == m.from || !canReach(Square(s), m.to) || !leavesKingSafe({Square(s), m.to, NoType})) continue;
                    ambiguous = true;
                    if (fileOf(Square(s)) == fileOf(m.from)) sameFile = true;
                    if (rankOf(Square(s)) == rankOf(m.from)) sameRank = true;
                }
            if (ambiguous) {
                if (!sameFile) san += char('a' + fileOf(m.from));
                else if (!sameRank) san += char('1' + rankOf(m.from));
                else san += squareName(m.from);
            }
        }
        if (capture) san += 'x';
        san += squareName(m.to);
        if (m.promo != NoType) {
            san += '=';
            san += letterFromType(m.promo);
        }
    }

    Position next = after(m);
    if (next.inCheck()) san += next.hasLegalMove() ? '+' : '#';
    return san;
}

Move Position::parseSan(std::string_view input) const { return parseSanWith(input, false); }
Move Position::parseSanLenient(std::string_view input) const { return parseSanWith(input, true); }

Move Position::parseSanWith(std::string_view input, bool lenient) const
{
    std::string s;
    for (char c : input)
        if (c != 'x' && c != '=' && c != '+' && c != '#' && c != '!' && c != '?' && c != ':') s += c;
    if (s.empty()) return {};

    for (char& c : s)
        if (c == '0') c = 'O';

    if (s == "O-O" || s == "O-O-O") {
        const Move m = castlingMove(s == "O-O");
        return !m.isNull() && isLegal(m) ? m : Move{};
    }

    PieceType type = Pawn;
    size_t i = 0;
    if (PieceType t = typeFromLetter(s[0]); t != NoType) {
        type = t;
        i = 1;
    }
    std::string rest = s.substr(i);

    PieceType promo = NoType;
    if (type == Pawn && !rest.empty()) {
        char last = rest.back();
        PieceType t = typeFromLetter(char(std::toupper(static_cast<unsigned char>(last))));
        if (t != NoType && t != King && (std::isupper(static_cast<unsigned char>(last)) || last == 'q' || last == 'r' || last == 'n')) {
            promo = t;
            rest.pop_back();
        }
    }
    if (rest.size() < 2) return {};
    Square to = parseSquare(rest.substr(rest.size() - 2));
    if (to == NoSquare) return {};
    std::string dis = rest.substr(0, rest.size() - 2);

    int fromFile = -1, fromRank = -1;
    for (char c : dis) {
        if (c >= 'a' && c <= 'h') fromFile = c - 'a';
        else if (c >= '1' && c <= '8') fromRank = c - '1';
        else return {};
    }

    // Only pieces of the right kind are examined (no generation of all legal moves).
    const Piece wanted = makePiece(m_stm, type);
    const bool lastRank = type == Pawn && rankOf(to) == (m_stm == White ? 7 : 0);
    if (lastRank != (promo != NoType)) return {}; // promotion missing or not on the last rank
    Move found;
    for (int sq = 0; sq < 64; ++sq) {
        if (m_board[sq] != wanted) continue;
        if (fromFile >= 0 && fileOf(Square(sq)) != fromFile) continue;
        if (fromRank >= 0 && rankOf(Square(sq)) != fromRank) continue;
        const Move m{Square(sq), to, promo};
        if (!canReach(Square(sq), to) || (!lenient && !leavesKingSafe(m))) continue;
        if (!found.isNull()) return {}; // ambiguous
        found = m;
    }
    // Lenient reading: pawn capture written onto an empty square ("cxb6" with no pawn to capture)
    if (lenient && found.isNull() && type == Pawn && fromFile >= 0 && std::abs(fromFile - fileOf(to)) == 1 && m_board[to] == NoPiece) {
        const int rank = rankOf(to) + (m_stm == White ? -1 : 1);
        if (rank >= 0 && rank < 8 && m_board[makeSquare(fromFile, rank)] == wanted) found = Move{makeSquare(fromFile, rank), to, promo};
    }
    // "Kg1" for castling: accepted as before
    if (found.isNull() && type == King && fromFile < 0 && fromRank < 0)
        for (const Move& m : legalMoves())
            if (isCastling(m) && makeSquare(isKingsideCastling(m) ? 6 : 2, rankOf(m.from)) == to) return m;
    return found;
}

Move Position::parseUci(std::string_view uci) const
{
    if (uci.size() < 4) return {};
    Move m{parseSquare(uci.substr(0, 2)), parseSquare(uci.substr(2, 2)), NoType};
    if (m.from == NoSquare || m.to == NoSquare) return {};
    if (uci.size() >= 5) {
        m.promo = typeFromLetter(char(std::toupper(static_cast<unsigned char>(uci[4]))));
    }
    // Castling: "king captures its rook" (engines in Chess960) or king onto its target square (e1g1),
    // brought back to the position's form.
    if (typeOf(m_board[m.from]) == King && colorOf(m_board[m.from]) == m_stm && rankOf(m.from) == rankOf(m.to)) {
        for (bool kingside : {true, false}) {
            const Move c = castlingMove(kingside);
            if (c.isNull() || c.from != m.from) continue;
            const Square rook = castlingRook(m_stm, kingside), target = makeSquare(kingside ? 6 : 2, rankOf(m.from));
            if (m.to == rook || (m.to == target && std::abs(fileOf(m.to) - fileOf(m.from)) >= 2) || (m.to == target && !isLegal(m)))
                return isLegal(c) ? c : Move{};
        }
    }
    return isLegal(m) ? m : Move{};
}

std::string Position::toUci(Move m)
{
    if (m.isNull()) return "0000";
    std::string s = squareName(m.from) + squareName(m.to);
    if (m.promo != NoType) s += char(std::tolower(letterFromType(m.promo)));
    return s;
}

} // namespace chess
