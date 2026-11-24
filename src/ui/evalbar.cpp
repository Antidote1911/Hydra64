#include "evalbar.h"

#include "engine/uciengine.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include <cmath>

EvalBar::EvalBar(QWidget* parent)
    : QWidget(parent)
    , m_anim(new QVariantAnimation(this))
{
    setFixedWidth(22);
    m_anim->setDuration(300);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_share = v.toReal();
        update();
    });
}

qreal EvalBar::whiteShare(const chess::Evaluation& e)
{
    if (!e.valid) return 0.5;
    if (e.mate) return e.value > 0 ? 1.0 : (e.value < 0 ? 0.0 : 0.5);
    // Logistic curve: ±4 pawns ≈ 92%
    return 1.0 / (1.0 + std::exp(-0.0062 * e.value));
}

QString EvalBar::format(const chess::Evaluation& e)
{
    if (!e.valid) return QStringLiteral("–");
    if (e.mate) return (e.value >= 0 ? QStringLiteral("M") : QStringLiteral("−M")) + QString::number(std::abs(e.value));
    // Win or loss read from the endgame tablebases ("+200.00" would say nothing)
    if (std::abs(e.value) >= TablebaseScore) return e.value > 0 ? tr("+Tables") : tr("−Tables");
    const double v = e.value / 100.0;
    if (std::abs(v) < 0.005) return QStringLiteral("0.00");
    return (v > 0 ? QStringLiteral("+") : QStringLiteral("−")) + QString::number(std::abs(v), 'f', 2);
}

void EvalBar::setEvaluation(const chess::Evaluation& eval)
{
    m_eval = eval;
    m_anim->stop();
    m_anim->setStartValue(m_share);
    m_anim->setEndValue(whiteShare(eval));
    m_anim->start();
}

void EvalBar::setFlipped(bool flipped)
{
    m_flipped = flipped;
    update();
}

void EvalBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Same height as the board (BoardWidget's 10 px margin)
    const QRectF r = QRectF(rect()).adjusted(3, 10, -3, -10);
    QPainterPath clip;
    clip.addRoundedRect(r, 3, 3);
    p.setClipPath(clip);

    p.fillRect(r, QColor(0x16, 0x16, 0x18));
    const qreal h = r.height() * m_share;
    QRectF white = m_flipped ? QRectF(r.left(), r.top(), r.width(), h)
                             : QRectF(r.left(), r.bottom() - h, r.width(), h);
    QLinearGradient g(white.topLeft(), white.topRight());
    g.setColorAt(0, QColor(0xf2, 0xf0, 0xea));
    g.setColorAt(1, QColor(0xd2, 0xcf, 0xc6));
    p.fillRect(white, g);

    // Middle marker
    p.fillRect(QRectF(r.left(), r.center().y() - 0.5, r.width(), 1), QColor(theme::Accent.red(), theme::Accent.green(), theme::Accent.blue(), 160));

    if (m_eval.valid) {
        QFont f = font();
        f.setPixelSize(8);
        f.setWeight(QFont::Bold);
        p.setFont(f);
        const bool whiteAhead = m_share >= 0.5;
        const QString text = m_eval.mate ? QStringLiteral("M%1").arg(std::abs(m_eval.value))
                             : std::abs(m_eval.value) >= TablebaseScore ? QStringLiteral("T")
                                         : QString::number(std::min(99.0, std::abs(m_eval.value) / 100.0), 'f', 1);
        const bool atBottom = whiteAhead != m_flipped;
        const QRectF tr = atBottom ? QRectF(r.left(), r.bottom() - 18, r.width(), 16) : QRectF(r.left(), r.top() + 2, r.width(), 16);
        p.setPen(whiteAhead ? QColor(0x22, 0x22, 0x26) : QColor(0xd6, 0xd6, 0xda));
        p.drawText(tr, Qt::AlignCenter, text);
    }
}
