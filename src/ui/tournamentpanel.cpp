#include "tournamentpanel.h"
#include "userdirs.h"

#include "bundledbases.h"
#include "tournamenthtml.h"
#include "participantdialog.h"
#include "engine/engineregistry.h"
#include "theme.h"
#include "tournament.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QTabWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QToolButton>
#include <QUuid>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QCursor>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QLabel* section(const QString& text)
{
    auto* l = new QLabel(text.toUpper());
    l->setObjectName("Faint");
    QFont f = l->font();
    f.setPixelSize(10);
    f.setWeight(QFont::DemiBold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    l->setFont(f);
    return l;
}

QTableWidget* table(const QStringList& headers)
{
    auto* t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setAlternatingRowColors(true);
    t->setShowGrid(false);
    t->verticalHeader()->setDefaultSectionSize(24);
    t->horizontalHeader()->setHighlightSections(false);
    return t;
}

QTableWidgetItem* cell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(align);
    return it;
}

QString points(double p) { return p == int(p) ? QString::number(int(p)) : QStringLiteral("%1½").arg(int(p) == 0 ? QString() : QString::number(int(p))); }

} // namespace

TournamentPanel::TournamentPanel(Tournament* tournament, QWidget* parent)
    : QWidget(parent)
    , m_tournament(tournament)
{
    setObjectName("PanelBody");
    QSettings s;

    // ---------------- Settings
    // Participants: each has its settings for the tournament (the engine manager is not modified).
    {
        const QByteArray saved = s.value("tournament/participants").toByteArray();
        if (!saved.isEmpty()) {
            for (const QJsonValue& v : QJsonDocument::fromJson(saved).array()) m_participants << Tournament::Participant::fromJson(v.toObject());
        } else { // old configuration: ticked engines, with their general settings
            for (const QString& id : s.value("tournament/engines").toStringList()) {
                const EngineConfig e = EngineRegistry::instance().engine(id);
                if (e.isValid()) m_participants << Tournament::Participant::fromEngine(e);
            }
        }
    }
    m_engines = new QListWidget;
    m_engines->setToolTip(tr("Double-clic : modifier le moteur et ses réglages pour le tournoi"));
    m_engines->setMaximumHeight(110);
    auto tool = [](const char* icon, const QString& tip) {
        auto* b = new QToolButton;
        b->setIcon(theme::icon(icon));
        b->setToolTip(tip);
        return b;
    };
    m_addEngine = tool("new", tr("Ajouter un moteur au tournoi…"));
    m_removeEngine = tool("minus", tr("Retirer le moteur sélectionné"));
    m_editEngine = tool("edit", tr("Modifier le moteur sélectionné et ses réglages…"));
    auto* engineButtons = new QVBoxLayout;
    engineButtons->setSpacing(2);
    engineButtons->addWidget(m_addEngine);
    engineButtons->addWidget(m_removeEngine);
    engineButtons->addWidget(m_editEngine);
    engineButtons->addStretch(1);
    auto* enginesRow = new QHBoxLayout;
    enginesRow->setSpacing(6);
    enginesRow->addWidget(m_engines, 1);
    enginesRow->addLayout(engineButtons);

    m_minutes = new QDoubleSpinBox;
    m_minutes->setRange(0.1, 180);
    m_minutes->setDecimals(1);
    m_minutes->setSingleStep(0.5);
    m_minutes->setSuffix(tr(" min"));
    m_minutes->setValue(s.value("tournament/minutes", 1.0).toDouble());
    m_increment = new QDoubleSpinBox;
    m_increment->setRange(0, 60);
    m_increment->setDecimals(1);
    m_increment->setSingleStep(0.5);
    m_increment->setSuffix(tr(" s"));
    m_increment->setValue(s.value("tournament/increment", 1.0).toDouble());
    m_cycles = new QSpinBox;
    m_cycles->setRange(1, 50);
    m_cycles->setValue(s.value("tournament/cycles", 1).toInt());
    m_double = new QCheckBox(tr("Aller-retour (chaque appariement avec les deux couleurs)"));
    m_double->setChecked(s.value("tournament/double", true).toBool());

    // Openings: a single choice (initial position, starting positions from a file, or engine book)
    m_openingMode = new QComboBox;
    m_openingMode->addItem(tr("Position initiale"), int(InitialPosition));
    m_openingMode->addItem(tr("Positions de départ"), int(StartPositions));
    m_openingMode->addItem(tr("Livre des moteurs"), int(EngineBook));
    m_openingMode->setItemData(1, tr("Chaque appariement part d'une position du fichier, la même avec les deux couleurs : le plus juste pour comparer des moteurs"),
                               Qt::ToolTipRole);
    m_openingMode->setItemData(2, tr("Les moteurs jouent les coups du livre tant que la position y figure (tirage pondéré, sans les coups à éviter)"),
                               Qt::ToolTipRole);
    {
        // old configuration: "Starting positions" checkbox, book in the Adjudication dialog
        int mode = s.value("tournament/openingMode", -1).toInt();
        if (mode < 0) mode = s.value("tournament/useOpenings", false).toBool() ? StartPositions : s.value("tournament/useBook", false).toBool() ? EngineBook : InitialPosition;
        m_openingMode->setCurrentIndex(std::clamp(mode, 0, 2));
    }
    m_openingsPath = new QLineEdit(bundled::currentPath(s.value("tournament/openings").toString()));
    m_openingsPath->setPlaceholderText(tr("Fichier EPD, FEN, PGN ou base Hydra64"));
    m_openingsPath->setToolTip(tr("EPD : une position par ligne.\nPGN ou base Hydra64 (.hy64, .hy64i) : position de départ et coups de chaque partie."));
    m_openingsBrowse = new QPushButton(theme::icon("open"), QString());
    m_openingsBrowse->setToolTip(tr("Choisir le fichier de positions…"));
    m_openingsRandom = new QCheckBox(tr("Aléatoire"));
    m_openingsRandom->setToolTip(tr("Tirer les positions au hasard plutôt que dans l'ordre du fichier"));
    m_openingsRandom->setChecked(s.value("tournament/openingsRandom", false).toBool());
    m_bookPath = new QLineEdit(s.value("tournament/book").toString());
    m_bookPath->setPlaceholderText(tr("Arbre Hydra64 (.hy64book) ou livre Polyglot (.bin)"));
    m_bookBrowse = new QPushButton(theme::icon("open"), QString());
    m_bookBrowse->setToolTip(tr("Choisir le livre des moteurs…"));
    m_bookMoves = new QSpinBox;
    m_bookMoves->setRange(1, 100);
    m_bookMoves->setPrefix(tr("jusqu'au coup "));
    m_bookMoves->setValue(s.value("tournament/bookMoves", 20).toInt());
    auto* op = new QHBoxLayout;
    op->setSpacing(6);
    op->addWidget(new QLabel(tr("Ouvertures")));
    op->addWidget(m_openingMode);
    op->addWidget(m_openingsPath, 1);
    op->addWidget(m_openingsBrowse);
    op->addWidget(m_openingsRandom);
    op->addWidget(m_bookPath, 1);
    op->addWidget(m_bookBrowse);
    op->addWidget(m_bookMoves);

    // Folder of the tournament databases: engines_tournaments/ of the user folder, unless another was
    // chosen. A folder remembered from an earlier default (Documents/Hydra64/Tournois, tournaments/)
    // is not a choice: the new default replaces it.
    QString pgnDir = QDir::fromNativeSeparators(s.value("tournament/pgnDir").toString());
    if (pgnDir.isEmpty() || pgnDir.endsWith(QLatin1String("/Hydra64/Tournois")) || QDir::cleanPath(pgnDir) == QDir::cleanPath(userdirs::root() + QStringLiteral("/tournaments")))
        pgnDir = Tournament::defaultPgnDir();
    m_pgnDir = new QLineEdit(QDir::toNativeSeparators(pgnDir));
    m_pgnDir->setToolTip(tr("Chaque tournoi crée sa base Hydra64 (.hy64) dans ce dossier, à la première partie finie ; chaque partie y est ajoutée dès qu'elle est finie"));
    m_pgnBrowse = new QPushButton(theme::icon("open"), QString());
    m_pgnBrowse->setToolTip(tr("Choisir le dossier des bases de tournoi…"));
    auto* pg = new QHBoxLayout;
    pg->setSpacing(6);
    pg->addWidget(new QLabel(tr("Bases dans")));
    pg->addWidget(m_pgnDir, 1);
    pg->addWidget(m_pgnBrowse);

    m_limit = new QComboBox;
    m_limit->addItem(tr("Pendule"), int(Tournament::Settings::Limit::Clock));
    m_limit->addItem(tr("Temps par coup"), int(Tournament::Settings::Limit::MoveTime));
    m_limit->addItem(tr("Profondeur fixe"), int(Tournament::Settings::Limit::Depth));
    m_limit->setCurrentIndex(std::clamp(s.value("tournament/limit", 0).toInt(), 0, 2));
    m_moveTime = new QDoubleSpinBox;
    m_moveTime->setRange(0.05, 600);
    m_moveTime->setDecimals(2);
    m_moveTime->setSuffix(tr(" s"));
    m_moveTime->setValue(s.value("tournament/moveTime", 1.0).toDouble());
    m_depth = new QSpinBox;
    m_depth->setRange(1, 99);
    m_depth->setSuffix(tr(" demi-coups"));
    m_depth->setValue(s.value("tournament/depth", 12).toInt());
    m_schedule = new QComboBox;
    m_schedule->addItem(tr("Toutes rondes"), int(Tournament::Settings::Schedule::RoundRobin));
    m_schedule->addItem(tr("Gauntlet (le premier contre chacun)"), int(Tournament::Settings::Schedule::Gauntlet));
    m_schedule->addItem(tr("Système suisse (« Tours » = rondes)"), int(Tournament::Settings::Schedule::Swiss));
    m_schedule->setItemData(2, tr("Chaque ronde est appariée d'après les résultats : groupes de points, sans revanche, couleurs équilibrées, exempt si nombre impair"),
                            Qt::ToolTipRole);
    m_schedule->setCurrentIndex(std::clamp(s.value("tournament/schedule", 0).toInt(), 0, 2));
    m_schedule->setToolTip(tr("Gauntlet : le premier participant de la liste joue contre chacun des autres (test d'une nouvelle version)"));
    m_options = new QPushButton(tr("Arbitrage…"));
    m_options->setToolTip(tr("Arbitrage automatique (nulle, abandon), évaluations des moteurs dans le PGN, nombre de parties simultanées"));
    m_overview = new QPushButton(tr("Vue d'ensemble"));
    m_overview->setCheckable(true);
    m_overview->setToolTip(tr("Tous les échiquiers des parties en cours ; un clic sur un échiquier montre sa partie dans la notation, un double-clic (ou un double-clic dans la liste des parties) la suit en grand"));
    auto* tc = new QHBoxLayout;
    tc->setSpacing(6);
    tc->addWidget(m_limit);
    tc->addWidget(m_minutes);
    tc->addWidget(m_increment);
    tc->addWidget(m_moveTime);
    tc->addWidget(m_depth);
    tc->addSpacing(10);
    tc->addWidget(new QLabel(tr("Tours")));
    tc->addWidget(m_cycles);
    tc->addStretch(1);
    auto* sc = new QHBoxLayout;
    sc->setSpacing(6);
    sc->addWidget(m_schedule, 1);
    sc->addWidget(m_options);
    sc->addWidget(m_overview);

    m_summary = new QLabel;
    m_summary->setObjectName("Faint");

    m_new = new QPushButton(theme::icon("new"), tr("Nouveau tournoi"));
    m_new->setObjectName("TournamentNew");
    m_new->setToolTip(tr("Abandonne le tournoi interrompu et en lance un nouveau avec les réglages affichés"));
    m_run = new QPushButton;
    m_run->setObjectName("Primary");
    m_run->setProperty("role", QStringLiteral("run"));
    m_export = new QPushButton(theme::icon("save"), QString());
    m_export->setToolTip(tr("Exporter le tournoi : toutes les parties en PGN, ou une page HTML avec le classement et un échiquier pour rejouer les parties"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_new);
    buttons->addWidget(m_run);
    m_openPgn = new QPushButton(theme::icon("database"), QString());
    buttons->addStretch(1);
    buttons->addWidget(m_openPgn);
    buttons->addWidget(m_export);

    m_status = new QLabel;
    m_status->setWordWrap(true);

    auto* top = new QWidget;
    auto* tl = new QVBoxLayout(top);
    tl->setContentsMargins(12, 10, 12, 6);
    tl->setSpacing(6);
    tl->addWidget(section(tr("Participants")));
    tl->addLayout(enginesRow);
    tl->addLayout(tc);
    tl->addLayout(sc);
    tl->addWidget(m_double);
    tl->addLayout(op);
    tl->addLayout(pg);
    tl->addWidget(m_summary);
    tl->addLayout(buttons);
    tl->addWidget(m_status);

    // ---------------- Standings and games
    m_standings = table({tr("#"), tr("Moteur"), tr("Points"), tr("Parties"), tr("+"), tr("="), tr("−"), tr("%"), tr("Elo ±"), tr("LOS")});
    m_standings->horizontalHeaderItem(8)->setToolTip(tr("Écart d'Elo estimé contre ses adversaires, avec la marge d'erreur à 95 %"));
    m_standings->horizontalHeaderItem(9)->setToolTip(tr("Probabilité d'être plus fort que ses adversaires (likelihood of superiority)"));
    m_cross = table({});
    m_cross->setToolTip(tr("Tableau croisé : points de la ligne contre la colonne"));
    m_standings->setObjectName("Standings");
    m_standings->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_standings->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_games = table({tr("N°"), tr("Blancs"), tr("Noirs"), tr("Résultat"), tr("Motif")});
    m_games->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_games->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_games->setToolTip(tr("Double-clic : afficher la partie sur l'échiquier"));

    auto* standingsBox = new QWidget;
    auto* sl = new QVBoxLayout(standingsBox);
    sl->setContentsMargins(0, 4, 0, 0);
    sl->setSpacing(4);
    auto* standingTabs = new QTabWidget;
    standingTabs->setDocumentMode(true);
    standingTabs->addTab(m_standings, tr("Classement"));
    standingTabs->addTab(m_cross, tr("Tableau croisé"));
    sl->addWidget(standingTabs);
    auto* gamesBox = new QWidget;
    auto* gl = new QVBoxLayout(gamesBox);
    gl->setContentsMargins(0, 4, 0, 0);
    gl->setSpacing(4);
    auto* gt = section(tr("Parties"));
    gt->setContentsMargins(12, 0, 0, 0);
    gl->addWidget(gt);
    gl->addWidget(m_games);

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(standingsBox);
    split->addWidget(gamesBox);
    split->setChildrenCollapsible(false); // neither the standings nor the games disappear when the splitter is dragged
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(top);
    layout->addWidget(split, 1);

    connect(m_cycles, &QSpinBox::valueChanged, this, &TournamentPanel::updateSummary);
    connect(m_schedule, &QComboBox::currentIndexChanged, this, &TournamentPanel::updateSummary);
    connect(m_limit, &QComboBox::currentIndexChanged, this, &TournamentPanel::updateLimit);
    connect(m_options, &QPushButton::clicked, this, &TournamentPanel::editOptions);
    connect(m_overview, &QPushButton::clicked, this, &TournamentPanel::overviewRequested);
    updateLimit();
    connect(m_double, &QCheckBox::toggled, this, &TournamentPanel::updateSummary);
    connect(m_addEngine, &QToolButton::clicked, this, [this] { editParticipant(-1); });
    connect(m_editEngine, &QToolButton::clicked, this, [this] { editParticipant(m_engines->currentRow()); });
    connect(m_removeEngine, &QToolButton::clicked, this, [this] { removeParticipant(m_engines->currentRow()); });
    connect(m_engines, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (!m_tournament->isRunning()) editParticipant(m_engines->row(it));
    });
    connect(m_engines, &QListWidget::currentRowChanged, this, &TournamentPanel::updateSummary);
    connect(m_openingMode, &QComboBox::currentIndexChanged, this, &TournamentPanel::updateOpenings);
    connect(m_tournament, &Tournament::changed, this, &TournamentPanel::updateOpenings);
    connect(m_openingsBrowse, &QPushButton::clicked, this, &TournamentPanel::chooseOpenings);
    connect(m_bookBrowse, &QPushButton::clicked, this, &TournamentPanel::chooseBook);
    updateOpenings();
    connect(m_new, &QPushButton::clicked, this, &TournamentPanel::newTournament);
    connect(m_run, &QPushButton::clicked, this, &TournamentPanel::toggleRunning);
    connect(m_export, &QPushButton::clicked, this, &TournamentPanel::exportGames);
    connect(m_openPgn, &QPushButton::clicked, this, [this] { emit databaseRequested(m_tournament->settings().pgnFile); });
    connect(m_pgnBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Dossier des bases de tournoi"), m_pgnDir->text());
        if (!dir.isEmpty()) m_pgnDir->setText(QDir::toNativeSeparators(dir));
    });
    connect(m_games, &QTableWidget::cellDoubleClicked, this, [this](int row) { emit gameViewRequested(row); });
    connect(m_tournament, &Tournament::changed, this, &TournamentPanel::refresh);
    connect(m_tournament, &Tournament::message, this, &TournamentPanel::statusMessage);
    connect(&EngineRegistry::instance(), &EngineRegistry::changed, this, [this] {
        refreshEngines();
        updateSummary();
        refresh();
    });

    refreshEngines();
    refreshSettings();
    updateSummary();
    refresh();
}

