#include "solutiontest.h"

#include "bundledbases.h"

#include "chess/epd.h"
#include "chess/pgn.h"
#include "conversion.h"
#include "db/database.h"
#include "engine/uciengine.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

using namespace chess;

namespace solutiontest {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("SolutionTest", text); }

bool badNag(int nag) { return nag == 2 || nag == 4 || nag == 6; } // ?, ??, ?!

// Game → position: first move of the main line, variations on the first move.
bool fromGame(const Game& g, int number, TestPosition& out)
{
    const Node* root = g.root();
    if (!root || root->children.empty()) return false;
    out.fen = QString::fromStdString(g.startPosition().fen());
    QStringList good, bad;
    for (const auto& child : root->children) {
        const bool avoid = std::any_of(child->nags.begin(), child->nags.end(), badNag);
        const QString uci = QString::fromStdString(Position::toUci(child->move));
        const QString san = QString::fromStdString(child->san.empty() ? g.startPosition().toSan(child->move) : child->san);
        if (avoid) {
            out.avoid << uci;
            bad << san;
        } else {
            out.best << uci;
            good << san;
        }
    }
    out.solution = good.join(QStringLiteral(", "));
    if (!bad.isEmpty()) out.solution += (good.isEmpty() ? QString() : QStringLiteral(" ; ")) + tr("pas %1").arg(bad.join(QStringLiteral(", ")));
    const std::string event = g.header("Event");
    out.id = event.empty() || event == "?" ? QString::number(number) : QString::fromStdString(event);
    // Full continuation (main line) and "dm N" from the starting comment
    // (collections created by "Add to collection" or converted from an EPD)
    out.rating = std::max(0, std::atoi(g.header("WhiteElo").c_str()));
    const std::vector<Node*> line = g.mainline();
    if (!line.empty()) {
        QStringList sans;
        Position p = g.startPosition();
        for (const Node* n : line) {
            sans << QString::fromStdString(p.toSan(n->move));
            p.makeMove(n->move);
        }
        out.line = sans.join(QLatin1Char(' '));
    }
    for (const auto& [code, value] : chess::epd::parseOperations(root->comment))
        if (code == "dm") out.mateIn = std::max(0, QString::fromStdString(value).toInt());
    return !out.best.isEmpty() || !out.avoid.isEmpty();
}

// EPD line: 4 position fields (+ possibly the 2 counters), then "opcode operands;".
bool fromEpd(const QString& line, int number, TestPosition& out)
{
    // Common reader (chess::epd): a ";" inside quotes stays in the string
    chess::epd::Record record;
    if (!chess::epd::parseLine(line.toStdString(), record)) return false;
    Position pos;
    if (!pos.setFen(record.fen)) return false;
    out.fen = QString::fromStdString(pos.fen());
    out.id = QString::number(number);
    QStringList goodSan, badSan;
    for (const auto& [opcode, operands] : record.ops) {
        const QString code = QString::fromStdString(opcode);
        const QStringList parts = (code + QLatin1Char(' ') + QString::fromStdString(operands)).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (code == QLatin1String("id")) {
            out.id = QString::fromStdString(operands);
        } else if (code == QLatin1String("dm") && parts.size() > 1) {
            out.mateIn = std::max(0, parts[1].toInt());
        } else if (code == QLatin1String("pv") && parts.size() > 1) {
            out.line = QString::fromStdString(operands);
        } else if (code == QLatin1String("bm") || code == QLatin1String("am")) {
            for (const QString& san : parts.mid(1)) {
                const Move m = pos.parseSan(san.toStdString());
                if (m.isNull()) continue;
                const QString uci = QString::fromStdString(Position::toUci(m));
                const QString shown = QString::fromStdString(pos.toSan(m));
                if (code == QLatin1String("bm")) {
                    out.best << uci;
                    goodSan << shown;
                } else {
                    out.avoid << uci;
                    badSan << shown;
                }
            }
        }
    }
    out.solution = goodSan.join(QStringLiteral(", "));
    if (!badSan.isEmpty()) out.solution += (goodSan.isEmpty() ? QString() : QStringLiteral(" ; ")) + tr("pas %1").arg(badSan.join(QStringLiteral(", ")));
    return !out.best.isEmpty() || !out.avoid.isEmpty();
}

} // namespace

bool isCorrect(const TestPosition& p, const QString& uci)
{
    if (uci.isEmpty() || p.avoid.contains(uci)) return false;
    return p.best.isEmpty() || p.best.contains(uci);
}

