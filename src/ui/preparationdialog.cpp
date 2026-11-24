#include "preparationdialog.h"

#include "display.h"
#include "openings.h"
#include "theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QDoubleSpinBox>
#include "engine/engineregistry.h"
#include <QSpinBox>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_map>

using hy64db::PrepNode;

namespace {
QString scoreCells(const hy64db::PlayerStats::Score& s)
{
    return QStringLiteral("<td align=right>+%1</td><td align=right>=%2</td><td align=right>−%3</td><td align=right>%4 %</td>")
        .arg(s.wins)
        .arg(s.draws)
        .arg(s.losses)
        .arg(s.games() ? QLocale().toString(s.percent(), 'f', 1) : QStringLiteral("–"));
}
} // namespace

PreparationDialog::PreparationDialog(const hy64db::Database& db, const QString& baseName, const QString& player, chess::Color side, QWidget* parent)
    : QDialog(parent)
    , m_db(db)
    , m_baseName(baseName)
    , m_side(side)
{
    setWindowTitle(tr("Se préparer contre un joueur"));
    resize(920, 720);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Joueur : début d'un mot du nom…"));
    m_name->setClearButtonEnabled(true);
    m_names = new QStringListModel(this);
    auto* completer = new QCompleter(m_names, this);
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion); // the database has already chosen the names
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_name->setCompleter(completer);

    auto* sides = new QButtonGroup(this);
    for (int c = 0; c < 2; ++c) {
        m_sideButton[c] = new QToolButton;
        m_sideButton[c]->setObjectName("Segment");
        m_sideButton[c]->setCheckable(true);
        m_sideButton[c]->setText(c == 0 ? tr("Il a les Blancs") : tr("Il a les Noirs"));
        m_sideButton[c]->setToolTip(c == 0 ? tr("Ce qu'il joue avec les Blancs : vous aurez les Noirs") : tr("Ce qu'il joue avec les Noirs : vous aurez les Blancs"));
        sides->addButton(m_sideButton[c], c);
    }
    m_sideButton[m_side == chess::White ? 0 : 1]->setChecked(true);

    m_moves = new QSpinBox;
    m_moves->setRange(4, 30);
    m_moves->setValue(12);
    m_moves->setToolTip(tr("Nombre de coups de chaque partie suivis dans l'arbre"));
    m_year = new QSpinBox;
    m_year->setRange(0, 2100);
    m_year->setSpecialValueText(tr("toutes"));
    m_year->setToolTip(tr("Ne garder que ses parties depuis cette année (les parties sans date sont alors écartées)"));
    m_minGames = new QSpinBox;
    m_minGames->setRange(1, 50);
    m_minGames->setValue(2);
    m_minGames->setToolTip(tr("Un coup joué dans moins de parties n'est pas affiché"));

    auto* top = new QHBoxLayout;
    top->addWidget(m_name, 1);
    top->addWidget(m_sideButton[0]);
    top->addWidget(m_sideButton[1]);
    auto* options = new QHBoxLayout;
    auto labelled = [options](const QString& text, QWidget* w) {
        auto* l = new QLabel(text);
        l->setObjectName("Dim");
        options->addWidget(l);
        options->addWidget(w);
        options->addSpacing(14);
    };
    labelled(tr("Coups suivis"), m_moves);
    labelled(tr("Depuis l'année"), m_year);
    labelled(tr("Parties au moins"), m_minGames);
    options->addStretch(1);

    // Error report: his most recent losses gone through by an engine
    m_errorGames = new QSpinBox;
    m_errorGames->setRange(1, 500);
    m_errorGames->setValue(30);
    m_errorGames->setToolTip(tr("Nombre de ses défaites analysées, les plus récentes d'abord"));
    m_errorSeconds = new QDoubleSpinBox;
    m_errorSeconds->setRange(0.1, 30);
    m_errorSeconds->setDecimals(1);
    m_errorSeconds->setSingleStep(0.1);
    m_errorSeconds->setValue(0.2);
    m_errorSeconds->setSuffix(tr(" s"));
    m_errorSeconds->setToolTip(tr("Temps du moteur par position ; une partie de 40 coups en compte environ 70"));
    m_errorButton = new QPushButton(theme::icon("engine"), tr("Analyser ses défaites"));
    m_errorButton->setToolTip(tr("Le moteur cherche, dans chacune de ses défaites, le coup qui lui a coûté le plus. Les résultats sont gardés."));
    m_errorButton->setEnabled(false);
    m_errorProgress = new QProgressBar;
    m_errorProgress->setTextVisible(true);
    m_errorProgress->setFormat(tr("%v / %m parties"));
    m_errorProgress->hide();
    auto* errorRow = new QHBoxLayout;
    {
        auto* l1 = new QLabel(tr("Rapport d'erreur :"));
        l1->setObjectName("Dim");
        auto* l2 = new QLabel(tr("défaites,"));
        l2->setObjectName("Dim");
        auto* l3 = new QLabel(tr("par position"));
        l3->setObjectName("Dim");
        errorRow->addWidget(l1);
        errorRow->addWidget(m_errorGames);
        errorRow->addWidget(l2);
        errorRow->addWidget(m_errorSeconds);
        errorRow->addWidget(l3);
        errorRow->addSpacing(10);
        errorRow->addWidget(m_errorButton);
        errorRow->addWidget(m_errorProgress, 1);
        errorRow->addStretch(1);
    }

    m_report = new QTextBrowser;
    m_report->setOpenLinks(false);
    m_games = new QPushButton(theme::icon("database"), tr("Ses parties"));
    m_games->setToolTip(tr("Afficher dans la liste ses parties avec cette couleur"));
    m_export = new QPushButton(theme::icon("save"), tr("Exporter (HTML)…"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    buttons->addButton(m_games, QDialogButtonBox::ActionRole);
    buttons->addButton(m_export, QDialogButtonBox::ActionRole);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(options);
    layout->addLayout(errorRow);
    layout->addWidget(m_report, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textEdited, this, &PreparationDialog::complete);
    connect(m_name, &QLineEdit::returnPressed, this, [this] { showPlayer(m_name->text().trimmed()); });
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& name) { showPlayer(name); });
    connect(sides, &QButtonGroup::idClicked, this, [this](int id) { setSide(id == 0 ? chess::White : chess::Black); });
    for (QSpinBox* s : {m_moves, m_year, m_minGames}) connect(s, &QSpinBox::valueChanged, this, [this] { compute(); });
    connect(m_errorButton, &QPushButton::clicked, this, [this] {
        if (isScanning()) m_scan->cancel();
        else startErrorScan();
    });
    connect(m_errorGames, &QSpinBox::valueChanged, this, [this] {
        if (m_id != hy64db::NoName && !isScanning()) m_report->setHtml(html());
    });
    connect(m_report, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == QLatin1String("position")) {
            const int i = url.path().toInt();
            if (i >= 0 && size_t(i) < m_positionLinks.size()) emit positionRequested(m_positionLinks[size_t(i)].first, m_positionLinks[size_t(i)].second);
            return;
        }
        if (url.scheme() != QLatin1String("games")) return;
        const int i = url.path().toInt();
        if (i >= 0 && size_t(i) < m_links.size() && !m_links[size_t(i)].empty()) emit gamesRequested(m_links[size_t(i)], m_linkTitles[size_t(i)]);
    });
    connect(m_games, &QPushButton::clicked, this, [this] {
        if (!m_prep.root.ids.empty())
            emit gamesRequested(m_prep.root.ids, (m_side == chess::White ? tr("%1 avec les Blancs") : tr("%1 avec les Noirs")).arg(m_player));
    });
    connect(m_export, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exporter la préparation"),
                                                          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + m_player + QStringLiteral(" - ")
                                                              + (m_side == chess::White ? tr("Blancs") : tr("Noirs")) + QStringLiteral(".html"),
                                                          tr("Page HTML (*.html)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write((QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title></head><body>").arg(m_player.toHtmlEscaped()) + html(false)
                     + QStringLiteral("</body></html>"))
                        .toUtf8());
    });
    m_games->setEnabled(false);
    m_export->setEnabled(false);
    if (!player.isEmpty()) {
        m_name->setText(player);
        showPlayer(player);
    }
    if (m_id == hy64db::NoName)
        m_report->setHtml(tr("<p>Tapez le nom d'un joueur de la base, puis choisissez la couleur qu'il aura contre vous.</p>"
                             "<p>Le rapport montre ce qu'il joue avec cette couleur, les lignes où il marque le moins et celles où il marque le plus, "
                             "ses ouvertures et ses défaites les plus courtes. Un clic sur un nombre de parties les affiche dans la liste.</p>"));
}

