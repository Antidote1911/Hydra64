#pragma once

#include "chess/game.h"
#include "format.h"
#include "gamecodec.h"
#include "headerquery.h"

namespace chess::eco { class Book; }
#include "mappedfile.h"
#include "names.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hy64db {

class PositionIndex;

// Sort columns (same numbers as the interface's game list).
enum Column { ColNumber = 0, ColWhite, ColWhiteElo, ColBlack, ColBlackElo, ColResult, ColDate, ColEvent, ColEco, ColSite, ColRound, ColMoves, ColBeauty };

// Duplicate criteria (like the "Twin checker" of SCID vs PC): same players and same
// length always; the rest is optional.
struct TwinCriteria {
    bool sameResult = true;
    bool sameYear = true;
    bool sameEvent = false;
    bool sameRound = false;
    bool sameMoves = true; // same moves of the main line
};

// Player profile (like "Player information" of SCID vs PC).
struct PlayerStats {
    struct Score {
        uint32_t wins = 0, draws = 0, losses = 0;
        uint32_t games() const { return wins + draws + losses; }
        double percent() const { return games() ? 100.0 * (wins + 0.5 * draws) / games() : 0; }
    };
    Score asWhite, asBlack;
    uint32_t unknownResults = 0;
    int firstYear = 0, lastYear = 0;
    struct YearElo {
        int year = 0;
        int maxElo = 0, lastElo = 0; // highest and last known Elo of the year
        uint32_t games = 0;
    };
    std::vector<YearElo> years; // increasing years
    struct Opening {
        uint16_t eco = 0;
        Score score;
    };
    std::vector<Opening> whiteOpenings, blackOpenings; // most played first
    struct Opponent {
        uint32_t id = 0;
        Score score; // from the player's point of view
    };
    std::vector<Opponent> opponents; // most frequent first
    std::vector<uint32_t> games;     // game numbers, in database order
};

// Crosstable of a tournament of the database (like "Crosstable" of SCID vs PC).
struct EventTable {
    struct Player {
        uint32_t id = 0;
        int elo = 0;           // highest Elo in the tournament (0: unknown)
        double points = 0;
        uint32_t games = 0;
        int performance = 0;   // average Elo of the opponents + offset by score (0: unknown)
    };
    std::vector<Player> players;                   // ranking (points, then performance)
    std::vector<std::vector<double>> points;       // points[i][j]: points of i against j
    std::vector<std::vector<uint32_t>> games;      // number of games between i and j
    std::vector<uint32_t> ids;                     // games of the tournament
    std::vector<std::vector<std::vector<uint32_t>>> pairGames; // games between i and j
};

// Integrity test of a database: every game read again from its block.
struct IntegrityReport {
    uint32_t games = 0;       // index rows checked, deleted games included
    uint32_t deleted = 0;
    uint32_t badLocation = 0; // block outside the games file, or empty
    uint32_t badNames = 0;    // a name number (player, tournament…) that does not exist
    uint32_t unreadable = 0;  // block that cannot be decoded
    uint32_t badLength = 0;   // number of half-moves of the index different from the game's
    bool cancelled = false;
    struct Problem {
        uint32_t id = 0;
        std::string what;
    };
    std::vector<Problem> first; // the first problems met (100 at most), in database order
    uint32_t problems() const { return badLocation + badNames + unreadable + badLength; }
    bool ok() const { return !cancelled && problems() == 0; }
};

// Paths of a database's files: `base` without extension, or any of the files.
std::string basePathOf(const std::string& path);
std::string indexPath(const std::string& base);
std::string namesPath(const std::string& base);
std::string gamesPath(const std::string& base);
std::string positionsPath(const std::string& base);

// Single-file database (.hy64): the four files of a database in a container, opened as
// it is; what is written to it afterwards goes into a journal at the end of the file. packDatabase() builds it
// from an ordinary database (a copy), unpackDatabase() rebuilds an ordinary database, journal included. `progress(done, total)` in bytes;
// false: cancel.
bool isPackedPath(const std::string& path);
bool packDatabase(const std::string& source, const std::string& destination, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress = {});
bool unpackDatabase(const std::string& source, const std::string& destination, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress = {});

