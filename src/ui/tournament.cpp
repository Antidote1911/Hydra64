#include "tournament.h"
#include "userdirs.h"

#include "hy64source.h"
#include "bundledbases.h"

#include "chess/tablebase.h"
#include "chessclock.h"
#include "display.h"
#include "engine/uciengine.h"

#include "chess/pgn.h"
#include "chess/polyglot.h"
#include "chess/treebook.h"
#include "conversion.h"
#include "db/database.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>
#include <functional>
#include <QSet>

#include <algorithm>
#include <limits>
#include <cmath>

using namespace chess;

namespace {
constexpr int MaxPlies = 600; // game declared drawn beyond this (the engines go round in circles)
constexpr int MaxOpenings = 1000000;

// "<fen> [moves <uci>…]" → game (false if the position or a move is invalid).
bool buildOpening(const QString& opening, Game& game)
{
    const qsizetype cut = opening.indexOf(QLatin1String(" moves "));
    const QString fen = (cut < 0 ? opening : opening.left(cut)).trimmed();
    Position start;
    if (!start.setFen(fen.toStdString())) return false;
    game.reset(start);
    if (cut >= 0)
        for (const QString& uci : opening.mid(cut + 7).split(' ', Qt::SkipEmptyParts)) {
            Node* last = game.current();
            const Move m = last->pos.parseUci(uci.toStdString());
            if (m.isNull() || !last->pos.isLegal(m)) return false;
            game.setCurrent(game.addMove(last, m));
        }
    return true;
}
}

QStringList Tournament::loadOpenings(const QString& saved, QString* error)
{
    const QString path = bundled::currentPath(saved); // bundled collection, saved before the folders were renamed
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return QStringList();
    };
    // Starting position and moves of the main line of a game
    auto openingOf = [](const Game& g) {
        QString line = QString::fromStdString(g.startPosition().fen());
        QStringList moves;
        for (const Node* n : g.mainline()) moves << QString::fromStdString(Position::toUci(n->move));
        if (!moves.isEmpty()) line += QStringLiteral(" moves ") + moves.join(' ');
        return line;
    };
    if (conversion::isProprietary(path))
        return fail(tr("« %1 » est dans un format propriétaire : convertissez-le d'abord (Fichier → Convertir…) en base Hydra64 ou en PGN.")
                        .arg(QFileInfo(path).fileName()));
    if (const QString suffix = QFileInfo(path).suffix().toLower(); suffix == QLatin1String("hy64i") || suffix == QLatin1String("hy64")) {
        // Hydra64 database: each (non-deleted) game gives a starting position and its moves.
        hy64db::Database db;
        std::string err;
        if (!db.open(path.toStdString(), &err)) return fail(tr("Impossible d'ouvrir « %1 » : %2").arg(path, QString::fromStdString(err)));
        QStringList out;
        for (uint32_t id : db.allGames()) {
            if (out.size() >= MaxOpenings) break;
            Game g;
            if (db.game(id, g)) out << openingOf(g);
        }
        if (out.isEmpty()) return fail(tr("Aucune position valide dans « %1 ».").arg(QFileInfo(path).fileName()));
        return out;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(tr("Impossible d'ouvrir « %1 » : %2").arg(path, f.errorString()));
    const QByteArray data = f.readAll();
    QStringList out;
    if (QFileInfo(path).suffix().compare(QLatin1String("pgn"), Qt::CaseInsensitive) == 0) {
        const std::string_view text(data.constData(), size_t(data.size()));
        for (const pgn::GameEntry& e : pgn::indexDatabase(text)) {
            if (out.size() >= MaxOpenings) break;
            Game g;
            if (!pgn::parseGame(text.substr(e.offset, e.length), g)) continue;
            out << openingOf(g);
        }
    } else {
        // EPD: one position per line; EPD operations ("bm …; id …;") are ignored.
        for (const QByteArray& raw : data.split('\n')) {
            if (out.size() >= MaxOpenings) break;
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) continue;
            QStringList fields = line.split(' ', Qt::SkipEmptyParts);
            if (fields.size() < 2) continue;
            bool numeric = fields.size() >= 6;
            if (numeric) {
                fields[4].toInt(&numeric);
                if (numeric) fields[5].toInt(&numeric);
            }
            const QString fen = fields.mid(0, numeric ? 6 : 4).join(' ');
            Position p;
            if (p.setFen(fen.toStdString())) out << QString::fromStdString(p.fen());
        }
    }
    if (out.isEmpty()) return fail(tr("Aucune position valide dans « %1 ».").arg(QFileInfo(path).fileName()));
    return out;
}

Tournament::Tournament(QObject* parent)
    : QObject(parent)
    , m_idleClock(new ChessClock(this))
{
}

Tournament::~Tournament()
{
    for (auto& g : m_slots) shutdownEngines(*g);
    closeIdleEngines();
}

const chess::Game& Tournament::liveGame() const { return m_shown ? m_shown->live : m_idle; }
const ChessClock* Tournament::clock() const { return m_shown ? m_shown->clock : m_idleClock; }
int Tournament::currentIndex() const { return m_shown ? m_shown->index : -1; }

// ---------------------------------------------------------------- Participant

Tournament::Participant Tournament::Participant::fromEngine(const EngineConfig& engine)
{
    return {QUuid::createUuid().toString(QUuid::WithoutBraces), engine.id, engine.name, engine.values};
}

EngineConfig Tournament::Participant::config() const
{
    EngineConfig cfg = EngineRegistry::instance().engine(engineId);
    if (!cfg.isValid()) return {};
    cfg.name = name;
    cfg.values = values;
    return cfg;
}

QJsonObject Tournament::Participant::toJson() const
{
    QJsonObject v;
    for (auto it = values.cbegin(); it != values.cend(); ++it) v[it.key()] = it.value();
    return QJsonObject{{"id", id}, {"engine", engineId}, {"name", name}, {"values", v}};
}

Tournament::Participant Tournament::Participant::fromJson(const QJsonObject& o)
{
    Participant p{o.value("id").toString(), o.value("engine").toString(), o.value("name").toString(), {}};
    const QJsonObject v = o.value("values").toObject();
    for (auto it = v.begin(); it != v.end(); ++it) p.values[it.key()] = it.value().toString();
    if (p.id.isEmpty()) p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return p;
}

