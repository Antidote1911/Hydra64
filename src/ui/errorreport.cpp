#include "errorreport.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

using namespace chess;

namespace errorreport {

namespace {
// Winning chances of `player` in a position evaluated for the side to move.
double chancesOf(const annotation::PositionEval& e, Color toMove, Color player)
{
    const double c = annotation::winningChances(e);
    return toMove == player ? c : -c;
}

QJsonObject toJson(const Mistake& m)
{
    return {{"ply", m.ply}, {"san", m.san}, {"best", m.best}, {"drop", m.drop}, {"before", m.before}, {"after", m.after}, {"phase", int(m.phase)}, {"standing", int(m.standing)}};
}

Mistake mistakeFrom(const QJsonObject& o)
{
    Mistake m;
    m.ply = o.value("ply").toInt();
    m.san = o.value("san").toString();
    m.best = o.value("best").toString();
    m.drop = o.value("drop").toDouble();
    m.before = o.value("before").toString();
    m.after = o.value("after").toString();
    m.phase = Phase(std::clamp(o.value("phase").toInt(), 0, 2));
    m.standing = Standing(std::clamp(o.value("standing").toInt(), 0, 2));
    return m;
}
} // namespace

Phase phaseOf(const Position& before, int ply)
{
    int material = 0;
    for (int s = 0; s < 64; ++s) {
        switch (typeOf(before.at(Square(s)))) {
        case Queen: material += 9; break;
        case Rook: material += 5; break;
        case Bishop:
        case Knight: material += 3; break;
        default: break;
        }
    }
    if (material <= 26) return Phase::Endgame;
    return (ply + 1) / 2 <= 15 ? Phase::Opening : Phase::Middlegame;
}

GameErrors analyse(const Game& game, const QList<annotation::PositionEval>& evals, int firstPly, Color player)
{
    GameErrors out;
    const std::vector<Node*> line = game.mainline();
    for (int ply = firstPly + 1; ply <= int(line.size()) && ply - firstPly < evals.size(); ++ply) {
        const Node* node = line[size_t(ply - 1)];
        const Position& pos = node->parent->pos;
        if (pos.sideToMove() != player) continue;
        const annotation::PositionEval& before = evals[ply - 1 - firstPly];
        const annotation::PositionEval& after = evals[ply - firstPly];
        if (!before.valid || !after.valid || before.pv.isEmpty()) continue;
        if (before.pv.first() == QString::fromStdString(Position::toUci(node->move))) continue; // the engine's move
        const double had = annotation::winningChances(before);
        const double drop = had - chancesOf(after, ~player, player);
        if (drop < 0.2) continue;
        ++out.mistakes;
        if (drop >= 0.3) ++out.blunders;
        if (out.hasTurning && drop <= out.turning.drop) continue;
        out.hasTurning = true;
        Mistake& m = out.turning;
        m.ply = ply;
        m.san = QString::fromStdString(pos.toSan(node->move));
        const Move best = pos.parseUci(before.pv.first().toStdString());
        m.best = !best.isNull() && pos.isLegal(best) ? QString::fromStdString(pos.toSan(best)) : QString();
        m.drop = drop;
        m.before = annotation::formatWhite(before, player);
        m.after = annotation::formatWhite(after, ~player);
        m.phase = phaseOf(pos, ply);
        m.standing = had > 0.25 ? Standing::Better : had < -0.25 ? Standing::Worse : Standing::Equal;
    }
    return out;
}

QString fingerprint(const hy64db::Database& db, uint32_t id)
{
    const hy64db::Record r = db.record(id);
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(QString::fromStdString(db.names().name(hy64db::Player, r.white)), QString::fromStdString(db.names().name(hy64db::Player, r.black)))
        .arg(r.date)
        .arg(r.plies)
        .arg(r.result);
}

QString cachePath(const QString& databasePath)
{
    const QFileInfo info(databasePath);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".errors.json");
}

std::map<uint32_t, GameErrors> loadCache(const QString& path, const hy64db::Database& db)
{
    std::map<uint32_t, GameErrors> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value("version").toInt() != 1) return out;
    for (const QJsonValue& v : root.value("games").toArray()) {
        const QJsonObject o = v.toObject();
        GameErrors g;
        g.game = uint32_t(o.value("game").toInteger(-1));
        g.fingerprint = o.value("fingerprint").toString();
        if (g.game >= db.count() || fingerprint(db, g.game) != g.fingerprint) continue; // the database changed since
        g.engine = o.value("engine").toString();
        g.movetimeMs = o.value("movetime").toInt();
        g.mistakes = o.value("mistakes").toInt();
        g.blunders = o.value("blunders").toInt();
        g.hasTurning = o.contains("turning");
        if (g.hasTurning) g.turning = mistakeFrom(o.value("turning").toObject());
        out[g.game] = g;
    }
    return out;
}

