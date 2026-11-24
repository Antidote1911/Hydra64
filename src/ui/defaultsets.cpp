#include "defaultsets.h"
#include "userdirs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace defaultsets {

namespace {

const char* const kinds[] = {"Pieces", "Boards"};

QString userRoot() { return themesDirectory(); }

// Documentation page of the formats, in step with the program (rewritten if missing or different).
void installFormatPage()
{
    QFile source(QStringLiteral(":/sets/format.html"));
    if (!source.open(QIODevice::ReadOnly)) return;
    const QByteArray wanted = source.readAll();
    const QString path = themesDirectory() + QStringLiteral("/format.html");
    QFile target(path);
    if (target.open(QIODevice::ReadOnly) && target.readAll() == wanted) return;
    target.close();
    QDir().mkpath(themesDirectory());
    if (target.open(QIODevice::WriteOnly | QIODevice::Truncate)) target.write(wanted);
}

// Previous versions: Pieces and Boards were directly in Documents/Hydra64, then the
// sets in svg/ and png/ subfolders. The sets move up one level; PNG pieces are no
// longer read, their png/ folder is left as is, for the user to delete.
void migrateOldFolders()
{
    const QString old = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Hydra64");
    for (const char* kind : kinds) {
        const QString from = old + QLatin1Char('/') + QLatin1String(kind), to = themesDirectory() + QLatin1Char('/') + QLatin1String(kind);
        if (QDir(from).exists() && !QDir(to).exists()) {
            QDir().mkpath(themesDirectory());
            QDir().rename(from, to);
        }
        for (const char* format : {"svg", "png"}) {
            if (QLatin1String(format) == QLatin1String("png") && QLatin1String(kind) == QLatin1String("Pieces")) continue;
            QDir sub(to + QLatin1Char('/') + QLatin1String(format));
            if (!sub.exists()) continue;
            for (const QString& set : sub.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (!QFileInfo::exists(to + QLatin1Char('/') + set)) QDir().rename(sub.filePath(set), to + QLatin1Char('/') + set);
            QDir().rmdir(sub.path()); // only if it is empty
        }
    }
}

// Walks the shipped sets (resources :/sets/<Pieces|Boards>/<set>); `visit(source, destination)`.
template <typename Visit>
void forEachSet(Visit visit)
{
    for (const char* kind : kinds) {
        const QDir sets(QStringLiteral(":/sets/%1").arg(QLatin1String(kind)));
        for (const QString& set : sets.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            visit(sets.filePath(set), QStringLiteral("%1/%2/%3").arg(userRoot(), QLatin1String(kind), set));
    }
}

bool copySet(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to)) return false;
    bool ok = true;
    for (const QString& file : QDir(from).entryList(QDir::Files)) {
        const QString target = to + QLatin1Char('/') + file;
        ok = QFile::copy(from + QLatin1Char('/') + file, target) && ok;
        // A resource is read-only: the copy must stay editable and deletable
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    return ok;
}

} // namespace

QString themesDirectory() { return userdirs::themes(); }

int missing()
{
    int n = 0;
    forEachSet([&](const QString&, const QString& to) { n += !QDir(to).exists(); });
    return n;
}

int restore()
{
    int n = 0;
    forEachSet([&](const QString& from, const QString& to) {
        if (!QDir(to).exists() && copySet(from, to)) ++n;
    });
    return n;
}

void installOnce()
{
    static bool checked = false;
    if (checked) return;
    checked = true;
    migrateOldFolders();
    installFormatPage();
    // Once per generation of shipped sets: 2 replaced the PNG piece sets with their SVG originals, 3 the boards;
    // 5 ships the former default set and board as Merida and Graphite
    constexpr int Generation = 5;
    QSettings s;
    if (s.value(QStringLiteral("sets/generation"), 0).toInt() >= Generation) return;
    restore();
    s.setValue(QStringLiteral("sets/generation"), Generation);
}

} // namespace defaultsets
