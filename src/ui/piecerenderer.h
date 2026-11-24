#pragma once

#include "chess/types.h"

#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>

// Draws the pieces with the set chosen in the Preferences: the default set, embedded
// (resources/pieces/hydra64/wK.svg … bP.svg), or a folder read from disk without
// recompiling, ~/Documents/hydra64/themes/Pieces/<set>/ with twelve files wK, wQ, wR, wB,
// wN, wP, bK…, each as SVG (drawn at the exact size of the square) or as an image with
// transparency (PNG, WebP: scaled to the square).
class PieceRenderer {
public:
    struct PieceSet {
        QString id;    // identifier stored in the preferences ("hydra64" or "user:<folder>")
        QString name;  // displayed name
        QString credit;
    };
    static constexpr const char* DefaultSet = "hydra64";

    // Available sets, the default set first, the user's at the end. The user
    // folder is reread on each call (an added set appears without restarting the application).
    static QList<PieceSet> availableSets();
    // ~/Documents/hydra64/themes/Pieces; `create`: creates the folder if missing.
    static QString userDirectory(bool create = false);
    // Current set, shared by all the boards (unknown → default set).
    static QString currentSet();
    static void setCurrentSet(const QString& id);

    // Light drop shadow under the pieces (Preferences), shared by all the boards.
    static bool shadow();
    static void setShadow(bool on);

    // Forces a set for this rendering only (Preferences preview) instead of the current set.
    void setFixedSet(const QString& id);
    // Forces the shadow for this rendering only: -1 = current setting, 0 = without, 1 = with.
    void setFixedShadow(int on);

    QPixmap pixmap(chess::Piece piece, int size, qreal dpr);

private:
    QString m_fixedSet; // empty: current set
    QString m_cachedSet; // set of the cached pixmaps
    int m_cachedScan = 0; // reread of the user folder the cache corresponds to
    int m_fixedShadow = -1;
    QHash<quint64, QPixmap> m_cache;
};