void PreparationDialog::complete(const QString& text)
{
    QStringList names;
    const QString t = text.trimmed();
    if (t.size() >= 2) {
        if (m_usage.empty()) m_usage = m_db.nameUsage(hy64db::Player);
        std::vector<uint32_t> ids = m_db.names().findPrefix(hy64db::Player, t.toStdString());
        auto used = [&](uint32_t id) { return id < m_usage.size() ? m_usage[id] : 0; };
        std::stable_sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) { return used(a) > used(b); });
        for (uint32_t id : ids) {
            if (!used(id)) continue;
            names << QString::fromStdString(m_db.names().name(hy64db::Player, id));
            if (names.size() >= 40) break;
        }
    }
    m_names->setStringList(names);
}

bool PreparationDialog::showPlayer(const QString& name)
{
    const uint32_t id = m_db.names().find(hy64db::Player, name.toStdString());
    if (id == hy64db::NoName) return false;
    m_id = id;
    m_player = name;
    compute();
    return true;
}

void PreparationDialog::setSide(chess::Color side)
{
    m_side = side;
    m_sideButton[side == chess::White ? 0 : 1]->setChecked(true);
    compute();
}

void PreparationDialog::setOptions(int moves, int fromYear, int minGames)
{
    for (QSpinBox* s : {m_moves, m_year, m_minGames}) s->blockSignals(true);
    m_moves->setValue(moves);
    m_year->setValue(fromYear);
    m_minGames->setValue(minGames);
    for (QSpinBox* s : {m_moves, m_year, m_minGames}) s->blockSignals(false);
    compute();
}

