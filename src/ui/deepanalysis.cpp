#include "deepanalysis.h"
#include "userdirs.h"

#include "chess/pgn.h"
#include "chess/polyglot.h"
#include "engine/engineregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <fstream>
#include <unordered_set>

using namespace chess;

namespace {
QString statusName(DeepAnalysis::Status s)
{
    switch (s) {
    case DeepAnalysis::Status::Inactive: return QStringLiteral("inactive");
    case DeepAnalysis::Status::Share: return QStringLiteral("share");
    default: return QStringLiteral("active");
    }
}

// Readable evaluation from White's point of view: "+0.35", "#3"
QString whiteScore(int cp, Color mover)
{
    if (cp == deep::Unknown) return QStringLiteral("?");
    const int v = mover == White ? cp : -cp;
    if (std::abs(v) > deep::MateValue / 2) {
        const int plies = deep::MateValue - std::abs(v);
        const int moves = (plies + 1) / 2;
        return v > 0 ? QStringLiteral("#%1").arg(moves) : QStringLiteral("#-%1").arg(moves);
    }
    return QString::asprintf("%+.2f", v / 100.0);
}
} // namespace

QString DeepAnalysis::directory()
{
    return userdirs::analyses();
}

DeepAnalysis::DeepAnalysis(QObject* parent)
    : QObject(parent)
    , m_limits(new QTimer(this))
    , m_autosave(new QTimer(this))
{
    m_clock.start();
    m_limits->setInterval(50);
    connect(m_limits, &QTimer::timeout, this, &DeepAnalysis::checkLimits);
    m_autosave->setInterval(60000);
    connect(m_autosave, &QTimer::timeout, this, [this] {
        for (int i = 0; i < count(); ++i)
            if (info(i).dirty) saveProject(i);
    });
    m_autosave->start();
}

DeepAnalysis::~DeepAnalysis()
{
    stop();
    saveAll();
}

// ---------------------------------------------------------------- projects

void DeepAnalysis::readMeta(Entry& e)
{
    const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(e.project->meta)).object();
    ProjectInfo& in = e.info;
    if (o.contains("name")) in.name = o.value("name").toString();
    const QString st = o.value("status").toString();
    in.status = st == QLatin1String("inactive") ? Status::Inactive : st == QLatin1String("share") ? Status::Share : Status::Active;
    in.share = std::clamp(o.value("share").toInt(100), 1, 100);
    in.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
    in.modified = QDateTime::fromString(o.value("modified").toString(), Qt::ISODate);
    in.master = o.value("master").toString();
    in.totalTasks = qint64(o.value("tasks").toDouble());
    in.totalCpuMs = qint64(o.value("cpuMs").toDouble());
}

void DeepAnalysis::writeMeta(Entry& e)
{
    const ProjectInfo& in = e.info;
    QJsonObject o;
    o["name"] = in.name;
    o["status"] = statusName(in.status);
    o["share"] = in.share;
    o["created"] = in.created.toString(Qt::ISODate);
    o["modified"] = in.modified.toString(Qt::ISODate);
    o["master"] = in.master;
    o["tasks"] = double(in.totalTasks);
    o["cpuMs"] = double(in.totalCpuMs);
    o["default"] = &e == (m_default >= 0 && m_default < count() ? &m_projects[size_t(m_default)] : nullptr);
    e.project->meta = QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
}

void DeepAnalysis::loadAll()
{
    m_projects.clear();
    m_default = -1;
    const QDir dir(directory());
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.hy64deep")}, QDir::Files, QDir::Name)) {
        Entry e;
        e.project = std::make_unique<deep::Project>();
        std::string err;
        if (!e.project->load(QFile::encodeName(fi.absoluteFilePath()).toStdString(), &err)) continue;
        e.info.path = fi.absoluteFilePath();
        e.info.name = fi.completeBaseName();
        readMeta(e);
        const bool isDefault = QJsonDocument::fromJson(QByteArray::fromStdString(e.project->meta)).object().value("default").toBool();
        m_projects.push_back(std::move(e));
        if (isDefault) m_default = count() - 1;
    }
    if (m_default < 0 && count() > 0) m_default = 0;
    emit projectsChanged();
}

int DeepAnalysis::indexOf(const QString& path) const
{
    for (int i = 0; i < count(); ++i)
        if (QFileInfo(info(i).path) == QFileInfo(path)) return i;
    return -1;
}

