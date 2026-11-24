#include "boardwidget.h"
#include "boardtheme.h"
#include "preferences.h"
#include "theme.h"

#include <QEasingCurve>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QVariantAnimation>

#include <cmath>

using namespace chess;

BoardWidget::BoardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, qOverload<>(&QWidget::update)); // piece set
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_pos = Position::startPos();
    m_legal = m_pos.legalMoves();

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(160);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_animT = v.toReal();
        update();
    });
    connect(m_anim, &QVariantAnimation::finished, this, [this] {
        m_animT = 1.0;
        m_animPiece = NoPiece;
        update();
    });
}

void BoardWidget::setPosition(const Position& pos, Move lastMove, bool animate)
{
    m_anim->stop();
    m_animPiece = NoPiece;
    if (animate && !m_suppressAnimation && !lastMove.isNull()) {
        m_animPiece = pos.at(lastMove.to);
        m_animFrom = lastMove.from;
        m_animTo = lastMove.to;
        // Chess960 castling (the king "captures" its rook): the king is the one animated, to g or c.
        if (m_pos.isCastling(lastMove) && m_pos.isLegal(lastMove) && m_pos.after(lastMove).fen() == pos.fen()) {
            m_animTo = makeSquare(m_pos.isKingsideCastling(lastMove) ? 6 : 2, rankOf(lastMove.from));
            m_animPiece = pos.at(m_animTo);
        }
        m_animT = 0.0;
        m_anim->start();
    }
    if (animate) m_suppressAnimation = false; // otherwise it will be used for the next animated move

    m_pos = pos;
    m_legal = pos.legalMoves();
    m_lastMove = lastMove;
    m_engineArrows.clear();
    m_qualities.clear();
    m_selected = NoSquare;
    m_pressSquare = NoSquare;
    m_dragging = false;
    m_graphics.clear(); // those of the new position are given by setGraphics()
    update();
}

void BoardWidget::setGraphics(const std::vector<Graphic>& graphics)
{
    m_graphics = graphics;
    update();
}

QColor BoardWidget::graphicColor(char color)
{
    switch (color) {
    case 'R': return QColor(224, 90, 80);
    case 'Y': return QColor(230, 190, 60);
    case 'B': return QColor(111, 163, 216);
    default: return QColor(92, 184, 126);
    }
}

void BoardWidget::setFlipped(bool flipped)
{
    m_flipped = flipped;
    update();
}

void BoardWidget::setMovableSides(bool white, bool black)
{
    m_movable[White] = white;
    m_movable[Black] = black;
    if (!m_movable[m_pos.sideToMove()]) {
        m_selected = NoSquare;
        m_pressSquare = NoSquare;
        m_dragging = false;
    }
    update();
}

void BoardWidget::setMoveQualities(const QHash<int, int>& qualities)
{
    if (qualities == m_qualities) return;
    m_qualities = qualities;
    if (m_selected != NoSquare) update();
}

QColor BoardWidget::qualityColor(int quality)
{
    switch (quality) {
    case 1: return QColor(0x4c, 0xaf, 0x50);
    case 2: return QColor(0xe0, 0xb0, 0x30);
    case 3: return QColor(0xd9, 0x53, 0x4f);
    default: return QColor(20, 20, 24);
    }
}

void BoardWidget::setEngineArrow(const QString& key, Move m, const QColor& color)
{
    if (m.isNull()) {
        if (m_engineArrows.remove(key)) update();
        return;
    }
    const auto it = m_engineArrows.constFind(key);
    if (it != m_engineArrows.cend() && it->first == m && it->second == color) return;
    m_engineArrows.insert(key, {m, color});
    update();
}

QRectF BoardWidget::boardRect() const
{
    const int margin = 10;
    int side = std::min(width(), height()) - 2 * margin;
    side = std::max(8, side - side % 8); // squares to the pixel
    return QRectF((width() - side) / 2, (height() - side) / 2, side, side);
}

QRectF BoardWidget::squareRect(Square s) const
{
    const QRectF b = boardRect();
    const qreal sq = b.width() / 8.0;
    int f = fileOf(s), r = rankOf(s);
    if (m_flipped) f = 7 - f;
    else r = 7 - r;
    return QRectF(b.left() + f * sq, b.top() + r * sq, sq, sq);
}

