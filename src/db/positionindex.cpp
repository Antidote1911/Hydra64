#include "positionindex.h"
#include "database.h"
#include "fileoffset.h"

#include "chess/polyglot.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
using namespace chess;

namespace hy64db {

namespace {

constexpr uint32_t StatsThreshold = 16;     // lists of at least K games: statistics stored
constexpr uint64_t SingleBit = 1ull << 63;

struct Entry {
    uint64_t key;
    uint32_t game;
    bool operator<(const Entry& o) const { return key != o.key ? key < o.key : game < o.game; }
};

unsigned threadCount() { return std::max(1u, std::min(16u, std::thread::hardware_concurrency())); }

// Per-game information useful for the statistics (result, highest Elo, year).
struct GameFacts {
    uint8_t result;
    uint16_t elo, year;
};

} // namespace

// ---------------------------------------------------------------- reading

bool PositionIndex::open(const std::string& path, std::string* error) { return openRange(path, 0, UINT64_MAX, error); }

bool PositionIndex::openRange(const std::string& path, uint64_t offset, uint64_t length, std::string* error)
{
    if (!m_file.openRange(path, offset, length, error)) return false;
    if (!checkHeader(m_file.data(), m_file.size(), PositionsMagic, error)) return false;
    const uint8_t* h = m_file.data();
    m_keys = get64(h + 16);
    m_depth = int(get32(h + 32));
    m_threshold = get32(h + 36);
    m_gamesIndexed = get64(h + 40);
    m_postings = get64(h + 48);
    m_states = get64(h + 56);
    if (HeaderSize + m_keys * 16 > m_file.size() || m_postings > m_file.size() || m_states + m_gamesIndexed * 6 > m_file.size()) {
        if (error) *error = "index des positions abîmé";
        return false;
    }
    return true;
}

bool PositionIndex::stateAtDepth(uint32_t game, uint16_t* missing, uint32_t* material) const
{
    if (!m_states || game >= m_gamesIndexed) return false;
    const uint8_t* p = m_file.data() + m_states + uint64_t(game) * 6;
    *missing = get16(p);
    *material = get32(p + 2);
    return true;
}

bool PositionIndex::lookup(uint64_t key, Stats* stats, std::vector<uint32_t>* games) const
{
    const uint8_t* table = m_file.data() + HeaderSize;
    uint64_t lo = 0, hi = m_keys;
    while (lo < hi) { // binary search in the mapped table
        const uint64_t mid = (lo + hi) / 2;
        if (get64(table + mid * 16) < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= m_keys || get64(table + lo * 16) != key) return false;
    const uint64_t data = get64(table + lo * 16 + 8);
    if (data & SingleBit) {
        if (stats) *stats = Stats{1, 0, 0, 0, 0, 0, false};
        if (games) games->assign(1, uint32_t(data));
        return true;
    }
    const uint8_t* p = m_file.data() + m_postings + data;
    const uint8_t* end = m_file.data() + m_file.size();
    uint64_t n = 0;
    if (!getVarint(p, end, n)) return false;
    Stats s;
    s.games = uint32_t(n);
    if (n >= m_threshold) {
        uint64_t w, d, b, elo, year;
        if (!getVarint(p, end, w) || !getVarint(p, end, d) || !getVarint(p, end, b) || !getVarint(p, end, elo) || !getVarint(p, end, year))
            return false;
        s.whiteWins = uint32_t(w), s.draws = uint32_t(d), s.blackWins = uint32_t(b), s.maxElo = uint16_t(elo), s.year = uint16_t(year);
        s.known = true;
    }
    if (stats) *stats = s;
    if (games) {
        games->clear();
        games->reserve(size_t(n));
        uint64_t id = 0;
        for (uint64_t i = 0; i < n; ++i) {
            uint64_t delta;
            if (!getVarint(p, end, delta)) return false;
            id += delta;
            games->push_back(uint32_t(id));
        }
    }
    return true;
}

// ---------------------------------------------------------------- building

bool buildPositionIndex(Database& db, int depth, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress)
{
    if (db.isPacked()) // single-file database: index built on the unpacked database, then everything packed again
        return db.rebuildPacked([&](Database& plain, std::string* e) { return buildPositionIndex(plain, depth, e, progress); }, error);
    const std::string out = positionsPath(db.base());
    const std::string tmpDir = out + ".tmp";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);
    auto cleanup = [&] { fs::remove_all(tmpDir, ec); };
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        cleanup();
        return false;
    };

