#include "clockpanel.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>

ClockPanel::ClockPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("PanelBody");
    setMinimumHeight(56);
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(12, 4, 12, 4);
    l->setSpacing(10);
    for (int side = 0; side < 2; ++side) {
        m_mark[side] = new QLabel;
        m_mark[side]->setFixedSize(14, 14);
        m_mark[side]->setObjectName(side == 0 ? "ClockMarkWhite" : "ClockMarkBlack");
        m_mark[side]->setToolTip(side == 0 ? tr("Blancs") : tr("Noirs"));
        m_time[side] = new QLabel(QStringLiteral("–"));
        m_time[side]->setObjectName("BigClock");
        m_time[side]->setAlignment(Qt::AlignCenter);
        m_time[side]->setMinimumWidth(120);
        m_time[side]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_time[side]->setToolTip(side == 0 ? tr("Pendule des Blancs") : tr("Pendule des Noirs"));
    }
    l->addWidget(m_mark[0], 0, Qt::AlignVCenter);
    l->addWidget(m_time[0], 1);
    l->addWidget(m_time[1], 1);
    l->addWidget(m_mark[1], 0, Qt::AlignVCenter);
}

QString ClockPanel::format(qint64 ms)
{
    if (ms < 0) return QStringLiteral("–");
    const qint64 s = ms / 1000;
    QString t = QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    if (ms < 10000) t += QStringLiteral(".%1").arg((ms / 100) % 10); // tenths in the last seconds
    return t;
}

void ClockPanel::setClocks(qint64 whiteMs, qint64 blackMs, int runningSide, bool live)
{
    const qint64 ms[2] = {whiteMs, blackMs};
    // Replay of a game without recorded times: the displays stay empty, and say why.
    const bool noTimes = !live && whiteMs < 0 && blackMs < 0;
    for (int side = 0; side < 2; ++side) {
        m_time[side]->setText(format(ms[side]));
        m_time[side]->setToolTip(noTimes ? tr("Pas de temps enregistré dans cette partie : la pendule ne suit que les parties en cours et celles qui notent le temps à chaque coup ([%clk])")
                                         : side == 0 ? tr("Pendule des Blancs") : tr("Pendule des Noirs"));
        const bool active = runningSide == side;
        const bool low = live && ms[side] >= 0 && ms[side] < 10000;
        for (QWidget* w : {static_cast<QWidget*>(m_time[side]), static_cast<QWidget*>(m_mark[side])}) {
            if (w->property("active").toBool() == active && w->property("low").toBool() == low) continue;
            w->setProperty("active", active);
            w->setProperty("low", low);
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
    }
}

QString ClockPanel::timeControlLabel(const QString& tc)
{
    const QStringList parts = tc.split('+');
    bool ok = false;
    const int base = parts.value(0).toInt(&ok);
    if (!ok || base <= 0) return {};
    const int inc = parts.value(1).toInt();
    // Category by the estimated duration of a 40-move game (base + 40 × increment).
    const int estimate = base + 40 * inc;
    const QString kind = estimate < 180 ? tr("Bullet") : estimate < 600 ? tr("Blitz") : estimate < 3600 ? tr("Rapide") : tr("Classique");
    const QString minutes = base % 60 == 0 ? QString::number(base / 60) : QString::number(base / 60.0, 'g', 3);
    return QStringLiteral("%1 %2+%3").arg(kind, minutes).arg(inc);
}
