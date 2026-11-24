#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QStringList>

#include <atomic>

// Score an engine announces for a win (or a loss) read from the endgame tablebases:
// Stockfish writes "cp 20000" minus the number of half-moves; no ordinary evaluation comes near that value.
constexpr int TablebaseScore = 19000;

struct EngineLine {
    int multipv = 1;
    int depth = 0;
    int seldepth = 0;
    bool mate = false;
    int score = 0; // from the point of view of the side to move
    qint64 nodes = 0;
    qint64 nps = 0;
    qint64 tbHits = 0; // tablebase probes since the start of the search
    qint64 timeMs = 0;
    QStringList pv; // UCI moves
};

// Process of an engine, in the engines' input/output thread (shared by all): what it
// writes is read and timestamped as soon as it arrives, even when the interface is busy — the
// measured thinking time does not depend on it.
class EngineIo : public QObject {
    Q_OBJECT
public:
    static qint64 nowNs();                    // monotonic clock
    std::atomic<qint64> bestMoveNs{0};        // time of the last "bestmove" read

    bool start(const QString& path);          // blocking (≤ 3 s)
    void write(const QByteArray& bytes);
    void shutdown();                          // "quit", then forced stop; the object then destroys itself

signals:
    // Lines read; `stampNs`: time the "bestmove" they contain was read (0 otherwise).
    void linesRead(const QList<QByteArray>& lines, qint64 stampNs);
    void failedToStart();
    void finished();

private:
    void onReadyRead();
    QProcess* m_proc = nullptr;
};

// Drives a UCI engine (Stockfish, Lc0, …) through QProcess, without blocking the interface.
class UciEngine : public QObject {
    Q_OBJECT
public:
    explicit UciEngine(QObject* parent = nullptr);
    ~UciEngine() override;

    bool start(const QString& path);
    void quit();
    bool isRunning() const { return m_state != State::Off; }
    QString name() const { return m_name; }
    QString path() const { return m_path; }

    void setOption(const QString& name, const QString& value);
    // Syzygy endgame tablebases: the folder (or folders, separated by ":") is given to every
    // engine that declares the SyzygyPath option, according to its role and the user's settings.
    // A SyzygyPath set by hand in an engine's configuration takes precedence.
    enum class TablebaseRole { Analysis, Play, Tournament };
    struct Tablebases {
        QString path;
        bool analysis = true, play = true, tournament = true;
    };
    static void setTablebases(const Tablebases& t); // applies to the next search of each engine
    static Tablebases tablebases();
    // To be called before start(). `allowed` false: never any tables for this engine (limited level).
    void setTablebaseRole(TablebaseRole role, bool allowed = true) { m_tbRole = role, m_tbAllowed = allowed; }
    QString tablebasePathSent() const { return m_tbSent; } // what was passed to the engine (empty: nothing)
    void newGame(); // "ucinewgame": clears the transposition table
    void analyse(const QString& fen) { search(fen, QStringLiteral("infinite")); }
    // Any search; `goArgs` follows "go" (e.g. "movetime 2000").
    // A finished search ends with bestMoveReceived(uci, fen).
    void search(const QString& fen, const QString& goArgs);
    void stop();
    // Pondering: "go ponder" search on the expected position; ponderHit() turns it
    // into a normal search when the opponent plays the expected move.
    void ponder(const QString& fen, const QString& goArgs) { search(fen, QStringLiteral("ponder ") + goArgs); }
    void ponderHit();
    QString ponderMove() const { return m_ponderMove; } // expected move, given with the last bestmove
    // Asks the engine to play its best move right away (bestMoveReceived is emitted).
    void moveNow();
    bool isSearching() const { return m_state == State::Searching; }
    // Duration of the last finished search, from "go" (or "ponderhit") to the reading of
    // "bestmove" by the input/output thread: independent of the interface's load.
    qint64 lastThinkMs() const { return m_lastThinkMs; }
    // The engine has already answered the current search, without the answer having been processed yet.
    bool hasPendingAnswer() const;

signals:
    void started(const QString& name);
    void infoReceived(const EngineLine& line);
    void bestMoveReceived(const QString& uci, const QString& fen);
    void errorOccurred(const QString& message);
    void terminated();

private:
    enum class State { Off, Initializing, Idle, Searching, Stopping };

    void send(const QString& cmd);
    void onLines(const QList<QByteArray>& lines, qint64 stampNs);
    void parseLine(const QString& line);
    void flushQueue();
    void beginSearch();
    void syncTablebases();

    EngineIo* m_io = nullptr;
    qint64 m_goNs = 0, m_lastThinkMs = 0, m_stampNs = 0;
    State m_state = State::Off;
    QString m_name, m_path;
    QString m_pendingFen, m_pendingGo, m_searchFen;
    bool m_hasPending = false;
    QStringList m_queued; // commands to send when the engine is idle
    QString m_ponderMove;
    bool m_chess960 = false; // last value sent for the UCI_Chess960 option
    QString tablebasePathWanted() const;
    TablebaseRole m_tbRole = TablebaseRole::Analysis;
    bool m_tbAllowed = true, m_hasSyzygyOption = false, m_tbManual = false;
    QString m_tbSent;
};
