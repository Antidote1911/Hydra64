#include "openingreport.h"

#include "gamelist.h"
#include "openings.h"

#include "chess/polyglot.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHash>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

using namespace chess;

namespace {
double points(int result, bool white) { return result == 2 ? 0 : result == 0 ? 0.5 : (result == 1) == white ? 1 : 0; }

void addResult(int result, int& w, int& d, int& b)
{
    if (result == 1) ++w;
    else if (result == 0) ++d;
    else if (result == -1) ++b;
}

QString pct(double v) { return QString::number(100.0 * v, 'f', 1) + QStringLiteral(" %"); }

QString scoreOf(int w, int d, int b)
{
    const int n = w + d + b;
    return n ? pct((w + 0.5 * d) / n) : QStringLiteral("–");
}

QString pawnsOf(const Position& p)
{
    QString s(64, QLatin1Char('.'));
    for (int r = 7; r >= 0; --r)
        for (int f = 0; f < 8; ++f) {
            const Piece pc = p.at(makeSquare(f, r));
            if (typeOf(pc) == Pawn) s[(7 - r) * 8 + f] = colorOf(pc) == White ? QLatin1Char('P') : QLatin1Char('p');
        }
    return s;
}

QString diagram(const QString& pawns)
{
    QString h = QStringLiteral("<table cellspacing='0' cellpadding='0' style='border:1px solid #555'>");
    for (int r = 0; r < 8; ++r) {
        h += QStringLiteral("<tr>");
        for (int f = 0; f < 8; ++f) {
            const QChar c = pawns[r * 8 + f];
            const QString bg = (r + f) % 2 ? QStringLiteral("#707a87") : QStringLiteral("#c4c8ce");
            const QString glyph = c == QLatin1Char('P') ? QStringLiteral("<span style='color:#fafafa'>♟</span>")
                                  : c == QLatin1Char('p') ? QStringLiteral("<span style='color:#101012'>♟</span>")
                                                          : QStringLiteral("&nbsp;");
            h += QStringLiteral("<td width='16' height='16' align='center' style='background:%1;font-size:12px'>%2</td>").arg(bg, glyph);
        }
        h += QStringLiteral("</tr>");
    }
    return h + QStringLiteral("</table>");
}
} // namespace

