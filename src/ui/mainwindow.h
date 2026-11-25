#pragma once

#include "db/patternsearch.h"

#include "addexercise.h"
#include "annotator.h"
#include "db/database.h"
#include "conversion.h"

#include <atomic>
#include <functional>

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QMainWindow>

#include "chess/types.h"
#include "engine/engineregistry.h"

#include <array>

#include <QImage>

class AutoEvaluator;
class BookPanel;
class NotationView;
class BoardViewer;
class EngineGame;
class EnginePanel;
class EvalGraph;
class GameListPanel;
class GameSession;
class ClockPanel;
class NotationPanel;
class PlayEnginePanel;
class QButtonGroup;
class QAction;
class QLabel;
class QMenu;
class QToolButton;
class Tournament;
class TournamentPanel;

namespace ads {
class CDockAreaWidget;
class CDockManager;
class CDockWidget;
} // namespace ads

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // Each page has its own game (same numbering as GameSession::Slot).
    enum Page { PageDatabase = 0, PageAnalysis = 1, PageBoard = 2, PageTournament = 3, PageCount = 4 };

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openDatabase(const QString& path);
    // File → Convert… (source prefilled if given).
    // `tree`: the opening tree of a game database is offered first
    void convertDialog(const QString& source = {}, bool tree = false);
    // Engine → Solution test… (single window, the test goes on with the window closed)
    class SolutionTestDialog* solutionTestDialog();
    void analysePosition(const QString& fen); // position alone in the Analysis page
    void analyseGame(chess::Game game);       // game (or line) in the Analysis page, after confirmation
    class DeepAnalysis* deepAnalysis() const { return m_deep; }
    class Coach* coach() const { return m_coach; }
    class EngineGame* engineGame() const { return m_play; }
    class DeepPanel* deepPanel() const { return m_deepPanel; }
    // Game → Annotate game…; annotateGame annotates the displayed game (progress, cancellation).
    void annotateDialog();
    bool annotateGame(const struct EngineConfig& engine, const annotation::Options& options, QString* summary = nullptr);
    // Batch annotation: games `rows` of the displayed database, saved again annotated. Number of
    // games annotated (-1: read-only database), `summary`: report.
    int annotateGames(const QList<int>& rows, const struct EngineConfig& engine, const annotation::Options& options, QString* summary = nullptr);
    // Converts (progress dialog), then opens the result if asked: database in the Databases
    // page, tree in the Opening tree panel. `showReport`: report in a message box.
    bool runConversion(conversion::Request request, bool openResult, bool showReport = true);
    void loadGameFromDatabase(int row); // opens the game on the Databases page
    void setPage(Page page);
    Page page() const { return m_page; }
    bool startEngineGame(const struct PlaySettings& settings);
    // Rematch: same settings and same starting position, colours reversed.
    bool rematch();
    void flushFloatingContainers(); // QtADS floating windows awaiting deletion: deleted right away
    bool continueFlaggedGame(); // game ended on time, still on screen: it continues (notation and clocks kept)
    EnginePanel* enginePanel() const { return m_engine; }
    Tournament* tournament() const { return m_tournament; }
    GameListPanel* gamesPanel() const { return m_games; }
    BookPanel* bookPanel() const { return m_book; }
    NotationView* notationView() const;
    TournamentPanel* tournamentPanel() const { return m_tournamentPanel; }

    // Copies the game of the displayed page (Databases, Play, Tournament) to the Analysis page.
    // `confirm`: ask before replacing an existing analysis.
    void sendToAnalysis(bool confirm = true);
    void sendDatabaseGameToAnalysis(int row);
    void editPosition();
    bool editGameInfo(); // displayed game; false if cancelled
    void editDatabaseGameInfo(int row);
    void saveToDatabase(bool chooseFile = false);
    // Help → Reset: the window closes without saving anything; main() then calls
    // wipeUserData() and restarts the application, as after a fresh installation.
    bool resetRequested() const { return m_resetRequested; }
    static void wipeUserData();
    void showAbout();
    void showPanel(const QString& name); // opens a panel by its name (e.g. "clock"), like the View menu

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    // Pages where a menu action is offered (bit combination).
    enum PageMask { OnDatabase = 1, OnAnalysis = 2, OnBoard = 4, OnTournament = 8, OnAll = 15 };

    void createActions();
    QAction* pageAction(QMenu* menu, int pages, QAction* action);
    void updateMenus(); // menus of the displayed page only
    QWidget* createPageBar();
    void applyDefaultLayout(Page page);
    void arrangeCenter(Page page);
    void applyDisplayPrefs();
    void onCurrentChanged(bool animate);
    void updateTitle();
    void manageEngines(const QString& engineId = {});
    void showPreferences();
    void resetApplication();
    void pasteFenOrPgn();
    void exportGame();

