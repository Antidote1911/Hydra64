#include "database.h"

#include "beauty.h"
#include "fileoffset.h"

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h> // _commit
#endif
#include "positionindex.h"
#include "chess/eco.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <map>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace hy64db {

// ---------------------------------------------------------------- paths

std::string basePathOf(const std::string& path)
{
    const fs::path p(path);
    const std::string ext = p.extension().string();
    if (ext == ".hy64i" || ext == ".hy64n" || ext == ".hy64g" || ext == ".hy64p") return (p.parent_path() / p.stem()).string();
    return path;
}
bool isPackedPath(const std::string& path) { return fs::path(path).extension() == ".hy64"; }

namespace {
// Copies `size` bytes of `in` (starting at `from`) to the end of `out`.
bool copyBytes(std::FILE* in, uint64_t from, uint64_t size, std::FILE* out, uint64_t& done, uint64_t total, const std::function<bool(uint64_t, uint64_t)>& progress, std::string* error)
{
    std::vector<uint8_t> buffer(4u << 20);
    if (seek64(in, int64_t(from), SEEK_SET) != 0) {
        if (error) *error = "lecture impossible";
        return false;
    }
    while (size > 0) {
        const size_t n = size_t(std::min<uint64_t>(size, buffer.size()));
        if (std::fread(buffer.data(), 1, n, in) != n || std::fwrite(buffer.data(), 1, n, out) != n) {
            if (error) *error = std::string("copie interrompue : ") + std::strerror(errno);
            return false;
        }
        size -= n;
        done += n;
        if (progress && !progress(done, total)) {
            if (error) *error = "cancelled";
            return false;
        }
    }
    return true;
}
} // namespace

