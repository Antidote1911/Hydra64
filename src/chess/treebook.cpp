#include "treebook.h"
#include "aquarium.h"
#include "ctg.h"
#include "polyglot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <queue>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace chess::treebook {

namespace {

constexpr char Magic[8] = {'B', '6', '4', 'B', 'O', 'O', 'K', '1'};
constexpr int HeaderSize = 32;
constexpr int EntrySize = 32;
#ifndef HYDRA64_TREEBOOK_RUN_ENTRIES
#define HYDRA64_TREEBOOK_RUN_ENTRIES (size_t(4) << 20) // 4 M entries (128 MB) per sort run
#endif
constexpr size_t RunEntries = HYDRA64_TREEBOOK_RUN_ENTRIES;

constexpr uint32_t Version = 2;

// Marks (byte 15)
constexpr uint8_t FlagStats = 1;       // statistics present (version 2)
constexpr uint8_t FlagGameCount = 2;   // number of games in 10–13 (instead of the year and the highest Elo)
constexpr uint8_t FlagCastle = 4;      // the move is a castling move (useful to write it in Polyglot)
constexpr uint8_t FlagEval = 8;        // evaluation (28–29) and depth (30–31) instead of the average Elo and the performance
constexpr uint8_t RecommendationMask = 64 | 128;

struct Entry {
    uint64_t key = 0;
    uint16_t move = 0; // target file | rank << 3 | source file << 6 | rank << 9 | promotion << 12
    uint16_t year = 0;
    uint16_t elo = 0;
    uint8_t annotation = 0;
    uint8_t flags = FlagStats;
    uint32_t wins = 0, draws = 0, losses = 0;
    uint32_t gameCount = 0; // with FlagGameCount
    uint16_t averageElo = 0, performance = 0;

    uint64_t games() const { return (flags & FlagGameCount) ? gameCount : uint64_t(wins) + draws + losses; }
};

// File order: increasing key, then decreasing games.
bool entryLess(const Entry& a, const Entry& b)
{
    if (a.key != b.key) return a.key < b.key;
    if (a.games() != b.games()) return a.games() > b.games();
    return a.move < b.move;
}

void putLe(uint8_t* p, uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i) p[i] = uint8_t(v >> (8 * i));
}

uint64_t getLe(const uint8_t* p, int bytes)
{
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

void encode(const Entry& e, uint8_t* p)
{
    std::memset(p, 0, EntrySize);
    putLe(p, e.key, 8);
    putLe(p + 8, e.move, 2);
    if (e.flags & FlagGameCount) putLe(p + 10, e.gameCount, 4);
    else {
        putLe(p + 10, e.year, 2);
        putLe(p + 12, e.elo, 2);
    }
    p[14] = e.annotation;
    p[15] = e.flags;
    putLe(p + 16, e.wins, 4);
    putLe(p + 20, e.draws, 4);
    putLe(p + 24, e.losses, 4);
    putLe(p + 28, e.averageElo, 2);
    putLe(p + 30, e.performance, 2);
}

Entry decode(const uint8_t* p, uint32_t version = Version)
{
    Entry e;
    e.key = getLe(p, 8);
    e.move = uint16_t(getLe(p + 8, 2));
    e.annotation = p[14];
    e.flags = version >= 2 ? p[15] : FlagStats; // version 1: statistics always present
    if (e.flags & FlagGameCount) e.gameCount = uint32_t(getLe(p + 10, 4));
    else {
        e.year = uint16_t(getLe(p + 10, 2));
        e.elo = uint16_t(getLe(p + 12, 2));
    }
    e.wins = uint32_t(getLe(p + 16, 4));
    e.draws = uint32_t(getLe(p + 20, 4));
    e.losses = uint32_t(getLe(p + 24, 4));
    e.averageElo = uint16_t(getLe(p + 28, 2));
    e.performance = uint16_t(getLe(p + 30, 2));
    return e;
}

uint16_t packMove(Move m)
{
    static constexpr int promoCode[] = {0, 0, 1, 2, 3, 4, 0}; // NoType, Pawn, Knight, Bishop, Rook, Queen, King
    return uint16_t(fileOf(m.to) | rankOf(m.to) << 3 | fileOf(m.from) << 6 | rankOf(m.from) << 9 | promoCode[m.promo] << 12);
}

Move unpackMove(uint16_t raw)
{
    static constexpr PieceType promos[] = {NoType, Knight, Bishop, Rook, Queen, NoType, NoType, NoType};
    return {makeSquare((raw >> 6) & 7, (raw >> 9) & 7), makeSquare(raw & 7, (raw >> 3) & 7), promos[(raw >> 12) & 7]};
}

bool parseUint(const std::string& s, uint64_t& out)
{
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + uint64_t(c - '0');
    }
    out = v;
    return true;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

// Aquarium EPD (4 fields) → position; the move counters are added.
bool parseEpd(const std::string& epd, Position& pos)
{
    std::istringstream in(epd);
    std::string board, stm, castling, ep;
    if (!(in >> board >> stm >> castling >> ep)) return false;
    return pos.setFen(board + ' ' + stm + ' ' + castling + ' ' + ep + " 0 1");
}

uint32_t clamp32(uint64_t v) { return uint32_t(std::min<uint64_t>(v, 0xFFFFFFFFu)); }

bool writeHeader(std::ostream& out, uint64_t count)
{
    uint8_t h[HeaderSize] = {};
    std::memcpy(h, Magic, 8);
    putLe(h + 8, Version, 4);   // version
    putLe(h + 12, EntrySize, 4);
    putLe(h + 16, count, 8);
    out.write(reinterpret_cast<const char*>(h), HeaderSize);
    return bool(out);
}

} // namespace