const Tournament::Participant* Tournament::participant(const QString& id) const
{
    for (const Participant& p : m_settings.participants)
        if (p.id == id) return &p;
    return nullptr;
}

QString Tournament::defaultPgnDir()
{
    return userdirs::tournaments();
}

QString Tournament::newPgnPath(const QString& dir)
{
    const QString base = dir + QStringLiteral("/Tournoi ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH'h'mm"));
    QString path = base + QStringLiteral(".hy64");
    for (int n = 2; QFileInfo::exists(path); ++n) path = base + QStringLiteral(" (%1).hy64").arg(n);
    return path;
}

bool Tournament::appendToDatabase(const QString& path, const chess::Game& game, QString* error)
{
    if (!Hy64Source::isDatabasePath(path) && !hy64db::isPackedPath(path.toStdString())) { // PGN: appended to the text
        QFile f(path);
        if (!f.open(QIODevice::ReadWrite | QIODevice::Append)) {
            if (error) *error = f.errorString();
            return false;
        }
        QByteArray text = QByteArray::fromStdString(pgn::writeGame(game)).trimmed() + "\n";
        if (f.size() > 0) text.prepend('\n');
        return f.write(text) == text.size();
    }
    if (!QFileInfo::exists(path) && !Hy64Source::create(path, error)) return false;
    hy64db::Database db;
    std::string err;
    uint32_t id = 0;
    const bool ok = db.open(path.toStdString(), &err) && db.append(game, &id, &err);
    if (ok) db.sync();
    db.close();
    if (!ok && error) *error = QString::fromStdString(err);
    return ok;
}

bool Tournament::setup(const Settings& settings, QString* error)
{
    closeIdleEngines(); // the kept engines had the settings of the previous tournament
    m_enginesReused = 0;
    if (settings.participants.size() < 2) {
        if (error) *error = tr("Choisissez au moins deux moteurs.");
        return false;
    }
    stop();
    m_settings = settings;
    m_settings.cycles = std::max(1, settings.cycles);
    m_settings.concurrency = std::clamp(settings.concurrency, 1, 64);
    QStringList openings;
    if (!m_settings.openingsFile.isEmpty()) {
        openings = loadOpenings(m_settings.openingsFile, error);
        if (openings.isEmpty()) return false;
        if (m_settings.openingsRandom)
            for (qsizetype i = openings.size() - 1; i > 0; --i) openings.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
    }
    m_bookTree.reset();
    m_bookPolyglot.reset();
    if (!m_settings.bookFile.isEmpty()) {
        const std::string path = QFile::encodeName(m_settings.bookFile).toStdString();
        bool ok = false;
        if (m_settings.bookFile.endsWith(QLatin1String(".bin"), Qt::CaseInsensitive)) {
            m_bookPolyglot = std::make_shared<polyglot::Book>();
            ok = m_bookPolyglot->open(path);
        } else {
            m_bookTree = std::make_shared<treebook::Book>();
            ok = m_bookTree->open(path);
        }
        if (!ok) {
            if (error) *error = tr("Livre des moteurs illisible : %1").arg(m_settings.bookFile);
            m_bookTree.reset();
            m_bookPolyglot.reset();
            return false;
        }
    }
    if (!m_settings.pgnFile.isEmpty()) {
        // The database itself is only written with the first finished game: a tournament set up and
        // never played, or interrupted before the end of a game, leaves no empty file. Its folder is
        // checked now, so that a wrong folder is reported before the games are played.
        const QString folder = QFileInfo(m_settings.pgnFile).absolutePath();
        if (!QDir().mkpath(folder) || !QFileInfo(folder).isWritable()) {
            if (error) *error = tr("Impossible de créer la base du tournoi « %1 » :\n%2").arg(m_settings.pgnFile, tr("dossier inaccessible en écriture"));
            return false;
        }
    }
    m_games.clear();
    QStringList ids, names;
    for (const Participant& p : m_settings.participants) {
        ids << p.id;
        names << p.name;
    }
    m_openings = openings;
    m_pairing = 0;
    m_swissRound = 0;
    if (m_settings.schedule == Settings::Schedule::Swiss) {
        pairSwissRound(); // round 1; the following ones according to the results
        m_shown = nullptr;
        emit changed();
        return true;
    }
    int pairing = 0; // each pairing takes the next position, the same for both colours
    // Round robin (or gauntlet: the first against each); every other cycle, the colours are reversed.
    const bool gauntlet = m_settings.schedule == Settings::Schedule::Gauntlet;
    for (int c = 0; c < m_settings.cycles; ++c) {
        for (int i = 0; i < (gauntlet ? 1 : int(ids.size())); ++i) {
            for (int j = i + 1; j < ids.size(); ++j) {
                const bool swap = c % 2 == 1;
                const int a = swap ? j : i, b = swap ? i : j;
                const QString opening = openings.isEmpty() ? QString() : openings[pairing++ % openings.size()];
                m_games << GameRecord{ids[a], ids[b], names[a], names[b], {}, {}, {}, opening};
                if (m_settings.doubleRound) m_games << GameRecord{ids[b], ids[a], names[b], names[a], {}, {}, {}, opening};
            }
        }
    }
    m_shown = nullptr;
    emit changed();
    return true;
}

bool Tournament::isComplete() const
{
    if (m_settings.schedule == Settings::Schedule::Swiss && m_swissRound < m_settings.cycles) return false;
    return !m_games.isEmpty() && std::all_of(m_games.begin(), m_games.end(), [](const GameRecord& g) { return !g.result.isEmpty(); });
}

QString Tournament::nextOpening()
{
    if (m_openings.isEmpty() && !m_settings.openingsFile.isEmpty()) {
        m_openings = loadOpenings(m_settings.openingsFile, nullptr);
        if (m_settings.openingsRandom)
            for (qsizetype i = m_openings.size() - 1; i > 0; --i) m_openings.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
    }
    return m_openings.isEmpty() ? QString() : m_openings[m_pairing++ % m_openings.size()];
}