OpeningReport OpeningReport::build(const Position& pos, const GameListModel& model, const QList<int>& rows,
                                   const std::vector<IndexedGameSource::Summary>& baseline, int maxSample, const std::function<bool(int, int)>& progress)
{
    OpeningReport r;
    r.position = pos;
    if (const eco::Opening* o = openingsBook().lookup(pos)) {
        r.eco = QString::fromStdString(o->eco);
        r.opening = openingName(o->name);
    }
    struct Row {
        int row;
        IndexedGameSource::Summary s;
        int avg;
    };
    std::vector<Row> all;
    all.reserve(size_t(rows.size()));
    QHash<QString, Player> whites, blacks;
    const int total = int(rows.size());
    int done = 0;
    for (int row : rows) {
        if (progress && (++done & 1023) == 0 && !progress(done, total * 2)) return r;
        IndexedGameSource::Summary s;
        if (!model.summary(row, s)) continue;
        const int avg = s.whiteElo > 0 && s.blackElo > 0 ? (s.whiteElo + s.blackElo) / 2 : 0;
        all.push_back({row, s, avg});
        ++r.games;
        addResult(s.result, r.white, r.draw, r.black);
        if (avg) {
            r.eloSum += avg;
            ++r.eloGames;
        }
        if (s.year >= 1000 && s.year <= 2999) {
            r.firstYear = r.firstYear ? std::min(r.firstYear, s.year) : s.year;
            r.lastYear = std::max(r.lastYear, s.year);
        }
        if (r.games <= 300000) { // names (read from the header): at most 300,000 games
            const QString w = model.data(model.index(row, GameListModel::White), Qt::DisplayRole).toString();
            const QString b = model.data(model.index(row, GameListModel::Black), Qt::DisplayRole).toString();
            if (!w.isEmpty()) {
                Player& p = whites[w];
                p.name = w;
                ++p.games;
                p.points += points(s.result, true);
            }
            if (!b.isEmpty()) {
                Player& p = blacks[b];
                p.name = b;
                ++p.games;
                p.points += points(s.result, false);
            }
        }
    }
    // Periods (5 or 10 years), popularity relative to the starting view
    if (r.firstYear) {
        const int step = r.lastYear - r.firstYear <= 30 ? 5 : 10;
        const int first = r.firstYear - r.firstYear % step;
        for (int y = first; y <= r.lastYear; y += step) r.periods.push_back({y, y + step - 1});
        auto at = [&](int year) -> Period* {
            if (year < first || year > r.lastYear) return nullptr;
            return &r.periods[size_t((year - first) / step)];
        };
        for (const Row& g : all)
            if (Period* p = at(g.s.year)) {
                ++p->games;
                addResult(g.s.result, p->white, p->draw, p->black);
            }
        for (const auto& s : baseline)
            if (Period* p = at(s.year)) ++p->baseGames;
    }
    auto top = [](const QHash<QString, Player>& h) {
        std::vector<Player> v(h.begin(), h.end());
        std::sort(v.begin(), v.end(), [](const Player& a, const Player& b) { return a.games != b.games ? a.games > b.games : a.name < b.name; });
        if (v.size() > 10) v.resize(10);
        return v;
    };
    r.whitePlayers = top(whites);
    r.blackPlayers = top(blacks);
    // Best games (average Elo) and replayed sample: the strongest first
    std::stable_sort(all.begin(), all.end(), [](const Row& a, const Row& b) { return a.avg > b.avg; });
    const uint64_t key = polyglot::key(pos);
    std::map<QString, MoveStat> moves;
    std::map<QString, Structure> structures;
    const int sample = std::min<int>(int(all.size()), std::max(1, maxSample));
    for (int i = 0; i < sample; ++i) {
        if (progress && (i & 63) == 0 && !progress(total + int(int64_t(i) * total / std::max(1, sample)), total * 2)) return r;
        const Row& g = all[size_t(i)];
        if (int(r.best.size()) < 10 && g.avg > 0) {
            BestGame b;
            b.row = g.row;
            b.avgElo = g.avg;
            const QString w = model.data(model.index(g.row, GameListModel::White), Qt::DisplayRole).toString();
            const QString k = model.data(model.index(g.row, GameListModel::Black), Qt::DisplayRole).toString();
            const QString res = g.s.result == 1 ? QStringLiteral("1-0") : g.s.result == -1 ? QStringLiteral("0-1") : g.s.result == 0 ? QStringLiteral("½-½") : QStringLiteral("*");
            b.label = QStringLiteral("%1 – %2, %3%4").arg(w, k, res, g.s.year ? QStringLiteral(", %1").arg(g.s.year) : QString());
            r.best.push_back(b);
        }
        Game game;
        if (!model.parseGame(g.row, game, nullptr)) continue;
        const std::vector<Node*> line = game.mainline();
        int at = -1; // index in the line where the position is reached (-1: root)
        bool found = polyglot::key(game.root()->pos) == key;
        for (int k = 0; !found && k < int(line.size()); ++k)
            if (polyglot::key(line[size_t(k)]->pos) == key) {
                at = k;
                found = true;
            }
        if (!found) continue;
        ++r.sampled;
        const Position& here = at < 0 ? game.root()->pos : line[size_t(at)]->pos;
        if (at + 1 < int(line.size())) {
            const QString san = QString::fromStdString(here.toSan(line[size_t(at + 1)]->move));
            MoveStat& m = moves[san];
            m.san = san;
            ++m.games;
            addResult(g.s.result, m.white, m.draw, m.black);
            if (g.avg) m.eloSum += g.avg, ++m.eloCount;
        }
        const int target = std::min(int(line.size()) - 1, at + r.structurePly);
        const Position& later = target < 0 ? game.root()->pos : line[size_t(target)]->pos;
        Structure& st = structures[pawnsOf(later)];
        st.pawns = pawnsOf(later);
        ++st.games;
        addResult(g.s.result, st.white, st.draw, st.black);
    }
    for (auto& [_, m] : moves) r.moves.push_back(m);
    std::sort(r.moves.begin(), r.moves.end(), [](const MoveStat& a, const MoveStat& b) { return a.games > b.games; });
    for (auto& [_, s] : structures) r.structures.push_back(s);
    std::sort(r.structures.begin(), r.structures.end(), [](const Structure& a, const Structure& b) { return a.games > b.games; });
    if (r.structures.size() > 6) r.structures.resize(6);
    return r;
}