// ---------------------------------------------------------------- Book

bool Book::open(const std::string& path, std::string* error)
{
    close();
    m_file.open(path, std::ios::binary);
    uint8_t h[HeaderSize];
    if (!m_file || !m_file.read(reinterpret_cast<char*>(h), HeaderSize)) {
        close();
        if (error) *error = "unreadable";
        return false;
    }
    m_file.seekg(0, std::ios::end);
    const auto size = static_cast<uint64_t>(m_file.tellg());
    const uint64_t count = getLe(h + 16, 8);
    m_version = uint32_t(getLe(h + 8, 4));
    if (std::memcmp(h, Magic, 8) != 0 || getLe(h + 12, 4) != EntrySize || size != HeaderSize + count * EntrySize || m_version > Version) {
        close();
        if (error) *error = "invalid";
        return false;
    }
    m_count = count;
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

std::vector<BookMove> Book::moves(const Position& pos)
{
    std::vector<BookMove> out;
    if (!isOpen()) return out;
    const uint64_t key = polyglot::key(pos);
    auto read = [&](uint64_t i, Entry& e) {
        uint8_t buf[EntrySize];
        m_file.clear();
        m_file.seekg(std::streamoff(HeaderSize + i * EntrySize));
        if (!m_file.read(reinterpret_cast<char*>(buf), EntrySize)) return false;
        e = decode(buf, m_version);
        return true;
    };
    uint64_t lo = 0, hi = m_count;
    Entry e;
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        if (!read(mid, e)) return out;
        if (e.key < key) lo = mid + 1;
        else hi = mid;
    }
    for (uint64_t i = lo; i < m_count && read(i, e) && e.key == key; ++i) {
        const Move m = unpackMove(e.move);
        if (!pos.isLegal(m)) continue; // key collision
        BookMove bm;
        bm.move = m;
        bm.wins = e.wins;
        bm.draws = e.draws;
        bm.losses = e.losses;
        bm.maxElo = e.elo;
        bm.year = e.year;
        bm.annotation = e.annotation;
        bm.recommendation = e.flags & RecommendationMask;
        if (e.flags & FlagEval) {
            bm.hasEval = true;
            bm.eval = int16_t(e.averageElo);
            bm.evalDepth = e.performance;
        } else {
            bm.averageElo = e.averageElo;
            bm.performance = e.performance;
        }
        bm.hasStats = e.flags & FlagStats;
        if (e.flags & FlagGameCount) bm.gameCount = e.gameCount;
        out.push_back(bm);
    }
    return out; // already sorted by decreasing games
}

// ---------------------------------------------------------------- Conversion