void PreparationDialog::setRepertoireLoader(RepertoireLoader loader)
{
    m_loadRepertoire = std::move(loader);
    compute();
}

void PreparationDialog::setErrorCache(const QString& cacheFile)
{
    m_errorCache = cacheFile;
    m_errors = errorreport::loadCache(cacheFile, m_db);
    if (m_id != hy64db::NoName) m_report->setHtml(html());
}

void PreparationDialog::setErrorOptions(int games, double secondsPerPosition)
{
    m_errorGames->setValue(games);
    m_errorSeconds->setValue(secondsPerPosition);
}

// His losses with this colour (year filter applied), most recent first, as many as asked.
std::vector<uint32_t> PreparationDialog::recentLosses() const
{
    std::vector<uint32_t> ids = m_prep.losses;
    std::stable_sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) { return m_db.record(a).date > m_db.record(b).date; });
    if (ids.size() > size_t(m_errorGames->value())) ids.resize(size_t(m_errorGames->value()));
    return ids;
}

bool PreparationDialog::startErrorScan(const EngineConfig& engine)
{
    if (m_id == hy64db::NoName || isScanning()) return false;
    const int movetime = int(m_errorSeconds->value() * 1000 + 0.5);
    std::vector<uint32_t> todo;
    for (uint32_t id : recentLosses()) {
        const auto known = m_errors.find(id);
        if (known == m_errors.end() || known->second.movetimeMs < movetime) todo.push_back(id); // (a longer analysis replaces a shorter one)
    }
    if (todo.empty()) {
        m_report->setHtml(html());
        return false;
    }
    if (!m_scan) {
        m_scan = new ErrorScan(m_db, this);
        connect(m_scan, &ErrorScan::gameDone, this, [this](const errorreport::GameErrors& result) { m_errors[result.game] = result; });
        connect(m_scan, &ErrorScan::progress, this, [this](int done, int total) {
            m_errorProgress->setMaximum(total);
            m_errorProgress->setValue(done);
        });
        connect(m_scan, &ErrorScan::finished, this, [this](bool completed) {
            if (!m_errorCache.isEmpty()) errorreport::saveCache(m_errorCache, m_errors); // what is done is kept, even after a stop
            m_errorProgress->hide();
            m_errorButton->setText(tr("Analyser ses défaites"));
            for (QWidget* w : {static_cast<QWidget*>(m_name), static_cast<QWidget*>(m_sideButton[0]), static_cast<QWidget*>(m_sideButton[1]), static_cast<QWidget*>(m_year)}) w->setEnabled(true);
            m_report->setHtml(html());
            emit scanFinished(completed);
        });
    }
    QString error;
    if (!m_scan->start(engine.isValid() ? engine : EngineRegistry::instance().fallbackEngine(), todo, m_side, movetime, &error)) return false;
    // The player, his colour and the years stay as they are while his games are being analysed.
    for (QWidget* w : {static_cast<QWidget*>(m_name), static_cast<QWidget*>(m_sideButton[0]), static_cast<QWidget*>(m_sideButton[1]), static_cast<QWidget*>(m_year)}) w->setEnabled(false);
    m_errorProgress->setMaximum(int(todo.size()));
    m_errorProgress->setValue(0);
    m_errorProgress->show();
    m_errorButton->setText(tr("Arrêter"));
    return true;
}

