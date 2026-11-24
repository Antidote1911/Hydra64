#include "piecerenderer.h"
#include "userdirs.h"

#include "defaultsets.h"
#include "preferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QLinearGradient>

#include <algorithm>
#include <QPainter>
#include <QRegularExpression>

using namespace chess;

namespace {

// File name of a piece: wK.svg, bQ.svg…
QString pieceFileName(Piece p)
{
    static const char* letters = " PNBRQK";
    return QStringLiteral("%1%2.svg").arg(colorOf(p) == White ? 'w' : 'b').arg(QChar(letters[typeOf(p)]));
}

// Only the default set is in the code (resources/pieces/hydra64/wK.svg…, Hydra64's own,
// drawn by antidote1911 in 2012: its SVG files are their own source). All the others are folders of ~/Documents/hydra64/themes/Pieces, those
// shipped with Hydra64 being copied there at first launch (defaultsets). The pieces are SVG drawings
// or images with transparency (PNG, WebP).
constexpr const char* DefaultCredit = "antidote1911, 2012, CC0";

bool isBundled(const QString& id) { return id == QLatin1String(PieceRenderer::DefaultSet); }

// ---- User sets: ~/Documents/hydra64/themes/Pieces/<set>/wK.svg… or wK.png…
constexpr QLatin1String UserPrefix("user:");

bool isSvg(const QString& file) { return file.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive); }

// File of a piece in a folder (wK.svg, wk.PNG…: the case of the name does not matter), empty if missing.
// A set may mix the formats; when a piece exists in several, the SVG is taken, then the PNG, then the WebP.
QString userPieceFile(const QString& dir, Piece piece)
{
    const QString base = QFileInfo(pieceFileName(piece)).completeBaseName();
    const QStringList files = QDir(dir).entryList(QDir::Files);
    for (const char* format : {"svg", "png", "webp"}) {
        const QString wanted = base + QLatin1Char('.') + QLatin1String(format);
        for (const QString& f : files)
            if (f.compare(wanted, Qt::CaseInsensitive) == 0) return dir + QLatin1Char('/') + f;
    }
    return {};
}

bool isCompleteSet(const QString& dir)
{
    for (Color c : {White, Black})
        for (PieceType t : {Pawn, Knight, Bishop, Rook, Queen, King})
            if (userPieceFile(dir, makePiece(c, t)).isEmpty()) return false;
    return true;
}

// Folder of a user set from its identifier ("user:Mon jeu"), empty if it
// does not exist or lacks a piece.
QString userSetDir(const QString& id)
{
    if (!id.startsWith(UserPrefix)) return {};
    const QString name = id.mid(UserPrefix.size());
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.'))) return {};
    const QString dir = PieceRenderer::userDirectory() + QLatin1Char('/') + name;
    return QFileInfo(dir).isDir() && isCompleteSet(dir) ? dir : QString();
}

// Name without case, spaces or hyphens: finds a set from the identifier of old versions ("kiwen-suwi").
QString normalized(QString name) { return name.toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-')); }

QString validSet(const QString& id)
{
    if (isBundled(id) || !userSetDir(id).isEmpty()) return id;
    // Preference of an old version ("celtic", "user:png/Celtic", "user:svg/Celtic"): the set is the Celtic folder
    const QString old = id.section(QLatin1Char('/'), -1).section(QLatin1Char(':'), -1);
    if (!old.isEmpty()) {
        const QDir root(PieceRenderer::userDirectory());
        for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (normalized(name) == normalized(old) && isCompleteSet(root.filePath(name))) return UserPrefix + name;
    }
    return QString::fromLatin1(PieceRenderer::DefaultSet);
}

// Credit of a set: first line of its CREDITS.txt, if it has one.
QString creditOf(const QString& dir)
{
    for (const QString& f : QDir(dir).entryList(QDir::Files))
        if (f.compare(QLatin1String("CREDITS.txt"), Qt::CaseInsensitive) == 0) {
            QFile file(dir + QLatin1Char('/') + f);
            if (file.open(QIODevice::ReadOnly)) return QString::fromUtf8(file.readLine(400)).trimmed();
        }
    return {};
}

// Counts the rereads of the user folder: the pixmaps kept by each board are then rebuilt.
int& scanCount()
{
    static int n = 0;
    return n;
}

// Content of an SVG file, kept in memory (reread when the user folder is reread).
// Qt reads eight-digit colours badly (#rrggbbaa, opaque or not): they are brought back to #rrggbb.
QHash<QString, QByteArray>& svgCache()
{
    static QHash<QString, QByteArray> files;
    return files;
}

QByteArray svgData(const QString& path)
{
    QHash<QString, QByteArray>& files = svgCache();
    if (auto it = files.constFind(path); it != files.constEnd()) return *it;
    QByteArray data;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        static const QRegularExpression eightDigits(QStringLiteral("((?:fill|stroke|color)\\s*[=:]\\s*[\"']?\\s*#[0-9a-fA-F]{6})[0-9a-fA-F]{2}\\b"));
        data = QString::fromUtf8(f.readAll()).replace(eightDigits, QStringLiteral("\\1")).toUtf8();
    }
    files.insert(path, data);
    return data;
}

