#pragma once

#include <functional>

#include "chess/pgn.h"
#include "chessbase/chessbase.h"
#include "indexedsource.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QPoint>

class QMimeData;
#include <QWidget>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTabBar;
class QTableView;
class QThread;

// Game database: PGN file indexed in memory, displayed without decoding it entirely, or an
// indexed database read on demand (Hydra64 .hy64i database, editable; ChessBase .cbh/.2cbh, read-only).
class GameListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // Same order as hy64db::Column (sort in Hydra64 databases)
    enum Column { Number, White, WhiteElo, Black, BlackElo, Result, Date, Event, Eco, Site, Round, Moves, Beauty, ColumnCount };
    static QList<int> defaultHiddenColumns() { return {Round}; }

    using QAbstractTableModel::QAbstractTableModel;

    bool loadFile(const QString& path, QString* error); // PGN, Hydra64 or ChessBase depending on the extension
    bool isIndexed() const { return m_indexed != nullptr; }
    bool isChessBase() const { return m_indexed && m_indexed->formatName() == QLatin1String("ChessBase"); }
    bool isReadOnly() const { return m_indexed && m_indexed->isReadOnly(); }
    // EPD file (open format): read as a database, one game per position; each
    // change rewrites the file as EPD (chess::epd::fromPgn), one line per game.
    static bool isEpdPath(const QString& path);
    bool isEpd() const { return m_epd; }
    IndexedGameSource* indexed() const { return m_indexed.get(); }
    // Put to sleep while another thread closes and reopens the database (journal tidying):
    // data() no longer reads anything, whichever view asks; on waking the views
    // redraw themselves.
    void setSuspended(bool suspended);
    int totalGames() const { return m_indexed ? m_indexed->total() : rowCount(); }
    // View of an indexed database: nullptr = the whole database in its order; `filtered` = search result.
    void setIndexedView(std::unique_ptr<IndexedGameSource::View> view, bool filtered);
    QString filePath() const { return m_path; }
    bool parseGame(int row, chess::Game& out, QString* error) const;
    QString gameText(int row) const; // raw PGN of the game, as it is in the file
    // Deletes games. PGN: removed from the file (atomic rewrite; the other games
    // are kept byte for byte). Hydra64 database: marked, they stay displayed
    // struck through (restorable) until compaction.
    bool removeGames(QList<int> rows, QString* error);
    bool restoreGames(const QList<int>& rows, QString* error);
    bool isDeleted(int row) const { return m_indexed && m_indexed->isDeleted(row); }
    // User flags (Hydra64 databases)
    static QString userFlagName(int flag);
    static QString userFlagLetter(int flag);
    bool supportsUserFlags() const { return m_indexed && m_indexed->supportsUserFlags(); }
    uint8_t userFlags(int row) const;
    bool summary(int row, IndexedGameSource::Summary& out) const; // year, Elo, result
    int numberOf(int row) const; // number of the game in the database (1 = first)
    bool setUserFlag(const QList<int>& rows, int flag, bool on, QString* error);
    // PGN database: row filter of the header search (empty: none)
    std::vector<char> matchHeaders(const hy64db::HeaderQuery& q) const;
    void setRowFilter(std::vector<char> rows);
    const std::vector<char>& rowFilter() const { return m_rowFilter; }
    bool rowAllowed(int row) const { return m_rowFilter.empty() || (row >= 0 && row < int(m_rowFilter.size()) && m_rowFilter[size_t(row)]); }
    int appendGame(const std::string& pgn, QString* error);            // new row, or -1
    // Several games at once (a single rewrite of a PGN file); number added, or -1.
    int appendGames(const std::vector<std::string>& pgns, QString* error);
    // Two Hydra64 databases: direct copy of games `rows` of `from` (deleted ones skipped); -2 if that is not the case.
    int copyFrom(const GameListModel& from, const QList<int>& rows, QString* error);
    // PGN database: `edit` on each game (true: game modified, rewritten), file rewritten only
    // once. Returns the number of games modified, -1 on error.
    int rewriteGames(const std::function<bool(chess::Game&)>& edit, QString* error, const std::function<bool(int, int)>& progress = {});
    bool replaceGame(int row, const std::string& pgn, QString* error);

    int rowCount(const QModelIndex& = {}) const override { return m_indexed ? m_indexed->count() : int(m_entries.size()); }
    int columnCount(const QModelIndex& = {}) const override { return ColumnCount; }
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    // Dragging games: source database and rows (MimeType), and their PGN as text.
    static inline const QString MimeType = QStringLiteral("application/x-hydra64-games");
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override { return {MimeType, QStringLiteral("text/plain")}; }
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }

private:
    bool m_suspended = false;
    bool writeData(std::string data, QString* error); // atomic write then reindexing
    int appendText(const std::vector<std::string>& pgns, QString* error); // append to the end of the PGN file
    QString m_path;
    std::string m_data;
    bool m_epd = false; // m_data: PGN produced from an EPD file (rewritten as EPD)
    std::vector<chess::pgn::GameEntry> m_entries;
    std::unique_ptr<IndexedGameSource> m_indexed;
    std::vector<char> m_rowFilter;
    mutable std::vector<int> m_moves; // PGN: moves counted on demand (-1: not yet)
};

// Open databases, one per tab; model() is the displayed database.
class GameListPanel : public QWidget {
    Q_OBJECT
public:
    explicit GameListPanel(QWidget* parent = nullptr);
    // List of the open databases (one row per database, close button, drop of games, "+"
    // button): a separate widget, which the window places in the "Databases" panel; this
    // panel is now only the game list of the chosen database.
    QWidget* basesWidget() const { return m_basesBox; }
    ~GameListPanel() override;
    bool isSearching() const { return m_search != nullptr; }
    void waitForSearch(); // waits for the current task to end and installs its result
    // Indexed database with positions: games going through `pos` (in the background). False if the
    // displayed database does not allow it.
    bool searchPosition(const chess::Position& pos);
    // Search that replays the games (Hydra64 database): manoeuvre, pawn structure, same material.
    // `scan` runs in a thread; `label` names the result in the status bar.
    using Scan = std::function<std::vector<uint32_t>(const hy64db::Database&, const std::atomic<int32_t>*)>;
    bool searchByReplay(const QString& label, Scan scan);

    // Opens the database in a new tab (or shows its tab if it is already open).
    bool openFile(const QString& path, QString* error);
    bool activate(const QString& path); // shows the database if it is open
    void reload(const QString& path);   // rereads the database if it is open (without changing tab)
    void closeDatabase(int index);
    void closeCurrent() { closeDatabase(currentIndex()); }
    // Single-file database: moves its journal into the body of the file if it exceeds TidyThreshold
    // (`always`: whatever its size). Done when a database and the application are closed.
    static constexpr qint64 TidyThreshold = 4 * 1024 * 1024;
    bool tidyDatabase(GameListModel* model, bool always);
    void tidyAll();
    int count() const { return int(m_bases.size()); }
    int currentIndex() const;
    QStringList openPaths() const;
    GameListModel* modelFor(const QString& path) const; // open database, nullptr otherwise
    GameListModel* model() const { return m_model; } // displayed database (empty if none)
    void selectGame(int row); // row of the source model
    QList<int> selectedRows() const; // rows of the source model
    void deleteSelected();  // selected games all deleted (Hydra64 database): restored
    void restoreSelected();
    // Search by headers (Database → Advanced search), combined with the current result;
    // invert the result; the whole database. False if no database is displayed.
    bool searchHeaders(const hy64db::HeaderQuery& q, IndexedGameSource::Combine combine);
    bool negateFilter();
    int randomRow() const;          // row (of the model) of a visible game drawn at random, -1 otherwise
    QList<int> visibleRows() const; // rows (of the model) of the displayed games, in displayed order
    // Displayed columns (right-click on the header), remembered; the number always stays displayed.
    void setColumnVisible(int column, bool visible);
    bool isColumnVisible(int column) const;
    int rowOfNumber(int number);    // row of game no. `number` (search cleared if needed), -1 otherwise
    void clearFilter();
    void refreshCount() { updateCount(); }
    // Adds to database `to` (open) the games `rows` of database `from` (open); deleted
    // games are skipped. Number of games added, or -1.
    int copyGames(const QString& from, const QList<int>& rows, const QString& to, QString* error);

signals:
    void gameActivated(int row);
    void statusMessage(const QString& msg);
    void gamesRemoved(const QList<int>& rows);
    void editInfoRequested(int row);
    void annotateRequested(const QList<int>& rows); // automatic annotation (batch)
    void playerReportRequested(const QString& player); // profile of a player of the game
    void eventTableRequested(int row);                 // crosstable of the game's tournament
    void clipboardRequested(const QList<int>& rows);                 // copy to the clipboard database
    void mergeRequested(const QList<int>& rows, bool intoAnalysis); // merge games into one
    void exportHtmlRequested(int row);                 // game as a standalone HTML page
    void previewRequested(int row);                    // preview of the game
    void sendToAnalysisRequested(int row);
    void databasesChanged(); // database opened, closed or displayed
    void openRequested();    // "+" button of the tabs
    void compactRequested(); // right-click on a database of the list: "Compact database" (the database is already displayed)

protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // drop of games on a tab

private:
    int dropTab(const QMimeData* mime, const QPoint& pos) const; // tab accepting the drop, or -1
    int dropBase(const QMimeData* mime, int index) const;        // database no. index accepts the drop: index, otherwise -1
    void highlightTab(int index);
    void highlightBase(int row);
    void syncBaseList();
    int m_highlighted = -1;
    QWidget* m_dropMark = nullptr; // frame on the targeted tab during the drag
    struct Base {
        GameListModel* model;
        QString filter;
        // Indexed database: sort chosen by clicking on the headers (-1: database order).
        int sortColumn = -1;
        Qt::SortOrder sortOrder = Qt::AscendingOrder;
    };
    int indexOf(const QString& path) const;
    void addTab(const QString& path);
    void show(int index);
    void updateCount();
    void showContextMenu(const QPoint& pos);
    int sourceRow(const QModelIndex& index) const; // row of the model, with or without the proxy
    // Indexed database, in the background: new search of the filter text (Enter), position
    // search, or only a new sort of the current view; then the chosen sort.
    enum class Job { Sort, Search, Position, Header, Negate, Replay };
    Scan m_scan;
    QString m_scanLabel;
    void startIndexedJob(Job job, const chess::Position* pos = nullptr, const hy64db::HeaderQuery* query = nullptr,
                         IndexedGameSource::Combine combine = IndexedGameSource::Combine::Replace);
    void startSearch() { startIndexedJob(Job::Search); }
    void stopSearch();   // interrupts the current task and waits for the thread to end
    void sortIndexed(int column); // click on a header