// "23. Qxb7" / "23… Qxb7", in the displayed notation
static QString numbered(int ply, const QString& san)
{
    return ((ply % 2 == 1) ? QStringLiteral("%1. ") : QStringLiteral("%1… ")).arg((ply + 1) / 2) + display::san(san.toStdString()).toHtmlEscaped();
}

QString PreparationDialog::errorsHtml(bool links) const
{
    if (links) m_positionLinks.clear();
    const std::vector<uint32_t> wanted = recentLosses();
    if (wanted.empty()) return {};
    const QString dim = theme::TextDim.name(), accent = theme::Accent.name();
    QString h = QStringLiteral("<h3>%1</h3>").arg(tr("Ses fautes (rapport d'erreur)"));
    std::vector<const errorreport::GameErrors*> done;
    for (uint32_t id : wanted)
        if (const auto it = m_errors.find(id); it != m_errors.end()) done.push_back(&it->second);
    if (done.empty())
        return h + QStringLiteral("<p><font color=\"%1\">%2</font></p>")
                       .arg(dim, tr("« Analyser ses défaites » : le moteur cherche, dans chacune de ses %1 défaites les plus récentes, le coup qui lui a coûté le plus.").arg(wanted.size()));
    int phase[3] = {0, 0, 0}, standing[3] = {0, 0, 0}, clear = 0, mistakes = 0, moveSum = 0;
    for (const auto* g : done) {
        mistakes += g->mistakes;
        if (!g->hasTurning) continue;
        ++clear;
        ++phase[int(g->turning.phase)];
        ++standing[int(g->turning.standing)];
        moveSum += (g->turning.ply + 1) / 2;
    }
    h += tr("<p>%1 de ses %2 défaites les plus récentes analysées (%3, %4 s par position).").arg(done.size()).arg(wanted.size()).arg(done.front()->engine.toHtmlEscaped()).arg(QLocale().toString(done.front()->movetimeMs / 1000.0, 'g', 3));
    h += tr(" En moyenne %1 fautes par défaite.").arg(QLocale().toString(double(mistakes) / double(done.size()), 'f', 1));
    if (clear) h += tr(" Sa faute la plus coûteuse tombe en moyenne au %1ᵉ coup.").arg(moveSum / clear);
    h += QStringLiteral("</p>");
    if (clear) {
        h += QStringLiteral("<table cellspacing=0 cellpadding=4>")
             + tr("<tr><th align=left>Sa faute la plus coûteuse</th><th align=right>Défaites</th></tr>")
             + QStringLiteral("<tr><td>%1</td><td align=right>%2</td></tr><tr><td>%3</td><td align=right>%4</td></tr><tr><td>%5</td><td align=right>%6</td></tr>")
                   .arg(tr("dans l'ouverture (15 premiers coups)")).arg(phase[0]).arg(tr("en milieu de jeu")).arg(phase[1]).arg(tr("en finale")).arg(phase[2])
             + QStringLiteral("<tr><td>&nbsp;</td><td></td></tr><tr><td>%1</td><td align=right>%2</td></tr><tr><td>%3</td><td align=right>%4</td></tr><tr><td>%5</td><td align=right>%6</td></tr>")
                   .arg(tr("alors qu'il était mieux")).arg(standing[0]).arg(tr("dans une position égale")).arg(standing[1]).arg(tr("alors qu'il était déjà moins bien")).arg(standing[2]);
        if (int(done.size()) > clear)
            h += QStringLiteral("<tr><td>&nbsp;</td><td></td></tr><tr><td>%1</td><td align=right>%2</td></tr>").arg(tr("aucune faute nette (perdue peu à peu, ou au temps)")).arg(int(done.size()) - clear);
        h += QStringLiteral("</table>");
        const bool white = m_prep.side == chess::White;
        const QString phaseName[3] = {tr("ouverture"), tr("milieu de jeu"), tr("finale")};
        h += QStringLiteral("<table cellspacing=0 cellpadding=4>")
             + tr("<tr><th align=left>Partie</th><th align=right>Année</th><th align=left>Sa faute</th><th align=left>Mieux</th><th align=left>Évaluation</th><th align=left>Phase</th><th></th></tr>");
        for (const auto* g : done) {
            if (!g->hasTurning) continue;
            const hy64db::Record r = m_db.record(g->game);
            const QString opponent = QString::fromStdString(m_db.names().name(hy64db::Player, white ? r.black : r.white));
            const errorreport::Mistake& m = g->turning;
            QString see;
            if (links) {
                m_positionLinks.push_back({g->game, m.ply});
                see = QStringLiteral("<a href=\"position:%1\" style=\"color:%2;text-decoration:none\">%3</a>").arg(m_positionLinks.size() - 1).arg(accent, tr("voir"));
            }
            h += QStringLiteral("<tr><td>%1</td><td align=right>%2</td><td><b>%3</b>%4</td><td>%5</td><td>%6 → %7</td><td>%8</td><td>%9</td></tr>")
                     .arg(tr("contre %1").arg(opponent.toHtmlEscaped()), (r.date >> 9) ? QString::number(r.date >> 9) : QString(), numbered(m.ply, m.san),
                          m.drop >= 0.3 ? QStringLiteral("??") : QStringLiteral("?"), m.best.isEmpty() ? QString() : display::san(m.best.toStdString()).toHtmlEscaped(), m.before, m.after,
                          phaseName[int(m.phase)], see);
        }
        h += QStringLiteral("</table>");
    }
    return h;
}