public:
    // Game as a standalone HTML page (board, moves, variations, comments). Empty `path`: asked.
    bool exportGameHtml(const chess::Game& game, const QString& path = {});
    // Game laid out for paper: headers, moves, variations, comments, and a diagram
    // at each "▦" marker (without a marker: a diagram every ten moves). Empty `path`: asked.
    bool exportGamePdf(const chess::Game& game, const QString& path = {});
    void printGame(const chess::Game& game);
    // Document ready to print; the number of diagrams is returned in `diagrams`.
    void fillGameDocument(class QTextDocument& doc, const chess::Game& game, int* diagrams = nullptr);
    // Merges games `rows` of the displayed database: into a new game, or into that of the
    // Analysis page (`intoAnalysis`). Number of games merged, -1 if none could be.
    int mergeGames(const QList<int>& rows, bool intoAnalysis);
    // Searches that replay the games of the displayed database (Hydra64); false if the database is not suitable.
    bool searchSimilar(bool pawnStructure);            // pawn structure, or material, of the board
    bool searchManeuver(const hy64db::ManeuverQuery& q);
    // Browsable index of the displayed database (Hydra64); null if the database is not suitable.
    class IndexDialog* showIndex();
    // Training on the questions of the displayed game.
    class QuizDialog* startQuiz();
    // Replay of the displayed game guessing the moves of `side` from move `fromMove` (asked if 0).
    class GuessDialog* startGuess(chess::Color side = chess::White, int fromMove = 0);
    // Texts of the displayed ChessBase database (.cbh): list of titles and reading; number of texts, -1 if the database has none.
    int showChessBaseTexts(bool show = true);
    bool showGamesOf(int nameType, quint32 id, const QString& name);
    void askManeuver();                                // dialog box
    // Clipboard database: a Hydra64 database (one file) where to drop games gathered here and there.
    static QString clipboardBasePath();
    int copyToClipboardBase(const QList<int>& rows);   // games of the displayed database; number copied, -1 on failure
    bool addCurrentGameToClipboardBase();
    bool openClipboardBase();                           // shows it (created if needed)
    bool emptyClipboardBase();
    // Opening repertoire: one tree per side, in ~/Documents/hydra64/repertoire.
    static QString repertoirePath(chess::Color color);
    static bool loadRepertoire(chess::Color color, chess::Game& out);
    static bool saveRepertoire(chess::Color color, const chess::Game& game, QString* error = nullptr);
    int addLineToRepertoire(chess::Color color);  // line leading to the displayed move; moves added, -1 on failure
    void openRepertoire(chess::Color color);       // in the Analysis page
    // Displayed games of the database compared with the repertoire; HTML text of the report (empty: nothing to say).
    QString repertoireReport(chess::Color color, bool show = true);

private:
    void newDatabase();
    void openDatabaseDialog(); // choose a PGN file then open it (Databases page)
    // ChessBase archive (.cbv): extracted once (folder offered, progress), then the
    // extracted database is opened. Returns the extracted .cbh/.2cbh, empty if cancelled or failed.
    QString extractChessBaseArchive(const QString& archive);
    // New empty database: PGN, or Hydra64 if the path ends with .hy64i. False (message) on failure.
    bool createDatabaseFile(const QString& path);

    void searchBoardPosition();     // games of the displayed database going through the position
    void compactDatabase();         // Hydra64 database: removes the deleted games