// Sequential writing of a new database (conversion, compaction): fast, buffered.
class Writer {
public:
    ~Writer();
    bool create(const std::string& base, std::string* error);
    // Adds an already encoded game; returns its number.
    uint32_t add(const GameInfo& info, const std::vector<uint8_t>& blob);
    bool finish(std::string* error); // final headers, files closed
    uint32_t count() const { return m_count; }
    const Names& names() const { return m_names; }

private:
    std::FILE* m_index = nullptr;
    std::FILE* m_names_f = nullptr;
    std::FILE* m_games = nullptr;
    Names m_names;
    uint32_t m_count = 0;
    uint64_t m_gamesEnd = HeaderSize;
    std::vector<uint8_t> m_pendingNames;
    std::string m_base;
};

// Open database. Concurrent reads are possible (search in a thread during display);
// writes are done from a single thread.
class Database {
public:
    Database();
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    static bool create(const std::string& base, std::string* error);
    bool open(const std::string& path, std::string* error);
    void close();
    bool isOpen() const { return m_open; }
    const std::string& base() const { return m_base; }
    bool isPacked() const { return m_packed; } // single-file database (.hy64)
    // Single-file database: what has been written since its creation is in a journal at the end of the
    // file — already saved, but kept in memory at each opening. tidy() moves it into the
    // body of the file (the file is rewritten alongside then replaced in one go; on failure,
    // the old one stays intact). Deleted games are not removed: that is compaction.
    uint64_t journalBytes() const { return m_packed && m_journalEnd > m_journalStart ? m_journalEnd - m_journalStart : 0; }
    uint32_t journalGames() const { return m_packed ? uint32_t(m_tailOffsets.size()) : 0; }
    bool tidy(std::string* error);
    // Forces to disk what has been written (to be called at the end of a user action).
    void sync();
    // Single-file database: unpacks it alongside, applies `work` to the resulting ordinary database, then
    // packs it again (the journal is then moved into the sections) and reopens it.
    bool rebuildPacked(const std::function<bool(Database&, std::string*)>& work, std::string* error);

    uint32_t count() const; // index rows, deleted games included
    Record record(uint32_t id) const;
    const Names& names() const { return m_names; }
    bool game(uint32_t id, chess::Game& out, std::string* error = nullptr) const;
    bool blob(uint32_t id, std::vector<uint8_t>& out) const;

    // Writing (files updated at once; the index row is written last).
    bool append(const chess::Game& game, uint32_t* id, std::string* error);
    // Copies game `fromId` of another database, without decoding it (block taken as is).
    bool copyGame(const Database& from, uint32_t fromId, uint32_t* id, std::string* error);
    bool replace(uint32_t id, const chess::Game& game, std::string* error);
    bool setDeleted(uint32_t id, bool deleted, std::string* error);
    // User flags (6 bits, 0…63)
    bool setUserFlags(uint32_t id, uint8_t flags, std::string* error);
    // Rewrites the database without the deleted games or the wasted space (files replaced at the end).
    // `order`: rewritten in this order (physical sort, like SCID's "Sort database").
    bool compact(std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress = {}, const std::vector<uint32_t>* order = nullptr);