// Piece drawn directly at the size of the square (sharp at any size), proportions kept and centred.
QPixmap svgImage(const QString& path, int size, qreal dpr)
{
    const QSize px = QSize(size, size) * dpr;
    QPixmap pm(px);
    pm.fill(Qt::transparent);
    QSvgRenderer svg(svgData(path));
    if (svg.isValid()) {
        svg.setAspectRatioMode(Qt::KeepAspectRatio);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        svg.render(&p, QRectF(QPointF(0, 0), QSizeF(px)));
    }
    pm.setDevicePixelRatio(dpr);
    return pm;
}

// Image of a piece (PNG, WebP), kept in memory (reread when the user folder is reread) at 512 px a
// side at most: enough for a sharp piece on a large high-density board, without holding huge files.
// A tiny image (64 px at most) is pixel art: its pixels are first enlarged as they are, in whole
// multiples, so that the piece stays crisp instead of blurring when it is brought to the square.
constexpr int MaxPieceImagePx = 512;
constexpr int PixelArtPx = 64;
QHash<QString, QImage>& imageCache()
{
    static QHash<QString, QImage> images;
    return images;
}

QImage pieceImage(const QString& path)
{
    QHash<QString, QImage>& images = imageCache();
    if (auto it = images.constFind(path); it != images.constEnd()) return *it;
    QImage img(path);
    if (!img.isNull()) {
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int side = std::max(img.width(), img.height());
        if (side > MaxPieceImagePx)
            img = img.scaled(MaxPieceImagePx, MaxPieceImagePx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        else if (side <= PixelArtPx)
            img = img.scaled(img.size() * (MaxPieceImagePx / side), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    images.insert(path, img);
    return img;
}

// Piece from an image, scaled to the square (smoothly, both down and up), proportions kept and centred.
QPixmap bitmapImage(const QString& path, int size, qreal dpr)
{
    const QSize px = QSize(size, size) * dpr;
    QPixmap pm(px);
    pm.fill(Qt::transparent);
    const QImage img = pieceImage(path);
    if (!img.isNull() && !px.isEmpty()) {
        const QImage fitted = img.scaled(px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter p(&pm);
        p.drawImage((px.width() - fitted.width()) / 2, (px.height() - fitted.height()) / 2, fitted);
    }
    pm.setDevicePixelRatio(dpr);
    return pm;
}

// Drop shadow: silhouette of the piece, blackened, blurred and offset down and to the right,
// drawn under the piece. The blur is obtained by smooth downscaling then upscaling.
QPixmap withShadow(const QPixmap& piece)
{
    const QImage src = piece.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = src.width(), h = src.height();
    QImage silhouette = src;
    {
        QPainter p(&silhouette);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(silhouette.rect(), Qt::black);
    }
    // Silhouette reduced to 18 px a side: blur of about 1/18 of the square, whatever its size.
    const QSize small(std::min(w, 18), std::min(h, 18));
    const QImage blurred = silhouette.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // Shadow offset, faded towards the bottom and right edges: no sharp cut at the edge of the square.
    QImage shadow(src.size(), QImage::Format_ARGB32_Premultiplied);
    shadow.fill(Qt::transparent);
    {
        QPainter p(&shadow);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QPointF(w * 0.035, h * 0.05), blurred);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QLinearGradient down(0, 0, 0, h), right(0, 0, w, 0);
        for (QLinearGradient* g : {&down, &right}) {
            g->setColorAt(0.0, Qt::black);
            g->setColorAt(0.9, Qt::black);
            g->setColorAt(1.0, Qt::transparent);
        }
        p.fillRect(shadow.rect(), down);
        p.fillRect(shadow.rect(), right);
    }
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setOpacity(0.4);
        p.drawImage(0, 0, shadow);
        p.setOpacity(1.0);
        p.drawImage(0, 0, src);
    }
    QPixmap pm = QPixmap::fromImage(out);
    pm.setDevicePixelRatio(piece.devicePixelRatio());
    return pm;
}

bool& shadowStorage()
{
    static bool on = prefs::pieceShadow();
    return on;
}

QString& currentSetStorage()
{
    static QString set = (defaultsets::installOnce(), validSet(prefs::pieceSet()));
    return set;
}

} // namespace

QList<PieceRenderer::PieceSet> PieceRenderer::availableSets()
{
    QList<PieceSet> sets;
    defaultsets::installOnce();
    sets.append({QLatin1String(DefaultSet), QStringLiteral("Hydra64"), QString::fromUtf8(DefaultCredit)});
    // User sets, reread on each call; the files kept in memory are forgotten (files replaced)
    svgCache().removeIf([](const auto& it) { return !it.key().startsWith(QLatin1String(":/")); });
    imageCache().clear();
    ++scanCount();
    const QDir root(userDirectory());
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (name.startsWith(QLatin1Char('.')) || !isCompleteSet(root.filePath(name))) continue; // a piece is missing: ignored
        const QString credit = creditOf(root.filePath(name));
        sets.append({UserPrefix + name, name, credit.isEmpty() ? QCoreApplication::translate("PieceRenderer", "jeu personnel") : credit});
    }
    return sets;
}