public:
    // Database → Sort the database in displayed order: Hydra64 database rewritten in the order of the chosen
    // sort (click on a header). `confirm`: ask first.
    bool sortDatabase(bool confirm = true);
    // Database → Duplicates…: search (criteria), then display or marking (games struck through)
    void twinsDialog();
    // Duplicates of the displayed database; `show`: view of the groups, `mark`: duplicates marked deleted.
    // Returns the number of duplicates (surplus games), -1 if impossible.
    int handleTwins(const hy64db::TwinCriteria& c, bool show, bool mark);
    // Database → Correct a name…; renameInDatabase does the replacement (number of games modified).
    void renameDialog();
    // Database → Player profile… (displayed Hydra64 database); returns the (non-modal) window or nullptr.
    class PlayerReportDialog* playerReport(const QString& player = {});
    // Preparation against a player of the displayed database (his colour: White unless `black`)
    class PreparationDialog* preparation(const QString& player = {}, bool black = false);
    class AssistedAnalysis* assistedAnalysis() const { return m_assisted; }
    // Integrity test of the displayed Hydra64 database: every game read again. Text of the result
    // (empty: not a Hydra64 database, or cancelled); `show`: in a message box.
    QString checkDatabaseIntegrity(bool show = true);
    // Where the pieces stand over the displayed games of a Hydra64 database (counted in the background).
    class PieceHeatDialog* pieceHeat();
    // Layouts of the panels saved under a name, page by page (View → Dispositions).
    QStringList savedLayouts() const;               // those of the displayed page, by name
    bool saveLayoutAs(const QString& name);         // the displayed layout; replaces one of the same name
    bool applySavedLayout(const QString& name);
    void removeSavedLayout(const QString& name);
    // Player names of the displayed Hydra64 database rewritten as `canonical` spells them (the
    // players encyclopedia if null). `ask` false: the sure ones are applied without the window.
    // Number of names changed, -1 if nothing could be done.
    int improvePlayerNames(const std::function<std::string(const std::string&)>& canonical = {}, bool ask = true);
    // Crosstable of the tournament of game `row` (displayed Hydra64 database).
    class EventTableDialog* eventTable(int row);
    // Database → Classify games (ECO); Game → Identify the opening. Number of games modified.
    int classifyDatabase(bool confirm = true);
    // Beauty mark of every game of the displayed Hydra64 database (db/beauty.h); games changed, -1 if none.
    int rateBeauty(bool confirm = true);
    QString identifyOpening(); // ECO and name set in the headers of the displayed game
    // Database → Export the displayed games: PGN, EPD (final position, or after `plies`
    // half-moves if > 0) or HTML. Returns the number of games written, -1 on error.
    enum class ExportFormat { Pgn, Epd, Html };
    int exportVisibleGames(ExportFormat format, const QString& path, int plies = 0);
    void exportVisibleDialog();
    class GamePreviewDialog* previewGame(int row); // preview of a game of the displayed database
    // Game → Finish game (Analysis page): the analysis engine plays both sides.
    bool finishGame(int moveTimeMs = 1000, int maxPlies = 200);
    class AutoPlayer* autoPlayer() const { return m_autoPlayer; }
    class TrainingDialog* trainingDialog(); // Game → Training… (single window)
    class CollectionCheckDialog* collectionCheckDialog(); // Engine → Check a collection… (single window)
    // Game → Add to collection…: displayed position, solution = next move of the notation
    bool addToCollection();
    std::unique_ptr<class AddExerciseDialog> addExerciseDialog(); // the dialog, prepared (tests)
    bool addExercise(const QString& path, const exercise::Exercise& ex);
    class TablebasePanel* tablebasePanel() const { return m_tablebase; }
    QImage boardImage() const; // displayed board (Game → Copy / Save the board as an image)
    bool saveBoardImage(const QString& path, QString* error = nullptr) const;
    class FilterGraphDialog* filterGraph(); // Database → Filter charts (non-modal window)
    class EcoBrowser* ecoBrowser();         // Database → ECO browser (counts of the open database)
    // Database → Opening surveys: list of the surveys indexed next to the displayed database
    // (surveyindex); nullptr if it has none.
    class SurveyBrowser* surveyBrowser();
    // Database → Opening report: board position in the open database (the list then shows
    // the games found). nullptr: no database, or cancelled.
    class OpeningReportDialog* openingReport(int maxSample = 5000);
    void showGameIds(const QString& path, const std::vector<uint32_t>& ids, const QString& title); // view of these games
    int64_t renameInDatabase(hy64db::NameType type, const QString& from, const QString& to);
    // Bookmarks (File → Bookmarks): a game of a database and the displayed move.
    struct Bookmark {
        QString name, path;
        int number = 0;
        std::vector<int> ply; // path of the displayed move (GameSession::pathTo)
    };
    static QList<Bookmark> bookmarks();
    bool addBookmark();               // displayed game (coming from a database)
    bool openBookmark(int index);
    void removeBookmark(int index);