    const uint32_t n = db.count();
    std::vector<GameFacts> facts(n);
    std::vector<uint8_t> states(size_t(n) * 6, 0); // state at half-move D, per game
    std::atomic<uint64_t> done{0};
    std::atomic<bool> cancelled{false}, failed{false};

    // 1. Keys of the first D half-moves, distributed by most significant byte into 256 temporary
    //    files: each slice is then sorted on its own, with no general merge.
    constexpr unsigned Buckets = 256;
    constexpr size_t BucketBuffer = 4096; // entries kept per thread and per slice before writing
    struct Bucket {
        std::FILE* f = nullptr;
        std::mutex mutex;
        uint64_t keys = 0, postings = 0; // sizes produced at step 2
    };
    std::vector<Bucket> buckets(Buckets);
    auto bucketPath = [&](unsigned b, const char* kind) { return tmpDir + "/" + kind + std::to_string(b); };
    for (unsigned b = 0; b < Buckets; ++b) {
        buckets[b].f = std::fopen(bucketPath(b, "part").c_str(), "w+b");
        if (!buckets[b].f) failed = true;
    }
    auto closeBuckets = [&] {
        for (Bucket& b : buckets)
            if (b.f) std::fclose(b.f), b.f = nullptr;
    };
    const unsigned t = threadCount();
    if (!failed) {
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < t; ++w) {
            pool.emplace_back([&, w] {
                std::vector<std::vector<Entry>> local(Buckets);
                for (auto& v : local) v.reserve(BucketBuffer);
                std::vector<uint8_t> blob;
                auto flush = [&](unsigned b) {
                    std::vector<Entry>& v = local[b];
                    if (v.empty()) return;
                    std::lock_guard lock(buckets[b].mutex);
                    if (std::fwrite(v.data(), sizeof(Entry), v.size(), buckets[b].f) != v.size()) failed = true;
                    v.clear();
                };
                for (uint32_t id = w; id < n && !cancelled && !failed; id += t) {
                    const Record r = db.record(id);
                    facts[id] = {r.result, std::max(r.whiteElo, r.blackElo), uint16_t(dateYear(r.date))};
                    if (!(r.flags & FlagDeleted) && db.blob(id, blob))
                        replayMainline(blob.data(), blob.size(), [&](const Position& p, int ply) {
                            Entry e{};
                            e.key = polyglot::key(p);
                            e.game = id;
                            const unsigned b = unsigned(e.key >> 56);
                            local[b].push_back(e);
                            if (local[b].size() >= BucketBuffer) flush(b);
                            if (ply == depth) {
                                put16(&states[size_t(id) * 6], PawnSignature::missing(p));
                                put32(&states[size_t(id) * 6 + 2], materialSignature(p));
                            }
                            return ply < depth;
                        });
                    const uint64_t d = ++done;
                    if (w == 0 && progress && d % 8192 < t && !progress(d, uint64_t(n) * 2)) cancelled = true;
                }
                for (unsigned b = 0; b < Buckets; ++b) flush(b);
            });
        }
        for (auto& th : pool) th.join();
    }
    if (cancelled || failed) {
        closeBuckets();
        return fail(cancelled ? "" : "écriture impossible dans " + tmpDir);
    }

    // 2. Each slice, in parallel: sort, then its share of the key table and of its lists
    //    (list offsets relative to the slice, shifted at step 3).
    {
        std::atomic<unsigned> next{0}, finished{0};
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < t; ++w) {
            pool.emplace_back([&, w] {
                std::vector<Entry> buf;
                std::vector<uint32_t> games;
                std::vector<uint8_t> keysOut, postOut;
                for (unsigned b; (b = next++) < Buckets && !cancelled && !failed;) {
                    Bucket& bucket = buckets[b];
                    std::fflush(bucket.f);
                    const int64_t bytes = tell64(bucket.f);
                    buf.resize(bytes > 0 ? size_t(bytes) / sizeof(Entry) : 0);
                    std::rewind(bucket.f);
                    if (!buf.empty() && std::fread(buf.data(), sizeof(Entry), buf.size(), bucket.f) != buf.size()) failed = true;
                    std::fclose(bucket.f);
                    bucket.f = nullptr;
                    std::error_code rm;
                    fs::remove(bucketPath(b, "part"), rm);
                    std::sort(buf.begin(), buf.end());
                    keysOut.clear();
                    postOut.clear();
                    auto emit = [&](uint64_t key) {
                        uint8_t entry[16];
                        put64(entry, key);
                        if (games.size() == 1) {
                            put64(entry + 8, SingleBit | games[0]);
                        } else {
                            put64(entry + 8, postOut.size());
                            putVarint(postOut, games.size());
                            if (games.size() >= StatsThreshold) {
                                uint32_t ww = 0, d = 0, bl = 0;
                                uint16_t elo = 0, year = 0;
                                for (uint32_t g : games) {
                                    const GameFacts& f = facts[g];
                                    ww += f.result == WhiteWins, d += f.result == Draw, bl += f.result == BlackWins;
                                    elo = std::max(elo, f.elo), year = std::max(year, f.year);
                                }
                                putVarint(postOut, ww), putVarint(postOut, d), putVarint(postOut, bl), putVarint(postOut, elo), putVarint(postOut, year);
                            }
                            uint32_t prev = 0;
                            for (uint32_t g : games) {
                                putVarint(postOut, g - prev);
                                prev = g;
                            }
                        }
                        keysOut.insert(keysOut.end(), entry, entry + 16);
                        games.clear();
                    };
                    games.clear();
                    for (size_t i = 0; i < buf.size(); ++i) {
                        if (i > 0 && buf[i].key != buf[i - 1].key) emit(buf[i - 1].key);
                        if (games.empty() || games.back() != buf[i].game) games.push_back(buf[i].game);
                    }
                    if (!buf.empty()) emit(buf.back().key);
                    for (auto [kind, data] : {std::pair<const char*, const std::vector<uint8_t>*>{"keys", &keysOut}, {"post", &postOut}}) {
                        std::FILE* f = std::fopen(bucketPath(b, kind).c_str(), "wb");
                        if (!f || std::fwrite(data->data(), 1, data->size(), f) != data->size()) failed = true;
                        if (f && std::fclose(f) != 0) failed = true;
                    }
                    bucket.keys = keysOut.size() / 16;
                    bucket.postings = postOut.size();
                    const unsigned f = ++finished;
                    if (w == 0 && progress && !progress(uint64_t(n) + uint64_t(n) * f / Buckets, uint64_t(n) * 2)) cancelled = true;
                }
            });
        }
        for (auto& th : pool) th.join();
    }
    closeBuckets();
    if (cancelled) return fail("");
    if (failed) return fail("écriture impossible dans " + tmpDir);

    // 3. Final file: key table (slices in order, list offsets shifted),
    //    lists, state at D, then the header.
    std::FILE* outF = std::fopen((out + ".new").c_str(), "wb");
    if (!outF) return fail("création impossible de " + out);
    std::setvbuf(outF, nullptr, _IOFBF, 1 << 20);
    uint8_t header[HeaderSize] = {};
    std::fwrite(header, 1, HeaderSize, outF);
    uint64_t keys = 0;
    bool ok = true;
    auto readAll = [&](const std::string& path, std::vector<uint8_t>& data, uint64_t size) {
        data.resize(size_t(size));
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f || (size && std::fread(data.data(), 1, data.size(), f) != data.size())) ok = false;
        if (f) std::fclose(f);
    };
    std::vector<uint8_t> chunk;
    uint64_t postBase = 0;
    for (unsigned b = 0; b < Buckets && ok; ++b) {
        readAll(bucketPath(b, "keys"), chunk, buckets[b].keys * 16);
        if (postBase)
            for (size_t i = 0; i + 16 <= chunk.size(); i += 16)
                if (const uint64_t v = get64(&chunk[i + 8]); !(v & SingleBit)) put64(&chunk[i + 8], v + postBase);
        ok = ok && std::fwrite(chunk.data(), 1, chunk.size(), outF) == chunk.size();
        keys += buckets[b].keys;
        postBase += buckets[b].postings;
    }
    const uint64_t postingsAt = HeaderSize + keys * 16;
    for (unsigned b = 0; b < Buckets && ok; ++b) {
        readAll(bucketPath(b, "post"), chunk, buckets[b].postings);
        ok = ok && std::fwrite(chunk.data(), 1, chunk.size(), outF) == chunk.size();
    }
    const uint64_t statesAt = postingsAt + postBase;
    ok = ok && std::fwrite(states.data(), 1, states.size(), outF) == states.size();
    writeHeader(header, PositionsMagic, keys, 0);
    put32(header + 32, uint32_t(depth));
    put32(header + 36, StatsThreshold);
    put64(header + 40, n);
    put64(header + 48, postingsAt);
    put64(header + 56, statesAt);
    seek64(outF, 0, SEEK_SET);
    std::fwrite(header, 1, HeaderSize, outF);
    if (std::fclose(outF) != 0 || !ok) {
        fs::remove(out + ".new", ec);
        return fail("écriture impossible de " + out);
    }
    db.reloadPositions(); // releases the old file before replacing it
    fs::rename(out + ".new", out, ec);
    cleanup();
    if (ec) {
        if (error) *error = out + " : " + ec.message();
        return false;
    }
    db.reloadPositions(error);
    return true;
}

