#include "trainingprogress.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {
QString collectionKey(const QString& path)
{
    // Absolute path of the collection (a ":/…" resource keeps its name)
    return path.startsWith(QLatin1Char(':')) ? path : QFileInfo(path).absoluteFilePath();
}
} // namespace

int TrainingProgress::intervalDays(int box)
{
    static const int days[MaxBox + 1] = {0, 1, 3, 7, 14, 30};
    return days[std::clamp(box, 0, MaxBox)];
}

TrainingProgress::TrainingProgress(QString storePath)
    : m_path(std::move(storePath))
{
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly)) m_root = QJsonDocument::fromJson(f.readAll()).object();
}

QString TrainingProgress::defaultStorePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/training-progress.json");
}

TrainingProgress::Entry TrainingProgress::entry(const QString& collection, const QString& key) const
{
    Entry e;
    const QJsonObject o = m_root.value(QStringLiteral("collections")).toObject().value(collectionKey(collection)).toObject().value(key).toObject();
    if (o.isEmpty()) return e;
    e.box = std::clamp(o.value(QStringLiteral("b")).toInt(), 0, MaxBox);
    e.due = QDate::fromString(o.value(QStringLiteral("d")).toString(), Qt::ISODate);
    e.tries = o.value(QStringLiteral("t")).toInt();
    e.successes = o.value(QStringLiteral("s")).toInt();
    return e;
}

void TrainingProgress::record(const QString& collection, const QString& key, bool success, const QDate& today)
{
    Entry e = entry(collection, key);
    ++e.tries;
    if (success) {
        ++e.successes;
        e.box = std::min(MaxBox, e.box + 1);
    } else {
        e.box = 0;
    }
    e.due = today.addDays(intervalDays(e.box));
    QJsonObject all = m_root.value(QStringLiteral("collections")).toObject();
    QJsonObject coll = all.value(collectionKey(collection)).toObject();
    coll.insert(key, QJsonObject{{QStringLiteral("b"), e.box},
                                 {QStringLiteral("d"), e.due.toString(Qt::ISODate)},
                                 {QStringLiteral("t"), e.tries},
                                 {QStringLiteral("s"), e.successes}});
    all.insert(collectionKey(collection), coll);
    m_root.insert(QStringLiteral("collections"), all);
}

void TrainingProgress::reset(const QString& collection)
{
    QJsonObject all = m_root.value(QStringLiteral("collections")).toObject();
    all.remove(collectionKey(collection));
    m_root.insert(QStringLiteral("collections"), all);
}

bool TrainingProgress::save() const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(m_root).toJson(QJsonDocument::Compact));
    return f.commit();
}

QList<int> TrainingProgress::order(const QString& collection, const QStringList& keys, const QDate& today) const
{
    struct Item {
        int index;
        Entry e;
    };
    QList<Item> due, fresh, later;
    for (int i = 0; i < keys.size(); ++i) {
        const Entry e = entry(collection, keys[i]);
        if (!e.seen()) fresh.append({i, e});
        else if (!e.due.isValid() || e.due <= today) due.append({i, e});
        else later.append({i, e});
    }
    std::stable_sort(due.begin(), due.end(), [](const Item& a, const Item& b) {
        if (a.e.due != b.e.due) return a.e.due < b.e.due; // the most overdue first
        return a.e.box < b.e.box;
    });
    std::stable_sort(later.begin(), later.end(), [](const Item& a, const Item& b) { return a.e.due < b.e.due; });
    QList<int> out;
    for (const QList<Item>* list : {&due, &fresh, &later})
        for (const Item& it : *list) out << it.index;
    return out;
}

TrainingProgress::Stats TrainingProgress::stats(const QString& collection, const QStringList& keys, const QDate& today) const
{
    Stats s;
    s.total = int(keys.size());
    for (const QString& k : keys) {
        const Entry e = entry(collection, k);
        if (!e.seen()) {
            ++s.fresh;
            continue;
        }
        if (!e.due.isValid() || e.due <= today) ++s.due;
        if (e.box >= 3) ++s.learned;
        s.tries += e.tries;
        s.successes += e.successes;
    }
    return s;
}
