#include "tabengine.h"

#include <QSettings>
#include <QUrl>

namespace tabengine {

namespace {

// An option name may contain spaces or slashes: encoded to serve as a key.
QString key(const QString& option) { return QString::fromLatin1(QUrl::toPercentEncoding(option)); }
QString group(const QString& tab, const QString& engineId) { return QStringLiteral("tabEngines/%1/%2").arg(tab, engineId.isEmpty() ? QStringLiteral("_") : engineId); }

} // namespace

Notifier& Notifier::instance()
{
    static Notifier n;
    return n;
}

QMap<QString, QString> overrides(const QString& tab, const QString& engineId)
{
    QMap<QString, QString> out;
    if (tab.isEmpty()) return out;
    QSettings s;
    s.beginGroup(group(tab, engineId));
    for (const QString& k : s.childKeys()) out.insert(QUrl::fromPercentEncoding(k.toLatin1()), s.value(k).toString());
    return out;
}

void setValue(const QString& tab, const EngineConfig& engine, const QString& option, const QString& value)
{
    if (tab.isEmpty()) return;
    QSettings s;
    s.beginGroup(group(tab, engine.id));
    if (value == engine.value(option)) s.remove(key(option)); // same value as the engine: no setting of its own any more
    else s.setValue(key(option), value);
    s.endGroup();
    emit Notifier::instance().changed(tab);
}

void clear(const QString& tab, const QString& engineId)
{
    if (tab.isEmpty()) return;
    QSettings().remove(group(tab, engineId));
    emit Notifier::instance().changed(tab);
}

void forget(const QString& tab)
{
    if (!tab.isEmpty()) QSettings().remove(QStringLiteral("tabEngines/%1").arg(tab));
}

EngineConfig effective(const QString& tab, EngineConfig engine)
{
    const QMap<QString, QString> own = overrides(tab, engine.id);
    for (auto it = own.cbegin(); it != own.cend(); ++it) {
        const UciOption* o = engine.option(it.key());
        if (!o && !engine.options.isEmpty()) continue; // option gone from the engine
        if (o && it.value() == o->defaultValue) engine.values.remove(o->name);
        else engine.values[o ? o->name : it.key()] = it.value();
    }
    return engine;
}

} // namespace tabengine