// ---------------------------------------------------------------- queries

namespace {

PositionIndex::Stats statsOf(const Database& db, const PositionIndex& index, uint64_t key, bool* found)
{
    PositionIndex::Stats s;
    std::vector<uint32_t> games;
    *found = index.lookup(key, &s, &games);
    if (!*found || s.known) return s;
    for (uint32_t id : games) {
        const Record r = db.record(id);
        if (r.flags & FlagDeleted) {
            --s.games;
            continue;
        }
        s.whiteWins += r.result == WhiteWins, s.draws += r.result == Draw, s.blackWins += r.result == BlackWins;
        s.maxElo = std::max({s.maxElo, r.whiteElo, r.blackElo});
        s.year = std::max(s.year, uint16_t(dateYear(r.date)));
    }
    return s;
}

} // namespace

std::vector<uint32_t> searchPosition(const Database& db, const Position& pos, const std::atomic<int32_t>* cancel)
{
    const uint64_t key = polyglot::key(pos);
    const PositionIndex* index = db.positions();
    std::vector<uint32_t> hits;
    const uint32_t n = db.count();
    uint64_t indexed = 0;
    int depth = 0;
    if (index) {
        index->lookup(key, nullptr, &hits);
        indexed = std::min<uint64_t>(index->gamesIndexed(), n);
        depth = index->depth();
    }
    // Filters for the replays: material (without promotion, nothing reappears) and starting pawns.
    const uint32_t material = materialSignature(pos);
    const uint16_t missing = PawnSignature::missing(pos);
    auto counts = [](uint32_t sig, int c) {
        const uint32_t s = sig >> (12 * c);
        return std::array<int, 5>{int(s & 3), int(s >> 2 & 3), int(s >> 4 & 3), int(s >> 6 & 3), int(s >> 8 & 15)};
    };
    const auto want0 = counts(material, 0), want1 = counts(material, 1);
    auto possible = [&](const Record& r) {
        if (r.flags & FlagDeleted) return false;
        if (!PawnSignature::compatible(r.pawns, missing)) return false;
        if (!(r.flags & FlagPromotion)) {
            const auto end0 = counts(r.material, 0), end1 = counts(r.material, 1);
            for (int i = 0; i < 5; ++i)
                if (end0[i] > want0[i] || end1[i] > want1[i]) return false; // pieces gone before the end
        }
        return true;
    };

    int wantPieces = 0;
    for (Square s = 0; s < 64; ++s) wantPieces += pos.at(s) != NoPiece;

    const unsigned t = threadCount();
    std::vector<std::vector<uint32_t>> found(t);
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < t; ++w) {
        pool.emplace_back([&, w] {
            std::vector<uint8_t> blob;
            for (uint32_t id = w; id < n; id += t) {
                if ((id & 0x3FFF) < t && cancel && cancel->load()) return;
                const Record r = db.record(id);
                const bool beyond = id < indexed; // indexed game: only the positions after D remain to be looked at
                if (beyond) {
                    if (r.plies <= depth) continue;
                    // At half-move D, some starting pawns had already left and some pieces had already been
                    // captured: the searched position can only come later if it has lost them too.
                    uint16_t gone = 0;
                    uint32_t mat = 0;
                    if (index->stateAtDepth(id, &gone, &mat)) {
                        if (gone & ~missing) continue;
                        if (!(r.flags & FlagPromotion)) {
                            const auto at0 = counts(mat, 0), at1 = counts(mat, 1);
                            bool more = false;
                            for (int i = 0; i < 5; ++i) more = more || want0[i] > at0[i] || want1[i] > at1[i];
                            if (more) continue;
                        }
                    }
                }
                if (!possible(r) || !db.blob(id, blob)) continue;
                bool hit = false;
                replayMainline(blob.data(), blob.size(), [&](const Position& p, int ply) {
                    if (beyond && ply <= depth) return true;
                    // The number of pieces never goes back up (a promotion keeps it): below that of
                    // the searched position, the game can no longer go through it; above,
                    // no need to compute the key.
                    int pieces = 0;
                    for (Square s = 0; s < 64; ++s) pieces += p.at(s) != NoPiece;
                    if (pieces < wantPieces) return false;
                    if (pieces > wantPieces) return true;
                    if (polyglot::key(p) == key) {
                        hit = true;
                        return false;
                    }
                    return true;
                });
                if (hit) found[w].push_back(id);
            }
        });
    }
    for (auto& th : pool) th.join();
    for (auto& f : found) hits.insert(hits.end(), f.begin(), f.end());
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
    hits.erase(std::remove_if(hits.begin(), hits.end(), [&](uint32_t id) { return db.record(id).flags & FlagDeleted; }), hits.end());
    return hits;
}

std::vector<treebook::BookMove> positionTree(const Database& db, const Position& pos)
{
    std::vector<treebook::BookMove> out;
    const PositionIndex* index = db.positions();
    if (!index) return out;
    const bool white = pos.sideToMove() == White;
    for (const Move& m : pos.legalMoves()) {
        bool found = false;
        const PositionIndex::Stats s = statsOf(db, *index, polyglot::key(pos.after(m)), &found);
        if (!found || s.games == 0) continue;
        treebook::BookMove bm;
        bm.move = m;
        bm.wins = white ? s.whiteWins : s.blackWins;
        bm.losses = white ? s.blackWins : s.whiteWins;
        bm.draws = s.draws;
        // games without a result: counted as draws so that the total stays right
        const uint32_t decided = s.whiteWins + s.blackWins + s.draws;
        if (s.games > decided) bm.draws += s.games - decided;
        bm.maxElo = s.maxElo;
        bm.year = s.year;
        out.push_back(bm);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.games() > b.games(); });
    return out;
}

} // namespace hy64db