bool packDatabase(const std::string& source, const std::string& destination, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress)
{
    const std::string base = basePathOf(source);
    const std::string parts[PackSectionCount] = {indexPath(base), namesPath(base), gamesPath(base), positionsPath(base)};
    uint64_t sizes[PackSectionCount] = {0, 0, 0, 0}, total = 0;
    std::error_code ec;
    for (int i = 0; i < PackSectionCount; ++i) {
        if (fs::exists(parts[i], ec)) sizes[i] = fs::file_size(parts[i], ec);
        else if (i != PackPositions) { // the position index is optional
            if (error) *error = parts[i] + " : fichier absent";
            return false;
        }
        total += sizes[i];
    }
    const std::string tmp = destination + ".part";
    std::FILE* out = std::fopen(tmp.c_str(), "wb");
    if (!out) {
        if (error) *error = tmp + " : " + std::strerror(errno);
        return false;
    }
    auto fail = [&] {
        std::fclose(out);
        fs::remove(tmp, ec);
        return false;
    };
    // Provisional header, then the aligned sections; the final header is written at the end.
    uint8_t header[PackHeaderSize] = {};
    std::memcpy(header, PackMagic, 8);
    put32(header + 8, FormatVersion);
    put32(header + 12, PackSectionCount);
    uint64_t at = PackHeaderSize, done = 0;
    if (std::fwrite(header, 1, PackHeaderSize, out) != PackHeaderSize) return fail();
    for (int i = 0; i < PackSectionCount; ++i) {
        const uint64_t aligned = (at + PackAlign - 1) / PackAlign * PackAlign;
        static const uint8_t zeros[PackAlign] = {};
        if (aligned > at && std::fwrite(zeros, 1, size_t(aligned - at), out) != size_t(aligned - at)) return fail();
        at = aligned;
        put64(header + 16 + size_t(i) * 16, at);
        put64(header + 24 + size_t(i) * 16, sizes[i]);
        if (sizes[i] == 0) continue;
        std::FILE* in = std::fopen(parts[i].c_str(), "rb");
        if (!in) {
            if (error) *error = parts[i] + " : " + std::strerror(errno);
            return fail();
        }
        const bool ok = copyBytes(in, 0, sizes[i], out, done, total, progress, error);
        std::fclose(in);
        if (!ok) return fail();
        at += sizes[i];
    }
    if (seek64(out, 0, SEEK_SET) != 0 || std::fwrite(header, 1, PackHeaderSize, out) != PackHeaderSize || std::fflush(out) != 0) return fail();
    std::fclose(out);
    fs::rename(tmp, destination, ec);
    if (ec) {
        if (error) *error = destination + " : " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool unpackDatabase(const std::string& source, const std::string& destination, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress)
{
    std::FILE* in = std::fopen(source.c_str(), "rb");
    if (!in) {
        if (error) *error = source + " : " + std::strerror(errno);
        return false;
    }
    uint8_t header[PackHeaderSize];
    if (std::fread(header, 1, PackHeaderSize, in) != PackHeaderSize || std::memcmp(header, PackMagic, 8) != 0 || get32(header + 12) != PackSectionCount) {
        std::fclose(in);
        if (error) *error = source + " : ce n'est pas une base Hydra64 en un fichier";
        return false;
    }
    const std::string base = basePathOf(destination);
    const std::string parts[PackSectionCount] = {indexPath(base), namesPath(base), gamesPath(base), positionsPath(base)};
    uint64_t total = 0, done = 0;
    for (int i = 0; i < PackSectionCount; ++i) total += get64(header + 24 + size_t(i) * 16);
    std::error_code ec;
    bool ok = true;
    for (int i = 0; i < PackSectionCount && ok; ++i) {
        const uint64_t offset = get64(header + 16 + size_t(i) * 16), size = get64(header + 24 + size_t(i) * 16);
        fs::remove(parts[i], ec);
        if (size == 0) continue; // no position index
        std::FILE* out = std::fopen(parts[i].c_str(), "wb");
        if (!out) {
            if (error) *error = parts[i] + " : " + std::strerror(errno);
            ok = false;
            break;
        }
        ok = copyBytes(in, offset, size, out, done, total, progress, error);
        ok = std::fclose(out) == 0 && ok;
    }
    // Journal: index rows, names and game blocks written after the file was created,
    // appended after each file (block offsets are thus preserved).
    const uint64_t journalStart = get64(header + PackJournalStart), journalEnd = get64(header + PackJournalStart + 8);
    if (ok && journalStart >= PackHeaderSize && journalEnd > journalStart) {
        std::FILE* fi = std::fopen(parts[PackIndex].c_str(), "r+b");
        std::FILE* fn = std::fopen(parts[PackNames].c_str(), "r+b");
        std::FILE* fg = std::fopen(parts[PackGames].c_str(), "r+b");
        uint8_t hi[HeaderSize], hn[HeaderSize], hg[HeaderSize];
        ok = fi && fn && fg && std::fread(hi, 1, HeaderSize, fi) == HeaderSize && std::fread(hn, 1, HeaderSize, fn) == HeaderSize && std::fread(hg, 1, HeaderSize, fg) == HeaderSize;
        uint64_t games = ok ? get64(hi + 16) : 0, names = ok ? get64(hn + 16) : 0, namesEnd = ok ? get64(hn + 24) : 0, gamesEnd = ok ? get64(hg + 24) : 0;
        std::vector<uint8_t> payload;
        for (uint64_t at = journalStart; ok && at + 5 <= journalEnd;) {
            uint8_t head[5];
            ok = seek64(in, int64_t(at), SEEK_SET) == 0 && std::fread(head, 1, 5, in) == 5;
            const uint32_t size = ok ? get32(head + 1) : 0;
            if (!ok || at + 5 + size > journalEnd) break;
            payload.resize(size);
            ok = size == 0 || std::fread(payload.data(), 1, size, in) == size;
            if (!ok) break;
            if (head[0] == 'I' && size == RecordSize) {
                ok = seek64(fi, int64_t(HeaderSize + games * RecordSize), SEEK_SET) == 0 && std::fwrite(payload.data(), 1, size, fi) == size;
                ++games;
            } else if (head[0] == 'G') {
                ok = seek64(fg, int64_t(gamesEnd), SEEK_SET) == 0 && std::fwrite(payload.data(), 1, size, fg) == size;
                gamesEnd += size;
            } else if (head[0] == 'N') {
                ok = seek64(fn, int64_t(namesEnd), SEEK_SET) == 0 && std::fwrite(payload.data(), 1, size, fn) == size;
                namesEnd += size;
                const uint8_t* p = payload.data();
                const uint8_t* e = p + size;
                while (p < e) { // a name: varint(length·8 + type), then the text
                    uint64_t v = 0;
                    if (!getVarint(p, e, v) || uint64_t(e - p) < (v >> 3)) break;
                    p += v >> 3;
                    ++names;
                }
            }
            at += 5 + size;
        }
        if (ok) {
            writeHeader(hi, IndexMagic, games);
            writeHeader(hn, NamesMagic, names, namesEnd);
            writeHeader(hg, GamesMagic, games, gamesEnd);
            ok = seek64(fi, 0, SEEK_SET) == 0 && std::fwrite(hi, 1, HeaderSize, fi) == HeaderSize && seek64(fn, 0, SEEK_SET) == 0 && std::fwrite(hn, 1, HeaderSize, fn) == HeaderSize
                 && seek64(fg, 0, SEEK_SET) == 0 && std::fwrite(hg, 1, HeaderSize, fg) == HeaderSize;
        }
        for (std::FILE* f : {fi, fn, fg})
            if (f) ok = std::fclose(f) == 0 && ok;
        if (!ok && error && error->empty()) *error = source + " : journal illisible";
    }
    std::fclose(in);
    if (!ok)
        for (const std::string& p : parts) fs::remove(p, ec);
    return ok;
}

std::string indexPath(const std::string& base) { return base + ".hy64i"; }
std::string namesPath(const std::string& base) { return base + ".hy64n"; }
std::string gamesPath(const std::string& base) { return base + ".hy64g"; }
std::string positionsPath(const std::string& base) { return base + ".hy64p"; }

namespace {

bool writeHeaderTo(std::FILE* f, const char magic[8], uint64_t count, uint64_t extra)
{
    uint8_t h[HeaderSize];
    writeHeader(h, magic, count, extra);
    return seek64(f, 0, SEEK_SET) == 0 && std::fwrite(h, 1, HeaderSize, f) == HeaderSize;
}

std::string errnoText(const std::string& path) { return path + " : " + std::strerror(errno); }

unsigned threadCount() { return std::max(1u, std::min(16u, std::thread::hardware_concurrency())); }

// Applies `f(first, last)` to slices of [0, n) in parallel.
template <typename F>
void parallelChunks(uint32_t n, F f)
{
    const unsigned t = n < 65536 ? 1 : threadCount();
    if (t == 1) {
        f(0u, n, 0u);
        return;
    }
    std::vector<std::thread> pool;
    const uint32_t step = (n + t - 1) / t;
    for (unsigned i = 0; i < t; ++i) {
        const uint32_t a = i * step, b = std::min(n, a + step);
        if (a >= b) break;
        pool.emplace_back(f, a, b, i);
    }
    for (auto& th : pool) th.join();
}

} // namespace

// ---------------------------------------------------------------- Writer

Writer::~Writer()
{
    for (std::FILE* f : {m_index, m_names_f, m_games})
        if (f) std::fclose(f);
}

bool Writer::create(const std::string& base, std::string* error)
{
    m_base = base;
    m_index = std::fopen(indexPath(base).c_str(), "wb");
    m_names_f = std::fopen(namesPath(base).c_str(), "wb");
    m_games = std::fopen(gamesPath(base).c_str(), "wb");
    if (!m_index || !m_names_f || !m_games) {
        if (error) *error = errnoText(base);
        return false;
    }
    for (std::FILE* f : {m_index, m_names_f, m_games}) std::setvbuf(f, nullptr, _IOFBF, 1 << 20);
    uint8_t h[HeaderSize] = {};
    for (std::FILE* f : {m_index, m_names_f, m_games}) std::fwrite(h, 1, HeaderSize, f); // final ones at the end
    return true;
}

uint32_t Writer::add(const GameInfo& info, const std::vector<uint8_t>& blob)
{
    Record r = info.record;
    auto id = [&](NameType t, const std::string& v, bool optional) {
        if (optional && v.empty()) return NoName;
        return m_names.intern(t, v, &m_pendingNames);
    };
    r.white = id(Player, info.white, false);
    r.black = id(Player, info.black, false);
    r.event = id(Event, info.event, false);
    r.site = id(Site, info.site, false);
    r.round = id(Round, info.round, false);
    r.annotator = id(Annotator, info.annotator, true);
    r.source = id(Source, info.source, true);
    r.offset = m_gamesEnd;
    r.size = uint32_t(blob.size());
    std::fwrite(blob.data(), 1, blob.size(), m_games);
    m_gamesEnd += blob.size();
    if (!m_pendingNames.empty()) {
        std::fwrite(m_pendingNames.data(), 1, m_pendingNames.size(), m_names_f);
        m_pendingNames.clear();
    }
    uint8_t rec[RecordSize];
    r.write(rec);
    std::fwrite(rec, 1, RecordSize, m_index);
    return m_count++;
}

bool Writer::finish(std::string* error)
{
    size_t names = 0;
    for (int t = 0; t < NameTypeCount; ++t) names += m_names.count(NameType(t));
    const int64_t namesEnd = tell64(m_names_f);
    bool ok = writeHeaderTo(m_index, IndexMagic, m_count, 0) && writeHeaderTo(m_names_f, NamesMagic, names, uint64_t(namesEnd))
           && writeHeaderTo(m_games, GamesMagic, m_count, m_gamesEnd);
    for (std::FILE** f : {&m_index, &m_names_f, &m_games}) {
        if (std::fclose(*f) != 0) ok = false;
        *f = nullptr;
    }
    if (!ok && error) *error = errnoText(m_base);
    return ok;
}

// ---------------------------------------------------------------- Database

Database::Database() = default;
Database::~Database() { close(); }

bool Database::create(const std::string& base, std::string* error)
{
    Writer w;
    return w.create(basePathOf(base), error) && w.finish(error);
}

namespace {
std::string compactMarker(const std::string& base) { return base + ".compact.commit"; }

// Finishes a compaction: new files in place of the old ones (those already moved are skipped).
bool finishCompaction(const std::string& base, std::string* error)
{
    const std::string tmp = base + ".compact";
    std::error_code ec;
    for (auto [from, to] : {std::pair{gamesPath(tmp), gamesPath(base)}, {namesPath(tmp), namesPath(base)}, {indexPath(tmp), indexPath(base)}}) {
        if (!fs::exists(from, ec)) continue;
        fs::rename(from, to, ec);
        if (ec) {
            if (error) *error = to + " : " + ec.message();
            return false;
        }
    }
    fs::remove(positionsPath(base), ec); // game numbers have changed: index to rebuild
    fs::remove(compactMarker(base), ec);
    return true;
}
} // namespace

bool Database::open(const std::string& path, std::string* error)
{
    close();
    MappedFile namesMap;
    if (isPackedPath(path)) {
        // Single-file database: the sections are mapped where they are, nothing is copied.
        MappedFile head;
        if (!head.openRange(path, 0, PackHeaderSize, error)) return false;
        if (std::memcmp(head.data(), PackMagic, 8) != 0 || get32(head.data() + 12) != PackSectionCount) {
            if (error) *error = path + " : ce n'est pas une base Hydra64 en un fichier";
            return false;
        }
        for (int i = 0; i < PackSectionCount; ++i) {
            m_packOffset[i] = get64(head.data() + 16 + size_t(i) * 16);
            m_packSize[i] = get64(head.data() + 24 + size_t(i) * 16);
        }
        m_journalStart = get64(head.data() + PackJournalStart);
        m_journalEnd = get64(head.data() + PackJournalStart + 8);
        m_base = path;
        m_packed = true;
        if (!m_indexMap.openRange(path, m_packOffset[PackIndex], m_packSize[PackIndex], error) || !namesMap.openRange(path, m_packOffset[PackNames], m_packSize[PackNames], error)
            || !m_gamesMap.openRange(path, m_packOffset[PackGames], m_packSize[PackGames], error)) {
            close();
            return false;
        }
    } else {
        m_base = basePathOf(path);
        {
            std::error_code ec;
            if (fs::exists(compactMarker(m_base), ec) && !finishCompaction(m_base, error)) return false;
        }
        if (!m_indexMap.open(indexPath(m_base), error) || !namesMap.open(namesPath(m_base), error) || !m_gamesMap.open(gamesPath(m_base), error))
            return false;
    }
    if (!checkHeader(m_indexMap.data(), m_indexMap.size(), IndexMagic, error) || !checkHeader(namesMap.data(), namesMap.size(), NamesMagic, error)
        || !checkHeader(m_gamesMap.data(), m_gamesMap.size(), GamesMagic, error))
        return false;
    // Only the data validated by the headers counts (an interrupted write leaves at
    // most orphan bytes at the end of the file).
    m_mappedCount = uint32_t(std::min<uint64_t>(get64(m_indexMap.data() + 16), (m_indexMap.size() - HeaderSize) / RecordSize));
    m_namesEnd = std::min<uint64_t>(get64(namesMap.data() + 24), namesMap.size());
    if (m_namesEnd < HeaderSize) m_namesEnd = HeaderSize;
    m_gamesEnd = m_mappedGamesEnd = std::min<uint64_t>(get64(m_gamesMap.data() + 24), m_gamesMap.size());
    if (!m_names.load(namesMap.data() + HeaderSize, size_t(m_namesEnd - HeaderSize), error)) return false;
    if (m_packed && m_journalStart >= PackHeaderSize && m_journalEnd > m_journalStart) {
        // Journal: what has been written since the file was created, reread in order.
        MappedFile journal;
        if (!journal.openRange(m_base, m_journalStart, m_journalEnd - m_journalStart, error)) {
            close();
            return false;
        }
        const uint8_t* d = journal.data();
        std::lock_guard lock(m_tailMutex);
        for (uint64_t at = 0; at + 5 <= journal.size();) {
            const uint32_t size = get32(d + at + 1);
            if (at + 5 + size > journal.size()) break;
            const uint8_t* payload = d + at + 5;
            if (d[at] == 'I' && size == RecordSize) {
                m_tail.push_back(Record::read(payload));
                m_tailOffsets.push_back(m_journalStart + at + 5);
            } else if (d[at] == 'G') {
                m_tailGames.insert(m_tailGames.end(), payload, payload + size);
                m_gamesEnd += size;
            } else if (d[at] == 'N') {
                m_names.loadMore(payload, size, nullptr);
            }
            at += 5 + size;
        }
    }
    m_open = true;
    reloadPositions();
    return true;
}

void Database::close()
{
    sync(); // nothing is left in the system buffers
    for (std::FILE** f : {&m_indexW, &m_namesW, &m_gamesW}) {
        if (*f) std::fclose(*f);
        *f = nullptr;
    }
    m_positions.reset();
    m_indexMap.close();
    m_gamesMap.close();
    m_tail.clear();
    m_tailGames.clear();
    m_mappedCount = 0;
    m_open = false;
    m_packed = false;
    m_journalStart = m_journalEnd = 0;
    m_tailOffsets.clear();
}

bool Database::reloadPositions(std::string* error)
{
    m_positions.reset();
    auto p = std::make_unique<PositionIndex>();
    if (m_packed) {
        if (m_packSize[PackPositions] == 0 || !p->openRange(m_base, m_packOffset[PackPositions], m_packSize[PackPositions], error)) return false;
        m_positions = std::move(p);
        return true;
    }
    if (!fs::exists(positionsPath(m_base))) return false;
    if (!p->open(positionsPath(m_base), error)) return false;
    m_positions = std::move(p);
    return true;
}

uint32_t Database::count() const
{
    std::lock_guard lock(m_tailMutex);
    return m_mappedCount + uint32_t(m_tail.size());
}

Record Database::record(uint32_t id) const
{
    if (id < m_mappedCount) return Record::read(m_indexMap.data() + HeaderSize + uint64_t(id) * RecordSize);
    std::lock_guard lock(m_tailMutex);
    const uint32_t t = id - m_mappedCount;
    return t < m_tail.size() ? m_tail[t] : Record();
}

bool Database::blob(uint32_t id, std::vector<uint8_t>& out) const
{
    const Record r = record(id);
    if (r.size == 0) return false;
    if (r.offset + r.size <= m_mappedGamesEnd) {
        out.assign(m_gamesMap.data() + r.offset, m_gamesMap.data() + r.offset + r.size);
        return true;
    }
    std::lock_guard lock(m_tailMutex);
    if (r.offset < m_mappedGamesEnd || r.offset + r.size - m_mappedGamesEnd > m_tailGames.size()) return false;
    const auto at = m_tailGames.begin() + std::ptrdiff_t(r.offset - m_mappedGamesEnd);
    out.assign(at, at + r.size);
    return true;
}

bool Database::game(uint32_t id, chess::Game& out, std::string* error) const
{
    std::vector<uint8_t> b;
    if (!blob(id, b)) {
        if (error) *error = "partie introuvable";
        return false;
    }
    return decodeGame(b.data(), b.size(), record(id), m_names, out, error);
}

// ---------------------------------------------------------------- writing

bool Database::openWrite(std::string* error)
{
    if (m_indexW) return true;
    if (m_packed) { // single file: index rows are modified in place, the rest goes to the journal
        m_indexW = std::fopen(m_base.c_str(), "r+b");
        if (!m_indexW) {
            if (error) *error = errnoText(m_base);
            return false;
        }
        if (m_journalStart < PackHeaderSize) { // first addition: the journal starts at the end of the file
            std::error_code ec;
            m_journalStart = m_journalEnd = fs::file_size(m_base, ec);
        }
        return true;
    }
    m_indexW = std::fopen(indexPath(m_base).c_str(), "r+b");
    m_namesW = std::fopen(namesPath(m_base).c_str(), "r+b");
    m_gamesW = std::fopen(gamesPath(m_base).c_str(), "r+b");
    if (!m_indexW || !m_namesW || !m_gamesW) {
        if (error) *error = errnoText(m_base);
        return false;
    }
    return true;
}

bool Database::writeAt(std::FILE* f, uint64_t at, const void* data, size_t n, bool flush)
{
    return seek64(f, int64_t(at), SEEK_SET) == 0 && std::fwrite(data, 1, n, f) == n && (!flush || std::fflush(f) == 0);
}

bool Database::writeRecord(uint32_t id, const uint8_t* rec, bool flush)
{
    uint64_t at = HeaderSize + uint64_t(id) * RecordSize;
    if (m_packed) {
        if (id < m_mappedCount) at += m_packOffset[PackIndex];
        else if (id - m_mappedCount < m_tailOffsets.size()) at = m_tailOffsets[id - m_mappedCount];
        else return false;
    }
    return writeAt(m_indexW, at, rec, RecordSize, flush);
}

bool Database::journalAppend(char type, const uint8_t* data, size_t size, uint64_t* payloadAt)
{
    uint8_t head[5] = {uint8_t(type), 0, 0, 0, 0};
    put32(head + 1, uint32_t(size));
    if (!writeAt(m_indexW, m_journalEnd, head, 5, false) || std::fwrite(data, 1, size, m_indexW) != size) return false;
    if (payloadAt) *payloadAt = m_journalEnd + 5;
    m_journalEnd += 5 + size;
    return true;
}

bool Database::journalCommit()
{
    // The end of the journal is only validated in the header once the blocks are written: an
    // interrupted write leaves at most ignored bytes at the end of the file.
    uint8_t ends[16];
    put64(ends, m_journalStart);
    put64(ends + 8, m_journalEnd);
    return std::fflush(m_indexW) == 0 && writeAt(m_indexW, PackJournalStart, ends, 16);
}

bool Database::appendGameBytes(const std::vector<uint8_t>& b, uint32_t gameCount)
{
    if (m_packed) return journalAppend('G', b.data(), b.size());
    uint8_t gh[HeaderSize];
    writeHeader(gh, GamesMagic, gameCount, m_gamesEnd + b.size());
    return writeAt(m_gamesW, m_gamesEnd, b.data(), b.size()) && writeAt(m_gamesW, 0, gh, HeaderSize);
}

uint32_t Database::internInfo(GameInfo& info, std::vector<uint8_t>& appended)
{
    Record& r = info.record;
    auto id = [&](NameType t, const std::string& v, bool optional) { return optional && v.empty() ? NoName : m_names.intern(t, v, &appended); };
    r.white = id(Player, info.white, false);
    r.black = id(Player, info.black, false);
    r.event = id(Event, info.event, false);
    r.site = id(Site, info.site, false);
    r.round = id(Round, info.round, false);
    r.annotator = id(Annotator, info.annotator, true);
    r.source = id(Source, info.source, true);
    return 0;
}

bool Database::appendNames(const std::vector<uint8_t>& bytes, std::string* error)
{
    if (bytes.empty()) return true;
    if (m_packed) {
        if (!journalAppend('N', bytes.data(), bytes.size()) || !journalCommit()) { // validated right away: a name correction adds nothing else
            if (error) *error = errnoText(m_base);
            return false;
        }
        m_namesEnd += bytes.size();
        return true;
    }
    size_t total = 0;
    for (int t = 0; t < NameTypeCount; ++t) total += m_names.count(NameType(t));
    uint8_t h[HeaderSize];
    writeHeader(h, NamesMagic, total, m_namesEnd + bytes.size());
    if (!writeAt(m_namesW, m_namesEnd, bytes.data(), bytes.size()) || !writeAt(m_namesW, 0, h, HeaderSize)) {
        if (error) *error = errnoText(namesPath(m_base));
        return false;
    }
    m_namesEnd += bytes.size();
    return true;
}

bool Database::append(const chess::Game& game, uint32_t* id, std::string* error)
{
    std::vector<uint8_t> b;
    GameInfo info;
    if (!encodeGame(game, b, info, error)) return false;
    return appendEncoded(info, b, id, error);
}

bool Database::copyGame(const Database& from, uint32_t fromId, uint32_t* id, std::string* error)
{
    // Block copied as is (no decoding, no notation); only the names are renumbered.
    std::vector<uint8_t> b;
    const Record r = from.record(fromId);
    if (!from.blob(fromId, b)) {
        if (error) *error = "partie introuvable";
        return false;
    }
    GameInfo info;
    info.record = r;
    info.record.flags = uint16_t(r.flags & ~FlagDeleted);
    auto nm = [&](NameType t, uint32_t v) { return v == NoName ? std::string() : from.names().name(t, v); };
    info.white = nm(Player, r.white), info.black = nm(Player, r.black), info.event = nm(Event, r.event), info.site = nm(Site, r.site);
    info.round = nm(Round, r.round), info.annotator = nm(Annotator, r.annotator), info.source = nm(Source, r.source);
    return appendEncoded(info, b, id, error);
}

bool Database::appendEncoded(GameInfo& info, const std::vector<uint8_t>& b, uint32_t* id, std::string* error)
{
    if (!openWrite(error)) return false;
    std::vector<uint8_t> names;
    internInfo(info, names);
    Record& r = info.record;
    r.offset = m_gamesEnd;
    r.size = uint32_t(b.size());
    const uint32_t n = count();
    if (!appendGameBytes(b, n + 1) || !appendNames(names, error)) {
        if (error && error->empty()) *error = errnoText(m_packed ? m_base : gamesPath(m_base));
        return false;
    }
    uint8_t rec[RecordSize], ih[HeaderSize];
    r.write(rec);
    writeHeader(ih, IndexMagic, n + 1);
    uint64_t recordAt = 0;
    const bool written = m_packed ? journalAppend('I', rec, RecordSize, &recordAt) && journalCommit()
                                  : writeAt(m_indexW, HeaderSize + uint64_t(n) * RecordSize, rec, RecordSize) && writeAt(m_indexW, 0, ih, HeaderSize);
    if (!written) {
        if (error) *error = errnoText(m_packed ? m_base : indexPath(m_base));
        return false;
    }
    {
        std::lock_guard lock(m_tailMutex);
        m_tailGames.insert(m_tailGames.end(), b.begin(), b.end());
        m_tail.push_back(r);
        if (m_packed) m_tailOffsets.push_back(recordAt);
    }
    m_gamesEnd += b.size();
    if (id) *id = n;
    return true;
}

bool Database::replace(uint32_t id, const chess::Game& game, std::string* error)
{
    if (id >= count()) return false;
    if (!openWrite(error)) return false;
    std::vector<uint8_t> b, names;
    GameInfo info;
    if (!encodeGame(game, b, info, error)) return false;
    internInfo(info, names);
    Record& r = info.record;
    r.flags = uint16_t(r.flags | (record(id).flags & (0xFC00 | FlagDeleted))); // user flags and deletion kept
    r.offset = m_gamesEnd;
    r.size = uint32_t(b.size());
    if (!appendGameBytes(b, count()) || !appendNames(names, error) || (m_packed && !journalCommit())) return false;
    {
        std::lock_guard lock(m_tailMutex);
        m_tailGames.insert(m_tailGames.end(), b.begin(), b.end());
    }
    m_gamesEnd += b.size();
    uint8_t rec[RecordSize];
    r.write(rec);
    if (!writeRecord(id, rec)) {
        if (error) *error = errnoText(indexPath(m_base));
        return false;
    }
    if (id >= m_mappedCount) {
        std::lock_guard lock(m_tailMutex);
        m_tail[id - m_mappedCount] = r;
    }
    return true; // row already in the mapped file: visible at once (shared mapping)
}

bool Database::setUserFlags(uint32_t id, uint8_t user, std::string* error)
{
    if (id >= count() || !openWrite(error)) return false;
    Record r = record(id);
    r.flags = uint16_t((r.flags & ~(0x3F << UserFlagShift)) | (uint16_t(user & 0x3F) << UserFlagShift));
    uint8_t rec[RecordSize];
    r.write(rec);
    if (!writeRecord(id, rec)) {
        if (error) *error = errnoText(indexPath(m_base));
        return false;
    }
    if (id >= m_mappedCount) {
        std::lock_guard lock(m_tailMutex);
        m_tail[id - m_mappedCount] = r;
    }
    return true;
}

bool Database::setDeleted(uint32_t id, bool deleted, std::string* error)
{
    if (id >= count() || !openWrite(error)) return false;
    Record r = record(id);
    r.flags = uint16_t(deleted ? r.flags | FlagDeleted : r.flags & ~FlagDeleted);
    uint8_t rec[RecordSize];
    r.write(rec);
    if (!writeRecord(id, rec)) {
        if (error) *error = errnoText(indexPath(m_base));
        return false;
    }
    if (id >= m_mappedCount) {
        std::lock_guard lock(m_tailMutex);
        m_tail[id - m_mappedCount] = r;
    }
    return true;
}

bool Database::compact(std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress, const std::vector<uint32_t>* order)
{
    if (m_packed) return rebuildPacked([&](Database& plain, std::string* e) { return plain.compact(e, progress, order); }, error);
    const std::string tmp = m_base + ".compact";
    Writer w;
    if (!w.create(tmp, error)) return false;
    const uint32_t n = order ? uint32_t(order->size()) : count();
    std::vector<uint8_t> b;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t id = order ? (*order)[k] : k;
        if (id >= count()) continue;
        if (progress && k % 4096 == 0 && !progress(k, n)) {
            w.finish(nullptr);
            for (auto f : {indexPath(tmp), namesPath(tmp), gamesPath(tmp)}) fs::remove(f);
            return false;
        }
        const Record r = record(id);
        if (r.deleted() || !blob(id, b)) continue;
        GameInfo info;
        info.record = r;
        auto nm = [&](NameType t, uint32_t v) { return v == NoName ? std::string() : m_names.name(t, v); };
        info.white = nm(Player, r.white);
        info.black = nm(Player, r.black);
        info.event = nm(Event, r.event);
        info.site = nm(Site, r.site);
        info.round = nm(Round, r.round);
        info.annotator = nm(Annotator, r.annotator);
        info.source = nm(Source, r.source);
        w.add(info, b);
    }
    if (!w.finish(error)) return false;
    const std::string base = m_base;
    close();
    // Marker: the three new files are complete. If the replacement is interrupted (power cut,
    // error), the next opening finishes it instead of mixing the old index with new games.
    if (std::FILE* marker = std::fopen(compactMarker(base).c_str(), "wb")) std::fclose(marker);
    if (!finishCompaction(base, error)) return false;
    return open(base, error);
}

bool Database::tidy(std::string* error)
{
    if (!m_packed || journalBytes() == 0) return true;
    return rebuildPacked([](Database&, std::string*) { return true; }, error);
}

void Database::sync()
{
    for (std::FILE* f : {m_indexW, m_namesW, m_gamesW}) {
        if (!f) continue;
        std::fflush(f);
#if defined(__unix__) || defined(__APPLE__)
        ::fsync(fileno(f));
#elif defined(_WIN32)
        ::_commit(_fileno(f));
#endif
    }
}

bool Database::rebuildPacked(const std::function<bool(Database&, std::string*)>& work, std::string* error)
{
    if (!m_packed) return false;
    const std::string file = m_base, plainBase = m_base + ".work";
    close();
    std::error_code ec;
    auto cleanup = [&] {
        for (const std::string& p : {indexPath(plainBase), namesPath(plainBase), gamesPath(plainBase), positionsPath(plainBase)}) fs::remove(p, ec);
    };
    bool ok = unpackDatabase(file, indexPath(plainBase), error);
    if (ok) {
        Database plain;
        ok = plain.open(indexPath(plainBase), error) && work(plain, error);
        plain.close();
    }
    ok = ok && packDatabase(indexPath(plainBase), file, error); // replaces the file with a single rename
    cleanup();
    return open(file, ok ? error : nullptr) && ok;
}

// ---------------------------------------------------------------- search, sort

std::vector<uint32_t> Database::allGames(bool withDeleted) const
{
    const uint32_t n = count();
    std::vector<uint32_t> out;
    out.reserve(n);
    for (uint32_t id = 0; id < n; ++id)
        if (withDeleted || !(record(id).flags & FlagDeleted)) out.push_back(id);
    return out;
}

std::vector<uint32_t> Database::search(const std::string& text, const std::atomic<int32_t>* cancel, bool withDeleted) const
{
    // Names → sets of numbers (bit arrays), then scan of the index rows.
    auto mark = [&](NameType t) {
        std::vector<uint8_t> set(m_names.count(t), 0);
        for (uint32_t id : m_names.findPrefix(t, text)) set[id] = 1;
        return set;
    };
    const std::vector<uint8_t> players = mark(Player), events = mark(Event);
    const uint32_t n = count();
    const unsigned t = threadCount();
    std::vector<std::vector<uint32_t>> parts(t);
    parallelChunks(n, [&](uint32_t a, uint32_t b, unsigned part) {
        auto& out = parts[part];
        for (uint32_t id = a; id < b; ++id) {
            if ((id & 0xFFFF) == 0 && cancel && cancel->load()) return;
            const Record r = record(id);
            if (!withDeleted && (r.flags & FlagDeleted)) continue;
            if ((r.white < players.size() && players[r.white]) || (r.black < players.size() && players[r.black])
                || (r.event < events.size() && events[r.event]))
                out.push_back(id);
        }
    });
    std::vector<uint32_t> ids;
    for (auto& p : parts) ids.insert(ids.end(), p.begin(), p.end());
    return ids;
}

std::vector<std::vector<uint32_t>> Database::findTwins(const TwinCriteria& c, const std::function<bool(uint64_t, uint64_t)>& progress) const
{
    // 1) Cheap grouping on the index row: players, length (+ chosen criteria)
    const uint32_t n = count();
    struct Item {
        uint64_t key;
        uint32_t id;
    };
    std::vector<Item> items;
    items.reserve(n);
    auto mix = [](uint64_t h, uint64_t v) { return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 0x100000001b3ull; };
    for (uint32_t id = 0; id < n; ++id) {
        const Record r = record(id);
        if (r.deleted() || r.plies == 0) continue;
        uint64_t k = mix(mix(mix(1469598103934665603ull, r.white), r.black), r.plies);
        if (c.sameResult) k = mix(k, r.result);
        if (c.sameYear) k = mix(k, r.date >> 9);
        if (c.sameEvent) k = mix(k, r.event);
        if (c.sameRound) k = mix(k, r.round);
        items.push_back({k, id});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key != b.key ? a.key < b.key : a.id < b.id; });
    auto same = [&](uint32_t a, uint32_t b) { // exact check (key collisions)
        const Record x = record(a), y = record(b);
        return x.white == y.white && x.black == y.black && x.plies == y.plies && (!c.sameResult || x.result == y.result) &&
               (!c.sameYear || (x.date >> 9) == (y.date >> 9)) && (!c.sameEvent || x.event == y.event) && (!c.sameRound || x.round == y.round);
    };
    auto richness = [&](uint32_t id) {
        const Record r = record(id);
        return int(bool(r.flags & FlagComments)) + int(bool(r.flags & FlagVariations)) + int(bool(r.flags & FlagNags));
    };
    std::vector<std::vector<uint32_t>> groups;
    for (size_t i = 0; i < items.size();) {
        size_t j = i + 1;
        while (j < items.size() && items[j].key == items[i].key) ++j;
        if (progress && !progress(i, items.size())) return {};
        if (j - i >= 2) {
            // 2) Exact subgroups, then same moves (only for these candidates)
            std::vector<uint32_t> ids;
            for (size_t k = i; k < j; ++k) ids.push_back(items[k].id);
            std::vector<std::vector<chess::Move>> moves(ids.size());
            if (c.sameMoves)
                for (size_t k = 0; k < ids.size(); ++k) {
                    chess::Game g;
                    if (game(ids[k], g))
                        for (const chess::Node* nd : g.mainline()) moves[k].push_back(nd->move);
                }
            std::vector<bool> used(ids.size(), false);
            for (size_t a = 0; a < ids.size(); ++a) {
                if (used[a]) continue;
                std::vector<uint32_t> group{ids[a]};
                for (size_t b = a + 1; b < ids.size(); ++b)
                    if (!used[b] && same(ids[a], ids[b]) && (!c.sameMoves || moves[a] == moves[b])) {
                        group.push_back(ids[b]);
                        used[b] = true;
                    }
                if (group.size() < 2) continue;
                std::stable_sort(group.begin(), group.end(), [&](uint32_t x, uint32_t y) {
                    const int rx = richness(x), ry = richness(y);
                    return rx != ry ? rx > ry : x < y;
                });
                groups.push_back(std::move(group));
            }
        }
        i = j;
    }
    if (progress) progress(items.size(), items.size());
    return groups;
}

