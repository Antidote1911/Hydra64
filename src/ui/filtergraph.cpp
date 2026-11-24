#include "filtergraph.h"

#include "theme.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace {
const QColor WhiteWin{0xd8, 0xd8, 0xdc};
const QColor DrawColor{0x8e, 0x8e, 0x96};
const QColor BlackWin{0x4a, 0x4a, 0x52};
const QColor UnknownColor{0x33, 0x33, 0x38};

void add(FilterGraphDialog::Bucket& b, int result)
{
    if (result == 1) ++b.white;
    else if (result == 0) ++b.draw;
    else if (result == -1) ++b.black;
    else ++b.unknown;
}
} // namespace

std::vector<FilterGraphDialog::Bucket> FilterGraphDialog::byYear(const std::vector<IndexedGameSource::Summary>& games, int* missing, int step)
{
    int lo = 99999, hi = 0, none = 0;
    for (const auto& g : games) {
        if (g.year < 1000 || g.year > 2999) {
            ++none;
            continue;
        }
        lo = std::min(lo, g.year);
        hi = std::max(hi, g.year);
    }
    if (missing) *missing = none;
    std::vector<Bucket> out;
    if (hi < lo) return out;
    if (step <= 0) { // about thirty bars at most
        const int span = hi - lo + 1;
        step = span <= 30 ? 1 : span <= 60 ? 2 : span <= 150 ? 5 : span <= 300 ? 10 : 25;
    }
    const int first = lo - lo % step;
    for (int y = first; y <= hi; y += step) out.push_back({y, y + step - 1});
    for (const auto& g : games)
        if (g.year >= 1000 && g.year <= 2999) add(out[size_t((g.year - first) / step)], g.result);
    return out;
}

std::vector<FilterGraphDialog::Bucket> FilterGraphDialog::byElo(const std::vector<IndexedGameSource::Summary>& games, int* missing)
{
    std::map<int, Bucket> m;
    int none = 0;
    for (const auto& g : games) {
        if (g.whiteElo <= 0 || g.blackElo <= 0) {
            ++none;
            continue;
        }
        const int avg = (g.whiteElo + g.blackElo) / 2;
        const int from = std::clamp(avg - avg % 100, 1000, 2900);
        Bucket& b = m[from];
        b.from = from;
        b.to = from + 99;
        add(b, g.result);
    }
    if (missing) *missing = none;
    std::vector<Bucket> out;
    if (m.empty()) return out;
    for (int e = m.begin()->first; e <= m.rbegin()->first; e += 100) { // empty slices included
        auto it = m.find(e);
        out.push_back(it != m.end() ? it->second : Bucket{e, e + 99});
    }
    return out;
}

FilterGraphDialog::FilterGraphDialog(const std::vector<IndexedGameSource::Summary>& games, int baseTotal, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Graphiques du filtre"));
    int noYear = 0, noElo = 0;
    const auto years = byYear(games, &noYear);
    const auto elos = byElo(games, &noElo);
    auto* tabs = new QTabWidget;
    tabs->addTab(new BucketChart(years, tr("années")), tr("Par année"));
    tabs->addTab(new BucketChart(elos, tr("Elo moyen")), tr("Par Elo moyen"));
    int w = 0, d = 0, b = 0;
    for (const auto& g : games) w += g.result == 1, d += g.result == 0, b += g.result == -1;
    const int decided = w + d + b;
    QString text = tr("%n partie(s) affichée(s)", nullptr, int(games.size()));
    if (baseTotal > 0) text += tr(" sur %1 (%2 %)").arg(baseTotal).arg(100.0 * double(games.size()) / baseTotal, 0, 'f', 1);
    if (decided)
        text += tr(" · Blancs +%1 =%2 −%3, score %4 %").arg(w).arg(d).arg(b).arg(100.0 * (w + 0.5 * d) / decided, 0, 'f', 1);
    if (noYear || noElo) text += tr(" · sans année : %1, sans Elo : %2").arg(noYear).arg(noElo);
    auto* summary = new QLabel(text);
    summary->setWordWrap(true);
    auto* legend = new QLabel(tr("<span style='color:%1'>■</span> gains des Blancs &nbsp; <span style='color:%2'>■</span> nulles &nbsp; "
                                 "<span style='color:%3'>■</span> gains des Noirs &nbsp; <span style='color:%4'>—</span> score des Blancs")
                                  .arg(WhiteWin.name(), DrawColor.name(), BlackWin.name(), theme::Accent.name()));
    legend->setObjectName("Faint");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(summary);
    layout->addWidget(tabs, 1);
    layout->addWidget(legend);
    layout->addWidget(buttons);
}