void TournamentPanel::refreshEngines()
{
    const int row = m_engines->currentRow();
    QSignalBlocker block(m_engines);
    m_engines->clear();
    const EngineRegistry& reg = EngineRegistry::instance();
    for (const Tournament::Participant& p : m_participants) {
        const EngineConfig engine = reg.engine(p.engineId);
        auto* item = new QListWidgetItem(m_engines);
        if (!engine.isValid()) {
            item->setText(tr("%1 — moteur indisponible").arg(p.name));
            item->setForeground(theme::Bad);
            item->setToolTip(tr("Ce moteur a été retiré de la gestion des moteurs"));
            continue;
        }
        // Settings specific to the tournament: those differing from the engine manager.
        QStringList own;
        for (const UciOption& o : engine.options) {
            if (o.type == QLatin1String("button") || o.name.compare(QLatin1String("MultiPV"), Qt::CaseInsensitive) == 0) continue;
            const QString v = p.values.value(o.name, o.defaultValue);
            if (v != engine.value(o.name)) own << QStringLiteral("%1 = %2").arg(o.name, v);
        }
        QString text = p.name;
        if (p.name != engine.name) text += QStringLiteral("  ·  ") + engine.name;
        if (!own.isEmpty()) text += QStringLiteral("  ·  ") + tr("%n réglage(s) propre(s)", nullptr, int(own.size()));
        item->setText(text);
        item->setToolTip(own.isEmpty() ? tr("%1 : réglages de la gestion des moteurs").arg(engine.name)
                                       : tr("%1, réglé pour le tournoi :\n%2").arg(engine.name, own.join('\n')));
    }
    m_engines->setCurrentRow(std::min(row, int(m_engines->count()) - 1));
}