int64_t Database::classifyEco(const chess::eco::Book& book, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress)
{
    const uint32_t n = count();
    std::vector<uint16_t> codes(n, 0);
    std::atomic<uint64_t> done{0};
    std::atomic<bool> cancelled{false};
    const unsigned t = threadCount();
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < t; ++w)
        pool.emplace_back([&, w] {
            std::vector<uint8_t> blob;
            for (uint32_t id = w; id < n && !cancelled; id += t) {
                const Record r = record(id);
                codes[id] = r.eco;
                if (!r.deleted() && !(r.flags & FlagStartFen) && this->blob(id, blob)) {
                    const chess::eco::Opening* best = nullptr;
                    replayMainline(blob.data(), blob.size(), [&](const chess::Position& p, int ply) {
                        if (const chess::eco::Opening* o = book.lookup(p)) best = o;
                        return ply < 60;
                    });
                    if (best) codes[id] = parseEco(best->eco);
                }
                const uint64_t d = ++done;
                if (w == 0 && progress && d % 8192 < t && !progress(d, uint64_t(n) * 2)) cancelled = true;
            }
        });
    for (auto& th : pool) th.join();
    if (cancelled) {
        if (error) *error = "annulé";
        return -1;
    }
    if (!openWrite(error)) return -1;
    int64_t changed = 0;
    for (uint32_t id = 0; id < n; ++id) {
        if (progress && id % 65536 == 0 && !progress(uint64_t(n) + id, uint64_t(n) * 2)) break;
        Record r = record(id);
        if (r.eco == codes[id]) continue;
        r.eco = codes[id];
        uint8_t rec[RecordSize];
        r.write(rec);
        if (!writeRecord(id, rec, false)) {
            if (error) *error = errnoText(indexPath(m_base));
            return -1;
        }
        if (id >= m_mappedCount) {
            std::lock_guard lock(m_tailMutex);
            m_tail[id - m_mappedCount] = r;
        }
        ++changed;
    }
    sync();
    if (std::fflush(m_indexW) != 0) {
        if (error) *error = errnoText(indexPath(m_base));
        return -1;
    }
    return changed;
}

