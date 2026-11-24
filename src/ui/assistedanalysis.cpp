#include "assistedanalysis.h"

#include "autoevaluator.h"
#include "boardwidget.h"
#include "engine/uciengine.h"
#include "gamesession.h"

#include <QTimer>

using namespace chess;

namespace assisted {

QHash<int, Quality> classify(const QHash<int, annotation::PositionEval>& evals)
{
    QHash<int, Quality> out;
    double best = -2.0;
    for (const auto& e : evals)
        if (e.valid) best = std::max(best, annotation::winningChances(e));
    for (auto it = evals.begin(); it != evals.end(); ++it) {
        if (!it->valid) continue;
        const double drop = best - annotation::winningChances(*it);
        out.insert(it.key(), drop < 0.1 ? Quality::Good : drop < 0.2 ? Quality::Playable : Quality::Bad);
    }
    return out;
}

bool isThreat(const annotation::PositionEval& best, const annotation::PositionEval& afterPass)
{
    if (!best.valid || !afterPass.valid) return false;
    // The opponent's chances today are the opposite of the best move's; after a pass, those of his own move.
    return annotation::winningChances(afterPass) + annotation::winningChances(best) >= 0.2;
}

} // namespace assisted

AssistedAnalysis::AssistedAnalysis(GameSession* session, BoardWidget* board, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_board(board)
    , m_engine(new UciEngine(this))
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150); // moves replayed quickly: only the position one stops on is gone through
    connect(m_debounce, &QTimer::timeout, this, &AssistedAnalysis::begin);
    connect(m_engine, &UciEngine::infoReceived, this, &AssistedAnalysis::onInfo);
    connect(m_engine, &UciEngine::bestMoveReceived, this, [this](const QString&, const QString& fen) { onBestMove(fen); });
    connect(session, &GameSession::currentChanged, this, &AssistedAnalysis::schedule);
    connect(session, &GameSession::gameReplaced, this, &AssistedAnalysis::schedule);
    connect(session, &GameSession::slotSwitched, this, &AssistedAnalysis::schedule);
}

AssistedAnalysis::~AssistedAnalysis() = default;

void AssistedAnalysis::setEnabled(bool squares, bool threat)
{
    if (squares == m_squares && threat == m_threat) return;
    m_squares = squares;
    m_threat = threat;
    if (!squares && !threat) {
        m_debounce->stop();
        if (m_engine->isRunning()) m_engine->quit(); // no process left behind while the help is off
        m_phase = Phase::Idle;
        m_skipOld = false;
        clear();
        return;
    }
    if (m_phase == Phase::Idle && m_fen == QString::fromStdString(m_session->current()->pos.fen()) && !m_evals.isEmpty()) show(); // already gone through
    else schedule();
}

void AssistedAnalysis::schedule()
{
    clear(); // what is displayed belongs to another position
    if (m_squares || m_threat) m_debounce->start();
}

void AssistedAnalysis::clear()
{
    const bool had = !m_qualities.isEmpty() || !m_threatMove.isNull();
    m_qualities.clear();
    m_threatMove = {};
    m_board->setMoveQualities({});
    m_board->setEngineArrow(QStringLiteral("threat"), {}, {});
    if (had) emit updated();
}

void AssistedAnalysis::begin()
{
    if (!m_squares && !m_threat) return;
    m_pos = m_session->current()->pos;
    m_fen = QString::fromStdString(m_pos.fen());
    m_evals.clear();
    m_passEval = {};
    m_passMove.clear();
    if (m_pos.legalMoves().empty()) { // mate or stalemate
        if (m_engine->isSearching()) m_engine->stop();
        m_phase = Phase::Idle;
        emit updated();
        return;
    }
    if (!m_engine->isRunning()) {
        const EngineConfig cfg = AutoEvaluator::findStockfish();
        if (!cfg.isValid() || !m_engine->start(cfg.path)) return;
        m_engine->setOption(QStringLiteral("Threads"), QStringLiteral("2"));
        m_engine->setOption(QStringLiteral("Hash"), QStringLiteral("64"));
        m_engine->setOption(QStringLiteral("UCI_LimitStrength"), QStringLiteral("false"));
        m_engine->setOption(QStringLiteral("MultiPV"), QStringLiteral("256")); // every legal move gets its line
    }
    m_skipOld = m_engine->isSearching(); // its last lines belong to the position being left
    m_phase = Phase::Moves;
    m_searchFen = m_fen;
    m_engine->search(m_fen, QStringLiteral("movetime 1200"));
}

void AssistedAnalysis::onInfo(const EngineLine& line)
{
    if (m_skipOld || m_phase == Phase::Idle || line.pv.isEmpty()) return;
    if (m_phase == Phase::Threat) {
        if (line.multipv != 1) return;
        m_passEval = {true, line.mate, line.score, line.depth, line.pv};
        m_passMove = line.pv.first();
        return;
    }
    const Move m = m_pos.parseUci(line.pv.first().toStdString());
    if (m.isNull() || !m_pos.isLegal(m)) return;
    const int key = assisted::moveKey(m);
    // A promotion: the square takes the best of its four moves (the queen's, nearly always).
    const annotation::PositionEval e{true, line.mate, line.score, line.depth, line.pv};
    if (m.promo != NoType && m.promo != Queen && m_evals.contains(key)) return;
    m_evals.insert(key, e);
    // Shown as the search deepens, once every move has a first value (a few milliseconds).
    if (line.multipv == 1 && m_evals.size() >= 1) show();
}

void AssistedAnalysis::onBestMove(const QString& fen)
{
    if (fen != m_searchFen) { // end of a search that was stopped: the current one starts now
        m_skipOld = false;
        return;
    }
    m_skipOld = false;
    if (m_phase == Phase::Moves) {
        show();
        // The threat: the same position, the other side to move (impossible in check).
        if (m_threat && !m_pos.inCheck()) {
            Position passed = m_pos;
            passed.makeMove(Move());
            if (!passed.legalMoves().empty()) {
                m_phase = Phase::Threat;
                m_searchFen = QString::fromStdString(passed.fen());
                m_engine->search(m_searchFen, QStringLiteral("movetime 400"));
                return;
            }
        }
    }
    m_phase = Phase::Idle;
    show();
}

void AssistedAnalysis::show()
{
    if (m_fen != QString::fromStdString(m_session->current()->pos.fen())) return; // the board has moved on
    m_qualities = m_squares ? assisted::classify(m_evals) : QHash<int, assisted::Quality>();
    QHash<int, int> forBoard;
    for (auto it = m_qualities.begin(); it != m_qualities.end(); ++it) forBoard.insert(it.key(), int(it.value()));
    m_board->setMoveQualities(forBoard);

    m_threatMove = {};
    if (m_threat && m_phase == Phase::Idle && !m_passMove.isEmpty()) {
        annotation::PositionEval best;
        for (const auto& e : std::as_const(m_evals))
            if (e.valid && (!best.valid || annotation::winningChances(e) > annotation::winningChances(best))) best = e;
        Position passed = m_pos;
        passed.makeMove(Move());
        const Move t = passed.parseUci(m_passMove.toStdString());
        if (!t.isNull() && passed.isLegal(t) && assisted::isThreat(best, m_passEval)) m_threatMove = t;
    }
    m_board->setEngineArrow(QStringLiteral("threat"), m_threatMove, QColor(0xd9, 0x53, 0x4f, 200));
    emit updated();
}
