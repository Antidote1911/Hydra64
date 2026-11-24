#include "gametree.h"

#include "chess/polyglot.h"
#include "database.h"
#include "gamecodec.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace hy64db {

using namespace chess;

namespace {

enum Outcome : uint8_t { Win, DrawGame, Loss, Unknown }; // for the side playing the move

// One move of one game.
struct Raw {
    uint64_t key; // position before the move
    uint16_t from : 6, to : 6, promo : 3, castling : 1;
    uint16_t year;
    uint16_t elo; // of the player of the move
    uint8_t outcome;

    uint32_t moveCode() const { return uint32_t(from) | uint32_t(to) << 6 | uint32_t(promo) << 12; }
};

// Statistics of a move in a position, over all the games.
struct Stat {
    uint64_t key = 0;
    uint32_t wins = 0, draws = 0, losses = 0, unknown = 0;
    uint16_t move = 0; // moveCode()
    uint16_t elo = 0, year = 0;
    uint16_t castling = 0;

    uint64_t games() const { return uint64_t(wins) + draws + losses + unknown; }
    bool sameMove(const Stat& o) const { return key == o.key && move == o.move; }
    bool operator<(const Stat& o) const { return key != o.key ? key < o.key : move < o.move; }
    void add(const Stat& o)
    {
        wins += o.wins, draws += o.draws, losses += o.losses, unknown += o.unknown;
        elo = std::max(elo, o.elo);
        year = std::max(year, o.year);
    }
};

#ifndef HYDRA64_GAMETREE_CHUNK
#define HYDRA64_GAMETREE_CHUNK (size_t(48) << 20) // 48 M moves (768 MB) sorted at a time
#endif
constexpr size_t ChunkMoves = HYDRA64_GAMETREE_CHUNK;

unsigned threadCount() { return std::max(1u, std::min(16u, std::thread::hardware_concurrency())); }

// Sorts the moves of a chunk and adds up those of the same position and move.
std::vector<Stat> aggregate(std::vector<Raw>& chunk)
{
    std::sort(chunk.begin(), chunk.end(), [](const Raw& a, const Raw& b) { return a.key != b.key ? a.key < b.key : a.moveCode() < b.moveCode(); });
    std::vector<Stat> out;
    for (const Raw& r : chunk) {
        const uint16_t move = uint16_t(r.moveCode());
        if (out.empty() || out.back().key != r.key || out.back().move != move) {
            out.emplace_back();
            out.back().key = r.key;
            out.back().move = move;
            out.back().castling = r.castling;
        }
        Stat& s = out.back();
        switch (r.outcome) {
        case Win: ++s.wins; break;
        case DrawGame: ++s.draws; break;
        case Loss: ++s.losses; break;
        default: ++s.unknown; break;
        }
        s.elo = std::max(s.elo, r.elo);
        s.year = std::max(s.year, r.year);
    }
    chunk.clear();
    return out;
}

bool writeRun(const std::vector<Stat>& stats, const std::string& name)
{
    std::ofstream f(name, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(stats.data()), std::streamsize(stats.size() * sizeof(Stat)));
    return bool(f);
}

// Sorted run read back in order.
struct RunReader {
    std::ifstream f;
    std::vector<Stat> buf;
    size_t at = 0;
    bool next(Stat& out)
    {
        if (at == buf.size()) {
            buf.resize(1 << 16);
            f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size() * sizeof(Stat)));
            buf.resize(size_t(f.gcount()) / sizeof(Stat));
            at = 0;
            if (buf.empty()) return false;
        }
        out = buf[at++];
        return true;
    }
};

// Merges the sorted runs (same position and move added up): `emit` for each move, in order.
bool mergeRuns(const std::vector<std::string>& runs, const std::function<bool(const Stat&)>& emit)
{
    std::vector<std::unique_ptr<RunReader>> readers;
    std::vector<Stat> heads(runs.size());
    const auto later = [&](size_t a, size_t b) { return heads[b] < heads[a]; };
    std::priority_queue<size_t, std::vector<size_t>, decltype(later)> heap(later);
    for (size_t i = 0; i < runs.size(); ++i) {
        readers.push_back(std::make_unique<RunReader>());
        readers[i]->f.open(runs[i], std::ios::binary);
        if (!readers[i]->f) return false;
        if (readers[i]->next(heads[i])) heap.push(i);
    }
    Stat cur;
    bool have = false;
    while (!heap.empty()) {
        const size_t i = heap.top();
        heap.pop();
        if (have && cur.sameMove(heads[i])) cur.add(heads[i]);
        else {
            if (have && !emit(cur)) return false;
            cur = heads[i];
            have = true;
        }
        if (readers[i]->next(heads[i])) heap.push(i);
    }
    return !have || emit(cur);
}

