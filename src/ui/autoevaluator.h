#pragma once

#include "chess/game.h"
#include "engine/engineregistry.h"

#include <QObject>

class GameSession;
class QTimer;
class UciEngine;

// Evaluates in the background, with a light engine, the positions of the game that have
// no evaluation yet: first the current position, then the main line
// from the start. Feeds the evaluation graph and bar.
// Always with the embedded Stockfish (built next to the executable), whatever the
// spectator engines or the opponent of the Play page; settings in the Preferences.
class AutoEvaluator : public QObject {
    Q_OBJECT
public:
    AutoEvaluator(GameSession* session, QObject* parent = nullptr);

    void setEngine(const EngineConfig& cfg);
    // Embedded Stockfish; failing that the one from the engine manager, otherwise the one from the PATH.
    static EngineConfig findStockfish();
    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }


private:
    void schedule();
    void applyPreferences();
    void evaluateNext();
    chess::Node* nextNode() const;
    void onInfo(int depth, bool mate, int score, int multipv);
    void onBestMove(const QString& fen);

    GameSession* m_session;
    UciEngine* m_engine;
    QTimer* m_debounce;
    EngineConfig m_config;
    bool m_enabled = true;
    int m_threads = 0, m_hash = 0; // settings of the launched process

    int m_nodeId = -1;          // node being evaluated
    QString m_fen;
    bool m_whiteToMove = true; // side to move of the searched position
    chess::Evaluation m_last;   // best info received for this node
};
