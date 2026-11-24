#pragma once

#include "chess/position.h"

#include <QObject>
#include <QString>

#include <functional>

class UciEngine;
struct EngineLine;

// Coach of the Play page (in the manner of the "Coach is watching" of SCID vs PC): a separate
// Stockfish evaluates the position before and after the player's move; a move losing winning chances
// is reported before the opponent's reply. Also gives a hint (best move).
class Coach : public QObject {
    Q_OBJECT
public:
    struct Verdict {
        bool weak = false;
        QString before, after; // readable evaluations (White's point of view)
        chess::Move best;      // best move of the starting position
        double drop = 0;       // winning chances lost (0 to 2)
    };
    explicit Coach(QObject* parent = nullptr);
    ~Coach() override;

    // `threshold`: winning chances lost from which the move is reported (0.1 / 0.2 / 0.3).
    void check(const chess::Position& before, chess::Move move, double threshold, std::function<void(const Verdict&)> done);
    void hint(const chess::Position& pos, std::function<void(chess::Move)> done);
    void setTimeMs(int ms) { m_timeMs = ms; }
    bool isBusy() const { return bool(m_step); }

private:
    bool ensureEngine();
    void search(const chess::Position& pos, std::function<void(const EngineLine&, const QString& best)> done);

    UciEngine* m_engine = nullptr;
    bool m_ready = false;
    int m_timeMs = 300;
    QString m_fen;
    std::function<void()> m_pendingStart;
    std::function<void(const EngineLine&, const QString&)> m_step;
    EngineLine* m_line = nullptr;
};