QList<TestPosition> load(const QString& saved, QString* error)
{
    const QString path = bundled::currentPath(saved); // bundled collection, saved before the folders were renamed
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return QList<TestPosition>();
    };
    const QString name = QFileInfo(path).fileName();
    if (conversion::isProprietary(path))
        return fail(tr("« %1 » est dans un format propriétaire : convertissez-le d'abord (Fichier → Convertir…) en base Hydra64 ou en PGN.").arg(name));
    QList<TestPosition> out;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("hy64i") || suffix == QLatin1String("hy64")) {
        hy64db::Database db;
        std::string err;
        if (!db.open(path.toStdString(), &err)) return fail(tr("Impossible d'ouvrir « %1 » : %2").arg(name, QString::fromStdString(err)));
        int number = 0;
        for (uint32_t id : db.allGames()) {
            Game g;
            TestPosition p;
            ++number;
            if (db.game(id, g) && fromGame(g, number, p)) out << p;
        }
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return fail(tr("Impossible d'ouvrir « %1 » : %2").arg(name, f.errorString()));
        const QByteArray data = f.readAll();
        if (suffix == QLatin1String("pgn")) {
            const std::string_view text(data.constData(), size_t(data.size()));
            int number = 0;
            for (const pgn::GameEntry& e : pgn::indexDatabase(text)) {
                Game g;
                TestPosition p;
                ++number;
                if (pgn::parseGame(text.substr(e.offset, e.length), g) && fromGame(g, number, p)) out << p;
            }
        } else {
            int number = 0;
            for (const QByteArray& raw : data.split('\n')) {
                const QString line = QString::fromUtf8(raw).trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
                TestPosition p;
                ++number;
                if (fromEpd(line, number, p)) out << p;
            }
        }
    }
    if (out.isEmpty()) return fail(tr("Aucune position avec solution dans « %1 ».").arg(name));
    return out;
}

} // namespace solutiontest

SolutionTest::SolutionTest(QObject* parent)
    : QObject(parent)
{
}

SolutionTest::~SolutionTest() { shutdown(); }

int SolutionTest::solvedCount() const
{
    return int(std::count_if(m_results.begin(), m_results.end(), [](const Result& r) { return r.solved; }));
}

bool SolutionTest::start(const EngineConfig& cfg, const QList<TestPosition>& positions, int movetimeMs, QString* error)
{
    shutdown();
    if (!cfg.isValid() || positions.isEmpty()) {
        if (error) *error = cfg.isValid() ? tr("aucune position") : tr("aucun moteur");
        return false;
    }
    m_positions = positions;
    m_results = QList<Result>(positions.size());
    m_movetime = std::max(100, movetimeMs);
    m_current = -1;
    m_stopping = false;
    m_engine = new UciEngine(this);
    connect(m_engine, &UciEngine::started, this, [this] { next(); });
    connect(m_engine, &UciEngine::infoReceived, this, &SolutionTest::onInfo);
    connect(m_engine, &UciEngine::bestMoveReceived, this, &SolutionTest::onBestMove);
    connect(m_engine, &UciEngine::errorOccurred, this, [this](const QString& message) {
        if (m_stopping) return;
        emit errorOccurred(message);
    });
    connect(m_engine, &UciEngine::terminated, this, [this] {
        if (!m_engine || m_stopping) return;
        m_engine->deleteLater();
        m_engine = nullptr;
        emit errorOccurred(tr("Le moteur s'est arrêté"));
        emit finished(false);
    });
    if (!m_engine->start(cfg.path)) {
        delete m_engine;
        m_engine = nullptr;
        if (error) *error = tr("Impossible de lancer le moteur");
        return false;
    }
    if (cfg.id.isEmpty()) m_engine->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
    for (auto it = cfg.values.cbegin(); it != cfg.values.cend(); ++it)
        if (it.key().compare(QLatin1String("MultiPV"), Qt::CaseInsensitive) != 0) m_engine->setOption(it.key(), it.value());
    return true;
}

void SolutionTest::stop()
{
    if (!m_engine) return;
    m_stopping = true;
    shutdown();
    emit finished(false);
}

void SolutionTest::shutdown()
{
    if (!m_engine) return;
    UciEngine* e = m_engine;
    m_engine = nullptr;
    e->disconnect(this);
    e->quit();
    e->deleteLater();
}

void SolutionTest::next()
{
    if (!m_engine || m_stopping) return;
    if (++m_current >= m_positions.size()) {
        shutdown();
        emit finished(true);
        return;
    }
    emit positionStarted(m_current);
    m_engine->newGame(); // with no memory of the previous position
    m_engine->search(m_positions[m_current].fen, QStringLiteral("movetime %1").arg(m_movetime));
}

void SolutionTest::onInfo(const EngineLine& line)
{
    if (m_current < 0 || m_current >= m_positions.size() || line.multipv != 1 || line.pv.isEmpty()) return;
    Result& r = m_results[m_current];
    const bool was = solutiontest::isCorrect(m_positions[m_current], r.move);
    r.move = line.pv.first();
    r.depth = line.depth;
    r.mate = line.mate;
    r.score = line.score;
    const bool now = solutiontest::isCorrect(m_positions[m_current], r.move);
    if (now && !was) r.foundMs = line.timeMs;
    else if (!now) r.foundMs = -1;
    emit positionUpdated(m_current);
}

void SolutionTest::onBestMove(const QString& uci, const QString& fen)
{
    if (m_current < 0 || m_current >= m_positions.size() || m_stopping) return;
    if (!fen.isEmpty() && fen != m_positions[m_current].fen) return; // answer of a previous search
    Result& r = m_results[m_current];
    const bool was = solutiontest::isCorrect(m_positions[m_current], r.move);
    r.move = uci;
    r.solved = solutiontest::isCorrect(m_positions[m_current], uci);
    if (r.solved && (!was || r.foundMs < 0)) r.foundMs = m_movetime; // found at the last moment
    if (!r.solved) r.foundMs = -1;
    r.done = true;
    emit positionFinished(m_current);
    QTimer::singleShot(0, this, &SolutionTest::next);
}
