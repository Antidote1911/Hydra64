#pragma once

#include <QString>
#include <QStringList>

#include <functional>

// Conversion of game databases and opening trees to Hydra64 or open formats.
// A game database can also be turned into an opening tree (statistics of its games: db/gametree).
// Formats read: PGN, Hydra64 (.hy64i), ChessBase (.cbh/.2cbh; .cbv archive extracted first),
// ChessBase books (.ctg), ChessOK / Aquarium trees (.hsh/.elm and text export), Polyglot (.bin),
// Hydra64 trees (.hy64book). Formats written: Hydra64 database, PGN, Hydra64 tree, Polyglot.
// Used by File → Convert… and by hydra64-convert.
namespace conversion {

enum class Kind { Unknown, Database, Tree };
// Hydra64Single: Hydra64 database in a single file (.hy64).
enum class Target { Hydra64Database, Pgn, Hydra64Tree, Polyglot, Hydra64Single };

// File of a ChessBase database other than the .cbh (.cbg, .cba, .cbj…): the .cbh beside it; otherwise `path`.
QString databaseFile(const QString& path);
Kind kindOf(const QString& path);
QString formatName(const QString& path);      // "Base ChessBase", "Livre ChessBase"…
bool isProprietary(const QString& path);       // ChessBase, ChessOK: cannot be opened directly
bool isArchive(const QString& path);           // .cbv, .2cbv: to be extracted first
QList<Target> targetsFor(Kind kind);           // the first one is offered by default
inline bool isTree(Target t) { return t == Target::Hydra64Tree || t == Target::Polyglot; }
QString targetName(Target target);
QString targetExtension(Target target);        // « .hy64i »…
Target targetForExtension(const QString& path, Kind kind, bool* ok);
QString sourceFilter();                        // file chooser filter (all formats read)

struct Request {
    QString source, destination;
    Target target = Target::Hydra64Database;
    int positionDepth = 24; // Hydra64 database: depth of the position index (0: none)
    // Opening tree made from a game database (hy64db::GameTreeOptions): half-moves of each game,
    // visits a position needs for the games to be followed beyond it
    int treePlies = 50;
    int treeMinGames = 2;
};
struct Report {
    quint64 items = 0;  // games or moves written
    quint64 failed = 0; // unreadable games
    quint64 positions = 0;
    double seconds = 0;
    QStringList failures;
    // Games damaged in the source, kept without their annotations or without their moves
    quint64 withoutAnnotations = 0, withoutMoves = 0;
    QStringList partial;
    QString summary() const;
    Kind kind = Kind::Unknown;
};

// Blocking (to be run off the interface thread). `progress(‰)`; returning false cancels.
bool run(const Request& request, Report* report, QString* error, const std::function<bool(int)>& progress = {});

} // namespace conversion
