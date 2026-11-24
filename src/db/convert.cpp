#include "convert.h"
#include "database.h"

#include "chess/epd.h"
#include "gamecodec.h"

#include "chess/pgn.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace hy64db {

// ---------------------------------------------------------------- PGN

bool PgnSource::open(const std::string& path, std::string* error)
{
    if (!m_file.open(path, error)) return false;
    // Same splitting as pgn::indexDatabase, without keeping the headers: a tag after moves
    // opens a new game.
    const std::string_view data(reinterpret_cast<const char*>(m_file.data()), m_file.size());
    const size_t n = data.size();
    size_t i = data.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0, start = 0; // UTF-8 BOM skipped, as in pgn::indexDatabase
    bool inMoves = true, open = false;
    while (i < n) {
        size_t lineEnd = data.find('\n', i);
        if (lineEnd == std::string_view::npos) lineEnd = n;
        size_t j = i;
        while (j < lineEnd && (data[j] == ' ' || data[j] == '\t' || data[j] == '\r')) ++j;
        if (j < lineEnd && data[j] == '[') {
            if (inMoves) {
                if (open) m_games.emplace_back(start, i - start);
                start = i;
                open = true;
                inMoves = false;
            }
        } else if (j < lineEnd) {
            inMoves = true;
        }
        i = lineEnd + 1;
    }
    if (open) m_games.emplace_back(start, n - start);
    return true;
}

bool PgnSource::read(uint64_t index, chess::Game& game, std::string* error) const
{
    const auto [at, len] = m_games[index];
    return chess::pgn::parseGame(std::string_view(reinterpret_cast<const char*>(m_file.data()) + at, len), game, error);
}

bool PgnSource::readForEncoding(uint64_t index, chess::Game& game, std::string* error) const
{
    const auto [at, len] = m_games[index];
    chess::pgn::ParseOptions options;
    options.withSan = false;
    return chess::pgn::parseGame(std::string_view(reinterpret_cast<const char*>(m_file.data()) + at, len), game, error, options);
}

// ---------------------------------------------------------------- EPD

bool EpdSource::open(const std::string& path, std::string* error)
{
    MappedFile file;
    if (!file.open(path, error)) return false;
    const std::string_view text(reinterpret_cast<const char*>(file.data()), file.size());
    const size_t slash = path.find_last_of("/\\");
    m_pgn = chess::epd::toPgn(text, slash == std::string::npos ? path : path.substr(slash + 1));
    m_games.clear();
    for (const chess::pgn::GameEntry& e : chess::pgn::indexDatabase(m_pgn)) m_games.emplace_back(e.offset, e.length);
    if (m_games.empty()) {
        if (error) *error = "aucune position EPD ou FEN valide";
        return false;
    }
    return true;
}

bool EpdSource::read(uint64_t index, chess::Game& game, std::string* error) const
{
    const auto [at, len] = m_games[index];
    return chess::pgn::parseGame(std::string_view(m_pgn).substr(at, len), game, error);
}

// ---------------------------------------------------------------- Hydra64 database

DatabaseSource::DatabaseSource() = default;
DatabaseSource::~DatabaseSource() = default;

bool DatabaseSource::open(const std::string& path, std::string* error)
{
    m_db = std::make_unique<Database>();
    if (!m_db->open(path, error)) return false;
    m_ids = m_db->allGames();
    return true;
}

bool DatabaseSource::read(uint64_t index, chess::Game& game, std::string* error) const
{
    return index < m_ids.size() && m_db->game(m_ids[size_t(index)], game, error);
}

// ---------------------------------------------------------------- PGN (writing)

