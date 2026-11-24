#include "coach.h"

#include "annotator.h"
#include "autoevaluator.h"
#include "engine/uciengine.h"

#include <QThread>

using namespace chess;

Coach::Coach(QObject* parent)
    : QObject(parent)
    , m_line(new EngineLine)
{
}

Coach::~Coach()
{
    if (m_engine) m_engine->quit();
    delete m_line;
}

bool Coach::ensureEngine()
{
    if (m_engine) return true;
    const EngineConfig sf = AutoEvaluator::findStockfish();
    if (!sf.isValid()) return false;
    m_engine = new UciEngine(this);
    connect(m_engine, &UciEngine::started, this, [this] {
        m_ready = true;
        if (m_pendingStart) {
            auto f = std::move(m_pendingStart);
            m_pendingStart = nullptr;
            f();
        }
    });
    connect(m_engine, &UciEngine::infoReceived, this, [this](const EngineLine& l) {
        if (l.multipv == 1 && !l.pv.isEmpty()) *m_line = l;
    });
    connect(m_engine, &UciEngine::bestMoveReceived, this, [this](const QString& uci, const QString& fen) {
        if (fen != m_fen || !m_step) return;
        auto step = std::move(m_step);
        m_step = nullptr;
        step(*m_line, uci);
    });
    connect(m_engine, &UciEngine::terminated, this, [this] {
        m_engine->deleteLater();
        m_engine = nullptr;
        m_ready = false;
        if (m_step) {
            auto step = std::move(m_step);
            m_step = nullptr;
            step(EngineLine{}, QString());
        }
    });
    if (!m_engine->start(sf.path)) {
        delete m_engine;
        m_engine = nullptr;
        return false;
    }
    m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 4)));
    m_engine->setOption(QStringLiteral("UCI_LimitStrength"), QStringLiteral("false"));
    m_engine->setOption(QStringLiteral("MultiPV"), QStringLiteral("1"));
    return true;
}

void Coach::search(const Position& pos, std::function<void(const EngineLine&, const QString&)> done)
{
    if (!ensureEngine()) {
        done(EngineLine{}, QString());
        return;
    }
    auto go = [this, pos, done = std::move(done)]() mutable {
        *m_line = {};
        m_fen = QString::fromStdString(pos.fen());
        m_step = std::move(done);
        m_engine->search(m_fen, QStringLiteral("movetime %1").arg(m_timeMs));
    };
    if (m_ready) go();
    else m_pendingStart = std::move(go);
}

namespace {
annotation::PositionEval toEval(const EngineLine& l)
{
    annotation::PositionEval e;
    e.valid = l.depth > 0;
    e.mate = l.mate;
    e.score = l.score;
    e.depth = l.depth;
    return e;
}
} // namespace

void Coach::check(const Position& before, Move move, double threshold, std::function<void(const Verdict&)> done)
{
    if (!before.isLegal(move)) {
        done(Verdict{});
        return;
    }
    const Position after = before.after(move);
    if (after.legalMoves().empty()) { // mate or stalemate: nothing to report
        done(Verdict{});
        return;
    }
    search(before, [this, before, after, move, threshold, done](const EngineLine& b, const QString& bestUci) {
        if (b.depth <= 0) {
            done(Verdict{});
            return;
        }
        const Move best = before.parseUci(bestUci.toStdString());
        if (best == move) { // the engine's move
            done(Verdict{});
            return;
        }
        search(after, [before, after, b, best, threshold, done](const EngineLine& a, const QString&) {
            Verdict v;
            if (a.depth <= 0) {
                done(v);
                return;
            }
            annotation::PositionEval eb = toEval(b), ea = toEval(a);
            annotation::PositionEval mover = ea; // from the player's point of view: sign reversed
            mover.score = -mover.score;
            if (mover.mate && mover.score == 0) mover.score = 1;
            v.drop = annotation::winningChances(eb) - annotation::winningChances(mover);
            v.weak = v.drop >= threshold;
            v.before = annotation::formatWhite(eb, before.sideToMove());
            v.after = annotation::formatWhite(ea, after.sideToMove());
            v.best = best;
            done(v);
        });
    });
}

void Coach::hint(const Position& pos, std::function<void(Move)> done)
{
    const int keep = m_timeMs;
    m_timeMs = std::max(m_timeMs, 800);
    search(pos, [this, pos, keep, done](const EngineLine&, const QString& uci) {
        m_timeMs = keep;
        done(pos.parseUci(uci.toStdString()));
    });
}