void TournamentPanel::updateSummary()
{
    const int n = int(m_participants.size());
    const bool running = m_tournament->isRunning();
    const int games = Tournament::gameCount(n, Tournament::Settings::Schedule(m_schedule->currentData().toInt()), m_double->isChecked(), m_cycles->value());
    m_summary->setText(n < 2 ? tr("Ajoutez au moins deux moteurs avec « + ».") : tr("%1 moteurs, %2 parties").arg(n).arg(games));
    m_new->setEnabled(n >= 2 && !running);
    m_new->setVisible(!running && !m_tournament->isEmpty() && !m_tournament->isComplete()); // only next to "Resume"
    m_run->setEnabled(running || (!m_tournament->isEmpty() && !m_tournament->isComplete()) || n >= 2);
    m_addEngine->setEnabled(!running);
    m_removeEngine->setEnabled(!running && m_engines->currentRow() >= 0);
    m_editEngine->setEnabled(!running && m_engines->currentRow() >= 0);
}

void TournamentPanel::saveParticipants() const
{
    QJsonArray list;
    for (const Tournament::Participant& p : m_participants) list.append(p.toJson());
    QSettings().setValue("tournament/participants", QJsonDocument(list).toJson(QJsonDocument::Compact));
}

QString TournamentPanel::uniqueName(QString name, int except) const
{
    auto taken = [&](const QString& n) {
        for (int i = 0; i < m_participants.size(); ++i)
            if (i != except && m_participants[i].name == n) return true;
        return false;
    };
    if (!taken(name)) return name;
    for (int k = 2;; ++k)
        if (!taken(QStringLiteral("%1 (%2)").arg(name).arg(k))) return QStringLiteral("%1 (%2)").arg(name).arg(k);
}