bool Tournament::pairSwissRound()
{
    if (m_swissRound >= m_settings.cycles) return false;
    const int round = ++m_swissRound;
    // Points, opponents already met, games with White, byes
    QStringList ids;
    QMap<QString, QString> names;
    QMap<QString, int> seed;
    for (const Participant& p : m_settings.participants) {
        seed[p.id] = int(ids.size());
        ids << p.id;
        names[p.id] = p.name;
    }
    QMap<QString, double> points;
    QMap<QString, int> whites, blacks;
    QSet<QString> met, byes;
    for (const GameRecord& g : m_games) {
        if (g.blackId.isEmpty()) { // bye
            byes << g.whiteId;
            points[g.whiteId] += 1;
            continue;
        }
        met << g.whiteId + QLatin1Char('|') + g.blackId << g.blackId + QLatin1Char('|') + g.whiteId;
        ++whites[g.whiteId];
        ++blacks[g.blackId];
        if (g.result == QLatin1String("1-0")) points[g.whiteId] += 1;
        else if (g.result == QLatin1String("0-1")) points[g.blackId] += 1;
        else if (!g.result.isEmpty()) points[g.whiteId] += 0.5, points[g.blackId] += 0.5;
    }
    std::stable_sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        return points.value(a) != points.value(b) ? points.value(a) > points.value(b) : seed.value(a) < seed.value(b);
    });
    // Odd number: bye for the lowest-ranked player who has not had one yet
    if (ids.size() % 2 == 1) {
        int k = int(ids.size()) - 1;
        while (k > 0 && byes.contains(ids[k])) --k;
        const QString bye = ids.takeAt(k);
        GameRecord r{bye, QString(), names[bye], tr("Exempt"), QStringLiteral("1-0"), tr("Exempt"), {}, {}};
        r.round = round;
        m_games << r;
    }
    // Pairings in ranking order, without rematch if possible (backtracking)
    std::vector<std::pair<QString, QString>> pairs;
    std::function<bool(QStringList, bool)> pairUp = [&](QStringList left, bool allowRematch) {
        if (left.isEmpty()) return true;
        const QString a = left.takeFirst();
        for (int k = 0; k < left.size(); ++k) {
            const QString b = left[k];
            if (!allowRematch && met.contains(a + QLatin1Char('|') + b)) continue;
            QStringList rest = left;
            rest.removeAt(k);
            pairs.push_back({a, b});
            if (pairUp(rest, allowRematch)) return true;
            pairs.pop_back();
        }
        return false;
    };
    if (!pairUp(ids, false)) {
        pairs.clear();
        pairUp(ids, true);
    }
    for (const auto& [a, b] : pairs) {
        // Colours: White to the one who had it least often (on ties, to the better ranked every other round)
        const int da = whites.value(a) - blacks.value(a), db = whites.value(b) - blacks.value(b);
        const bool aWhite = da != db ? da < db : round % 2 == 1;
        const QString w = aWhite ? a : b, k = aWhite ? b : a;
        const QString opening = nextOpening();
        GameRecord g{w, k, names[w], names[k], {}, {}, {}, opening};
        g.round = round;
        m_games << g;
        if (m_settings.doubleRound) {
            GameRecord r{k, w, names[k], names[w], {}, {}, {}, opening};
            r.round = round;
            m_games << r;
        }
    }
    return true;
}

int Tournament::playedCount() const
{
    return int(std::count_if(m_games.begin(), m_games.end(), [](const GameRecord& g) { return !g.result.isEmpty(); }));
}

void Tournament::start()
{
    if (m_running || m_games.isEmpty() || isComplete()) return;
    m_running = true;
    emit changed();
    startNextGame();
}

void Tournament::stop()
{
    if (!m_running) return;
    m_running = false;
    for (auto& g : m_slots) {
        g->clock->stop();
        shutdownEngines(*g);
        g->index = -1; // interrupted games are not counted: they will be replayed
        ++g->serial;
    }
    closeIdleEngines();
    emit message(tr("Tournoi interrompu"));
    emit changed();
}

void Tournament::shutdownEngines(Running& g, bool keep)
{
    for (int side = 0; side < 2; ++side) {
        UciEngine*& e = g.engines[size_t(side)];
        if (!e) continue;
        disconnect(e, nullptr, this, nullptr);
        if (keep && m_running && e->isRunning() && !g.engineIds[size_t(side)].isEmpty()) {
            e->stop();    // possible pondering
            e->newGame(); // transposition table cleared during the pause between two games
            m_idleEngines.append({g.engineIds[size_t(side)], e});
        } else {
            e->quit();
            e->deleteLater();
        }
        e = nullptr;
    }
    g.ready = 0;
    // No more engines waiting than engines playing (each keeps its transposition table).
    while (m_idleEngines.size() > 2 * std::max(1, m_settings.concurrency)) {
        UciEngine* old = m_idleEngines.takeFirst().second;
        old->quit();
        old->deleteLater();
    }
}

UciEngine* Tournament::takeIdleEngine(const QString& participantId)
{
    for (int i = int(m_idleEngines.size()) - 1; i >= 0; --i) {
        if (m_idleEngines[i].first != participantId) continue;
        UciEngine* e = m_idleEngines.takeAt(i).second;
        if (e->isRunning()) return e;
        e->deleteLater(); // stopped in the meantime
    }
    return nullptr;
}

void Tournament::closeIdleEngines()
{
    for (const auto& [id, e] : std::as_const(m_idleEngines)) {
        e->quit();
        e->deleteLater();
    }
    m_idleEngines.clear();
}

QList<int> Tournament::runningGames() const
{
    QList<int> out;
    for (const auto& g : m_slots)
        if (g->index >= 0) out << g->index;
    std::sort(out.begin(), out.end());
    return out;
}

const chess::Game* Tournament::runningGame(int index) const
{
    for (const auto& g : m_slots)
        if (g->index == index && index >= 0) return &g->live;
    return nullptr;
}

const ChessClock* Tournament::runningClock(int index) const
{
    for (const auto& g : m_slots)
        if (g->index == index && index >= 0) return g->clock;
    return nullptr;
}

bool Tournament::showGame(int index)
{
    for (auto& g : m_slots) {
        if (g->index != index || index < 0) continue;
        if (m_shown != g.get()) {
            m_shown = g.get();
            emit gameStarted(index);
            emit changed();
            emit clockTick();
        }
        return true;
    }
    return false;
}

