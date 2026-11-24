#include "autoplayer.h"

#include "display.h"
#include "engine/uciengine.h"
#include "gamesession.h"

#include <QThread>
#include <QTimer>

using namespace chess;

AutoPlayer::AutoPlayer(GameSession* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
}

AutoPlayer::~AutoPlayer()
{
    if (m_engine) {
        m_engine->disconnect(this);
        m_engine->quit();
    }
}

bool AutoPlayer::start(const EngineConfig& cfg, int moveTimeMs, int maxPlies, QString* error)
{
    if (m_engine) return true;
    if (!cfg.isValid()) {
        if (error) *error = tr("aucun moteur");
        return false;
    }
    m_moveTime = std::max(50, moveTimeMs);
    m_maxPlies = std::max(1, maxPlies);
    m_played = 0;
    m_engine = new UciEngine(this);
    connect(m_engine, &UciEngine::started, this, [this] { next(); });
    connect(m_engine, &UciEngine::bestMoveReceived, this, [this](const QString& uci, const QString& fen) {
        if (fen != m_fen) return;
        Node* cur = m_session->current();
        if (cur->pos.fen() != m_fen.toStdString()) { // the user has changed position: stop
            shutdown(tr("position changée"));
            return;
        }
        const Move m = cur->pos.parseUci(uci.toStdString());
        if (m.isNull() || !cur->pos.isLegal(m)) {
            shutdown(tr("coup illégal du moteur"));
            return;
        }
        m_session->playMove(m);
        ++m_played;
        QTimer::singleShot(0, this, &AutoPlayer::next);
    });
    connect(m_engine, &UciEngine::terminated, this, [this] {
        if (m_engine) shutdown(tr("le moteur s'est arrêté"));
    });
    if (!m_engine->start(cfg.path)) {
        delete m_engine;
        m_engine = nullptr;
        if (error) *error = tr("Impossible de lancer le moteur");
        return false;
    }
    if (cfg.id.isEmpty()) m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
    for (auto it = cfg.values.cbegin(); it != cfg.values.cend(); ++it) m_engine->setOption(it.key(), it.value());
    if (cfg.option(QStringLiteral("UCI_LimitStrength"))) m_engine->setOption(QStringLiteral("UCI_LimitStrength"), QStringLiteral("false"));
    if (cfg.option(QStringLiteral("MultiPV"))) m_engine->setOption(QStringLiteral("MultiPV"), QStringLiteral("1"));
    return true;
}

void AutoPlayer::next()
{
    if (!m_engine) return;
    Node* cur = m_session->current();
    GameEnd end;
    if (detectGameEnd(cur, end)) {
        shutdown(display::endReason(end.kind));
        return;
    }
    if (m_played >= m_maxPlies) {
        shutdown(tr("limite de %n demi-coup(s)", nullptr, m_maxPlies));
        return;
    }
    m_fen = QString::fromStdString(cur->pos.fen());
    m_engine->search(m_fen, QStringLiteral("movetime %1").arg(m_moveTime));
}

void AutoPlayer::stop() { shutdown(tr("arrêtée")); }

void AutoPlayer::shutdown(const QString& reason)
{
    if (!m_engine) return;
    UciEngine* e = m_engine;
    m_engine = nullptr;
    e->disconnect(this);
    e->quit();
    e->deleteLater();
    m_reason = reason;
    emit finished(reason);
}
