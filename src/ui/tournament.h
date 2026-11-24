#pragma once

#include "chess/game.h"
#include "engine/engineregistry.h"
#include "engine/uciengine.h"

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QElapsedTimer>
#include <QStringList>

#include <array>
#include <memory>
#include <vector>

namespace chess::treebook { class Book; }
namespace chess::polyglot { class Book; }

class ChessClock;
class UciEngine;

// Engine tournament (round robin): each engine meets all the others,
// with both colours in turn. Games are played one after the other,
// with a clock (Fischer time control), each engine with its options from the engine manager.
class Tournament : public QObject {
    Q_OBJECT
public:
    // A participant: an engine of the engine manager, with a name and settings
    // specific to the tournament (a copy: the engine manager is never modified). The same
    // engine can take part several times with different settings.
    struct Participant {
        QString id;                    // identifier in the tournament (games, standings)
        QString engineId;              // engine of the engine manager
        QString name;                  // displayed name (games, standings, PGN)
        QMap<QString, QString> values; // UCI options (differing from the engine's default)

        static Participant fromEngine(const EngineConfig& engine); // current settings of the engine
        EngineConfig config() const; // engine of the registry with these settings; invalid if it has disappeared
        QJsonObject toJson() const;
        static Participant fromJson(const QJsonObject& o);
    };
    struct Settings {
        QList<Participant> participants;
        int cycles = 1;          // number of full cycles
        bool doubleRound = true; // reversed colours: each pairing with both colours
        qint64 baseMs = 60000;
        qint64 incMs = 1000;
        QString openingsFile;        // starting positions (EPD/FEN or PGN); empty: initial position
        bool openingsRandom = false; // drawn at random rather than in file order
        // Database of the tournament: each finished game is added to it. A Hydra64 database (.hy64)
        // for new tournaments; a .pgn path (tournament started by an earlier version) is still written as PGN.
        QString pgnFile;
        // Pairings: round robin, or gauntlet (the first participant against each of the others)
        // Swiss system: `cycles` rounds, paired as the tournament goes (score groups)
        enum class Schedule { RoundRobin, Gauntlet, Swiss };
        Schedule schedule = Schedule::RoundRobin;
        // Thinking limit: clock (baseMs + incMs), fixed time per move, or fixed depth
        enum class Limit { Clock, MoveTime, Depth };
        Limit limit = Limit::Clock;
        int moveTimeMs = 1000;
        int depth = 12;
        bool engineComments = true; // evaluation, depth and time of each move as a comment
        bool ponder = false;        // pondering by the engines (twice as many processors)
        int concurrency = 1;        // games played at the same time (each starts its two engines)
        QString bookFile;           // engine book (.hy64book or Polyglot .bin); empty: none
        int bookMaxMoves = 20;      // book moves at most (move number)
        // Automatic adjudication (both engines agreeing, over the last moves)
        bool drawAdjudication = false;
        int drawFromMove = 40, drawMoves = 8, drawScoreCp = 10;   // |eval| ≤ threshold
        bool resignAdjudication = false;
        int resignMoves = 4, resignScoreCp = 700;                 // eval ≥ threshold for the same side
        // Adjudication by the endgame tablebases: the game stops as soon as the position is in them
        // (tables chosen in Preferences → Tablebases), with the result they give.
        bool tablebaseAdjudication = false;
    };
    // Verdict of the tables for a position: PGN result and reason; false if it is not covered.
    static bool tablebaseVerdict(const chess::Position& pos, QString& result, QString& reason);
    struct GameRecord {
        QString whiteId, blackId;
        QString whiteName, blackName;
        QString result;   // empty: not played yet; "1-0", "0-1", "1/2-1/2"
        QString reason;
        QString pgn;
        QString opening; // "<fen> [moves <uci>…]"; empty: initial position
        int round = 0;   // round (Swiss system)
        // Game ended before any move (engine that could not be started, forfeit): it counts in the
        // standings, but is written neither to the tournament's database nor to the exports.
        bool noMoves = false;
    };
    struct Standing {
        QString id, name;
        double points = 0;
        int played = 0, wins = 0, draws = 0, losses = 0;
        // Estimated Elo difference against its opponents, 95% margin and LOS (NaN: undefined)
        double elo = 0, margin = 0, los = 0;
        bool hasElo = false;
    };
    // Elo difference from the results (cutechess-cli formulas); false if undefined (0 or 100%).
    static bool eloStats(int wins, int draws, int losses, double* elo, double* margin, double* los);
    static int gameCount(int participants, Settings::Schedule schedule, bool doubleRound, int cycles);

    explicit Tournament(QObject* parent = nullptr);

    const Participant* participant(const QString& id) const;
    ~Tournament() override;

    // New tournament (previous results are erased). False with fewer than two engines.
    bool setup(const Settings& settings, QString* error);
    void start(); // starts (or resumes): next unplayed game
    void stop();  // interrupts the game in progress, which will be replayed on resumption
    // Pause between two games, long enough to see the final position (800 ms). The tests shorten it.
    void setGamePauseMs(int ms) { m_gamePauseMs = std::max(0, ms); }