IntegrityReport Database::checkIntegrity(const std::function<bool(uint64_t, uint64_t)>& progress) const
{
    const uint32_t n = count();
    IntegrityReport total;
    std::mutex gather;
    std::atomic<uint64_t> done{0};
    std::atomic<bool> cancelled{false};
    size_t nameCount[NameTypeCount];
    for (int t = 0; t < NameTypeCount; ++t) nameCount[t] = m_names.count(NameType(t));
    parallelChunks(n, [&](uint32_t a, uint32_t b, unsigned worker) {
        IntegrityReport local;
        std::vector<uint8_t> block;
        auto problem = [&](uint32_t id, std::string what) {
            if (local.first.size() < 100) local.first.push_back({id, std::move(what)});
        };
        for (uint32_t id = a; id < b && !cancelled; ++id) {
            const Record r = record(id);
            ++local.games;
            local.deleted += r.deleted();
            const std::pair<NameType, uint32_t> used[] = {{Player, r.white}, {Player, r.black}, {Event, r.event}, {Site, r.site},
                                                          {Round, r.round},  {Annotator, r.annotator}, {Source, r.source}};
            bool namesOk = true;
            for (const auto& [type, number] : used) namesOk = namesOk && (number == NoName || number < nameCount[type]);
            if (!namesOk) {
                ++local.badNames;
                problem(id, "numéro de nom inconnu");
            }
            if (!blob(id, block)) {
                ++local.badLocation;
                problem(id, "bloc hors du fichier des parties");
            } else {
                chess::Game g;
                std::string err;
                if (!decodeGame(block.data(), block.size(), r, m_names, g, &err)) {
                    ++local.unreadable;
                    problem(id, "partie illisible : " + err);
                } else if (const size_t plies = std::min<size_t>(g.mainline().size(), 65535); plies != r.plies) {
                    ++local.badLength;
                    problem(id, "longueur : " + std::to_string(r.plies) + " demi-coups dans l'index, " + std::to_string(plies) + " dans la partie");
                }
            }
            const uint64_t d = ++done;
            if (worker == 0 && progress && d % 4096 == 0 && !progress(d, n)) cancelled = true;
        }
        std::lock_guard lock(gather);
        total.games += local.games;
        total.deleted += local.deleted;
        total.badLocation += local.badLocation;
        total.badNames += local.badNames;
        total.unreadable += local.unreadable;
        total.badLength += local.badLength;
        total.first.insert(total.first.end(), local.first.begin(), local.first.end());
    });
    total.cancelled = cancelled;
    std::sort(total.first.begin(), total.first.end(), [](const auto& x, const auto& y) { return x.id < y.id; });
    if (total.first.size() > 100) total.first.resize(100);
    return total;
}

