#include "playerreport.h"

#include "openings.h"
#include "playerencyclopedia.h"
#include "theme.h"

#include <QDate>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace {
QString scoreCells(const hy64db::PlayerStats::Score& s)
{
    return QStringLiteral("<td align=right>%1</td><td align=right>+%2</td><td align=right>=%3</td><td align=right>−%4</td><td align=right>%5 %</td>")
        .arg(s.games())
        .arg(s.wins)
        .arg(s.draws)
        .arg(s.losses)
        .arg(QLocale().toString(s.percent(), 'f', 1));
}
} // namespace

PlayerReportDialog::PlayerReportDialog(const hy64db::Database& db, const QString& baseName, const QString& player, QWidget* parent)
    : QDialog(parent)
    , m_db(db)
    , m_baseName(baseName)
{
    setWindowTitle(tr("Fiche du joueur"));
    resize(900, 680);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Joueur : début d'un mot du nom…"));
    m_name->setClearButtonEnabled(true);
    m_matches = new QListWidget;
    m_matches->setMaximumWidth(280);
    m_report = new QTextBrowser;
    m_report->setOpenLinks(false);
    m_games = new QPushButton(theme::icon("database"), tr("Ses parties"));
    m_games->setToolTip(tr("Afficher les parties du joueur dans la liste"));
    m_export = new QPushButton(theme::icon("save"), tr("Exporter (HTML)…"));
    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(m_name);
    ll->addWidget(m_matches, 1);
    auto* split = new QSplitter;
    split->addWidget(left);
    split->addWidget(m_report);
    split->setChildrenCollapsible(false); // neither half disappears when the splitter is dragged
    split->setStretchFactor(1, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->addButton(m_games, QDialogButtonBox::ActionRole);
    buttons->addButton(m_export, QDialogButtonBox::ActionRole);
    m_prepare = new QPushButton(theme::icon("book"), tr("Se préparer contre lui…"));
    m_prepare->setToolTip(tr("Ce qu'il joue avec chaque couleur, les lignes où il marque le moins, ses défaites"));
    buttons->addButton(m_prepare, QDialogButtonBox::ActionRole);
    m_prepare->setEnabled(false);
    connect(m_prepare, &QPushButton::clicked, this, [this] {
        if (!m_player.isEmpty()) emit preparationRequested(m_player);
    });
    m_encyclopedia = new QLabel;
    m_encyclopedia->setObjectName("EncyclopediaStatus");
    updateEncyclopediaStatus();
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(split, 1);
    auto* bottom = new QHBoxLayout; // state of the encyclopedia on the left, buttons on the right
    bottom->addWidget(m_encyclopedia);
    bottom->addStretch(1);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, &PlayerReportDialog::search);
    connect(m_matches, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (it) showPlayer(it->data(Qt::UserRole).toString());
    });
    connect(m_games, &QPushButton::clicked, this, [this] {
        if (!m_stats.games.empty()) emit gamesRequested(m_stats.games, m_player);
    });
    connect(m_export, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exporter la fiche"),
                                                          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + m_player + QStringLiteral(".html"),
                                                          tr("Page HTML (*.html)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(html().toUtf8());
    });
    m_games->setEnabled(false);
    m_export->setEnabled(false);
    if (!player.isEmpty()) {
        m_name->setText(player);
        showPlayer(player);
    } else {
        m_report->setHtml(welcomeHtml());
    }
}

void PlayerReportDialog::updateEncyclopediaStatus()
{
    if (const hy64db::PlayerBase* base = playerEncyclopedia::get()) {
        m_encyclopedia->setText(tr("Encyclopédie des joueurs : %1 fiches").arg(QLocale().toString(qulonglong(base->count()))));
        m_encyclopedia->setToolTip(playerEncyclopedia::directory());
    } else {
        m_encyclopedia->setText(tr("Encyclopédie des joueurs : non chargée"));
        m_encyclopedia->setToolTip(tr("Son dossier se choisit dans Préférences → Bases."));
    }
}

QString PlayerReportDialog::welcomeHtml() const
{
    QString h = tr("<p>Cherchez un joueur à gauche.</p>");
    if (const hy64db::PlayerBase* base = playerEncyclopedia::get())
        h += tr("<p>Encyclopédie des joueurs ouverte : %1 fiches. La fiche d'un joueur qui y figure affiche sa photo, son titre, sa fédération et ses Elo FIDE.</p>").arg(QLocale().toString(qulonglong(base->count())));
    return h;
}

