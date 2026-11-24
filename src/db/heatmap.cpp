#include "heatmap.h"

#include "chess/position.h"
#include "database.h"

#include <atomic>
#include <mutex>
#include <thread>

using namespace chess;

namespace hy64db {

std::array<int, 64> PieceHeat::shares(Piece piece, int slice) const
{
    std::array<int, 64> out{};
    const uint64_t total = positionCount(slice);
    if (!total) return out;
    for (int s = 0; s < 64; ++s) {
        uint64_t n = 0;
        for (int k = 0; k < Slices; ++k)
            if (slice < 0 || slice == k) n += occupancy[size_t(k)][size_t(piece)][size_t(s)];
        out[size_t(s)] = int((n * 1000000 + total / 2) / total);
    }
    return out;
}

uint64_t PieceHeat::positionCount(int slice) const
{
    uint64_t total = 0;
    for (int k = 0; k < Slices; ++k)
        if (slice < 0 || slice == k) total += positions[size_t(k)];
    return total;
}

uint32_t PieceHeat::moved(Square origin) const
{
    uint32_t n = 0;
    for (uint32_t c : firstTo[size_t(origin)]) n += c;
    return n;
}

void PieceHeat::add(const PieceHeat& o)
{
    games += o.games;
    skipped += o.skipped;
    for (int k = 0; k < Slices; ++k) {
        positions[size_t(k)] += o.positions[size_t(k)];
        for (int p = 0; p < 16; ++p)
            for (int s = 0; s < 64; ++s) occupancy[size_t(k)][size_t(p)][size_t(s)] += o.occupancy[size_t(k)][size_t(p)][size_t(s)];
    }
    for (int a = 0; a < 64; ++a) {
        firstPly[size_t(a)] += o.firstPly[size_t(a)];
        for (int b = 0; b < 64; ++b) firstTo[size_t(a)][size_t(b)] += o.firstTo[size_t(a)][size_t(b)];
    }
}

PieceHeat pieceHeat(const Database& db, const std::vector<uint32_t>& ids, const std::function<bool(uint64_t, uint64_t)>& progress, bool* cancelled)
{
    PieceHeat total;
    const size_t n = ids.size();
    const unsigned threads = n < 4096 ? 1 : std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::atomic<uint64_t> done{0};
    std::atomic<bool> stop{false};
    std::mutex gather;
    const std::string start = Position::startPos().fen();
    auto work = [&](unsigned worker) {
        auto local = std::make_unique<PieceHeat>(); // (large: not on the stack)
        std::vector<uint8_t> blob;
        for (size_t i = worker; i < n && !stop; i += threads) {
            const uint32_t id = ids[i];
            const Record r = db.record(id);
            const uint64_t d = ++done;
            if (worker == 0 && progress && d % 2048 < threads && !progress(d, n)) stop = true;
            if (r.deleted()) continue;
            if ((r.flags & FlagStartFen) || !db.blob(id, blob)) {
                ++local->skipped;
                continue;
            }
            uint64_t left = 0; // origin squares whose piece has moved or been captured
            bool standard = true;
            replayMainlineMoves(blob.data(), blob.size(), [&](const Position& before, Move m, int ply) {
                if (ply == 1 && before.fen() != start) {
                    standard = false;
                    return false;
                }
                const size_t slice = size_t(PieceHeat::sliceOfPly(ply));
                ++local->positions[slice];
                auto& occ = local->occupancy[slice];
                for (int s = 0; s < 64; ++s)
                    if (const Piece p = before.at(Square(s)); p != NoPiece) ++occ[size_t(p)][size_t(s)];
                // First move of a piece still on its starting square
                const bool castling = before.isCastling(m);
                const int rank = rankOf(m.from);
                auto first = [&](Square origin, Square target) {
                    if ((rank != 0 && rank != 1 && rank != 6 && rank != 7) || (left >> origin & 1)) return;
                    left |= uint64_t(1) << origin;
                    ++local->firstTo[size_t(origin)][size_t(target)];
                    local->firstPly[size_t(origin)] += uint64_t(ply);
                };
                if (castling) {
                    const bool kingside = before.isKingsideCastling(m);
                    const Square rook = before.castlingRook(before.sideToMove(), kingside);
                    first(m.from, makeSquare(kingside ? 6 : 2, rank));
                    if (rook != NoSquare) first(rook, makeSquare(kingside ? 5 : 3, rank));
                } else {
                    first(m.from, m.to);
                    left |= uint64_t(1) << m.to; // a piece captured at home never makes a first move
                }
                return true;
            });
            if (standard) ++local->games;
            else ++local->skipped;
        }
        std::lock_guard lock(gather);
        total.add(*local);
    };
    if (threads == 1) work(0);
    else {
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < threads; ++w) pool.emplace_back(work, w);
        for (auto& t : pool) t.join();
    }
    if (cancelled) *cancelled = stop;
    return total;
}

} // namespace hy64db
