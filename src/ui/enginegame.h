#pragma once

#include "chess/types.h"
#include "engine/engineregistry.h"
#include "engine/uciengine.h"
#include "chess/position.h"

#include "chessclock.h"

#include <QJsonObject>

#include <functional>
#include <QMap>
#include <QObject>
#include <QString>

class GameSession;
namespace chess { struct Node; }
class UciEngine;

struct PlaySettings {
    enum Side { PlayWhite, PlayBlack, PlayRandom };
    Side side = PlayWhite;
    bool fullStrength = false;
    bool tablebases = true;  // at full strength: with the endgame tablebases (if the Preferences give them to games)
    int elo = 1800;          // UCI_Elo (Stockfish: 1320 to 3190)
    int moveTimeMs = 1000;   // thinking time per move
    bool fromCurrentPosition = false;
    QString openingMoves; // imposed opening: SAN moves played automatically at the start (empty: none)
    // Beginner level (like the tactical game of SCID vs PC with Phalanx): 1 to 3, the engine
    // (at its lowest Elo) sometimes chooses a weaker move among its lines; 0: no.
    int beginnerLevel = 0;
    QString engineId; // engine of the registry (empty: that of the analysis panel)
    QString playerName; // name written in the PGN (empty: "Vous")
    // Clock (Fischer time control). baseMs == 0: no clock, the engine plays in moveTimeMs.
    qint64 baseMs = 0;
    qint64 incMs = 0;
    bool engineSeparate = false; // different time control for the engine (handicap)
    qint64 engineBaseMs = 0;
    qint64 engineIncMs = 0;
    int depth = 0;           // without clock: fixed depth (0: time per move)
    bool coach = false;      // report the player's weak moves and offer to take them back
    double coachThreshold = 0.2; // winning chances lost: 0.1 inaccuracy, 0.2 mistake, 0.3 blunder
    bool ponder = false;     // pondering: the engine thinks during the player's time
    bool isTimed() const { return baseMs > 0; }
};

// Game against a UCI engine: the engine replies as soon as it is its turn.
// Uses its own engine instance, independent of the analysis panel.
class EngineGame : public QObject {
    Q_OBJECT
public:
    EngineGame(GameSession* session, QObject* parent = nullptr);

    bool start(const EngineConfig& engine, const PlaySettings& settings, QString* error);
    // Resumes a game whose moves are already in the session (Play page), paused.
    bool resume(const EngineConfig& engine, const PlaySettings& settings, chess::Color human, qint64 whiteMs, qint64 blackMs,
                QString* error);
    // State to save in order to resume the game at the next launch (empty if no game).
    QJsonObject saveState() const;
    bool restoreState(const QJsonObject& state, QString* error);
    void stop();      // stops the game without a result
    // Pause while the Play page is not displayed: engine and clock stopped. On return the game
    // stays paused (isPaused()): nothing restarts by itself.
    void setSuspended(bool suspended);
    bool isSuspended() const { return m_suspended; }
    void resign();   // the player resigns
    void takeBack(); // undoes the player's last move (and the engine's reply)
    // Player's move during the game. Played from a past position, it is a take-back:
    // a different move leaves the old continuation as a variation; the same move drops it
    // (otherwise we would fall back into the old continuation, with no reply from the engine).
    void playHumanMove(chess::Move m);

