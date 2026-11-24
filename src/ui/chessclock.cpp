#include "chessclock.h"

#include <QTimer>

using namespace chess;

ChessClock::ChessClock(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(100);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!m_running) return;
        if (remaining(m_side) <= 0 && !m_flagless[m_side]) {
            if (m_flagGuard && m_flagGuard()) return;
            commit();
            m_time[m_side] = 0;
            stop();
            emit tick();
            emit flagFell(m_side);
            return;
        }
        emit tick();
    });
}

void ChessClock::setup(qint64 whiteMs, qint64 whiteIncMs, qint64 blackMs, qint64 blackIncMs)
{
    stop();
    m_time[White] = whiteMs;
    m_time[Black] = blackMs;
    m_inc[White] = whiteIncMs;
    m_inc[Black] = blackIncMs;
    m_flagless[White] = m_flagless[Black] = false;
    emit tick();
}

void ChessClock::commit()
{
    if (!m_running) return;
    m_time[m_side] -= m_since.restart();
}

void ChessClock::start(Color running)
{
    commit();
    m_side = running;
    m_running = true;
    m_since.restart();
    m_timer->start();
    emit tick();
}

void ChessClock::stop()
{
    commit();
    m_running = false;
    m_timer->stop();
}

void ChessClock::moveMade(Color mover)
{
    if (!m_running) return;
    commit();
    if (m_flagless[mover]) m_time[mover] = 0;
    else m_time[mover] += m_inc[mover];
    m_side = ~mover;
    m_since.restart();
    emit tick();
}

bool ChessClock::moveMade(Color mover, qint64 usedMs)
{
    if (!m_running) return true;
    m_since.restart();
    m_time[mover] -= usedMs;
    if (m_flagless[mover]) {
        m_time[mover] = 0;
        m_side = ~mover;
        emit tick();
        return true;
    }
    if (m_time[mover] < 0) {
        m_time[mover] = 0;
        m_running = false;
        m_timer->stop();
        emit tick();
        return false;
    }
    m_time[mover] += m_inc[mover];
    m_side = ~mover;
    emit tick();
    return true;
}

void ChessClock::setRunning(Color c)
{
    if (!m_running) return;
    commit();
    m_side = c;
    m_since.restart();
    emit tick();
}

qint64 ChessClock::remaining(Color c) const
{
    qint64 t = m_time[c];
    if (m_running && c == m_side) t -= m_since.elapsed();
    return std::max<qint64>(0, t);
}

QString ChessClock::format(qint64 ms)
{
    ms = std::max<qint64>(0, ms);
    if (ms < 10000) // less than 10 s: tenths (rounded down, like a real clock)
        return QStringLiteral("%1.%2").arg(ms / 1000).arg(ms / 100 % 10);
    const qint64 s = (ms + 999) / 1000; // seconds started
    if (s >= 3600) return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
