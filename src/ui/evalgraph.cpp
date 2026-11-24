#include "evalgraph.h"
#include "evalbar.h"
#include "gamesession.h"
#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace chess;

EvalGraph::EvalGraph(GameSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    setMouseTracking(true);
    setMinimumHeight(80);
    auto refresh = [this] { update(); };
    connect(session, &GameSession::currentChanged, this, refresh);
    connect(session, &GameSession::treeChanged, this, refresh);
    connect(session, &GameSession::evaluationsChanged, this, refresh);
}

QRectF EvalGraph::plotRect() const { return QRectF(rect()).adjusted(12, 12, -12, -18); }

int EvalGraph::plyAt(qreal x) const
{
    const int n = int(m_session->game().mainline().size());
    if (n == 0) return -1;
    const QRectF r = plotRect();
    const qreal t = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
    return std::clamp(int(std::round(t * n)), 0, n);
}

void EvalGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::Panel);

    const QRectF r = plotRect();
    const auto line = m_session->game().mainline();
    const int n = int(line.size());

    // Background: very discreet white / black half-planes
    p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() / 2), QColor(255, 255, 255, 10));
    p.fillRect(QRectF(r.left(), r.center().y(), r.width(), r.height() / 2), QColor(0, 0, 0, 40));

    // Horizontal gridlines
    p.setPen(QPen(theme::BorderSoft, 1, Qt::DotLine));
    for (qreal share : {0.25, 0.75}) p.drawLine(QPointF(r.left(), r.bottom() - share * r.height()), QPointF(r.right(), r.bottom() - share * r.height()));
    p.setPen(QPen(theme::TextFaint, 1));
    p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));

    if (n == 0) {
        p.setPen(theme::TextFaint);
        p.drawText(rect(), Qt::AlignCenter, tr("Aucun coup — l'évaluation s'affiche pendant l'analyse"));
        return;
    }

    auto xOf = [&](int ply) { return r.left() + r.width() * ply / qreal(n); };
    auto yOf = [&](const Evaluation& e) { return r.bottom() - EvalBar::whiteShare(e) * r.height(); };

    // Evaluated points (the root counts as ply 0)
    QVector<QPointF> pts;
    const Node* root = m_session->game().root();
    if (root->eval.valid) pts.append({xOf(0), yOf(root->eval)});
    for (int i = 0; i < n; ++i)
        if (line[i]->eval.valid) pts.append({xOf(i + 1), yOf(line[i]->eval)});

    if (pts.size() >= 2) {
        QPainterPath curve;
        curve.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i) curve.lineTo(pts[i]);

        // Fill relative to the central axis
        QPainterPath area = curve;
        area.lineTo(pts.last().x(), r.center().y());
        area.lineTo(pts.first().x(), r.center().y());
        area.closeSubpath();
        p.save();
        p.setClipRect(QRectF(r.left(), r.top(), r.width(), r.height() / 2));
        p.fillPath(area, QColor(0xf0, 0xee, 0xe8, 150));
        p.restore();
        p.save();
        p.setClipRect(QRectF(r.left(), r.center().y(), r.width(), r.height() / 2));
        p.fillPath(area, QColor(0x0c, 0x0c, 0x0e, 200));
        p.restore();

        p.setPen(QPen(theme::Accent, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve);
    }

    // Mistakes flagged by NAGs
    for (int i = 0; i < n; ++i) {
        for (int nag : line[i]->nags) {
            QColor c;
            if (nag == 4) c = theme::Bad;
            else if (nag == 2) c = theme::Accent;
            else if (nag == 6) c = QColor(0xe0, 0xc0, 0x50);
            else continue;
            const qreal y = line[i]->eval.valid ? yOf(line[i]->eval) : r.center().y();
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(QPointF(xOf(i + 1), y), 3.2, 3.2);
        }
    }

    // Current move and hover
    int currentPly = -1;
    for (int i = 0; i < n; ++i)
        if (line[i] == m_session->current()) currentPly = i + 1;
    if (m_session->current() == root) currentPly = 0;

    if (m_hoverPly >= 0) {
        p.setPen(QPen(QColor(255, 255, 255, 50), 1));
        p.drawLine(QPointF(xOf(m_hoverPly), r.top()), QPointF(xOf(m_hoverPly), r.bottom()));
    }
    if (currentPly >= 0) {
        const qreal x = xOf(currentPly);
        p.setPen(QPen(theme::Accent, 1.4));
        p.drawLine(QPointF(x, r.top() - 4), QPointF(x, r.bottom() + 4));
        // small playhead, like a timeline
        QPainterPath head;
        head.moveTo(x - 5, r.top() - 10);
        head.lineTo(x + 5, r.top() - 10);
        head.lineTo(x, r.top() - 4);
        head.closeSubpath();
        p.fillPath(head, theme::Accent);
    }

    // Move numbers
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(theme::TextFaint);
    const int moveStep = n > 100 ? 10 : 5;
    for (int mv = moveStep; mv * 2 - 1 <= n; mv += moveStep)
        p.drawText(QRectF(xOf(mv * 2 - 1) - 20, r.bottom() + 2, 40, 14), Qt::AlignCenter, QString::number(mv));
}

void EvalGraph::mousePressEvent(QMouseEvent* e)
{
    const int ply = plyAt(e->position().x());
    if (ply < 0) return;
    if (ply == 0) m_session->toStart();
    else m_session->goTo(m_session->game().mainline()[ply - 1]);
}

void EvalGraph::mouseMoveEvent(QMouseEvent* e)
{
    m_hoverPly = plyAt(e->position().x());
    if (e->buttons() & Qt::LeftButton) mousePressEvent(e);
    update();
}

void EvalGraph::leaveEvent(QEvent*)
{
    m_hoverPly = -1;
    update();
}
