#pragma once

#include <QString>

// User folder: everything Hydra64 reads or writes for the user apart from its settings.
//
//   ~/Documents/hydra64/ on every system (Windows: Documents\hydra64\)
//   (HYDRA64_HOME environment variable: another folder — portable installation, tests)
//
//     databases/      game databases: the bundled ones (games, training, tournament-openings,
//                     engine-tests, copied there at first launch) and your own
//     openingbooks/   opening books (.hy64book, .bin)
//     tablebases/     endgame tablebases
//     uci_engines/    your UCI engines (empty at first; the built-in Stockfish and Shuffle are not here)
//     engines_tournaments/  databases of the engine tournaments
//     exercises/      your exercise collections
//     analyses/       deep analysis projects
//     repertoire/     opening repertoire (White.pgn, Black.pgn)
//     themes/         piece sets and boards
//
// File choosers open first in the folder of the requested kind (a database:
// databases, a book: openingbooks, tables: tablebases), then wherever one went the last
// time for that kind.
namespace userdirs {

QString root();
// Files written when the program was called Basilisk64 (.b64, .b64book, .b64deep, .b64i…) get the
// Hydra64 extensions, in `folder` and below; the formats are unchanged. Returns the number of files renamed.
int renameLegacyFiles(const QString& folder);
QString databases();
QString openingBooks();
QString tablebases();
QString uciEngines();
QString tournaments(); // engines_tournaments/
QString exercises();
QString analyses();
QString repertoire();
QString themes();

// Creates databases/, openingbooks/, tablebases/, uci_engines/ and engines_tournaments/, and copies into databases/ the databases shipped with the
// program (only the missing ones: a database that was edited or deleted is not overwritten… nor
// recreated if its folder already exists). To be called when the application starts.
void install();

// Folder in which to open a file chooser for this kind of file, and remembering of the last choice.
enum class Kind { Database, OpeningBook, Tablebase };
QString startDir(Kind kind);
void remember(Kind kind, const QString& chosenPath); // chosen file or folder

} // namespace userdirs
