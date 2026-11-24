#include "autoevaluator.h"
#include "engine/uciengine.h"
#include "gamesession.h"
#include "preferences.h"

#include <QCoreApplication>

#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

using namespace chess;

AutoEvaluator::AutoEvaluator(GameSession* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_engine(new UciEngine(this))
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(80);
    connect(m_debounce, &QTimer::timeout, this, &AutoEvaluator::evaluateNext);

    connect(m_engine, &UciEngine::infoReceived, this, [this](const EngineLine& l) { onInfo(l.depth, l.mate, l.score, l.multipv); });
    connect(m_engine, &UciEngine::bestMoveReceived, this, [this](const QString&, const QString& fen) { onBestMove(fen); });

    setEngine(findStockfish());
    // Stockfish added, removed or configured in the engine manager
    connect(&EngineRegistry::instance(), &EngineRegistry::changed, this, [this] { setEngine(findStockfish()); });
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &AutoEvaluator::applyPreferences);

    connect(session, &GameSession::currentChanged, this, &AutoEvaluator::schedule);
    connect(session, &GameSession::treeChanged, this, &AutoEvaluator::schedule);
    connect(session, &GameSession::gameReplaced, this, [this] {
        m_nodeId = -1; // the nodes of the old game no longer exist
        schedule();
    });
}

EngineConfig AutoEvaluator::findStockfish()
{
    auto isStockfish = [](const EngineConfig& e) {
        for (const QString& s : {e.idName, e.name, QFileInfo(e.path).baseName()})
            if (s.contains(QLatin1String("stockfish"), Qt::CaseInsensitive)) return true;
        return false;
    };
    EngineConfig embedded;
    embedded.path = EngineRegistry::builtinStockfishPath();
    embedded.name = QStringLiteral("Stockfish");
    if (QFileInfo(embedded.path).isExecutable()) return embedded;
    const EngineRegistry& reg = EngineRegistry::instance();
    for (const EngineConfig& e : reg.engines())
        if (isStockfish(e)) return e;
    EngineConfig cfg;
    cfg.path = QStandardPaths::findExecutable(QStringLiteral("stockfish"));
    if (!cfg.path.isEmpty()) cfg.name = QStringLiteral("Stockfish");
    return cfg;
}

void AutoEvaluator::setEngine(const EngineConfig& cfg)
{
    if (cfg.path == m_config.path && cfg.values == m_config.values) return;
    m_engine->quit();
    m_config = cfg;
    m_nodeId = -1;
    schedule();
}

void AutoEvaluator::applyPreferences()
{
    // Processors or memory changed: the engine is restarted; depth and time apply
    // from the next position on.
    if (m_engine->isRunning() && (m_threads != prefs::evalThreads() || m_hash != prefs::evalHashMb())) {
        m_engine->quit();
        m_nodeId = -1;
        schedule();
    }
}

void AutoEvaluator::setEnabled(bool on)
{
    if (on == m_enabled) return;
    m_enabled = on;
    if (!on) {
        m_engine->stop();
        m_nodeId = -1;
    } else {
        schedule();
    }
}

void AutoEvaluator::schedule()
{
    if (m_enabled) m_debounce->start();
}

Node* AutoEvaluator::nextNode() const
{
    auto needs = [](const Node* n) {
        GameEnd end;
        return !n->eval.valid && !detectGameEnd(n, end); // final positions: nothing to search
    };
    Node* cur = m_session->current();
    if (needs(cur)) return cur;
    if (needs(m_session->game().root())) return m_session->game().root();
    for (Node* n : m_session->game().mainline())
        if (needs(n)) return n;
    return nullptr;
}

void AutoEvaluator::evaluateNext()
{
    if (!m_enabled || !m_config.isValid()) return;
    Node* n = nextNode();
    if (!n) return;
    if (n->id == m_nodeId && m_engine->isSearching()) return; // already in progress

    if (!m_engine->isRunning()) {
        if (!m_engine->start(m_config.path)) return;
        // Preferences settings (Evaluation section), at full strength, a single line.
        m_threads = prefs::evalThreads();
        m_hash = prefs::evalHashMb();
        m_engine->setOption(QStringLiteral("Threads"), QString::number(m_threads));
        m_engine->setOption(QStringLiteral("Hash"), QString::number(m_hash));
        m_engine->setOption(QStringLiteral("MultiPV"), QStringLiteral("1"));
        m_engine->setOption(QStringLiteral("UCI_LimitStrength"), QStringLiteral("false"));
    }
    m_nodeId = n->id;
    m_fen = QString::fromStdString(n->pos.fen());
    m_whiteToMove = n->pos.sideToMove() == White;
    m_last = {};
    m_engine->search(m_fen, QStringLiteral("depth %1 movetime %2").arg(prefs::evalDepth()).arg(prefs::evalTimeMs()));
}

void AutoEvaluator::onInfo(int depth, bool mate, int score, int multipv)
{
    if (multipv != 1 || m_nodeId < 0) return;
    const int sign = m_whiteToMove ? 1 : -1;
    m_last = {true, mate, sign * score, depth};
}

void AutoEvaluator::onBestMove(const QString& fen)
{
    if (fen != m_fen || m_nodeId < 0) return;
    if (Node* n = m_session->game().findById(m_nodeId); n && QString::fromStdString(n->pos.fen()) == fen && m_last.valid)
        m_session->setEvaluation(n, m_last);
    m_nodeId = -1;
    schedule(); // next position
}