void PlayerReportDialog::search()
{
    m_matches->clear();
    const QString text = m_name->text().trimmed();
    if (text.isEmpty()) return;
    if (m_usage.empty()) m_usage = m_db.nameUsage(hy64db::Player);
    std::vector<uint32_t> ids = m_db.names().findPrefix(hy64db::Player, text.toStdString());
    std::stable_sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) {
        return (a < m_usage.size() ? m_usage[a] : 0) > (b < m_usage.size() ? m_usage[b] : 0);
    });
    int shown = 0;
    for (uint32_t id : ids) {
        if (id >= m_usage.size() || m_usage[id] == 0) continue;
        const QString name = QString::fromStdString(m_db.names().name(hy64db::Player, id));
        auto* it = new QListWidgetItem(tr("%1  (%n partie(s))", nullptr, int(m_usage[id])).arg(name));
        it->setData(Qt::UserRole, name);
        m_matches->addItem(it);
        if (++shown >= 300) break;
    }
}

bool PlayerReportDialog::showPlayer(const QString& name)
{
    const uint32_t id = m_db.names().find(hy64db::Player, name.toStdString());
    if (id == hy64db::NoName) return false;
    m_player = name;
    m_stats = m_db.playerStats(id);
    loadEncyclopedia();
    m_report->document()->addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("elo")), eloChart());
    m_report->document()->addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("fide")), fideChart());
    m_report->setHtml(html());
    m_games->setEnabled(!m_stats.games.empty());
    m_export->setEnabled(true);
    m_prepare->setEnabled(!m_stats.games.empty());
    return true;
}

void PlayerReportDialog::loadEncyclopedia()
{
    updateEncyclopediaStatus(); // it may have been chosen or removed in the Preferences
    m_known = false;
    m_info = {};
    m_ratings.clear();
    m_photos.clear();
    const hy64db::PlayerBase* base = playerEncyclopedia::get();
    if (!base) return;
    const auto found = base->find(m_player.toStdString());
    if (found.empty()) return;
    m_known = true;
    m_info = found.front();
    m_ratings = base->ratings(m_info.id);
    for (auto& ph : base->photos(m_info))
        if (!ph.path.empty()) m_photos.push_back(std::move(ph));
}

