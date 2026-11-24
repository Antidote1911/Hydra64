#include "bundledbases.h"
#include "userdirs.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace bundled {

QString databasesDir()
{
    // The user folder (the bundled databases are copied there at first launch), otherwise the program's
    const QString marker = QStringLiteral("/training/mates/mate_in_1.hy64");
    if (QFileInfo::exists(userdirs::databases() + marker)) return userdirs::databases();
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/databases");
    if (QFileInfo::exists(beside + marker)) return beside;
    return QStringLiteral(HYDRA64_DATABASES_DIR);
}

QString currentPath(const QString& path)
{
    if (path.isEmpty() || QFileInfo::exists(path)) return path;
    static const std::pair<const char*, const char*> moved[] = {{"/positions-depart-tournois/", "/tournament-openings/"},
                                                                {"/tests-moteurs/", "/engine-tests/"}};
    const QString normal = QString(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
    for (const auto& [before, after] : moved) {
        if (!normal.contains(QLatin1String(before))) continue;
        const QString candidate = databasesDir() + QLatin1String(after) + QFileInfo(normal).completeBaseName() + QStringLiteral(".hy64");
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return path;
}

} // namespace bundled