void DeepAnalysis::setDefaultProject(int i)
{
    if (i < -1 || i >= count()) return;
    const int old = m_default;
    m_default = i;
    if (old >= 0 && old < count()) markDirty(old);
    if (i >= 0) markDirty(i);
    emit projectsChanged();
}

int DeepAnalysis::createProject(const QString& name, const Position& root, const deep::Options& options, const QString& master, QString* error)
{
    QDir().mkpath(directory());
    QString base = name.trimmed().isEmpty() ? tr("Analyse") : name.trimmed();
    base.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("_"));
    QString path = directory() + QLatin1Char('/') + base + QStringLiteral(".hy64deep");
    for (int n = 2; QFileInfo::exists(path); ++n) path = directory() + QLatin1Char('/') + base + QStringLiteral(" (%1).hy64deep").arg(n);
    Entry e;
    e.project = std::make_unique<deep::Project>();
    e.project->options = options;
    e.info.path = path;
    e.info.name = name.trimmed().isEmpty() ? base : name.trimmed();
    e.info.created = e.info.modified = QDateTime::currentDateTime();
    e.info.master = master;
    if (!master.isEmpty() && QFileInfo::exists(master)) {
        deep::Project m;
        std::string err;
        if (m.load(QFile::encodeName(master).toStdString(), &err)) e.project->tree.merge(m.tree);
    }
    e.project->tree.addRoot(root);
    e.project->tree.minimaxAll();
    m_projects.push_back(std::move(e));
    const int i = count() - 1;
    if (m_default < 0) m_default = i;
    if (!saveProject(i, error)) {
        m_projects.pop_back();
        if (m_default == i) m_default = -1;
        return -1;
    }
    emit projectsChanged();
    return i;
}

bool DeepAnalysis::deleteProject(int i, QString* error)
{
    if (i < 0 || i >= count()) return false;
    for (auto& w : m_workers) {
        if (w->project == i && w->busy) {
            w->discard = true;
            w->stopping = true;
            w->engine->moveNow();
        }
    }
    if (!QFile::remove(info(i).path) && QFileInfo::exists(info(i).path)) {
        if (error) *error = tr("Impossible de supprimer « %1 »").arg(info(i).path);
        return false;
    }
    m_projects.erase(m_projects.begin() + i);
    for (auto& w : m_workers) {
        if (w->project == i) w->project = -1;
        else if (w->project > i) --w->project;
    }
    if (m_default == i) m_default = count() > 0 ? 0 : -1;
    else if (m_default > i) --m_default;
    emit projectsChanged();
    return true;
}

void DeepAnalysis::markDirty(int i)
{
    if (i < 0 || i >= count()) return;
    info(i).dirty = true;
    info(i).modified = QDateTime::currentDateTime();
}

bool DeepAnalysis::saveProject(int i, QString* error)
{
    if (i < 0 || i >= count()) return false;
    Entry& e = m_projects[size_t(i)];
    writeMeta(e);
    // Running tasks are kept in the file, to resume at the next launch.
    std::vector<deep::Task> runningTasks;
    for (const auto& w : m_workers)
        if (w->busy && w->project == i && !w->discard) runningTasks.push_back(w->task);
    auto& q = e.project->queue();
    const size_t before = q.size();
    q.insert(q.end(), runningTasks.begin(), runningTasks.end());
    std::string err;
    const bool ok = e.project->save(QFile::encodeName(e.info.path).toStdString(), &err);
    q.resize(before);
    if (!ok && error) *error = QString::fromStdString(err);
    if (ok) e.info.dirty = false;
    return ok;
}

void DeepAnalysis::saveAll()
{
    for (int i = 0; i < count(); ++i) saveProject(i);
}

void DeepAnalysis::setStatus(int i, Status status, int share)
{
    if (i < 0 || i >= count()) return;
    info(i).status = status;
    info(i).share = std::clamp(share, 1, 100);
    markDirty(i);
    emit projectsChanged();
    schedule();
}

