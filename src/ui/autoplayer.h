#pragma once

#include "engine/engineregistry.h"

#include <QObject>
#include <QString>

class GameSession;
class UciEngine;

// Game → Finish game (like "Finish game" of SCID vs PC): an engine plays both sides
// from the displayed position, move after move in the notation, until the end of the game
// (mate, stalemate, draw by rule), a move limit or the stop.
class AutoPlayer : public QObject {
    Q_OBJECT
public:
    AutoPlayer(GameSession* session, QObject* parent = nullptr);
    ~AutoPlayer() override;

    bool start(const EngineConfig& engine, int moveTimeMs, int maxPlies, QString* error);
    void stop();
    bool isRunning() const { return m_engine != nullptr; }
    int played() const { return m_played; }

signals:
    void finished(const QString& reason);

private:
    void next();
    void shutdown(const QString& reason);

    GameSession* m_session;
    UciEngine* m_engine = nullptr;
    int m_moveTime = 1000;
    int m_maxPlies = 200;
    int m_played = 0;
    QString m_fen;
public:
    QString lastReason() const { return m_reason; }
private:
    QString m_reason;
};