int Tournament::runningCount() const
{
    return int(std::count_if(m_slots.begin(), m_slots.end(), [](const auto& g) { return g->index >= 0; }));
}

// Fills the free slots with the next games to play (only one at a time without
// simultaneous games). The displayed game is that of `m_shown`; when it ends,
// the display switches to the game that starts, otherwise to another game in progress.
void Tournament::startNextGame()
{
    if (!m_running) return;
    const size_t wanted = size_t(std::clamp(m_settings.concurrency, 1, 64));
    while (m_slots.size() < wanted) {
        auto g = std::make_unique<Running>();
        g->clock = new ChessClock(this);
        Running* raw = g.get();
        connect(g->clock, &ChessClock::tick, this, [this, raw] {
            if (raw == m_shown) emit clockTick();
        });
        connect(g->clock, &ChessClock::flagFell, this, [this, raw](Color side) { onFlag(*raw, side); });
        // The flag does not fall if the engine has already replied without the interface having seen it yet.
        g->clock->setFlagGuard([raw] {
            const UciEngine* e = raw->engines[size_t(raw->clock->runningSide())];
            return e && e->hasPendingAnswer();
        });
        m_slots.push_back(std::move(g));
    }
    auto nextGame = [&] {
        for (int i = 0; i < m_games.size(); ++i) {
            if (!m_games[i].result.isEmpty()) continue;
            if (std::any_of(m_slots.begin(), m_slots.end(), [i](const auto& g) { return g->index == i; })) continue;
            return i;
        }
        return -1;
    };
    bool endedAtOnce = false;
    for (size_t k = 0; k < wanted && m_running; ++k) {
        Running& g = *m_slots[k];
        if (g.index >= 0) continue;
        int index = nextGame();
        // Swiss system: the next round is only paired once the current round is over.
        if (index < 0 && runningCount() == 0 && m_settings.schedule == Settings::Schedule::Swiss && pairSwissRound()) {
            emit message(tr("Ronde %1 appariée").arg(m_swissRound));
            emit changed();
            index = nextGame();
        }
        if (index < 0) break;
        startGame(g, index);
        endedAtOnce |= g.index < 0;
    }
    if (!m_running) return;
    // A game that ended as soon as it started (engine unavailable): finishGame() has already scheduled
    // the next call — nothing running does not mean the tournament is over, other games may remain.
    if (endedAtOnce && runningCount() == 0) return;
    if (runningCount() == 0) { // finished
        m_running = false;
        emit message(tr("Tournoi terminé"));
        emit changed();
        return;
    }
    if (!m_shown || m_shown->index < 0) // the displayed game is over: another game in progress is followed
        for (auto& g : m_slots)
            if (g->index >= 0) {
                m_shown = g.get();
                emit gameStarted(g->index);
                emit changed();
                break;
            }
}

void Tournament::startGame(Running& g, int index)
{
    g.index = index;
    ++g.serial;
    GameRecord& rec = m_games[index];
    const Participant* players[2] = {participant(rec.whiteId), participant(rec.blackId)};
    const EngineConfig cfg[2] = {players[0] ? players[0]->config() : EngineConfig(), players[1] ? players[1]->config() : EngineConfig()};
    if (cfg[0].isValid()) rec.whiteName = cfg[0].name;
    if (cfg[1].isValid()) rec.blackName = cfg[1].name;

    g.live = Game();
    g.played = 0;
    if (!rec.opening.isEmpty() && !buildOpening(rec.opening, g.live)) {
        g.live = Game(); // unreadable position: we play from the initial position
        emit message(tr("Position de départ invalide, partie jouée depuis la position initiale"));
    }
    if (!rec.opening.isEmpty()) {
        // Marker in the notation and in the PGN: where the imposed moves stop.
        Node* last = g.live.root();
        while (last->mainChild()) last = last->mainChild();
        last->comment = last == g.live.root() ? tr("Position de départ de la base (sans coups) : fin des coups de la base").toStdString()
                                              : tr("Fin des coups de la base").toStdString();
    }
    g.live.setHeader("Event", tr("Tournoi de moteurs").toStdString());
    g.live.setHeader("Site", "Hydra64");
    g.live.setHeader("Date", QDate::currentDate().toString(QStringLiteral("yyyy.MM.dd")).toStdString());
    g.live.setHeader("Round", std::to_string(index + 1));
    g.live.setHeader("White", rec.whiteName.toStdString());
    g.live.setHeader("Black", rec.blackName.toStdString());
    g.live.setHeader("Result", "*");
    if (g.live.startPosition().fen() != Position::startPos().fen()) {
        g.live.setHeader("SetUp", "1");
        g.live.setHeader("FEN", g.live.startPosition().fen());
    }
    if (m_settings.limit == Settings::Limit::Clock)
        g.live.setHeader("TimeControl", std::to_string(m_settings.baseMs / 1000) + "+" + std::to_string(m_settings.incMs / 1000));
    else if (m_settings.limit == Settings::Limit::MoveTime)
        g.live.setHeader("TimeControl", "1/" + std::to_string(std::max(1, m_settings.moveTimeMs / 1000)));
    else
        g.live.setHeader("TimeControl", "-");
    g.evalHistory.clear();
    g.lastLine = {};
    g.ponderKey = {};
    g.clock->setup(m_settings.baseMs, m_settings.incMs, m_settings.baseMs, m_settings.incMs);
    if (!m_shown || m_shown->index < 0 || m_shown == &g) { // nothing displayed (or the displayed game has just ended)
        m_shown = &g;
        emit gameStarted(index);
    }
    emit changed();

    shutdownEngines(g);
    g.ready = 0;
    {
        Node* last = g.live.root();
        while (last->mainChild()) last = last->mainChild();
        GameEnd end;
        if (detectGameEnd(last, end)) { // starting position already finished
            finishGame(g, end.result, display::endReason(end.kind));
            return;
        }
    }
    Running* slot = &g;
    int reused = 0;
    for (int side = 0; side < 2; ++side) {
        if (!cfg[side].isValid()) {
            finishGame(g, side == 0 ? "0-1" : "1-0", tr("Moteur indisponible"), true);
            return;
        }
        g.engineIds[size_t(side)] = side == 0 ? rec.whiteId : rec.blackId;
        UciEngine* e = takeIdleEngine(g.engineIds[size_t(side)]); // engine kept from a previous game
        const bool fresh = e == nullptr;
        if (fresh) e = new UciEngine(this);
        else ++reused, ++m_enginesReused;
        g.engines[side] = e;
        connect(e, &UciEngine::started, this, [this, slot] {
            if (++slot->ready == 2) requestMove(*slot); // the clock starts when both engines are ready
        });
        connect(e, &UciEngine::bestMoveReceived, this, [this, slot, side](const QString& uci, const QString& key) { onBestMove(*slot, side, uci, key); });
        connect(e, &UciEngine::infoReceived, this, [slot, side](const EngineLine& l) {
            if (l.multipv == 1) slot->lastLine[side] = l;
        });
        connect(e, &UciEngine::terminated, this, [this, slot, side] {
            if (m_running && slot->index >= 0) finishGame(*slot, side == 0 ? "0-1" : "1-0", tr("Le moteur s'est arrêté"), true);
        });
        if (!fresh) continue; // already set, "ucinewgame" already sent
        e->setTablebaseRole(UciEngine::TablebaseRole::Tournament);
        if (!e->start(cfg[side].path)) {
            finishGame(g, side == 0 ? "0-1" : "1-0", tr("Impossible de lancer le moteur"), true);
            return;
        }
        if (cfg[side].id.isEmpty()) e->setOption(QStringLiteral("Threads"), QString::number(std::max(1, QThread::idealThreadCount() / 2)));
        for (auto it = cfg[side].values.cbegin(); it != cfg[side].values.cend(); ++it)
            if (it.key().compare(QLatin1String("MultiPV"), Qt::CaseInsensitive) != 0) e->setOption(it.key(), it.value());
        if (cfg[side].option(QStringLiteral("Ponder"))) e->setOption(QStringLiteral("Ponder"), m_settings.ponder ? QStringLiteral("true") : QStringLiteral("false"));
        e->newGame();
    }
    if (reused) { // the reused engines are already ready
        const quint64 serial = g.serial;
        QTimer::singleShot(0, this, [this, slot, serial, reused] {
            if (slot->serial != serial || slot->index < 0) return;
            const int before = slot->ready;
            slot->ready += reused;
            if (before < 2 && slot->ready >= 2) requestMove(*slot);
        });
    }
}