namespace {

// Sorted run: the entries in memory are sorted then written to a temporary file.
bool flushRun(std::vector<Entry>& chunk, const std::string& output, std::vector<std::string>& runs)
{
    if (chunk.empty()) return true;
    std::sort(chunk.begin(), chunk.end(), entryLess);
    const std::string name = output + ".run" + std::to_string(runs.size()) + ".tmp";
    std::ofstream f(name, std::ios::binary | std::ios::trunc);
    runs.push_back(name);
    std::vector<uint8_t> buf(chunk.size() * EntrySize);
    for (size_t i = 0; i < chunk.size(); ++i) encode(chunk[i], &buf[i * EntrySize]);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    chunk.clear();
    return bool(f);
}

void removeRuns(const std::vector<std::string>& runs)
{
    std::error_code ec;
    for (const std::string& r : runs) std::filesystem::remove(r, ec);
}

// Merge of the sorted runs into `output` (key + move duplicates: the most played is kept).
bool mergeRuns(const std::vector<std::string>& runs, const std::string& output, uint64_t& written, TreeFormat format = TreeFormat::Hy64Book);

} // namespace

bool looksLikeAquariumExport(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string line;
    if (!std::getline(in, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto fields = split(line, '\t');
    Position pos;
    if (fields.size() < 2 || !parseEpd(fields[0], pos)) return false;
    return std::any_of(fields.begin() + 1, fields.end(), [](const std::string& f) { return split(f, '|').size() >= 7; });
}

bool convertAquariumExport(const std::string& input, const std::string& output, ConvertResult* result, std::string* error,
                           const std::function<bool(uint64_t, uint64_t)>& progress)
{
    namespace fs = std::filesystem;
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    std::ifstream in(input, std::ios::binary);
    if (!in) return fail("unreadable");
    in.seekg(0, std::ios::end);
    const auto total = static_cast<uint64_t>(in.tellg());
    in.seekg(0);

    ConvertResult res;
    std::vector<std::string> runs;
    auto cleanup = [&] {
        std::error_code ec;
        for (const std::string& r : runs) fs::remove(r, ec);
    };
    std::vector<Entry> chunk;
    chunk.reserve(std::min<size_t>(RunEntries, 1 << 16));
    auto flush = [&]() -> bool { return flushRun(chunk, output, runs); };

    // 1. Reading: one sorted run per slice of RunEntries entries
    std::string line;
    uint64_t lineNo = 0;
    while (std::getline(in, line)) {
        if (++lineNo % 4096 == 1 && progress) { // from the first line, then every 4096
            const auto pos = in.tellg();
            if (!progress(pos < 0 ? total : uint64_t(pos), total)) {
                cleanup();
                return fail("cancelled");
            }
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto fields = split(line, '\t');
        Position pos;
        if (fields.size() < 2 || !parseEpd(fields[0], pos)) {
            ++res.skipped;
            continue;
        }
        ++res.positions;
        const uint64_t key = polyglot::key(pos);
        const std::vector<Move> legal = pos.legalMoves(); // once per position
        auto findUci = [&](const std::string& u) -> Move {
            if (u.size() < 4) return {};
            const Square from = parseSquare(std::string_view(u).substr(0, 2)), to = parseSquare(std::string_view(u).substr(2, 2));
            PieceType promo = NoType;
            if (u.size() > 4) {
                switch (u[4] | 0x20) {
                case 'q': promo = Queen; break;
                case 'r': promo = Rook; break;
                case 'b': promo = Bishop; break;
                case 'n': promo = Knight; break;
                default: break;
                }
            }
            for (const Move& lm : legal)
                if (lm.from == from && lm.to == to && lm.promo == promo) return lm;
            return {};
        };
        // The 2nd field may be the number of moves (diagnostics): only "move|…" fields count.
        for (size_t i = 1; i < fields.size(); ++i) {
            const auto parts = split(fields[i], '|');
            if (parts.size() < 7) continue;
            uint64_t w, d, l, elo, year;
            Move m = findUci(parts[0]);
            if (m.isNull()) { // promotion without suffix…: SAN notation
                m = pos.parseSan(parts[1]);
                if (!m.isNull() && !pos.isLegal(m)) m = {};
            }
            if (m.isNull() || !parseUint(parts[2], w) || !parseUint(parts[3], d) || !parseUint(parts[4], l)) {
                ++res.skipped;
                continue;
            }
            Entry e;
            e.key = key;
            e.move = packMove(m);
            e.wins = clamp32(w);
            e.draws = clamp32(d);
            e.losses = clamp32(l);
            e.elo = parseUint(parts[5], elo) ? uint16_t(std::min<uint64_t>(elo, 0xFFFF)) : 0;
            e.year = parseUint(parts[6], year) ? uint16_t(std::min<uint64_t>(year, 0xFFFF)) : 0;
            chunk.push_back(e);
            if (chunk.size() >= RunEntries && !flush()) {
                cleanup();
                return fail("write");
            }
        }
    }
    if (!flush()) {
        cleanup();
        return fail("write");
    }

    // 2. Merge of the sorted runs (key + move duplicates: the most played is kept)
    uint64_t written = 0;
    const bool merged = mergeRuns(runs, output, written);
    cleanup();
    if (!merged) return fail("write");
    res.moves = written;
    if (progress) progress(total, total);
    if (result) *result = res;
    return true;
}

namespace {

// Move of the entry → Polyglot code (castling is written there king onto its rook's square).
uint16_t polyglotMove(const Entry& e)
{
    uint16_t m = uint16_t(e.move & 0x0FFF);
    if (e.flags & FlagCastle) {
        const int toFile = e.move & 7, toRank = (e.move >> 3) & 7;
        m = uint16_t((m & ~0x3F) | (toFile == 6 ? 7 : 0) | toRank << 3);
    }
    static constexpr int promo[] = {0, 1, 2, 3, 4, 0, 0, 0}; // packMove codes → Polyglot (knight 1 … queen 4)
    return uint16_t(m | promo[(e.move >> 12) & 7] << 12);
}

bool mergeRuns(const std::vector<std::string>& runs, const std::string& output, uint64_t& written, TreeFormat format)
{
    namespace fs = std::filesystem;
    const std::string tmpOut = output + ".tmp";
    std::ofstream out(tmpOut, std::ios::binary | std::ios::trunc);
    const bool polyglot = format == TreeFormat::Polyglot;
    if (!out || (!polyglot && !writeHeader(out, 0))) return false;
    struct Reader {
        std::ifstream f;
        Entry cur;
        bool next()
        {
            uint8_t b[EntrySize];
            if (!f.read(reinterpret_cast<char*>(b), EntrySize)) return false;
            cur = decode(b);
            return true;
        }
    };
    std::vector<std::unique_ptr<Reader>> readers;
    auto cmp = [&](size_t a, size_t b) { return entryLess(readers[b]->cur, readers[a]->cur); }; // min-heap
    std::priority_queue<size_t, std::vector<size_t>, decltype(cmp)> heap(cmp);
    for (const std::string& r : runs) {
        auto rd = std::make_unique<Reader>();
        rd->f.open(r, std::ios::binary);
        readers.push_back(std::move(rd));
        if (readers.back()->next()) heap.push(readers.size() - 1);
    }
    std::vector<uint8_t> buf;
    buf.reserve(size_t(EntrySize) * 65536);
    written = 0;
    bool haveKey = false;
    uint64_t currentKey = 0;
    std::vector<uint16_t> seenMoves; // moves already written for the current position
    auto flushBuf = [&] {
        if (buf.size() >= size_t(EntrySize) * 65536) {
            out.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
            buf.clear();
        }
    };
    // Polyglot: the moves of a position are gathered to compute their weights (16 bits).
    std::vector<Entry> group;
    auto flushGroup = [&] {
        if (group.empty()) return;
        uint64_t max = 0;
        for (const Entry& g : group) max = std::max(max, g.games());
        for (const Entry& g : group) {
            uint64_t w = g.games();
            if (max > 0xFFFF) w = w ? std::max<uint64_t>(1, w * 0xFFFF / max) : 0;
            const bool avoid = (g.flags & RecommendationMask) == 64 || g.annotation == 0x02 || g.annotation == 0x04; // to avoid, ?, ??
            if (avoid) w = 0;
            uint8_t b[16];
            for (int k = 0; k < 8; ++k) b[k] = uint8_t(g.key >> (56 - 8 * k)); // big-endian
            const uint16_t mv = polyglotMove(g);
            b[8] = uint8_t(mv >> 8), b[9] = uint8_t(mv);
            b[10] = uint8_t(w >> 8), b[11] = uint8_t(w);
            b[12] = b[13] = b[14] = b[15] = 0;
            buf.insert(buf.end(), b, b + 16);
            ++written;
            flushBuf();
        }
        group.clear();
    };
    auto emit = [&](const Entry& e) {
        if (polyglot) {
            if (!group.empty() && group.front().key != e.key) flushGroup();
            group.push_back(e);
            return;
        }
        buf.resize(buf.size() + EntrySize);
        encode(e, &buf[buf.size() - EntrySize]);
        ++written;
        flushBuf();
    };
    while (!heap.empty()) {
        const size_t i = heap.top();
        heap.pop();
        const Entry e = readers[i]->cur;
        if (readers[i]->next()) heap.push(i);
        // The order (key, decreasing games) puts the most played first: duplicates ignored
        if (!haveKey || e.key != currentKey) {
            currentKey = e.key;
            haveKey = true;
            seenMoves.clear();
        } else if (std::find(seenMoves.begin(), seenMoves.end(), e.move) != seenMoves.end()) {
            continue;
        }
        seenMoves.push_back(e.move);
        emit(e);
    }
    flushGroup();
    out.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    if (!polyglot) {
        out.seekp(0);
        writeHeader(out, written);
    }
    out.close();
    readers.clear();
    std::error_code ec;
    if (!out) {
        fs::remove(tmpOut, ec);
        return false;
    }
    fs::rename(tmpOut, output, ec);
    if (ec) {
        fs::remove(tmpOut, ec);
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------- tree conversion

TreeSource treeSourceOf(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".ctg" || ext == ".cto" || ext == ".ctb") return TreeSource::ChessBase;
    if (ext == ".hsh" || ext == ".elm") return TreeSource::Aquarium;
    if (ext == ".bin") return TreeSource::Polyglot;
    if (ext == ".hy64book") return TreeSource::Hy64Book;
    if (ext == ".txt" && looksLikeAquariumExport(path)) return TreeSource::AquariumExport;
    return TreeSource::Unknown;
}

namespace {

// Reader of a tree for one thread: moves of a position in the common format.
using TreeReader = std::function<std::vector<BookMove>(const Position&)>;

// Opens the source (once per thread); empty reader if unreadable.
TreeReader openReader(TreeSource kind, const std::string& path, std::string* error)
{
    switch (kind) {
    case TreeSource::ChessBase: {
        auto b = std::make_shared<ctg::Book>();
        if (!b->open(path, error)) return {};
        return [b](const Position& pos) {
            std::vector<BookMove> out;
            for (const ctg::BookMove& m : b->moves(pos)) {
                BookMove bm;
                bm.move = m.move;
                bm.annotation = m.annotation;
                bm.recommendation = m.stats.recommendation;
                bm.hasStats = m.hasStats;
                bm.gameCount = uint64_t(std::max(0, m.stats.games));
                bm.wins = uint32_t(std::max(0, m.stats.wins));
                bm.draws = uint32_t(std::max(0, m.stats.draws));
                bm.losses = uint32_t(std::max(0, m.stats.losses));
                bm.averageElo = uint16_t(std::clamp(m.stats.averageElo, 0, 0xFFFF));
                bm.performance = uint16_t(std::clamp(m.stats.performance, 0, 0xFFFF));
                out.push_back(bm);
            }
            return out;
        };
    }
    case TreeSource::Aquarium: {
        auto b = std::make_shared<aquarium::Book>();
        if (!b->open(path, error)) return {};
        return [b](const Position& pos) {
            std::vector<BookMove> out;
            for (const aquarium::BookMove& m : b->moves(pos)) {
                BookMove bm;
                bm.move = m.move;
                bm.hasStats = m.hasStats;
                bm.wins = m.wins, bm.draws = m.draws, bm.losses = m.losses;
                bm.maxElo = m.maxElo, bm.year = m.year;
                out.push_back(bm);
            }
            return out;
        };
    }
    case TreeSource::Polyglot: {
        auto b = std::make_shared<polyglot::Book>();
        if (!b->open(path, error)) return {};
        return [b](const Position& pos) {
            std::vector<BookMove> out;
            for (const polyglot::BookMove& m : b->moves(pos)) {
                BookMove bm;
                bm.move = m.move;
                bm.gameCount = m.weight; // Polyglot weight kept as the number of games (no results)
                out.push_back(bm);
            }
            return out;
        };
    }
    case TreeSource::Hy64Book: {
        auto b = std::make_shared<Book>();
        if (!b->open(path, error)) return {};
        return [b](const Position& pos) { return b->moves(pos); };
    }
    default: return {};
    }
}

// Breadth-first walk from the initial position (level by level, in parallel); the
// entries go into sorted runs (`runs`).
bool walkTree(const std::vector<TreeReader>& readers, const std::vector<Position>& starts, const std::string& output, std::vector<std::string>& runs,
              ConvertResult& res, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress, uint64_t estimate)
{
    const unsigned threads = unsigned(readers.size());
    std::vector<Entry> chunk;
    std::unordered_set<uint64_t> visited;
    std::vector<Position> frontier;
    for (const Position& s : starts)
        if (visited.insert(polyglot::key(s)).second) frontier.push_back(s);
    while (!frontier.empty()) {
        std::vector<std::vector<Entry>> entries(threads);
        std::vector<std::vector<Position>> children(threads);
        std::atomic<size_t> next{0};
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) {
            pool.emplace_back([&, t] {
                for (size_t i; (i = next.fetch_add(64)) < frontier.size();) {
                    for (size_t j = i; j < std::min(i + 64, frontier.size()); ++j) {
                        const Position& pos = frontier[j];
                        const uint64_t key = polyglot::key(pos);
                        for (const BookMove& bm : readers[t](pos)) {
                            Entry e;
                            e.key = key;
                            e.move = packMove(bm.move);
                            e.annotation = bm.annotation;
                            const bool castle = pos.isCastling(bm.move);
                            e.flags = uint8_t((bm.hasStats ? FlagStats : 0) | (bm.recommendation & RecommendationMask) | (castle ? FlagCastle : 0));
                            if (bm.gameCount) {
                                e.flags |= FlagGameCount;
                                e.gameCount = uint32_t(std::min<uint64_t>(bm.gameCount, 0xFFFFFFFFu));
                            } else {
                                e.year = bm.year;
                                e.elo = bm.maxElo;
                            }
                            e.wins = bm.wins, e.draws = bm.draws, e.losses = bm.losses;
                            e.averageElo = bm.averageElo, e.performance = bm.performance;
                            if (bm.hasEval) {
                                e.flags |= FlagEval;
                                e.averageElo = uint16_t(bm.eval);
                                e.performance = bm.evalDepth;
                            }
                            entries[t].push_back(e);
                            children[t].push_back(pos.after(bm.move)); // followed: without moves, it yields nothing
                        }
                    }
                }
            });
        }
        for (auto& th : pool) th.join();
        res.positions += frontier.size();
        std::vector<Position> level;
        for (unsigned t = 0; t < threads; ++t) {
            for (const Entry& e : entries[t]) {
                chunk.push_back(e);
                if (chunk.size() >= RunEntries && !flushRun(chunk, output, runs)) {
                    if (error) *error = "write";
                    return false;
                }
            }
            for (const Position& c : children[t])
                if (visited.insert(polyglot::key(c)).second) level.push_back(c);
        }
        frontier = std::move(level);
        if (progress && !progress(res.positions, std::max(estimate, res.positions + frontier.size()))) {
            if (error) *error = "cancelled";
            return false;
        }
    }
    if (!flushRun(chunk, output, runs)) {
        if (error) *error = "write";
        return false;
    }
    return true;
}

} // namespace

bool convertTree(const std::string& input, const std::string& output, TreeFormat target, ConvertResult* result, std::string* error,
                 const std::function<bool(uint64_t, uint64_t)>& progress)
{
    const TreeSource kind = treeSourceOf(input);
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (kind == TreeSource::Unknown) return fail("format non reconnu");
    if (kind == TreeSource::AquariumExport) {
        // Text export: first to .hy64book (its own conversion), then to the wanted format.
        if (target == TreeFormat::Hy64Book) return convertAquariumExport(input, output, result, error, progress);
        const std::string tmp = output + ".tmp.hy64book"; // extension recognised at the next step
        ConvertResult first;
        if (!convertAquariumExport(input, tmp, &first, error, progress)) return false;
        const bool ok = convertTree(tmp, output, target, result, error, progress);
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return ok;
    }
    ConvertResult res;
    uint64_t estimate = 0;
    if (kind == TreeSource::ChessBase) {
        ctg::Book b;
        if (b.open(input)) res.inFile = b.positionCount();
        estimate = 2 * res.inFile; // a ChessBase entry also serves the mirrored position
    }
    const unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<TreeReader> readers;
    for (unsigned t = 0; t < threads; ++t) {
        std::string err;
        TreeReader r = openReader(kind, input, &err);
        if (!r) return fail(err.empty() ? "unreadable" : err);
        readers.push_back(std::move(r));
    }
    std::vector<std::string> runs;
    if (!walkTree(readers, {Position::startPos()}, output, runs, res, error, progress, estimate)) {
        removeRuns(runs);
        return false;
    }
    uint64_t written = 0;
    const bool merged = mergeRuns(runs, output, written, target);
    removeRuns(runs);
    if (!merged) return fail("write");
    res.moves = written;
    if (result) *result = res;
    return true;
}

bool writeTree(const MoveReader& reader, const std::vector<Position>& starts, const std::string& output, TreeFormat target, ConvertResult* result,
               std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress, uint64_t estimate)
{
    const unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    const std::vector<TreeReader> readers(threads, reader);
    ConvertResult res;
    std::vector<std::string> runs;
    if (!walkTree(readers, starts, output, runs, res, error, progress, estimate)) {
        removeRuns(runs);
        return false;
    }
    uint64_t written = 0;
    const bool merged = mergeRuns(runs, output, written, target);
    removeRuns(runs);
    if (!merged) {
        if (error) *error = "write";
        return false;
    }
    res.moves = written;
    if (result) *result = res;
    return true;
}

struct BookWriter::Private {
    std::string output;
    TreeFormat format;
    std::vector<Entry> chunk;
    std::vector<std::string> runs;
    bool failed = false;
};

BookWriter::BookWriter(std::string output, TreeFormat format)
    : d(new Private{std::move(output), format, {}, {}, false})
{
}

BookWriter::~BookWriter()
{
    removeRuns(d->runs);
    delete d;
}

bool BookWriter::add(uint64_t key, const BookMove& bm, bool castling)
{
    Entry e;
    e.key = key;
    e.move = packMove(bm.move);
    e.annotation = bm.annotation;
    e.flags = uint8_t((bm.hasStats ? FlagStats : 0) | (bm.recommendation & RecommendationMask) | (castling ? FlagCastle : 0));
    e.year = bm.year;
    e.elo = bm.maxElo;
    e.wins = bm.wins, e.draws = bm.draws, e.losses = bm.losses;
    e.averageElo = bm.averageElo, e.performance = bm.performance;
    d->chunk.push_back(e);
    if (d->chunk.size() >= RunEntries && !flushRun(d->chunk, d->output, d->runs)) d->failed = true;
    return !d->failed;
}

bool BookWriter::finish(uint64_t* written)
{
    if (!d->failed && !flushRun(d->chunk, d->output, d->runs)) d->failed = true;
    uint64_t n = 0;
    const bool ok = !d->failed && mergeRuns(d->runs, d->output, n, d->format);
    removeRuns(d->runs);
    d->runs.clear();
    if (written) *written = n;
    return ok;
}

bool convertCtg(const std::string& ctgPath, const std::string& output, ConvertResult* result, std::string* error,
                const std::function<bool(uint64_t, uint64_t)>& progress)
{
    return convertTree(ctgPath, output, TreeFormat::Hy64Book, result, error, progress);
}

} // namespace chess::treebook