QString PieceRenderer::userDirectory(bool create)
{
    const QString dir = userdirs::themes() + QStringLiteral("/Pieces");
    if (create) QDir().mkpath(dir);
    return dir;
}

QString PieceRenderer::currentSet() { return currentSetStorage(); }

void PieceRenderer::setCurrentSet(const QString& id) { currentSetStorage() = validSet(id); }

bool PieceRenderer::shadow() { return shadowStorage(); }

void PieceRenderer::setShadow(bool on) { shadowStorage() = on; }

void PieceRenderer::setFixedShadow(int on)
{
    m_fixedShadow = on;
    m_cache.clear();
}

void PieceRenderer::setFixedSet(const QString& id)
{
    m_fixedSet = validSet(id);
    m_cache.clear();
}

QPixmap PieceRenderer::pixmap(Piece piece, int size, qreal dpr)
{
    const QString set = m_fixedSet.isEmpty() ? currentSet() : m_fixedSet;
    if (set != m_cachedSet || m_cachedScan != scanCount()) {
        m_cache.clear();
        m_cachedSet = set;
        m_cachedScan = scanCount();
    }
    const bool withShadowOn = m_fixedShadow < 0 ? shadow() : m_fixedShadow == 1;
    const quint64 key = (quint64(withShadowOn) << 63) | (quint64(piece) << 48) | (quint64(size) << 16) | quint64(dpr * 100);
    if (auto it = m_cache.find(key); it != m_cache.end()) return *it;

    const QString dir = userSetDir(set);
    const QString file = dir.isEmpty() ? QStringLiteral(":/pieces/hydra64/") + pieceFileName(piece) : userPieceFile(dir, piece);
    QPixmap pm = isSvg(file) ? svgImage(file, size, dpr) : bitmapImage(file, size, dpr);
    if (withShadowOn) pm = withShadow(pm);
    m_cache.insert(key, pm);
    return pm;
}
