#pragma once

#include "chess/game.h"
#include "mappedfile.h"
#include "positionindex.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hy64db {

// Game source for conversion. `read` must be callable from several threads.
class GameSource {
public:
    virtual ~GameSource() = default;
    virtual uint64_t count() const = 0;
    virtual bool read(uint64_t index, chess::Game& game, std::string* error) const = 0;
    // Game that will only be encoded into a database: the move notation (Node::san) may
    // stay empty, which saves computing it.
    virtual bool readForEncoding(uint64_t index, chess::Game& game, std::string* error) const { return read(index, game, error); }
    // Games returned incomplete because they are damaged in the source (without their annotations, or without
    // their moves): count of each kind and the first ones, for the conversion report.
    struct Partial {
        uint64_t withoutAnnotations = 0, withoutMoves = 0;
        std::vector<std::string> first;
    };
    virtual Partial partial() const { return {}; }
};

// PGN file (mapped: a file of several GB is not loaded into memory).
class PgnSource : public GameSource {
public:
    bool open(const std::string& path, std::string* error);
    uint64_t count() const override { return m_games.size(); }
    bool read(uint64_t index, chess::Game& game, std::string* error) const override;
    bool readForEncoding(uint64_t index, chess::Game& game, std::string* error) const override;

private:
    MappedFile m_file;
    std::vector<std::pair<uint64_t, uint64_t>> m_games; // offset, length
};

// EPD file (one game per position, see chess::epd::toPgn).
class EpdSource : public GameSource {
public:
    bool open(const std::string& path, std::string* error);
    uint64_t count() const override { return m_games.size(); }
    bool read(uint64_t index, chess::Game& game, std::string* error) const override;

private:
    std::string m_pgn;
    std::vector<std::pair<uint64_t, uint64_t>> m_games; // offset, length in m_pgn
};

class Database;

// Hydra64 database as a source (non-deleted games, in database order).
class DatabaseSource : public GameSource {
public:
    DatabaseSource();
    ~DatabaseSource() override;
    bool open(const std::string& path, std::string* error);
    uint64_t count() const override { return m_ids.size(); }
    bool read(uint64_t index, chess::Game& game, std::string* error) const override;

private:
    std::unique_ptr<Database> m_db;
    std::vector<uint32_t> m_ids;
};

struct ConvertOptions {
    int positionDepth = DefaultPositionDepth; // 0: no position index
    unsigned threads = 0;                      // 0: depending on the machine
};

struct ConvertResult {
    uint64_t games = 0;                // games written
    uint64_t failed = 0;               // unreadable games (skipped)
    std::vector<std::string> failures; // the first ones, for the report
    double seconds = 0;
};

// Steps reported to `progress(step, done, total)`; returning false cancels (nothing is kept).
enum ConvertStep { StepGames = 0, StepPositions = 1 };
using ConvertProgress = std::function<bool(ConvertStep, uint64_t, uint64_t)>;

// Converts `source` into the Hydra64 database `destination` (base path or .hy64i). An
// existing database of the same name is replaced.
bool convert(const GameSource& source, const std::string& destination, const ConvertOptions& options, ConvertResult* result,
             std::string* error, const ConvertProgress& progress = {});

// Writes all the games of `source` to a PGN file (open format), in order.
bool exportPgn(const GameSource& source, const std::string& destination, ConvertResult* result, std::string* error,
               const ConvertProgress& progress = {});

} // namespace hy64db
