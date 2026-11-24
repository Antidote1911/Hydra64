#include "uciengine.h"

#include "chess/position.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QThread>

#include <chrono>
#include <QTimer>

UciEngine::UciEngine(QObject* parent)
    : QObject(parent)
{
}

UciEngine::~UciEngine() { quit(); }

namespace {
// Shared thread for the engines' input/output, stopped when the application ends.
QThread* ioThread()
{
    static QThread* thread = [] {
        auto* t = new QThread;
        t->setObjectName(QStringLiteral("engine-io"));
        t->start();
        qAddPostRoutine([] {
            if (QThread* th = ioThread()) {
                th->quit();
                th->wait(2000);
            }
        });
        return t;
    }();
    return thread;
}
} // namespace

// ---------------------------------------------------------------- EngineIo (input/output thread)

qint64 EngineIo::nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool EngineIo::start(const QString& path)
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    m_proc->setStandardErrorFile(QProcess::nullDevice()); // never read: do not let it pile up in memory
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &EngineIo::onReadyRead);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) emit failedToStart();
    });
    connect(m_proc, &QProcess::finished, this, &EngineIo::finished);
    m_proc->start(path, {});
    if (m_proc->waitForStarted(3000)) return true;
    delete m_proc;
    m_proc = nullptr;
    return false;
}

void EngineIo::write(const QByteArray& bytes)
{
    if (m_proc && m_proc->state() == QProcess::Running) m_proc->write(bytes);
}

void EngineIo::onReadyRead()
{
    QList<QByteArray> lines;
    qint64 stamp = 0;
    while (m_proc && m_proc->canReadLine()) {
        const QByteArray line = m_proc->readLine().trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("bestmove")) {
            stamp = nowNs();
            bestMoveNs.store(stamp);
        }
        lines << line;
    }
    if (!lines.isEmpty()) emit linesRead(lines, stamp);
}

