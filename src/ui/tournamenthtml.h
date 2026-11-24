#pragma once

#include "chess/game.h"

#include <QString>

class Tournament;

// Result of a tournament as a single standalone HTML page: standings, crosstable, all the
// games with their moves, and a board to replay them (keyboard arrows, click on a
// move, evaluation graph). No file alongside, no connection needed: the pieces of the
// current set are included in the page, with their credit.
namespace tournamentHtml {

QString page(const Tournament& tournament, const QString& title);
// A single game, in the same presentation: moves, variations, comments, board.
QString gamePage(const chess::Game& game);

} // namespace tournamentHtml