    // Games in database order (without the deleted ones, unless `withDeleted`).
    std::vector<uint32_t> allGames(bool withDeleted = false) const;
    // Games in which a player or the tournament starts with `text` (words, ignoring case and accents).
    std::vector<uint32_t> search(const std::string& text, const std::atomic<int32_t>* cancel = nullptr, bool withDeleted = false) const;
    // Duplicates: groups of identical games according to `c` (deleted games ignored). In each
    // group, the game to keep comes first (the richest: comments, variations, NAGs;
    // then the oldest). `progress(done, total)`; false = cancelled (empty result).
    std::vector<std::vector<uint32_t>> findTwins(const TwinCriteria& c, const std::function<bool(uint64_t, uint64_t)>& progress = {}) const;
    // ECO classification of all the games (main line replayed, in parallel): code of
    // the deepest opening recognised. Returns the number of games whose code changed
    // (-1: error or cancelled).
    int64_t classifyEco(const chess::eco::Book& book, std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress = {});
    // Beauty mark of the games (db/beauty.h), stored in the index flags. Games encoded since the mark
    // exists already carry it; this rates the others — or all of them with `all`, after a change of
    // the rules. Returns the number of games whose flags changed (-1: error or cancelled).
    int64_t rateBeauty(std::string* error, const std::function<bool(uint64_t, uint64_t)>& progress = {}, bool all = false);
    // Number of (non-deleted) games without a beauty mark.
    uint32_t unratedBeauty() const;
    // Reads every game again (in parallel, nothing written): place of the block, name numbers,
    // full decoding, length. `progress(done, total)`; false = cancelled.
    IntegrityReport checkIntegrity(const std::function<bool(uint64_t, uint64_t)>& progress = {}) const;
    // Crosstable of the tournament (Event name number).
    EventTable eventTable(uint32_t event) const;
    // Player profile (Player name number); `top`: openings and opponents kept.
    PlayerStats playerStats(uint32_t player, size_t top = 12) const;
    // Number of (non-deleted) games using each name of type `t`.
    std::vector<uint32_t> nameUsage(NameType t) const;
    // Replaces the name `from` with `to` in all the games (merge if `to` already exists).
    // Returns the number of games modified, -1 on error.
    int64_t renameName(NameType t, uint32_t from, const std::string& to, std::string* error,
                       const std::function<bool(uint64_t, uint64_t)>& progress = {});
    // Several names of type `t` replaced in one pass over the index: (number, new name) pairs; a
    // new name that already exists merges the two. Returns the number of games modified, -1 on error.
    int64_t renameNames(NameType t, const std::vector<std::pair<uint32_t, std::string>>& changes, std::string* error,
                        const std::function<bool(uint64_t, uint64_t)>& progress = {});
    // Search by headers, in `within` (order kept) or in the whole database.
    std::vector<uint32_t> searchHeaders(const HeaderQuery& q, const std::vector<uint32_t>* within = nullptr,
                                        const std::atomic<int32_t>* cancel = nullptr, bool withDeleted = false) const;
    // Sorts `ids` (unknown values at the end of the list in both directions; on ties, database order).
    bool sort(std::vector<uint32_t>& ids, Column column, bool ascending, const std::atomic<int32_t>* cancel = nullptr) const;

    // Position index (.hy64p), if it exists and is up to date.
    PositionIndex* positions() const { return m_positions.get(); }
    bool reloadPositions(std::string* error = nullptr);

private:
    // `flush` false: writes in series (ECO classification, name correction), flushed once at the end.
    bool writeAt(std::FILE* f, uint64_t at, const void* data, size_t n, bool flush = true);
    bool appendNames(const std::vector<uint8_t>& bytes, std::string* error);
    bool openWrite(std::string* error);
    bool appendEncoded(GameInfo& info, const std::vector<uint8_t>& blob, uint32_t* id, std::string* error);
    uint32_t internInfo(GameInfo& info, std::vector<uint8_t>& appendedNames);

    std::string m_base;
    bool m_open = false;
    bool m_packed = false;      // single-file database: m_base is that file
    uint64_t m_packOffset[4] = {0, 0, 0, 0}, m_packSize[4] = {0, 0, 0, 0};
    uint64_t m_journalStart = 0, m_journalEnd = 0; // journal of the single-file database
    std::vector<uint64_t> m_tailOffsets;           // offset in the file of each index row of the journal
    bool writeRecord(uint32_t id, const uint8_t* rec, bool flush = true); // index row, wherever it is
    bool appendGameBytes(const std::vector<uint8_t>& blob, uint32_t gameCount);
    bool journalAppend(char type, const uint8_t* data, size_t size, uint64_t* payloadAt = nullptr);
    bool journalCommit();
    MappedFile m_indexMap, m_gamesMap;
    Names m_names;
    uint32_t m_mappedCount = 0;
    mutable std::mutex m_tailMutex;
    std::vector<Record> m_tail;        // rows added since opening
    std::vector<uint8_t> m_tailGames;  // blocks added since opening
    uint64_t m_mappedGamesEnd = 0;
    uint64_t m_gamesEnd = 0;
    uint64_t m_namesEnd = 0;
    std::FILE* m_indexW = nullptr;
    std::FILE* m_namesW = nullptr;
    std::FILE* m_gamesW = nullptr;
    std::unique_ptr<PositionIndex> m_positions;
};

} // namespace hy64db