QString Tournament::positionKey(const Running& g) const
{
    // Starting position and moves played: the engine knows the history (repetitions).
    QString key = QString::fromStdString(g.live.root()->pos.fen());
    QStringList moves;
    for (const Node* n : g.live.mainline()) moves << QString::fromStdString(Position::toUci(n->move));
    if (!moves.isEmpty()) key += QStringLiteral(" moves ") + moves.join(' ');
    return key;
}

void Tournament::requestMove(Running& g)
{
    if (!m_running || g.index < 0) return;
    const Node* last = g.live.root();
    while (last->mainChild()) last = last->mainChild();
    const Color side = last->pos.sideToMove();
    UciEngine* e = g.engines[side];
    if (!e) return;
    g.expectedKey = positionKey(g);
    g.lastLine[side] = {};
    g.thinkTimer.start();
    if (m_settings.limit == Settings::Limit::Clock && !g.clock->isRunning()) g.clock->start(side);
    Running* slot = &g;
    const quint64 serial = g.serial;
    // Engine book: move drawn from the book as long as the position is in it
    if ((m_bookTree || m_bookPolyglot) && last->pos.fullmoveNumber() <= m_settings.bookMaxMoves) {
        const Move bm = pickBookMove(last->pos);
        if (!bm.isNull()) {
            if (!g.ponderKey[side].isEmpty()) { // pondering that became pointless
                g.ponderKey[side].clear();
                e->stop();
            }
            const QString key = g.expectedKey, uci = QString::fromStdString(Position::toUci(bm));
            QTimer::singleShot(0, this, [this, slot, serial, side, key, uci] {
                if (slot->serial != serial) return;
                slot->bookMove = true;
                onBestMove(*slot, int(side), uci, key);
                slot->bookMove = false;
            });
            return;
        }
    }
    // Fixed time per move: a frozen engine does not block the tournament (with a clock, the flag falls).
    if (m_settings.limit == Settings::Limit::MoveTime) {
        const QString key = g.expectedKey;
        QTimer::singleShot(std::max(10, m_settings.moveTimeMs) + 10000, this, [this, slot, serial, key, side] {
            if (m_running && slot->serial == serial && slot->index >= 0 && slot->expectedKey == key)
                finishGame(*slot, side == White ? "0-1" : "1-0", tr("Le moteur ne répond plus"), true);
        });
    }
    // Pondering: the expected move was played → the search in progress goes on
    if (!g.ponderKey[side].isEmpty()) {
        const bool hit = g.ponderKey[side] == g.expectedKey;
        g.ponderKey[side].clear();
        if (hit) {
            ++m_ponderHits;
            e->ponderHit();
            return;
        }
    }
    e->search(g.expectedKey, goArgs(g)); // otherwise the pondering is stopped and a search starts
}

QString Tournament::goArgs(const Running& g) const
{
    if (m_settings.limit == Settings::Limit::MoveTime) return QStringLiteral("movetime %1").arg(std::max(10, m_settings.moveTimeMs));
    if (m_settings.limit == Settings::Limit::Depth) return QStringLiteral("depth %1").arg(std::max(1, m_settings.depth));
    return QStringLiteral("wtime %1 btime %2 winc %3 binc %4")
        .arg(g.clock->remaining(White))
        .arg(g.clock->remaining(Black))
        .arg(g.clock->increment(White))
        .arg(g.clock->increment(Black));
}