// Replays the games in parallel and gathers their moves into `chunk` (in memory) or, beyond
// ChunkMoves, into sorted runs on disk. `shared` (sorted keys): each game is cut after its last
// position found there.
struct Collector {
    const Database& db;
    const GameTreeOptions& options;
    std::string prefix; // of the temporary files
    int countPlies = 0; // positionsOnly: half-moves of each game read
    std::vector<Raw> chunk{};
    std::vector<std::string> runs{};
    std::atomic<uint64_t> used{0};
    bool writeFailed = false, cancelled = false;

    ~Collector() { removeRuns(); }
    void removeRuns()
    {
        std::error_code ec;
        for (const std::string& r : runs) std::filesystem::remove(r, ec);
        runs.clear();
    }
    void flushChunk()
    {
        const std::string name = prefix + std::to_string(runs.size()) + ".tmp";
        runs.push_back(name);
        if (!writeRun(aggregate(chunk), name)) writeFailed = true;
    }

    // `positionsOnly`: one entry per position visited (the starting one and after each move, as a
    // null move), to count the visits. Otherwise the moves; `shared` (sorted keys) then cuts each
    // game one move after its last position found there. `progress(games done, games in all)`;
    // false cancels.
    bool collect(bool positionsOnly, const std::vector<uint64_t>* shared, const std::function<bool(uint64_t, uint64_t)>& progress)
    {
        const int maxPlies = std::max(1, options.maxPlies);
        const uint64_t total = options.games ? options.games->size() : db.count();
        const uint64_t startKey = polyglot::key(Position::startPos());
        std::mutex mutex;
        std::atomic<uint64_t> next{0};
        std::atomic<bool> stop{false};
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threadCount(); ++t) {
            pool.emplace_back([&] {
                std::vector<Raw> local, game;
                std::vector<uint8_t> blob;
                const auto hand = [&] {
                    std::lock_guard<std::mutex> lock(mutex);
                    chunk.insert(chunk.end(), local.begin(), local.end());
                    local.clear();
                    if (chunk.size() >= ChunkMoves) {
                        flushChunk();
                        if (writeFailed) stop = true;
                    }
                };
                const auto isShared = [&](uint64_t key) { return !shared || std::binary_search(shared->begin(), shared->end(), key); };
                constexpr uint64_t Block = 512;
                for (uint64_t i; !stop && (i = next.fetch_add(Block)) < total;) {
                    for (uint64_t j = i; j < std::min(i + Block, total); ++j) {
                        const uint32_t id = options.games ? (*options.games)[j] : uint32_t(j);
                        if (id >= db.count()) continue;
                        const Record r = db.record(id);
                        if (r.deleted() || !db.blob(id, blob)) continue;
                        if (positionsOnly) {
                            replayMainline(blob.data(), blob.size(), [&](const Position& pos, int ply) {
                                Raw raw{};
                                raw.key = polyglot::key(pos);
                                if (ply == 0 && raw.key != startKey) return false; // other starting positions: no opening tree
                                raw.outcome = Unknown;
                                local.push_back(raw);
                                return ply < countPlies;
                            });
                            continue;
                        }
                        const uint16_t year = uint16_t(dateYear(r.date));
                        game.clear();
                        size_t keep = 0; // moves of the game that go into the tree
                        Position lastPos;
                        Move lastMove;
                        replayMainlineMoves(blob.data(), blob.size(), [&](const Position& pos, Move m, int ply) {
                            const uint64_t key = polyglot::key(pos);
                            if (ply == 1 && key != startKey) return false;
                            const bool white = pos.sideToMove() == White;
                            Raw raw{};
                            raw.key = key;
                            raw.from = uint16_t(m.from), raw.to = uint16_t(m.to), raw.promo = uint16_t(m.promo), raw.castling = pos.isCastling(m);
                            raw.year = year;
                            raw.elo = white ? r.whiteElo : r.blackElo;
                            raw.outcome = r.result == Draw ? DrawGame : r.result == WhiteWins ? (white ? Win : Loss) : r.result == BlackWins ? (white ? Loss : Win) : Unknown;
                            game.push_back(raw);
                            if (isShared(key)) keep = game.size(); // the move played in a shared position
                            if (shared) lastPos = pos, lastMove = m;
                            return ply < maxPlies;
                        });
                        // The last position reached is shared: nothing to cut
                        if (shared && keep < game.size() && isShared(polyglot::key(lastPos.after(lastMove)))) keep = game.size();
                        local.insert(local.end(), game.begin(), game.begin() + std::ptrdiff_t(keep));
                        if (keep) ++used;
                    }
                    if (local.size() >= (1u << 18)) hand();
                    if (progress) {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (!stop && !progress(std::min(total, i + Block), total)) cancelled = true, stop = true;
                    }
                }
                hand();
            });
        }
        for (auto& th : pool) th.join();
        return !cancelled && !writeFailed;
    }

    // The moves gathered, positions and moves in order, same ones added up. False: write failure,
    // or `emit` returned false.
    bool forEach(const std::function<bool(const Stat&)>& emit)
    {
        if (runs.empty()) {
            for (const Stat& s : aggregate(chunk))
                if (!emit(s)) return false;
            return true;
        }
        if (!chunk.empty()) flushChunk();
        std::vector<Raw>().swap(chunk);
        return !writeFailed && mergeRuns(runs, emit);
    }
};

} // namespace