QString OpeningReport::toHtml(const QString& baseName) const
{
    auto esc = [](const QString& s) { return s.toHtmlEscaped(); };
    QString h = QStringLiteral("<html><head><meta charset='utf-8'><title>%1</title>"
                               "<style>body{font-family:sans-serif} table.t{border-collapse:collapse} table.t td,table.t th{padding:2px 8px;"
                               "border-bottom:1px solid #444;text-align:right} table.t td:first-child,table.t th:first-child{text-align:left}"
                               " h2{margin-top:18px}</style></head><body>")
                    .arg(QObject::tr("Rapport d'ouverture"));
    h += QStringLiteral("<h1>%1</h1>").arg(esc(eco.isEmpty() ? QObject::tr("Rapport d'ouverture") : eco + QLatin1Char(' ') + opening));
    h += QStringLiteral("<p><code>%1</code><br>").arg(esc(QString::fromStdString(position.fen())));
    h += QObject::tr("Base : %1 — %n partie(s) passent par cette position", nullptr, games).arg(esc(baseName));
    if (games) {
        h += QStringLiteral("<br>") + QObject::tr("Blancs +%1 =%2 −%3, score des Blancs %4").arg(white).arg(draw).arg(black).arg(scoreOf(white, draw, black));
        if (eloGames) h += QStringLiteral("<br>") + QObject::tr("Elo moyen %1 (%2 parties avec Elo)").arg(qRound(eloSum / eloGames)).arg(eloGames);
        if (firstYear) h += QStringLiteral("<br>") + QObject::tr("Jouée de %1 à %2").arg(firstYear).arg(lastYear);
    }
    h += QStringLiteral("</p>");
    if (!moves.empty()) {
        h += QStringLiteral("<h2>%1</h2><table class='t'><tr><th>%2</th><th>%3</th><th>%4</th><th>%5</th><th>%6</th><th>%7</th></tr>")
                 .arg(QObject::tr("Coups suivants"), QObject::tr("Coup"), QObject::tr("Parties"), QObject::tr("Part"), QObject::tr("Score des Blancs"),
                      QObject::tr("Nulles"), QObject::tr("Elo moyen"));
        for (const MoveStat& m : moves)
            h += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                     .arg(esc(m.san))
                     .arg(m.games)
                     .arg(pct(double(m.games) / std::max(1, sampled)))
                     .arg(scoreOf(m.white, m.draw, m.black))
                     .arg(m.games ? pct(double(m.draw) / m.games) : QStringLiteral("–"))
                     .arg(m.eloCount ? QString::number(qRound(m.eloSum / m.eloCount)) : QStringLiteral("–"));
        h += QStringLiteral("</table>");
        if (sampled < games) h += QStringLiteral("<p><small>%1</small></p>").arg(QObject::tr("Coups et structures : les %1 parties les plus fortes.").arg(sampled));
    }
    if (!periods.empty()) {
        h += QStringLiteral("<h2>%1</h2><table class='t'><tr><th>%2</th><th>%3</th><th>%4</th><th>%5</th><th>%6</th></tr>")
                 .arg(QObject::tr("Par période"), QObject::tr("Années"), QObject::tr("Parties"), QObject::tr("Popularité"), QObject::tr("Score des Blancs"),
                      QObject::tr("Nulles"));
        for (const Period& p : periods)
            h += QStringLiteral("<tr><td>%1–%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                     .arg(p.from)
                     .arg(p.to)
                     .arg(p.games)
                     .arg(p.baseGames ? pct(double(p.games) / p.baseGames) : QStringLiteral("–"))
                     .arg(scoreOf(p.white, p.draw, p.black))
                     .arg(p.games ? pct(double(p.draw) / p.games) : QStringLiteral("–"));
        h += QStringLiteral("</table><p><small>%1</small></p>").arg(QObject::tr("Popularité : part des parties de la période (dans la vue de départ) qui passent par la position."));
    }
    auto players = [&](const std::vector<Player>& v, const QString& title) {
        if (v.empty()) return;
        h += QStringLiteral("<h2>%1</h2><table class='t'><tr><th>%2</th><th>%3</th><th>%4</th></tr>").arg(title, QObject::tr("Joueur"), QObject::tr("Parties"), QObject::tr("Score"));
        for (const Player& p : v)
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(esc(p.name)).arg(p.games).arg(pct(p.points / std::max(1, p.games)));
        h += QStringLiteral("</table>");
    };
    players(whitePlayers, QObject::tr("Joueurs les plus fréquents avec les Blancs"));
    players(blackPlayers, QObject::tr("Joueurs les plus fréquents avec les Noirs"));
    if (!best.empty()) {
        h += QStringLiteral("<h2>%1</h2><ol>").arg(QObject::tr("Meilleures parties (Elo moyen)"));
        for (const BestGame& b : best) h += QStringLiteral("<li><a href='game:%1'>%2</a> (%3)</li>").arg(b.row).arg(esc(b.label)).arg(b.avgElo);
        h += QStringLiteral("</ol>");
    }
    if (!structures.empty()) {
        h += QStringLiteral("<h2>%1</h2><table><tr>").arg(QObject::tr("Structures de pions typiques (%n demi-coup(s) plus tard)", nullptr, structurePly));
        for (const Structure& s : structures)
            h += QStringLiteral("<td valign='top' style='padding-right:14px'>%1<br>%2<br>%3</td>")
                     .arg(diagram(s.pawns))
                     .arg(QObject::tr("%n partie(s)", nullptr, s.games))
                     .arg(QObject::tr("score des Blancs %1").arg(scoreOf(s.white, s.draw, s.black)));
        h += QStringLiteral("</tr></table>");
    }
    return h + QStringLiteral("</body></html>");
}

// ---------------------------------------------------------------- dialog

OpeningReportDialog::OpeningReportDialog(OpeningReport report, QString baseName, QWidget* parent)
    : QDialog(parent)
    , m_report(std::move(report))
{
    setWindowTitle(tr("Rapport d'ouverture"));
    resize(820, 760);
    m_html = m_report.toHtml(baseName);
    m_view = new QTextBrowser;
    m_view->setOpenLinks(false);
    m_view->setHtml(m_html);
    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == QLatin1String("game")) emit gameRequested(url.path().toInt());
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    auto* save = buttons->addButton(tr("Exporter en HTML…"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
        QSettings s;
        const QString path = QFileDialog::getSaveFileName(this, tr("Exporter le rapport"), s.value("lastDir").toString() + QStringLiteral("/rapport-ouverture.html"),
                                                          tr("Pages HTML (*.html)"));
        if (path.isEmpty()) return;
        QString error;
        if (!exportHtml(path, &error)) QMessageBox::warning(this, tr("Rapport d'ouverture"), error);
    });
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_view, 1);
    layout->addWidget(buttons);
}

bool OpeningReportDialog::exportHtml(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    // Links to the games: meaningless outside the application
    QString html = m_html;
    html.replace(QRegularExpression(QStringLiteral("<a href='game:\\d+'>([^<]*)</a>")), QStringLiteral("\\1"));
    f.write(html.toUtf8());
    return true;
}