void Tournament::onBestMove(Running& g, int side, const QString& uci, const QString& key)
{
    if (!m_running || g.index < 0 || key != g.expectedKey) return;
    Node* last = g.live.root();
    while (last->mainChild()) last = last->mainChild();
    if (last->pos.sideToMove() != Color(side)) return;
    const Move m = last->pos.parseUci(uci.toStdString());
    if (m.isNull() || !last->pos.isLegal(m)) {
        finishGame(g, side == 0 ? "0-1" : "1-0", tr("Coup illégal (%1)").arg(uci), true);
        return;
    }
    const bool clocked = m_settings.limit == Settings::Limit::Clock;
    // Move time: measured by the engines' thread (from "go" to the reading of "bestmove"),
    // hence independent of the interface's load; a book move costs nothing.
    const qint64 usedMs = g.bookMove || !g.engines[size_t(side)] ? 0 : g.engines[size_t(side)]->lastThinkMs();
    if (clocked && !g.clock->moveMade(Color(side), usedMs)) {
        onFlag(g, Color(side));
        return;
    }
    Node* n = g.live.addMove(last, m);
    ++g.played;
    if (clocked) n->clockMs = int(g.clock->remaining(Color(side)));
    // Evaluation of the engine that played (White's point of view): comment and adjudication
    const EngineLine& line = g.lastLine[side];
    int white = std::numeric_limits<int>::min();
    if (line.depth > 0) {
        const int sign = side == 0 ? 1 : -1;
        white = sign * (line.mate ? (line.score > 0 ? 100000 : -100000) : line.score);
        if (m_settings.engineComments) {
            QString c = line.mate ? (sign * line.score > 0 ? QStringLiteral("#%1").arg(std::abs(line.score)) : QStringLiteral("#-%1").arg(std::abs(line.score)))
                                  : QString::asprintf("%+.2f", sign * line.score / 100.0);
            c += QStringLiteral("/%1 %2s").arg(line.depth).arg(QString::number(usedMs / 1000.0, 'f', 1));
            n->comment = c.toStdString();
        }
    }
    if (g.bookMove) {
        n->comment = tr("livre").toStdString();
        ++m_bookMovesPlayed;
    }
    g.evalHistory.push_back(white);
    g.live.setCurrent(n);
    if (&g == m_shown) emit moveMade(m, n->clockMs);
    emit boardChanged(g.index);

    GameEnd end;
    if (detectGameEnd(n, end)) {
        finishGame(g, end.result, display::endReason(end.kind));
        return;
    }
    if (int(g.live.mainline().size()) >= MaxPlies) {
        finishGame(g, "1/2-1/2", tr("Limite de %1 demi-coups").arg(MaxPlies));
        return;
    }
    if (adjudicate(g)) return;
    // Pondering of the engine that just moved, on the reply it expects
    if (m_settings.ponder && g.engines[side]) {
        const Move expected = n->pos.parseUci(g.engines[side]->ponderMove().toStdString());
        if (!expected.isNull() && n->pos.isLegal(expected) && !n->pos.after(expected).legalMoves().empty()) {
            const QString key2 = positionKey(g);
            g.ponderKey[side] = key2 + (key2.contains(QLatin1String(" moves ")) ? QStringLiteral(" ") : QStringLiteral(" moves ")) +
                                QString::fromStdString(Position::toUci(expected));
            g.engines[side]->ponder(g.ponderKey[side], goArgs(g));
        }
    }
    requestMove(g);
}

Move Tournament::pickBookMove(const Position& pos) const
{
    std::vector<std::pair<Move, double>> choices;
    if (m_bookPolyglot)
        for (const auto& bm : m_bookPolyglot->moves(pos))
            if (bm.weight > 0) choices.push_back({bm.move, double(bm.weight)});
    if (m_bookTree)
        for (const auto& bm : m_bookTree->moves(pos)) {
            if (bm.recommendation == 64 || bm.annotation == 2 || bm.annotation == 4) continue; // to avoid, ?, ??
            choices.push_back({bm.move, double(std::max<uint64_t>(1, bm.games()))});
        }
    double total = 0;
    for (const auto& c : choices) total += c.second;
    if (total <= 0) return {};
    double r = QRandomGenerator::global()->bounded(total);
    for (const auto& c : choices) {
        if (r < c.second && pos.isLegal(c.first)) return c.first;
        r -= c.second;
    }
    return pos.isLegal(choices.back().first) ? choices.back().first : Move{};
}

bool Tournament::tablebaseVerdict(const chess::Position& pos, QString& result, QString& reason)
{
    int wdl = 0;
    if (!chess::tb::result(pos, wdl)) return false;
    const int forWhite = pos.sideToMove() == chess::White ? wdl : -wdl;
    if (forWhite == 2) result = QStringLiteral("1-0"), reason = tr("Arbitrage : tables de finales (gain des Blancs)");
    else if (forWhite == -2) result = QStringLiteral("0-1"), reason = tr("Arbitrage : tables de finales (gain des Noirs)");
    else if (forWhite == 0) result = QStringLiteral("1/2-1/2"), reason = tr("Arbitrage : tables de finales (nulle)");
    else result = QStringLiteral("1/2-1/2"), reason = tr("Arbitrage : tables de finales (nulle par la règle des 50 coups)");
    return true;
}

bool Tournament::adjudicate(Running& g)
{
    const Settings& s = m_settings;
    if (s.tablebaseAdjudication) {
        const Node* last = g.live.root();
        while (last->mainChild()) last = last->mainChild();
        QString result, reason;
        if (tablebaseVerdict(last->pos, result, reason)) {
            finishGame(g, result.toStdString(), reason);
            return true;
        }
    }
    const auto& h = g.evalHistory;
    auto lastAll = [&](int plies, auto pred) {
        if (plies <= 0 || int(h.size()) < plies) return false;
        for (size_t i = h.size() - size_t(plies); i < h.size(); ++i)
            if (h[i] == std::numeric_limits<int>::min() || !pred(h[i])) return false;
        return true;
    };
    if (s.resignAdjudication) {
        const int plies = 2 * std::max(1, s.resignMoves);
        if (lastAll(plies, [&](int v) { return v >= s.resignScoreCp; })) {
            finishGame(g, "1-0", tr("Arbitrage : abandon des Noirs (évaluation ≥ %1 pendant %n coup(s))", nullptr, s.resignMoves).arg(s.resignScoreCp / 100.0, 0, 'f', 2));
            return true;
        }
        if (lastAll(plies, [&](int v) { return v <= -s.resignScoreCp; })) {
            finishGame(g, "0-1", tr("Arbitrage : abandon des Blancs (évaluation ≥ %1 pendant %n coup(s))", nullptr, s.resignMoves).arg(s.resignScoreCp / 100.0, 0, 'f', 2));
            return true;
        }
    }
    if (s.drawAdjudication) {
        const Node* last = g.live.root();
        while (last->mainChild()) last = last->mainChild();
        if (last->pos.fullmoveNumber() >= s.drawFromMove && lastAll(2 * std::max(1, s.drawMoves), [&](int v) { return std::abs(v) <= s.drawScoreCp; })) {
            finishGame(g, "1/2-1/2", tr("Arbitrage : nulle (évaluation proche de 0 pendant %n coup(s))", nullptr, s.drawMoves));
            return true;
        }
    }
    return false;
}

