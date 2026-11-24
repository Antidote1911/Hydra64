#pragma once

#include "db/playerbase.h"

#include <QString>

// ChessBase players encyclopedia ("Playbase"), optional: its folder is chosen by
// the user (player profile) and remembered. It completes a player's profile: photo, title,
// federation, dates, Elo history from the FIDE lists.
namespace playerEncyclopedia {

QString directory();                                    // remembered folder (empty: none)
bool setDirectory(const QString& dir, QString* error);  // opens and remembers; empty folder: forgets
const hy64db::PlayerBase* get();                         // open encyclopedia, nullptr otherwise

} // namespace playerEncyclopedia
