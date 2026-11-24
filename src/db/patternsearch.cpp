#include "patternsearch.h"

#include "gamecodec.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <thread>

using namespace chess;

namespace hy64db {

namespace {

// Walks the non-deleted games on several threads; `match(id, block)` says whether the game matches.
std::vector<uint32_t> scan(const Database& db, const std::function<bool(uint32_t, const std::vector<uint8_t>&)>& match, const std::atomic<int32_t>* cancel, const ScanProgress& progress)
{
    const uint32_t total = db.count();
    const unsigned threads = std::max(1u, std::min(32u, std::thread::hardware_concurrency()));
    std::atomic<uint32_t> next{0};
    std::vector<std::vector<uint32_t>> found(threads);
    std::mutex progressMutex;
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            std::vector<uint8_t> blob;
            constexpr uint32_t Chunk = 2048;
            for (;;) {
                const uint32_t first = next.fetch_add(Chunk);
                if (first >= total || (cancel && cancel->load())) return;
                const uint32_t last = std::min(total, first + Chunk);
                for (uint32_t id = first; id < last; ++id) {
                    if (db.record(id).deleted() || !db.blob(id, blob)) continue;
                    if (match(id, blob)) found[t].push_back(id);
                }
                if (progress) {
                    std::lock_guard lock(progressMutex);
                    progress(std::min<uint64_t>(last, total), total);
                }
            }
        });
    }
    for (std::thread& th : pool) th.join();
    std::vector<uint32_t> out;
    for (const auto& f : found) out.insert(out.end(), f.begin(), f.end());
    std::sort(out.begin(), out.end());
    return out;
}

uint64_t squaresOf(const Position& pos, Piece piece)
{
    uint64_t bits = 0;
    for (int s = 0; s < 64; ++s)
        if (pos.at(Square(s)) == piece) bits |= uint64_t(1) << s;
    return bits;
}

// Number of pieces of each kind (kings do not count).
std::array<uint8_t, 10> materialOf(const Position& pos)
{
    std::array<uint8_t, 10> m{};
    for (int s = 0; s < 64; ++s) {
        const Piece p = pos.at(Square(s));
        if (p == NoPiece || typeOf(p) == King) continue;
        ++m[size_t((colorOf(p) == White ? 0 : 5) + int(typeOf(p)) - int(Pawn))];
    }
    return m;
}

} // namespace

// ---------------------------------------------------------------- manoeuvre

ManeuverTracker::ManeuverTracker(const ManeuverQuery& q)
    : m_q(q)
{
}

bool ManeuverTracker::feed(const Position& pos)
{
    bool done = false;
    for (int c = 0; c < 2; ++c) {
        if (m_q.color != 2 && m_q.color != c) continue;
        const uint64_t now = squaresOf(pos, makePiece(c == 0 ? White : Black, m_q.piece));
        const uint64_t before = m_prev[c];
        m_prev[c] = now;
        if (m_first) continue;
        const uint64_t gone = before & ~now, came = now & ~before;
        if (!gone) continue;
        // A single departure and a single arrival: the piece has moved (otherwise capture, promotion…)
        const bool moved = (gone & (gone - 1)) == 0 && came && (came & (came - 1)) == 0;
        const Square from = Square(__builtin_ctzll(gone));
        const Square to = moved ? Square(__builtin_ctzll(came)) : NoSquare;
        std::vector<Track>& tracks = m_tracks[c];
        for (size_t i = 0; i < tracks.size();) {
            Track& t = tracks[i];
            if (!(gone & (uint64_t(1) << t.at))) {
                ++i;
                continue;
            }
            if (!moved || t.moves + 1 > m_q.maxMoves) { // piece captured, or too many moves
                tracks.erase(tracks.begin() + std::ptrdiff_t(i));
                continue;
            }
            t.at = to;
            ++t.moves;
            if (to == m_q.via) t.viaSeen = true;
            if (to == m_q.to && (m_q.via == NoSquare || t.viaSeen)) done = true;
            ++i;
        }
        if (moved && from == m_q.from) { // departure from the origin square: new attempt
            const bool via = to == m_q.via;
            if (to == m_q.to && m_q.via == NoSquare) done = true;
            else if (m_q.maxMoves > 1) tracks.push_back({to, 1, via});
        }
    }
    m_first = false;
    return done;
}

std::vector<uint32_t> searchManeuver(const Database& db, const ManeuverQuery& q, const std::atomic<int32_t>* cancel, const ScanProgress& progress)
{
    if (!q.valid()) return {};
    return scan(db, [&](uint32_t, const std::vector<uint8_t>& blob) {
        ManeuverTracker tracker(q);
        bool found = false;
        replayMainline(blob.data(), blob.size(), [&](const Position& pos, int) {
            found = tracker.feed(pos);
            return !found;
        });
        return found;
    }, cancel, progress);
}

// ---------------------------------------------------------------- pawn structure, material

std::vector<uint32_t> searchPawnStructure(const Database& db, const Position& pos, const std::atomic<int32_t>* cancel, const ScanProgress& progress)
{
    const uint64_t white = squaresOf(pos, makePiece(White, Pawn)), black = squaresOf(pos, makePiece(Black, Pawn));
    const uint16_t missing = PawnSignature::missing(pos);
    const int pawns = __builtin_popcountll(white) + __builtin_popcountll(black);
    return scan(db, [&](uint32_t id, const std::vector<uint8_t>& blob) {
        if (!PawnSignature::compatible(db.record(id).pawns, missing)) return false; // the starting pawns do not leave in that order
        bool found = false;
        replayMainline(blob.data(), blob.size(), [&](const Position& p, int) {
            int count = 0;
            for (int s = 8; s < 56; ++s)
                if (typeOf(p.at(Square(s))) == Pawn) ++count;
            if (count < pawns) return false; // pawns do not come back
            if (count == pawns && squaresOf(p, makePiece(White, Pawn)) == white && squaresOf(p, makePiece(Black, Pawn)) == black) found = true;
            return !found;
        });
        return found;
    }, cancel, progress);
}

std::vector<uint32_t> searchSameMaterial(const Database& db, const Position& pos, const std::atomic<int32_t>* cancel, const ScanProgress& progress)
{
    const auto wanted = materialOf(pos);
    int total = 0;
    for (uint8_t n : wanted) total += n;
    return scan(db, [&](uint32_t id, const std::vector<uint8_t>& blob) {
        const bool promotions = db.record(id).flags & FlagPromotion;
        bool found = false;
        replayMainline(blob.data(), blob.size(), [&](const Position& p, int) {
            const auto m = materialOf(p);
            int count = 0;
            for (uint8_t n : m) count += n;
            if (count < total && !promotions) return false; // without promotion, material only decreases
            if (m == wanted) found = true;
            return !found;
        });
        return found;
    }, cancel, progress);
}

} // namespace hy64db
