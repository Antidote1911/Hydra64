#pragma once

#include "annotator.h"
#include "chess/game.h"

#include <QHash>
#include <QObject>

class BoardWidget;
class GameSession;
class QTimer;
class UciEngine;

// Assisted analysis (ChessBase: "Analyse assistée", "Menace comme flèche"): the embedded
// Stockfish evaluates every legal move of the displayed position; when a piece is picked up,
// its target squares are coloured by the quality of the move. The threat — what the opponent
// would play if the side to move passed — can be shown as an arrow.
namespace assisted {

enum class Quality { Unknown = 0, Good, Playable, Bad };

inline int moveKey(chess::Move m) { return int(m.from) * 64 + int(m.to); }

// Quality of each evaluated move: winning chances lost compared with the best one (the
// annotator's thresholds: under 0.1 good, under 0.2 playable, a mistake beyond).
// `evals`: by move, from the point of view of the side that plays it.
QHash<int, Quality> classify(const QHash<int, annotation::PositionEval>& evals);

// Is what the opponent would play after a pass a real threat? `best`: evaluation of the
// position for the side to move; `afterPass`: for the opponent, were it his turn. True when the
// pass would cost at least 0.2 of winning chances.
bool isThreat(const annotation::PositionEval& best, const annotation::PositionEval& afterPass);

} // namespace assisted

class AssistedAnalysis : public QObject {
    Q_OBJECT
public:
    AssistedAnalysis(GameSession* session, BoardWidget* board, QObject* parent = nullptr);
    ~AssistedAnalysis() override;

    // `squares`: target squares coloured; `threat`: threat as an arrow. Both off: the engine stops.
    void setEnabled(bool squares, bool threat);
    bool squaresEnabled() const { return m_squares; }
    bool threatEnabled() const { return m_threat; }
    const QHash<int, assisted::Quality>& qualities() const { return m_qualities; }
    chess::Move threat() const { return m_threatMove; } // null: none
    bool isIdle() const { return m_phase == Phase::Idle; } // the displayed position has been gone through

signals:
    void updated(); // qualities or threat of the displayed position changed

private:
    enum class Phase { Idle, Moves, Threat };
    void schedule();
    void begin();
    void onInfo(const struct EngineLine& line);
    void onBestMove(const QString& fen);
    void show();
    void clear();

    GameSession* m_session;
    BoardWidget* m_board;
    UciEngine* m_engine;
    QTimer* m_debounce;
    bool m_squares = false, m_threat = false;
    Phase m_phase = Phase::Idle;
    bool m_skipOld = false; // lines of a search being stopped are still coming in
    chess::Position m_pos;  // position gone through
    QString m_fen, m_searchFen;
    QHash<int, annotation::PositionEval> m_evals;
    annotation::PositionEval m_passEval;
    QString m_passMove;
    QHash<int, assisted::Quality> m_qualities;
    chess::Move m_threatMove;
};
