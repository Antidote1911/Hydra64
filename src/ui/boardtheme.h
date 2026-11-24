#pragma once

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QString>

class QPainter;
class QRectF;

// Board theme, chosen in the Preferences: Hydra64's own squares (default, resources/board/hydra64), plain squares in two
// colours chosen by the user (prefs::boardLightColor, boardDarkColor), or a folder read from disk without recompiling, ~/Documents/hydra64/themes/Boards/<board>/.
// The folder contains either an image of the whole board (PNG, JPEG, WebP or SVG, a8 at the top
// left), or two squares "light" and "dark" repeated over the 64 squares.
namespace boardtheme {

struct Theme {
    QString id;     // identifier stored in the preferences
    QString name;   // displayed name
    QString credit;
};
inline constexpr const char* DefaultTheme = "hydra64";
inline constexpr const char* ColorTheme = "colors"; // plain squares, colours chosen in the Preferences

// The default theme first, then the chosen colours, the user's at the end; the user folder is
// reread on each call (an added board appears without restarting the application).
QList<Theme> available();
QString userDirectory(bool create = false); // ~/Documents/hydra64/themes/Boards; `create`: creates the folder if missing
QString current();        // unknown → default theme
void setCurrent(const QString& id);

// (Average) colours of the light and dark squares: coordinates, previews…
QColor lightColor(const QString& id = current());
QColor darkColor(const QString& id = current());

// Draws the 64 squares into `board` (square). The image is the same whatever the orientation:
// a8 and h1 are light squares, always at the top left and bottom right.
void paint(QPainter& p, const QRectF& board, qreal dpr, const QString& id = current());

// Square of the board (file 0–7 from the left, rank 0–7 from the top), for the previews.
QPixmap tile(int col, int row, int size, qreal dpr, const QString& id = current());

} // namespace boardtheme