    bool isRunning() const { return m_running; }
    bool isComplete() const;
    bool isEmpty() const { return m_games.isEmpty(); }
    const Settings& settings() const { return m_settings; }
    const QList<GameRecord>& games() const { return m_games; }
    // Displayed game (the one the interface follows): its number (-1 if it is over or there is none),
    // its tree and its clock. With simultaneous games, the others are played without being displayed.
    int currentIndex() const;
    int runningCount() const; // games in progress
    // Simultaneous games: numbers of the games in progress (increasing), their tree and their clock;
    // showGame() chooses the one the interface follows (gameStarted is then emitted). False if the
    // game is not in progress.
    QList<int> runningGames() const;
    const chess::Game* runningGame(int index) const;
    const ChessClock* runningClock(int index) const;
    bool showGame(int index);
    int enginesReused() const { return m_enginesReused; } // engines reused from a previous game
    // True if the game of the last gameFinished() was the displayed game.
    bool finishedGameWasShown() const { return m_finishedShown; }
    int playedCount() const;
    const chess::Game& liveGame() const;
    const ChessClock* clock() const;
    QList<Standing> standings() const; // sorted standings
    QString exportPgn() const;          // all the games played (those without a move left out)

    // Starting positions of an EPD/FEN file (one per line) or PGN (starting position and
    // moves of the main line of each game), in the format "<fen> [moves <uci>…]".
    static QStringList loadOpenings(const QString& path, QString* error);
    // New Hydra64 database "Tournoi YYYY-MM-DD HHhMM.hy64" (numbered if it already exists) in `dir`.
    static QString newPgnPath(const QString& dir);
    // Adds a game to the tournament database `path`, created if missing (.hy64, .hy64i: Hydra64; otherwise PGN).
    static bool appendToDatabase(const QString& path, const chess::Game& game, QString* error);
    // Replaces appendToDatabase(): the window adds the game through the open tab when the database is
    // displayed on the Databases page (a single writer per file).
    void setGameSaver(std::function<bool(const QString&, const chess::Game&, QString*)> saver) { m_gameSaver = std::move(saver); }
    static QString defaultPgnDir();

    QJsonObject saveState() const;
    void restoreState(const QJsonObject& state);

signals:
    void changed();                                // results, state, settings
    void gameStarted(int index);                   // liveGame() has just been (re)initialised
    void moveMade(chess::Move move, int clockMs);  // appended to the end of liveGame()
    void gameFinished(int index);
    void pgnSaved(const QString& path); // a game has just been added to the tournament database
    void clockTick();
    void boardChanged(int index); // a game in progress (displayed or not) has started, advanced or ended
    void message(const QString& text);

private:
    // A game in progress: its two engines, its clock, its tree.
    struct Running {
        int index = -1;       // number in m_games, -1: free slot
        quint64 serial = 0;   // changes at each start or end of a game (ignores the deferred callbacks of a finished game)
        chess::Game live;
        std::array<UciEngine*, 2> engines{};
        std::array<QString, 2> engineIds{};   // participant of each engine (to keep it after the game)
        ChessClock* clock = nullptr;
        int ready = 0;
        QString expectedKey;
        std::array<EngineLine, 2> lastLine{}; // last main line of each engine
        QElapsedTimer thinkTimer;
        std::vector<int> evalHistory;         // evaluation of each move played (White's point of view)
        std::array<QString, 2> ponderKey{};   // expected position of each pondering engine
        bool bookMove = false;                // the current move comes from the book
        int played = 0;                       // moves played in this game (the imposed opening moves not counted)
    };
    void startNextGame();                 // fills the free slots
    void startGame(Running& g, int index);
    void requestMove(Running& g);
    void onBestMove(Running& g, int side, const QString& uci, const QString& key);
    // `engineFault`: an engine is at fault (frozen, illegal move, time exceeded…) → they are not kept.
    void finishGame(Running& g, const std::string& result, const QString& reason, bool engineFault = false);
    void onFlag(Running& g, chess::Color side);
    // End of a game: with `keep`, the engines still running wait for the next game of
    // their participant ("ucinewgame" sent) instead of being restarted.
    void shutdownEngines(Running& g, bool keep = false);
    UciEngine* takeIdleEngine(const QString& participantId);
    void closeIdleEngines();
    QList<QPair<QString, UciEngine*>> m_idleEngines; // (participant, engine), from the oldest to the most recent
    int m_enginesReused = 0;
    QString positionKey(const Running& g) const; // "<starting fen> moves …" sent to the engine
    bool adjudicate(Running& g);                 // automatic adjudication after a move (true: game over)
    bool pairSwissRound();       // Swiss system: pairings of the next round (false: no more rounds)
    QString nextOpening();       // next starting position (the same for both colours of a pairing)
    QString goArgs(const Running& g) const; // arguments of "go" depending on the limit (clock, time per move, depth)

    Settings m_settings;
    QList<GameRecord> m_games;
    bool m_running = false;
    int m_gamePauseMs = 800;
    std::function<bool(const QString&, const chess::Game&, QString*)> m_gameSaver;
    std::vector<std::unique_ptr<Running>> m_slots; // simultaneous games (kept from one tournament to the next)
    Running* m_shown = nullptr;                    // displayed game
    bool m_finishedShown = false;
    chess::Game m_idle;        // displayed when no game has been played yet
    ChessClock* m_idleClock;
public:
    int ponderHits() const { return m_ponderHits; }
private:
    int m_ponderHits = 0;
    int m_swissRound = 0;   // rounds paired
    int m_pairing = 0;      // starting positions already handed out
    QStringList m_openings; // starting positions (reloaded if needed)
public:
    int swissRound() const { return m_swissRound; }
private:
    std::shared_ptr<chess::treebook::Book> m_bookTree;
    std::shared_ptr<chess::polyglot::Book> m_bookPolyglot;
    chess::Move pickBookMove(const chess::Position& pos) const;
public:
    int bookMoves() const { return m_bookMovesPlayed; }
private:
    int m_bookMovesPlayed = 0;
};