bool Tournament::eloStats(int wins, int draws, int losses, double* elo, double* margin, double* los)
{
    const int n = wins + draws + losses;
    if (n <= 0) return false;
    const double mu = (wins + 0.5 * draws) / n;
    if (los) *los = wins + losses > 0 ? 0.5 * (1 + std::erf((wins - losses) / std::sqrt(2.0 * (wins + losses)))) : 0.5;
    if (mu <= 0 || mu >= 1) return false;
    auto diff = [](double p) { return -400.0 * std::log10(1.0 / p - 1.0); };
    const double var = (wins * (1 - mu) * (1 - mu) + draws * (0.5 - mu) * (0.5 - mu) + losses * mu * mu) / n;
    const double dev = std::sqrt(var / n);
    const double lo = std::clamp(mu - 1.959964 * dev, 1e-6, 1 - 1e-6), hi = std::clamp(mu + 1.959964 * dev, 1e-6, 1 - 1e-6);
    if (elo) *elo = diff(mu);
    if (margin) *margin = (diff(hi) - diff(lo)) / 2;
    return true;
}

int Tournament::gameCount(int n, Settings::Schedule schedule, bool doubleRound, int cycles)
{
    if (schedule == Settings::Schedule::Swiss) return std::max(1, cycles) * (n / 2) * (doubleRound ? 2 : 1);
    const int pairs = schedule == Settings::Schedule::Gauntlet ? std::max(0, n - 1) : n * (n - 1) / 2;
    return pairs * (doubleRound ? 2 : 1) * std::max(1, cycles);
}

void Tournament::onFlag(Running& g, Color side)
{
    if (!m_running || g.index < 0) return;
    const Node* last = g.live.root();
    while (last->mainChild()) last = last->mainChild();
    int material = 0;
    bool heavy = false;
    for (int s = 0; s < 64; ++s) {
        const Piece p = last->pos.at(Square(s));
        if (p == NoPiece || colorOf(p) == side || typeOf(p) == King) continue;
        ++material;
        if (typeOf(p) == Pawn || typeOf(p) == Rook || typeOf(p) == Queen) heavy = true;
    }
    if (!heavy && material <= 1) finishGame(g, "1/2-1/2", tr("Temps écoulé — matériel insuffisant pour mater"), true);
    else finishGame(g, side == White ? "0-1" : "1-0", tr("Temps écoulé"), true);
}

void Tournament::finishGame(Running& g, const std::string& result, const QString& reason, bool engineFault)
{
    if (g.index < 0) return;
    g.clock->stop();
    const int index = g.index;
    g.index = -1;
    const quint64 serial = ++g.serial;
    // Deferred stop: we may be called from a signal of one of the engines.
    Running* slot = &g;
    QTimer::singleShot(0, this, [this, slot, serial, engineFault] {
        if (slot->serial == serial && slot->index < 0) shutdownEngines(*slot, !engineFault);
        if (!m_running) closeIdleEngines(); // tournament finished
    });
    g.live.setHeader("Result", result);
    g.live.setHeader("Termination", reason.toStdString());
    GameRecord& rec = m_games[index];
    rec.result = QString::fromStdString(result);
    rec.reason = reason;
    rec.pgn = QString::fromStdString(pgn::writeGame(g.live));
    rec.noMoves = g.played == 0; // imposed opening moves alone are not a game
    if (!m_settings.pgnFile.isEmpty() && !rec.noMoves) { // a game without a move is not saved
        // Added immediately: the database is up to date even if the application stops in mid-tournament.
        QString saveError;
        const bool saved = m_gameSaver ? m_gameSaver(m_settings.pgnFile, g.live, &saveError) : appendToDatabase(m_settings.pgnFile, g.live, &saveError);
        if (saved) emit pgnSaved(m_settings.pgnFile);
        else emit message(tr("Partie non enregistrée dans %1 : %2").arg(QFileInfo(m_settings.pgnFile).fileName(), saveError));
    }
    if (m_running && isComplete()) { // last game: the tournament is over
        m_running = false;
        emit message(tr("Tournoi terminé"));
    }
    m_finishedShown = &g == m_shown;
    emit boardChanged(index);
    emit gameFinished(index);
    emit changed();
    if (m_running) QTimer::singleShot(m_gamePauseMs, this, &Tournament::startNextGame);
}

QList<Tournament::Standing> Tournament::standings() const
{
    QList<Standing> table;
    for (const Participant& p : m_settings.participants) {
        Standing s;
        s.id = p.id;
        s.name = p.name;
        table << s;
    }
    auto find = [&](const QString& id, const QString& name) -> Standing& {
        for (Standing& s : table)
            if (s.id == id) {
                if (s.name.isEmpty()) s.name = name;
                return s;
            }
        table << Standing{id, name};
        return table.last();
    };
    for (const GameRecord& g : m_games) {
        if (g.result.isEmpty()) continue;
        if (g.blackId.isEmpty()) { // bye (Swiss system): one point, no game
            find(g.whiteId, g.whiteName).points += 1;
            continue;
        }
        Standing& w = find(g.whiteId, g.whiteName);
        Standing& b = find(g.blackId, g.blackName);
        ++w.played;
        ++b.played;
        if (g.result == QLatin1String("1-0")) {
            w.points += 1;
            ++w.wins;
            ++b.losses;
        } else if (g.result == QLatin1String("0-1")) {
            b.points += 1;
            ++b.wins;
            ++w.losses;
        } else {
            w.points += 0.5;
            b.points += 0.5;
            ++w.draws;
            ++b.draws;
        }
    }
    for (Standing& s : table) s.hasElo = eloStats(s.wins, s.draws, s.losses, &s.elo, &s.margin, &s.los);
    std::stable_sort(table.begin(), table.end(), [](const Standing& a, const Standing& b) {
        if (a.points != b.points) return a.points > b.points;
        return a.wins > b.wins;
    });
    return table;
}