void EngineIo::shutdown()
{
    // Without waiting: the engine receives "quit"; it is killed if it has not stopped after one
    // and a half seconds.
    if (!m_proc || m_proc->state() != QProcess::Running) {
        deleteLater();
        return;
    }
    disconnect(m_proc, nullptr, this, nullptr);
    connect(m_proc, &QProcess::finished, this, &QObject::deleteLater);
    m_proc->write("stop\nquit\n");
    QTimer::singleShot(1500, this, [this] {
        if (m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill(); // → finished → deleteLater
    });
}

// ---------------------------------------------------------------- UciEngine (interface thread)

namespace {
UciEngine::Tablebases g_tablebases;
}

void UciEngine::setTablebases(const Tablebases& t) { g_tablebases = t; }
UciEngine::Tablebases UciEngine::tablebases() { return g_tablebases; }

QString UciEngine::tablebasePathWanted() const
{
    if (!m_hasSyzygyOption || !m_tbAllowed || g_tablebases.path.isEmpty()) return {};
    const bool on = m_tbRole == TablebaseRole::Analysis ? g_tablebases.analysis : m_tbRole == TablebaseRole::Play ? g_tablebases.play : g_tablebases.tournament;
    return on ? g_tablebases.path : QString();
}

bool UciEngine::start(const QString& path)
{
    quit();
    m_path = path;
    m_name = QFileInfo(path).baseName();
    m_chess960 = false;
    m_hasSyzygyOption = false;
    m_tbSent.clear();
    m_tbManual = false;
    for (const QString& cmd : std::as_const(m_queued))
        if (cmd.startsWith(QLatin1String("setoption name SyzygyPath "), Qt::CaseInsensitive)) m_tbManual = true;
    auto* io = new EngineIo;
    io->moveToThread(ioThread());
    bool ok = false;
    QMetaObject::invokeMethod(io, [io, path] { return io->start(path); }, Qt::BlockingQueuedConnection, &ok);
    if (!ok) {
        io->deleteLater();
        m_state = State::Off;
        return false;
    }
    m_io = io;
    connect(io, &EngineIo::linesRead, this, &UciEngine::onLines);
    connect(io, &EngineIo::failedToStart, this, [this] { emit errorOccurred(tr("Impossible de lancer le moteur « %1 ».").arg(m_path)); });
    connect(io, &EngineIo::finished, this, [this] {
        m_state = State::Off;
        emit terminated();
    });
    m_state = State::Initializing;
    send(QStringLiteral("uci"));
    return true;
}

void UciEngine::quit()
{
    if (!m_io) return;
    disconnect(m_io, nullptr, this, nullptr);
    EngineIo* io = m_io;
    m_io = nullptr;
    m_state = State::Off;
    m_hasPending = false;
    QMetaObject::invokeMethod(io, &EngineIo::shutdown, Qt::QueuedConnection); // the interface does not wait
}

void UciEngine::send(const QString& cmd)
{
    if (!m_io) return;
    QMetaObject::invokeMethod(m_io, [io = m_io, bytes = (cmd + '\n').toUtf8()] { io->write(bytes); }, Qt::QueuedConnection);
}

bool UciEngine::hasPendingAnswer() const
{
    return m_io && (m_state == State::Searching || m_state == State::Stopping) && m_io->bestMoveNs.load() > m_goNs;
}

void UciEngine::setOption(const QString& name, const QString& value)
{
    // An empty SyzygyPath in an engine's configuration (its default value) does not erase
    // the tablebase folder chosen in the Preferences.
    if (name.compare(QLatin1String("SyzygyPath"), Qt::CaseInsensitive) == 0) {
        if (value.trimmed().isEmpty()) return;
        m_tbManual = true; // set by hand: the Preferences no longer touch it
    }
    m_queued << QStringLiteral("setoption name %1 value %2").arg(name, value);
    if (m_state == State::Idle) flushQueue();
}

void UciEngine::newGame()
{
    m_queued << QStringLiteral("ucinewgame");
    if (m_state == State::Idle) flushQueue();
}

void UciEngine::flushQueue()
{
    for (const QString& cmd : std::as_const(m_queued)) send(cmd);
    m_queued.clear();
}

void UciEngine::search(const QString& fen, const QString& goArgs)
{
    m_pendingFen = fen;
    m_pendingGo = goArgs;
    m_hasPending = true;
    switch (m_state) {
    case State::Idle: beginSearch(); break;
    case State::Searching:
        send(QStringLiteral("stop"));
        m_state = State::Stopping;
        break;
    default: break; // Initializing / Stopping: the search will start later
    }
}

void UciEngine::stop()
{
    m_hasPending = false;
    if (m_state == State::Searching) {
        send(QStringLiteral("stop"));
        m_state = State::Stopping;
    }
}

void UciEngine::ponderHit()
{
    if (m_state != State::Searching) return;
    m_goNs = EngineIo::nowNs(); // the engine's time counts from here
    send(QStringLiteral("ponderhit"));
}

void UciEngine::moveNow()
{
    // Unlike stop(), the state stays "Searching": the bestmove will be passed on.
    if (m_state == State::Searching) send(QStringLiteral("stop"));
}

void UciEngine::syncTablebases()
{
    if (m_tbManual || !m_hasSyzygyOption) return;
    const QString tables = tablebasePathWanted();
    if (tables == m_tbSent) return;
    // "<empty>": the empty string of the UCI protocol, the engine closes its tables
    send(QStringLiteral("setoption name SyzygyPath value %1").arg(tables.isEmpty() ? QStringLiteral("<empty>") : tables));
    m_tbSent = tables;
}

void UciEngine::beginSearch()
{
    // The tablebase settings may have changed since the engine was started
    syncTablebases();
    flushQueue();
    m_hasPending = false;
    m_searchFen = m_pendingFen;
    // Chess960: engines expect the UCI_Chess960 option and then write castling as "the king
    // captures its rook", the form Position uses for these positions.
    {
        chess::Position start;
        const qsizetype moves = m_pendingFen.indexOf(QLatin1String(" moves "));
        const bool chess960 = start.setFen((moves < 0 ? m_pendingFen : m_pendingFen.left(moves)).toStdString()) && start.isChess960();
        if (chess960 != m_chess960) {
            m_chess960 = chess960;
            send(QStringLiteral("setoption name UCI_Chess960 value %1").arg(chess960 ? QStringLiteral("true") : QStringLiteral("false")));
        }
    }
    send(QStringLiteral("position fen ") + m_pendingFen);
    m_goNs = EngineIo::nowNs();
    send(QStringLiteral("go ") + m_pendingGo);
    m_state = State::Searching;
}

void UciEngine::onLines(const QList<QByteArray>& lines, qint64 stampNs)
{
    m_stampNs = stampNs;
    for (const QByteArray& line : lines) {
        if (!m_io) return; // engine quit while a line was being processed
        parseLine(QString::fromUtf8(line));
    }
}

void UciEngine::parseLine(const QString& line)
{
    if (line.isEmpty()) return;
    const QStringList t = line.split(' ', Qt::SkipEmptyParts);
    const QString& cmd = t.first();

    if (cmd == QLatin1String("id") && t.size() > 2 && t[1] == QLatin1String("name")) {
        m_name = t.mid(2).join(' ');
    } else if (cmd == QLatin1String("option") && t.value(1) == QLatin1String("name") && t.value(2).compare(QLatin1String("SyzygyPath"), Qt::CaseInsensitive) == 0) {
        m_hasSyzygyOption = true;
    } else if (cmd == QLatin1String("uciok")) {
        // Tablebases: before the pending options, so that a manual setting comes after
        if (const QString tables = tablebasePathWanted(); !tables.isEmpty()) {
            send(QStringLiteral("setoption name SyzygyPath value %1").arg(tables));
            m_tbSent = tables;
        }
        flushQueue();
        send(QStringLiteral("isready"));
    } else if (cmd == QLatin1String("readyok")) {
        if (m_state == State::Initializing) {
            m_state = State::Idle;
            emit started(m_name);
            if (m_hasPending) beginSearch();
        }
    } else if (cmd == QLatin1String("bestmove")) {
        const bool wasSearching = m_state == State::Searching || m_state == State::Stopping;
        if (!wasSearching) return;
        const bool stoppedByUs = m_state == State::Stopping;
        m_lastThinkMs = std::max<qint64>(0, ((m_stampNs ? m_stampNs : EngineIo::nowNs()) - m_goNs) / 1000000);
        m_ponderMove = t.value(2) == QLatin1String("ponder") ? t.value(3) : QString();
        m_state = State::Idle;
        if (m_hasPending) {
            beginSearch();
        } else {
            flushQueue(); // pending options and "ucinewgame"
            if (!stoppedByUs) emit bestMoveReceived(t.value(1), m_searchFen);
        }
    } else if (cmd == QLatin1String("info") && m_state == State::Searching) {
        EngineLine info;
        bool hasScore = false, hasPv = false;
        for (int i = 1; i < t.size(); ++i) {
            const QString& k = t[i];
            if (k == QLatin1String("depth")) info.depth = t.value(++i).toInt();
            else if (k == QLatin1String("seldepth")) info.seldepth = t.value(++i).toInt();
            else if (k == QLatin1String("multipv")) info.multipv = t.value(++i).toInt();
            else if (k == QLatin1String("nodes")) info.nodes = t.value(++i).toLongLong();
            else if (k == QLatin1String("nps")) info.nps = t.value(++i).toLongLong();
            else if (k == QLatin1String("tbhits")) info.tbHits = t.value(++i).toLongLong();
            else if (k == QLatin1String("time")) info.timeMs = t.value(++i).toLongLong();
            else if (k == QLatin1String("score")) {
                const QString kind = t.value(++i);
                info.mate = kind == QLatin1String("mate");
                info.score = t.value(++i).toInt();
                hasScore = true;
            } else if (k == QLatin1String("pv")) {
                info.pv = t.mid(i + 1);
                hasPv = true;
                break;
            } else if (k == QLatin1String("string")) {
                break;
            }
        }
        if (hasScore && hasPv && !info.pv.isEmpty()) emit infoReceived(info);
    }
}