void PreparationDialog::compute()
{
    if (m_id == hy64db::NoName) return;
    hy64db::PrepOptions o;
    o.maxPlies = m_moves->value() * 2;
    o.fromYear = m_year->value();
    o.minGames = uint32_t(m_minGames->value());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_prep = hy64db::prepareAgainst(m_db, m_id, m_side, o);
    // One's own repertoire, for the other colour
    m_coverage = {};
    m_repertoire = chess::Game();
    m_hasRepertoire = m_loadRepertoire && m_loadRepertoire(~m_side, m_repertoire) && m_repertoire.root()->mainChild();
    if (m_hasRepertoire) m_coverage = hy64db::coverage(m_prep, m_repertoire);
    QApplication::restoreOverrideCursor();
    m_report->setHtml(html());
    m_games->setEnabled(!m_prep.root.ids.empty());
    m_export->setEnabled(true);
    m_errorButton->setEnabled(!m_prep.losses.empty());
}

// "1. e4 c5 2. Nf3", the player's moves in bold; started at node `from` of the path ("2… d6").
QString PreparationDialog::lineText(const std::vector<const PrepNode*>& path, size_t from) const
{
    QStringList parts;
    for (size_t i = from; i < path.size(); ++i) {
        const PrepNode* n = path[i];
        QString t;
        if (n->ply % 2 == 1) t = QStringLiteral("%1. ").arg((n->ply + 1) / 2);
        else if (i == from) t = QStringLiteral("%1… ").arg(n->ply / 2);
        const QString san = display::san(n->san).toHtmlEscaped();
        parts << t + (n->byPlayer ? QStringLiteral("<b>%1</b>").arg(san) : san);
    }
    return parts.join(QLatin1Char(' '));
}