// ---------------------------------------------------------------- chart

BucketChart::BucketChart(std::vector<FilterGraphDialog::Bucket> buckets, QString unit, QWidget* parent)
    : QWidget(parent)
    , m_buckets(std::move(buckets))
    , m_unit(std::move(unit))
{
    setMouseTracking(true);
    setMinimumSize(420, 240);
}

QRectF BucketChart::plotRect() const { return QRectF(52, 14, width() - 70, height() - 56); }

int BucketChart::bucketAt(QPointF p) const
{
    const QRectF r = plotRect();
    if (m_buckets.empty() || !r.contains(p)) return -1;
    return std::clamp(int((p.x() - r.left()) / r.width() * double(m_buckets.size())), 0, int(m_buckets.size()) - 1);
}

void BucketChart::mouseMoveEvent(QMouseEvent* e)
{
    const int i = bucketAt(e->position());
    if (i != m_hover) {
        m_hover = i;
        update();
    }
    if (i < 0) {
        QToolTip::hideText();
        return;
    }
    const auto& b = m_buckets[size_t(i)];
    const int decided = b.white + b.draw + b.black;
    QString t = (b.from == b.to ? QString::number(b.from) : QStringLiteral("%1–%2").arg(b.from).arg(b.to)) + QStringLiteral("\n");
    t += tr("%n partie(s)", nullptr, b.total());
    t += tr("\n+%1 =%2 −%3").arg(b.white).arg(b.draw).arg(b.black);
    if (decided) t += tr("\nscore des Blancs : %1 %").arg(100.0 * (b.white + 0.5 * b.draw) / decided, 0, 'f', 1);
    QToolTip::showText(e->globalPosition().toPoint(), t, this);
}

void BucketChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::Panel);
    const QRectF r = plotRect();
    if (m_buckets.empty()) {
        p.setPen(theme::TextDim);
        p.drawText(rect(), Qt::AlignCenter, tr("Aucune partie avec %1").arg(m_unit));
        return;
    }
    int top = 1;
    for (const auto& b : m_buckets) top = std::max(top, b.total());
    // "Round" gridline
    // "Round" step (1, 2 or 5 × 10ⁿ): six gridlines at most
    double step = std::pow(10.0, std::floor(std::log10(std::max(1.0, top / 5.0))));
    for (double k : {1.0, 2.0, 5.0, 10.0})
        if (top / (step * k) <= 6) {
            step *= k;
            break;
        }
    const double maxY = std::ceil(top / step) * step;
    p.setPen(theme::BorderSoft);
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    for (double v = 0; v <= maxY + 0.5; v += step) {
        const double y = r.bottom() - v / maxY * r.height();
        p.setPen(theme::BorderSoft);
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(theme::TextDim);
        p.drawText(QRectF(0, y - 8, r.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(qint64(v)));
    }
    const double bw = r.width() / double(m_buckets.size());
    const int labelEvery = std::max(1, int(std::ceil(60.0 / bw)));
    QPolygonF score;
    for (size_t i = 0; i < m_buckets.size(); ++i) {
        const auto& b = m_buckets[i];
        const double x = r.left() + bw * double(i) + bw * 0.12, w = bw * 0.76;
        double y = r.bottom();
        const std::pair<int, QColor> parts[] = {{b.white, WhiteWin}, {b.draw, DrawColor}, {b.black, BlackWin}, {b.unknown, UnknownColor}};
        for (const auto& [n, c] : parts) {
            if (!n) continue;
            const double h = n / maxY * r.height();
            p.fillRect(QRectF(x, y - h, w, h), int(i) == m_hover ? c.lighter(115) : c);
            y -= h;
        }
        const int decided = b.white + b.draw + b.black;
        if (decided) score << QPointF(x + w / 2, r.bottom() - (b.white + 0.5 * b.draw) / decided * r.height());
        if (int(i) % labelEvery == 0) {
            p.setPen(theme::TextDim);
            p.drawText(QRectF(x - bw, r.bottom() + 4, w + 2 * bw, 16), Qt::AlignHCenter | Qt::AlignTop, QString::number(b.from));
        }
    }
    // White's score (0–100% scale over the height)
    if (score.size() > 1) {
        p.setPen(QPen(theme::Accent, 1.6));
        p.drawPolyline(score);
    }
    for (const QPointF& pt : std::as_const(score)) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme::Accent);
        p.drawEllipse(pt, 2.2, 2.2);
    }
    p.setPen(theme::TextFaint);
    p.drawText(QRectF(r.left(), r.bottom() + 22, r.width(), 16), Qt::AlignCenter, m_unit);
}