QString Tournament::exportPgn() const
{
    QStringList parts;
    for (const GameRecord& g : m_games)
        if (!g.pgn.isEmpty() && !g.noMoves) parts << g.pgn.trimmed();
    return parts.join(QStringLiteral("\n\n")) + (parts.isEmpty() ? QString() : QStringLiteral("\n"));
}

QJsonObject Tournament::saveState() const
{
    if (m_games.isEmpty()) return {};
    QJsonArray games;
    for (const GameRecord& g : m_games)
        games.append(QJsonObject{{"white", g.whiteId}, {"black", g.blackId}, {"whiteName", g.whiteName}, {"blackName", g.blackName},
                                 {"result", g.result}, {"reason", g.reason}, {"pgn", g.pgn}, {"opening", g.opening}, {"round", g.round}, {"noMoves", g.noMoves}});
    QJsonArray participants;
    for (const Participant& p : m_settings.participants) participants.append(p.toJson());
    return QJsonObject{{"participants", participants},
                       {"cycles", m_settings.cycles},
                       {"doubleRound", m_settings.doubleRound},
                       {"baseMs", m_settings.baseMs},
                       {"incMs", m_settings.incMs},
                       {"openingsFile", m_settings.openingsFile},
                       {"openingsRandom", m_settings.openingsRandom},
                       {"pgnFile", m_settings.pgnFile},
                       {"schedule", int(m_settings.schedule)},
                       {"limit", int(m_settings.limit)},
                       {"moveTimeMs", m_settings.moveTimeMs},
                       {"depth", m_settings.depth},
                       {"engineComments", m_settings.engineComments},
                       {"ponder", m_settings.ponder},
                       {"concurrency", m_settings.concurrency},
                       {"bookFile", m_settings.bookFile},
                       {"bookMaxMoves", m_settings.bookMaxMoves},
                       {"swissRound", m_swissRound},
                       {"pairing", m_pairing},
                       {"drawAdjudication", m_settings.drawAdjudication},
                       {"drawFromMove", m_settings.drawFromMove},
                       {"drawMoves", m_settings.drawMoves},
                       {"drawScoreCp", m_settings.drawScoreCp},
                       {"resignAdjudication", m_settings.resignAdjudication},
                       {"resignMoves", m_settings.resignMoves},
                       {"resignScoreCp", m_settings.resignScoreCp},
                       {"games", games}};
}

void Tournament::restoreState(const QJsonObject& o)
{
    if (o.isEmpty()) return;
    stop();
    m_settings = {};
    for (const QJsonValue& v : o.value("participants").toArray()) m_settings.participants << Participant::fromJson(v.toObject());
    // Old format: a list of engines, with the settings of the engine manager.
    for (const QJsonValue& v : o.value("engines").toArray()) {
        const QString id = v.toString();
        Participant p{id, id, EngineRegistry::instance().engine(id).name, EngineRegistry::instance().engine(id).values};
        m_settings.participants << p;
    }
    m_settings.cycles = std::max(1, o.value("cycles").toInt(1));
    m_settings.doubleRound = o.value("doubleRound").toBool(true);
    m_settings.baseMs = o.value("baseMs").toInteger(60000);
    m_settings.incMs = o.value("incMs").toInteger(1000);
    m_settings.openingsFile = o.value("openingsFile").toString();
    m_settings.openingsRandom = o.value("openingsRandom").toBool();
    m_settings.pgnFile = o.value("pgnFile").toString();
    m_settings.schedule = Settings::Schedule(std::clamp(o.value("schedule").toInt(), 0, 2));
    m_swissRound = o.value("swissRound").toInt();
    m_pairing = o.value("pairing").toInt();
    m_openings.clear();
    m_settings.limit = Settings::Limit(std::clamp(o.value("limit").toInt(), 0, 2));
    m_settings.moveTimeMs = o.value("moveTimeMs").toInt(1000);
    m_settings.depth = o.value("depth").toInt(12);
    m_settings.engineComments = o.value("engineComments").toBool(true);
    m_settings.ponder = o.value("ponder").toBool();
    m_settings.concurrency = std::clamp(o.value("concurrency").toInt(1), 1, 64);
    m_settings.bookFile = o.value("bookFile").toString();
    m_settings.bookMaxMoves = o.value("bookMaxMoves").toInt(20);
    if (!m_settings.bookFile.isEmpty()) {
        const std::string path = QFile::encodeName(m_settings.bookFile).toStdString();
        if (m_settings.bookFile.endsWith(QLatin1String(".bin"), Qt::CaseInsensitive)) {
            m_bookPolyglot = std::make_shared<polyglot::Book>();
            if (!m_bookPolyglot->open(path)) m_bookPolyglot.reset();
        } else {
            m_bookTree = std::make_shared<treebook::Book>();
            if (!m_bookTree->open(path)) m_bookTree.reset();
        }
    }
    m_settings.drawAdjudication = o.value("drawAdjudication").toBool();
    m_settings.drawFromMove = o.value("drawFromMove").toInt(40);
    m_settings.drawMoves = o.value("drawMoves").toInt(8);
    m_settings.drawScoreCp = o.value("drawScoreCp").toInt(10);
    m_settings.resignAdjudication = o.value("resignAdjudication").toBool();
    m_settings.resignMoves = o.value("resignMoves").toInt(4);
    m_settings.resignScoreCp = o.value("resignScoreCp").toInt(700);
    m_games.clear();
    for (const QJsonValue& v : o.value("games").toArray()) {
        const QJsonObject g = v.toObject();
        GameRecord r{g.value("white").toString(),  g.value("black").toString(),  g.value("whiteName").toString(),
                     g.value("blackName").toString(), g.value("result").toString(), g.value("reason").toString(),
                     g.value("pgn").toString(), g.value("opening").toString()};
        r.round = g.value("round").toInt();
        r.noMoves = g.value("noMoves").toBool();
        m_games << r;
    }
    emit changed();
}