// Elo curve: points (abscissa in years, Elo).
static QImage drawEloCurve(const std::vector<std::pair<double, int>>& points)
{
    QImage img(560, 180, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(0x1d, 0x1d, 0x21));
    if (points.size() < 2) return img;
    int lo = points.front().second, hi = lo;
    for (const auto& p : points) {
        lo = std::min(lo, p.second);
        hi = std::max(hi, p.second);
    }
    lo = (lo / 50) * 50;
    hi = (hi / 50 + 1) * 50;
    const double x0 = points.front().first, x1 = std::max(points.back().first, x0 + 1);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF area(48, 12, img.width() - 60, img.height() - 36);
    p.setPen(QColor(0x5c, 0x5c, 0x64));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    for (int e = lo; e <= hi; e += std::max(50, (hi - lo) / 4 / 50 * 50)) {
        const qreal y = area.bottom() - area.height() * (e - lo) / std::max(1, hi - lo);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.drawText(QRectF(0, y - 7, 44, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(e));
    }
    p.drawText(QRectF(area.left(), area.bottom() + 4, 60, 14), Qt::AlignLeft, QString::number(int(x0)));
    p.drawText(QRectF(area.right() - 60, area.bottom() + 4, 60, 14), Qt::AlignRight, QString::number(int(x1)));
    QPainterPath path;
    for (size_t i = 0; i < points.size(); ++i) {
        const QPointF pt(area.left() + area.width() * (points[i].first - x0) / (x1 - x0), area.bottom() - area.height() * (points[i].second - lo) / std::max(1, hi - lo));
        if (i == 0) path.moveTo(pt);
        else path.lineTo(pt);
    }
    p.setPen(QPen(QColor(0xe6, 0x8a, 0x35), 2));
    p.drawPath(path);
    return img;
}

QImage PlayerReportDialog::fideChart() const
{
    std::vector<std::pair<double, int>> points;
    for (const auto& r : m_ratings) points.push_back({r.date.year + (std::max(1, r.date.month) - 1) / 12.0, r.elo});
    return drawEloCurve(points);
}

QImage PlayerReportDialog::eloChart() const
{
    // Highest Elo of each year, according to the games of the database
    std::vector<std::pair<double, int>> points;
    for (const auto& y : m_stats.years)
        if (y.maxElo > 0) points.push_back({double(y.year), y.maxElo});
    return drawEloCurve(points);
}

QString PlayerReportDialog::html() const
{
    const hy64db::PlayerStats& s = m_stats;
    const QLocale loc;
    hy64db::PlayerStats::Score all{s.asWhite.wins + s.asBlack.wins, s.asWhite.draws + s.asBlack.draws, s.asWhite.losses + s.asBlack.losses};
    QString h = QStringLiteral("<h2>%1</h2>").arg(m_player.toHtmlEscaped());
    if (m_known) { // players encyclopedia: photo, personal details, Elo from the FIDE lists
        auto dateText = [&](const hy64db::PlayerBase::Date& d) {
            if (d.month && d.day) return loc.toString(QDate(d.year, d.month, d.day), QStringLiteral("d MMMM yyyy"));
            return QString::number(d.year);
        };
        QStringList facts;
        if (const QString full = QString::fromStdString(m_info.name()); full.compare(m_player, Qt::CaseInsensitive) != 0) facts << full; // full name if it differs
        QStringList titles;
        if (!m_info.title.empty()) titles << QString::fromStdString(m_info.title);
        if (!m_info.womenTitle.empty() && m_info.title != "GM") titles << QString::fromStdString(m_info.womenTitle);
        if (m_info.reigningChampion) titles << (m_info.male ? tr("champion du monde en titre") : tr("championne du monde en titre"));
        else if (m_info.worldChampion) titles << tr("ancien champion du monde");
        else if (m_info.womenWorldChampion) titles << tr("ancienne championne du monde");
        if (!titles.isEmpty()) facts << titles.join(QStringLiteral(", "));
        if (!m_info.nation.empty()) {
            QString n = QString::fromStdString(m_info.nation);
            if (!m_info.formerNation.empty() && m_info.formerNation != m_info.nation) n += tr(" (auparavant %1)").arg(QString::fromStdString(m_info.formerNation));
            facts << n;
        }
        if (m_info.birth.known()) {
            facts << (m_info.death.known() ? tr("%1 – %2").arg(dateText(m_info.birth), dateText(m_info.death))
                                           : (m_info.male ? tr("né en %1") : tr("née en %1")).arg(dateText(m_info.birth)));
        }
        QString card;
        if (!m_photos.empty()) {
            // Portrait: the most recent photo; then a few others, small
            const auto& main = m_photos.back();
            card += QStringLiteral("<td valign=top><img src=\"%1\" height=220></td>").arg(QUrl::fromLocalFile(QString::fromStdString(main.path)).toString());
        }
        QString text = QStringLiteral("<p><b>%1</b></p>").arg(facts.join(QStringLiteral(" · ")).toHtmlEscaped());
        if (!m_ratings.empty()) {
            const auto peak = std::max_element(m_ratings.begin(), m_ratings.end(), [](const auto& a, const auto& b) { return a.elo < b.elo; });
            const auto& last = m_ratings.back();
            auto listDate = [&](const hy64db::PlayerBase::Date& d) { return loc.toString(QDate(d.year, std::max(1, d.month), 1), QStringLiteral("MMMM yyyy")); };
            text += tr("<p>Elo FIDE : %1 (%2)").arg(last.elo).arg(listDate(last.date));
            if (last.rank > 0) text += tr(", n° %1 mondial").arg(last.rank);
            text += tr("<br>Meilleur Elo : %1 (%2)").arg(peak->elo).arg(listDate(peak->date));
            int bestRank = 0;
            hy64db::PlayerBase::Date bestRankDate;
            for (const auto& r : m_ratings)
                if (r.rank > 0 && (bestRank == 0 || r.rank < bestRank)) bestRank = r.rank, bestRankDate = r.date;
            if (bestRank) text += tr("<br>Meilleur rang mondial : n° %1 (%2)").arg(bestRank).arg(listDate(bestRankDate));
            text += QStringLiteral("</p>");
        }
        if (!m_photos.empty()) {
            const auto& main = m_photos.back();
            QStringList credit;
            if (!main.place.empty()) credit << QString::fromStdString(main.place);
            if (main.year) credit << QString::number(main.year);
            if (!main.photographer.empty()) credit << tr("photo %1").arg(QString::fromStdString(main.photographer));
            if (!credit.isEmpty()) text += QStringLiteral("<p><small>%1</small></p>").arg(credit.join(QStringLiteral(", ")).toHtmlEscaped());
        }
        card += QStringLiteral("<td valign=top>%1</td>").arg(text);
        h += QStringLiteral("<table cellspacing=0 cellpadding=8><tr>%1</tr></table>").arg(card);
        if (m_photos.size() > 1) { // other photos, from the oldest to the most recent
            QString strip;
            const size_t others = m_photos.size() - 1, shown = std::min<size_t>(others, 8);
            for (size_t k = 0; k < shown; ++k) {
                const auto& ph = m_photos[shown == others ? k : k * (others - 1) / (shown - 1)];
                strip += QStringLiteral("<td align=center><img src=\"%1\" height=96><br><small>%2</small></td>")
                             .arg(QUrl::fromLocalFile(QString::fromStdString(ph.path)).toString(), ph.year ? QString::number(ph.year) : QString());
            }
            h += QStringLiteral("<table cellspacing=0 cellpadding=4><tr>%1</tr></table>").arg(strip);
        }
        if (m_ratings.size() >= 2) h += tr("<h3>Elo des listes FIDE</h3>") + QStringLiteral("<img src=\"fide\">");
    } else if (playerEncyclopedia::get()) {
        // In orange (the accent colour of the Elo curve): visible without being an error
        h += QStringLiteral("<p><font color=\"#e68a35\">%1</font></p>").arg(tr("« %1 » ne figure pas dans l'encyclopédie des joueurs (nom écrit autrement ?).").arg(m_player.toHtmlEscaped()));
    }
    h += tr("<p>%n partie(s) dans « %1 »", nullptr, int(s.games.size())).arg(m_baseName.toHtmlEscaped());
    if (s.firstYear) h += tr(", de %1 à %2").arg(s.firstYear).arg(s.lastYear);
    int best = 0, bestYear = 0;
    for (const auto& y : s.years)
        if (y.maxElo > best) best = y.maxElo, bestYear = y.year;
    if (best) h += tr(" · Elo le plus haut : %1 (%2)").arg(best).arg(bestYear);
    h += QStringLiteral("</p>");
    const QString head = tr("<tr><th></th><th align=right>Parties</th><th>+</th><th>=</th><th>−</th><th align=right>Score</th></tr>");
    h += QStringLiteral("<table cellspacing=0 cellpadding=4>") + head;
    h += QStringLiteral("<tr><td>%1</td>%2</tr>").arg(tr("Avec les Blancs"), scoreCells(s.asWhite));
    h += QStringLiteral("<tr><td>%1</td>%2</tr>").arg(tr("Avec les Noirs"), scoreCells(s.asBlack));
    h += QStringLiteral("<tr><td><b>%1</b></td>%2</tr></table>").arg(tr("Total"), scoreCells(all));
    if (s.years.size() >= 2) h += tr("<h3>Elo par année</h3>") + QStringLiteral("<img src=\"elo\">");
    auto openings = [&](const std::vector<hy64db::PlayerStats::Opening>& list, const QString& title) {
        if (list.empty()) return QString();
        QString t = QStringLiteral("<h3>%1</h3><table cellspacing=0 cellpadding=4>").arg(title) + head;
        for (const auto& o : list)
            t += QStringLiteral("<tr><td>%1</td>%2</tr>").arg(openingLabel(QString::fromStdString(hy64db::formatEco(o.eco))).toHtmlEscaped(), scoreCells(o.score));
        return t + QStringLiteral("</table>");
    };
    h += openings(s.whiteOpenings, tr("Ouvertures avec les Blancs (ECO)"));
    h += openings(s.blackOpenings, tr("Ouvertures avec les Noirs (ECO)"));
    if (!s.opponents.empty()) {
        h += tr("<h3>Adversaires les plus fréquents</h3>") + QStringLiteral("<table cellspacing=0 cellpadding=4>") + head;
        for (const auto& o : s.opponents)
            h += QStringLiteral("<tr><td>%1</td>%2</tr>").arg(QString::fromStdString(m_db.names().name(hy64db::Player, o.id)).toHtmlEscaped(), scoreCells(o.score));
        h += QStringLiteral("</table>");
    }
    (void)loc;
    return h;
}
