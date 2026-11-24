#pragma once

#include "chess/types.h"

#include <QElapsedTimer>

#include <functional>
#include <QObject>

class QTimer;

// Chess clock (Fischer time control: base time + increment after each move).
class ChessClock : public QObject {
    Q_OBJECT
public:
    explicit ChessClock(QObject* parent = nullptr);

    void setup(qint64 whiteMs, qint64 whiteIncMs, qint64 blackMs, qint64 blackIncMs);
    void start(chess::Color running); // starts (or restarts) the time of the given side
    void stop();
    // Side `mover` has just moved: it gets its increment, the opponent starts.
    void moveMade(chess::Color mover);
    // Move whose duration was measured elsewhere (engine: from "go" to the reading of "bestmove"):
    // that is what is deducted, not the time seen by the interface. False if time has run out
    // (the clock is then stopped, without emitting flagFell).
    bool moveMade(chess::Color mover, qint64 usedMs);
    // Flag guard: as long as it answers true (the move has already arrived, not processed yet),
    // the flag does not fall; moveMade(mover, usedMs) will decide.
    void setFlagGuard(std::function<bool()> guard) { m_flagGuard = std::move(guard); }
    void setRunning(chess::Color c); // after a move is taken back
    // Side without a flag (game continued after it fell): its clock stays at 0:00, with no increment,
    // and no longer falls. setup() removes it.
    void setFlagless(chess::Color c, bool on) { m_flagless[c] = on; }
    bool isFlagless(chess::Color c) const { return m_flagless[c]; }

    qint64 remaining(chess::Color c) const;
    qint64 increment(chess::Color c) const { return m_inc[c]; }
    bool isRunning() const { return m_running; }
    chess::Color runningSide() const { return m_side; }

    static QString format(qint64 ms); // « 4:58 », « 1:02:03 », « 9.4 »

signals:
    void tick();                     // ~10 times per second while the clock is running
    void flagFell(chess::Color side); // elapsed time

private:
    void commit(); // carries the elapsed time over to the side to move

    qint64 m_time[2] = {0, 0};
    qint64 m_inc[2] = {0, 0};
    bool m_flagless[2] = {false, false};
    chess::Color m_side = chess::White;
    bool m_running = false;
    QElapsedTimer m_since;
    QTimer* m_timer;
    std::function<bool()> m_flagGuard;
};