private:
    void rebuildPositionIndex();    // Hydra64 database: (re)builds the position index
    void updateDatabaseTree();      // tree of the displayed database → Book panel
    // Adds the displayed game to an open Hydra64 database (notation menu).
    void addGameToDatabase(const QString& path);
    // Runs `work` in a thread behind a progress dialog ("percent": 0–1000,
    // ‑1 = indeterminate); `cancel` becomes true if the user cancels.
    bool runWithProgress(const QString& title, const QString& label, const std::function<bool(std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status)>& work,
                         bool cancellable);
    void restoreSettings();
    void saveSession() const; // game of each page → <AppData>/session.json
    void restoreSession();
    void startEngineGame();       // game settings (Ctrl+G)
    void startEngineForSideToMove(); // Play of the engine tab, with no game in progress
    // CPU button of an engine tab: its own settings (tabengine), without touching the other tabs
    void engineTabSettings(const QString& tab, const QString& tabTitle, const QString& engineId);
    void changeOpponent(const QString& engineId); // list of the engine tab: the game goes on
    void updateBoardInteraction();
    void onBoardMove(chess::Move m);
    void updateClocks();
    void updatePlayHint();
    void updateClockTitle();
    void placeClockDock();
    void lockStripHeights();   // Clock and Evaluation areas: fixed height
    void scheduleStripLock();  // …once the panels are settled
    bool m_stripLockPending = false;
    void updateAutoEval(); // automatic evaluation active if the Evaluation panel is open
    // Spectator engines added on the Analysis page (in addition to the first engine panel)
    EnginePanel* addSpectator(int page, const QString& engineId = {}, const QString& objectName = {}, int multiPv = 4, const QColor& color = {},
                              int sides = 0);
    QColor nextArrowColor() const;
    EngineConfig unusedEngine() const;
    void removeFirstEngine(); // the cross of the first engine panel
    void updateEngineStatus(); // status bar: engines currently analysing
    void addSpectatorFromUi(const QString& engineId = {}); // empty: an engine not used yet
    void renumberSpectators(); // titles "Spectateur 1, 2…", numbered page by page
    void removeSpectator(ads::CDockWidget* dock);
    void placeSpectator(ads::CDockWidget* dock);
    void saveSpectators() const;
    void restoreSpectators();
    void showLiveTournamentGame();
    void viewTournamentGame(int index);
    // Tournament page: a single board full size, or the overview of all the games in progress.
    void setTournamentOverview(bool on);
    void updateTournamentView();

public:
    bool tournamentOverviewShown() const;
    class TournamentOverview* tournamentOverview() const { return m_overview; }
    void showTournamentOverview(bool on) { setTournamentOverview(on); }

private:

    GameSession* m_session;
    BoardViewer* m_viewer;
    NotationPanel* m_notationPanel;
    EnginePanel* m_engine;
    EngineGame* m_play;
    PlayEnginePanel* m_playEngine;
    AutoEvaluator* m_autoEval;
    class AssistedAnalysis* m_assisted = nullptr;
    EvalGraph* m_graph;
    GameListPanel* m_games;
    class SolutionTestDialog* m_solutionTest = nullptr;
    Tournament* m_tournament;
    TournamentPanel* m_tournamentPanel;
    BookPanel* m_book;
    class DeepAnalysis* m_deep = nullptr;
    class Coach* m_coach = nullptr; // coach and hint of the Play page
    class AutoPlayer* m_autoPlayer = nullptr; // Finish game
    class TrainingDialog* m_training = nullptr;
    class CollectionCheckDialog* m_collectionCheck = nullptr;
    void showHint();