bool exportPgn(const GameSource& source, const std::string& destination, ConvertResult* result, std::string* error,
               const ConvertProgress& progress)
{
    const auto t0 = std::chrono::steady_clock::now();
    std::error_code ec;
    if (const fs::path parent = fs::path(destination).parent_path(); !parent.empty()) fs::create_directories(parent, ec);
    const std::string tmp = destination + ".tmp";
    std::FILE* out = std::fopen(tmp.c_str(), "wb");
    if (!out) {
        if (error) *error = destination + " : écriture impossible";
        return false;
    }
    std::setvbuf(out, nullptr, _IOFBF, 1 << 20);
    constexpr uint64_t Chunk = 1024;
    const uint64_t total = source.count();
    const uint64_t chunks = (total + Chunk - 1) / Chunk;
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::mutex mutex;
    std::condition_variable ready, room;
    std::map<uint64_t, std::pair<std::string, std::vector<std::string>>> done; // text, errors
    uint64_t nextChunk = 0, written = 0;
    bool cancelled = false;
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < threads; ++w) {
        pool.emplace_back([&] {
            for (;;) {
                uint64_t c;
                {
                    std::unique_lock lock(mutex);
                    room.wait(lock, [&] { return cancelled || nextChunk >= chunks || nextChunk < written + 4 * threads; });
                    if (cancelled || nextChunk >= chunks) return;
                    c = nextChunk++;
                }
                std::string text;
                std::vector<std::string> failures;
                for (uint64_t i = c * Chunk; i < std::min(total, (c + 1) * Chunk); ++i) {
                    chess::Game g;
                    std::string err;
                    if (source.read(i, g, &err)) {
                        if (!text.empty()) text += '\n';
                        text += chess::pgn::writeGame(g);
                    } else {
                        failures.push_back("partie " + std::to_string(i + 1) + " : " + (err.empty() ? "illisible" : err));
                    }
                }
                std::lock_guard lock(mutex);
                done.emplace(c, std::make_pair(std::move(text), std::move(failures)));
                ready.notify_all();
            }
        });
    }
    ConvertResult res;
    bool first = true, ok = true;
    for (uint64_t c = 0; c < chunks; ++c) {
        std::pair<std::string, std::vector<std::string>> part;
        {
            std::unique_lock lock(mutex);
            ready.wait(lock, [&] { return done.count(c) != 0; });
            part = std::move(done[c]);
            done.erase(c);
            written = c + 1;
            room.notify_all();
        }
        if (!part.first.empty()) {
            if (!first) std::fputc('\n', out);
            ok = ok && std::fwrite(part.first.data(), 1, part.first.size(), out) == part.first.size();
            first = false;
        }
        const uint64_t inChunk = std::min(total, (c + 1) * Chunk) - c * Chunk;
        res.failed += part.second.size();
        res.games += inChunk - part.second.size();
        for (auto& f : part.second)
            if (res.failures.size() < 20) res.failures.push_back(std::move(f));
        if ((progress && !progress(StepGames, std::min(total, (c + 1) * Chunk), total)) || !ok) {
            std::lock_guard lock(mutex);
            cancelled = true;
            room.notify_all();
            break;
        }
    }
    {
        std::lock_guard lock(mutex);
        room.notify_all();
    }
    for (auto& th : pool) th.join();
    ok = std::fclose(out) == 0 && ok;
    if (cancelled || !ok) {
        fs::remove(tmp, ec);
        if (error) *error = ok ? std::string() : destination + " : écriture impossible";
        return false;
    }
    fs::rename(tmp, destination, ec);
    if (ec) {
        if (error) *error = destination + " : " + ec.message();
        return false;
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (result) *result = std::move(res);
    return true;
}

// ---------------------------------------------------------------- conversion

