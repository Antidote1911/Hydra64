#include "userdirs.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace userdirs {

namespace {

const char* settingsKey(Kind kind)
{
    switch (kind) {
    case Kind::OpeningBook: return "dirs/openingBook";
    case Kind::Tablebase: return "dirs/tablebase";
    case Kind::Database: break;
    }
    return "dirs/database";
}

QString defaultDir(Kind kind)
{
    switch (kind) {
    case Kind::OpeningBook: return openingBooks();
    case Kind::Tablebase: return tablebases();
    case Kind::Database: break;
    }
    return databases();
}

// Bundled databases: next to the executable (distributed archive, build folder), otherwise the sources.
QString shippedDatabases()
{
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/databases");
    if (QDir(beside).exists()) return beside;
#ifdef HYDRA64_DATABASES_DIR
    return QStringLiteral(HYDRA64_DATABASES_DIR);
#else
    return QString();
#endif
}

} // namespace

int renameLegacyFiles(const QString& folder)
{
    static const std::pair<const char*, const char*> extensions[] = {{"b64", "hy64"},   {"b64book", "hy64book"}, {"b64deep", "hy64deep"}, {"b64i", "hy64i"},
                                                                     {"b64n", "hy64n"}, {"b64g", "hy64g"},       {"b64p", "hy64p"}};
    QStringList files;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
    int renamed = 0;
    for (const QString& file : std::as_const(files)) {
        const QString suffix = QFileInfo(file).suffix().toLower();
        for (const auto& [from, to] : extensions) {
            if (suffix != QLatin1String(from)) continue;
            const QString target = file.left(file.size() - suffix.size()) + QLatin1String(to);
            if (!QFileInfo::exists(target) && QFile::rename(file, target)) ++renamed;
        }
    }
    return renamed;
}

QString root()
{
    const QString forced = qEnvironmentVariable("HYDRA64_HOME");
    if (!forced.isEmpty()) return QDir::cleanPath(forced);
    // In the Documents of every system. The program was called Basilisk64 until 22 November 2026:
    // its folder (Documents/basilisk64) is taken over once, if there is no Hydra64 folder yet, and
    // the files it holds get the new extensions (.b64 → .hy64, .b64book → .hy64book…). Their content
    // does not change: the formats are the same.
    static const QString documents = [] {
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString dir = docs + QStringLiteral("/hydra64");
        const QString old = docs + QStringLiteral("/basilisk64");
        if (QDir(old).exists() && !QFileInfo::exists(dir) && QDir().rename(old, dir)) renameLegacyFiles(dir);
        return dir;
    }();
    return documents;
}

QString databases() { return root() + QStringLiteral("/databases"); }
QString openingBooks() { return root() + QStringLiteral("/openingbooks"); }
QString tablebases() { return root() + QStringLiteral("/tablebases"); }
QString uciEngines() { return root() + QStringLiteral("/uci_engines"); }
QString tournaments() { return root() + QStringLiteral("/engines_tournaments"); }
QString exercises() { return root() + QStringLiteral("/exercises"); }
QString analyses() { return root() + QStringLiteral("/analyses"); }
QString repertoire() { return root() + QStringLiteral("/repertoire"); }
QString themes() { return root() + QStringLiteral("/themes"); }

void install()
{
    for (const QString& dir : {databases(), openingBooks(), tablebases(), uciEngines(), tournaments()}) QDir().mkpath(dir);
    const QString from = shippedDatabases();
    if (from.isEmpty() || QFileInfo(from) == QFileInfo(databases())) return;
    // Folder by folder: a bundled subfolder already present on the user's side is left as is
    // (they may have edited or deleted databases in it); only missing folders are copied, whole.
    const QDir source(from);
    for (const QFileInfo& top : source.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        const QString target = databases() + QLatin1Char('/') + top.fileName();
        if (QFileInfo::exists(target)) continue;
        if (top.isFile()) {
            QFile::copy(top.absoluteFilePath(), target);
            continue;
        }
        QDirIterator it(top.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            const QString to = target + QLatin1Char('/') + QDir(top.absoluteFilePath()).relativeFilePath(file);
            QDir().mkpath(QFileInfo(to).absolutePath());
            QFile::copy(file, to);
        }
    }
}

QString startDir(Kind kind)
{
    const QString last = QSettings().value(QLatin1String(settingsKey(kind))).toString();
    if (!last.isEmpty() && QDir(last).exists()) return last;
    const QString dir = defaultDir(kind);
    QDir().mkpath(dir);
    return dir;
}

void remember(Kind kind, const QString& chosenPath)
{
    if (chosenPath.isEmpty()) return;
    const QFileInfo info(chosenPath);
    QSettings().setValue(QLatin1String(settingsKey(kind)), info.isDir() ? info.absoluteFilePath() : info.absolutePath());
}

} // namespace userdirs