bool saveCache(const QString& path, const std::map<uint32_t, GameErrors>& results)
{
    QJsonArray games;
    for (const auto& [id, g] : results) {
        QJsonObject o{{"game", qint64(id)}, {"fingerprint", g.fingerprint}, {"engine", g.engine}, {"movetime", g.movetimeMs}, {"mistakes", g.mistakes}, {"blunders", g.blunders}};
        if (g.hasTurning) o.insert("turning", toJson(g.turning));
        games.append(o);
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"games", games}}).toJson(QJsonDocument::Compact));
    return f.commit();
}

} // namespace errorreport

// ---------------------------------------------------------------- ErrorScan

ErrorScan::ErrorScan(const hy64db::Database& db, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_annotator(new Annotator(this))
{
    connect(m_annotator, &Annotator::finished, this, &ErrorScan::gameFinished);
    // Game lost beyond doubt (four positions in a row): what follows teaches nothing, the analysis stops
    // there. Asked for once the annotator has handed back (it is in the middle of its loop here).
    connect(m_annotator, &Annotator::progress, this, [this](int done, int) {
        if (!m_running || m_decided || done < 4) return;
        const auto& evals = m_annotator->evaluations();
        for (int k = done - 4; k < done; ++k) {
            const Color toMove = (m_firstPly + k) % 2 == 0 ? White : Black;
            if (!evals[k].valid) return;
            const double c = annotation::winningChances(evals[k]);
            if ((toMove == m_player ? c : -c) > -0.9) return;
        }
        m_decided = true;
        QTimer::singleShot(0, this, [this] {
            if (m_running && m_decided) m_annotator->cancel(); // → gameFinished(false), taken as done
        });
    });
}

bool ErrorScan::start(const EngineConfig& engine, std::vector<uint32_t> games, Color player, int movetimeMs, QString* error)
{
    if (m_running) cancel();
    if (!engine.isValid()) {
        if (error) *error = tr("aucun moteur");
        return false;
    }
    m_engine = engine;
    m_games = std::move(games);
    m_at = 0;
    m_player = player;
    m_movetime = std::max(50, movetimeMs);
    m_running = true;
    QTimer::singleShot(0, this, &ErrorScan::nextGame);
    return true;
}

void ErrorScan::cancel()
{
    if (!m_running) return;
    m_running = false;
    m_decided = false;
    m_annotator->cancel();
    emit finished(false);
}

void ErrorScan::nextGame()
{
    while (m_running) {
        emit progress(int(m_at), int(m_games.size()));
        if (m_at >= m_games.size()) {
            m_running = false;
            emit finished(true);
            return;
        }
        m_game = Game();
        m_decided = false;
        QStringList fens;
        if (m_db.game(m_games[m_at], m_game)) {
            const std::vector<Node*> line = m_game.mainline();
            // The first four moves are not analysed (theory), unless the game is hardly longer.
            m_firstPly = line.size() > 16 ? 8 : 0;
            for (int ply = m_firstPly; ply <= int(line.size()); ++ply)
                fens << QString::fromStdString((ply == 0 ? m_game.root() : line[size_t(ply - 1)])->pos.fen());
        }
        QString error;
        if (fens.size() >= 2 && m_annotator->start(m_engine, fens, m_movetime, &error)) return; // → gameFinished
        if (fens.size() >= 2) { // the engine does not start: nothing more to expect
            m_running = false;
            emit finished(false);
            return;
        }
        ++m_at; // game without moves
    }
}

void ErrorScan::gameFinished(bool completed)
{
    if (!m_running) return;
    if (!completed && !m_decided) { // engine stopped
        m_running = false;
        emit finished(false);
        return;
    }
    errorreport::GameErrors result = errorreport::analyse(m_game, m_annotator->evaluations(), m_firstPly, m_player);
    result.game = m_games[m_at];
    result.fingerprint = errorreport::fingerprint(m_db, result.game);
    result.engine = m_annotator->engineName();
    result.movetimeMs = m_movetime;
    emit gameDone(result);
    ++m_at;
    QTimer::singleShot(0, this, &ErrorScan::nextGame);
}
