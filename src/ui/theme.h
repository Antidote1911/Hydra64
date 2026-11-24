#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;

// Palette inspired by DaVinci Resolve: neutral graphite greys, orange accent.
namespace theme {

inline const QColor Window{0x18, 0x18, 0x1a};
inline const QColor Panel{0x21, 0x21, 0x24};
inline const QColor PanelAlt{0x26, 0x26, 0x29};
inline const QColor Header{0x2a, 0x2a, 0x2e};
inline const QColor Raised{0x33, 0x33, 0x38};
inline const QColor Border{0x0e, 0x0e, 0x10};
inline const QColor BorderSoft{0x36, 0x36, 0x3b};
inline const QColor Text{0xd6, 0xd6, 0xda};
inline const QColor TextDim{0x8e, 0x8e, 0x96};
inline const QColor TextFaint{0x5c, 0x5c, 0x64};
inline const QColor Accent{0xe6, 0x8a, 0x35};
inline const QColor AccentSoft{0xe6, 0x8a, 0x35, 0x40};
inline const QColor Good{0x5c, 0xb8, 0x7e};
inline const QColor Bad{0xe0, 0x5a, 0x50};
inline const QColor Comment{0x86, 0xb0, 0x95};

// Squares of the default board (average of resources/board/hydra64/light.svg and dark.svg)
inline const QColor SquareLight{0x9c, 0x9e, 0xa5};
inline const QColor SquareDark{0x46, 0x47, 0x4e};

void apply(QApplication& app);

// Style override for the Qt Advanced Docking System (to be added to ADS's own).
QString dockStyleSheet();

// Built-in vector icons (outline), recoloured according to the state.
QIcon icon(const QString& name);
QIcon icon(const QString& name, const QColor& color); // same drawing, in a single colour (state of a tab)
// Icon from a 24×24 SVG line drawing supplied by the caller (database icons), up to 48 px.
QIcon drawnIcon(const QString& body);

// Application logo (three-headed hydra; its central head alone below 48 px), in colour.
QPixmap logo(int size, qreal dpr = 1.0);
QIcon logoIcon();
// Banner of the page bar: close-up of the hydra's three heads, without text.
QPixmap logoBanner(const QSize& size, qreal dpr = 1.0);

} // namespace theme
