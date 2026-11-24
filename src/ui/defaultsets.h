#pragma once

#include <QString>

// Piece sets (SVG) and boards (images or SVG) shipped with Hydra64, those of Lichess. Only the default
// piece set and board are in the code: the others are ordinary folders,
// copied at first launch into ~/Documents/hydra64/themes/Pieces/<set> and themes/Boards/<set>,
// where the user also drops their own. They can therefore rename, edit or delete them.
// The themes folder also receives format.html, which describes the accepted formats (French / English).
namespace defaultsets {

QString themesDirectory(); // ~/Documents/hydra64/themes
// Copy at first launch (only once: a set deleted afterwards does not come back by itself).
// At each launch: format.html is put back if missing, and the folders of previous versions
// (Pieces and Boards outside Themes, svg/ subfolders) are put back in their place.
void installOnce();
// Number of shipped sets missing from ~/Documents/hydra64/themes (folder deleted or renamed).
int missing();
// Copies back the missing shipped sets; returns the number of sets restored. Folders that are present are not touched.
int restore();

} // namespace defaultsets
