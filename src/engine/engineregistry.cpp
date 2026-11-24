#include "engineregistry.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

// ---------------------------------------------------------------- UciOption

bool UciOption::parse(const QString& line, UciOption& out)
{
    const QStringList t = line.split(' ', Qt::SkipEmptyParts);
    if (t.size() < 5 || t[0] != QLatin1String("option")) return false;
    static const QStringList keys = {"name", "type", "default", "min", "max", "var"};
    out = {};
    QString key;
    QStringList acc;
    auto flush = [&] {
        const QString v = acc.join(' ');
        if (key == QLatin1String("name")) out.name = v;
        else if (key == QLatin1String("type")) out.type = v;
        else if (key == QLatin1String("default")) out.defaultValue = v == QLatin1String("<empty>") ? QString() : v;
        else if (key == QLatin1String("min")) out.min = v.toLongLong();
        else if (key == QLatin1String("max")) out.max = v.toLongLong();
        else if (key == QLatin1String("var")) out.choices << v;
        acc.clear();
    };
    for (int i = 1; i < t.size(); ++i) {
        // The name may contain spaces: only "type" ends the name.
        const bool isKey = key == QLatin1String("name") ? t[i] == QLatin1String("type") : keys.contains(t[i]);
        if (isKey) {
            if (!key.isEmpty()) flush();
            key = t[i];
        } else {
            acc << t[i];
        }
    }
    if (!key.isEmpty()) flush();
    return !out.name.isEmpty() && !out.type.isEmpty();
}

// ---------------------------------------------------------------- EngineConfig

const UciOption* EngineConfig::option(const QString& name) const
{
    for (const UciOption& o : options)
        if (o.name.compare(name, Qt::CaseInsensitive) == 0) return &o;
    return nullptr;
}

QString EngineConfig::value(const QString& name) const
{
    if (values.contains(name)) return values.value(name);
    const UciOption* o = option(name);
    return o ? o->defaultValue : QString();
}

// ---------------------------------------------------------------- EngineRegistry

EngineRegistry& EngineRegistry::instance()
{
    static EngineRegistry registry;
    return registry;
}

EngineRegistry::EngineRegistry()
{
    load();
    registerBundled();
}

void EngineRegistry::registerBundled()
{
    // Engines that were shipped on trial (Berserk, PlentyChess, Rodent IV, Shallow Blue) are no longer:
    // their entries, which pointed next to the program, leave the list.
    bool changed = false;
    for (const char* old : {"berserk", "plentychess", "rodent-iv/rodent", "shallowblue"}) {
        const QFileInfo path(besideApplication(QLatin1String(old)));
        changed |= m_engines.removeIf([&](const EngineConfig& e) { return !e.path.isEmpty() && QFileInfo(e.path) == path; }) > 0;
    }
    // The two built-in engines, Stockfish and Shuffle (built next to the executable): always in the
    // list, added back if missing, under a name that tells them from engines added by the user.
    // They cannot be removed (remove()).
    for (const char* kind : {"stockfish", "shuffle"}) {
        const QString path = bundledPath(QLatin1String(kind));
        if (!QFileInfo::exists(path)) continue;
        if (std::any_of(m_engines.begin(), m_engines.end(), [&](const EngineConfig& e) { return bundledKind(e) == QLatin1String(kind); })) continue;
        // Executable moved (program folder renamed…): the existing entry is reattached, with its settings.
        bool repaired = false;
        if (QLatin1String(kind) == QLatin1String("shuffle"))
            for (EngineConfig& e : m_engines)
                if (!repaired && e.idName.startsWith(QLatin1String("Shuffle")) && !QFileInfo::exists(e.path)) {
                    e.path = path;
                    repaired = true;
                }
        EngineConfig cfg;
        if (!repaired && create(path, cfg, nullptr)) {
            if (!cfg.name.contains(tr("intégré"))) cfg.name = tr("%1 (intégré)").arg(cfg.name);
            if (QLatin1String(kind) == QLatin1String("stockfish")) m_engines.prepend(cfg);
            else m_engines.append(cfg);
        }
        changed |= repaired || cfg.isValid();
    }
    // Shuffle registered before it became a built-in engine: same "(intégré)" mark as Stockfish
    for (EngineConfig& e : m_engines)
        if (bundledKind(e) == QLatin1String("shuffle") && !e.name.contains(tr("intégré"))) {
            e.name = tr("%1 (intégré)").arg(e.name);
            changed = true;
        }
    if (changed) save();
}

