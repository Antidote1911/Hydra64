#include "surveyindex.h"

#include "db/database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <map>

namespace surveyindex {

namespace {
const QLatin1String kSuffix(".surveys.json");
const QLatin1String kFormat("hydra64-surveys");

QString tr(const char* text) { return QCoreApplication::translate("surveyindex", text); }

bool before(const Survey& a, const Survey& b)
{
    if (a.eco.isEmpty() != b.eco.isEmpty()) return b.eco.isEmpty();
    if (a.eco != b.eco) return a.eco < b.eco;
    if (a.date != b.date) return a.date < b.date;
    return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
}

// "C55-56_2000-05_Canal-Gambit" → ECO range, date, title of the file name (used when the
// database itself gives no title).
void fromFileName(const QString& baseName, Survey& s)
{
    static const QRegularExpression re(QStringLiteral("^([A-E]\\d\\d)(?:-([A-E]?\\d\\d))?_(\\d{4}-\\d{2})_(.*)$"));
    const auto m = re.match(baseName);
    QString title = baseName;
    if (m.hasMatch()) {
        s.eco = m.captured(1);
        s.ecoTo = m.captured(2);
        if (s.ecoTo.size() == 2) s.ecoTo.prepend(s.eco[0]); // "C55-56"
        s.date = m.captured(3);
        title = m.captured(4);
    }
    s.title = title.replace(QLatin1Char('-'), QLatin1Char(' '));
}
} // namespace

QString pathFor(const QString& databasePath)
{
    const QFileInfo fi(databasePath);
    return fi.dir().filePath(fi.completeBaseName() + kSuffix);
}

QString find(const QString& databasePath)
{
    if (databasePath.isEmpty()) return {};
    const QString own = pathFor(databasePath);
    if (QFileInfo::exists(own)) return own;
    QDir dir = QFileInfo(databasePath).dir();
    for (int level = 0; level < 2; ++level) {
        const QStringList found = dir.entryList({QLatin1Char('*') + kSuffix}, QDir::Files, QDir::Name);
        if (!found.isEmpty()) return dir.filePath(found.first());
        if (!dir.cdUp()) break;
    }
    return {};
}

QList<Survey> load(const QString& indexPath, QString* error)
{
    QList<Survey> out;
    QFile f(indexPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return out;
    }
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError || root.value(QLatin1String("format")).toString() != kFormat) {
        if (error) *error = pe.error != QJsonParseError::NoError ? pe.errorString() : tr("ce n'est pas un index de synthèses");
        return out;
    }
    const QDir dir = QFileInfo(indexPath).dir();
    const QJsonArray list = root.value(QLatin1String("surveys")).toArray();
    out.reserve(list.size());
    for (const QJsonValue& v : list) {
        const QJsonObject o = v.toObject();
        Survey s;
        s.file = QDir::cleanPath(dir.absoluteFilePath(o.value(QLatin1String("file")).toString()));
        s.eco = o.value(QLatin1String("eco")).toString();
        s.ecoTo = o.value(QLatin1String("ecoTo")).toString();
        s.date = o.value(QLatin1String("date")).toString();
        s.title = o.value(QLatin1String("title")).toString();
        s.author = o.value(QLatin1String("author")).toString();
        s.games = o.value(QLatin1String("games")).toInt();
        out.append(std::move(s));
    }
    std::stable_sort(out.begin(), out.end(), before);
    return out;
}

int build(const QString& databasePath, const QString& surveysDir, QString* error, const std::function<void(int, int)>& progress)
{
    const QDir source(surveysDir);
    const QStringList files = source.entryList({QStringLiteral("*.hy64"), QStringLiteral("*.hy64i")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        if (error) *error = tr("aucune base Hydra64 dans %1").arg(QDir::toNativeSeparators(surveysDir));
        return -1;
    }
    const QDir indexDir = QFileInfo(databasePath).dir();
    QList<Survey> surveys;
    QStringList relative;
    for (int i = 0; i < files.size(); ++i) {
        if (progress) progress(i, int(files.size()));
        const QString path = source.absoluteFilePath(files[i]);
        hy64db::Database db;
        std::string err;
        if (!db.open(path.toStdString(), &err)) {
            if (error) *error = QStringLiteral("%1 : %2").arg(files[i], QString::fromStdString(err));
            return -1;
        }
        Survey s;
        s.file = path;
        fromFileName(QFileInfo(path).completeBaseName(), s);
        s.games = int(db.count());
        // Title: the survey text opens the database, its title is in the "White" field.
        std::map<uint32_t, int> annotators;
        for (uint32_t id = 0; id < db.count(); ++id) {
            const hy64db::Record r = db.record(id);
            if (id == 0 && db.names().name(hy64db::Player, r.black).size() <= 1) {
                QString title = QString::fromStdString(db.names().name(hy64db::Player, r.white)).trimmed();
                // "C11 - 7...Be7 8.Qd2": the code is already in its own column
                if (!s.eco.isEmpty() && title.startsWith(s.eco)) title = title.mid(s.eco.size()).remove(QRegularExpression(QStringLiteral("^\\s*[-–:]\\s*")));
                // "9.a4" alone says nothing: the opening named by the file comes first
                if (!title.isEmpty() && title[0].isDigit()) title = s.title + QStringLiteral(" — ") + title;
                if (title.size() > 1) s.title = title;
            }
            if (r.annotator != hy64db::NoName) ++annotators[r.annotator];
        }
        int best = 0;
        for (const auto& [id, n] : annotators)
            if (n > best) best = n, s.author = QString::fromStdString(db.names().name(hy64db::Annotator, id)).trimmed();
        surveys.append(std::move(s));
    }
    if (progress) progress(int(files.size()), int(files.size()));
    std::stable_sort(surveys.begin(), surveys.end(), before);

    QJsonArray list;
    for (const Survey& s : surveys) {
        QJsonObject o;
        o.insert(QLatin1String("file"), indexDir.relativeFilePath(s.file));
        if (!s.eco.isEmpty()) o.insert(QLatin1String("eco"), s.eco);
        if (!s.ecoTo.isEmpty()) o.insert(QLatin1String("ecoTo"), s.ecoTo);
        if (!s.date.isEmpty()) o.insert(QLatin1String("date"), s.date);
        o.insert(QLatin1String("title"), s.title);
        if (!s.author.isEmpty()) o.insert(QLatin1String("author"), s.author);
        o.insert(QLatin1String("games"), s.games);
        list.append(o);
    }
    QJsonObject root;
    root.insert(QLatin1String("format"), kFormat);
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("database"), QFileInfo(databasePath).fileName());
    root.insert(QLatin1String("surveys"), list);
    QSaveFile out(pathFor(databasePath));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !out.commit()) {
        if (error) *error = out.errorString();
        return -1;
    }
    return int(surveys.size());
}

} // namespace surveyindex
