#include "tablebase.h"

#include <algorithm>
#include <mutex>

extern "C" {
#include "../../third_party/fathom/tbprobe.h"
}

namespace chess::tb {

namespace {
std::mutex g_mutex; // tb_probe_root is not reentrant

int wdlOf(unsigned v)
{
    switch (v) {
    case TB_WIN: return 2;
    case TB_CURSED_WIN: return 1;
    case TB_DRAW: return 0;
    case TB_BLESSED_LOSS: return -1;
    default: return -2;
    }
}
} // namespace

int init(const std::string& path)
{
    std::lock_guard lock(g_mutex);
    tb_free();
    if (path.empty()) return 0;
    if (!tb_init(path.c_str())) return 0;
    return int(TB_LARGEST);
}

int largest() { return int(TB_LARGEST); }

int pieceCount(const Position& pos)
{
    int n = 0;
    for (int s = 0; s < 64; ++s) n += pos.at(Square(s)) != NoPiece;
    return n;
}

bool result(const Position& pos, int& wdl)
{
    if (TB_LARGEST == 0 || pieceCount(pos) > int(TB_LARGEST) || pos.castlingRights() != 0) return false;
    if (pos.halfmoveClock() != 0) { // the 50-move clock counts: the distance is needed
        const Probe p = probe(pos);
        wdl = p.wdl;
        return p.ok;
    }
    uint64_t white = 0, black = 0, kings = 0, queens = 0, rooks = 0, bishops = 0, knights = 0, pawns = 0;
    for (int s = 0; s < 64; ++s) {
        const Piece p = pos.at(Square(s));
        if (p == NoPiece) continue;
        const uint64_t bit = 1ull << s;
        (colorOf(p) == White ? white : black) |= bit;
        switch (typeOf(p)) {
        case King: kings |= bit; break;
        case Queen: queens |= bit; break;
        case Rook: rooks |= bit; break;
        case Bishop: bishops |= bit; break;
        case Knight: knights |= bit; break;
        case Pawn: pawns |= bit; break;
        default: break;
        }
    }
    if (pos.legalMoves().empty()) { // mate or stalemate: not the tables' business
        wdl = pos.inCheck() ? -2 : 0;
        return true;
    }
    const unsigned ep = pos.epSquare() == NoSquare ? 0 : unsigned(pos.epSquare());
    const unsigned r = tb_probe_wdl(white, black, kings, queens, rooks, bishops, knights, pawns, 0, 0, ep, pos.sideToMove() == White);
    if (r == TB_RESULT_FAILED) return false;
    wdl = wdlOf(r);
    return true;
}

Probe probe(const Position& pos)
{
    Probe out;
    if (TB_LARGEST == 0 || pieceCount(pos) > int(TB_LARGEST) || pos.castlingRights() != 0) return out;
    uint64_t white = 0, black = 0, kings = 0, queens = 0, rooks = 0, bishops = 0, knights = 0, pawns = 0;
    for (int s = 0; s < 64; ++s) {
        const Piece p = pos.at(Square(s));
        if (p == NoPiece) continue;
        const uint64_t bit = 1ull << s;
        (colorOf(p) == White ? white : black) |= bit;
        switch (typeOf(p)) {
        case King: kings |= bit; break;
        case Queen: queens |= bit; break;
        case Rook: rooks |= bit; break;
        case Bishop: bishops |= bit; break;
        case Knight: knights |= bit; break;
        case Pawn: pawns |= bit; break;
        default: break;
        }
    }
    const unsigned ep = pos.epSquare() == NoSquare ? 0 : unsigned(pos.epSquare());
    std::vector<unsigned> results(TB_MAX_MOVES, TB_RESULT_FAILED);
    unsigned root;
    {
        std::lock_guard lock(g_mutex);
        root = tb_probe_root(white, black, kings, queens, rooks, bishops, knights, pawns, unsigned(pos.halfmoveClock()), 0, ep,
                             pos.sideToMove() == White, results.data());
    }
    if (root == TB_RESULT_FAILED) return out;
    out.ok = true;
    if (root == TB_RESULT_CHECKMATE) {
        out.wdl = -2;
        return out;
    }
    if (root == TB_RESULT_STALEMATE) return out;
    out.wdl = wdlOf(TB_GET_WDL(root));
    out.dtz = int(TB_GET_DTZ(root));
    for (unsigned r : results) {
        if (r == TB_RESULT_FAILED) break;
        MoveResult mr;
        mr.move.from = Square(TB_GET_FROM(r));
        mr.move.to = Square(TB_GET_TO(r));
        switch (TB_GET_PROMOTES(r)) {
        case TB_PROMOTES_QUEEN: mr.move.promo = Queen; break;
        case TB_PROMOTES_ROOK: mr.move.promo = Rook; break;
        case TB_PROMOTES_BISHOP: mr.move.promo = Bishop; break;
        case TB_PROMOTES_KNIGHT: mr.move.promo = Knight; break;
        default: break;
        }
        if (!pos.isLegal(mr.move)) continue;
        mr.wdl = wdlOf(TB_GET_WDL(r));
        mr.dtz = int(TB_GET_DTZ(r));
        out.moves.push_back(mr);
    }
    // Best first: result, then win fast / lose slowly
    std::stable_sort(out.moves.begin(), out.moves.end(), [](const MoveResult& a, const MoveResult& b) {
        if (a.wdl != b.wdl) return a.wdl > b.wdl;
        return a.wdl > 0 ? a.dtz < b.dtz : a.dtz > b.dtz;
    });
    return out;
}

} // namespace chess::tb