QString EngineRegistry::filePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/engines.json");
}

EngineConfig EngineRegistry::engine(const QString& id) const
{
    for (const EngineConfig& e : m_engines)
        if (e.id == id) return e;
    return {};
}

void EngineRegistry::add(const EngineConfig& cfg)
{
    m_engines.append(cfg);
    save();
    emit changed();
}

void EngineRegistry::update(const EngineConfig& cfg)
{
    for (EngineConfig& e : m_engines)
        if (e.id == cfg.id) {
            const QString builtinPath = isBuiltin(e) ? e.path : QString();
            e = cfg;
            if (!builtinPath.isEmpty()) e.path = builtinPath; // a built-in engine keeps its executable
            save();
            emit changed();
            return;
        }
}

QString EngineRegistry::besideApplication(const QString& relative)
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative + QStringLiteral(".exe");
#else
    return QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative;
#endif
}

QString EngineRegistry::builtinStockfishPath() { return besideApplication(QStringLiteral("stockfish")); }

bool EngineRegistry::isBuiltin(const EngineConfig& cfg) { return !bundledKind(cfg).isEmpty(); }

QString EngineRegistry::bundledPath(const QString& kind)
{
    return besideApplication(kind);
}

QString EngineRegistry::bundledKind(const EngineConfig& cfg)
{
    if (cfg.path.isEmpty()) return {};
    for (const char* file : {"stockfish", "shuffle"})
        if (QFileInfo(cfg.path) == QFileInfo(bundledPath(QLatin1String(file)))) return QLatin1String(file);
    return {};
}

void EngineRegistry::remove(const QString& id)
{
    if (isBuiltin(engine(id))) return; // the built-in engines (Stockfish, Shuffle) cannot be removed
    m_engines.removeIf([&](const EngineConfig& e) { return e.id == id; });
    save();
    emit changed();
}

EngineConfig EngineRegistry::fallbackEngine() const
{
    for (const EngineConfig& e : m_engines)
        if (isBuiltin(e)) return e;
    return m_engines.isEmpty() ? EngineConfig() : m_engines.first();
}

bool EngineRegistry::probe(const QString& path, EngineConfig& cfg, QString* error)
{
    QProcess p;
    p.setStandardErrorFile(QProcess::nullDevice());
    p.start(path, {});
    if (!p.waitForStarted(3000)) {
        if (error) *error = QObject::tr("Impossible de lancer « %1 ».").arg(path);
        return false;
    }
    p.write("uci\n");
    QByteArray out;
    bool ok = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        if (!p.waitForReadyRead(200)) {
            if (p.state() != QProcess::Running) break;
            continue;
        }
        out += p.readAllStandardOutput();
        if (out.contains("\nuciok") || out.startsWith("uciok")) {
            ok = true;
            break;
        }
    }
    p.write("quit\n");
    if (!p.waitForFinished(1000)) p.kill();
    if (!ok) {
        if (error) *error = QObject::tr("« %1 » ne répond pas au protocole UCI.").arg(QFileInfo(path).fileName());
        return false;
    }

    cfg.path = path;
    cfg.options.clear();
    for (const QString& raw : QString::fromUtf8(out).split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("id name "))) cfg.idName = line.mid(8);
        else if (line.startsWith(QLatin1String("id author "))) cfg.author = line.mid(10);
        else if (line.startsWith(QLatin1String("option "))) {
            UciOption o;
            if (UciOption::parse(line, o)) cfg.options << o;
        }
    }
    // Values of options that have disappeared are forgotten.
    for (auto it = cfg.values.begin(); it != cfg.values.end();)
        it = cfg.option(it.key()) ? std::next(it) : cfg.values.erase(it);
    cfg.verified = true;
    return true;
}