uint32_t Database::unratedBeauty() const
{
    const uint32_t n = count();
    std::atomic<uint32_t> unrated{0};
    parallelChunks(n, [&](uint32_t a, uint32_t b, unsigned) {
        uint32_t local = 0;
        for (uint32_t id = a; id < b; ++id) {
            const Record r = record(id);
            local += !r.deleted() && !(r.flags & FlagBeautyRated);
        }
        unrated += local;
    });
    return unrated;
}

int64_t Database::rateBeauty(std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress, bool all)
{
    const uint32_t n = count();
    std::vector<uint16_t> flags(n, 0);
    std::atomic<uint64_t> done{0};
    std::atomic<bool> cancelled{false};
    const unsigned t = threadCount();
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < t; ++w)
        pool.emplace_back([&, w] {
            std::vector<uint8_t> blob;
            BeautyRater rater;
            for (uint32_t id = w; id < n && !cancelled; id += t) {
                const Record r = record(id);
                flags[id] = r.flags;
                if (!r.deleted() && (all || !(r.flags & FlagBeautyRated)) && this->blob(id, blob)) {
                    rater.start();
                    const bool read = replayMainlineMoves(blob.data(), blob.size(), [&](const chess::Position& before, chess::Move m, int) {
                        rater.move(before, m);
                        return true;
                    });
                    flags[id] = uint16_t((r.flags & ~BeautyMask) | beautyFlags(read ? rater.medals(Result(r.result)) : 0));
                }
                const uint64_t d = ++done;
                if (w == 0 && progress && d % 8192 < t && !progress(d, uint64_t(n) * 2)) cancelled = true;
            }
        });
    for (auto& th : pool) th.join();
    if (cancelled) {
        if (error) *error = "annulé";
        return -1;
    }
    if (!openWrite(error)) return -1;
    int64_t changed = 0;
    for (uint32_t id = 0; id < n; ++id) {
        if (progress && id % 65536 == 0 && !progress(uint64_t(n) + id, uint64_t(n) * 2)) break;
        Record r = record(id);
        if (r.flags == flags[id]) continue;
        r.flags = flags[id];
        uint8_t rec[RecordSize];
        r.write(rec);
        if (!writeRecord(id, rec, false)) {
            if (error) *error = errnoText(indexPath(m_base));
            return -1;
        }
        if (id >= m_mappedCount) {
            std::lock_guard lock(m_tailMutex);
            m_tail[id - m_mappedCount] = r;
        }
        ++changed;
    }
    sync();
    if (std::fflush(m_indexW) != 0) {
        if (error) *error = errnoText(indexPath(m_base));
        return -1;
    }
    return changed;
}

