#pragma once

#include <QString>

// Databases shipped with Hydra64 ("databases" folder): sample games, training
// exercises, openings for tournaments, engine tests.
namespace bundled {

// The "databases" folder: the user's (userdirs::databases(), where the bundled databases are
// copied at first launch), otherwise next to the executable (distributed archive, build),
// otherwise that of the sources.
QString databasesDir();

// Path saved before the folders were renamed (November 2026): if the file no longer exists and
// it came from a former bundled folder ("positions-depart-tournois", "tests-moteurs"), its
// current equivalent, as a single-file database. Any other path is returned as is.
QString currentPath(const QString& path);

} // namespace bundled