bool EngineRegistry::create(const QString& path, EngineConfig& cfg, QString* error)
{
    cfg = {};
    cfg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!probe(path, cfg, error)) return false;
    cfg.name = cfg.idName.isEmpty() ? QFileInfo(path).baseName() : cfg.idName;
    // Starting settings more useful than the UCI defaults (1 thread, 16 MB).
    if (const UciOption* o = cfg.option(QStringLiteral("Threads")))
        cfg.values[o->name] = QString::number(std::clamp<qint64>(QThread::idealThreadCount() / 2, std::max<qint64>(1, o->min), std::max<qint64>(1, o->max)));
    if (const UciOption* o = cfg.option(QStringLiteral("Hash")))
        cfg.values[o->name] = QString::number(std::clamp<qint64>(256, o->min, o->max));
    return true;
}

void EngineRegistry::load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) {
        autoDetect();
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : root.value("engines").toArray()) {
        const QJsonObject o = v.toObject();
        EngineConfig cfg;
        cfg.id = o.value("id").toString();
        cfg.name = o.value("name").toString();
        cfg.path = o.value("path").toString();
        cfg.idName = o.value("idName").toString();
        cfg.author = o.value("author").toString();
        cfg.verified = o.value("verified").toBool() && QFileInfo::exists(cfg.path);
        for (const QJsonValue& ov : o.value("options").toArray()) {
            const QJsonObject oo = ov.toObject();
            UciOption opt;
            opt.name = oo.value("name").toString();
            opt.type = oo.value("type").toString();
            opt.defaultValue = oo.value("default").toString();
            opt.min = oo.value("min").toInteger();
            opt.max = oo.value("max").toInteger();
            for (const QJsonValue& c : oo.value("choices").toArray()) opt.choices << c.toString();
            cfg.options << opt;
        }
        const QJsonObject values = o.value("values").toObject();
        for (auto it = values.begin(); it != values.end(); ++it) cfg.values[it.key()] = it.value().toString();
        if (!cfg.id.isEmpty()) m_engines << cfg;
    }
}

void EngineRegistry::save() const
{
    QJsonArray engines;
    for (const EngineConfig& cfg : m_engines) {
        QJsonArray options;
        for (const UciOption& opt : cfg.options) {
            QJsonObject oo{{"name", opt.name}, {"type", opt.type}, {"default", opt.defaultValue}};
            if (opt.type == QLatin1String("spin")) {
                oo["min"] = opt.min;
                oo["max"] = opt.max;
            }
            if (!opt.choices.isEmpty()) oo["choices"] = QJsonArray::fromStringList(opt.choices);
            options << oo;
        }
        QJsonObject values;
        for (auto it = cfg.values.begin(); it != cfg.values.end(); ++it) values[it.key()] = it.value();
        engines << QJsonObject{{"id", cfg.id},         {"name", cfg.name},         {"path", cfg.path},
                               {"idName", cfg.idName}, {"author", cfg.author},     {"verified", cfg.verified},
                               {"options", options},   {"values", values}};
    }
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{"engines", engines}}).toJson());
}

void EngineRegistry::autoDetect()
{
    // First launch: take over the old setting, otherwise Stockfish from the PATH.
    QStringList candidates;
    const QString old = QSettings().value("enginePath").toString();
    if (!old.isEmpty()) candidates << old;
    const QString sf = QStandardPaths::findExecutable(QStringLiteral("stockfish"));
    if (!sf.isEmpty() && !candidates.contains(sf)) candidates << sf;
    for (const QString& path : candidates) {
        EngineConfig cfg;
        if (QFileInfo::exists(path) && create(path, cfg, nullptr)) {
            m_engines << cfg;
        }
    }
    if (!m_engines.isEmpty()) save();
}
