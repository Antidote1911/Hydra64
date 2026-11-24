#include "boardtheme.h"
#include "userdirs.h"
#include "defaultsets.h"
#include "preferences.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QStandardPaths>
#include <QSvgRenderer>

namespace boardtheme {

namespace {

// Only the default board (Hydra64's two squares, resources/board/hydra64, drawn by
// tools/sets/make_hydra64.py) and the plain colours are in the code. All the others are
// folders of ~/Documents/hydra64/themes/Boards (image or SVG), those shipped with Hydra64 (Lichess) being copied there at
// first launch (defaultsets).
bool isDefault(const QString& id) { return id == QLatin1String(DefaultTheme); }
bool isColors(const QString& id) { return id == QLatin1String(ColorTheme); }

// ---- User boards: ~/Documents/hydra64/themes/Boards/<board>/, as an image or as SVG
constexpr QLatin1String UserPrefix("user:");
constexpr int UserBoardPx = 1600; // side of the image kept in memory (200 px per square)

// Files of a user board: a whole image, or two squares (light, dark).
struct UserBoard {
    QString whole, light, dark;
    bool ok() const { return !whole.isEmpty() || (!light.isEmpty() && !dark.isEmpty()); }
};
UserBoard userBoard(const QString& id)
{
    UserBoard b;
    if (!id.startsWith(UserPrefix)) return b;
    const QString name = id.mid(UserPrefix.size());
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.'))) return b;
    const QDir dir(userDirectory() + QLatin1Char('/') + name);
    if (!dir.exists()) return b;
    static const QStringList formats = {QStringLiteral("*.svg"), QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.webp")};
    const QStringList files = dir.entryList(formats, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        const QString base = QFileInfo(f).completeBaseName().toLower();
        if (base == QLatin1String("light") || base == QLatin1String("white")) b.light = dir.filePath(f);
        else if (base == QLatin1String("dark") || base == QLatin1String("black")) b.dark = dir.filePath(f);
        else if (b.whole.isEmpty()) b.whole = dir.filePath(f);
    }
    if (!b.light.isEmpty() && !b.dark.isEmpty()) b.whole.clear(); // two squares: they take precedence
    return b;
}

// Image (PNG, JPEG, WebP) or SVG drawing rendered into a square of side `px`.
QImage loadSquare(const QString& file, int px)
{
    if (!file.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        const QImage img(file);
        return img.isNull() ? img : img.scaled(px, px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    }
    QSvgRenderer svg(file);
    if (!svg.isValid()) return {};
    QImage out(px, px, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    svg.render(&p, QRectF(0, 0, px, px));
    return out;
}

// Board made of two squares, light and dark, repeated over the 64 squares.
QImage tiledImage(const QString& lightFile, const QString& darkFile)
{
    const int sq = UserBoardPx / 8;
    const QImage light = loadSquare(lightFile, sq), dark = loadSquare(darkFile, sq);
    if (light.isNull() || dark.isNull()) return {};
    QImage out(UserBoardPx, UserBoardPx, QImage::Format_RGB32);
    QPainter p(&out);
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col) p.drawImage(col * sq, row * sq, (row + col) & 1 ? dark : light); // a8 light
    return out;
}

QImage userImage(const QString& id)
{
    const UserBoard b = userBoard(id);
    if (!b.whole.isEmpty()) return loadSquare(b.whole, UserBoardPx);
    return b.ok() ? tiledImage(b.light, b.dark) : QImage();
}

// Images, colours and pixmaps in memory, per theme; the user's are forgotten each time the folder is reread.
struct Caches {
    QHash<QString, QImage> images;
    QHash<QString, std::pair<QColor, QColor>> colors;
    QHash<QString, QPixmap> scaled;
    QSet<QString> validUser; // user boards already checked (no disk access on each drawing)
};
Caches& caches()
{
    static Caches c;
    return c;
}

QString normalized(QString name) { return name.toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-')); }

// Identifiers of old versions already resolved ("wood4" → "user:png/Wood 4"), with no disk access on each drawing.
QHash<QString, QString>& legacyIds()
{
    static QHash<QString, QString> ids;
    return ids;
}

QString valid(const QString& id)
{
    if (isDefault(id) || isColors(id)) return id;
    if (id.startsWith(UserPrefix)) {
        if (caches().validUser.contains(id)) return id;
        if (userBoard(id).ok()) {
            caches().validUser.insert(id);
            return id;
        }
    }
    // Preference of an old version ("wood4", "user:png/Wood 4"): the board is the "Wood 4" folder
    if (auto it = legacyIds().constFind(id); it != legacyIds().constEnd()) return *it;
    QString found = QString::fromLatin1(DefaultTheme);
    const QString old = id.section(QLatin1Char('/'), -1).section(QLatin1Char(':'), -1);
    if (!old.isEmpty())
        for (const QString& name : QDir(userDirectory()).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (normalized(name) == normalized(old) && userBoard(UserPrefix + name).ok()) found = UserPrefix + name;
    legacyIds().insert(id, found);
    return found;
}

// Credit of a board: first line of its CREDITS.txt, if it has one.
QString creditOf(const QString& dir)
{
    for (const QString& f : QDir(dir).entryList(QDir::Files))
        if (f.compare(QLatin1String("CREDITS.txt"), Qt::CaseInsensitive) == 0) {
            QFile file(dir + QLatin1Char('/') + f);
            if (file.open(QIODevice::ReadOnly)) return QString::fromUtf8(file.readLine(400)).trimmed();
        }
    return {};
}

QString& currentStorage()
{
    static QString id = (defaultsets::installOnce(), valid(prefs::boardTheme()));
    return id;
}

// Full-resolution image (null for plain squares), loaded once.
QImage image(const QString& id)
{
    QHash<QString, QImage>& images = caches().images;
    if (auto it = images.constFind(id); it != images.constEnd()) return *it;
    const QImage img = id.startsWith(UserPrefix) ? userImage(id)
                       : isDefault(id) ? tiledImage(QStringLiteral(":/board/hydra64/light.svg"), QStringLiteral(":/board/hydra64/dark.svg"))
                                       : QImage(); // null: plain squares
    images.insert(id, img);
    return img;
}

// Average colour of the centre of a square of the image (edges excluded: grain, borders).
QColor average(const QImage& img, int col, int row)
{
    const int sq = img.width() / 8;
    qint64 r = 0, g = 0, b = 0, n = 0;
    for (int y = row * sq + sq / 4; y < (row + 1) * sq - sq / 4; y += 2)
        for (int x = col * sq + sq / 4; x < (col + 1) * sq - sq / 4; x += 2) {
            const QRgb c = img.pixel(x, y);
            r += qRed(c);
            g += qGreen(c);
            b += qBlue(c);
            ++n;
        }
    return n ? QColor(int(r / n), int(g / n), int(b / n)) : QColor();
}

// Light / dark colours of a theme, computed once (those chosen by the user are read each time).
std::pair<QColor, QColor> colors(const QString& id)
{
    if (isColors(id)) return {prefs::boardLightColor(), prefs::boardDarkColor()};
    QHash<QString, std::pair<QColor, QColor>>& cache = caches().colors;
    if (auto it = cache.constFind(id); it != cache.constEnd()) return *it;
    const QImage img = image(id);
    std::pair<QColor, QColor> c = img.isNull() ? std::pair{theme::SquareLight, theme::SquareDark}
                                               : std::pair{average(img, 0, 0), average(img, 1, 0)};
    cache.insert(id, c);
    return c;
}

// Image scaled to the displayed board's size (keeps the last size of each theme).
QPixmap scaled(const QString& id, int px)
{
    QHash<QString, QPixmap>& cache = caches().scaled;
    auto it = cache.find(id);
    if (it != cache.end() && it->width() == px) return *it;
    const QImage img = image(id);
    QPixmap pm = img.isNull() ? QPixmap() : QPixmap::fromImage(img.scaled(px, px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    cache.insert(id, pm);
    return pm;
}

} // namespace

QList<Theme> available()
{
    QList<Theme> list;
    defaultsets::installOnce();
    list.append({QLatin1String(DefaultTheme), QStringLiteral("Hydra64"), QString()});
    list.append({QLatin1String(ColorTheme), QCoreApplication::translate("boardtheme", "Couleurs unies"), QString()});
    legacyIds().clear();
    // User boards, reread on each call; what was kept of them in memory is forgotten
    Caches& c = caches();
    c.validUser.clear();
    c.images.removeIf([](const auto& it) { return it.key().startsWith(UserPrefix); });
    c.colors.removeIf([](const auto& it) { return it.key().startsWith(UserPrefix); });
    c.scaled.removeIf([](const auto& it) { return it.key().startsWith(UserPrefix); });
    const QDir root(userDirectory());
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        const QString id = UserPrefix + name;
        if (name.startsWith(QLatin1Char('.')) || image(id).isNull()) continue; // neither a whole image nor two readable squares: ignored
        const QString credit = creditOf(root.filePath(name));
        list.append({id, name, credit.isEmpty() ? QCoreApplication::translate("boardtheme", "échiquier personnel") : credit});
    }
    return list;
}

QString userDirectory(bool create)
{
    const QString dir = userdirs::themes() + QStringLiteral("/Boards");
    if (create) QDir().mkpath(dir);
    return dir;
}

QString current() { return currentStorage(); }
void setCurrent(const QString& id) { currentStorage() = valid(id); }

QColor lightColor(const QString& id) { return colors(valid(id)).first; }
QColor darkColor(const QString& id) { return colors(valid(id)).second; }

void paint(QPainter& p, const QRectF& board, qreal dpr, const QString& id)
{
    const QString t = valid(id);
    const QPixmap pm = scaled(t, qRound(board.width() * dpr));
    if (!pm.isNull()) {
        p.drawPixmap(board, pm, QRectF(QPointF(0, 0), QSizeF(pm.size())));
        return;
    }
    const auto [light, dark] = colors(t);
    const qreal sq = board.width() / 8;
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
            p.fillRect(QRectF(board.left() + col * sq, board.top() + row * sq, sq, sq), (row + col) & 1 ? dark : light);
}

QPixmap tile(int col, int row, int size, qreal dpr, const QString& id)
{
    const QString t = valid(id);
    QPixmap pm(QSize(size, size) * dpr);
    const QImage img = image(t);
    if (img.isNull()) {
        pm.fill((row + col) & 1 ? colors(t).second : colors(t).first);
    } else {
        const int sq = img.width() / 8;
        pm = QPixmap::fromImage(img.copy(col * sq, row * sq, sq, sq).scaled(QSize(size, size) * dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    pm.setDevicePixelRatio(dpr);
    return pm;
}

} // namespace boardtheme
