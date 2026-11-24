#pragma once

#include "ui/indexedsource.h"

#include <map>
#include <mutex>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

// Reading of ChessBase databases (.cbh, .2cbh) and .cbv archives, through the Rust bridge
// src/chessbase/bridge (cbvault library, MIT). Read-only.
// Without cargo at build time, available() is false and open() fails cleanly.
class ChessBaseSource : public IndexedGameSource {
public:
    // Result of a search, computed off the interface thread then installed by setView().
    class Ids : public IndexedGameSource::View {
    public:
        Ids() = default;
        ~Ids();
        Ids(Ids&& other) noexcept : m_ids(std::exchange(other.m_ids, nullptr)) {}
        Ids& operator=(Ids&& other) noexcept;
        Ids(const Ids&) = delete;
        Ids& operator=(const Ids&) = delete;
        bool isValid() const { return m_ids != nullptr; }
        int count() const override;

    private:
        friend class ChessBaseSource;
        explicit Ids(void* ids) : m_ids(ids) {}
        void* m_ids = nullptr;
    };

    ChessBaseSource() = default;
    ~ChessBaseSource() override;
    ChessBaseSource(const ChessBaseSource&) = delete;
    ChessBaseSource& operator=(const ChessBaseSource&) = delete;

    static bool available();                          // ChessBase reading compiled in
    static bool isDatabasePath(const QString& path);  // .cbh, .2cbh
    static bool isArchivePath(const QString& path);   // .cbv, .2cbv
    // Extracts an archive into `dir`: .cbv (ChessBase format, through cbvault) or .2cbv of ChessBase 18
    // (a ZIP file, extracted here with zlib). Only file names are kept, never a path. (blocking: a few seconds for the Mega Database).
    static bool extractArchive(const QString& archive, const QString& dir, QString* error);
    static bool extractZip(const QString& archive, const QString& dir, QString* error);

    bool open(const QString& path, QString* error);
    bool isOpen() const { return m_db != nullptr; }
    bool isTwoCbh() const;
    QString gamePgn(int row, QString* error) const;
    // Header read without going through header()'s cache: usable from several threads (conversion).
    bool headerDirect(int row, Header& out) const;
    // Sets on `game` the tags taken from the database files (site, round, annotator, teams,
    // tournament, source); true if the database header says the game has no moves.
    bool headerExtras(int row, chess::Game& game) const;
    // Game meant to be encoded into a database: move notation not computed, and moves read
    // in binary when the game has neither annotation nor variation. Safe for parallel reading.
    bool gameForEncoding(int row, chess::Game& out, std::string* error) const;

    // IndexedGameSource
    QString formatName() const override { return QStringLiteral("ChessBase"); }
    bool isReadOnly() const override { return true; }
    int total() const override; // games of the database
    int count() const override; // rows of the view (the whole database, or the search result)
    bool isFiltered() const override { return m_filtered; }
    bool header(int row, Header& out) const override; // header of a row (cached)
    bool game(int row, chess::Game& out, QString* error) const override;
    QString gameText(int row) const override { return gamePgn(row, nullptr).trimmed(); }
    // Search by the start of a player or tournament name; can run in another thread
    // (read-only) as long as the source exists. Null result if cancelled or failed.
    std::unique_ptr<View> search(const QString& text, const Cancel* cancel, QString* error) const override;
    // Games to sort: the current view, or the whole database.
    std::unique_ptr<View> viewIds(bool all) const override;
    // Sort by a column of the list (GameListModel::Column); like search(), usable
    // from another thread. False if cancelled or failed.
    bool sort(View& ids, int column, bool ascending, const Cancel* cancel, QString* error) const override;
    // Installs a view: nullptr = the whole database in its order; `filtered` = search result.
    void setView(std::unique_ptr<View> ids, bool filtered) override;

private:
    void* m_db = nullptr;
    bool m_filtered = false;
    // Small cache of the displayed headers (scrolling often asks for the same rows again).
    mutable int m_cacheRow[256];
    mutable Header m_cache[256];
};

#include "cbtexts.h"
#include "db/convert.h"

#include <vector>

// ChessBase game source for conversion to a Hydra64 database (safe for parallel reading).
class ChessBaseGameSource : public hy64db::GameSource {
public:
    bool open(const QString& path, QString* error);
    uint64_t count() const override { return m_count; }
    bool read(uint64_t index, chess::Game& game, std::string* error) const override;
    bool readForEncoding(uint64_t index, chess::Game& game, std::string* error) const override;
    // Texts of the database (.cbh): kept in their place among the games, each as a game
    // without moves (White = title, event "Texte", the text as a comment).
    int textCount() const { return int(m_texts.size()); }
    static void textAsGame(const CbText& text, chess::Game& game);
    Partial partial() const override;
    // Game whose moves are unreadable in the database: it keeps its place and its headers, without moves,
    // with the reason as a comment. Nothing is dropped silently, and the numbers stay ChessBase's.
    bool unreadableAsGame(int row, const std::string& why, chess::Game& game) const;

private:
    // Rank among the games (≥ 0), or -1 - number of the text.
    int64_t locate(uint64_t index) const;
    ChessBaseSource m_source;
    uint64_t m_count = 0;
    QList<CbText> m_texts;
    void notePartial(uint64_t index, const chess::Game& game) const; // game returned without annotations or without moves
    mutable std::mutex m_partialLock;
    mutable std::map<uint64_t, std::pair<bool, std::string>> m_partial; // rank → (without moves?, reason); a game read again is counted only once
    std::vector<uint64_t> m_textAt; // rank of each text in the sequence games + texts
};
