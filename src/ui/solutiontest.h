#pragma once

#include "engine/engineregistry.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;
class UciEngine;
struct EngineLine;

// Position of a solution test (tactical collection: Tactics Marathon, WAC…): the engine must
// find one of the `best` moves (or, without `best`, avoid the `avoid` moves).
struct TestPosition {
    QString id;       // "946 ECM", "WAC.001"… (or the number)
    QString fen;
    QStringList best;  // UCI moves
    QStringList avoid; // UCI moves
    QString solution;  // in notation (for display): "Nd5", "pas Bf3"…
    QString line;      // EPD "pv": full continuation of the solution (display), otherwise empty
    int mateIn = 0;    // EPD "dm": mate in N moves announced by the collection, otherwise 0
    int rating = 0;    // rating of the exercise (WhiteElo of a game), otherwise 0
};

namespace solutiontest {
// EPD (bm / am / id operations), PGN or Hydra64 database (.hy64, .hy64i): for a game, the first move
// of the main line is the solution; a variation on the first move is another solution,
// or a move to avoid if it is marked "?", "??" or "?!". Positions without a solution
// are skipped. Proprietary formats refused (File → Convert…).
QList<TestPosition> load(const QString& path, QString* error);
bool isCorrect(const TestPosition& p, const QString& uci);
}

// Has the engine search each position for `movetime` and records whether it finds the solution, and
// since when it has kept it (the solving time: last switch to a good move).
class SolutionTest : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool done = false;
        bool solved = false;
        QString move;        // UCI move played (or proposed during the search)
        qint64 foundMs = -1; // since when the right move has been on top (-1: not found)
        int depth = 0;
        bool mate = false;
        int score = 0; // from the point of view of the side to move
    };

    explicit SolutionTest(QObject* parent = nullptr);
    ~SolutionTest() override;

    bool start(const EngineConfig& engine, const QList<TestPosition>& positions, int movetimeMs, QString* error);
    void stop();
    bool isRunning() const { return m_engine != nullptr; }
    const QList<TestPosition>& positions() const { return m_positions; }
    const QList<Result>& results() const { return m_results; }
    int current() const { return m_current; }
    int solvedCount() const;
    int movetime() const { return m_movetime; }

signals:
    void positionStarted(int index);
    void positionUpdated(int index); // engine's main line
    void positionFinished(int index);
    void finished(bool completed);
    void errorOccurred(const QString& message);

private:
    void next();
    void onInfo(const EngineLine& line);
    void onBestMove(const QString& uci, const QString& fen);
    void shutdown();

    UciEngine* m_engine = nullptr;
    QList<TestPosition> m_positions;
    QList<Result> m_results;
    int m_current = -1;
    int m_movetime = 1000;
    bool m_stopping = false;
};