bool DeepAnalysis::mergeIntoMaster(int i, QString* error)
{
    if (i < 0 || i >= count() || info(i).master.isEmpty()) {
        if (error) *error = tr("pas d'arbre maître pour ce projet");
        return false;
    }
    const QString master = info(i).master;
    deep::Project m;
    std::string err;
    if (QFileInfo::exists(master) && !m.load(QFile::encodeName(master).toStdString(), &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    m.tree.merge(project(i).tree);
    if (m.tree.roots().empty() && !project(i).tree.roots().empty()) {
        Position p;
        if (p.setFen(project(i).tree.roots().front().fen)) m.tree.addRoot(p);
    }
    if (const int open = indexOf(master); open >= 0) { // master open: updated in memory too
        project(open).tree.merge(project(i).tree);
        markDirty(open);
        emit treeChanged(open);
    }
    if (!m.save(QFile::encodeName(master).toStdString(), &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- engines

QList<DeepAnalysis::EngineSlot> DeepAnalysis::defaultEngines()
{
    EngineSlot s;
    s.engineId = EngineRegistry::instance().fallbackEngine().id;
    s.count = std::max(1, QThread::idealThreadCount() / 2);
    return {s};
}

QList<DeepAnalysis::EngineSlot> DeepAnalysis::engines() const
{
    const QJsonArray a = QJsonDocument::fromJson(QSettings().value("deep/engines").toByteArray()).array();
    QList<EngineSlot> out;
    for (const QJsonValue& v : a) {
        const QJsonObject o = v.toObject();
        EngineSlot s;
        s.engineId = o.value("engine").toString();
        s.count = std::clamp(o.value("count").toInt(1), 1, 256);
        s.enabled = o.value("enabled").toBool(true);
        s.threads = std::clamp(o.value("threads").toInt(1), 1, 512);
        s.hashMb = std::clamp(o.value("hash").toInt(64), 1, 1 << 20);
        out << s;
    }
    return out.isEmpty() ? defaultEngines() : out;
}

void DeepAnalysis::setEngines(const QList<EngineSlot>& engineSlots)
{
    QJsonArray a;
    for (const EngineSlot& s : engineSlots)
        a.append(QJsonObject{{"engine", s.engineId}, {"count", s.count}, {"enabled", s.enabled}, {"threads", s.threads}, {"hash", s.hashMb}});
    QSettings().setValue("deep/engines", QJsonDocument(a).toJson(QJsonDocument::Compact));
}

bool DeepAnalysis::start(QString* error)
{
    if (m_running) return true;
    const QList<EngineSlot> engineSlots = engines();
    for (int s = 0; s < engineSlots.size(); ++s) {
        if (!engineSlots[s].enabled) continue;
        const EngineConfig cfg = EngineRegistry::instance().engine(engineSlots[s].engineId);
        if (!cfg.isValid()) continue;
        for (int k = 0; k < engineSlots[s].count; ++k) {
            auto w = std::make_unique<Worker>();
            Worker* wp = w.get();
            wp->slot = s;
            wp->engineIndex = uint8_t(std::min(s, 255));
            wp->name = cfg.name.isEmpty() ? cfg.idName : cfg.name;
            wp->engine = new UciEngine(this);
            connect(wp->engine, &UciEngine::started, this, [this, wp] {
                wp->ready = true;
                schedule();
            });
            connect(wp->engine, &UciEngine::infoReceived, this, [this, wp](const EngineLine& l) { onInfo(*wp, l); });
            connect(wp->engine, &UciEngine::bestMoveReceived, this, [this, wp](const QString& uci, const QString& fen) { onBestMove(*wp, uci, fen); });
            connect(wp->engine, &UciEngine::terminated, this, [this, wp] {
                if (wp->busy && wp->project >= 0 && wp->project < count() && !wp->discard) project(wp->project).queue().push_back(wp->task);
                wp->busy = false;
                wp->ready = false;
                if (m_running && std::none_of(m_workers.begin(), m_workers.end(), [](const auto& x) { return x->ready || !x->engine; })) stop();
            });
            if (!wp->engine->start(cfg.path)) {
                delete wp->engine;
                wp->engine = nullptr;
                continue;
            }
            wp->engine->setOption(QStringLiteral("Threads"), QString::number(engineSlots[s].threads));
            wp->engine->setOption(QStringLiteral("Hash"), QString::number(engineSlots[s].hashMb));
            for (auto it = cfg.values.cbegin(); it != cfg.values.cend(); ++it) {
                const QString key = it.key();
                if (key.compare(QLatin1String("Threads"), Qt::CaseInsensitive) == 0 || key.compare(QLatin1String("Hash"), Qt::CaseInsensitive) == 0)
                    continue;
                wp->engine->setOption(key, it.value());
            }
            if (cfg.option(QStringLiteral("MultiPV"))) wp->engine->setOption(QStringLiteral("MultiPV"), QStringLiteral("1"));
            if (cfg.option(QStringLiteral("UCI_LimitStrength"))) wp->engine->setOption(QStringLiteral("UCI_LimitStrength"), QStringLiteral("false"));
            m_workers.push_back(std::move(w));
        }
    }
    if (m_workers.empty()) {
        if (error) *error = tr("Aucun moteur utilisable (Moteurs… de l'analyse approfondie)");
        return false;
    }
    m_running = true;
    for (int i = 0; i < count(); ++i) {
        info(i).sessionTasks = info(i).sessionCpuMs = 0;
        info(i).stageStartMs = m_clock.elapsed();
    }
    m_limits->start();
    emit runningChanged(true);
    emit projectsChanged();
    return true;
}

void DeepAnalysis::shutdownWorkers()
{
    for (auto& w : m_workers) {
        if (!w->engine) continue;
        w->engine->disconnect(this);
        w->engine->quit();
        w->engine->deleteLater();
        w->engine = nullptr;
    }
    m_workers.clear();
}

void DeepAnalysis::stop()
{
    if (!m_running && m_workers.empty()) return;
    m_running = false;
    m_limits->stop();
    // Running tasks put back in their queue (resumed at the next start)
    for (auto& w : m_workers)
        if (w->busy && !w->discard && w->project >= 0 && w->project < count()) {
            project(w->project).queue().push_back(w->task);
            markDirty(w->project);
        }
    shutdownWorkers();
    for (int i = 0; i < count(); ++i) {
        info(i).activity = Activity::Idle;
        emit queueChanged(i);
    }
    saveAll();
    emit runningChanged(false);
    emit projectsChanged();
}

QList<DeepAnalysis::Running> DeepAnalysis::running() const
{
    QList<Running> out;
    for (const auto& w : m_workers) {
        if (!w->busy) continue;
        Running r;
        r.project = w->project;
        r.task = w->task;
        r.line = w->line;
        r.elapsedMs = w->timer.elapsed();
        r.engineName = w->name;
        r.slot = w->slot;
        out << r;
    }
    return out;
}

int DeepAnalysis::runningCount(int project) const
{
    return int(std::count_if(m_workers.begin(), m_workers.end(), [&](const auto& w) { return w->busy && w->project == project; }));
}

// ---------------------------------------------------------------- execution

deep::Quality DeepAnalysis::qualityFor(const Worker& w) const
{
    deep::Quality q;
    if (w.project >= 0 && w.project < count()) q = project(w.project).options.quality;
    if (w.task.timeMs > 0 || w.task.depth > 0) { // custom task
        q.minTimeMs = w.task.timeMs;
        q.minDepth = w.task.depth;
        q.maxTimeMs = 0;
    }
    return q;
}

void DeepAnalysis::schedule()
{
    if (!m_running) return;
    for (auto& w : m_workers)
        if (w->engine && w->ready && !w->busy) assign(*w);
}

bool DeepAnalysis::assign(Worker& w)
{
    // Active projects, from the least served (computing time / share) to the most served
    std::vector<int> order;
    for (int i = 0; i < count(); ++i)
        if (info(i).status != Status::Inactive) order.push_back(i);
    auto weight = [&](int i) { return info(i).status == Status::Share ? info(i).share : 100; };
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return double(info(a).sessionCpuMs + 1) / weight(a) < double(info(b).sessionCpuMs + 1) / weight(b);
    });
    for (int p : order) {
        deep::Project& pr = project(p);
        for (int attempt = 0; attempt < 64; ++attempt) {
            auto accept = [&](const deep::Task& t) { return t.engine < 0 || t.engine == w.slot; };
            std::optional<deep::Task> t = pr.takeNext(accept);
            if (!t) {
                // Empty queue and nothing running: next step
                const bool empty = std::none_of(pr.queue().begin(), pr.queue().end(), accept);
                if (!empty || runningCount(p) > 0) break;
                info(p).activity = Activity::Generating;
                const int created = pr.generateStage();
                info(p).stageStartMs = m_clock.elapsed();
                info(p).stageDone = 0;
                markDirty(p);
                emit queueChanged(p);
                emit treeChanged(p);
                if (created == 0 && pr.queue().empty()) {
                    info(p).activity = Activity::Waiting;
                    break;
                }
                continue;
            }
            Position pos;
            if (!pr.positionOf(*t, pos)) continue; // path became illegal: task dropped
            QString go = QStringLiteral("infinite");
            if (t->kind == deep::TaskKind::Alternative) {
                const std::vector<Move> moves = pr.searchMoves(*t);
                if (moves.empty()) {
                    pr.applyResult(*t, deep::Eval{});
                    continue;
                }
                go += QStringLiteral(" searchmoves");
                for (const Move& m : moves) go += QLatin1Char(' ') + QString::fromStdString(Position::toUci(m));
            }
            if (pos.legalMoves().empty()) { // final position: the planner scores it
                pr.applyResult(*t, deep::Eval{});
                continue;
            }
            w.busy = true;
            w.discard = false;
            w.stopping = false;
            w.infinite = t->kind == deep::TaskKind::Infinite;
            w.project = p;
            w.task = std::move(*t);
            w.fen = QString::fromStdString(pos.fen());
            w.line = {};
            w.depthAtTarget = -1;
            w.timer.start();
            w.engine->search(w.fen, go);
            info(p).activity = Activity::Analysing;
            emit queueChanged(p);
            return true;
        }
    }
    return false;
}

void DeepAnalysis::checkLimits()
{
    for (auto& w : m_workers) {
        if (!w->busy || w->stopping || w->infinite || !w->engine) continue;
        const deep::Quality q = qualityFor(*w);
        const qint64 elapsed = w->timer.elapsed();
        bool stop = q.maxTimeMs > 0 && elapsed >= q.maxTimeMs;
        if (!stop && elapsed >= q.minTimeMs && w->line.depth >= q.minDepth) {
            if (!q.waitNextDepth) stop = true;
            else if (w->depthAtTarget < 0) w->depthAtTarget = w->line.depth;
            else if (w->line.depth > w->depthAtTarget) stop = true;
        }
        if (stop) {
            w->stopping = true;
            w->engine->moveNow();
        }
    }
}

void DeepAnalysis::onInfo(Worker& w, const EngineLine& line)
{
    if (!w.busy || line.multipv != 1 || line.pv.isEmpty()) return;
    w.line = line;
    emit taskProgress(w.task.id);
}

void DeepAnalysis::onBestMove(Worker& w, const QString& uci, const QString& fen)
{
    if (!w.busy || fen != w.fen) return;
    w.busy = false;
    const int p = w.project;
    const qint64 elapsed = w.timer.elapsed();
    if (!w.discard && p >= 0 && p < count()) {
        Position pos;
        pos.setFen(w.fen.toStdString());
        deep::Eval e;
        e.valid = w.line.depth > 0;
        e.mate = w.line.mate;
        e.score = w.line.score;
        e.depth = w.line.depth;
        e.timeMs = uint32_t(elapsed);
        e.engine = w.engineIndex;
        Position walk = pos;
        for (const QString& m : w.line.pv) {
            const Move mv = walk.parseUci(m.toStdString());
            if (mv.isNull() || !walk.isLegal(mv)) break;
            e.pv.push_back(mv);
            walk.makeMove(mv);
        }
        const Move best = pos.parseUci(uci.toStdString());
        if (!best.isNull() && pos.isLegal(best) && (e.pv.empty() || !(e.pv.front() == best))) e.pv = {best};
        deep::Project& pr = project(p);
        pr.applyResult(w.task, e);
        ProjectInfo& in = info(p);
        ++in.sessionTasks;
        ++in.totalTasks;
        ++in.stageDone;
        const int threads = w.slot >= 0 && w.slot < engines().size() ? engines()[w.slot].threads : 1;
        in.sessionCpuMs += elapsed * threads;
        in.totalCpuMs += elapsed * threads;
        markDirty(p);
        emit treeChanged(p);
        emit queueChanged(p);
    }
    QTimer::singleShot(0, this, &DeepAnalysis::schedule);
}

// ---------------------------------------------------------------- tasks

uint64_t DeepAnalysis::addTask(int p, deep::Task t)
{
    if (p < 0 || p >= count()) return 0;
    const uint64_t id = project(p).addTask(std::move(t));
    if (info(p).activity == Activity::Waiting) info(p).activity = Activity::Idle;
    markDirty(p);
    emit queueChanged(p);
    schedule();
    return id;
}

bool DeepAnalysis::pathTo(int p, const Game& game, const Node* node, int& root, std::vector<Move>& path)
{
    if (p < 0 || p >= count() || !node) return false;
    deep::Project& pr = project(p);
    std::vector<const Node*> chain;
    for (const Node* n = node; n; n = n->parent) chain.push_back(n);
    std::reverse(chain.begin(), chain.end()); // root of the game first
    auto rootAt = [&](const Position& pos, bool enabledOnly) {
        const uint64_t key = polyglot::key(pos);
        for (size_t r = 0; r < pr.tree.roots().size(); ++r) {
            const deep::Root& rt = pr.tree.roots()[r];
            if (rt.node != deep::NoNode && pr.tree.node(rt.node).key == key && (!enabledOnly || rt.enabled)) return int(r);
        }
        return -1;
    };
    for (bool enabledOnly : {true, false}) {
        for (size_t i = 0; i < chain.size(); ++i) {
            const int r = rootAt(chain[i]->pos, enabledOnly);
            if (r < 0) continue;
            root = r;
            path.clear();
            for (size_t k = i + 1; k < chain.size(); ++k) path.push_back(chain[k]->move);
            return true;
        }
    }
    (void)game;
    // No root on the path: disabled root at the position (serves as a starting point)
    root = pr.tree.addRoot(node->pos);
    pr.tree.roots()[size_t(root)].enabled = false;
    path.clear();
    markDirty(p);
    emit projectsChanged();
    return true;
}

void DeepAnalysis::finishTask(uint64_t id)
{
    for (auto& w : m_workers)
        if (w->busy && w->task.id == id && w->engine) {
            w->stopping = true;
            w->engine->moveNow();
        }
}

void DeepAnalysis::deleteTask(int p, uint64_t id)
{
    if (p < 0 || p >= count()) return;
    if (project(p).removeTask(id)) {
        markDirty(p);
        emit queueChanged(p);
        return;
    }
    for (auto& w : m_workers)
        if (w->busy && w->task.id == id && w->engine) {
            w->discard = true;
            w->stopping = true;
            w->engine->moveNow();
        }
}

void DeepAnalysis::makeInfinite(int p, uint64_t id)
{
    if (p < 0 || p >= count()) return;
    for (deep::Task& t : project(p).queue())
        if (t.id == id && t.kind == deep::TaskKind::Evaluate) t.kind = deep::TaskKind::Infinite;
    for (auto& w : m_workers)
        if (w->busy && w->task.id == id) w->infinite = true;
    emit queueChanged(p);
}

void DeepAnalysis::minimaxNow(int p)
{
    if (p < 0 || p >= count()) return;
    info(p).activity = Activity::Minimizing;
    project(p).tree.minimaxAll();
    info(p).activity = m_running ? Activity::Analysing : Activity::Idle;
    emit treeChanged(p);
}

void DeepAnalysis::spectatorLine(const Game& game, const Node* node, const EngineLine& line)
{
    const int p = m_default;
    if (p < 0 || p >= count() || !node || line.pv.isEmpty()) return;
    const deep::Options& o = project(p).options;
    if (!o.insertSpectators || line.timeMs < o.spectatorMinTimeMs || line.depth < o.spectatorMinDepth) return;
    const QString key = QString::number(polyglot::key(node->pos)) + QLatin1Char('/') + QString::number(line.depth);
    if (key == m_lastSpectator) return;
    m_lastSpectator = key;
    int root = 0;
    std::vector<Move> path;
    if (!pathTo(p, game, node, root, path)) return;
    Position pos = node->pos;
    const int plies = o.spectatorMinDepth == 0 ? 1 : std::min<int>(int(line.pv.size()), 12);
    for (int k = 0; k < plies; ++k) {
        const Move m = pos.parseUci(line.pv[k].toStdString());
        if (m.isNull() || !pos.isLegal(m)) break;
        path.push_back(m);
        pos.makeMove(m);
        deep::Task t;
        t.root = root;
        t.path = path;
        t.priority = -1e6 + k; // after the manual tasks, before those of the step
        addTask(p, t);
    }
}

// ---------------------------------------------------------------- exports

bool DeepAnalysis::exportPgn(int p, const QString& path, QString* error) const
{
    if (p < 0 || p >= count()) return false;
    const deep::Tree& tree = project(p).tree;
    std::string out;
    for (const deep::Root& r : tree.roots()) {
        Position start;
        if (!start.setFen(r.fen) || r.node == deep::NoNode) continue;
        Game g(start);
        g.setHeader("Event", (tr("Analyse approfondie : %1").arg(info(p).name)).toStdString());
        g.setHeader("Annotator", "Hydra64");
        if (r.fen != Position::startPos().fen()) {
            g.setHeader("SetUp", "1");
            g.setHeader("FEN", r.fen);
        }
        std::vector<uint32_t> stack;
        std::function<void(Node*, uint32_t, const Position&)> add = [&](Node* gnode, uint32_t tnode, const Position& pos) {
            if (stack.size() > 300 || std::find(stack.begin(), stack.end(), tnode) != stack.end()) return;
            stack.push_back(tnode);
            std::vector<const deep::Edge*> edges;
            for (const deep::Edge& e : tree.node(tnode).edges)
                if (e.mark != deep::Mark::Exclude) edges.push_back(&e);
            std::stable_sort(edges.begin(), edges.end(), [&](const deep::Edge* a, const deep::Edge* b) { return tree.moveScore(*a) > tree.moveScore(*b); });
            for (const deep::Edge* e : edges) {
                if (!pos.isLegal(e->move)) continue;
                Node* child = g.addMove(gnode, e->move);
                const deep::Node& tn = tree.node(e->child);
                QString c = whiteScore(tree.moveScore(*e), pos.sideToMove());
                if (tn.eval.valid) c += QStringLiteral("/%1").arg(tn.eval.depth);
                if (!tn.comment.empty()) c += QLatin1Char(' ') + QString::fromStdString(tn.comment);
                child->comment = c.toStdString();
                add(child, e->child, pos.after(e->move));
            }
            stack.pop_back();
        };
        add(g.root(), r.node, start);
        out += pgn::writeGame(g);
        out += "\n";
    }
    std::ofstream f(QFile::encodeName(path).toStdString(), std::ios::binary | std::ios::trunc);
    if (!f || !f.write(out.data(), std::streamsize(out.size()))) {
        if (error) *error = tr("écriture impossible : %1").arg(path);
        return false;
    }
    return true;
}

bool DeepAnalysis::exportBook(int p, const QString& path, treebook::TreeFormat format, QString* error) const
{
    if (p < 0 || p >= count()) return false;
    const deep::Tree& tree = project(p).tree;
    const int width = std::max(1, project(p).options.widthCp);
    // Moves of a position: evaluation, depth; best = recommended, red = to avoid.
    // Weight (file order, Polyglot): decreases with the gap from the best move.
    const treebook::MoveReader reader = [&tree, width](const Position& pos) {
        std::vector<treebook::BookMove> out;
        const uint32_t n = tree.find(pos);
        if (n == deep::NoNode) return out;
        const deep::Edge* best = tree.bestEdge(n);
        const int bestScore = best ? tree.moveScore(*best) : deep::Unknown;
        for (const deep::Edge& e : tree.node(n).edges) {
            if (!pos.isLegal(e.move)) continue;
            const int s = tree.moveScore(e);
            treebook::BookMove bm;
            bm.move = e.move;
            bm.hasStats = false;
            if (s != deep::Unknown) {
                bm.hasEval = true;
                bm.eval = int16_t(std::clamp(s, -deep::MateValue, deep::MateValue));
                bm.evalDepth = uint16_t(std::max(0, tree.node(e.child).eval.depth));
            }
            const int gap = s == deep::Unknown || bestScore == deep::Unknown ? 4 * width : bestScore - s;
            bm.gameCount = uint64_t(std::max(1, 10000 - 40 * gap / std::max(1, width / 10)));
            if (&e == best) bm.recommendation = 128;
            if (e.mark == deep::Mark::Exclude || gap > 4 * width) bm.recommendation = 64, bm.gameCount = 1;
            out.push_back(bm);
        }
        return out;
    };
    std::vector<Position> starts;
    for (const deep::Root& r : tree.roots()) {
        Position s;
        if (s.setFen(r.fen)) starts.push_back(s);
    }
    std::string err;
    if (!treebook::writeTree(reader, starts, QFile::encodeName(path).toStdString(), format, nullptr, &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    return true;
}