    // Engine tab of the Play page:
    void moveNow();               // the engine plays right away (or takes the side to move)
    // Changes opponent during the game: moves, clocks and sides are kept.
    bool changeEngine(const EngineConfig& engine, QString* error = nullptr);
    // Changes the opponent's strength during the game (limited Elo, maximum strength or beginner): applies from
    // its next search, endgame tablebases included; nothing is written
    // in the notation. False if nothing changes (no game, engine without UCI_Elo, same setting).
    bool setStrength(bool fullStrength, int elo, int beginnerLevel = 0, bool tablebases = true);
    // UCI options the game in progress sets itself (name → value sent to the engine; null
    // value: option with no effect during the game). Empty without a game in progress.
    QMap<QString, QString> managedOptions() const;
    bool hasStrengthControl() const; // the game's engine has the UCI_Elo option
    static QString strengthText(const PlaySettings& settings); // "Elo 1500", "force maximale", "force maximale avec tables de finales", "débutant 2"
    // Game lost (or drawn) on time, still on screen: it can continue. Same game, notation
    // and clocks kept; the side whose flag fell stays at 0:00 and no longer falls.
    bool canContinue() const { return !m_active && m_flagEnded; }
    bool continueGame(QString* error = nullptr);
    void halt();                  // the engine stops and no longer replies until moveNow()
    bool isHalted() const { return m_halted; }
    // Game resumed at launch: clocks and engine wait for the player's first move (or Play).
    bool isPaused() const { return m_active && m_paused; }
    // A move has just been deleted and it is the engine's turn: it waits. Play restarts it; playing
    // a move of its side swaps the sides (the engine then replies with the other one).
    bool isAwaiting() const { return m_awaiting; }
    // Opening book: on its turn, if the book knows the position, the engine takes a
    // move from it instead of thinking. The provider returns a null move outside the book.
    using BookProvider = std::function<chess::Move(const chess::Position&)>;
    // Coach: called after each move of the player, before the engine's reply; `done(true)` keeps the
    // move (the engine replies), `done(false)` takes it back.
    using CoachHook = std::function<void(const chess::Position& before, chess::Move move, std::function<void(bool)> done)>;
    void setCoachHook(CoachHook hook) { m_coachHook = std::move(hook); }
    bool isCoachChecking() const { return m_coachPending; }
    void setBookProvider(BookProvider provider) { m_bookProvider = std::move(provider); }
    void setUseBook(bool on); // remembered in the settings
    bool useBook() const { return m_useBook; }
    bool isPlayingBookMove() const { return m_bookPending; }
    static constexpr int MaxMultiPv = 10;
    static constexpr int DefaultMultiPv = 4;
    void setMultiPv(int n);       // number of lines displayed by the engine (1 to 10)
    int multiPv() const { return m_multiPv; }
    const EngineConfig& engineConfig() const { return m_config; }
    const chess::Position& searchPosition() const { return m_searchPos; }

    bool isActive() const { return m_active; }
    bool isThinking() const { return m_thinking; }
    bool isAtGameEnd() const; // displayed position = last move of the game
    chess::Color humanColor() const { return m_human; }
    QString engineLabel() const;
    bool isTimed() const { return m_settings.isTimed(); }
    const PlaySettings& settings() const { return m_settings; } // settings of the last game
    const ChessClock* clock() const { return m_clock; }

signals:
    void stateChanged();
    void finished(const QString& result, const QString& reason);
    void clockTick();
    void engineInfo(const EngineLine& line); // engine's thinking (position searchPosition())
    void searchStarted();                    // new search: previous lines are stale
    void bookMovePlayed(chess::Move m);      // the engine played a book move

private:
    void onPositionChanged();
    void onBestMove(const QString& uci, const QString& fen);
    void finish(const std::string& result, const QString& reason);
    void setThinking(bool on);
    void updateHeaders();
    bool launchEngine(const EngineConfig& engine, const PlaySettings& settings, QString* error);
    void setupClock(qint64 whiteMs, qint64 blackMs);
    void onFlag(chess::Color side);
    void applyRegistryChanges();
    void sendSkillLevel(); // "Skill Level" and "UCI_Elo" at the maximum when the game is at full strength
    chess::Node* lastNode() const; // last move of the main line
    QString searchArgs() const;    // arguments of "go" (clock, depth or time per move)
    void stopPondering();

    GameSession* m_session;
    UciEngine* m_engine;
    PlaySettings m_settings;
    EngineConfig m_config;
    ChessClock* m_clock;
    chess::Color m_human = chess::White;
    bool m_active = false;
    bool m_thinking = false;
    bool m_starting = false;
    bool m_newMove = false;
    bool m_deleted = false;  // moves have been deleted since the last change of position
    bool m_awaiting = false; // see isAwaiting()
    size_t m_treeSize = 0;   // number of moves in the tree, to tell an added move from a deleted one
    bool m_suspended = false;
    bool m_flagEnded = false;   // the game ended on time (see canContinue())
    chess::Color m_flagSide = chess::White;
    bool m_engineReady = false; // the clock only starts once the engine is ready
    bool m_halted = false;      // stopped by the Stop button: the engine no longer plays
    bool m_paused = false;      // see isPaused()
    int m_multiPv = DefaultMultiPv;
    QMap<int, EngineLine> m_lines; // last lines of the search (beginner level)
public:
    // Move chosen among the lines (beginner level): tolerated loss and weighted draw. `roll`
    // in [0, 1). Returns the index of the line.
    static int pickBeginnerLine(const QList<EngineLine>& lines, int level, double roll);
private:
    BookProvider m_bookProvider;
    bool m_useBook = true;
    bool m_bookPending = false; // book move about to be played
    chess::Position m_searchPos;
    CoachHook m_coachHook;
    bool m_coachPending = false;
    std::string m_coachChecked; // position already checked by the coach (the engine may reply)
    bool m_pondering = false;   // pondering on the expected position
    std::string m_ponderFen;
public:
    bool isPondering() const { return m_pondering; }
    const std::string& ponderFen() const { return m_ponderFen; } // expected position
    int ponderHits() const { return m_ponderHits; }
private:
    int m_ponderHits = 0;
};