QString PreparationDialog::html(bool links) const
{
    if (links) { // (the page without links leaves those of the displayed report alone)
        m_links.clear();
        m_linkTitles.clear();
    }
    const hy64db::Preparation& p = m_prep;
    const QLocale loc;
    const QString accent = theme::Accent.name(), dim = theme::TextDim.name();
    const bool white = p.side == chess::White;
    // Number of games, clickable: they are shown in the game list.
    auto gamesCell = [&](const std::vector<uint32_t>& ids, uint32_t count, const QString& title) {
        if (!links || ids.empty()) return QString::number(count);
        m_links.push_back(ids);
        m_linkTitles.push_back(title);
        return QStringLiteral("<a href=\"games:%1\" style=\"color:%2;text-decoration:none\">%3</a>").arg(m_links.size() - 1).arg(accent).arg(count);
    };
    auto plain = [](QString htmlText) { return htmlText.remove(QStringLiteral("<b>")).remove(QStringLiteral("</b>")); };

    QString h = QStringLiteral("<h2>%1</h2>").arg((white ? tr("%1 avec les Blancs") : tr("%1 avec les Noirs")).arg(m_player.toHtmlEscaped()));
    h += tr("<p>%n partie(s) dans « %1 »", nullptr, int(p.root.games)).arg(m_baseName.toHtmlEscaped());
    if (p.firstYear) h += p.firstYear == p.lastYear ? tr(", en %1").arg(p.firstYear) : tr(", de %1 à %2").arg(p.firstYear).arg(p.lastYear);
    if (p.root.score.games())
        h += tr(" · score %1 % (+%2 =%3 −%4)").arg(loc.toString(p.root.score.percent(), 'f', 1)).arg(p.root.score.wins).arg(p.root.score.draws).arg(p.root.score.losses);
    h += QStringLiteral("</p>");
    if (p.otherStarts) h += QStringLiteral("<p><font color=\"%1\">%2</font></p>").arg(dim, tr("%n partie(s) écartée(s) : autre position de départ ou coups illisibles.", nullptr, int(p.otherStarts)));
    if (p.root.games == 0) return h + tr("<p>Aucune partie avec cette couleur.</p>");

    const QString head = tr("<tr><th align=left>Ligne</th><th align=right>Parties</th><th>+</th><th>=</th><th>−</th><th align=right>Son score</th><th align=right>Vu en</th></tr>");
    auto row = [&](const QString& line, const PrepNode* n, const QString& title) {
        return QStringLiteral("<tr><td>%1</td><td align=right>%2</td>%3<td align=right>%4</td></tr>")
            .arg(line, gamesCell(n->ids, n->games, title), scoreCells(n->score), n->lastYear ? QString::number(n->lastYear) : QString());
    };
    // Lines where he scores worst, then best
    const uint32_t enough = std::max<uint32_t>(3, uint32_t(m_minGames->value()));
    auto ranked = [&](bool weakest, const QString& title, const QString& none) {
        QString t = QStringLiteral("<h3>%1</h3>").arg(title);
        const auto lines = hy64db::rankedLines(p, weakest, enough);
        if (lines.empty()) return t + QStringLiteral("<p><font color=\"%1\">%2</font></p>").arg(dim, none);
        t += QStringLiteral("<table cellspacing=0 cellpadding=4>") + head;
        for (const auto& path : lines) {
            const QString line = lineText(path);
            t += row(line, path.back(), tr("%1 : %2").arg(m_player, plain(line)));
        }
        return t + QStringLiteral("</table>");
    };
    h += ranked(true, tr("Où il marque le moins"), tr("Aucune ligne d'au moins %1 parties où il marque 10 points de moins que son score.").arg(enough));
    h += ranked(false, tr("Où il marque le plus"), tr("Aucune ligne d'au moins %1 parties où il marque 10 points de plus que son score.").arg(enough));

    // His lines against one's own repertoire: where they leave it
    const QString mine = white ? tr("Noirs") : tr("Blancs");
    if (m_hasRepertoire) {
        h += QStringLiteral("<h3>%1</h3>").arg(tr("Face à votre répertoire des %1").arg(mine));
        if (m_coverage.gaps.empty()) {
            h += QStringLiteral("<p>%1</p>").arg(tr("Toutes ses lignes affichées restent dans votre répertoire."));
        } else {
            h += QStringLiteral("<table cellspacing=0 cellpadding=4>")
                 + tr("<tr><th align=left>Ligne</th><th align=right>Parties</th><th>+</th><th>=</th><th>−</th><th align=right>Son score</th><th align=left>Votre répertoire</th></tr>");
            size_t shown = 0;
            for (const hy64db::PrepGap& g : m_coverage.gaps) {
                if (++shown > 15) break;
                const PrepNode* n = g.path.back();
                const QString line = lineText(g.path);
                QStringList known;
                for (const std::string& k : g.known) known << display::san(k).toHtmlEscaped();
                const QString what = g.kind == hy64db::PrepCover::Ends ? tr("s'arrête avant ce coup") : tr("coup non prévu (prévus : %1)").arg(known.join(QStringLiteral(", ")));
                h += QStringLiteral("<tr><td>%1</td><td align=right>%2</td>%3<td>%4</td></tr>")
                         .arg(line, gamesCell(n->ids, n->games, tr("%1 : %2").arg(m_player, plain(line))), scoreCells(n->score), what);
            }
            h += QStringLiteral("</table>");
            if (m_coverage.gaps.size() > 15) h += QStringLiteral("<p><font color=\"%1\">%2</font></p>").arg(dim, tr("… et %1 autres, moins jouées.").arg(m_coverage.gaps.size() - 15));
        }
    } else if (m_loadRepertoire) {
        h += QStringLiteral("<h3>%1</h3><p><font color=\"%2\">%3</font></p>")
                 .arg(tr("Face à votre répertoire des %1").arg(mine), dim,
                      tr("Vous n'avez pas de répertoire des %1 (Partie → Répertoire d'ouvertures) : le rapport dirait ici où ses lignes en sortent.").arg(mine));
    }

    // Tree of his lines: one row per choice; the moves that follow without any other choice stay on the row.
    h += QStringLiteral("<h3>%1</h3>").arg(tr("Ce qu'il joue"));
    if (p.root.children.empty()) {
        h += QStringLiteral("<p><font color=\"%1\">%2</font></p>").arg(dim, tr("Aucun coup joué dans au moins %1 parties.").arg(m_minGames->value()));
    } else {
        h += QStringLiteral("<table cellspacing=0 cellpadding=3>") + head;
        const double overall = p.root.score.percent();
        std::vector<const PrepNode*> path;
        // Where each position is developed: a transposition names that line.
        std::unordered_map<std::string, QString> developedAt;
        auto locate = [&](auto&& self, const PrepNode& node) -> void {
            for (const PrepNode& c : node.children) {
                path.push_back(&c);
                if (!c.transposition) developedAt.emplace(c.key, plain(lineText(path)));
                self(self, c);
                path.pop_back();
            }
        };
        locate(locate, p.root);
        auto walk = [&](auto&& self, const PrepNode& node, int depth) -> void {
            for (const PrepNode& first : node.children) {
                const size_t from = path.size();
                const PrepNode* n = &first;
                path.push_back(n);
                // (a row stops where the repertoire is left: the mark below then says so for its last move)
                while (n->children.size() == 1 && n->children[0].games == n->games
                       && (!m_hasRepertoire || m_coverage.of(&n->children[0]) == m_coverage.of(n))) {
                    n = &n->children[0];
                    path.push_back(n);
                }
                const QString indent = QString(depth * 4, QChar(0xA0));
                // His results after a move of the opponent, well under his score: a line to look at
                const bool marked = !first.byPlayer && first.score.games() >= enough && first.score.percent() <= overall - 10.0;
                QString text = indent + lineText(path, from);
                if (marked) text = QStringLiteral("<font color=\"%1\">%2</font>").arg(accent, text);
                QStringList notes;
                if (n->transposition) {
                    const auto where = developedAt.find(n->key);
                    notes << tr("→ suite sous %1").arg(where != developedAt.end() ? where->second : QString());
                }
                if (m_hasRepertoire) {
                    switch (m_coverage.of(n)) {
                    case hy64db::PrepCover::Inside: notes << tr("dans votre répertoire"); break;
                    case hy64db::PrepCover::Uncovered: notes << tr("non prévu par votre répertoire"); break;
                    case hy64db::PrepCover::Ends: notes << tr("votre répertoire s'arrête"); break;
                    case hy64db::PrepCover::Outside: break;
                    }
                }
                if (!notes.isEmpty()) text += QStringLiteral(" &nbsp;<font color=\"%1\">%2</font>").arg(dim, notes.join(QStringLiteral(" · ")));
                h += row(text, &first, tr("%1 : %2").arg(m_player, plain(lineText(path))));
                self(self, *n, depth + 1);
                path.resize(from);
            }
        };
        walk(walk, p.root, 0);
        h += QStringLiteral("</table>");
        h += QStringLiteral("<p><font color=\"%1\">%2</font></p>")
                 .arg(dim, tr("Ses coups sont en gras. Les chiffres d'un coup sont ceux de toutes les parties où il a été joué dans cette position, quel que soit l'ordre des coups ; "
                             "une position atteinte par plusieurs ordres est développée une seule fois."));
    }

    if (!p.openings.empty()) {
        h += QStringLiteral("<h3>%1</h3><table cellspacing=0 cellpadding=4>").arg(tr("Ses ouvertures (ECO)"))
             + tr("<tr><th align=left>Ouverture</th><th align=right>Parties</th><th>+</th><th>=</th><th>−</th><th align=right>Son score</th></tr>");
        for (const auto& o : p.openings)
            h += QStringLiteral("<tr><td>%1</td><td align=right>%2</td>%3</tr>")
                     .arg(openingLabel(QString::fromStdString(hy64db::formatEco(o.eco))).toHtmlEscaped())
                     .arg(o.score.games())
                     .arg(scoreCells(o.score));
        h += QStringLiteral("</table>");
    }

    const uint32_t losses = p.lossesShort + p.lossesMiddle + p.lossesLong;
    if (losses) {
        h += QStringLiteral("<h3>%1</h3>").arg(tr("Ses défaites"));
        h += tr("<p>%1 avant le 25ᵉ coup, %2 du 25ᵉ au 40ᵉ, %3 après le 40ᵉ.").arg(p.lossesShort).arg(p.lossesMiddle).arg(p.lossesLong);
        if (p.averageLossMoves) h += tr(" Longueur moyenne : %1 coups").arg(p.averageLossMoves);
        if (p.averageLossMoves && p.averageWinMoves) h += tr(" (ses gains : %1 coups)").arg(p.averageWinMoves);
        h += QStringLiteral(".</p>");
        if (!p.quickLosses.empty()) {
            h += QStringLiteral("<table cellspacing=0 cellpadding=4>")
                 + tr("<tr><th align=left>Défaites les plus courtes</th><th align=right>Année</th><th align=left>Ouverture</th><th align=right>Coups</th><th></th></tr>");
            for (uint32_t id : p.quickLosses) {
                const hy64db::Record r = m_db.record(id);
                const QString opponent = QString::fromStdString(m_db.names().name(hy64db::Player, white ? r.black : r.white));
                QString see;
                if (links) {
                    m_links.push_back({id});
                    m_linkTitles.push_back(tr("%1 – %2").arg(white ? m_player : opponent, white ? opponent : m_player));
                    see = QStringLiteral("<a href=\"games:%1\" style=\"color:%2;text-decoration:none\">%3</a>").arg(m_links.size() - 1).arg(accent, tr("voir"));
                }
                h += QStringLiteral("<tr><td>%1</td><td align=right>%2</td><td>%3</td><td align=right>%4</td><td>%5</td></tr>")
                         .arg(tr("contre %1").arg(opponent.toHtmlEscaped()), (r.date >> 9) ? QString::number(r.date >> 9) : QString(),
                              r.eco ? openingLabel(QString::fromStdString(hy64db::formatEco(r.eco))).toHtmlEscaped() : QString())
                         .arg((r.plies + 1) / 2)
                         .arg(see);
            }
            h += QStringLiteral("</table>");
        }
    }
    h += errorsHtml(links);
    return h;
}