Square BoardWidget::squareAt(QPointF p) const
{
    const QRectF b = boardRect();
    if (!b.contains(p)) return NoSquare;
    const qreal sq = b.width() / 8.0;
    int f = std::clamp(int((p.x() - b.left()) / sq), 0, 7);
    int r = std::clamp(int((p.y() - b.top()) / sq), 0, 7);
    if (m_flipped) f = 7 - f;
    else r = 7 - r;
    return makeSquare(f, r);
}

bool BoardWidget::isTarget(Square from, Square to) const { return !resolve(from, to).isNull(); }

// Legal move from `from` to `to`. Chess960: castling ("the king captures its rook") is also played by
// dropping the king on its target square (g or c), if that is not already an ordinary king move.
Move BoardWidget::resolve(Square from, Square to) const
{
    for (const Move& m : m_legal)
        if (m.from == from && m.to == to) return m;
    for (const Move& m : m_legal)
        if (m.from == from && m_pos.isCastling(m) && makeSquare(m_pos.isKingsideCastling(m) ? 6 : 2, rankOf(from)) == to) return m;
    return {};
}

void BoardWidget::drawArrow(QPainter& p, Square from, Square to, const QColor& color, qreal width) const
{
    const QPointF a = squareRect(from).center(), b = squareRect(to).center();
    const QLineF line(a, b);
    const qreal len = line.length();
    if (len < 1) return;
    const qreal head = width * 2.4, headW = width * 2.6;
    const qreal shaft = len - head - squareSize() * 0.12;

    QPainterPath path;
    path.moveTo(0, -width / 2);
    path.lineTo(shaft, -width / 2);
    path.lineTo(shaft, -headW / 2);
    path.lineTo(shaft + head, 0);
    path.lineTo(shaft, headW / 2);
    path.lineTo(shaft, width / 2);
    path.lineTo(0, width / 2);
    path.closeSubpath();

    QTransform t;
    t.translate(a.x(), a.y());
    t.rotate(-line.angle());
    p.fillPath(t.map(path), color);
}

void BoardWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF b = boardRect();
    const qreal sq = b.width() / 8.0;
    const qreal dpr = devicePixelRatioF();

    // Drop shadow around the board
    p.setPen(Qt::NoPen);
    for (int i = 1; i <= 8; ++i) {
        p.setBrush(QColor(0, 0, 0, 10));
        p.drawRoundedRect(b.adjusted(-i, -i + 3, i, i + 3), 4 + i, 4 + i);
    }

    // Squares
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(b, 3, 3);
    p.setClipPath(clip);
    p.setRenderHint(QPainter::Antialiasing, false);
    boardtheme::paint(p, b, devicePixelRatioF()); // board theme (Preferences)
    p.setRenderHint(QPainter::Antialiasing, true);

    // Last move
    if (!m_lastMove.isNull()) {
        QColor c = theme::Accent;
        c.setAlpha(95);
        p.fillRect(squareRect(m_lastMove.from), c);
        p.fillRect(squareRect(m_lastMove.to), c);
    }
    // Selected square
    if (m_selected != NoSquare) {
        QColor c = theme::Accent;
        c.setAlpha(150);
        p.fillRect(squareRect(m_selected), c);
    }
    // Check
    if (m_pos.inCheck()) {
        const QRectF r = squareRect(m_pos.kingSquare(m_pos.sideToMove()));
        QRadialGradient g(r.center(), sq * 0.62);
        g.setColorAt(0.0, QColor(255, 70, 60, 230));
        g.setColorAt(0.45, QColor(230, 40, 40, 150));
        g.setColorAt(1.0, QColor(200, 0, 0, 0));
        p.fillRect(r, g);
    }
    // Hover during drag and drop
    if (m_dragging && m_hover != NoSquare) {
        p.setPen(QPen(QColor(255, 255, 255, 170), std::max(2.0, sq * 0.045)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(squareRect(m_hover).adjusted(sq * 0.025, sq * 0.025, -sq * 0.025, -sq * 0.025));
    }
    // Marks (right-click)
    for (const Graphic& g : m_graphics) {
        if (g.isArrow()) continue;
        QColor c = graphicColor(g.color);
        c.setAlpha(210);
        p.setPen(QPen(c, sq * 0.07));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(squareRect(g.from).adjusted(sq * 0.07, sq * 0.07, -sq * 0.07, -sq * 0.07));
    }

    // Coordinates
    QFont cf = font();
    cf.setPixelSize(std::max(9, int(sq * 0.15)));
    cf.setWeight(QFont::DemiBold);
    p.setFont(cf);
    for (int i = 0; i < 8; ++i) {
        const Square fileSq = m_flipped ? makeSquare(7 - i, 7) : makeSquare(i, 0);
        const Square rankSq = m_flipped ? makeSquare(7, 7 - i) : makeSquare(0, 7 - i);
        const QRectF fr = squareRect(fileSq).adjusted(0, 0, -sq * 0.06, -sq * 0.03);
        const QRectF rr = squareRect(rankSq).adjusted(sq * 0.06, sq * 0.03, 0, 0);
        auto other = [](Square s) {
            return ((fileOf(s) + rankOf(s)) & 1) ? boardtheme::darkColor() : boardtheme::lightColor();
        };
        p.setPen(other(fileSq));
        p.drawText(fr, Qt::AlignRight | Qt::AlignBottom, QString(QChar('a' + fileOf(fileSq))));
        p.setPen(other(rankSq));
        p.drawText(rr, Qt::AlignLeft | Qt::AlignTop, QString(QChar('1' + rankOf(rankSq))));
    }

    // Pieces
    const int pieceSize = int(sq);
    for (int s = 0; s < 64; ++s) {
        const Piece pc = m_pos.at(Square(s));
        if (pc == NoPiece) continue;
        if (m_dragging && s == m_pressSquare) continue;
        if (m_animPiece != NoPiece && s == m_animTo) continue;
        p.drawPixmap(squareRect(Square(s)).topLeft(), m_pieces.pixmap(pc, pieceSize, dpr));
    }

    // Legal moves of the selected piece
    if (m_selected != NoSquare) {
        p.setPen(Qt::NoPen);
        for (const Move& m : m_legal) {
            if (m.from != m_selected || (m.promo != NoType && m.promo != Queen)) continue;
            const QRectF r = squareRect(m.to);
            // Assisted analysis: the mark takes the colour of the move's quality, and stands out more.
            const int quality = m_qualities.value(int(m.from) * 64 + int(m.to), 0);
            QColor mark = qualityColor(quality);
            if (m_pos.at(m.to) != NoPiece) {
                QPainterPath ring;
                ring.addRect(r);
                QPainterPath inner;
                inner.addEllipse(r.center(), sq * 0.5, sq * 0.5);
                mark.setAlpha(quality ? 170 : 70);
                p.fillPath(ring.subtracted(inner), mark);
            } else {
                mark.setAlpha(quality ? 200 : 75);
                p.setBrush(mark);
                p.drawEllipse(r.center(), sq * (quality ? 0.17 : 0.15), sq * (quality ? 0.17 : 0.15));
            }
        }
    }
    p.restore();

    // Arrows
    // One engine, one colour. Several engines proposing the same move: arrows thinner and
    // thinner on top of one another, so that every colour stays visible.
    QList<QPair<Move, QList<QColor>>> arrows;
    for (const auto& [m, color] : std::as_const(m_engineArrows)) {
        auto same = std::find_if(arrows.begin(), arrows.end(), [&](const auto& a) { return a.first == m; });
        if (same == arrows.end()) arrows.append({m, {color}});
        else same->second.append(color);
    }
    for (const auto& [m, colors] : std::as_const(arrows)) {
        for (int i = 0; i < colors.size(); ++i) {
            QColor c = colors[i];
            c.setAlpha(200);
            drawArrow(p, m.from, m.to, c, sq * 0.16 * (1.0 - 0.75 * i / colors.size()));
        }
    }
    for (const Graphic& g : m_graphics) {
        if (!g.isArrow()) continue;
        QColor c = graphicColor(g.color);
        c.setAlpha(200);
        drawArrow(p, g.from, g.to, c, sq * 0.16);
    }

    // Animated piece
    if (m_animPiece != NoPiece) {
        const QPointF a = squareRect(m_animFrom).topLeft(), c = squareRect(m_animTo).topLeft();
        p.drawPixmap(a + (c - a) * m_animT, m_pieces.pixmap(m_animPiece, pieceSize, dpr));
    }
    // Piece moved with the mouse
    if (m_dragging && m_pressSquare != NoSquare) {
        const int big = int(sq * 1.1);
        p.drawPixmap(m_dragPos - QPointF(big / 2.0, big / 2.0), m_pieces.pixmap(m_pos.at(m_pressSquare), big, dpr));
    }
}

void BoardWidget::mousePressEvent(QMouseEvent* e)
{
    const Square s = squareAt(e->position());
    if (e->button() == Qt::RightButton) {
        m_arrowFrom = s;
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    if (m_selected != NoSquare && s != NoSquare && isTarget(m_selected, s)) {
        tryMove(m_selected, s, false);
        return;
    }
    const Piece pc = s != NoSquare ? m_pos.at(s) : NoPiece;
    if (pc != NoPiece && colorOf(pc) == m_pos.sideToMove() && m_movable[colorOf(pc)]) {
        m_wasSelected = m_selected == s;
        m_selected = s;
        m_pressSquare = s;
        m_pressPos = e->position();
    } else {
        m_selected = NoSquare;
    }
    update();
}

void BoardWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_pressSquare == NoSquare || !(e->buttons() & Qt::LeftButton)) return;
    if (!m_dragging && (e->position() - m_pressPos).manhattanLength() > 4) m_dragging = true;
    if (m_dragging) {
        m_dragPos = e->position();
        m_hover = squareAt(e->position());
        update();
    }
}

void BoardWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::RightButton) {
        const Square to = squareAt(e->position());
        if (m_arrowFrom != NoSquare && to != NoSquare) {
            // Colour depending on the keys; the same gesture in the same colour removes, in another one recolours.
            const bool shift = e->modifiers() & Qt::ShiftModifier, ctrl = e->modifiers() & Qt::ControlModifier;
            Graphic g;
            g.from = m_arrowFrom;
            g.to = to == m_arrowFrom ? NoSquare : to;
            g.color = shift && ctrl ? 'B' : shift ? 'R' : ctrl ? 'Y' : 'G';
            const auto same = std::find_if(m_graphics.begin(), m_graphics.end(), [&](const Graphic& x) { return x.from == g.from && x.to == g.to; });
            if (same == m_graphics.end()) m_graphics.push_back(g);
            else if (same->color == g.color) m_graphics.erase(same);
            else same->color = g.color;
            emit graphicsChanged(m_graphics);
        }
        m_arrowFrom = NoSquare;
        update();
        return;
    }
    if (e->button() != Qt::LeftButton || m_pressSquare == NoSquare) return;

    const Square from = m_pressSquare;
    m_pressSquare = NoSquare;
    if (m_dragging) {
        m_dragging = false;
        m_hover = NoSquare;
        const Square to = squareAt(e->position());
        // Piece put back on its square (or dropped elsewhere with no possible move): the selection
        // disappears, the square gets its colour back and the target squares are cleared.
        m_selected = NoSquare;
        if (to != NoSquare && to != from && isTarget(from, to)) tryMove(from, to, true);
    } else if (m_wasSelected) {
        m_selected = NoSquare;
    }
    update();
}

void BoardWidget::wheelEvent(QWheelEvent* e)
{
    m_wheelAccum += e->angleDelta().y();
    const int steps = m_wheelAccum / 120; // one wheel notch = 120
    m_wheelAccum -= steps * 120;
    if (steps) emit wheelSteps(-steps); // wheel down (negative delta) = next move
    e->accept();
}

void BoardWidget::leaveEvent(QEvent*)
{
    m_hover = NoSquare;
    update();
}

void BoardWidget::tryMove(Square from, Square to, bool fromDrag)
{
    std::vector<Move> candidates;
    for (const Move& m : m_legal)
        if (m.from == from && m.to == to) candidates.push_back(m);
    if (candidates.empty()) {
        const Move castle = resolve(from, to); // king dropped on its castling target square
        if (castle.isNull()) return;
        candidates.push_back(castle);
    }

    Move chosen = candidates.front();
    if (candidates.size() > 1) { // promotion
        QMenu menu(this);
        const qreal dpr = devicePixelRatioF();
        const Color c = m_pos.sideToMove();
        const std::pair<PieceType, QString> choices[] = {
            {Queen, tr("Dame")}, {Rook, tr("Tour")}, {Bishop, tr("Fou")}, {Knight, tr("Cavalier")}};
        for (const auto& [t, label] : choices) {
            QAction* a = menu.addAction(QIcon(m_pieces.pixmap(makePiece(c, t), 32, dpr)), label);
            a->setData(int(t));
        }
        QAction* picked = menu.exec(mapToGlobal(squareRect(to).center().toPoint()));
        if (!picked) {
            m_selected = NoSquare;
            update();
            return;
        }
        chosen.promo = PieceType(picked->data().toInt());
    }
    m_selected = NoSquare;
    m_suppressAnimation = fromDrag;
    emit moveRequested(chosen);
}