EventTable Database::eventTable(uint32_t event) const
{
    EventTable t;
    std::unordered_map<uint32_t, size_t> index;
    auto slot = [&](uint32_t player) {
        auto [it, added] = index.emplace(player, t.players.size());
        if (added) {
            EventTable::Player p;
            p.id = player;
            t.players.push_back(p);
        }
        return it->second;
    };
    struct G {
        size_t w, b;
        double ws; // White's points (-1: unknown)
        uint32_t id;
    };
    std::vector<G> list;
    const uint32_t n = count();
    for (uint32_t id = 0; id < n; ++id) {
        const Record r = record(id);
        if (r.deleted() || r.event != event) continue;
        const size_t w = slot(r.white), b = slot(r.black);
        t.players[w].elo = std::max<int>(t.players[w].elo, r.whiteElo);
        t.players[b].elo = std::max<int>(t.players[b].elo, r.blackElo);
        const double ws = r.result == WhiteWins ? 1 : r.result == BlackWins ? 0 : r.result == Draw ? 0.5 : -1;
        list.push_back({w, b, ws, id});
        t.ids.push_back(id);
    }
    const size_t m = t.players.size();
    std::vector<std::vector<double>> pts(m, std::vector<double>(m, 0));
    std::vector<std::vector<uint32_t>> gms(m, std::vector<uint32_t>(m, 0));
    std::vector<std::vector<std::vector<uint32_t>>> pg(m, std::vector<std::vector<uint32_t>>(m));
    std::vector<double> oppElo(m, 0);
    std::vector<uint32_t> oppRated(m, 0);
    for (const G& g : list) {
        pg[g.w][g.b].push_back(g.id);
        pg[g.b][g.w].push_back(g.id);
        if (g.ws < 0) continue;
        pts[g.w][g.b] += g.ws;
        pts[g.b][g.w] += 1 - g.ws;
        ++gms[g.w][g.b];
        ++gms[g.b][g.w];
        t.players[g.w].points += g.ws;
        t.players[g.b].points += 1 - g.ws;
        ++t.players[g.w].games;
        ++t.players[g.b].games;
        if (t.players[g.b].elo) oppElo[g.w] += t.players[g.b].elo, ++oppRated[g.w];
        if (t.players[g.w].elo) oppElo[g.b] += t.players[g.w].elo, ++oppRated[g.b];
    }
    for (size_t i = 0; i < m; ++i) {
        EventTable::Player& p = t.players[i];
        if (!p.games || !oppRated[i]) continue;
        const double score = std::clamp(p.points / p.games, 0.01, 0.99);
        p.performance = int(std::lround(oppElo[i] / oppRated[i] - 400.0 * std::log10(1.0 / score - 1.0)));
    }
    // Standings, then matrices in that order
    std::vector<size_t> order(m);
    for (size_t i = 0; i < m; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const auto &x = t.players[a], &y = t.players[b];
        return x.points != y.points ? x.points > y.points : x.performance > y.performance;
    });
    std::vector<EventTable::Player> players;
    for (size_t i : order) players.push_back(t.players[i]);
    t.players = std::move(players);
    t.points.assign(m, std::vector<double>(m, 0));
    t.games.assign(m, std::vector<uint32_t>(m, 0));
    t.pairGames.assign(m, std::vector<std::vector<uint32_t>>(m));
    for (size_t i = 0; i < m; ++i)
        for (size_t j = 0; j < m; ++j) {
            t.points[i][j] = pts[order[i]][order[j]];
            t.games[i][j] = gms[order[i]][order[j]];
            t.pairGames[i][j] = pg[order[i]][order[j]];
        }
    return t;
}

