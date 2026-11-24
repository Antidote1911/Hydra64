#pragma once

#include "chess/game.h"
#include "chess/treebook.h"
#include "db/format.h"
#include "db/headerquery.h"

#include <QList>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Indexed game database, read on demand (millions of games): ChessBase (read-only)
// or Hydra64 database (editable, positions). The Databases page displays it without a proxy; searches and
// sorts run in another thread (const methods marked "thread"), the view is installed
// afterwards from the interface thread.
class IndexedGameSource {
public:
    struct Header {
        quint32 id = 0; // number of the game in the database
        QString white, black, event;
        QString date;   // "YYYY.MM.DD" (with "?")
        QString result; // « 1-0 », « 0-1 », « 1/2-1/2 », « * »
        QString eco;
        QString site, round;
        int plies = -1; // half-moves of the main line (-1: unknown)
        int whiteElo = 0, blackElo = 0;
        bool deleted = false; // marked deleted (displayed struck through until compaction)
        uint8_t userFlags = 0; // user flags (6 bits)
        int beauty = -1; // beauty mark 0…3 (db/beauty.h); -1: not rated, or not a Hydra64 database
    };
    // List of games (search result, sort order).
    class View {
    public:
        virtual ~View() = default;
        virtual int count() const = 0;
    };
    using Cancel = std::atomic<int32_t>;

    virtual ~IndexedGameSource() = default;

    virtual QString formatName() const = 0; // "ChessBase", "Hydra64"
    virtual bool isReadOnly() const = 0;
    virtual int total() const = 0;  // games of the database (without the deleted ones)
    virtual int deletedCount() const { return 0; } // games marked deleted
    virtual int count() const = 0;  // rows of the view
    virtual bool isFiltered() const = 0;
    virtual bool header(int row, Header& out) const = 0;
    virtual bool game(int row, chess::Game& out, QString* error) const = 0;
    virtual QString gameText(int row) const = 0;

    // Year, Elo and result (filter charts): fast, without the names. Result: 1 White
    // win, 0 draw, -1 Black win, 2 unknown.
    struct Summary {
        int year = 0, whiteElo = 0, blackElo = 0, result = 2;
        uint16_t eco = 0; // hy64db::parseEco (1 = A00), 0 unknown
    };
    virtual bool summary(int row, Summary& out) const
    {
        Header h;
        if (!header(row, h)) return false;
        out.year = h.date.left(4).toInt();
        out.whiteElo = h.whiteElo;
        out.blackElo = h.blackElo;
        out.result = h.result == QLatin1String("1-0") ? 1 : h.result == QLatin1String("0-1") ? -1 : h.result == QLatin1String("1/2-1/2") ? 0 : 2;
        out.eco = hy64db::parseEco(h.eco.toStdString());
        return true;
    }
    virtual std::unique_ptr<View> search(const QString& text, const Cancel* cancel, QString* error) const = 0;      // thread
    virtual std::unique_ptr<View> viewIds(bool all) const = 0;                                                        // view copied, or the whole database
    virtual bool sort(View& view, int column, bool ascending, const Cancel* cancel, QString* error) const = 0;      // thread
    virtual void setView(std::unique_ptr<View> view, bool filtered) = 0; // nullptr: the whole database in its order

    // Search by headers, combined with the view `current` (thread); negation of the view.
    enum class Combine { Replace, Within, Add };
    virtual std::unique_ptr<View> searchHeaders(const hy64db::HeaderQuery&, Combine, const View*, const Cancel*, QString*) const { return nullptr; }
    virtual std::unique_ptr<View> negate(const View&) const { return nullptr; }
    // Row of the view of game no. `number` (1 = first of the database), -1 if absent from the view.
    virtual int rowOfNumber(int) const { return -1; }

    // Positions (Hydra64 databases with a position index)
    virtual bool hasPositions() const { return false; }
    virtual std::unique_ptr<View> searchPosition(const chess::Position&, const Cancel*, QString*) const { return nullptr; } // thread
    virtual std::vector<chess::treebook::BookMove> tree(const chess::Position&) const { return {}; }

    // Writing (editable databases); rows of the view.
    virtual int append(const chess::Game&, QString* error) { return fail(error); }
    virtual bool replace(int, const chess::Game&, QString* error) { return fail(error) >= 0; }
    // Deletion: marks (true) or restores (false); the rows stay in the view.
    virtual bool setDeleted(QList<int>, bool, QString* error) { return fail(error) >= 0; }
    virtual bool isDeleted(int) const { return false; }
    // User flags (Hydra64 databases): `flag` 0…5 set or cleared on the rows.
    virtual bool supportsUserFlags() const { return false; }
    virtual bool setUserFlag(const QList<int>&, int, bool, QString* error) { return fail(error) >= 0; }

protected:
    int fail(QString* error) const
    {
        if (error) *error = QStringLiteral("base en lecture seule");
        return -1;
    }
};