    QList<Base> m_bases;
    GameListModel* m_empty;  // shown when no database is open
    GameListModel* m_model;
    class RowFilterProxy* m_proxy;
    QTabBar* m_tabs; // reference state (current database, order), never displayed: see basesWidget()
    QWidget* m_basesBox = nullptr;
    class QListWidget* m_baseList = nullptr;
    // Its viewport, kept to recognise it in eventFilter without asking the list: the last events of the
    // viewport arrive while the list is being destroyed.
    QObject* m_baseListViewport = nullptr;
    int m_dropRow = -1;
    // Open databases as a list (one row per database) or as large icons, ChessBase style.
    bool m_basesAsIcons = false;
    class QToolButton* m_basesListButton = nullptr;
    class QToolButton* m_basesIconsButton = nullptr;
    void setBasesAsIcons(bool icons);
    void arrangeColumns(); // "Round" last, the only column stretched over the remaining width
    void fitColumns();     // measures the widest content of each column (sample of rows), then layoutColumns()
    void layoutColumns();  // shares out the list's width: all columns visible, never wider than their content
    QList<int> m_naturalWidths; // content width of each column, measured by fitColumns()
    int m_fillerColumn = -1;    // column receiving the remaining width (arrangeColumns)
    QHash<int, int> m_manualWidths; // widths set by hand on the header, kept by layoutColumns()
    void saveManualWidths();
    bool m_layingOut = false;    // layoutColumns() is resizing the sections (not the user)
    bool m_headerPressed = false; // mouse button held on the header
    // Open databases reordered with the mouse (eventFilter)
    int m_dragRow = -1;
    QPoint m_dragStart;
    bool m_reordering = false;
    class QTimer* m_typing = nullptr; // indexed database: the search starts a moment after the last keystroke
    QMetaObject::Connection m_fitOnReset; // new content of the displayed list: widths recomputed
    QTableView* m_table;
    QLineEdit* m_filter;
    QLabel* m_count;
    // ChessBase search in the background
    QThread* m_search = nullptr;
    GameListModel* m_searchModel = nullptr;
    Job m_job = Job::Sort; // nature of the current task
    std::shared_ptr<std::atomic<int32_t>> m_cancel;
};