PlayerStats Database::playerStats(uint32_t player, size_t top) const
{
    PlayerStats s;
    std::map<int, PlayerStats::YearElo> years;
    std::map<uint16_t, PlayerStats::Score> white, black;
    std::unordered_map<uint32_t, PlayerStats::Score> opp;
    const uint32_t n = count();
    for (uint32_t id = 0; id < n; ++id) {
        const Record r = record(id);
        if (r.deleted() || (r.white != player && r.black != player)) continue;
        const bool isWhite = r.white == player;
        s.games.push_back(id);
        PlayerStats::Score& side = isWhite ? s.asWhite : s.asBlack;
        PlayerStats::Score* op = &opp[isWhite ? r.black : r.white];
        PlayerStats::Score* eco = r.eco ? &(isWhite ? white : black)[r.eco] : nullptr;
        auto add = [&](PlayerStats::Score& sc, int outcome) { // 1 win, 0 draw, -1 loss
            if (outcome > 0) ++sc.wins;
            else if (outcome < 0) ++sc.losses;
            else ++sc.draws;
        };
        int outcome = 2;
        if (r.result == WhiteWins) outcome = isWhite ? 1 : -1;
        else if (r.result == BlackWins) outcome = isWhite ? -1 : 1;
        else if (r.result == Draw) outcome = 0;
        if (outcome == 2) ++s.unknownResults;
        else {
            add(side, outcome);
            add(*op, outcome);
            if (eco) add(*eco, outcome);
        }
        const int year = int(r.date >> 9);
        if (year) {
            PlayerStats::YearElo& y = years[year];
            y.year = year;
            ++y.games;
            const int elo = isWhite ? r.whiteElo : r.blackElo;
            if (elo) {
                y.maxElo = std::max(y.maxElo, elo);
                y.lastElo = elo; // database order: roughly chronological
            }
            if (!s.firstYear || year < s.firstYear) s.firstYear = year;
            s.lastYear = std::max(s.lastYear, year);
        }
    }
    for (auto& [y, v] : years) s.years.push_back(v);
    auto ranked = [&](const std::map<uint16_t, PlayerStats::Score>& m) {
        std::vector<PlayerStats::Opening> out;
        for (const auto& [eco, sc] : m) out.push_back({eco, sc});
        std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.score.games() > b.score.games(); });
        if (out.size() > top) out.resize(top);
        return out;
    };
    s.whiteOpenings = ranked(white);
    s.blackOpenings = ranked(black);
    for (const auto& [id, sc] : opp) s.opponents.push_back({id, sc});
    std::sort(s.opponents.begin(), s.opponents.end(), [](const auto& a, const auto& b) {
        return a.score.games() != b.score.games() ? a.score.games() > b.score.games() : a.id < b.id;
    });
    if (s.opponents.size() > top) s.opponents.resize(top);
    return s;
}

std::vector<uint32_t> Database::nameUsage(NameType t) const
{
    std::vector<uint32_t> use(m_names.count(t), 0);
    const uint32_t n = count();
    for (uint32_t id = 0; id < n; ++id) {
        const Record r = record(id);
        if (r.deleted()) continue;
        auto add = [&](uint32_t v) {
            if (v < use.size()) ++use[v];
        };
        switch (t) {
        case Player:
            add(r.white);
            if (r.black != r.white) add(r.black);
            break;
        case Event: add(r.event); break;
        case Site: add(r.site); break;
        case Round: add(r.round); break;
        case Annotator: add(r.annotator); break;
        case Source: add(r.source); break;
        default: break;
        }
    }
    return use;
}

int64_t Database::renameName(NameType t, uint32_t from, const std::string& to, std::string* error,
                             const std::function<bool(uint64_t, uint64_t)>& progress)
{
    if (from >= m_names.count(t) || to.empty()) {
        if (error) *error = "nom inconnu";
        return -1;
    }
    if (!openWrite(error)) return -1;
    std::vector<uint8_t> appended;
    const uint32_t target = m_names.intern(t, to, &appended); // new name, or the one that already exists (merge)
    if (!appendNames(appended, error)) return -1;
    if (target == from) return 0;
    const uint32_t n = count();
    int64_t changed = 0;
    for (uint32_t id = 0; id < n; ++id) {
        if (progress && id % 65536 == 0 && !progress(id, n)) break;
        Record r = record(id);
        bool touched = false;
        auto fix = [&](uint32_t& v) {
            if (v == from) {
                v = target;
                touched = true;
            }
        };
        switch (t) {
        case Player:
            fix(r.white);
            fix(r.black);
            break;
        case Event: fix(r.event); break;
        case Site: fix(r.site); break;
        case Round: fix(r.round); break;
        case Annotator: fix(r.annotator); break;
        case Source: fix(r.source); break;
        default: break;
        }
        if (!touched) continue;
        uint8_t rec[RecordSize];
        r.write(rec);
        if (!writeRecord(id, rec, false)) {
            if (error) *error = errnoText(indexPath(m_base));
            return -1;
        }
        if (id >= m_mappedCount) {
            std::lock_guard lock(m_tailMutex);
            m_tail[id - m_mappedCount] = r;
        }
        ++changed;
    }
    sync();
    if (std::fflush(m_indexW) != 0) {
        if (error) *error = errnoText(indexPath(m_base));
        return -1;
    }
    return changed;
}

int64_t Database::renameNames(NameType t, const std::vector<std::pair<uint32_t, std::string>>& changes, std::string* error,
                              const std::function<bool(uint64_t, uint64_t)>& progress)
{
    if (changes.empty()) return 0;
    if (!openWrite(error)) return -1;
    std::vector<uint8_t> appended;
    std::unordered_map<uint32_t, uint32_t> target;
    for (const auto& [from, to] : changes) {
        if (from >= m_names.count(t) || to.empty()) continue;
        const uint32_t id = m_names.intern(t, to, &appended);
        if (id != from) target[from] = id;
    }
    if (!appendNames(appended, error)) return -1;
    if (target.empty()) return 0;
    const uint32_t n = count();
    int64_t changed = 0;
    for (uint32_t id = 0; id < n; ++id) {
        if (progress && id % 65536 == 0 && !progress(id, n)) break;
        Record r = record(id);
        bool touched = false;
        auto fix = [&](uint32_t& v) {
            if (const auto it = target.find(v); it != target.end()) {
                v = it->second;
                touched = true;
            }
        };
        switch (t) {
        case Player:
            fix(r.white);
            fix(r.black);
            break;
        case Event: fix(r.event); break;
        case Site: fix(r.site); break;
        case Round: fix(r.round); break;
        case Annotator: fix(r.annotator); break;
        case Source: fix(r.source); break;
        default: break;
        }
        if (!touched) continue;
        uint8_t rec[RecordSize];
        r.write(rec);
        if (!writeRecord(id, rec, false)) {
            if (error) *error = errnoText(indexPath(m_base));
            return -1;
        }
        if (id >= m_mappedCount) {
            std::lock_guard lock(m_tailMutex);
            m_tail[id - m_mappedCount] = r;
        }
        ++changed;
    }
    sync();
    if (std::fflush(m_indexW) != 0) {
        if (error) *error = errnoText(indexPath(m_base));
        return -1;
    }
    return changed;
}