void TournamentPanel::addParticipant(Tournament::Participant p)
{
    p.name = uniqueName(p.name, -1);
    m_participants << p;
    saveParticipants();
    refreshEngines();
    m_engines->setCurrentRow(int(m_participants.size()) - 1);
    updateSummary();
}

void TournamentPanel::replaceParticipant(int row, Tournament::Participant p)
{
    if (row < 0 || row >= m_participants.size()) return;
    p.id = m_participants[row].id; // same participant: the tournament in progress recognises it
    p.name = uniqueName(p.name, row);
    m_participants[row] = p;
    saveParticipants();
    refreshEngines();
    updateSummary();
}

void TournamentPanel::removeParticipant(int row)
{
    if (row < 0 || row >= m_participants.size()) return;
    m_participants.removeAt(row);
    saveParticipants();
    refreshEngines();
    updateSummary();
}

void TournamentPanel::editParticipant(int row)
{
    if (EngineRegistry::instance().engines().isEmpty()) {
        QMessageBox::information(this, tr("Ajouter un moteur"), tr("Aucun moteur installé : ajoutez-en d'abord dans la gestion des moteurs."));
        return;
    }
    const bool isNew = row < 0 || row >= m_participants.size();
    ParticipantDialog dlg(isNew ? Tournament::Participant{} : m_participants[row], this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (isNew) {
        Tournament::Participant p = dlg.participant();
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        addParticipant(p);
    } else {
        replaceParticipant(row, dlg.participant());
    }
}

void TournamentPanel::refreshSettings()
{
    if (m_tournament->isEmpty()) return;
    const Tournament::Settings& s = m_tournament->settings();
    m_participants = s.participants;
    refreshEngines();
    m_minutes->setValue(s.baseMs / 60000.0);
    m_increment->setValue(s.incMs / 1000.0);
    m_cycles->setValue(s.cycles);
    m_double->setChecked(s.doubleRound);
    m_schedule->setCurrentIndex(m_schedule->findData(int(s.schedule)));
    m_limit->setCurrentIndex(m_limit->findData(int(s.limit)));
    m_moveTime->setValue(s.moveTimeMs / 1000.0);
    m_depth->setValue(s.depth);
    updateLimit();
    m_openingMode->setCurrentIndex(!s.openingsFile.isEmpty() ? StartPositions : !s.bookFile.isEmpty() ? EngineBook : InitialPosition);
    if (!s.openingsFile.isEmpty()) m_openingsPath->setText(bundled::currentPath(s.openingsFile));
    if (!s.bookFile.isEmpty()) m_bookPath->setText(s.bookFile);
    m_bookMoves->setValue(s.bookMaxMoves);
    m_openingsRandom->setChecked(s.openingsRandom);
    updateOpenings();
}

void TournamentPanel::updateOpenings()
{
    const int mode = m_openingMode->currentData().toInt();
    const bool running = m_tournament->isRunning();
    for (QWidget* w : std::initializer_list<QWidget*>{m_openingsPath, m_openingsBrowse, m_openingsRandom}) {
        w->setVisible(mode == StartPositions);
        w->setEnabled(!running);
    }
    for (QWidget* w : std::initializer_list<QWidget*>{m_bookPath, m_bookBrowse, m_bookMoves}) {
        w->setVisible(mode == EngineBook);
        w->setEnabled(!running);
    }
}

void TournamentPanel::chooseBook()
{
    const QString start = m_bookPath->text().isEmpty() ? userdirs::startDir(userdirs::Kind::OpeningBook) : QFileInfo(m_bookPath->text()).absolutePath();
    const QString p = QFileDialog::getOpenFileName(this, tr("Livre des moteurs"), start, tr("Arbres d'ouverture (*.hy64book *.bin);;Tous les fichiers (*)"));
    if (p.isEmpty()) return;
    userdirs::remember(userdirs::Kind::OpeningBook, p);
    m_bookPath->setText(QDir::toNativeSeparators(p));
    m_openingMode->setCurrentIndex(EngineBook);
}

void TournamentPanel::setOverviewChecked(bool on) { m_overview->setChecked(on); }

void TournamentPanel::updateLimit()
{
    const auto limit = Tournament::Settings::Limit(m_limit->currentData().toInt());
    m_minutes->setVisible(limit == Tournament::Settings::Limit::Clock);
    m_increment->setVisible(limit == Tournament::Settings::Limit::Clock);
    m_moveTime->setVisible(limit == Tournament::Settings::Limit::MoveTime);
    m_depth->setVisible(limit == Tournament::Settings::Limit::Depth);
}

void TournamentPanel::editOptions()
{
    QSettings s;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Arbitrage et options du tournoi"));
    auto* comments = new QCheckBox(tr("Évaluation, profondeur et temps de chaque coup en commentaire (PGN)"));
    comments->setChecked(s.value("tournament/engineComments", true).toBool());
    auto* ponder = new QCheckBox(tr("Réflexion permanente des moteurs (pendant le temps de l'adversaire)"));
    ponder->setChecked(s.value("tournament/ponder", false).toBool());
    ponder->setToolTip(tr("Option UCI Ponder : comme sur un vrai échiquier, mais deux moteurs calculent à la fois (deux fois plus de processeurs)"));
    auto* draw = new QCheckBox(tr("Nulle quand les deux moteurs voient l'égalité"));
    draw->setChecked(s.value("tournament/drawAdj", false).toBool());
    auto spin = [](int lo, int hi, int v, const QString& suffix) {
        auto* b = new QSpinBox;
        b->setRange(lo, hi);
        b->setValue(v);
        b->setSuffix(suffix);
        return b;
    };
    auto* concurrency = spin(1, 64, s.value("tournament/concurrency", 1).toInt(), QString());
    concurrency->setToolTip(tr("Nombre de parties jouées en même temps. Chaque partie lance ses deux moteurs : comptez les processeurs "
                               "(parties × 2 × Threads de chaque moteur) pour que les moteurs ne se gênent pas."));
    auto* drawFrom = spin(1, 500, s.value("tournament/drawFrom", 40).toInt(), QString());
    auto* drawMoves = spin(1, 100, s.value("tournament/drawMoves", 8).toInt(), tr(" coups"));
    auto* drawScore = spin(0, 500, s.value("tournament/drawScore", 10).toInt(), tr(" cp"));
    auto* resign = new QCheckBox(tr("Abandon quand les deux moteurs voient un camp perdant"));
    resign->setChecked(s.value("tournament/resignAdj", false).toBool());
    auto* resignMoves = spin(1, 100, s.value("tournament/resignMoves", 4).toInt(), tr(" coups"));
    auto* resignScore = spin(100, 30000, s.value("tournament/resignScore", 700).toInt(), tr(" cp"));
    auto* tables = new QCheckBox(tr("Arrêter la partie dès que la position est dans les tables de finales"));
    tables->setChecked(s.value("tournament/tablebaseAdj", false).toBool());
    tables->setToolTip(tr("Le résultat est celui des tables (règle des 50 coups comprise), sans faire jouer la finale. Tables choisies dans "
                          "Préférences → Finales ; sans table couvrant la position, la partie continue."));
    auto* form = new QFormLayout;
    form->addRow(QString(), comments);
    form->addRow(QString(), ponder);
    form->addRow(tr("Parties simultanées"), concurrency);
    form->addRow(QString(), draw);
    form->addRow(tr("À partir du coup"), drawFrom);
    form->addRow(tr("Pendant"), drawMoves);
    form->addRow(tr("Évaluation au plus (±)"), drawScore);
    form->addRow(QString(), resign);
    form->addRow(tr("Pendant"), resignMoves);
    form->addRow(tr("Évaluation au moins"), resignScore);
    form->addRow(QString(), tables);
    auto* note = new QLabel(tr("Les deux moteurs doivent être d'accord sur chacun des derniers coups. Cela raccourcit beaucoup les tournois "
                               "sans changer les résultats (réglages courants : nulle 8 coups à ±0,10 après le coup 40 ; abandon 4 coups à 7,00)."));
    note->setWordWrap(true);
    note->setObjectName("Faint");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Enregistrer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    s.setValue("tournament/engineComments", comments->isChecked());
    s.setValue("tournament/ponder", ponder->isChecked());
    s.setValue("tournament/concurrency", concurrency->value());
    s.setValue("tournament/drawAdj", draw->isChecked());
    s.setValue("tournament/drawFrom", drawFrom->value());
    s.setValue("tournament/drawMoves", drawMoves->value());
    s.setValue("tournament/drawScore", drawScore->value());
    s.setValue("tournament/resignAdj", resign->isChecked());
    s.setValue("tournament/resignMoves", resignMoves->value());
    s.setValue("tournament/resignScore", resignScore->value());
    s.setValue("tournament/tablebaseAdj", tables->isChecked());
}

void TournamentPanel::chooseOpenings()
{
    QSettings s;
    // No collection chosen: the folder of the bundled collections
    const QString start = m_openingsPath->text().isEmpty() ? bundled::databasesDir() + QStringLiteral("/tournament-openings")
                                                           : QFileInfo(m_openingsPath->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Positions de départ"), start,
                                                      tr("Positions (*.hy64 *.hy64i *.epd *.pgn *.txt);;Base Hydra64 (*.hy64 *.hy64i);;EPD (*.epd *.txt);;Parties PGN (*.pgn);;"
                                                         "Tous les fichiers (*)"));
    if (path.isEmpty()) return;
    m_openingsPath->setText(path);
    m_openingMode->setCurrentIndex(StartPositions);
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    QString error;
    const QStringList list = Tournament::loadOpenings(path, &error);
    emit statusMessage(list.isEmpty() ? error : tr("%n position(s) de départ", nullptr, int(list.size())));
}

void TournamentPanel::newTournament()
{
    if (m_tournament->playedCount() > 0 && !m_tournament->isComplete()
        && QMessageBox::question(this, tr("Nouveau tournoi"), tr("Le tournoi en cours n'est pas terminé. L'abandonner et en commencer un nouveau ?"))
               != QMessageBox::Yes)
        return;
    Tournament::Settings s;
    s.participants = m_participants;
    s.baseMs = qint64(m_minutes->value() * 60000);
    s.incMs = qint64(m_increment->value() * 1000);
    s.cycles = m_cycles->value();
    s.doubleRound = m_double->isChecked();
    s.schedule = Tournament::Settings::Schedule(m_schedule->currentData().toInt());
    s.limit = Tournament::Settings::Limit(m_limit->currentData().toInt());
    s.moveTimeMs = int(m_moveTime->value() * 1000);
    s.depth = m_depth->value();
    {
        QSettings o;
        s.engineComments = o.value("tournament/engineComments", true).toBool();
        s.ponder = o.value("tournament/ponder", false).toBool();
        s.concurrency = std::clamp(o.value("tournament/concurrency", 1).toInt(), 1, 64);
        s.drawAdjudication = o.value("tournament/drawAdj", false).toBool();
        s.drawFromMove = o.value("tournament/drawFrom", 40).toInt();
        s.drawMoves = o.value("tournament/drawMoves", 8).toInt();
        s.drawScoreCp = o.value("tournament/drawScore", 10).toInt();
        s.resignAdjudication = o.value("tournament/resignAdj", false).toBool();
        s.resignMoves = o.value("tournament/resignMoves", 4).toInt();
        s.resignScoreCp = o.value("tournament/resignScore", 700).toInt();
        s.tablebaseAdjudication = o.value("tournament/tablebaseAdj", false).toBool();
    }
    const int mode = m_openingMode->currentData().toInt();
    if (mode == EngineBook) {
        s.bookFile = QDir::fromNativeSeparators(m_bookPath->text().trimmed());
        s.bookMaxMoves = m_bookMoves->value();
        if (s.bookFile.isEmpty()) {
            QMessageBox::warning(this, tr("Nouveau tournoi"), tr("Choisissez le livre des moteurs."));
            return;
        }
    }
    if (mode == StartPositions) {
        s.openingsFile = m_openingsPath->text().trimmed();
        if (s.openingsFile.isEmpty()) {
            QMessageBox::warning(this, tr("Nouveau tournoi"), tr("Choisissez un fichier de positions de départ."));
            return;
        }
        s.openingsRandom = m_openingsRandom->isChecked();
    }
    QString dir = QDir::fromNativeSeparators(m_pgnDir->text().trimmed());
    if (dir.isEmpty()) dir = Tournament::defaultPgnDir();
    s.pgnFile = Tournament::newPgnPath(dir);
    QString error;
    if (!m_tournament->setup(s, &error)) {
        QMessageBox::warning(this, tr("Nouveau tournoi"), error);
        return;
    }
    QSettings st;
    saveParticipants();
    st.setValue("tournament/minutes", m_minutes->value());
    st.setValue("tournament/increment", m_increment->value());
    st.setValue("tournament/cycles", s.cycles);
    st.setValue("tournament/double", s.doubleRound);
    st.setValue("tournament/schedule", m_schedule->currentIndex());
    st.setValue("tournament/limit", m_limit->currentIndex());
    st.setValue("tournament/moveTime", m_moveTime->value());
    st.setValue("tournament/depth", m_depth->value());
    st.setValue("tournament/openingMode", m_openingMode->currentIndex());
    st.setValue("tournament/book", QDir::fromNativeSeparators(m_bookPath->text().trimmed()));
    st.setValue("tournament/bookMoves", m_bookMoves->value());
    st.setValue("tournament/openings", m_openingsPath->text().trimmed());
    st.setValue("tournament/openingsRandom", m_openingsRandom->isChecked());
    // Only a folder chosen by the user is remembered: the default one follows the user folder
    if (QDir::cleanPath(dir) == QDir::cleanPath(Tournament::defaultPgnDir())) st.remove("tournament/pgnDir");
    else st.setValue("tournament/pgnDir", m_pgnDir->text().trimmed());
    emit statusMessage(tr("Parties du tournoi enregistrées dans %1").arg(QDir::toNativeSeparators(s.pgnFile)));
    m_tournament->start();
}

void TournamentPanel::toggleRunning()
{
    if (m_tournament->isRunning()) m_tournament->stop();
    else if (m_tournament->isEmpty() || m_tournament->isComplete()) newTournament();
    else m_tournament->start();
}

bool TournamentPanel::writeHtml(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(tournamentHtml::page(*m_tournament, QFileInfo(path).completeBaseName()).toUtf8());
    return true;
}

void TournamentPanel::exportHtml()
{
    if (m_tournament->playedCount() == 0) {
        emit statusMessage(tr("Aucune partie jouée à exporter"));
        return;
    }
    // A single page for the whole tournament: beyond a few thousand games it becomes heavy.
    if (m_tournament->playedCount() > 3000
        && QMessageBox::question(this, tr("Exporter en page HTML"),
                                 tr("Ce tournoi compte %1 parties : la page fera plusieurs dizaines de Mo et sera lente à ouvrir. Continuer ?").arg(m_tournament->playedCount()))
               != QMessageBox::Yes)
        return;
    QSettings s;
    const QString base = m_tournament->settings().pgnFile.isEmpty() ? tr("Tournoi") : QFileInfo(m_tournament->settings().pgnFile).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter le tournoi en page HTML"), s.value("lastDir").toString() + QLatin1Char('/') + base + QStringLiteral(".html"),
                                                      tr("Page HTML (*.html)"));
    if (path.isEmpty()) return;
    QString error;
    if (!writeHtml(path, &error)) {
        QMessageBox::warning(this, tr("Exporter"), error);
        return;
    }
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    emit statusMessage(tr("Tournoi exporté dans %1").arg(QDir::toNativeSeparators(path)));
}