bool buildGameTree(const Database& db, const std::string& output, treebook::TreeFormat format, const GameTreeOptions& options, GameTreeResult* result,
                   std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress)
{
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const uint32_t minGames = std::max<uint32_t>(1, options.minGames);
    constexpr uint64_t Scale = 1000; // progress: the games count for 80 %, the writing for the rest
    const uint64_t passes = minGames > 1 ? 2 : 1;
    const auto step = [&](uint64_t pass) {
        return [&progress, pass, passes](uint64_t done, uint64_t total) {
            return !progress || progress((pass * Scale * 8 / 10 + done * (Scale * 8 / 10) / std::max<uint64_t>(1, total)) / passes, Scale);
        };
    };

    // ---- 1. Positions visited at least `minGames` times (a first reading of the games)
    std::vector<uint64_t> shared;
    if (minGames > 1) {
        Collector all{db, options, output + ".all"};
        // Visits are counted over the whole game, not only the depth of the tree: a position of the
        // opening that comes back later in the same game (repetition) counts as shared. That is what
        // ChessOK trees do (checked on the tree of their 2012 correspondence database: same moves).
        all.countPlies = std::numeric_limits<int>::max();
        if (!all.collect(true, nullptr, step(0))) return fail(all.cancelled ? "cancelled" : "write");
        uint64_t key = 0, games = 0;
        bool have = false;
        const auto close = [&] {
            if (have && games >= minGames) shared.push_back(key);
        };
        const bool ok = all.forEach([&](const Stat& s) {
            if (!have || s.key != key) {
                close();
                key = s.key, games = 0, have = true;
            }
            games += s.games();
            return true;
        });
        if (!ok) return fail("write");
        close();
        if (shared.empty()) return fail("empty");
    }

    // ---- 2. Moves of the games, each game up to one move after its last shared position
    Collector kept{db, options, output + ".moves"};
    if (!kept.collect(false, minGames > 1 ? &shared : nullptr, step(passes - 1))) return fail(kept.cancelled ? "cancelled" : "write");
    std::vector<uint64_t>().swap(shared);

    // ---- 3. The book
    treebook::BookWriter writer(output, format);
    uint64_t positions = 0, lastKey = 0, moves = 0;
    bool writerFailed = false;
    const bool ok = kept.forEach([&](const Stat& s) {
        treebook::BookMove bm;
        bm.move = Move{Square(s.move & 63), Square((s.move >> 6) & 63), PieceType(s.move >> 12)};
        bm.wins = s.wins, bm.draws = s.draws, bm.losses = s.losses; // games without a result: in none of the three
        bm.maxElo = s.elo;
        bm.year = s.year;
        if (moves == 0 || s.key != lastKey) ++positions;
        lastKey = s.key;
        ++moves;
        if (!writer.add(s.key, bm, s.castling != 0)) writerFailed = true;
        return !writerFailed;
    });
    if (!ok) return fail("write");
    if (moves == 0) return fail("empty");
    kept.removeRuns();
    if (progress && !progress(Scale * 9 / 10, Scale)) return fail("cancelled");
    uint64_t written = 0;
    if (!writer.finish(&written)) return fail("write");
    if (progress) progress(Scale, Scale);
    if (result) {
        result->games = kept.used;
        result->positions = positions;
        result->moves = written;
    }
    return true;
}

} // namespace hy64db