public:
    // Variations of an engine added to the notation from `pos` (position of the displayed game).
    int insertEngineLines(const chess::Position& pos, const QList<std::vector<chess::Move>>& lines);
    // Annotation options completed with the book exit (open opening tree)
    annotation::Options bookOptions(const chess::Game& g, annotation::Options o);
    // Game → Find the novelty: first move of the main line absent from an open Hydra64
    // database with a position index. Returns the node (or nullptr).
    chess::Node* findNovelty(QString* message = nullptr);
private:
    class DeepPanel* m_deepPanel = nullptr;
    ads::CDockWidget* m_deepDock = nullptr;
    class TablebasePanel* m_tablebase = nullptr;
    ads::CDockWidget* m_tablebaseDock = nullptr;
    chess::Move m_deepBest; // best move of the deep tree (arrow, Analysis page)
    void updateDeepArrow();

    ads::CDockWidget* makeDock(const QString& title, const QString& name, const QString& icon, QWidget* content, int minHeight = 0); // minHeight: floor of the panel (0: the common one)
    QList<ads::CDockWidget*> panels() const;

    ads::CDockManager* m_dockManager;
    ads::CDockWidget* m_notationDock;
    ads::CDockWidget* m_engineDock;
    ads::CDockWidget* m_playEngineDock;
    ads::CDockWidget* m_graphDock;
    ads::CDockWidget* m_gamesDock;
    ads::CDockWidget* m_boardDock = nullptr; // board of the Databases page (elsewhere it is in the centre)
    QWidget* m_gamesSide = nullptr;          // content of the "Databases" panel
    QWidget* m_gamesCenter = nullptr;        // centre of the Databases page: the game list
    QWidget* m_boardHolder = nullptr;        // content of the "Board" panel
    ads::CDockWidget* m_tournamentDock;
    ads::CDockWidget* m_clockDock;
    ads::CDockWidget* m_bookDock;
    ClockPanel* m_clockPanel;

    QButtonGroup* m_pageButtons = nullptr;
    QToolButton* m_logo = nullptr; // hydra head: Preferences, or the menus if the bar is hidden
    QMenu* m_compactMenu = nullptr;
    QLabel* m_statusEngine = nullptr;
    QAction* m_menuBarAction = nullptr;
    QAction* m_evalBarAction = nullptr;
    QAction* m_assistedAction = nullptr;
    QAction* m_threatAction = nullptr;
    QList<QMenu*> m_menus;
    QHash<QAction*, int> m_actionPages;
    Page m_page = PageAnalysis;
    bool m_pageShown = false;
    bool m_layoutPending = false;
    class TournamentOverview* m_overview = nullptr;
    class QStackedWidget* m_centerStack = nullptr; // board | tournament overview
    QAction* m_overviewAction = nullptr;
    bool m_overviewOn = false;
    bool m_undoAvailable = false, m_redoAvailable = false;
    bool m_followLive = true;           // the Tournament page follows the game in progress
    bool m_tournamentWasRunning = false;
    bool m_restoringDatabases = false;  // reopening of the databases at startup: remembered list left intact
    QString m_playHint;
    int m_layoutGeneration = 0;
    std::array<QByteArray, PageCount> m_pageStates;
    bool m_keepPageState = false; // setPage() on the same page: the state to restore has just been set
    std::array<bool, PageCount> m_pageFlipped{};
    struct Spectator {
        EnginePanel* panel;
        ads::CDockWidget* dock;
        int page;            // page of the spectator: PageAnalysis, PageBoard (Play) or PageDatabase (Databases)
    };
    QList<Spectator> m_spectators;
    int m_spectatorSerial = 0;
    bool m_closing = false;
    bool m_resetRequested = false;
    bool m_engineRemoved = false; // first engine panel removed by its cross
};