void TournamentPanel::exportGames()
{
    const QString text = m_tournament->exportPgn();
    if (text.isEmpty()) {
        emit statusMessage(tr("Aucune partie jouée à exporter"));
        return;
    }
    // The button offers both forms
    QMenu menu(this);
    QAction* asPgn = menu.addAction(tr("Parties en PGN…"));
    QAction* asHtml = menu.addAction(tr("Page HTML avec échiquier…"));
    QAction* chosen = qobject_cast<QPushButton*>(sender()) ? menu.exec(QCursor::pos()) : asPgn;
    if (chosen == asHtml) {
        exportHtml();
        return;
    }
    if (chosen != asPgn) return;
    QSettings s;
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter les parties du tournoi"),
                                                      s.value("lastDir").toString() + QStringLiteral("/Tournoi.pgn"), tr("Parties PGN (*.pgn)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Exporter"), f.errorString());
        return;
    }
    f.write(text.toUtf8());
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    emit statusMessage(tr("%1 parties enregistrées dans %2").arg(m_tournament->playedCount()).arg(path));
}

void TournamentPanel::refresh()
{
    const Tournament* t = m_tournament;
    const bool running = t->isRunning();
    const int checked = int(m_participants.size());
    // A single button depending on the state: no tournament → "Start the tournament"; in progress →
    // "Interrupt"; interrupted → "Resume" (with "New tournament" next to it to
    // abandon it); finished → "New tournament".
    const bool paused = !running && !t->isEmpty() && !t->isComplete();
    m_run->setText(running ? tr("Interrompre") : paused ? tr("Reprendre le tournoi") : t->isComplete() && !t->isEmpty() ? tr("Nouveau tournoi") : tr("Lancer le tournoi"));
    m_run->setIcon(theme::icon(running ? "stop" : t->isComplete() && !t->isEmpty() ? "new" : "play"));
    m_run->setToolTip(running ? tr("Arrête les parties en cours ; elles seront rejouées à la reprise")
                      : paused ? tr("Continue le tournoi : %1 parties jouées sur %2").arg(t->playedCount()).arg(t->games().size())
                               : tr("Crée le tournoi avec les réglages affichés et le lance"));
    m_run->setEnabled(running || (!t->isEmpty() && !t->isComplete()) || checked >= 2);
    m_export->setEnabled(t->playedCount() > 0);
    const QString pgnFile = t->settings().pgnFile;
    m_openPgn->setEnabled(!pgnFile.isEmpty() && QFileInfo::exists(pgnFile));
    m_openPgn->setToolTip(pgnFile.isEmpty() ? tr("Ce tournoi n'a pas de base") : tr("Ouvrir %1 dans la page Bases").arg(QDir::toNativeSeparators(pgnFile)));
    for (QWidget* w : std::initializer_list<QWidget*>{m_minutes, m_increment, m_cycles, m_double, m_openingMode, m_pgnDir, m_pgnBrowse, m_new, m_schedule,
                                                      m_limit, m_moveTime, m_depth, m_options})
        w->setEnabled(!running);
    if (!running) m_new->setEnabled(checked >= 2);
    updateSummary(); // "+", "−", "Edit" buttons depending on the state

    if (t->isEmpty()) m_status->setText(tr("Aucun tournoi. Ajoutez des moteurs avec « + », puis « Lancer le tournoi »."));
    else if (running && t->currentIndex() >= 0) {
        const Tournament::GameRecord& g = t->games()[t->currentIndex()];
        m_status->setText(tr("Partie %1/%2 : %3 – %4").arg(t->currentIndex() + 1).arg(t->games().size()).arg(g.whiteName, g.blackName));
    } else if (t->isComplete()) m_status->setText(tr("Tournoi terminé : %1 parties jouées.").arg(t->games().size()));
    else m_status->setText(tr("Tournoi interrompu : %1 parties jouées sur %2.").arg(t->playedCount()).arg(t->games().size()));

    const QList<Tournament::Standing> table = t->standings();
    m_standings->setRowCount(int(table.size()));
    for (int i = 0; i < table.size(); ++i) {
        const Tournament::Standing& s = table[i];
        const Qt::Alignment right = Qt::AlignRight | Qt::AlignVCenter;
        m_standings->setItem(i, 0, cell(QString::number(i + 1), right));
        m_standings->setItem(i, 1, cell(s.name));
        m_standings->setItem(i, 2, cell(points(s.points), right));
        m_standings->setItem(i, 3, cell(QString::number(s.played), right));
        m_standings->setItem(i, 4, cell(QString::number(s.wins), right));
        m_standings->setItem(i, 5, cell(QString::number(s.draws), right));
        m_standings->setItem(i, 6, cell(QString::number(s.losses), right));
        m_standings->setItem(i, 7, cell(s.played ? QStringLiteral("%1").arg(100.0 * s.points / s.played, 0, 'f', 0) : QStringLiteral("–"), right));
        m_standings->setItem(i, 8, cell(s.hasElo ? QStringLiteral("%1 ± %2").arg(s.elo >= 0 ? QStringLiteral("+%1").arg(s.elo, 0, 'f', 0) : QString::number(s.elo, 'f', 0))
                                                      .arg(s.margin, 0, 'f', 0)
                                                : QStringLiteral("–"),
                                        right));
        m_standings->setItem(i, 9, cell(s.played ? QStringLiteral("%1 %").arg(100.0 * s.los, 0, 'f', 1) : QStringLiteral("–"), right));
    }
    // Crosstable: points of the row against the column (ranking order)
    m_cross->setRowCount(int(table.size()));
    m_cross->setColumnCount(int(table.size()) + 1);
    QStringList crossHeaders{tr("Moteur")};
    for (int j = 0; j < table.size(); ++j) crossHeaders << QString::number(j + 1);
    m_cross->setHorizontalHeaderLabels(crossHeaders);
    for (int i = 0; i < table.size(); ++i) {
        m_cross->setItem(i, 0, cell(QStringLiteral("%1. %2").arg(i + 1).arg(table[i].name)));
        for (int j = 0; j < table.size(); ++j) {
            if (i == j) {
                m_cross->setItem(i, j + 1, cell(QStringLiteral("×"), Qt::AlignCenter));
                continue;
            }
            double pts = 0;
            int played = 0;
            for (const Tournament::GameRecord& g : t->games()) {
                if (g.result.isEmpty()) continue;
                const bool white = g.whiteId == table[i].id && g.blackId == table[j].id, black = g.blackId == table[i].id && g.whiteId == table[j].id;
                if (!white && !black) continue;
                ++played;
                if (g.result == QLatin1String("1/2-1/2")) pts += 0.5;
                else if ((g.result == QLatin1String("1-0")) == white) pts += 1;
            }
            m_cross->setItem(i, j + 1, cell(played ? QStringLiteral("%1/%2").arg(points(pts)).arg(played) : QString(), Qt::AlignCenter));
        }
    }

    const QList<Tournament::GameRecord>& games = t->games();
    m_games->setRowCount(int(games.size()));
    for (int i = 0; i < games.size(); ++i) {
        const Tournament::GameRecord& g = games[i];
        const bool live = i == t->currentIndex();       // the game followed on the board
        const bool running = live || t->runningGame(i);  // simultaneous games: the other games in progress
        QString result = g.result;
        result.replace(QStringLiteral("1/2"), QStringLiteral("½"));
        m_games->setItem(i, 0, cell(g.round ? tr("%1 · R%2").arg(i + 1).arg(g.round) : QString::number(i + 1), Qt::AlignRight | Qt::AlignVCenter));
        m_games->setItem(i, 1, cell(g.whiteName));
        m_games->setItem(i, 2, cell(g.blackName));
        m_games->setItem(i, 3, cell(running ? tr("en cours") : result, Qt::AlignCenter));
        m_games->setItem(i, 4, cell(running && !live ? tr("double-clic : la suivre") : g.reason));
        if (!g.opening.isEmpty())
            for (int c = 0; c < m_games->columnCount(); ++c) m_games->item(i, c)->setToolTip(tr("Départ : %1").arg(g.opening));
        if (live)
            for (int c = 0; c < m_games->columnCount(); ++c) m_games->item(i, c)->setForeground(theme::Accent);
    }
    if (t->currentIndex() >= 0) m_games->scrollToItem(m_games->item(t->currentIndex(), 0));
}