bool convert(const GameSource& source, const std::string& destination, const ConvertOptions& options, ConvertResult* result,
             std::string* error, const ConvertProgress& progress)
{
    const auto t0 = std::chrono::steady_clock::now();
    const std::string base = basePathOf(destination);
    const std::string tmp = base + ".converting";
    std::error_code ec;
    auto removeBase = [&](const std::string& b) {
        for (const std::string& p : {indexPath(b), namesPath(b), gamesPath(b), positionsPath(b)}) fs::remove(p, ec);
    };
    removeBase(tmp);
    if (const fs::path parent = fs::path(base).parent_path(); !parent.empty()) fs::create_directories(parent, ec); // destination folder

    Writer writer;
    if (!writer.create(tmp, error)) return false;

    // Parallel encoding in batches; writing in source order.
    struct Coded {
        GameInfo info;
        std::vector<uint8_t> blob;
        bool ok = false;
        std::string error;
    };
    constexpr uint64_t Chunk = 1024;
    const uint64_t total = source.count();
    const uint64_t chunks = (total + Chunk - 1) / Chunk;
    const unsigned threads = options.threads ? options.threads : std::max(1u, std::thread::hardware_concurrency());
    std::mutex mutex;
    std::condition_variable ready, room;
    std::map<uint64_t, std::vector<Coded>> done;
    uint64_t nextChunk = 0, written = 0;
    bool cancelled = false;

    std::vector<std::thread> pool;
    for (unsigned w = 0; w < threads; ++w) {
        pool.emplace_back([&] {
            for (;;) {
                uint64_t c;
                {
                    std::unique_lock lock(mutex);
                    // do not get too far ahead of the writing (bounded memory)
                    room.wait(lock, [&] { return cancelled || nextChunk >= chunks || nextChunk < written + 4 * threads; });
                    if (cancelled || nextChunk >= chunks) return;
                    c = nextChunk++;
                }
                std::vector<Coded> out(std::min(Chunk, total - c * Chunk));
                for (size_t i = 0; i < out.size(); ++i) {
                    chess::Game g;
                    std::string err;
                    Coded& o = out[i];
                    if (source.readForEncoding(c * Chunk + i, g, &err)) o.ok = encodeGame(g, o.blob, o.info, &o.error);
                    else o.error = err.empty() ? "partie illisible" : err;
                }
                std::lock_guard lock(mutex);
                done.emplace(c, std::move(out));
                ready.notify_all();
            }
        });
    }

    ConvertResult res;
    for (uint64_t c = 0; c < chunks; ++c) {
        std::vector<Coded> batch;
        {
            std::unique_lock lock(mutex);
            ready.wait(lock, [&] { return done.count(c) != 0; });
            batch = std::move(done[c]);
            done.erase(c);
            written = c + 1;
            room.notify_all();
        }
        for (size_t i = 0; i < batch.size(); ++i) {
            Coded& o = batch[i];
            if (o.ok) {
                writer.add(o.info, o.blob);
                ++res.games;
            } else {
                ++res.failed;
                if (res.failures.size() < 20) res.failures.push_back("partie " + std::to_string(c * Chunk + i + 1) + " : " + o.error);
            }
        }
        if (progress && !progress(StepGames, std::min(total, (c + 1) * Chunk), total)) {
            std::lock_guard lock(mutex);
            cancelled = true;
            room.notify_all();
            break;
        }
    }
    {
        std::lock_guard lock(mutex);
        room.notify_all();
    }
    for (auto& th : pool) th.join();
    if (!writer.finish(error) || cancelled) {
        removeBase(tmp);
        if (cancelled && error) error->clear();
        return false;
    }

    // Position index
    if (options.positionDepth > 0) {
        Database db;
        if (!db.open(tmp, error)) {
            removeBase(tmp);
            return false;
        }
        const bool ok = buildPositionIndex(db, options.positionDepth, error, progress ? [&](uint64_t d, uint64_t t) { return progress(StepPositions, d, t); }
                                                                                       : std::function<bool(uint64_t, uint64_t)>());
        db.close();
        if (!ok) {
            removeBase(tmp);
            return false;
        }
    }

    // Replacement of the destination database
    removeBase(base);
    for (auto [from, to] : {std::pair{indexPath(tmp), indexPath(base)}, {namesPath(tmp), namesPath(base)}, {gamesPath(tmp), gamesPath(base)},
                            {positionsPath(tmp), positionsPath(base)}}) {
        if (!fs::exists(from)) continue;
        fs::rename(from, to, ec);
        if (ec) {
            if (error) *error = to + " : " + ec.message();
            return false;
        }
    }
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (result) *result = std::move(res);
    return true;
}

} // namespace hy64db