std::vector<uint32_t> Database::searchHeaders(const HeaderQuery& q, const std::vector<uint32_t>* within, const std::atomic<int32_t>* cancel,
                                             bool withDeleted) const
{
    // Searched names → sets of numbers (empty: criterion absent)
    auto mark = [&](NameType t, const std::string& want) {
        std::vector<uint8_t> set;
        if (want.empty()) return set;
        set.assign(std::max<size_t>(1, m_names.count(t)), 0);
        for (uint32_t id : m_names.findPrefix(t, want)) set[id] = 1;
        return set;
    };
    const std::vector<uint8_t> white = mark(Player, q.white), black = mark(Player, q.black), event = mark(Event, q.event), site = mark(Site, q.site),
                               round = mark(Round, q.round), annotator = mark(Annotator, q.annotator);
    auto in = [](const std::vector<uint8_t>& set, uint32_t v) { return set.empty() || (v < set.size() && set[v]); };
    auto range = [](int v, int lo, int hi) { return (!lo || v >= lo) && (!hi || v <= hi); };
    auto accept = [&](uint32_t id) {
        const Record r = record(id);
        if (!withDeleted && r.deleted()) return false;
        if (q.ignoreColors) {
            if (!((in(white, r.white) && in(black, r.black)) || (in(white, r.black) && in(black, r.white)))) return false;
        } else if (!in(white, r.white) || !in(black, r.black)) {
            return false;
        }
        if (!in(event, r.event) || !in(site, r.site) || !in(round, r.round) || !in(annotator, r.annotator)) return false;
        if (!range(r.whiteElo, q.whiteEloMin, q.whiteEloMax) || !range(r.blackElo, q.blackEloMin, q.blackEloMax)) return false;
        if (q.yearFrom || q.yearTo) {
            const int y = int(r.date >> 9);
            if (!y || !range(y, q.yearFrom, q.yearTo)) return false;
        }
        if ((q.ecoFrom || q.ecoTo) && (!r.eco || !range(r.eco, q.ecoFrom, q.ecoTo))) return false;
        if (q.results != HeaderQuery::AllResults) {
            const uint8_t bit = r.result == WhiteWins   ? HeaderQuery::WhiteWin
                                : r.result == BlackWins ? HeaderQuery::BlackWin
                                : r.result == Draw      ? HeaderQuery::DrawResult
                                                        : HeaderQuery::Unknown;
            if (!(q.results & bit)) return false;
        }
        if ((q.movesMin || q.movesMax) && !range((r.plies + 1) / 2, q.movesMin, q.movesMax)) return false;
        if (q.withVariations && !(r.flags & FlagVariations)) return false;
        if (q.withComments && !(r.flags & FlagComments)) return false;
        if (q.userFlags && ((r.flags >> UserFlagShift) & q.userFlags) != q.userFlags) return false;
        if (!q.material.isEmpty()) {
            // Without replaying: material only decreases (pawns always; pieces except promotion),
            // so a game ending with more than the requested maximum never fits.
            if (q.material.plyFrom > int(r.plies)) return false;
            for (int c = 0; c < 2; ++c) {
                const uint32_t side = r.material >> (12 * c);
                if (int(side >> 8 & 15) > q.material.maxCount[c][chess::Pawn]) return false;
                if (r.flags & FlagPromotion) continue;
                if (int(side & 3) > q.material.maxCount[c][chess::Queen] || int(side >> 2 & 3) > q.material.maxCount[c][chess::Rook]
                    || int(side >> 4 & 3) > q.material.maxCount[c][chess::Bishop] || int(side >> 6 & 3) > q.material.maxCount[c][chess::Knight])
                    return false;
            }
            thread_local std::vector<uint8_t> buf;
            if (!blob(id, buf)) return false;
            MaterialMatcher m(q.material);
            replayMainline(buf.data(), buf.size(), [&m](const chess::Position& p, int ply) { return m.feed(p, ply); });
            if (!m.matched()) return false;
        }
        return true;
    };
    const uint32_t n = within ? uint32_t(within->size()) : count();
    std::vector<std::vector<uint32_t>> parts(threadCount());
    parallelChunks(n, [&](uint32_t a, uint32_t b, unsigned part) {
        auto& out = parts[part];
        for (uint32_t i = a; i < b; ++i) {
            if ((i & 0xFFFF) == 0 && cancel && cancel->load()) return;
            const uint32_t id = within ? (*within)[i] : i;
            if (accept(id)) out.push_back(id);
        }
    });
    std::vector<uint32_t> ids;
    for (auto& p : parts) ids.insert(ids.end(), p.begin(), p.end());
    return ids;
}

bool Database::sort(std::vector<uint32_t>& ids, Column column, bool ascending, const std::atomic<int32_t>* cancel) const
{
    struct Key {
        uint64_t v;
        uint32_t id;
    };
    // Sorted value: bit 63 = unknown (always at the end of the list); direction applied to the value.
    std::shared_ptr<const std::vector<uint32_t>> ranks;
    std::vector<uint8_t> blank;
    if (column == ColWhite || column == ColBlack || column == ColEvent || column == ColSite || column == ColRound) {
        const NameType t = column == ColEvent ? Event : column == ColSite ? Site : column == ColRound ? Round : Player;
        ranks = m_names.ranks(t);
        blank.assign(ranks->size(), 0);
        for (uint32_t i = 0; i < blank.size(); ++i) {
            const std::string s = m_names.name(t, i);
            blank[i] = s.empty() || s == "?";
        }
    }
    std::vector<Key> keys(ids.size());
    std::atomic<bool> stopped{false};
    parallelChunks(uint32_t(ids.size()), [&](uint32_t a, uint32_t b, unsigned) {
        for (uint32_t i = a; i < b; ++i) {
            if ((i & 0xFFFF) == 0 && cancel && cancel->load()) {
                stopped = true;
                return;
            }
            const uint32_t id = ids[i];
            const Record r = record(id);
            bool unknown = false;
            uint64_t v = 0;
            auto name = [&](uint32_t nid) {
                if (nid >= ranks->size()) unknown = true;
                else {
                    unknown = blank[nid];
                    v = (*ranks)[nid];
                }
            };
            switch (column) {
            case ColWhite: name(r.white); break;
            case ColBlack: name(r.black); break;
            case ColEvent: name(r.event); break;
            case ColWhiteElo: unknown = r.whiteElo == 0, v = r.whiteElo; break;
            case ColBlackElo: unknown = r.blackElo == 0, v = r.blackElo; break;
            case ColResult: unknown = r.result == ResultNone, v = r.result; break;
            case ColDate: unknown = dateYear(r.date) == 0, v = r.date; break;
            case ColEco: unknown = r.eco == 0, v = r.eco; break;
            case ColSite: name(r.site); break;
            case ColRound: name(r.round); break;
            case ColMoves: unknown = r.plies == 0, v = r.plies; break;
            case ColBeauty: unknown = beautyOf(r.flags) <= 0, v = uint64_t(std::max(0, beautyOf(r.flags))); break;
            default: v = id; break;
            }
            if (!ascending) v = ~v & 0x7FFFFFFFFFFFFFFFull;
            keys[i] = {(unknown ? 1ull << 63 : 0) | (v & 0x7FFFFFFFFFFFFFFFull), id};
        }
    });
    if (stopped) return false;
    auto less = [](const Key& a, const Key& b) { return a.v != b.v ? a.v < b.v : a.id < b.id; };
    // Parallel sort: sorted slices, then pairwise merges.
    const unsigned t = keys.size() < 200000 ? 1 : threadCount();
    std::vector<size_t> bounds;
    for (unsigned i = 0; i <= t; ++i) bounds.push_back(keys.size() * i / t);
    {
        std::vector<std::thread> pool;
        for (unsigned i = 0; i < t; ++i) pool.emplace_back([&, i] { std::sort(keys.begin() + std::ptrdiff_t(bounds[i]), keys.begin() + std::ptrdiff_t(bounds[i + 1]), less); });
        for (auto& th : pool) th.join();
    }
    while (bounds.size() > 2) {
        if (cancel && cancel->load()) return false;
        std::vector<size_t> next{bounds[0]};
        std::vector<std::thread> pool;
        for (size_t i = 0; i + 2 < bounds.size(); i += 2) {
            const size_t a = bounds[i], m = bounds[i + 1], e = bounds[i + 2];
            pool.emplace_back([&keys, a, m, e, less] { std::inplace_merge(keys.begin() + std::ptrdiff_t(a), keys.begin() + std::ptrdiff_t(m), keys.begin() + std::ptrdiff_t(e), less); });
            next.push_back(e);
        }
        if (bounds.size() % 2 == 0) next.push_back(bounds.back());
        for (auto& th : pool) th.join();
        bounds = std::move(next);
    }
    for (size_t i = 0; i < keys.size(); ++i) ids[i] = keys[i].id;
    return true;
}

} // namespace hy64db
