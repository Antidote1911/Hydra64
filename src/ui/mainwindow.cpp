#include "mainwindow.h"
#include "userdirs.h"
#include "tabengine.h"
#include "quizdialog.h"
#include "chessbase/cbtexts.h"
#include "indexdialog.h"
#include "chess/repertoire.h"
#include "autoevaluator.h"
#include "boardviewer.h"
#include "bookpanel.h"
#include "display.h"
#include "boardwidget.h"
#include "clockpanel.h"
#include "chess/pgn.h"
#include "enginegame.h"
#include "enginemanager.h"
#include "enginepanel.h"
#include "playdialog.h"
#include "playenginepanel.h"
#include "positioneditor.h"
#include "modulewidgets.h"
#include "preferences.h"
#include "evalbar.h"
#include "evalgraph.h"
#include "gameinfodialog.h"
#include "gamelist.h"
#include "gamesession.h"
#include "notationview.h"
#include "theme.h"
#include "tournament.h"
#include "tournamentoverview.h"
#include "tournamenthtml.h"
#include "tournamentpanel.h"
#include "hy64source.h"
#include "convertdialog.h"
#include "solutiontestdialog.h"
#include "deepanalysis.h"
#include "deeppanel.h"
#include "headersearchdialog.h"
#include "coach.h"
#include "playerreport.h"
#include "preparationdialog.h"
#include "guessdialog.h"
#include "pieceheatdialog.h"
#include "namefixdialog.h"
#include "playerencyclopedia.h"
#include "assistedanalysis.h"
#include "eventtable.h"
#include "openings.h"
#include "gamepreview.h"
#include "autoplayer.h"
#include "training.h"
#include "addexercise.h"
#include "collectioncheck.h"
#include "tablebasepanel.h"
#include "filtergraph.h"
#include "ecobrowser.h"
#include "surveybrowser.h"
#include "piecetracker.h"
#include "calcvar.h"
#include "openingreport.h"
#include "chess/polyglot.h"
#include "db/convert.h"
#include "db/positionindex.h"

#include <DockAreaTitleBar.h>
#include <DockAreaWidget.h>
#include <DockContainerWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QEventLoop>
#include <QMenuBar>
#include <QProgressDialog>
#include <QImage>
#include <QGuiApplication>
#include <QThread>
#include <QInputDialog>
#include <QStackedWidget>
#include <QPageSize>
#include <QTextDocument>
#include <QPrintDialog>
#include <QPrinter>
#include <QPdfWriter>
#include <QDialog>
#include <QTextBrowser>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QListWidget>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QMessageBox>
#include <FloatingDockContainer.h>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <mutex>

using namespace chess;

namespace {
constexpr int LayoutVersion = 3;  // format of the ADS states: incrementing it erases all the layouts (3: Board panel)
constexpr int MinPanelWidth = 200, MinPanelHeight = 120; // floor of every panel (see makeDock)
constexpr int ClockStripHeight = 60, GraphStripHeight = 80; // Clock and Evaluation: strips of fixed height (lockStripHeights)
constexpr int LayoutRevision = 9; // revision of the default layouts, migrated page by page
const char* pageKeys[] = {"database", "analysis", "board", "tournament"};
const char* AppName = "Hydra64";

// Colours of the engines' arrows (green is reserved for hand-drawn arrows).
const QList<QColor>& arrowPalette()
{
    static const QList<QColor> colors = {theme::Accent,         QColor(0x5b, 0x9b, 0xd5), QColor(0xb0, 0x7f, 0xe0),
                                         QColor(0x4f, 0xc3, 0xc8), QColor(0xe0, 0x6c, 0x9f), QColor(0xe0, 0xc0, 0x50),
                                         QColor(0xe0, 0x5a, 0x50), QColor(0xc0, 0xc0, 0xc8)};
    return colors;
}

// Full copy of a game (variations, comments, clock times).
Game copyGame(const Game& g)
{
    Game copy;
    pgn::parseGame(pgn::writeGame(g), copy);
    return copy;
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_session(new GameSession(this))
{
    setWindowTitle(QString::fromLatin1(AppName));
    setWindowIcon(theme::logoIcon());

    // Qt Advanced Docking System: global configuration, before the manager
    using DM = ads::CDockManager;
    DM::setConfigFlags(DM::DefaultOpaqueConfig);
    DM::setConfigFlag(DM::DockAreaHasUndockButton, false);
    DM::setConfigFlag(DM::ActiveTabHasCloseButton, false); // a single cross: that of the area
    DM::setConfigFlag(DM::DockAreaDynamicTabsMenuButtonVisibility, true);
    DM::setConfigFlag(DM::FloatingContainerHasWidgetTitle, true);
    DM::setConfigFlag(DM::FocusHighlighting, false);
    DM::setConfigFlag(DM::DockAreaHideDisabledButtons, true); // no greyed cross (Opponent tab)
    // Under X11, a floating window moved by the window manager's title bar
    // (openbox…) does not show the docking markers: impossible to re-dock it. The title
    // bar drawn by ADS handles the drag itself (Wayland imposes the native bar).
    if (QGuiApplication::platformName() == QLatin1String("xcb"))
        DM::setConfigFlag(DM::FloatingContainerForceQWidgetTitleBar, true);
    DM::setAutoHideConfigFlags(DM::DefaultAutoHideConfig);

    m_dockManager = new DM(this); // becomes the window's central widget
    m_dockManager->setStyleSheet(m_dockManager->styleSheet() + theme::dockStyleSheet());

    // Central viewer: can be neither closed nor detached
    m_viewer = new BoardViewer(m_session);
    // Created after the viewer: the order of the connections to currentChanged matters
    // (the viewer hides the banner, then EngineGame may show it).
    m_play = new EngineGame(m_session, this);
    // Coach (game option): a weak move by the player is reported before the engine's reply.
    m_coach = new Coach(this);
    m_autoPlayer = new AutoPlayer(m_session, this);
    connect(m_autoPlayer, &AutoPlayer::finished, this, [this](const QString& reason) {
        statusBar()->showMessage(tr("Partie finie par le moteur (%n demi-coup(s)) : %1", nullptr, m_autoPlayer->played()).arg(reason), 8000);
    });
    m_play->setCoachHook([this](const Position& before, Move move, std::function<void(bool)> done) {
        const double threshold = m_play->settings().coachThreshold;
        statusBar()->showMessage(tr("Le coach regarde votre coup…"));
        m_coach->check(before, move, threshold, [this, before, move, done](const Coach::Verdict& v) {
            statusBar()->clearMessage();
            if (!v.weak) {
                done(true);
                return;
            }
            const QString kind = v.drop >= 0.3 ? tr("une gaffe (?\?)") : v.drop >= 0.2 ? tr("une erreur (?)") : tr("une imprécision (?!)");
            QMessageBox box(QMessageBox::Question, tr("Coach"),
                            tr("%1 est %2 : l'évaluation passe de %3 à %4.").arg(display::san(before.toSan(move)), kind, v.before, v.after),
                            QMessageBox::NoButton, this);
            if (!v.best.isNull()) box.setInformativeText(tr("Meilleur coup : %1. Reprendre votre coup ?").arg(display::san(before.toSan(v.best))));
            QPushButton* takeBack = box.addButton(tr("Reprendre"), QMessageBox::AcceptRole);
            box.addButton(tr("Jouer quand même"), QMessageBox::RejectRole);
            box.setDefaultButton(takeBack);
            box.exec();
            done(box.clickedButton() != takeBack);
        });
    });
    m_autoEval = new AutoEvaluator(m_session, this);
    m_assisted = new AssistedAnalysis(m_session, m_viewer->board(), this); // switched on by applyDisplayPrefs
    m_tournament = new Tournament(this);
    auto* viewerDock = new ads::CDockWidget(m_dockManager, tr("Échiquier"));
    viewerDock->setObjectName(QStringLiteral("viewer"));
    // In the centre: the board, or (Tournament page, on request) the overview of the games in progress
    m_overview = new TournamentOverview(m_tournament);
    m_centerStack = new QStackedWidget;
    m_centerStack->addWidget(m_viewer);
    m_centerStack->addWidget(m_overview);
    // Databases page: the game list of the chosen database takes the centre (see arrangeCenter)
    m_gamesCenter = new QWidget;
    {
        auto* l = new QVBoxLayout(m_gamesCenter);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
    }
    m_centerStack->addWidget(m_gamesCenter);
    viewerDock->setWidget(m_centerStack, ads::CDockWidget::ForceNoScrollArea);
    // The centre (board, or game list on the Databases page) cannot be squashed either.
    // In height: what the whole board needs (player strips and transport bar included).
    m_centerStack->setMinimumSize(360, m_viewer->minimumSizeHint().height());
    viewerDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
    connect(m_overview, &TournamentOverview::gameSelected, this, [this](int index) {
        // The game becomes that of the notation (and of the clocks), without leaving the overview.
        if (index == m_tournament->currentIndex() || m_tournament->showGame(index)) showLiveTournamentGame();
    });
    connect(m_overview, &TournamentOverview::gameChosen, this, [this](int index) {
        setTournamentOverview(false); // the chosen game goes full size
        viewTournamentGame(index);
    });
    viewerDock->setFeature(ads::CDockWidget::NoTab, true);
    m_dockManager->setCentralWidget(viewerDock)->setAllowedAreas(ads::DockWidgetArea::OuterDockAreas);

    // Panels
    m_notationPanel = new NotationPanel(m_session);
    m_engine = new EnginePanel;
    m_engine->setTabKey(QLatin1String(tabengine::AnalysisTab));
    m_playEngine = new PlayEnginePanel(m_play);
    m_graph = new EvalGraph(m_session);
    m_games = new GameListPanel;
    m_tournamentPanel = new TournamentPanel(m_tournament);
    m_notationPanel->setMinimumWidth(260);
    m_engine->setMinimumWidth(300);
    m_playEngine->setMinimumWidth(300);
    m_games->setMinimumWidth(360);
    m_tournamentPanel->setMinimumWidth(360);

    m_notationDock = makeDock(tr("Notation"), "notation", "comment", m_notationPanel);
    // Engine tabs: their role (Spectator, Opponent), the engine's name as a tooltip.
    m_engineDock = makeDock(tr("Spectateur"), "engine", "engine", m_engine);
    // Its cross removes it like the other engines (it comes back with "+" or the View menu).
    m_engineDock->setFeature(ads::CDockWidget::CustomCloseHandling, true);
    connect(m_engineDock, &ads::CDockWidget::closeRequested, this, &MainWindow::removeFirstEngine);
    connect(m_engineDock->toggleViewAction(), &QAction::triggered, this, [this](bool on) {
        if (!on) removeFirstEngine();
        else if (m_engineRemoved) {
            m_engineRemoved = false;
            QSettings().setValue("firstEngineRemoved", false);
            renumberSpectators();
        }
    });
    m_playEngineDock = makeDock(tr("Adversaire"), "playEngine", "engine", m_playEngine);
    m_playEngineDock->setToolTip(tr("Adversaire : %1").arg(m_playEngine->engine().name));
    // The opponent cannot be closed (no cross, nor in the View menu): it stays
    // movable, detachable and pinnable.
    m_playEngineDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_graphDock = makeDock(tr("Évaluation"), "graph", "analysis", m_graph, GraphStripHeight); // a thin strip, of fixed height
    // The Evaluation panel controls the automatic evaluation: closed on the page, Stockfish
    // does not run in the background.
    connect(m_graphDock, &ads::CDockWidget::viewToggled, this, &MainWindow::updateAutoEval);
    connect(m_graphDock, &ads::CDockWidget::closed, this, &MainWindow::updateAutoEval);
    connect(m_graphDock, &ads::CDockWidget::visibilityChanged, this, &MainWindow::updateAutoEval); // tab shown / hidden
    // "Databases" panel: the list of open databases. Outside the Databases page, the game list
    // of the chosen database sits below it; on the Databases page it is in the centre.
    m_gamesSide = new QWidget;
    {
        auto* l = new QVBoxLayout(m_gamesSide);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        l->addWidget(m_games->basesWidget(), 1);
    }
    m_gamesDock = makeDock(tr("Bases de données"), "games", "database", m_gamesSide);
    // "Board" panel: the board of the Databases page, whose centre is taken by the game
    // list. Closed on the other pages, where the board is in the centre.
    m_boardHolder = new QWidget;
    {
        auto* l = new QVBoxLayout(m_boardHolder);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
    }
    m_boardHolder->setMinimumWidth(360); // its minimum height depends on the page: see setPage
    m_boardDock = makeDock(tr("Échiquier"), "board", "board", m_boardHolder);
    m_tournamentDock = makeDock(tr("Tournoi"), "tournament", "tournament", m_tournamentPanel);
    m_clockPanel = new ClockPanel;
    m_clockDock = makeDock(tr("Pendule"), "clock", "clock", m_clockPanel, ClockStripHeight); // a thin strip, of fixed height
    m_book = new BookPanel(m_session);
    m_book->setMinimumWidth(260);
    m_bookDock = makeDock(tr("Arbre d'ouverture"), "book", "book", m_book);
    // Deep analysis (in the manner of IDeA): projects, engines, panel of the Analysis page
    m_deep = new DeepAnalysis(this);
    m_deep->loadAll();
    m_deepPanel = new DeepPanel(m_deep, m_session);
    m_deepPanel->setMinimumWidth(320);
    m_deepDock = makeDock(tr("Analyse approfondie"), "deep", "analysis", m_deepPanel);
    // Endgame tablebases (local Syzygy or online service)
    m_tablebase = new TablebasePanel;
    m_tablebase->setMinimumWidth(260);
    m_tablebaseDock = makeDock(tr("Finales"), "tablebase", "board", m_tablebase);
    // ADS only restores the panels already added to the manager:
    // an initial layout is installed before any restoreState().
    applyDefaultLayout(PageAnalysis);

    // Page bar under the whole window (above the status bar)
    statusBar()->setSizeGripEnabled(false);
    m_statusEngine = new QLabel;
    statusBar()->addPermanentWidget(m_statusEngine);
    auto* pageBarHost = new QWidget;
    auto* hostLayout = new QVBoxLayout(pageBarHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->addWidget(createPageBar());

    createActions();

    // The page bar is placed as a non-movable toolbar at the bottom
    auto* pageToolbar = addToolBar(tr("Pages"));
    pageToolbar->setObjectName("pagesToolbar");
    pageToolbar->setMovable(false);
    pageToolbar->setFloatable(false);
    pageToolbar->toggleViewAction()->setVisible(false);
    pageToolbar->setStyleSheet(QStringLiteral("QToolBar { border: none; padding: 0; spacing: 0; background: transparent; }"));
    pageToolbar->addWidget(pageBarHost);
    addToolBar(Qt::BottomToolBarArea, pageToolbar);

    // Connections
    connect(m_session, &GameSession::currentChanged, this, &MainWindow::onCurrentChanged);
    connect(m_session, &GameSession::headersChanged, this, &MainWindow::updateTitle);
    connect(m_viewer->board(), &BoardWidget::moveRequested, this, &MainWindow::onBoardMove);
    connect(m_viewer, &BoardViewer::flipRequested, this, [this] { m_viewer->setFlipped(!m_viewer->isFlipped()); });
    connect(m_engine, &EnginePanel::engineChanged, this, [this](const EngineConfig& e) {
        if (e.isValid()) m_engineDock->setToolTip(tr("Spectateur : %1").arg(e.name));
    });
    if (m_engine->engineConfig().isValid()) m_engineDock->setToolTip(tr("Spectateur : %1").arg(m_engine->engineConfig().name));
    connect(m_engine, &EnginePanel::sidesChanged, this, [](EnginePanel::Sides sd) { QSettings().setValue("analysisSides", int(sd)); });
    connect(m_engine, &EnginePanel::engineSettingsRequested, this, [this](const QString& id) { engineTabSettings(m_engine->tabKey(), m_engineDock->windowTitle(), id); });
    // Evaluation bar and graph: Stockfish in the background (AutoEvaluator), never the
    // spectator engines nor the opponent of the Play page.
    connect(m_session, &GameSession::evaluationsChanged, this, [this] {
        if (m_session->current()->eval.valid) m_viewer->evalBar()->setEvaluation(m_session->current()->eval);
    });
    m_engine->setArrowColor(arrowPalette().first());
    connect(m_engine, &EnginePanel::bestMoveChanged, this, [this](Move m) {
        m_viewer->board()->setEngineArrow(QStringLiteral("engine"), m, m_engine->arrowColor());
    });
    connect(m_engine, &EnginePanel::movePicked, m_session, &GameSession::playMove);
    connect(m_engine, &EnginePanel::linesInsertRequested, this, &MainWindow::insertEngineLines);
    connect(m_engine, &EnginePanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(m_engine, &EnginePanel::analysingChanged, this, &MainWindow::updateEngineStatus);
    // Engine state on its tab's icon: green, it is ready; red, it is working
    connect(m_engine, &EnginePanel::analysingChanged, this, [this](bool on) { m_engineDock->setIcon(module::statusIcon(on)); });
    m_engineDock->setIcon(module::statusIcon(m_engine->isAnalysing()));
    connect(m_playEngine, &PlayEnginePanel::runningChanged, this, [this](bool on) { m_playEngineDock->setIcon(module::statusIcon(on)); });
    m_playEngineDock->setIcon(module::statusIcon(false));
    connect(m_engine->engine(), &UciEngine::started, this, &MainWindow::updateEngineStatus);
    connect(m_viewer->board(), &BoardWidget::wheelSteps, this, [this](int steps) {
        if (!prefs::wheelNavigation()) return;
        if (prefs::wheelInverted()) steps = -steps;
        for (int i = 0; i < std::abs(steps); ++i) steps > 0 ? m_session->forward() : m_session->back();
    });
    connect(m_viewer, &BoardViewer::sendToAnalysisRequested, this, [this] { sendToAnalysis(); });
    connect(m_notationPanel->view(), &NotationView::sendToAnalysisRequested, this, [this] { sendToAnalysis(); });
    connect(m_viewer, &BoardViewer::editRequested, this, &MainWindow::editPosition);
    // Clock panel: same times as the board's strips; title = time control.
    connect(m_viewer, &BoardViewer::clocksChanged, m_clockPanel, &ClockPanel::setClocks);
    connect(m_session, &GameSession::headersChanged, this, &MainWindow::updateClockTitle);
    connect(m_play, &EngineGame::stateChanged, this, &MainWindow::updateClockTitle);
    connect(m_clockDock->toggleViewAction(), &QAction::triggered, this, [this](bool on) {
        if (on) placeClockDock();
    });
    connect(m_play, &EngineGame::stateChanged, this, &MainWindow::updateBoardInteraction);
    connect(m_play, &EngineGame::clockTick, this, &MainWindow::updateClocks);
    connect(m_play, &EngineGame::stateChanged, this, &MainWindow::updateClocks);
    connect(m_play, &EngineGame::finished, this, [this](const QString& result, const QString& reason) {
        const bool humanWhite = m_play->humanColor() == White;
        QString title;
        if (result == QLatin1String("1/2-1/2")) title = tr("Partie nulle");
        else if (result == QLatin1String("*")) title = tr("Partie interrompue");
        else title = (result == QLatin1String("1-0")) == humanWhite ? tr("Vous avez gagné !") : tr("Le moteur gagne");
        const QString text = result == QLatin1String("*") ? reason : QStringLiteral("%1  ·  %2").arg(reason, QString(result).replace(QStringLiteral("1/2"), QStringLiteral("½")));
        m_viewer->showBanner(title, text, true);
        statusBar()->showMessage(QStringLiteral("%1 — %2").arg(title, text), 8000);
    });

    // Tab of the opponent engine (Play page)
    connect(m_playEngine, &PlayEnginePanel::titleChanged, this, [this](const QString& name) { m_playEngineDock->setToolTip(tr("Adversaire : %1").arg(name)); });
    connect(m_playEngine, &PlayEnginePanel::startRequested, this, &MainWindow::startEngineForSideToMove);
    connect(m_playEngine, &PlayEnginePanel::chooseEngineRequested, this, qOverload<>(&MainWindow::startEngineGame));
    connect(m_playEngine, &PlayEnginePanel::engineSelected, this, &MainWindow::changeOpponent);
    connect(m_playEngine, &PlayEnginePanel::strengthChanged, this, &MainWindow::updatePlayHint);
    connect(m_playEngine, &PlayEnginePanel::spectatorRequested, this, &MainWindow::addSpectatorFromUi);
    connect(m_playEngine, &PlayEnginePanel::engineSettingsRequested, this, [this](const QString& id) { engineTabSettings(QLatin1String(tabengine::PlayTab), tr("Adversaire"), id); });

    // Game database
    connect(m_games, &GameListPanel::gameActivated, this, &MainWindow::loadGameFromDatabase);
    connect(m_games, &GameListPanel::sendToAnalysisRequested, this, &MainWindow::sendDatabaseGameToAnalysis);
    // Games deleted from the database: the origin of the displayed games shifts.
    connect(m_games, &GameListPanel::gamesRemoved, this, [this](const QList<int>& rows) {
        GameOrigin o = m_session->origin();
        if (!o.isValid() || QFileInfo(o.path) != QFileInfo(m_games->model()->filePath())) return;
        if (rows.contains(o.row)) o = {};
        else o.row -= int(std::count_if(rows.begin(), rows.end(), [&](int r) { return r < o.row; }));
        m_session->setOrigin(o);
    });
    connect(m_notationPanel->view(), &NotationView::infoRequested, this, [this] { editGameInfo(); });
    // Notation menu: export as PGN, or add to an open Hydra64 database (the active one first)
    m_notationPanel->view()->setDatabaseList([this] {
        QList<QPair<QString, QString>> bases;
        const QString active = m_games->model()->filePath();
        for (const QString& path : m_games->openPaths()) {
            const GameListModel* m = m_games->modelFor(path);
            if (!m || !m->isIndexed() || m->isReadOnly()) continue;
            const QPair<QString, QString> entry{QFileInfo(path).completeBaseName(), path};
            if (QFileInfo(path) == QFileInfo(active)) bases.prepend(entry);
            else bases.append(entry);
        }
        return bases;
    });
    // Arrows and marked squares drawn on the board: they belong to the displayed move.
    connect(m_session, &GameSession::graphicsChanged, this, [this] { m_viewer->board()->setGraphics(m_session->current()->graphics); });
    connect(m_viewer->board(), &BoardWidget::graphicsChanged, this, [this](const std::vector<chess::Graphic>& g) { m_session->setGraphics(m_session->current(), g); });
    connect(m_notationPanel->view(), &NotationView::exportPgnRequested, this, &MainWindow::exportGame);
    connect(m_notationPanel->view(), &NotationView::exportHtmlRequested, this, [this] { exportGameHtml(m_session->game()); });
    connect(m_games, &GameListPanel::clipboardRequested, this, [this](const QList<int>& rows) { copyToClipboardBase(rows); });
    connect(m_games, &GameListPanel::mergeRequested, this, [this](const QList<int>& rows, bool intoAnalysis) { mergeGames(rows, intoAnalysis); });
    connect(m_games, &GameListPanel::exportHtmlRequested, this, [this](int row) {
        GameListModel* model = m_games->model();
        Game g;
        QString error;
        if (row < 0 || row >= model->rowCount() || !model->parseGame(row, g, &error)) {
            statusBar()->showMessage(tr("Partie illisible : %1").arg(error), 6000);
            return;
        }
        exportGameHtml(g);
    });
    connect(m_notationPanel->view(), &NotationView::addToDatabaseRequested, this, &MainWindow::addGameToDatabase);
    connect(m_games, &GameListPanel::editInfoRequested, this, &MainWindow::editDatabaseGameInfo);
    connect(m_games, &GameListPanel::playerReportRequested, this, [this](const QString& name) { playerReport(name); });
    connect(m_games, &GameListPanel::eventTableRequested, this, [this](int row) { eventTable(row); });
    connect(m_games, &GameListPanel::previewRequested, this, [this](int row) { previewGame(row); });
    connect(m_games, &GameListPanel::annotateRequested, this, [this](const QList<int>& rows) {
        AnnotateDialog dlg(this);
        dlg.setWindowTitle(tr("Annoter les parties"));
        if (dlg.exec() != QDialog::Accepted) return;
        QString summary;
        if (annotateGames(rows, dlg.engine(), dlg.options(), &summary) >= 0) statusBar()->showMessage(summary, 10000);
    });
    // Open databases: list kept for the next start. (The panel's title stays
    // "Databases": each database's name is in its tab.)
    connect(m_games, &GameListPanel::openRequested, this, &MainWindow::openDatabaseDialog);
    connect(m_games, &GameListPanel::compactRequested, this, &MainWindow::compactDatabase);
    connect(m_games, &GameListPanel::databasesChanged, this, [this] {
        const QString path = m_games->model()->filePath();
        updateDatabaseTree();
        if (m_restoringDatabases) return;
        QSettings s;
        s.setValue("databases", m_games->openPaths());
        s.setValue("lastDatabase", path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath());
    });
    connect(m_viewer, &BoardViewer::saveRequested, this, [this] { saveToDatabase(); });
    connect(m_viewer, &BoardViewer::analyseRequested, this, [this] { sendToAnalysis(); });
    connect(m_viewer, &BoardViewer::rematchRequested, this, [this] { rematch(); });
    connect(m_viewer, &BoardViewer::newGameRequested, this, qOverload<>(&MainWindow::startEngineGame));
    // Opening book: double-click on a move, like a move played on the board
    // (Play page: only if the side to move is playable there).
    // The opponent engine (Play page) can play the moves of the open book.
    m_play->setBookProvider([this](const Position& pos) { return m_book->pickMove(pos); });
    connect(m_play, &EngineGame::bookMovePlayed, this, [this](Move m) {
        statusBar()->showMessage(tr("Coup de l'arbre d'ouverture : %1").arg(display::san(m_session->current()->pos.toSan(m))), 4000);
    });
    connect(m_book, &BookPanel::movePicked, this, [this](Move m) {
        if (m_page != PageBoard || m_viewer->board()->isMovable(m_session->current()->pos.sideToMove())) onBoardMove(m);
    });
    connect(m_book, &BookPanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(m_deepPanel, &DeepPanel::movePicked, this, [this](Move m) { m_session->playMove(m); });
    // Move chosen in the tablebases: it goes through the board (game against the engine on your turn only,
    // tournament game in progress not editable)
    connect(m_tablebase, &TablebasePanel::movePicked, this, [this](Move m) {
        if (m_page == PageBoard && !m_viewer->board()->isMovable(m_session->current()->pos.sideToMove())) return;
        onBoardMove(m);
    });
    connect(m_tablebase, &TablebasePanel::settingsRequested, this, [this] {
        PreferencesDialog::openOnTablebases();
        showPreferences();
    });
    connect(m_tablebaseDock, &ads::CDockWidget::viewToggled, this, [this](bool on) {
        if (!on) return;
        // Absent from the page's saved layout (Play, Tournament): as a tab next to the notation, not as a floating window
        if ((!m_tablebaseDock->dockAreaWidget() || m_tablebaseDock->isFloating()) && !m_notationDock->isClosed() && m_notationDock->dockAreaWidget())
            m_dockManager->addDockWidgetTabToArea(m_tablebaseDock, m_notationDock->dockAreaWidget());
        m_tablebase->setPosition(m_session->current()->pos);
    });
    connect(m_deepPanel, &DeepPanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(m_deepPanel, &DeepPanel::gameRequested, this, [this](const Game& g) {
        Game copy;
        pgn::parseGame(pgn::writeGame(g), copy);
        copy.toEnd();
        analyseGame(std::move(copy));
    });
    connect(m_deepPanel, &DeepPanel::bestMoveChanged, this, [this](Move m) {
        m_deepBest = m;
        updateDeepArrow();
    });
    connect(m_deepDock, &ads::CDockWidget::viewToggled, this, &MainWindow::updateDeepArrow);
    // Lines of the first engine of the Analysis page: sent to the deep analysis if the project asks for it
    connect(m_engine->engine(), &UciEngine::infoReceived, this, [this](const EngineLine& l) {
        if (m_page == PageAnalysis && l.multipv == 1) m_deep->spectatorLine(m_session->game(), m_session->current(), l);
    });
    connect(m_book, &BookPanel::convertRequested, this, [this](const QString& path) { convertDialog(path); });
    connect(m_games, &GameListPanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });

    // Engine tournament: the game in progress is displayed (Tournament page) if it is being followed.
    connect(m_tournament, &Tournament::gameStarted, this, [this] {
        if (m_followLive) showLiveTournamentGame();
    });
    connect(m_tournament, &Tournament::moveMade, this, [this](Move m, int clockMs) {
        if (m_followLive) m_session->appendMoveIn(GameSession::TournamentSlot, m, clockMs);
    });
    connect(m_tournament, &Tournament::gameFinished, this, [this](int index) {
        const Tournament::GameRecord& g = m_tournament->games()[index];
        if (m_followLive && m_tournament->finishedGameWasShown()) { // simultaneous games: only the displayed game
            m_session->setHeaderIn(GameSession::TournamentSlot, "Result", g.result.toStdString());
            m_session->setHeaderIn(GameSession::TournamentSlot, "Termination", g.reason.toStdString());
        }
        statusBar()->showMessage(tr("Partie %1 : %2 – %3  %4 (%5)").arg(index + 1).arg(g.whiteName, g.blackName, g.result, g.reason), 6000);
    });
    connect(m_tournament, &Tournament::changed, this, [this] {
        // Start (or resumption): the game in progress is followed, and the centre switches to the overview
        // (one board per game in progress; a double-click on one of them brings it back full size).
        if (m_tournament->isRunning() && !m_tournamentWasRunning) {
            m_followLive = true;
            setTournamentOverview(true);
        }
        m_tournamentWasRunning = m_tournament->isRunning();
        updateBoardInteraction();
        updateClocks();
    });
    connect(m_tournament, &Tournament::clockTick, this, &MainWindow::updateClocks);
    connect(m_tournament, &Tournament::message, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(m_tournamentPanel, &TournamentPanel::gameViewRequested, this, &MainWindow::viewTournamentGame);
    connect(m_tournamentPanel, &TournamentPanel::overviewRequested, this, &MainWindow::setTournamentOverview);
    connect(m_tournamentPanel, &TournamentPanel::databaseRequested, this, [this](const QString& path) {
        if (openDatabase(path)) setPage(PageDatabase);
    });
    // Tournament games go to a Hydra64 database. When it is displayed on the Databases page, the
    // game is added through its tab (one writer per file); the tab is then already up to date.
    m_tournament->setGameSaver([this](const QString& path, const Game& game, QString* error) {
        if (GameListModel* open = m_games->modelFor(path)) return open->appendGame(pgn::writeGame(game), error) >= 0;
        return Tournament::appendToDatabase(path, game, error);
    });
    connect(m_tournament, &Tournament::pgnSaved, this, [this](const QString& path) {
        GameListModel* open = m_games->modelFor(path);
        if (open && !open->isIndexed()) m_games->reload(path); // PGN of an earlier version: reread
        else if (open && m_games->model() == open) m_games->refreshCount();
    });
    connect(m_tournamentPanel, &TournamentPanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });

    connect(&prefs::Notifier::instance(), &prefs::Notifier::changed, this, &MainWindow::applyDisplayPrefs);

    restoreSettings();
    applyDisplayPrefs();
    onCurrentChanged(false);
    updateTitle();
}

ads::CDockWidget* MainWindow::makeDock(const QString& title, const QString& name, const QString& icon, QWidget* content, int minHeight)
{
    auto* dock = new ads::CDockWidget(m_dockManager, title.toUpper());
    dock->setObjectName(name);
    dock->setIcon(theme::icon(icon));
    content->setAttribute(Qt::WA_StyledBackground);
    // Minimum size in both directions: dragging a splitter must never shrink a panel
    // to nothing (ADS takes the content's minimum size, zero if it is not set).
    content->setMinimumSize(std::max(content->minimumWidth(), MinPanelWidth), minHeight > 0 ? minHeight : std::max(content->minimumHeight(), MinPanelHeight));
    dock->setWidget(content, ads::CDockWidget::ForceNoScrollArea);
    dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
    // Any change of the panels' arrangement: the fixed-height strips are locked again
    for (auto signal : {&ads::CDockWidget::viewToggled, &ads::CDockWidget::topLevelChanged, &ads::CDockWidget::visibilityChanged})
        connect(dock, signal, this, &MainWindow::scheduleStripLock);
    return dock;
}

void MainWindow::scheduleStripLock()
{
    if (m_stripLockPending) return;
    m_stripLockPending = true;
    // Tied to the dock manager: it is destroyed before the window when the window closes
    QTimer::singleShot(0, m_dockManager, [this] {
        m_stripLockPending = false;
        lockStripHeights();
    });
}

void MainWindow::lockStripHeights()
{
    // Clock and Evaluation need no more than their strip: an area that only shows them cannot be made
    // taller (nor lower). Tabbed with another panel, the area is free again.
    for (ads::CDockContainerWidget* container : m_dockManager->dockContainers())
        for (ads::CDockAreaWidget* area : container->openedDockAreas()) {
            int strip = 0;
            bool only = true;
            for (ads::CDockWidget* d : area->openedDockWidgets()) {
                if (d == m_clockDock) strip = std::max(strip, ClockStripHeight);
                else if (d == m_graphDock) strip = std::max(strip, GraphStripHeight);
                else only = false;
            }
            const int bar = area->titleBar() && !area->titleBar()->isHidden() ? area->titleBar()->sizeHint().height() : 0;
            const int wanted = only && strip > 0 ? strip + bar : QWIDGETSIZE_MAX;
            if (area->maximumHeight() != wanted) area->setMaximumHeight(wanted);
        }
}

QList<ads::CDockWidget*> MainWindow::panels() const
{
    QList<ads::CDockWidget*> list{m_notationDock, m_engineDock, m_playEngineDock, m_graphDock, m_gamesDock, m_tournamentDock, m_clockDock, m_bookDock, m_deepDock, m_tablebaseDock, m_boardDock};
    for (const Spectator& s : m_spectators) list << s.dock;
    return list;
}

// ---------------------------------------------------------------- Spectator engines

void MainWindow::updateEngineStatus()
{
    QStringList names;
    if (m_engine->isAnalysing()) names << m_engine->engineConfig().name;
    for (const Spectator& s : std::as_const(m_spectators))
        if (s.panel->isAnalysing()) names << s.panel->engineConfig().name;
    m_statusEngine->setText(names.isEmpty() ? QString() : tr("● Analyse : %1").arg(names.join(QStringLiteral(", "))));
}

QColor MainWindow::nextArrowColor() const
{
    // First free colour; beyond that, the palette starts over.
    QList<QColor> used{m_engine->arrowColor()};
    for (const Spectator& s : m_spectators) used << s.panel->arrowColor();
    for (const QColor& c : arrowPalette())
        if (!used.contains(c)) return c;
    return arrowPalette()[(m_spectators.size() + 1) % arrowPalette().size()];
}

EngineConfig MainWindow::unusedEngine() const
{
    // An engine not already analysing on the page, otherwise the fallback engine.
    const EngineRegistry& reg = EngineRegistry::instance();
    QStringList used;
    if (!m_engineRemoved) used << m_engine->engineConfig().id;
    for (const Spectator& s : m_spectators) used << s.panel->engineConfig().id;
    for (const EngineConfig& e : reg.engines())
        if (!used.contains(e.id)) return e;
    return reg.fallbackEngine();
}

void MainWindow::removeFirstEngine()
{
    m_engineRemoved = true;
    QSettings().setValue("firstEngineRemoved", true);
    m_engine->setAnalysing(false); // also clears its arrow
    m_engineDock->toggleView(false);
    renumberSpectators();
    updateEngineStatus();
}

EnginePanel* MainWindow::addSpectator(int page, const QString& engineId, const QString& objectName, int multiPv, const QColor& color, int sides)
{
    EngineConfig cfg = EngineRegistry::instance().engine(engineId);
    if (!cfg.isValid()) cfg = unusedEngine();
    QString name = objectName;
    if (name.isEmpty()) name = QStringLiteral("spectator%1").arg(++m_spectatorSerial);
    else m_spectatorSerial = std::max(m_spectatorSerial, name.mid(9).toInt());

    auto* panel = new EnginePanel;
    panel->setTabKey(name); // engine settings specific to this spectator
    panel->setRemembersChoice(false);
    panel->setMinimumWidth(300);
    panel->setMultiPv(multiPv);
    panel->setSides(EnginePanel::Sides(std::clamp(sides, 0, 2)));
    panel->setArrowColor(color.isValid() ? color : nextArrowColor());
    panel->setEngine(cfg);
    ads::CDockWidget* dock = makeDock(tr("Spectateur"), name, "engine", panel);
    dock->setFeature(ads::CDockWidget::CustomCloseHandling, true); // the cross removes the spectator
    dock->setToolTip(cfg.isValid() ? tr("Spectateur : %1").arg(cfg.name) : tr("Moteur spectateur"));
    m_spectators << Spectator{panel, dock, page};

    connect(dock, &ads::CDockWidget::closeRequested, this, [this, dock] { removeSpectator(dock); });
    connect(panel, &EnginePanel::engineChanged, this, [this, dock](const EngineConfig& e) {
        if (e.isValid()) dock->setToolTip(tr("Spectateur : %1").arg(e.name));
        saveSpectators();
    });
    connect(panel, &EnginePanel::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
    connect(panel, &EnginePanel::sidesChanged, this, [this] { saveSpectators(); });
    // Double-click on a line: on the Play page, the move goes through the board, only if
    // the side to move is playable there (during a game: on your turn, not the engine's).
    connect(panel, &EnginePanel::movePicked, this, [this, page](Move m) {
        if (page != PageBoard) {
            m_session->playMove(m);
            return;
        }
        if (m_viewer->board()->isMovable(m_session->current()->pos.sideToMove())) onBoardMove(m);
    });
    connect(panel, &EnginePanel::bestMoveChanged, this, [this, panel, name](Move m) {
        m_viewer->board()->setEngineArrow(name, m, panel->arrowColor());
    });
    connect(panel, &EnginePanel::analysingChanged, this, &MainWindow::updateEngineStatus);
    connect(panel, &EnginePanel::analysingChanged, dock, [dock](bool on) { dock->setIcon(module::statusIcon(on)); });
    dock->setIcon(module::statusIcon(panel->isAnalysing()));
    connect(panel, &EnginePanel::linesInsertRequested, this, &MainWindow::insertEngineLines);
    connect(panel->engine(), &UciEngine::started, this, &MainWindow::updateEngineStatus);
    connect(panel->engine(), &UciEngine::infoReceived, this, [this, page](const EngineLine& l) {
        if (page == PageAnalysis && m_page == PageAnalysis && l.multipv == 1) m_deep->spectatorLine(m_session->game(), m_session->current(), l);
    });
    connect(panel, &EnginePanel::engineSettingsRequested, this, [this, panel, dock](const QString& id) { engineTabSettings(panel->tabKey(), dock->windowTitle(), id); });
    placeSpectator(dock);
    renumberSpectators();
    panel->setPosition(m_session->current()->pos);
    return panel;
}

void MainWindow::renumberSpectators()
{
    // Analysis page: the first engine panel (if not removed), then the added
    // spectators, in order; Play and Databases pages: their spectators, in order.
    int analysis = 0, play = 0, database = 0;
    if (!m_engineRemoved) m_engineDock->setWindowTitle(tr("Spectateur %1").arg(++analysis).toUpper());
    for (const Spectator& s : std::as_const(m_spectators))
        s.dock->setWindowTitle(tr("Spectateur %1").arg(s.page == PageBoard ? ++play : s.page == PageDatabase ? ++database : ++analysis).toUpper());
}

void MainWindow::placeSpectator(ads::CDockWidget* dock)
{
    // Under the last visible engine of its page: first panel or added spectator (Analysis),
    // opponent engine or added spectator (Play), notation or added spectator (Databases).
    int page = PageAnalysis;
    for (const Spectator& s : std::as_const(m_spectators))
        if (s.dock == dock) page = s.page;
    ads::CDockAreaWidget* area = nullptr;
    for (const Spectator& s : std::as_const(m_spectators))
        if (s.dock != dock && s.page == page && !s.dock->isClosed() && s.dock->dockAreaWidget()) area = s.dock->dockAreaWidget();
    if (!area && page == PageBoard && !m_playEngineDock->isClosed()) area = m_playEngineDock->dockAreaWidget();
    if (!area && page == PageAnalysis && dock != m_engineDock && !m_engineDock->isClosed()) area = m_engineDock->dockAreaWidget();
    if (!area && !m_notationDock->isClosed()) area = m_notationDock->dockAreaWidget();
    if (area) m_dockManager->addDockWidget(ads::BottomDockWidgetArea, dock, area);
    else m_dockManager->addDockWidget(ads::RightDockWidgetArea, dock);
}

void MainWindow::addSpectatorFromUi(const QString& engineId)
{
    if (EngineRegistry::instance().engines().isEmpty()) {
        statusBar()->showMessage(tr("Aucun moteur configuré (Moteur → Gestion des moteurs…)"), 6000);
        return;
    }
    EnginePanel* panel;
    if (m_page == PageBoard || m_page == PageDatabase) {
        panel = addSpectator(m_page, engineId); // spectator of the Play or Databases page, stopped
        saveSpectators();
        statusBar()->showMessage(tr("Moteur spectateur ajouté : %1").arg(panel->engineConfig().name), 5000);
        return;
    }
    if (m_page != PageAnalysis) setPage(PageAnalysis); // otherwise, spectators go to the Analysis page
    if (m_engineRemoved) {
        // The first panel had been removed: it is the one that comes back (stopped).
        m_engineRemoved = false;
        QSettings().setValue("firstEngineRemoved", false);
        const EngineConfig chosen = EngineRegistry::instance().engine(engineId);
        const EngineConfig cfg = chosen.isValid() ? chosen : unusedEngine();
        m_engine->setEngine(cfg);
        QSettings().setValue("analysisEngine", cfg.id);
        QList<QColor> used;
        for (const Spectator& s : std::as_const(m_spectators)) used << s.panel->arrowColor();
        if (used.contains(m_engine->arrowColor()))
            for (const QColor& col : arrowPalette())
                if (!used.contains(col)) {
                    m_engine->setArrowColor(col);
                    break;
                }
        m_engineDock->toggleView(true);
        if (!m_engineDock->dockAreaWidget() || m_engineDock->isFloating()) placeSpectator(m_engineDock);
        panel = m_engine;
        renumberSpectators();
    } else {
        panel = addSpectator(PageAnalysis, engineId); // stopped: Play in its panel to start it
    }
    saveSpectators();
    statusBar()->showMessage(tr("Moteur spectateur ajouté : %1").arg(panel->engineConfig().name), 5000);
}

void MainWindow::removeSpectator(ads::CDockWidget* dock)
{
    for (int i = 0; i < m_spectators.size(); ++i) {
        if (m_spectators[i].dock != dock) continue;
        EnginePanel* panel = m_spectators[i].panel;
        m_spectators.removeAt(i);
        panel->setAnalysing(false);
        m_viewer->board()->setEngineArrow(dock->objectName(), {}, {});
        updateEngineStatus();
        panel->engine()->quit();
        tabengine::forget(panel->tabKey()); // its engine settings disappear with it
        m_dockManager->removeDockWidget(dock);
        dock->deleteLater();
        renumberSpectators();
        saveSpectators();
        return;
    }
}

void MainWindow::saveSpectators() const
{
    QVariantList list;
    for (const Spectator& s : m_spectators)
        list << QVariantMap{{"name", s.dock->objectName()},
                            {"engine", s.panel->engineConfig().id},
                            {"multiPv", s.panel->multiPv()},
                            {"color", s.panel->arrowColor().name()},
                            {"page", s.page == PageBoard ? "play" : s.page == PageDatabase ? "database" : "analysis"},
                            {"sides", int(s.panel->sides())}}; // never the analysis state: nothing starts at launch
    QSettings().setValue("spectators", list);
}

void MainWindow::restoreSpectators()
{
    for (const QVariant& v : QSettings().value("spectators").toList()) {
        const QVariantMap m = v.toMap();
        const QString id = m.value("engine").toString();
        if (!EngineRegistry::instance().engine(id).isValid()) continue; // engine removed since
        const QString where = m.value("page").toString();
        const int page = where == QLatin1String("play") ? PageBoard : where == QLatin1String("database") ? PageDatabase : PageAnalysis;
        addSpectator(page, id, m.value("name").toString(), m.value("multiPv", 4).toInt(), QColor(m.value("color").toString()),
                     m.value("sides", 0).toInt());
    }
}

QWidget* MainWindow::createPageBar()
{
    auto* bar = new QWidget;
    bar->setObjectName("PageBar");
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedHeight(54);
    auto* l = new QHBoxLayout(bar);
    l->setContentsMargins(12, 0, 16, 0);
    l->setSpacing(0);

    // Logo (hydra head): a click opens the application's settings.
    auto* left = new QWidget;
    left->setFixedWidth(230);
    auto* ll = new QHBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(8);
    auto* logo = new QToolButton;
    logo->setObjectName("BrandLogo");
    {
        // Rectangular banner: close-up of the hydra's heads, no text.
        const QSize banner(200, 44);
        QIcon ic;
        for (qreal dpr : {1.0, 2.0}) ic.addPixmap(theme::logoBanner(banner, dpr));
        logo->setIcon(ic);
        logo->setIconSize(banner);
    }
    logo->setCursor(Qt::PointingHandCursor);
    m_logo = logo;
    // Menu bar visible: the Preferences. Bar hidden: the page's menus.
    connect(logo, &QToolButton::clicked, this, [this] {
        if (!prefs::hideMenuBar()) {
            showPreferences();
            return;
        }
        // The menu opens upwards: the logo is at the very bottom of the window.
        const QSize size = m_compactMenu->sizeHint();
        m_compactMenu->exec(m_logo->mapToGlobal(QPoint(0, -size.height())));
    });
    ll->addWidget(logo);
    ll->addStretch(1);

    m_pageButtons = new QButtonGroup(this);
    m_pageButtons->setExclusive(true);
    const std::tuple<Page, QString, QString, QString> pages[] = {
        {PageDatabase, "database", tr("Bases"), QStringLiteral("Shift+1")},
        {PageAnalysis, "analysis", tr("Analyse"), QStringLiteral("Shift+2")},
        {PageBoard, "board", tr("Jouer"), QStringLiteral("Shift+3")},
        {PageTournament, "tournament", tr("Tournoi", "page"), QStringLiteral("Shift+4")},
    };
    l->addWidget(left);
    l->addStretch(1);
    for (const auto& [page, icon, label, key] : pages) {
        auto* b = new QToolButton;
        b->setCheckable(true);
        b->setIcon(theme::icon(icon));
        b->setIconSize(QSize(22, 22));
        b->setText(label);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setToolTip(QStringLiteral("%1 (%2)").arg(label, key));
        b->setShortcut(QKeySequence(key));
        m_pageButtons->addButton(b, page);
        l->addWidget(b);
    }
    l->addStretch(1);

    // On the right: same width as the logo block, to centre the pages.
    auto* right = new QWidget;
    right->setFixedWidth(230);
    l->addWidget(right);

    connect(m_pageButtons, &QButtonGroup::idClicked, this, [this](int id) { setPage(Page(id)); });
    return bar;
}

QAction* MainWindow::pageAction(QMenu* menu, int pages, QAction* action)
{
    if (menu && action->parent() != menu) menu->addAction(action);
    addAction(action); // shortcuts active even when the menu bar is hidden
    m_actionPages.insert(action, pages);
    return action;
}

void MainWindow::createActions()
{
    auto add = [this](QMenu* menu, int pages, const QString& icon, const QString& text, const QKeySequence& key, auto&& slot) {
        QAction* a = menu->addAction(icon.isEmpty() ? QIcon() : theme::icon(icon), text);
        if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        return pageAction(menu, pages, a);
    };
    const QKeySequence none;

    // Submenu shown on the pages `pages` (it follows its page like an action: updateMenus)
    auto submenu = [this](QMenu* parent, int pages, const char* icon, const QString& title) {
        QMenu* m = parent->addMenu(theme::icon(icon), title);
        m_actionPages.insert(m->menuAction(), pages);
        return m;
    };

    // ---- File
    auto* fileMenu = menuBar()->addMenu(tr("&Fichier"));
    add(fileMenu, OnAnalysis, "new", tr("Nouvelle partie"), QKeySequence::New, [this] { m_session->newGame(); });
    add(fileMenu, OnAnalysis, "", tr("Nouvelle partie Chess960…"), none, [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Nouvelle partie Chess960"), tr("Numéro de la position de départ (0 à 959 ; 518 = position classique) :"),
                                           int(QRandomGenerator::global()->bounded(960)), 0, 959, 1, &ok);
        if (!ok) return;
        m_session->newGame(Position::chess960Start(n));
        if (n != 518) m_session->setHeader("Variant", "Chess960");
    });
    add(fileMenu, OnDatabase | OnAnalysis, "open", tr("Ouvrir une base…"), QKeySequence::Open, [this] { openDatabaseDialog(); });
    add(fileMenu, OnDatabase, "new", tr("Nouvelle base…"), none, [this] { newDatabase(); });
    add(fileMenu, OnAll, "", tr("Convertir…"), none, [this] { convertDialog(); });
    add(fileMenu, OnDatabase, "close", tr("Fermer la base"), QKeySequence::Close, [this] { m_games->closeCurrent(); });
    add(fileMenu, OnAnalysis | OnBoard, "save", tr("Enregistrer dans la base"), QKeySequence::Save, [this] { saveToDatabase(); });
    add(fileMenu, OnAnalysis | OnBoard, "", tr("Enregistrer dans une autre base…"), none, [this] { saveToDatabase(true); });
    add(fileMenu, OnAll, "", tr("Exporter en PGN…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this] { exportGame(); });
    add(fileMenu, OnAll, "", tr("Exporter en PDF…"), none, [this] { exportGamePdf(m_session->game()); });
    add(fileMenu, OnAll, "", tr("Imprimer la partie…"), QKeySequence::Print, [this] { printGame(m_session->game()); });
    fileMenu->addSeparator();
    {
        // Bookmarks: rebuilt each time the menu opens
        auto* bm = new QMenu(tr("Signets"), fileMenu);
        bm->setIcon(theme::icon("book"));
        connect(bm, &QMenu::aboutToShow, this, [this, bm] {
            bm->clear();
            QAction* add = bm->addAction(theme::icon("new"), tr("Ajouter un signet"), this, [this] { addBookmark(); });
            add->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
            add->setShortcutContext(Qt::WidgetShortcut); // reminder in the menu; the shortcut is on the window
            const QList<Bookmark> list = bookmarks();
            if (!list.isEmpty()) bm->addSeparator();
            for (int i = 0; i < list.size(); ++i) bm->addAction(list[i].name, this, [this, i] { openBookmark(i); })->setToolTip(QDir::toNativeSeparators(list[i].path));
            if (list.isEmpty()) return;
            bm->addSeparator();
            QMenu* remove = bm->addMenu(theme::icon("trash"), tr("Supprimer un signet"));
            for (int i = 0; i < list.size(); ++i) remove->addAction(list[i].name, this, [this, i] { removeBookmark(i); });
        });
        auto* shortcut = new QAction(tr("Ajouter un signet"), this);
        shortcut->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
        connect(shortcut, &QAction::triggered, this, [this] { addBookmark(); });
        addAction(shortcut);
        pageAction(fileMenu, OnAll, bm->menuAction());
    }
    fileMenu->addSeparator();
    add(fileMenu, OnAll, "", tr("Quitter"), QKeySequence::Quit, [this] { close(); });

    // ---- Edit
    auto* editMenu = menuBar()->addMenu(tr("&Édition"));
    // Undo / redo the changes to the game (Databases and Analysis pages; on the Play page,
    // Ctrl+Z takes the move back).
    QAction* undoAction = add(editMenu, OnDatabase | OnAnalysis, "undo", tr("Annuler"), QKeySequence::Undo, [this] {
        if (m_session->undo()) statusBar()->showMessage(tr("Modification annulée"), 3000);
    });
    QAction* redoAction = add(editMenu, OnDatabase | OnAnalysis, "", tr("Rétablir"), QKeySequence::Redo, [this] {
        if (m_session->redo()) statusBar()->showMessage(tr("Modification rétablie"), 3000);
    });
    undoAction->setToolTip(tr("Annule la dernière modification de la partie : coup ajouté, variante supprimée ou promue, commentaire, symbole, flèches, informations"));
    auto refreshUndo = [this, undoAction, redoAction] {
        m_undoAvailable = m_session->canUndo();
        m_redoAvailable = m_session->canRedo();
        undoAction->setText(m_undoAvailable ? tr("Annuler") : tr("Annuler (rien à annuler)"));
        redoAction->setText(m_redoAvailable ? tr("Rétablir") : tr("Rétablir (rien à rétablir)"));
    };
    connect(m_session, &GameSession::undoStateChanged, this, refreshUndo);
    // A move of the notation waiting to be replaced: said in the status bar as long as it lasts
    connect(m_session, &GameSession::replaceModeChanged, this, [this](bool armed) {
        if (!armed) statusBar()->clearMessage();
        else
            statusBar()->showMessage(m_session->replaceKeepsFollowing() ? tr("Jouez sur l'échiquier le coup de remplacement ; la suite est gardée tant qu'elle reste légale (aller ailleurs : abandon)")
                                                                         : tr("Jouez sur l'échiquier le coup de remplacement ; la suite sera supprimée (aller ailleurs : abandon)"));
    });
    connect(m_session, &GameSession::replaceRefused, this, [this] { statusBar()->showMessage(tr("Ce coup est déjà une autre ligne de cette position : rien n'est remplacé"), 6000); });
    refreshUndo();
    editMenu->addSeparator();
    add(editMenu, OnAll, "copy", tr("Copier la FEN"), QKeySequence(QStringLiteral("Ctrl+Shift+C")), [this] {
        QApplication::clipboard()->setText(QString::fromStdString(m_session->current()->pos.fen()));
        statusBar()->showMessage(tr("FEN copiée"), 3000);
    });
    add(editMenu, OnAll, "copy", tr("Copier le PGN"), none, [this] {
        QApplication::clipboard()->setText(QString::fromStdString(pgn::writeGame(m_session->game())));
        statusBar()->showMessage(tr("PGN copié"), 3000);
    });
    add(editMenu, OnAnalysis, "paste", tr("Coller FEN ou PGN"), QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this] { pasteFenOrPgn(); });
    add(editMenu, OnAnalysis | OnBoard, "edit", tr("Éditer la position…"), QKeySequence(QStringLiteral("Ctrl+E")), [this] { editPosition(); });
    editMenu->addSeparator();
    add(editMenu, OnAll, "layout", tr("Préférences…"), QKeySequence::Preferences, [this] { showPreferences(); });

    // ---- Database (Databases page): the searches first, then submenus by theme
    auto* dbMenu = menuBar()->addMenu(tr("&Base"));
    add(dbMenu, OnDatabase, "search", tr("Recherche avancée…"), QKeySequence(QStringLiteral("Ctrl+Shift+H")), [this] {
        if (m_games->currentIndex() < 0) {
            statusBar()->showMessage(tr("Ouvrez d'abord une base"), 4000);
            return;
        }
        HeaderSearchDialog dlg(this);
        dlg.setReferencePosition(m_session->current()->pos);
        if (dlg.exec() == QDialog::Accepted) m_games->searchHeaders(dlg.query(), dlg.combine());
    });
    add(dbMenu, OnDatabase, "search", tr("Rechercher la position de l'échiquier"), QKeySequence(QStringLiteral("Ctrl+Shift+F")),
        [this] { searchBoardPosition(); });
    QMenu* searchMenu = submenu(dbMenu, OnDatabase, "search", tr("Autres recherches"));
    add(searchMenu, OnDatabase, "search", tr("Recherche par matériel et motifs…"), none, [this] {
        if (m_games->currentIndex() < 0) {
            statusBar()->showMessage(tr("Ouvrez d'abord une base"), 4000);
            return;
        }
        HeaderSearchDialog dlg(this);
        dlg.setReferencePosition(m_session->current()->pos);
        dlg.showMaterialTab();
        if (dlg.exec() == QDialog::Accepted) m_games->searchHeaders(dlg.query(), dlg.combine());
    });
    add(searchMenu, OnDatabase, "", tr("Rechercher une manœuvre…"), none, [this] { askManeuver(); });
    add(searchMenu, OnDatabase, "", tr("Rechercher la structure de pions de l'échiquier"), none, [this] { searchSimilar(true); });
    add(searchMenu, OnDatabase, "", tr("Rechercher le matériel de l'échiquier (finales semblables)"), none, [this] { searchSimilar(false); });
    searchMenu->addSeparator();
    add(searchMenu, OnDatabase, "", tr("Inverser le résultat de la recherche"), none, [this] { m_games->negateFilter(); });
    add(dbMenu, OnDatabase, "", tr("Toute la base (effacer la recherche)"), none, [this] { m_games->clearFilter(); });
    dbMenu->addSeparator();
    add(dbMenu, OnDatabase, "", tr("Aller à la partie n°…"), QKeySequence(QStringLiteral("Ctrl+Shift+G")), [this] {
        if (m_games->currentIndex() < 0) return;
        const int total = m_games->model()->totalGames() + (m_games->model()->indexed() ? m_games->model()->indexed()->deletedCount() : 0);
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Aller à la partie"), tr("Numéro de la partie (1 à %1) :").arg(total), 1, 1, std::max(1, total), 1, &ok);
        if (!ok) return;
        const int row = m_games->rowOfNumber(n);
        if (row < 0) {
            statusBar()->showMessage(tr("Partie n° %1 introuvable").arg(n), 4000);
            return;
        }
        m_games->selectGame(row);
        loadGameFromDatabase(row);
    });
    add(dbMenu, OnDatabase, "", tr("Partie au hasard"), none, [this] {
        const int row = m_games->randomRow();
        if (row < 0) return;
        m_games->selectGame(row);
        loadGameFromDatabase(row);
    });
    dbMenu->addSeparator();
    QMenu* selectionMenu = submenu(dbMenu, OnDatabase, "edit", tr("Parties sélectionnées"));
    add(selectionMenu, OnDatabase, "analysis", tr("Envoyer la partie sélectionnée dans l'analyse"), none, [this] {
        const QList<int> rows = m_games->selectedRows();
        if (rows.isEmpty()) statusBar()->showMessage(tr("Sélectionnez une partie dans la base"), 4000);
        else sendDatabaseGameToAnalysis(rows.first());
    });
    add(selectionMenu, OnDatabase, "edit", tr("Modifier les informations de la partie sélectionnée…"), none, [this] {
        const QList<int> rows = m_games->selectedRows();
        if (!rows.isEmpty()) editDatabaseGameInfo(rows.first());
    });
    add(selectionMenu, OnDatabase, "trash", tr("Supprimer les parties sélectionnées…"), none, [this] { m_games->deleteSelected(); });
    QMenu* openingsMenu = submenu(dbMenu, OnDatabase, "book", tr("Ouvertures"));
    add(openingsMenu, OnDatabase, "book", tr("Navigateur ECO…"), none, [this] { ecoBrowser()->show(); });
    add(openingsMenu, OnDatabase, "book", tr("Arbre de la base"), none, [this] {
        if (!m_games->model()->indexed() || !m_games->model()->indexed()->hasPositions()) {
            statusBar()->showMessage(tr("L'arbre demande une base Hydra64 avec index des positions"), 6000);
            return;
        }
        m_book->showDatabaseTree();
        showPanel(QStringLiteral("book"));
    });
    add(openingsMenu, OnDatabase, "book", tr("Rapport d'ouverture (position de l'échiquier)…"), none, [this] {
        if (OpeningReportDialog* d = openingReport()) d->show();
    });
    add(openingsMenu, OnDatabase, "book", tr("Synthèses d'ouverture…"), none, [this] {
        if (SurveyBrowser* d = surveyBrowser()) {
            d->show();
            return;
        }
        QMessageBox::information(this, tr("Synthèses d'ouverture"),
                                 tr("La base affichée n'a pas d'index de synthèses d'ouverture.\n\n"
                                    "Un index accompagne une base livrée avec des synthèses, chacune dans sa propre base "
                                    "(encyclopédie des ouvertures de ChessBase) ; il se crée avec "
                                    "« hydra64-convert --survey-index <base.hy64> <dossier des synthèses> »."));
    });
    add(openingsMenu, OnDatabase, "book", tr("Créer l'arbre d'ouverture de la base…"), none, [this] {
        if (m_games->currentIndex() < 0) return;
        convertDialog(m_games->model()->filePath(), true);
    });
    QMenu* statsMenu = submenu(dbMenu, OnDatabase, "analysis", tr("Statistiques"));
    add(statsMenu, OnDatabase, "book", tr("Index : joueurs, tournois, annotateurs…"), none, [this] { showIndex(); });
    add(statsMenu, OnDatabase, "search", tr("Fiche du joueur…"), none, [this] {
        const QList<int> rows = m_games->selectedRows();
        playerReport(rows.isEmpty() ? QString() : m_games->model()->data(m_games->model()->index(rows.first(), GameListModel::White), Qt::DisplayRole).toString());
    });
    add(statsMenu, OnDatabase, "book", tr("Se préparer contre un joueur…"), none, [this] {
        // The player with White of the selected game is proposed; the window lets another one be typed.
        const QList<int> rows = m_games->selectedRows();
        preparation(rows.isEmpty() ? QString() : m_games->model()->data(m_games->model()->index(rows.first(), GameListModel::White), Qt::DisplayRole).toString());
    });
    add(statsMenu, OnDatabase, "tournament", tr("Tableau croisé du tournoi"), none, [this] {
        const QList<int> rows = m_games->selectedRows();
        if (rows.isEmpty()) statusBar()->showMessage(tr("Sélectionnez une partie du tournoi"), 4000);
        else eventTable(rows.first());
    });
    add(statsMenu, OnDatabase, "board", tr("Pièces sur les parties affichées…"), none, [this] {
        if (PieceHeatDialog* d = pieceHeat()) d->show();
    });
    add(statsMenu, OnDatabase, "analysis", tr("Graphiques du filtre…"), none, [this] {
        if (FilterGraphDialog* d = filterGraph()) d->show();
    });
    {
        QMenu* clip = dbMenu->addMenu(theme::icon("copy"), tr("Presse-papiers (base de dépôt)"));
        m_actionPages.insert(clip->menuAction(), OnDatabase); // a submenu follows its page like an action
        add(clip, OnDatabase, "", tr("Ouvrir la base presse-papiers"), none, [this] { openClipboardBase(); });
        add(clip, OnDatabase, "", tr("Y ajouter la partie affichée"), none, [this] { addCurrentGameToClipboardBase(); });
        add(clip, OnDatabase, "", tr("Vider la base presse-papiers…"), none, [this] {
            if (QMessageBox::question(this, tr("Base presse-papiers"), tr("Retirer toutes les parties de la base presse-papiers ?")) == QMessageBox::Yes) emptyClipboardBase();
        });
    }
    add(dbMenu, OnDatabase, "save", tr("Exporter les parties affichées…"), none, [this] { exportVisibleDialog(); });
    add(dbMenu, OnDatabase, "comment", tr("Textes de la base ChessBase…"), none, [this] { showChessBaseTexts(); });
    dbMenu->addSeparator();
    QMenu* upkeepMenu = submenu(dbMenu, OnDatabase, "layout", tr("Entretien de la base"));
    add(upkeepMenu, OnDatabase, "book", tr("Classer les parties (ECO)…"), none, [this] { classifyDatabase(); });
    add(upkeepMenu, OnDatabase, "star", tr("Évaluer la beauté des parties…"), none, [this] { rateBeauty(); });
    add(upkeepMenu, OnDatabase, "", tr("Doublons…"), none, [this] { twinsDialog(); });
    add(upkeepMenu, OnDatabase, "edit", tr("Corriger un nom…"), none, [this] { renameDialog(); });
    add(upkeepMenu, OnDatabase, "edit", tr("Améliorer les noms des joueurs…"), none, [this] { improvePlayerNames(); });
    upkeepMenu->addSeparator();
    add(upkeepMenu, OnDatabase, "", tr("Trier la base dans l'ordre affiché…"), none, [this] { sortDatabase(); });
    add(upkeepMenu, OnDatabase, "", tr("Compacter la base"), none, [this] { compactDatabase(); });
    add(upkeepMenu, OnDatabase, "", tr("Reconstruire l'index des positions"), none, [this] { rebuildPositionIndex(); });
    add(upkeepMenu, OnDatabase, "", tr("Test d'intégrité…"), none, [this] { checkDatabaseIntegrity(); });

    // ---- Game: what is done to the game first, the tools in submenus
    auto* gameMenu = menuBar()->addMenu(tr("&Partie"));
    add(gameMenu, OnBoard, "engine", tr("Jouer contre le moteur…"), QKeySequence(QStringLiteral("Ctrl+G")), [this] { startEngineGame(); });
    add(gameMenu, OnDatabase | OnBoard | OnTournament, "analysis", tr("Envoyer dans l'analyse"), QKeySequence(QStringLiteral("Ctrl+Shift+A")),
        [this] { sendToAnalysis(); });
    add(gameMenu, OnDatabase | OnAnalysis | OnBoard, "comment", tr("Informations de la partie…"), QKeySequence(QStringLiteral("Ctrl+I")),
        [this] { editGameInfo(); });
    add(gameMenu, OnAnalysis, "engine", tr("Annoter la partie…"), QKeySequence(QStringLiteral("Ctrl+Shift+N")), [this] { annotateDialog(); });
    add(gameMenu, OnBoard, "prev", tr("Reprendre le coup"), QKeySequence::Undo, [this] {
        if (m_play->isActive() && m_page == PageBoard) m_play->takeBack();
        else m_session->back();
    });
    add(gameMenu, OnBoard, "search", tr("Indice"), QKeySequence(QStringLiteral("Ctrl+Shift+I")), [this] { showHint(); });
    add(gameMenu, OnBoard, "", tr("Abandonner"), none, [this] {
        if (m_play->isActive() && QMessageBox::question(this, tr("Abandonner"), tr("Abandonner la partie ?")) == QMessageBox::Yes) m_play->resign();
    });
    gameMenu->addSeparator();
    QMenu* toolsMenu = submenu(gameMenu, OnAll, "analysis", tr("Outils"));
    add(toolsMenu, OnAll, "book", tr("Identifier l'ouverture"), none, [this] { statusBar()->showMessage(identifyOpening(), 8000); });
    add(toolsMenu, OnAnalysis | OnDatabase, "search", tr("Trouver la nouveauté"), none, [this] {
        QString message;
        findNovelty(&message);
        statusBar()->showMessage(message, 10000);
    });
    add(toolsMenu, OnAll, "board", tr("Suivi des pièces…"), none, [this] {
        auto* d = new PieceTracker(m_session->game(), this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    add(toolsMenu, OnAll, "analysis", tr("Calcul de variantes…"), none, [this] {
        auto* d = new CalcVarDialog(m_session->current()->pos, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    add(toolsMenu, OnAnalysis, "play", tr("Finir la partie (moteur)…"), none, [this] {
        if (m_autoPlayer->isRunning()) {
            m_autoPlayer->stop();
            return;
        }
        bool ok = false;
        const double s = QInputDialog::getDouble(this, tr("Finir la partie"), tr("Le moteur d'analyse joue les deux camps depuis la position affichée.\nTemps par coup (secondes) :"),
                                                 QSettings().value("finishGame/seconds", 1.0).toDouble(), 0.1, 60, 1, &ok);
        if (!ok) return;
        QSettings().setValue("finishGame/seconds", s);
        finishGame(int(s * 1000));
    });
    QMenu* trainingMenu = submenu(gameMenu, OnAll, "cursor", tr("Entraînement"));
    add(trainingMenu, OnAll, "cursor", tr("Entraînement…"), none, [this] {
        TrainingDialog* d = trainingDialog();
        d->show();
        d->raise();
        d->activateWindow();
    });
    add(trainingMenu, OnDatabase | OnAnalysis, "", tr("S'entraîner sur les questions de la partie…"), none, [this] { startQuiz(); });
    add(trainingMenu, OnDatabase | OnAnalysis, "", tr("Deviner les coups de la partie…"), none, [this] { startGuess(); });
    add(trainingMenu, OnAll, "new", tr("Ajouter au recueil…"), QKeySequence(QStringLiteral("Ctrl+Shift+R")), [this] { addToCollection(); });
    {
        // Opening repertoire: one tree of moves per side
        QMenu* rep = gameMenu->addMenu(theme::icon("book"), tr("Répertoire d'ouvertures"));
        for (chess::Color c : {White, Black}) {
            const QString side = c == White ? tr("Blancs") : tr("Noirs");
            add(rep, OnAll, "", tr("Ajouter la ligne affichée au répertoire des %1").arg(side), none, [this, c] { addLineToRepertoire(c); });
        }
        rep->addSeparator();
        for (chess::Color c : {White, Black}) {
            const QString side = c == White ? tr("Blancs") : tr("Noirs");
            add(rep, OnAll, "", tr("Ouvrir le répertoire des %1 (page Analyse)").arg(side), none, [this, c] { openRepertoire(c); });
        }
        rep->addSeparator();
        for (chess::Color c : {White, Black}) {
            const QString side = c == White ? tr("Blancs") : tr("Noirs");
            add(rep, OnAnalysis, "", tr("Enregistrer la partie de l'analyse comme répertoire des %1").arg(side), none, [this, c, side] {
                if (QFileInfo::exists(repertoirePath(c))
                    && QMessageBox::question(this, tr("Répertoire d'ouvertures"), tr("Remplacer le répertoire des %1 par la partie affichée ?").arg(side)) != QMessageBox::Yes)
                    return;
                QString error;
                if (saveRepertoire(c, m_session->game(), &error)) statusBar()->showMessage(tr("Répertoire des %1 enregistré").arg(side), 5000);
                else QMessageBox::warning(this, tr("Répertoire d'ouvertures"), error);
            });
        }
        rep->addSeparator();
        for (chess::Color c : {White, Black}) {
            const QString side = c == White ? tr("Blancs") : tr("Noirs");
            add(rep, OnDatabase, "", tr("Rapport : les parties affichées face au répertoire des %1…").arg(side), none, [this, c] { repertoireReport(c); });
        }
    }
    add(gameMenu, OnAll, "copy", tr("Ajouter à la base presse-papiers"), none, [this] { addCurrentGameToClipboardBase(); });
    QMenu* boardMenu = submenu(gameMenu, OnAll, "board", tr("Échiquier"));
    add(boardMenu, OnAll, "flip", tr("Retourner l'échiquier"), QKeySequence(Qt::Key_F), [this] { m_viewer->setFlipped(!m_viewer->isFlipped()); });
    add(boardMenu, OnAll, "copy", tr("Copier l'échiquier en image"), none, [this] {
        QGuiApplication::clipboard()->setImage(boardImage());
        statusBar()->showMessage(tr("Échiquier copié dans le presse-papiers"), 3000);
    });
    add(boardMenu, OnAll, "save", tr("Enregistrer l'échiquier en image…"), none, [this] {
        QSettings st;
        const QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer l'échiquier"), st.value("lastDir").toString() + QStringLiteral("/echiquier.png"),
                                                          tr("Images (*.png *.jpg)"));
        if (path.isEmpty()) return;
        QString error;
        if (!saveBoardImage(path, &error)) QMessageBox::warning(this, tr("Enregistrer l'échiquier"), error);
        else statusBar()->showMessage(tr("Échiquier enregistré : %1").arg(QDir::toNativeSeparators(path)), 4000);
    });
    QMenu* moveMenu = submenu(gameMenu, OnAll, "next", tr("Navigation"));
    add(moveMenu, OnAll, "first", tr("Début"), QKeySequence(Qt::Key_Home), [this] { m_session->toStart(); });
    add(moveMenu, OnAll, "prev", tr("Coup précédent"), QKeySequence(Qt::Key_Left), [this] { m_session->back(); });
    add(moveMenu, OnAll, "next", tr("Coup suivant"), QKeySequence(Qt::Key_Right), [this] { m_session->forward(); });
    add(moveMenu, OnAll, "last", tr("Fin"), QKeySequence(Qt::Key_End), [this] { m_session->toEnd(); });
    gameMenu->addSeparator();
    add(gameMenu, OnAnalysis, "up", tr("Promouvoir la variante"), none, [this] { m_session->promoteVariation(m_session->current()); });
    add(gameMenu, OnAnalysis, "trash", tr("Supprimer le coup et la suite"), QKeySequence(QStringLiteral("Ctrl+Del")),
        [this] { m_session->deleteNode(m_session->current()); });

    // ---- Engine
    auto* engineMenu = menuBar()->addMenu(tr("&Moteur"));
    // Submenu: the engine of the spectator to add; Ctrl+Shift+K adds an engine not used yet.
    auto* spectatorMenu = new QMenu(tr("Ajouter un moteur spectateur"), engineMenu);
    spectatorMenu->setIcon(theme::icon("new"));
    spectatorMenu->setProperty("filledOnShow", true); // otherwise hidden as an empty submenu (updateMenus)
    module::engineChoiceMenu(spectatorMenu, [this](const QString& id) { addSpectatorFromUi(id); });
    pageAction(engineMenu, OnDatabase | OnAnalysis | OnBoard, spectatorMenu->menuAction());
    auto* quickSpectator = new QAction(tr("Ajouter un moteur spectateur"), this);
    quickSpectator->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    connect(quickSpectator, &QAction::triggered, this, [this] { addSpectatorFromUi(); });
    pageAction(nullptr, OnDatabase | OnAnalysis | OnBoard, quickSpectator);
    add(engineMenu, OnBoard, "play", tr("Faire jouer / arrêter le moteur"), QKeySequence(Qt::Key_Space), [this] { m_playEngine->togglePlay(); });
    add(engineMenu, OnBoard, "movenow", tr("Jouer maintenant (coup du moteur)"), QKeySequence(QStringLiteral("Ctrl+Space")), [this] { m_playEngine->forceMove(); });
    add(engineMenu, OnBoard, "engine", tr("Paramètres du moteur adversaire…"), none, [this] { manageEngines(m_playEngine->engine().id); });
    engineMenu->addSeparator();
    add(engineMenu, OnAnalysis, "analysis", tr("Analyse approfondie"), none, [this] { showPanel(QStringLiteral("deep")); });
    add(engineMenu, OnAnalysis, "play", tr("Démarrer / arrêter l'analyse approfondie"), QKeySequence(QStringLiteral("Ctrl+Shift+D")),
        [this] { m_deepPanel->toggleRunning(); });
    add(engineMenu, OnAnalysis, "", tr("Envoyer la position à l'analyse approfondie"), QKeySequence(QStringLiteral("Ctrl+Shift+T")), [this] {
        if (m_deepPanel->sendCurrentPosition()) statusBar()->showMessage(tr("Position envoyée à l'analyse approfondie"), 4000);
    });
    add(engineMenu, OnAnalysis, "", tr("Meilleure ligne de l'analyse approfondie"), QKeySequence(Qt::Key_F4), [this] { m_deepPanel->copyBestLine(); });
    engineMenu->addSeparator();
    add(engineMenu, OnAll, "search", tr("Test de solutions…"), none, [this] {
        SolutionTestDialog* d = solutionTestDialog();
        d->show();
        d->raise();
        d->activateWindow();
    });
    add(engineMenu, OnAll, "search", tr("Vérifier un recueil…"), none, [this] {
        CollectionCheckDialog* d = collectionCheckDialog();
        d->show();
        d->raise();
        d->activateWindow();
    });
    add(engineMenu, OnAll, "engine", tr("Gestion des moteurs…"), QKeySequence(QStringLiteral("Ctrl+Shift+E")), [this] { manageEngines(); });

    // ---- Tournament
    auto* tournamentMenu = menuBar()->addMenu(tr("&Tournoi"));
    add(tournamentMenu, OnTournament, "new", tr("Nouveau tournoi"), none, [this] { m_tournamentPanel->newTournament(); });
    add(tournamentMenu, OnTournament, "play", tr("Lancer / interrompre le tournoi"), QKeySequence(Qt::Key_Space),
        [this] { m_tournamentPanel->toggleRunning(); });
    add(tournamentMenu, OnTournament, "next", tr("Suivre la partie en cours"), none, [this] {
        if (m_tournament->currentIndex() >= 0) showLiveTournamentGame();
        else statusBar()->showMessage(tr("Aucune partie en cours"), 4000);
    });
    m_overviewAction = add(tournamentMenu, OnTournament, "board", tr("Vue d'ensemble des parties en cours"), QKeySequence(QStringLiteral("Ctrl+Shift+G")),
                           [this](bool on) { setTournamentOverview(on); });
    m_overviewAction->setCheckable(true);
    m_overviewAction->setToolTip(tr("Tous les échiquiers des parties simultanées ; un clic sur un échiquier montre sa partie dans la notation, un double-clic la suit en grand"));
    add(tournamentMenu, OnTournament, "save", tr("Exporter les parties…"), none, [this] { m_tournamentPanel->exportGames(); });
    add(tournamentMenu, OnTournament, "", tr("Exporter en page HTML…"), none, [this] { m_tournamentPanel->exportHtml(); });

    // ---- View
    auto* viewMenu = menuBar()->addMenu(tr("&Affichage"));
    const std::pair<ads::CDockWidget*, int> docks[] = {
        {m_notationDock, OnAll},          {m_graphDock, OnDatabase | OnAnalysis | OnBoard},        {m_engineDock, OnDatabase | OnAnalysis},
        {m_gamesDock, OnDatabase | OnAnalysis}, {m_tournamentDock, OnTournament}, {m_clockDock, OnAll},
        {m_bookDock, OnDatabase | OnAnalysis | OnBoard}, {m_deepDock, OnAnalysis}, {m_tablebaseDock, OnAll},
    };
    for (const auto& [dock, pages] : docks) pageAction(viewMenu, pages, dock->toggleViewAction());
    viewMenu->addSeparator();
    add(viewMenu, OnAll, "fontUp", tr("Agrandir la notation"), QKeySequence::ZoomIn, [] { prefs::setNotationFontSize(prefs::notationFontSize() + 1); });
    add(viewMenu, OnAll, "fontDown", tr("Réduire la notation"), QKeySequence::ZoomOut, [] { prefs::setNotationFontSize(prefs::notationFontSize() - 1); });
    viewMenu->addSeparator();
    m_evalBarAction = add(viewMenu, OnAll, "", tr("Barre d'évaluation"), none, [] {});
    m_evalBarAction->setCheckable(true);
    // The Databases page has its own choice (hidden by default); the other pages share that of the Preferences.
    connect(m_evalBarAction, &QAction::toggled, this, [this](bool on) {
        if (m_page == PageDatabase) prefs::setShowEvalBarOnBases(on);
        else prefs::setShowEvalBar(on);
    });
    // Assisted analysis: never on the Play page (it would be the engine helping against the engine)
    m_assistedAction = add(viewMenu, OnDatabase | OnAnalysis, "", tr("Analyse assistée (cases d'arrivée en couleur)"), none, [] {});
    m_assistedAction->setCheckable(true);
    m_assistedAction->setToolTip(tr("Quand vous prenez une pièce, ses cases d'arrivée se colorent : vert bon coup, jaune jouable, rouge faute (Stockfish intégré)"));
    connect(m_assistedAction, &QAction::toggled, this, [](bool on) { prefs::setAssistedAnalysis(on); });
    m_threatAction = add(viewMenu, OnDatabase | OnAnalysis, "", tr("Menace en flèche"), none, [] {});
    m_threatAction->setCheckable(true);
    m_threatAction->setToolTip(tr("Flèche rouge : ce que l'adversaire jouerait si le camp au trait passait son tour, quand cela lui rapporte"));
    connect(m_threatAction, &QAction::toggled, this, [](bool on) { prefs::setThreatArrow(on); });
    m_menuBarAction = add(viewMenu, OnAll, "", tr("Barre de menu"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), [] {});
    m_menuBarAction->setCheckable(true);
    connect(m_menuBarAction, &QAction::toggled, this, [](bool on) { prefs::setHideMenuBar(!on); });
    viewMenu->addSeparator();
    add(viewMenu, OnAll, "layout", tr("Réinitialiser la disposition de la page"), none, [this] { applyDefaultLayout(m_page); });
    {
        // Saved layouts of the displayed page: rebuilt each time the menu opens
        auto* layouts = new QMenu(tr("Dispositions enregistrées"), viewMenu);
        layouts->setIcon(theme::icon("layout"));
        connect(layouts, &QMenu::aboutToShow, this, [this, layouts] {
            layouts->clear();
            layouts->addAction(theme::icon("save"), tr("Enregistrer la disposition de cette page…"), this, [this] {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("Enregistrer la disposition"), tr("Nom de la disposition (un nom déjà pris est remplacé) :"), QLineEdit::Normal, QString(), &ok).trimmed();
                if (ok && saveLayoutAs(name)) statusBar()->showMessage(tr("Disposition « %1 » enregistrée pour cette page").arg(name), 5000);
            });
            const QStringList names = savedLayouts();
            if (names.isEmpty()) return;
            layouts->addSeparator();
            for (const QString& name : names) layouts->addAction(name, this, [this, name] { applySavedLayout(name); });
            layouts->addSeparator();
            QMenu* remove = layouts->addMenu(theme::icon("trash"), tr("Supprimer une disposition"));
            for (const QString& name : names) remove->addAction(name, this, [this, name] { removeSavedLayout(name); });
        });
        pageAction(viewMenu, OnAll, layouts->menuAction());
    }

    // ---- Help: far right of the menu bar
    auto* helpMenu = new QMenu(tr("&Aide"), this);
    add(helpMenu, OnAll, "comment", tr("À propos de Hydra64…"), none, [this] { showAbout(); });
    add(helpMenu, OnAll, "", tr("À propos de Qt…"), none, [] { QApplication::aboutQt(); });
    helpMenu->addSeparator();
    add(helpMenu, OnAll, "trash", tr("Réinitialiser Hydra64…"), none, [this] { resetApplication(); });
    auto* helpBar = new QMenuBar(menuBar());
    helpBar->setStyleSheet(QStringLiteral("QMenuBar { border: none; padding: 0 6px 0 0; }"));
    helpBar->addMenu(helpMenu);
    menuBar()->setCornerWidget(helpBar, Qt::TopRightCorner);

    m_menus = {fileMenu, editMenu, dbMenu, gameMenu, engineMenu, tournamentMenu, viewMenu, helpMenu};
    // Compact menu (click on the logo when the menu bar is hidden): the same menus.
    m_compactMenu = new QMenu(this);
    for (QMenu* m : std::as_const(m_menus)) m_compactMenu->addAction(m->menuAction());
}

void MainWindow::updateMenus()
{
    const int bit = 1 << m_page;
    for (auto it = m_actionPages.cbegin(); it != m_actionPages.cend(); ++it) it.key()->setVisible(it.value() & bit);
    // A submenu with nothing to offer on this page is hidden too
    for (auto it = m_actionPages.cbegin(); it != m_actionPages.cend(); ++it) {
        QMenu* sub = it.key()->menu();
        if (!sub || !(it.value() & bit) || sub->property("filledOnShow").toBool()) continue; // filled when it opens: empty until then
        bool any = false;
        for (QAction* a : sub->actions()) any |= !a->isSeparator() && a->isVisible();
        it.key()->setVisible(any);
    }
    for (QMenu* m : std::as_const(m_menus)) {
        bool any = false;
        for (QAction* a : m->actions()) any |= !a->isSeparator() && a->isVisible();
        m->menuAction()->setVisible(any);
    }
}

void MainWindow::applyDisplayPrefs()
{
    // Databases page (small browsing board): its own choice, hidden by default.
    const bool showBar = m_page == PageDatabase ? prefs::showEvalBarOnBases() : prefs::showEvalBar();
    m_viewer->setEvalBarVisible(showBar && m_page != PageTournament); // no evaluation on the Tournament page
    const bool hideMenu = prefs::hideMenuBar();
    menuBar()->setVisible(!hideMenu);
    if (m_logo)
        m_logo->setToolTip(hideMenu ? tr("Menus (la barre de menu est masquée : Ctrl+Maj+M pour la réafficher)")
                                    : tr("Paramètres de Hydra64 (Ctrl+,)"));
    if (m_evalBarAction) {
        QSignalBlocker b(m_evalBarAction);
        m_evalBarAction->setChecked(showBar);
    }
    const bool helped = m_page == PageDatabase || m_page == PageAnalysis;
    if (m_assisted) m_assisted->setEnabled(helped && prefs::assistedAnalysis(), helped && prefs::threatArrow());
    if (m_assistedAction) {
        QSignalBlocker b(m_assistedAction);
        m_assistedAction->setChecked(prefs::assistedAnalysis());
    }
    if (m_threatAction) {
        QSignalBlocker b(m_threatAction);
        m_threatAction->setChecked(prefs::threatArrow());
    }
    if (m_menuBarAction) {
        QSignalBlocker b(m_menuBarAction);
        m_menuBarAction->setChecked(!hideMenu);
    }
}

namespace {

// Adjusts the first QSplitter ancestor of `w` with the wanted orientation.
void setSplitSizes(QWidget* w, Qt::Orientation o, const QList<int>& sizes)
{
    for (QWidget* p = w ? w->parentWidget() : nullptr; p; p = p->parentWidget()) {
        auto* sp = qobject_cast<QSplitter*>(p);
        if (sp && sp->orientation() == o && sp->count() == sizes.size()) {
            sp->setSizes(sizes);
            return;
        }
    }
}

} // namespace

void MainWindow::applyDefaultLayout(Page page)
{
    // addDockWidget() moves an already docked panel: no need (and harmful) to
    // remove it from the manager. Panels are only taken out of auto-hide.
    // Panels are reopened before being moved: a panel moved while
    // closed keeps a hidden tab in its new area.
    for (ads::CDockWidget* d : panels()) {
        if (d->isAutoHide()) d->setAutoHide(false);
        d->toggleView(true);
    }

    // Each page contains all the panels (the unneeded ones closed), so that
    // restoreState() and the View menu work everywhere.
    ads::CDockAreaWidget* notationArea = m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_notationDock);
    auto tabs = [&](std::initializer_list<ads::CDockWidget*> docks) {
        for (ads::CDockWidget* d : docks) m_dockManager->addDockWidgetTabToArea(d, notationArea);
    };

    QList<ads::CDockWidget*> visible;
    switch (page) {
    case PageAnalysis:
        // On the left: deep analysis above the evaluation. In the centre: the board.
        // On the right, top to bottom: opening tree, tablebases, notation, spectator engines.
        m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_engineDock, notationArea);
        {
            ads::CDockAreaWidget* bookArea = m_dockManager->addDockWidget(ads::TopDockWidgetArea, m_bookDock, notationArea);
            m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_tablebaseDock, bookArea);
            ads::CDockAreaWidget* deepArea = m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_deepDock);
            m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_graphDock, deepArea);
        }
        tabs({m_gamesDock, m_playEngineDock, m_tournamentDock, m_clockDock, m_boardDock});
        visible = {m_notationDock, m_graphDock, m_bookDock, m_deepDock, m_tablebaseDock};
        if (!m_engineRemoved) visible << m_engineDock;
        {
            // Spectators stacked under the first engine panel
            ads::CDockAreaWidget* below = m_engineDock->dockAreaWidget();
            for (const Spectator& s : std::as_const(m_spectators)) {
                if (s.page != PageAnalysis) {
                    m_dockManager->addDockWidgetTabToArea(s.dock, notationArea);
                    continue;
                }
                below = m_dockManager->addDockWidget(ads::BottomDockWidgetArea, s.dock, below);
                visible << s.dock;
            }
        }
        break;
    case PageDatabase:
        // Open databases (as icons) above the game list, board on the right above the notation.
        m_dockManager->addDockWidget(ads::TopDockWidgetArea, m_gamesDock, m_dockManager->centralWidget()->dockAreaWidget());
        m_dockManager->addDockWidget(ads::TopDockWidgetArea, m_boardDock, notationArea);
        tabs({m_engineDock, m_graphDock, m_playEngineDock, m_tournamentDock, m_clockDock, m_bookDock, m_deepDock, m_tablebaseDock});
        visible = {m_gamesDock, m_boardDock, m_notationDock};
        {
            // Spectators of the Databases page stacked under the notation
            ads::CDockAreaWidget* below = notationArea;
            for (const Spectator& s : std::as_const(m_spectators)) {
                if (s.page != PageDatabase) {
                    m_dockManager->addDockWidgetTabToArea(s.dock, notationArea);
                    continue;
                }
                below = m_dockManager->addDockWidget(ads::BottomDockWidgetArea, s.dock, below);
                visible << s.dock;
            }
        }
        break;
    case PageTournament:
        // Tournament on the right over the full height, notation to the left of the board.
        // No evaluation on this page: the tournament engines keep all the processors.
        tabs({m_tournamentDock, m_gamesDock, m_engineDock, m_playEngineDock, m_graphDock, m_clockDock, m_bookDock, m_deepDock, m_tablebaseDock, m_boardDock});
        for (const Spectator& s : std::as_const(m_spectators)) m_dockManager->addDockWidgetTabToArea(s.dock, notationArea);
        m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_notationDock);
        visible = {m_tournamentDock, m_notationDock};
        break;
    case PageBoard:
    default:
        // Right column: clock, notation, evaluation, opponent engine.
        m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_graphDock, notationArea);
        m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_playEngineDock, m_graphDock->dockAreaWidget());
        m_dockManager->addDockWidget(ads::TopDockWidgetArea, m_clockDock, notationArea);
        tabs({m_gamesDock, m_engineDock, m_tournamentDock, m_bookDock, m_deepDock, m_tablebaseDock, m_boardDock});
        visible = {m_clockDock, m_notationDock, m_graphDock, m_playEngineDock};
        {
            // Spectators of the Play page stacked under the opponent engine
            ads::CDockAreaWidget* below = m_playEngineDock->dockAreaWidget();
            for (const Spectator& s : std::as_const(m_spectators)) {
                if (s.page != PageBoard) {
                    m_dockManager->addDockWidgetTabToArea(s.dock, notationArea);
                    continue;
                }
                below = m_dockManager->addDockWidget(ads::BottomDockWidgetArea, s.dock, below);
                visible << s.dock;
            }
        }
        break;
    }
    // Explicit visibility: a panel closed on another page would otherwise stay closed.
    for (ads::CDockWidget* d : panels()) d->toggleView(visible.contains(d));
    m_notationDock->setAsCurrentTab();
    m_tournamentDock->setAsCurrentTab();

    // Proportions applied once the geometry is computed. The areas are read again
    // at that point: ADS may have destroyed some in the meantime (do not capture them).
    m_layoutPending = true;
    const int generation = ++m_layoutGeneration;
    QTimer::singleShot(0, this, [this, page, generation] {
        if (generation != m_layoutGeneration) return; // a more recent layout has been applied
        m_layoutPending = false;
        if (page != m_page) return;
        ads::CDockAreaWidget* notationArea = m_notationDock->dockAreaWidget();
        ads::CDockAreaWidget* engineArea = m_engineDock->dockAreaWidget();
        ads::CDockAreaWidget* graphArea = m_graphDock->dockAreaWidget();
        ads::CDockAreaWidget* gamesArea = m_gamesDock->dockAreaWidget();
        ads::CDockAreaWidget* tournamentArea = m_tournamentDock->dockAreaWidget();
        const int w = m_dockManager->width(), h = m_dockManager->height();
        switch (page) {
        case PageAnalysis: {
            const int left = std::max(320, int(w * 0.27)), right = std::max(420, int(w * 0.25));
            setSplitSizes(notationArea, Qt::Horizontal, {left, w - left - right, right});
            setSplitSizes(graphArea, Qt::Vertical, {h - 150, 150});
            // Right column: tree, tablebases, notation, then the bottom shared between the page's engines
            int engines = m_engineRemoved ? 0 : 1;
            for (const Spectator& sp : std::as_const(m_spectators)) engines += sp.page == PageAnalysis;
            QList<int> sizes{int(h * 0.25), int(h * 0.34), engines ? int(h * 0.20) : h - int(h * 0.25) - int(h * 0.34)};
            for (int i = 0; i < engines; ++i) sizes << (h - sizes[0] - sizes[1] - sizes[2]) / engines;
            setSplitSizes(notationArea, Qt::Vertical, sizes);
            Q_UNUSED(engineArea);
            break;
        }
        case PageDatabase: {
            // On the left the open databases above the game list; on the right the board above the notation
            const int right = std::max(380, int(w * 0.225)), top = std::max(150, int(h * 0.22));
            setSplitSizes(notationArea, Qt::Horizontal, {w - right, right});
            setSplitSizes(gamesArea, Qt::Vertical, {top, h - top});
            // Board, notation, then the page's spectators if any
            int engines = 0;
            for (const Spectator& sp : std::as_const(m_spectators)) engines += sp.page == PageDatabase;
            QList<int> sizes{int(h * 0.62), h - int(h * 0.62)};
            if (engines) {
                sizes = {int(h * 0.5), int(h * 0.2)};
                for (int i = 0; i < engines; ++i) sizes << (h - sizes[0] - sizes[1]) / engines;
            }
            setSplitSizes(m_boardDock->dockAreaWidget(), Qt::Vertical, sizes);
            break;
        }
        case PageBoard: {
            const int right = std::max(420, int(w * 0.325));
            setSplitSizes(notationArea, Qt::Horizontal, {w - right, right});
            // Clock, notation, evaluation, then the bottom shared between the opponent and the page's spectators
            int engines = 1;
            for (const Spectator& sp : std::as_const(m_spectators)) engines += sp.page == PageBoard;
            QList<int> sizes{int(h * 0.165), int(h * 0.41), int(h * 0.17)};
            const int rest = h - sizes[0] - sizes[1] - sizes[2];
            for (int i = 0; i < engines; ++i) sizes << rest / engines;
            setSplitSizes(notationArea, Qt::Vertical, sizes);
            break;
        }
        case PageTournament: {
            const int left = std::max(260, int(w * 0.18)), right = std::max(480, int(w * 0.35));
            setSplitSizes(tournamentArea, Qt::Horizontal, {left, w - left - right, right});
            break;
        }
        default: break;
        }
    });
}

void MainWindow::flushFloatingContainers()
{
    // QtADS deletes its floating windows that are no longer needed in a deferred way (deleteLater). As long as the event
    // loop has not run, such a window still exists: saving or restoring a layout
    // at that moment may put panels into it, which its deletion then takes away (panels destroyed, crash).
    // The pending deletions are therefore done here, for those windows only.
    // (parentless windows, see cmake/patch-qtads.cmake: they are found among the application's windows)
    QList<QPointer<QWidget>> floating;
    for (QWidget* w : QApplication::topLevelWidgets())
        if (qobject_cast<ads::CFloatingDockContainer*>(w)) floating << w;
    for (const QPointer<QWidget>& f : std::as_const(floating))
        if (f) QCoreApplication::sendPostedEvents(f, QEvent::DeferredDelete);
}

void MainWindow::setPage(Page page)
{
    const Page previous = m_page;
    const bool changed = page != previous || !m_pageShown;
    flushFloatingContainers();
    // Same page asked again (a database opened from the Databases page): the layout restored below
    // must be the current one, not the one saved when the page was last left — a panel moved since
    // would go back to its old place.
    if (page == previous && m_pageShown && !m_layoutPending && !m_keepPageState) m_pageStates[page] = m_dockManager->saveState(LayoutVersion);
    if (page != previous) {
        // A default layout not yet sized is not remembered.
        if (!m_layoutPending) m_pageStates[previous] = m_dockManager->saveState(LayoutVersion);
        m_pageFlipped[previous] = m_viewer->isFlipped();
        // Changing page stops the engines; none restarts by itself on return.
        m_engine->setAnalysing(false);
        m_viewer->hideBanner(); // the end-of-game banner concerns the page being left
        for (Spectator& s : m_spectators) // the spectators of the page being left stop
            if (s.page == previous) s.panel->setAnalysing(false);
        m_page = page;
    }
    m_pageShown = true;
    arrangeCenter(page);
    updateTournamentView();
    // Each page keeps its game. Leaving Play pauses the game against the engine (engine and
    // clock stopped); on return it stays paused until the player's move, or Play.
    const auto wanted = GameSession::Slot(page);
    if (m_session->slot() != wanted) {
        if (m_session->slot() == GameSession::PlaySlot) m_play->setSuspended(true);
        m_session->switchSlot(wanted);
        if (wanted == GameSession::PlaySlot) m_play->setSuspended(false);
    }
    if (changed) {
        m_viewer->setFlipped(m_pageFlipped[page]);
    }
    // Databases page: the Board panel keeps the height of the whole board (raising the notation does not
    // cover it). Elsewhere it is closed, stored as a tab: it must not enlarge its area.
    // Set before placing the panels: an area remembers the minimum size of its panels at that moment.
    m_boardHolder->setMinimumHeight(page == PageDatabase ? m_viewer->minimumSizeHint().height() : MinPanelHeight);
    if (m_pageStates[page].isEmpty() || !m_dockManager->restoreState(m_pageStates[page], LayoutVersion)) applyDefaultLayout(page);
    if (page != PageDatabase) m_boardDock->toggleView(false); // the board is in the centre of these pages
    if (m_engineRemoved) m_engineDock->toggleView(false); // first engine panel removed
    if (page == PageTournament) m_graphDock->toggleView(false); // no evaluation during tournaments
    lockStripHeights();
    if (page == PageBoard && m_playEngineDock->isClosed()) m_playEngineDock->toggleView(true); // layout saved earlier
    applyDisplayPrefs(); // evaluation bar hidden on the Tournament page
    // Spectators: only on their page (and placed again if missing from the layout).
    for (Spectator& s : m_spectators) {
        if (page != s.page) {
            s.dock->toggleView(false);
            continue;
        }
        if (s.dock->isClosed() || !s.dock->dockAreaWidget()) {
            s.dock->toggleView(true);
            if (!s.dock->dockAreaWidget() || s.dock->isFloating()) placeSpectator(s.dock);
        }
    }
    if (auto* b = m_pageButtons->button(page)) b->setChecked(true);
    m_viewer->setSendToAnalysisVisible(page != PageAnalysis);
    m_notationPanel->view()->setSendToAnalysisVisible(page != PageAnalysis);
    updateDeepArrow();
    updateMenus();
    updateAutoEval();
    updateClockTitle();
    updateBoardInteraction();
    updateClocks();
    updatePlayHint();
    updateTitle();
}

void MainWindow::updateAutoEval()
{
    if (m_closing) return;
    // Only when the Evaluation tab is actually displayed (not behind another tab,
    // nor folded into a side bar). Never on the Tournament page: the competing
    // engines keep all the processors.
    m_autoEval->setEnabled(m_page != PageTournament && !m_graphDock->isClosed() && m_graphDock->isVisible());
}

void MainWindow::updateClockTitle()
{
    // Time control of the displayed game; Play page without a game: that of the next game.
    QString tc = QString::fromStdString(m_session->game().header("TimeControl"));
    if (tc.isEmpty() && m_page == PageBoard && !m_play->isActive()) {
        const PlaySettings ps = PlayDialog::savedSettings();
        if (ps.isTimed()) tc = QStringLiteral("%1+%2").arg(ps.baseMs / 1000).arg(ps.incMs / 1000);
    }
    const QString label = ClockPanel::timeControlLabel(tc);
    m_clockDock->setWindowTitle((label.isEmpty() ? tr("Pendule") : tr("Pendule : %1").arg(label)).toUpper());
}

void MainWindow::showPanel(const QString& name)
{
    for (ads::CDockWidget* d : panels())
        if (d->objectName() == name && d->toggleViewAction()->isVisible()) {
            if (d->isClosed()) d->toggleViewAction()->trigger();
            d->setAsCurrentTab(); // tab behind another: brought to the front
            return;
        }
}

void MainWindow::placeClockDock()
{
    // Default layout being applied: we wait for its proportions.
    if (m_layoutPending) {
        QTimer::singleShot(30, this, &MainWindow::placeClockDock);
        return;
    }
    // Opened from the menu: above the notation, as a low strip (not as a hidden tab).
    ads::CDockAreaWidget* notationArea = m_notationDock->isClosed() ? nullptr : m_notationDock->dockAreaWidget();
    // Already alone in its area (placed by the user): it is left where it is.
    ads::CDockAreaWidget* own = m_clockDock->dockAreaWidget();
    if (own && own->dockWidgetsCount() == 1 && !m_clockDock->isFloating()) return;
    // Heights of the column before the addition: they are restored to scale afterwards
    // (otherwise ADS shares the room equally and squashes the other panels).
    QSplitter* column = notationArea ? qobject_cast<QSplitter*>(notationArea->parentWidget()) : nullptr;
    QList<QPair<QPointer<QWidget>, int>> before;
    if (column && column->orientation() == Qt::Vertical)
        for (int i = 0; i < column->count(); ++i) before.append({column->widget(i), column->sizes().value(i)});
    if (notationArea) m_dockManager->addDockWidget(ads::TopDockWidgetArea, m_clockDock, notationArea);
    else m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_clockDock);
    m_clockDock->toggleView(true);
    QTimer::singleShot(0, this, [this, before] {
        ads::CDockAreaWidget* clockArea = m_clockDock->dockAreaWidget();
        auto* sp = clockArea ? qobject_cast<QSplitter*>(clockArea->parentWidget()) : nullptr;
        if (!sp || sp->orientation() != Qt::Vertical) return;
        const int want = 100; // just the height of the displays
        const QList<int> now = sp->sizes();
        int total = 0, oldTotal = 0;
        for (int v : now) total += v;
        for (const auto& [w, v] : before)
            if (w && sp->indexOf(w) >= 0) oldTotal += v;
        QList<int> sizes;
        for (int i = 0; i < sp->count(); ++i) {
            QWidget* w = sp->widget(i);
            if (w == clockArea) {
                sizes << want;
                continue;
            }
            int v = now.value(i);
            for (const auto& [bw, bv] : before)
                if (bw == w && oldTotal > 0) v = int(qint64(bv) * (total - want) / oldTotal);
            sizes << v;
        }
        sp->setSizes(sizes);
    });
}

void MainWindow::updatePlayHint()
{
    if (m_page == PageBoard && !m_play->isActive()) {
        const PlaySettings ps = PlayDialog::savedSettings();
        EngineConfig e = EngineRegistry::instance().engine(ps.engineId);
        if (!e.isValid()) e = EngineRegistry::instance().fallbackEngine();
        if (e.isValid()) {
            const bool hasElo = e.options.isEmpty() || e.option(QStringLiteral("UCI_Elo"));
            m_playHint = !hasElo ? tr("Jouez un coup pour commencer une partie contre %1 — réglages : Ctrl+G").arg(e.name)
                                 : tr("Jouez un coup pour commencer une partie contre %1 (%2) — réglages : Ctrl+G")
                                       .arg(e.name, ps.fullStrength ? tr("force maximale") : tr("Elo %1").arg(ps.elo));
            statusBar()->showMessage(m_playHint);
        }
    } else if (!m_playHint.isEmpty() && statusBar()->currentMessage() == m_playHint) {
        statusBar()->clearMessage(); // the hint only concerns the Play page
    }
}

bool MainWindow::openDatabase(const QString& requested)
{
    const QString path = requested;
    // Proprietary formats (ChessBase…): no direct opening, conversion is offered.
    if (conversion::isProprietary(path)) {
        if (m_restoringDatabases) return false;
        QMessageBox box(QMessageBox::Question, tr("Ouvrir une base"),
                        tr("« %1 » est au format %2, que Hydra64 n'ouvre pas directement.").arg(QFileInfo(path).fileName(), conversion::formatName(path)),
                        QMessageBox::NoButton, this);
        box.setInformativeText(tr("Convertissez-la au format Hydra64 (rapide, modifiable) ou en PGN."));
        QPushButton* convert = box.addButton(tr("Convertir…"), QMessageBox::AcceptRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == convert) convertDialog(path);
        return false;
    }
    // Opening tree (.hy64book, Polyglot .bin): it is not a database of games; its place is the tree panel.
    if (conversion::kindOf(path) == conversion::Kind::Tree) {
        if (m_restoringDatabases) return false;
        QMessageBox box(QMessageBox::Question, tr("Ouvrir une base"),
                        tr("« %1 » est un arbre d'ouverture (%2), pas une base de parties.").arg(QFileInfo(path).fileName(), conversion::formatName(path)),
                        QMessageBox::NoButton, this);
        box.setInformativeText(tr("Il s'ouvre dans le panneau Arbre d'ouverture."));
        QPushButton* asTree = box.addButton(tr("Ouvrir l'arbre"), QMessageBox::AcceptRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == asTree) {
            QString why;
            if (m_book->openBook(path, &why)) showPanel(QStringLiteral("book"));
            else QMessageBox::warning(this, tr("Ouvrir une base"), why);
        }
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = m_games->openFile(path, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Ouvrir une base"), tr("Impossible d'ouvrir %1 :\n%2").arg(path, error));
        return false;
    }
    const int games = m_games->model()->totalGames();
    statusBar()->showMessage(tr("%1 : %2 partie(s)").arg(QFileInfo(path).fileName(), QLocale().toString(games)), 6000);
    return true;
}

QString MainWindow::extractChessBaseArchive(const QString& archive)
{
    const QString name = QFileInfo(archive).completeBaseName();
    auto databaseIn = [](const QString& dir) {
        const QFileInfoList found = QDir(dir).entryInfoList({QStringLiteral("*.cbh"), QStringLiteral("*.2cbh")}, QDir::Files);
        return found.isEmpty() ? QString() : found.first().absoluteFilePath();
    };
    QString dir = userdirs::databases() + QLatin1Char('/') + name;
    if (const QString db = databaseIn(dir); !db.isEmpty()) return db; // already extracted

    QMessageBox box(QMessageBox::Question, tr("Archive ChessBase"), tr("« %1 » est une archive ChessBase (%2).").arg(QFileInfo(archive).fileName(),
                                                                                                                     QLocale().formattedDataSize(QFileInfo(archive).size())),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("Elle doit être extraite une fois pour être lue (la base extraite occupe environ deux fois plus de place).\n\n"
                              "Dossier : %1")
                               .arg(QDir::toNativeSeparators(dir)));
    QPushButton* extract = box.addButton(tr("Extraire"), QMessageBox::AcceptRole);
    QPushButton* other = box.addButton(tr("Choisir un dossier…"), QMessageBox::ActionRole);
    box.addButton(tr("Annuler"), QMessageBox::RejectRole);
    box.setDefaultButton(extract);
    box.exec();
    if (box.clickedButton() == other) {
        const QString parent = QFileDialog::getExistingDirectory(this, tr("Dossier où extraire la base"), QFileInfo(dir).absolutePath());
        if (parent.isEmpty()) return {};
        dir = parent + QLatin1Char('/') + name;
        if (const QString db = databaseIn(dir); !db.isEmpty()) return db;
    } else if (box.clickedButton() != extract) {
        return {};
    }

    // Extraction in the background: a few seconds, the interface stays drawn.
    QProgressDialog progress(tr("Extraction de « %1 »…").arg(name), QString(), 0, 0, this);
    progress.setWindowTitle(tr("Archive ChessBase"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    bool ok = false;
    QString error;
    QEventLoop loop;
    QThread* thread = QThread::create([&] { ok = ChessBaseSource::extractArchive(archive, dir, &error); });
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;
    progress.close();
    const QString db = ok ? databaseIn(dir) : QString();
    if (db.isEmpty()) {
        QMessageBox::warning(this, tr("Archive ChessBase"),
                             tr("Impossible d'extraire « %1 » :\n%2").arg(QFileInfo(archive).fileName(), ok ? tr("aucune base dans l'archive") : error));
        return {};
    }
    statusBar()->showMessage(tr("Archive extraite dans %1").arg(QDir::toNativeSeparators(dir)), 6000);
    return db;
}

void MainWindow::openDatabaseDialog()
{
    QSettings s;
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir une base"), userdirs::startDir(userdirs::Kind::Database),
                                                      tr("Bases de parties (*.pgn *.hy64i *.hy64 *.epd);;Base Hydra64 (*.hy64i *.hy64);;Parties PGN (*.pgn);;"
                                                         "Positions EPD (*.epd);;Tous les fichiers (*)"));
    if (path.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    userdirs::remember(userdirs::Kind::Database, path);
    if (openDatabase(path)) setPage(PageDatabase);
}

namespace {
const char* const Hy64SingleFilter = QT_TRANSLATE_NOOP("MainWindow", "Base Hydra64 en un fichier (*.hy64)");
const char* const Hy64Filter = QT_TRANSLATE_NOOP("MainWindow", "Base Hydra64 en quatre fichiers (*.hy64i)");
const char* const PgnFilter = QT_TRANSLATE_NOOP("MainWindow", "Parties PGN (*.pgn)");

// Adds the extension of the format chosen in the file chooser if missing.
QString withSuffix(QString path, const QString& filter)
{
    const bool single = filter.contains(QLatin1String("*.hy64)")) || path.endsWith(QLatin1String(".hy64"), Qt::CaseInsensitive);
    const bool hy64 = filter.contains(QLatin1String(".hy64i")) || path.endsWith(QLatin1String(".hy64i"), Qt::CaseInsensitive);
    const QString ext = hy64 ? QStringLiteral(".hy64i") : single ? QStringLiteral(".hy64") : QStringLiteral(".pgn");
    if (!path.endsWith(ext, Qt::CaseInsensitive)) path += ext;
    return path;
}
} // namespace

bool MainWindow::createDatabaseFile(const QString& path)
{
    QString error;
    if (Hy64Source::isDatabasePath(path)) {
        if (Hy64Source::create(path, &error)) return true;
    } else {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return true;
        error = f.errorString();
    }
    QMessageBox::warning(this, tr("Nouvelle base"), tr("Impossible de créer %1 :\n%2").arg(path, error));
    return false;
}

void MainWindow::newDatabase()
{
    QSettings s;
    QString filter = tr(Hy64SingleFilter); // offered first: a single file
    QString path = QFileDialog::getSaveFileName(this, tr("Nouvelle base"), userdirs::startDir(userdirs::Kind::Database) + QStringLiteral("/Nouvelle base"),
                                                tr(Hy64SingleFilter) + QStringLiteral(";;") + tr(Hy64Filter) + QStringLiteral(";;") + tr(PgnFilter), &filter);
    if (path.isEmpty()) return;
    path = withSuffix(path, filter);
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    userdirs::remember(userdirs::Kind::Database, path);
    if (createDatabaseFile(path)) openDatabase(path);
}

bool MainWindow::runWithProgress(const QString& title, const QString& label,
                                 const std::function<bool(std::atomic<int>&, std::atomic<bool>&, QString&)>& work, bool cancellable)
{
    QProgressDialog progress(label, cancellable ? tr("Annuler") : QString(), 0, 1000, this);
    progress.setWindowTitle(title);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    std::atomic<int> permille{-1};
    std::atomic<bool> cancel{false};
    QString status;
    std::mutex statusMutex;
    bool ok = false;
    QEventLoop loop;
    QThread* thread = QThread::create([&] {
        QString s;
        ok = work(permille, cancel, s);
        std::lock_guard lock(statusMutex);
        status = s;
    });
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    connect(&progress, &QProgressDialog::canceled, &progress, [&] { cancel = true; });
    QTimer tick;
    connect(&tick, &QTimer::timeout, &progress, [&] {
        const int p = permille.load();
        if (p < 0) progress.setRange(0, 0);
        else {
            progress.setRange(0, 1000);
            progress.setValue(p);
        }
    });
    tick.start(100);
    progress.show();
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;
    // Closing the window emits "canceled": the cancellation state is read beforehand.
    const bool cancelled = cancel.load();
    progress.disconnect();
    progress.close();
    if (!ok && !cancelled && !status.isEmpty()) QMessageBox::warning(this, title, status);
    return ok && !cancelled;
}

void MainWindow::convertDialog(const QString& source, bool tree)
{
    ConvertDialog dlg(source, this);
    if (tree) dlg.setTarget(conversion::Target::Hydra64Tree);
    if (dlg.exec() != QDialog::Accepted) return;
    runConversion(dlg.request(), dlg.openResult());
}

bool MainWindow::runConversion(conversion::Request r, bool openResult, bool showReport)
{
    if (conversion::isArchive(r.source)) { // ChessBase archive: extracted once, then converted
        r.source = extractChessBaseArchive(r.source);
        if (r.source.isEmpty()) return false;
    }
    if (QFileInfo(r.destination) == QFileInfo(r.source)) {
        QMessageBox::warning(this, tr("Convertir"), tr("La destination doit être différente de la source."));
        return false;
    }
    // Destination file already open: it is closed before being replaced.
    if (m_games->activate(r.destination)) m_games->closeCurrent();
    if (QFileInfo(m_book->bookPath()) == QFileInfo(r.destination)) m_book->closeBook();

    conversion::Report report;
    QString error;
    const bool ok = runWithProgress(tr("Convertir"), tr("Conversion de « %1 »…").arg(QFileInfo(r.source).fileName()),
                                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                                        const bool done = conversion::run(r, &report, &status, [&](int p) {
                                            permille = p;
                                            return !cancel.load();
                                        });
                                        return done;
                                    },
                                    true);
    if (!ok) {
        statusBar()->showMessage(tr("Conversion interrompue"), 6000);
        return false;
    }
    if (showReport) QMessageBox::information(this, tr("Convertir"), report.summary());
    statusBar()->showMessage(tr("Converti : %1").arg(QDir::toNativeSeparators(r.destination)), 6000);
    if (!openResult) return true;
    if (report.kind == conversion::Kind::Database) {
        if (openDatabase(r.destination)) setPage(PageDatabase);
    } else if (m_book->openBook(r.destination)) {
        showPanel(QStringLiteral("book"));
    }
    return true;
}

IndexDialog* MainWindow::showIndex()
{
    GameListModel* model = m_games->model();
    const auto* source = model && model->isIndexed() ? dynamic_cast<const Hy64Source*>(model->indexed()) : nullptr;
    if (!source) {
        statusBar()->showMessage(tr("L'index demande une base Hydra64 (Fichier → Convertir une base…)"), 8000);
        return nullptr;
    }
    // Modal window: the database cannot be closed or modified while the index reads it.
    auto* dlg = new IndexDialog(source->database(), QFileInfo(model->filePath()).completeBaseName(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::WindowModal);
    connect(dlg, &IndexDialog::gamesRequested, this, [this, dlg](int type, quint32 id, const QString& name) {
        dlg->close();
        showGamesOf(type, id, name);
    });
    connect(dlg, &IndexDialog::playerCardRequested, this, [this, dlg](const QString& name) {
        dlg->close();
        playerReport(name);
    });
    dlg->show();
    return dlg;
}

QuizDialog* MainWindow::startQuiz()
{
    auto* dlg = new QuizDialog(m_session->game(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    return dlg;
}

GuessDialog* MainWindow::startGuess(chess::Color side, int fromMove)
{
    if (!m_session->game().root()->mainChild()) {
        statusBar()->showMessage(tr("Aucun coup à deviner : la partie affichée est vide"), 4000);
        return nullptr;
    }
    if (fromMove <= 0 && !GuessDialog::ask(this, m_session->game(), &side, &fromMove)) return nullptr;
    auto* dlg = new GuessDialog(m_session->game(), side, fromMove, this);
    dlg->setEngine(AutoEvaluator::findStockfish());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    return dlg;
}

int MainWindow::showChessBaseTexts(bool show)
{
    const QString path = m_games->model() ? m_games->model()->filePath() : QString();
    QList<CbText> texts;
    QString error;
    if (!path.endsWith(QLatin1String(".cbh"), Qt::CaseInsensitive)) {
        statusBar()->showMessage(tr("Les textes se lisent dans une base ChessBase (.cbh) ouverte ; à la conversion, ils sont gardés parmi les parties"), 9000);
        return -1;
    }
    if (!cbtexts::read(path, cbtexts::interfaceLanguage(), texts, &error) || texts.isEmpty()) {
        statusBar()->showMessage(error.isEmpty() ? tr("Cette base ne contient aucun texte") : error, 6000);
        return -1;
    }
    if (!show) return int(texts.size());
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Textes de « %1 »").arg(QFileInfo(path).completeBaseName()));
    dlg->resize(1000, 680);
    auto* list = new QListWidget;
    auto* view = new QTextBrowser;
    view->setOpenLinks(false); // the links target ChessBase games or files
    for (const CbText& t : texts) list->addItem(t.title.isEmpty() ? tr("Texte n° %1").arg(t.record) : t.title);
    connect(list, &QListWidget::currentRowChanged, view, [view, texts](int row) {
        if (row >= 0 && row < texts.size()) view->setHtml(texts[row].html);
    });
    auto* split = new QSplitter;
    split->addWidget(list);
    split->addWidget(view);
    split->setStretchFactor(1, 1);
    split->setSizes({280, 720});
    auto* layout = new QVBoxLayout(dlg);
    layout->addWidget(split);
    list->setCurrentRow(0);
    dlg->show();
    return int(texts.size());
}

bool MainWindow::showGamesOf(int nameType, quint32 id, const QString& name)
{
    const auto type = hy64db::NameType(nameType);
    const bool started = m_games->searchByReplay(name, [type, id](const hy64db::Database& db, const std::atomic<int32_t>* cancel) { return hy64db::gamesOf(db, type, id, cancel); });
    if (started && m_page != PageDatabase) setPage(PageDatabase);
    return started;
}

bool MainWindow::searchSimilar(bool pawnStructure)
{
    const chess::Position pos = m_session->current()->pos;
    const bool started = m_games->searchByReplay(pawnStructure ? tr("Même structure de pions") : tr("Même matériel"), [pos, pawnStructure](const hy64db::Database& db, const std::atomic<int32_t>* cancel) {
        return pawnStructure ? hy64db::searchPawnStructure(db, pos, cancel) : hy64db::searchSameMaterial(db, pos, cancel);
    });
    if (!started) {
        statusBar()->showMessage(tr("Cette recherche demande une base Hydra64 (Fichier → Convertir une base…)"), 8000);
        return false;
    }
    if (m_page != PageDatabase) setPage(PageDatabase);
    return true;
}

bool MainWindow::searchManeuver(const hy64db::ManeuverQuery& q)
{
    if (!q.valid()) return false;
    const QString label = tr("Manœuvre %1%2 → %3").arg(display::san(std::string(1, "PNBRQK"[int(q.piece) - int(chess::Pawn)])), QString::fromStdString(chess::squareName(q.from)), QString::fromStdString(chess::squareName(q.to)));
    const bool started = m_games->searchByReplay(label, [q](const hy64db::Database& db, const std::atomic<int32_t>* cancel) { return hy64db::searchManeuver(db, q, cancel); });
    if (!started) {
        statusBar()->showMessage(tr("Cette recherche demande une base Hydra64 (Fichier → Convertir une base…)"), 8000);
        return false;
    }
    if (m_page != PageDatabase) setPage(PageDatabase);
    return true;
}

void MainWindow::askManeuver()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Recherche de manœuvre"));
    auto* form = new QFormLayout(&dlg);
    auto* piece = new QComboBox;
    const chess::PieceType types[] = {chess::Knight, chess::Bishop, chess::Rook, chess::Queen, chess::King, chess::Pawn};
    const QString names[] = {tr("Cavalier"), tr("Fou"), tr("Tour"), tr("Dame"), tr("Roi"), tr("Pion")};
    for (int i = 0; i < 6; ++i) piece->addItem(names[i], int(types[i]));
    auto* color = new QComboBox;
    color->addItems({tr("Blancs"), tr("Noirs"), tr("Les deux camps")});
    color->setCurrentIndex(2);
    auto* from = new QLineEdit;
    auto* to = new QLineEdit;
    auto* via = new QLineEdit;
    for (QLineEdit* e : {from, to, via}) e->setMaxLength(2);
    from->setPlaceholderText(QStringLiteral("f3"));
    to->setPlaceholderText(QStringLiteral("f5"));
    via->setPlaceholderText(tr("facultatif"));
    auto* moves = new QSpinBox;
    moves->setRange(1, 12);
    moves->setValue(4);
    form->addRow(tr("Pièce"), piece);
    form->addRow(tr("Camp"), color);
    form->addRow(tr("De la case"), from);
    form->addRow(tr("À la case"), to);
    form->addRow(tr("En passant par"), via);
    form->addRow(tr("En combien de coups de la pièce, au plus"), moves);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    hy64db::ManeuverQuery q;
    q.piece = chess::PieceType(piece->currentData().toInt());
    q.color = color->currentIndex();
    q.from = chess::parseSquare(from->text().trimmed().toLower().toStdString());
    q.to = chess::parseSquare(to->text().trimmed().toLower().toStdString());
    q.via = via->text().trimmed().isEmpty() ? chess::NoSquare : chess::parseSquare(via->text().trimmed().toLower().toStdString());
    q.maxMoves = moves->value();
    if (!q.valid()) {
        QMessageBox::information(this, tr("Recherche de manœuvre"), tr("Indiquez deux cases différentes, par exemple f3 et f5."));
        return;
    }
    searchManeuver(q);
}

void MainWindow::searchBoardPosition()
{
    const chess::Position pos = m_session->current()->pos;
    if (!m_games->searchPosition(pos)) {
        statusBar()->showMessage(tr("La recherche de position demande une base Hydra64 avec index des positions (Fichier → Convertir une base…)"), 8000);
        return;
    }
    if (m_page != PageDatabase) setPage(PageDatabase);
}

void MainWindow::compactDatabase()
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("Le compactage concerne les bases Hydra64"), 6000);
        return;
    }
    const QString path = m_games->model()->filePath();
    {
        QMessageBox box(QMessageBox::Question, tr("Compacter la base"), tr("Compacter « %1 » ?").arg(QFileInfo(path).completeBaseName()), QMessageBox::NoButton, this);
        box.setInformativeText(tr("Les parties supprimées (rayées) seront retirées définitivement : elles ne pourront plus être restaurées."));
        QPushButton* compact = box.addButton(tr("Compacter"), QMessageBox::DestructiveRole);
        box.setDefaultButton(box.addButton(tr("Annuler"), QMessageBox::RejectRole));
        box.exec();
        if (box.clickedButton() != compact) return;
    }
    const bool ok = runWithProgress(tr("Compacter la base"), tr("Compactage de « %1 »…").arg(QFileInfo(path).completeBaseName()),
                                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                                        std::string err;
                                        const bool done = b->database().compact(&err, [&](uint64_t d, uint64_t t) {
                                            permille = t ? int(1000 * d / t) : 0;
                                            return !cancel.load();
                                        });
                                        status = QString::fromStdString(err);
                                        return done;
                                    },
                                    true);
    m_games->reload(path);
    updateDatabaseTree();
    statusBar()->showMessage(ok ? tr("Base compactée : %1 parties").arg(QLocale().toString(m_games->model()->totalGames())) : tr("Compactage interrompu"), 6000);
}

int MainWindow::improvePlayerNames(const std::function<std::string(const std::string&)>& canonical, bool ask)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("L'amélioration des noms concerne les bases Hydra64"), 6000);
        return -1;
    }
    if (!canonical && !playerEncyclopedia::get()) {
        QMessageBox::information(this, tr("Améliorer les noms des joueurs"),
                                 tr("Il faut l'encyclopédie des joueurs de ChessBase (« Playbase ») : indiquez son dossier dans Préférences → Bases."));
        return -1;
    }
    const auto spell = canonical ? canonical : std::function<std::string(const std::string&)>(namefix::encyclopediaName);
    std::vector<namefix::Proposal> found;
    bool cancelled = false;
    runWithProgress(tr("Améliorer les noms des joueurs"), tr("Recherche des joueurs dans l'encyclopédie…"),
                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString&) {
                        found = namefix::proposals(b->database(), spell, [&](uint64_t d, uint64_t t) {
                            permille = t ? int(1000 * d / t) : 0;
                            if (cancel.load()) cancelled = true;
                            return !cancelled;
                        });
                        return !cancelled;
                    },
                    true);
    if (cancelled) return -1;
    if (found.empty()) {
        statusBar()->showMessage(tr("Aucun nom à améliorer : ceux que l'encyclopédie reconnaît sont déjà écrits comme elle"), 8000);
        return 0;
    }
    std::vector<std::pair<uint32_t, std::string>> changes;
    if (ask) {
        NameFixDialog dlg(found, this);
        if (dlg.exec() != QDialog::Accepted) return -1;
        changes = dlg.chosen();
    } else {
        for (const namefix::Proposal& p : found)
            if (p.sure) changes.push_back({p.id, p.to});
    }
    if (changes.empty()) return 0;
    int64_t games = -1;
    runWithProgress(tr("Améliorer les noms des joueurs"), tr("Écriture de %1 noms…").arg(changes.size()),
                    [&](std::atomic<int>& permille, std::atomic<bool>&, QString& status) {
                        std::string err;
                        games = b->database().renameNames(hy64db::Player, changes, &err, [&](uint64_t d, uint64_t t) {
                            permille = t ? int(1000 * d / t) : 0;
                            return true;
                        });
                        status = QString::fromStdString(err);
                        return games >= 0;
                    },
                    false);
    GameListModel* model = m_games->model();
    emit model->dataChanged(model->index(0, 0), model->index(std::max(0, model->rowCount() - 1), GameListModel::ColumnCount - 1));
    if (games < 0) return -1;
    statusBar()->showMessage(tr("%1 noms améliorés dans %2 parties").arg(changes.size()).arg(QLocale().toString(qlonglong(games))), 8000);
    return int(changes.size());
}

// Saved layouts: QSettings group "savedLayouts/<page>", one entry per name (the state of the panels).
static QString layoutGroup(int page) { return QStringLiteral("savedLayouts/%1").arg(page); }

QStringList MainWindow::savedLayouts() const
{
    QSettings s;
    s.beginGroup(layoutGroup(m_page));
    QStringList names;
    for (const QString& key : s.childKeys()) names << QString::fromUtf8(QByteArray::fromPercentEncoding(key.toLatin1()));
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool MainWindow::saveLayoutAs(const QString& name)
{
    if (name.trimmed().isEmpty()) return false;
    flushFloatingContainers();
    // (the key is percent-encoded: a name may hold a slash, which QSettings takes for a group)
    QSettings().setValue(layoutGroup(m_page) + QLatin1Char('/') + QString::fromLatin1(name.trimmed().toUtf8().toPercentEncoding()), m_dockManager->saveState(LayoutVersion));
    return true;
}

bool MainWindow::applySavedLayout(const QString& name)
{
    const QByteArray state = QSettings().value(layoutGroup(m_page) + QLatin1Char('/') + QString::fromLatin1(name.toUtf8().toPercentEncoding())).toByteArray();
    if (state.isEmpty()) return false;
    // The page is shown again from that state: what it then arranges (board, spectators…) is the same as on arrival.
    m_pageStates[m_page] = state;
    m_keepPageState = true;
    setPage(m_page);
    m_keepPageState = false;
    statusBar()->showMessage(tr("Disposition « %1 »").arg(name), 4000);
    return true;
}

void MainWindow::removeSavedLayout(const QString& name)
{
    QSettings().remove(layoutGroup(m_page) + QLatin1Char('/') + QString::fromLatin1(name.toUtf8().toPercentEncoding()));
}

PieceHeatDialog* MainWindow::pieceHeat()
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("Les pièces sur les parties affichées concernent les bases Hydra64 (Fichier → Convertir… pour un PGN)"), 6000);
        return nullptr;
    }
    std::vector<uint32_t> ids;
    ids.reserve(size_t(b->count()));
    for (int row = 0; row < b->count(); ++row) ids.push_back(b->idOf(row));
    hy64db::PieceHeat heat;
    bool cancelled = false;
    const QString name = QFileInfo(m_games->model()->filePath()).completeBaseName();
    runWithProgress(tr("Pièces sur les parties affichées"), tr("Lecture de %1 parties de « %2 »…").arg(QLocale().toString(qulonglong(ids.size())), name),
                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString&) {
                        heat = hy64db::pieceHeat(b->database(), ids, [&](uint64_t d, uint64_t t) {
                            permille = t ? int(1000 * d / t) : 0;
                            return !cancel.load();
                        }, &cancelled);
                        return !cancelled;
                    },
                    true);
    if (cancelled) return nullptr;
    auto* d = new PieceHeatDialog(heat, b->isFiltered() ? tr("%1 (%2 parties)").arg(name, QLocale().toString(heat.games)) : name, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    return d;
}

QString MainWindow::checkDatabaseIntegrity(bool show)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("Le test d'intégrité concerne les bases Hydra64"), 6000);
        return {};
    }
    const QString name = QFileInfo(m_games->model()->filePath()).completeBaseName();
    hy64db::IntegrityReport report;
    const bool finished = runWithProgress(tr("Test d'intégrité"), tr("Relecture de toutes les parties de « %1 »…").arg(name),
                                          [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString&) {
                                              report = b->database().checkIntegrity([&](uint64_t d, uint64_t t) {
                                                  permille = t ? int(1000 * d / t) : 0;
                                                  return !cancel.load();
                                              });
                                              return !report.cancelled;
                                          },
                                          true);
    if (!finished || report.cancelled) {
        statusBar()->showMessage(tr("Test d'intégrité interrompu"), 6000);
        return {};
    }
    const QLocale loc;
    QString text = tr("%1 parties relues dans « %2 »").arg(loc.toString(report.games), name);
    if (report.deleted) text += tr(", dont %1 supprimées").arg(loc.toString(report.deleted));
    QString details;
    if (report.ok()) {
        text += tr(" : aucun problème.");
    } else {
        text += tr(" : %1 problème(s).").arg(loc.toString(report.problems()));
        QStringList kinds;
        if (report.unreadable) kinds << tr("%1 parties illisibles").arg(loc.toString(report.unreadable));
        if (report.badLocation) kinds << tr("%1 blocs hors du fichier des parties").arg(loc.toString(report.badLocation));
        if (report.badNames) kinds << tr("%1 parties avec un nom inconnu").arg(loc.toString(report.badNames));
        if (report.badLength) kinds << tr("%1 longueurs différentes de l'index").arg(loc.toString(report.badLength));
        details = kinds.join(QStringLiteral(", ")) + QStringLiteral(".\n\n");
        for (const auto& p : report.first) details += tr("Partie n° %1 : %2").arg(p.id + 1).arg(QString::fromStdString(p.what)) + QLatin1Char('\n');
        if (report.problems() > report.first.size()) details += tr("… (les %1 premières seulement)").arg(report.first.size()) + QLatin1Char('\n');
        details += QLatin1Char('\n') + tr("Les parties lisibles se sauvent en exportant la base (Base → Exporter les parties affichées…) ou en la convertissant vers une nouvelle base.");
    }
    if (show) {
        QMessageBox box(report.ok() ? QMessageBox::Information : QMessageBox::Warning, tr("Test d'intégrité"), text, QMessageBox::Ok, this);
        if (!details.isEmpty()) {
            box.setInformativeText(details.section(QStringLiteral("\n\n"), 0, 0));
            box.setDetailedText(details);
        }
        box.exec();
    }
    return details.isEmpty() ? text : text + QLatin1Char('\n') + details;
}

QList<MainWindow::Bookmark> MainWindow::bookmarks()
{
    QList<Bookmark> out;
    for (const QJsonValue& v : QJsonDocument::fromJson(QSettings().value("bookmarks").toByteArray()).array()) {
        const QJsonObject o = v.toObject();
        Bookmark b;
        b.name = o.value("name").toString();
        b.path = o.value("path").toString();
        b.number = o.value("number").toInt();
        for (const QJsonValue& p : o.value("ply").toArray()) b.ply.push_back(p.toInt());
        out << b;
    }
    return out;
}

static void saveBookmarks(const QList<MainWindow::Bookmark>& list)
{
    QJsonArray a;
    for (const MainWindow::Bookmark& b : list) {
        QJsonArray ply;
        for (int i : b.ply) ply.append(i);
        a.append(QJsonObject{{"name", b.name}, {"path", b.path}, {"number", b.number}, {"ply", ply}});
    }
    QSettings().setValue("bookmarks", QJsonDocument(a).toJson(QJsonDocument::Compact));
}

bool MainWindow::addBookmark()
{
    const GameOrigin o = m_session->origin();
    GameListModel* model = o.isValid() ? m_games->modelFor(o.path) : nullptr;
    if (!model) {
        statusBar()->showMessage(tr("Signet : la partie affichée doit venir d'une base ouverte"), 5000);
        return false;
    }
    Bookmark b;
    b.path = QFileInfo(o.path).absoluteFilePath();
    b.number = model->numberOf(o.row);
    b.ply = GameSession::pathTo(m_session->current());
    const Game& g = m_session->game();
    const Node* cur = m_session->current();
    QString name = QStringLiteral("%1 – %2").arg(QString::fromStdString(g.header("White")), QString::fromStdString(g.header("Black")));
    if (cur != g.root()) name += tr(", coup %1").arg(cur->parent->pos.fullmoveNumber());
    name += QStringLiteral("  (%1 n° %2)").arg(QFileInfo(o.path).completeBaseName()).arg(b.number);
    b.name = name;
    QList<Bookmark> list = bookmarks();
    list.prepend(b);
    while (list.size() > 50) list.removeLast();
    saveBookmarks(list);
    statusBar()->showMessage(tr("Signet ajouté : %1").arg(name), 5000);
    return true;
}

bool MainWindow::openBookmark(int index)
{
    const QList<Bookmark> list = bookmarks();
    if (index < 0 || index >= list.size()) return false;
    const Bookmark& b = list[index];
    if (!QFileInfo::exists(b.path) || !openDatabase(b.path)) {
        statusBar()->showMessage(tr("Signet : base introuvable (%1)").arg(QDir::toNativeSeparators(b.path)), 6000);
        return false;
    }
    m_games->activate(b.path);
    const int row = m_games->rowOfNumber(b.number);
    if (row < 0) {
        statusBar()->showMessage(tr("Signet : partie n° %1 introuvable").arg(b.number), 6000);
        return false;
    }
    m_games->selectGame(row);
    loadGameFromDatabase(row);
    Node* n = m_session->game().root();
    for (int i : b.ply)
        if (i >= 0 && i < int(n->children.size())) n = n->children[size_t(i)].get();
    m_session->goTo(n);
    return true;
}

void MainWindow::removeBookmark(int index)
{
    QList<Bookmark> list = bookmarks();
    if (index < 0 || index >= list.size()) return;
    list.removeAt(index);
    saveBookmarks(list);
}

int MainWindow::handleTwins(const hy64db::TwinCriteria& c, bool show, bool mark)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("Les doublons se cherchent dans une base Hydra64 (Fichier → Convertir… pour un PGN)"), 6000);
        return -1;
    }
    std::vector<std::vector<uint32_t>> groups;
    const bool ok = runWithProgress(tr("Doublons"), tr("Recherche des doublons…"),
                                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString&) {
                                        groups = b->database().findTwins(c, [&](uint64_t d, uint64_t t) {
                                            permille = t ? int(1000 * d / t) : 0;
                                            return !cancel.load();
                                        });
                                        return !cancel.load();
                                    },
                                    true);
    if (!ok) return -1;
    std::vector<uint32_t> all, extra;
    for (const auto& g : groups) {
        all.insert(all.end(), g.begin(), g.end());
        extra.insert(extra.end(), g.begin() + 1, g.end()); // the first of each group is kept
    }
    if (mark) {
        QString error;
        GameListModel* model = m_games->model();
        if (!b->setDeletedIds(extra, true, &error)) QMessageBox::warning(this, tr("Doublons"), error);
        emit model->dataChanged(model->index(0, 0), model->index(std::max(0, model->rowCount() - 1), GameListModel::ColumnCount - 1));
    }
    if (show && !all.empty()) m_games->model()->setIndexedView(Hy64Source::viewOf(all), true);
    m_games->refreshCount();
    statusBar()->showMessage(tr("Doublons : %n groupe(s)", nullptr, int(groups.size())) + tr(", %n partie(s) en trop", nullptr, int(extra.size())) +
                                 (mark ? tr(" — marquées supprimées (rayées) ; « Compacter la base » les retire") : QString()),
                             10000);
    return int(extra.size());
}

void MainWindow::twinsDialog()
{
    if (!dynamic_cast<Hy64Source*>(m_games->model()->indexed())) {
        statusBar()->showMessage(tr("Les doublons se cherchent dans une base Hydra64 (Fichier → Convertir… pour un PGN)"), 6000);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Doublons"));
    auto* note = new QLabel(tr("Deux parties sont des doublons si elles ont les mêmes joueurs (mêmes couleurs), la même longueur et les critères "
                               "cochés. Dans chaque groupe, la partie la plus riche (commentaires, variantes) est gardée."));
    note->setWordWrap(true);
    note->setObjectName("Faint");
    auto* moves = new QCheckBox(tr("Mêmes coups"));
    auto* result = new QCheckBox(tr("Même résultat"));
    auto* year = new QCheckBox(tr("Même année"));
    auto* event = new QCheckBox(tr("Même tournoi"));
    auto* round = new QCheckBox(tr("Même ronde"));
    QSettings st;
    moves->setChecked(st.value("twins/moves", true).toBool());
    result->setChecked(st.value("twins/result", true).toBool());
    year->setChecked(st.value("twins/year", true).toBool());
    event->setChecked(st.value("twins/event", false).toBool());
    round->setChecked(st.value("twins/round", false).toBool());
    auto* action = new QComboBox;
    action->addItem(tr("Afficher les doublons (groupes l'un après l'autre)"), 0);
    action->addItem(tr("Marquer les doublons comme supprimés (rayés, restaurables)"), 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Chercher"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(note);
    for (QCheckBox* c : {moves, result, year, event, round}) layout->addWidget(c);
    layout->addWidget(action);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    st.setValue("twins/moves", moves->isChecked());
    st.setValue("twins/result", result->isChecked());
    st.setValue("twins/year", year->isChecked());
    st.setValue("twins/event", event->isChecked());
    st.setValue("twins/round", round->isChecked());
    hy64db::TwinCriteria c;
    c.sameMoves = moves->isChecked();
    c.sameResult = result->isChecked();
    c.sameYear = year->isChecked();
    c.sameEvent = event->isChecked();
    c.sameRound = round->isChecked();
    const bool mark = action->currentData().toInt() == 1;
    handleTwins(c, !mark, mark);
}

int64_t MainWindow::renameInDatabase(hy64db::NameType type, const QString& from, const QString& to)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) return -1;
    const uint32_t id = b->database().names().find(type, from.toStdString());
    if (id == hy64db::NoName) return -1;
    int64_t changed = -1;
    QString failure;
    runWithProgress(tr("Corriger un nom"), tr("Remplacement de « %1 »…").arg(from),
                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                        std::string err;
                        changed = b->database().renameName(type, id, to.toStdString(), &err, [&](uint64_t d, uint64_t t) {
                            permille = t ? int(1000 * d / t) : 0;
                            return !cancel.load();
                        });
                        status = QString::fromStdString(err);
                        return changed >= 0;
                    },
                    true);
    GameListModel* model = m_games->model();
    emit model->dataChanged(model->index(0, 0), model->index(std::max(0, model->rowCount() - 1), GameListModel::ColumnCount - 1));
    if (changed >= 0) statusBar()->showMessage(tr("« %1 » → « %2 » : %n partie(s) modifiée(s)", nullptr, int(changed)).arg(from, to), 8000);
    return changed;
}

void MainWindow::showGameIds(const QString& path, const std::vector<uint32_t>& ids, const QString& title)
{
    if (!m_games->activate(path) || !m_games->model()->isIndexed()) return;
    m_games->model()->setIndexedView(Hy64Source::viewOf(ids), true);
    m_games->refreshCount();
    if (m_page != PageDatabase) setPage(PageDatabase);
    statusBar()->showMessage(tr("%1 : %n partie(s)", nullptr, int(ids.size())).arg(title), 6000);
}

int MainWindow::exportVisibleGames(ExportFormat format, const QString& path, int plies)
{
    GameListModel* model = m_games->model();
    if (m_games->currentIndex() < 0) return -1;
    const QList<int> rows = m_games->visibleRows();
    // Hydra64 database → PGN: export in parallel, off the interface thread (millions of games).
    if (auto* b = dynamic_cast<Hy64Source*>(model->indexed()); b && format == ExportFormat::Pgn) {
        struct Rows : hy64db::GameSource {
            const hy64db::Database& db;
            std::vector<uint32_t> ids;
            explicit Rows(const hy64db::Database& d) : db(d) {}
            uint64_t count() const override { return ids.size(); }
            bool read(uint64_t i, chess::Game& g, std::string* error) const override { return db.game(ids[size_t(i)], g, error); }
        } source(b->database());
        source.ids.reserve(size_t(rows.size()));
        for (int row : rows) source.ids.push_back(b->idOf(row));
        hy64db::ConvertResult result;
        const bool ok = runWithProgress(tr("Export"), tr("Export des parties…"),
                                        [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                                            std::string err;
                                            const bool done = hy64db::exportPgn(source, path.toStdString(), &result, &err, [&](hy64db::ConvertStep, uint64_t d, uint64_t t) {
                                                permille = t ? int(1000 * d / t) : 0;
                                                return !cancel.load();
                                            });
                                            status = QString::fromStdString(err);
                                            return done;
                                        },
                                        true);
        if (!ok) return -1;
        statusBar()->showMessage(tr("%n partie(s) exportée(s) dans %1", nullptr, int(result.games)).arg(QDir::toNativeSeparators(path)), 8000);
        return int(result.games);
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return -1;
    const QString base = QFileInfo(model->filePath()).completeBaseName();
    if (format == ExportFormat::Html)
        f.write(QStringLiteral("<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><title>%1</title><style>"
                               "body{font-family:system-ui,sans-serif;max-width:860px;margin:2em auto;padding:0 16px;background:#18181b;color:#e8e6e0}"
                               "h2{font-size:1.05em;margin:1.6em 0 .2em;color:#e68a35}.h{color:#9a9aa2;font-size:.9em}"
                               ".m{line-height:1.6}.c{color:#8fb38f}.v{color:#9a9aa2}</style></head><body><h1>%1</h1>\n")
                    .arg(base.toHtmlEscaped())
                    .toUtf8());
    int written = 0;
    QProgressDialog progress(tr("Export des parties…"), tr("Annuler"), 0, int(rows.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    for (int k = 0; k < rows.size(); ++k) {
        if (k % 64 == 0) {
            progress.setValue(k);
            if (progress.wasCanceled()) break;
        }
        Game g;
        if (!model->parseGame(rows[k], g, nullptr)) continue;
        if (format == ExportFormat::Pgn) {
            f.write((QString::fromStdString(pgn::writeGame(g)).trimmed() + QStringLiteral("\n\n")).toUtf8());
        } else if (format == ExportFormat::Epd) {
            const Node* n = g.root();
            for (int i = 0; n->mainChild() && (plies <= 0 || i < plies); ++i) n = n->mainChild();
            std::string fen = n->pos.fen();
            for (int cut = 0; cut < 2; ++cut) fen = fen.substr(0, fen.find_last_of(' ')); // EPD: without the counters
            const QString id = QStringLiteral("%1 - %2, %3 %4").arg(QString::fromStdString(g.header("White")), QString::fromStdString(g.header("Black")),
                                                                     QString::fromStdString(g.header("Event")), QString::fromStdString(g.header("Date")).left(4));
            QString line = QString::fromStdString(fen) + QStringLiteral(" id \"%1\";").arg(QString(id).replace('"', '\''));
            const std::string result = g.header("Result");
            if (!result.empty() && result != "*") line += QStringLiteral(" c0 \"%1\";").arg(QString::fromStdString(result));
            f.write((line + QLatin1Char('\n')).toUtf8());
        } else {
            // HTML: header of the game, then the moves (comments and variations)
            QString moves;
            std::function<void(const Node*, bool)> line = [&](const Node* n, bool forceNumber) {
                bool number = forceNumber;
                while (n) {
                    const Node* parent = n->parent;
                    if (!n->preComment.empty()) { // pre-move comment
                        moves += QStringLiteral("<span class=c>%1</span> ").arg(QString::fromStdString(n->preComment).toHtmlEscaped());
                        number = true;
                    }
                    if (parent->pos.sideToMove() == White) moves += QStringLiteral("%1. ").arg(parent->pos.fullmoveNumber());
                    else if (number) moves += QStringLiteral("%1… ").arg(parent->pos.fullmoveNumber());
                    number = false;
                    moves += QString::fromStdString(parent->pos.toSan(n->move)).toHtmlEscaped() + QLatin1Char(' ');
                    if (!n->comment.empty()) {
                        moves += QStringLiteral("<span class=c>%1</span> ").arg(QString::fromStdString(n->comment).toHtmlEscaped());
                        number = true;
                    }
                    for (size_t v = 1; v < parent->children.size() && parent->mainChild() == n; ++v) {
                        moves += QStringLiteral("<span class=v>(");
                        line(parent->children[v].get(), true);
                        moves += QStringLiteral(")</span> ");
                        number = true;
                    }
                    n = n->mainChild();
                }
            };
            line(g.root()->mainChild(), false);
            auto h = [&](const char* k) { return QString::fromStdString(g.header(k)).toHtmlEscaped(); };
            f.write(QStringLiteral("<h2>%1 – %2 &nbsp; %3</h2><div class=h>%4 · %5 · %6</div><p class=m>%7</p>\n")
                        .arg(h("White"), h("Black"), h("Result"), h("Event"), h("Site"), h("Date"), moves)
                        .toUtf8());
        }
        ++written;
    }
    if (format == ExportFormat::Html) f.write("</body></html>\n");
    progress.close();
    if (!f.commit()) return -1;
    statusBar()->showMessage(tr("%n partie(s) exportée(s) dans %1", nullptr, written).arg(QDir::toNativeSeparators(path)), 8000);
    return written;
}

QImage MainWindow::boardImage() const
{
    QWidget* board = m_viewer->board();
    const qreal dpr = board->devicePixelRatioF();
    QPixmap pm(board->size() * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    board->render(&pm);
    return pm.toImage();
}

bool MainWindow::saveBoardImage(const QString& path, QString* error) const
{
    const QImage img = boardImage();
    if (img.isNull() || !img.save(path)) {
        if (error) *error = tr("Impossible d'enregistrer l'image : %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

FilterGraphDialog* MainWindow::filterGraph()
{
    if (m_games->currentIndex() < 0) {
        statusBar()->showMessage(tr("Ouvrez d'abord une base"), 4000);
        return nullptr;
    }
    const QList<int> rows = m_games->visibleRows();
    GameListModel* model = m_games->model();
    std::vector<IndexedGameSource::Summary> games;
    games.reserve(size_t(rows.size()));
    for (int row : rows) {
        IndexedGameSource::Summary s;
        if (model->summary(row, s)) games.push_back(s);
    }
    auto* d = new FilterGraphDialog(games, model->totalGames(), this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(tr("Graphiques du filtre — %1").arg(QFileInfo(model->filePath()).fileName()));
    return d;
}

OpeningReportDialog* MainWindow::openingReport(int maxSample)
{
    if (m_games->currentIndex() < 0) {
        statusBar()->showMessage(tr("Ouvrez d'abord une base"), 4000);
        return nullptr;
    }
    GameListModel* model = m_games->model();
    const Position pos = m_session->current()->pos;
    // Starting view (popularity by period)
    std::vector<IndexedGameSource::Summary> baseline;
    const QList<int> before = m_games->visibleRows();
    baseline.reserve(size_t(before.size()));
    for (int row : before) {
        IndexedGameSource::Summary s;
        if (model->summary(row, s)) baseline.push_back(s);
    }
    QProgressDialog progress(tr("Rapport d'ouverture…"), tr("Annuler"), 0, 1000, this);
    progress.setWindowTitle(tr("Rapport d'ouverture"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QList<int> rows;
    if (m_games->searchPosition(pos)) { // position index (Hydra64 database)
        progress.setLabelText(tr("Recherche de la position…"));
        m_games->waitForSearch();
        rows = m_games->visibleRows();
    } else { // without an index: the games of the view are replayed
        const uint64_t key = polyglot::key(pos);
        int k = 0;
        for (int row : before) {
            if ((++k & 255) == 0) {
                progress.setValue(int(int64_t(k) * 500 / std::max<qsizetype>(1, before.size())));
                QCoreApplication::processEvents();
                if (progress.wasCanceled()) return nullptr;
            }
            Game g;
            if (!model->parseGame(row, g, nullptr)) continue;
            bool hit = polyglot::key(g.root()->pos) == key;
            for (const Node* n : g.mainline()) {
                if (hit) break;
                hit = polyglot::key(n->pos) == key;
            }
            if (hit) rows << row;
        }
    }
    progress.setLabelText(tr("Statistiques de %n partie(s)…", nullptr, int(rows.size())));
    bool cancelled = false;
    OpeningReport report = OpeningReport::build(pos, *model, rows, baseline, maxSample, [&](int done, int total) {
        progress.setValue(500 + int(int64_t(done) * 500 / std::max(1, total)));
        QCoreApplication::processEvents();
        cancelled = progress.wasCanceled();
        return !cancelled;
    });
    progress.close();
    if (cancelled) return nullptr;
    const QString path = model->filePath();
    auto* d = new OpeningReportDialog(std::move(report), QFileInfo(path).fileName(), this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &OpeningReportDialog::gameRequested, this, [this, path](int row) {
        if (!m_games->activate(path)) return;
        m_games->selectGame(row);
        loadGameFromDatabase(row);
    });
    return d;
}

EcoBrowser* MainWindow::ecoBrowser()
{
    std::vector<int> counts;
    if (m_games->currentIndex() >= 0) {
        counts.assign(501, 0);
        GameListModel* model = m_games->model();
        const int n = model->isIndexed() ? model->indexed()->count() : model->rowCount();
        for (int row = 0; row < n; ++row) { // the whole view (search result included)
            IndexedGameSource::Summary s;
            if (model->summary(row, s) && s.eco > 0 && s.eco <= 500) ++counts[s.eco];
        }
    }
    auto* d = new EcoBrowser(std::move(counts), this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &EcoBrowser::filterRequested, this, [this](int from, int to) {
        if (m_games->currentIndex() < 0) return;
        hy64db::HeaderQuery q;
        q.ecoFrom = uint16_t(from);
        q.ecoTo = uint16_t(to);
        setPage(PageDatabase);
        m_games->searchHeaders(q, IndexedGameSource::Combine::Replace);
    });
    connect(d, &EcoBrowser::lineRequested, this, [this](const QString& moves) {
        Game g;
        if (!pgn::parseGame(moves.toStdString() + " *", g)) return;
        g.setCurrent(g.mainline().empty() ? g.root() : g.mainline().back());
        analyseGame(std::move(g));
    });
    return d;
}

SurveyBrowser* MainWindow::surveyBrowser()
{
    if (m_games->currentIndex() < 0) return nullptr;
    const QString index = surveyindex::find(m_games->model()->filePath());
    if (index.isEmpty()) return nullptr;
    QString error;
    QList<surveyindex::Survey> surveys = surveyindex::load(index, &error);
    if (surveys.isEmpty()) {
        if (!error.isEmpty()) statusBar()->showMessage(tr("Index des synthèses illisible : %1").arg(error), 8000);
        return nullptr;
    }
    QString name = QFileInfo(index).fileName();
    name.chop(int(qstrlen(".surveys.json")));
    auto* d = new SurveyBrowser(std::move(surveys), name, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    // The list stays open: several surveys can be opened one after the other.
    connect(d, &SurveyBrowser::openRequested, this, [this](const QString& path) {
        if (openDatabase(path)) setPage(PageDatabase);
    });
    return d;
}

std::unique_ptr<AddExerciseDialog> MainWindow::addExerciseDialog()
{
    const Node* cur = m_session->current();
    std::vector<Move> following;
    for (const Node* n = cur->mainChild(); n; n = n->mainChild()) following.push_back(n->move);
    // Proposed name: "White – Black year, move N" if the game has players
    const Game& g = m_session->game();
    const QString white = QString::fromStdString(g.header("White")), black = QString::fromStdString(g.header("Black"));
    QString name;
    if (!white.isEmpty() && white != QLatin1String("?") && !black.isEmpty() && black != QLatin1String("?")) {
        name = white + QStringLiteral(" – ") + black;
        const QString year = QString::fromStdString(g.header("Date")).left(4);
        if (year.size() == 4 && year.front().isDigit()) name += QLatin1Char(' ') + year;
        name += tr(", coup %1").arg(cur->pos.fullmoveNumber());
    }
    return std::make_unique<AddExerciseDialog>(cur->pos, std::move(following), name, this);
}

bool MainWindow::addToCollection()
{
    auto d = addExerciseDialog();
    if (!d->hasSolution()) {
        QMessageBox::information(this, tr("Ajouter au recueil"),
                                 tr("Aucun coup suivant dans la notation : jouez d'abord la solution sur l'échiquier, puis revenez d'un coup pour ajouter la position."));
        return false;
    }
    if (d->exec() != QDialog::Accepted) return false;
    return addExercise(d->collectionPath(), d->exercise());
}

bool MainWindow::addExercise(const QString& path, const exercise::Exercise& ex)
{
    QString error;
    GameListModel* open = m_games->modelFor(path);
    if (!exercise::append(path, ex, open, &error)) {
        QMessageBox::warning(this, tr("Ajouter au recueil"), tr("Impossible d'ajouter l'exercice à %1 :\n%2").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    if (open && open == m_games->model()) m_games->reload(path); // number of games up to date
    statusBar()->showMessage(tr("Exercice ajouté à %1").arg(QFileInfo(path).fileName()), 5000);
    return true;
}

CollectionCheckDialog* MainWindow::collectionCheckDialog()
{
    if (!m_collectionCheck) {
        m_collectionCheck = new CollectionCheckDialog(this);
        connect(m_collectionCheck, &CollectionCheckDialog::analyseRequested, this, &MainWindow::analysePosition);
    }
    return m_collectionCheck;
}

TrainingDialog* MainWindow::trainingDialog()
{
    if (!m_training) {
        TrainingDialog::Book book;
        book.known = [this](const Position& p) { return m_book->movesAt(p); };
        book.pick = [this](const Position& p) { return m_book->pickMove(p); };
        book.name = [this] { return m_book->bookName(); };
        m_training = new TrainingDialog(book, this);
        connect(m_training, &TrainingDialog::analyseRequested, this, &MainWindow::analysePosition);
        connect(m_training, &TrainingDialog::checkRequested, this, [this](const QString& path) {
            CollectionCheckDialog* d = collectionCheckDialog();
            if (!d->isRunning()) d->setCollection(path);
            d->show();
            d->raise();
            d->activateWindow();
        });
    }
    return m_training;
}

bool MainWindow::finishGame(int moveTimeMs, int maxPlies)
{
    EngineConfig cfg = m_engine->engineConfig();
    if (!cfg.isValid()) cfg = EngineRegistry::instance().fallbackEngine();
    QString error;
    if (!m_autoPlayer->start(cfg, moveTimeMs, maxPlies, &error)) {
        QMessageBox::warning(this, tr("Finir la partie"), error);
        return false;
    }
    statusBar()->showMessage(tr("Le moteur finit la partie… (Partie → Finir la partie pour arrêter)"));
    return true;
}

GamePreviewDialog* MainWindow::previewGame(int row)
{
    GameListModel* model = m_games->model();
    Game g;
    if (row < 0 || row >= model->rowCount() || !model->parseGame(row, g, nullptr)) return nullptr;
    const QString title = QStringLiteral("%1 – %2").arg(QString::fromStdString(g.header("White")), QString::fromStdString(g.header("Black")));
    auto* dlg = new GamePreviewDialog(std::move(g), title, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    const QString path = model->filePath();
    connect(dlg, &GamePreviewDialog::openRequested, this, [this, path, row] {
        if (!m_games->activate(path)) return;
        m_games->selectGame(row);
        loadGameFromDatabase(row);
    });
    dlg->show();
    return dlg;
}

void MainWindow::exportVisibleDialog()
{
    if (m_games->currentIndex() < 0) {
        statusBar()->showMessage(tr("Ouvrez d'abord une base"), 4000);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Exporter les parties affichées"));
    auto* format = new QComboBox;
    format->addItem(tr("PGN"), int(ExportFormat::Pgn));
    format->addItem(tr("EPD (une position par partie)"), int(ExportFormat::Epd));
    format->addItem(tr("Page HTML"), int(ExportFormat::Html));
    auto* plies = new QSpinBox;
    plies->setRange(0, 600);
    plies->setSpecialValueText(tr("position finale"));
    plies->setPrefix(tr("après "));
    plies->setSuffix(tr(" demi-coups"));
    auto* note = new QLabel(tr("%n partie(s) affichée(s) (la recherche en cours s'applique).", nullptr, int(m_games->visibleRows().size())));
    note->setObjectName("Faint");
    auto* form = new QFormLayout;
    form->addRow(tr("Format"), format);
    form->addRow(tr("Position (EPD)"), plies);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Exporter…"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(format, &QComboBox::currentIndexChanged, &dlg, [format, plies] { plies->setEnabled(format->currentData().toInt() == int(ExportFormat::Epd)); });
    plies->setEnabled(false);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto f = ExportFormat(format->currentData().toInt());
    static const char* exts[] = {".pgn", ".epd", ".html"};
    const QString ext = QLatin1String(exts[int(f)]);
    QString path = QFileDialog::getSaveFileName(this, tr("Exporter les parties"),
                                                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') +
                                                    QFileInfo(m_games->model()->filePath()).completeBaseName() + ext);
    if (path.isEmpty()) return;
    if (!path.endsWith(ext, Qt::CaseInsensitive)) path += ext;
    if (exportVisibleGames(f, path, plies->value()) < 0) QMessageBox::warning(this, tr("Exporter les parties"), tr("Écriture impossible : %1").arg(path));
}

int MainWindow::rateBeauty(bool confirm)
{
    GameListModel* model = m_games->model();
    if (m_games->currentIndex() < 0) return -1;
    auto* b = dynamic_cast<Hy64Source*>(model->indexed());
    const QString name = QFileInfo(model->filePath()).completeBaseName();
    if (!b || model->isReadOnly()) {
        if (confirm) QMessageBox::information(this, tr("Beauté des parties"), tr("La beauté se calcule dans une base Hydra64 modifiable.\nConvertissez d'abord cette base (Fichier → Convertir…)."));
        return -1;
    }
    if (confirm && QMessageBox::question(this, tr("Beauté des parties"),
                                         tr("Évaluer la beauté de toutes les parties de « %1 » d'après leurs coups ?\n\n"
                                            "Chaque partie reçoit de zéro à trois étoiles (sacrifices, mat, chasse au roi, sous-promotion), "
                                            "affichées dans la colonne Beauté. Les parties elles-mêmes ne sont pas modifiées.")
                                             .arg(name))
                       != QMessageBox::Yes)
        return -1;
    int64_t changed = -1;
    runWithProgress(tr("Beauté des parties"), tr("Évaluation de « %1 »…").arg(name),
                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                        std::string err;
                        changed = b->database().rateBeauty(&err, [&](uint64_t d, uint64_t t) {
                            permille = t ? int(1000 * d / t) : 0;
                            return !cancel.load();
                        }, true);
                        status = QString::fromStdString(err);
                        return changed >= 0;
                    },
                    true);
    emit model->dataChanged(model->index(0, 0), model->index(std::max(0, model->rowCount() - 1), GameListModel::ColumnCount - 1));
    if (changed >= 0) statusBar()->showMessage(tr("Beauté : %n partie(s) mise(s) à jour — cliquez sur l'en-tête Beauté pour voir les plus belles", nullptr, int(changed)), 10000);
    return int(changed);
}

int MainWindow::classifyDatabase(bool confirm)
{
    GameListModel* model = m_games->model();
    if (m_games->currentIndex() < 0 || model->isReadOnly()) return -1;
    const QString path = model->filePath();
    if (confirm && QMessageBox::question(this, tr("Classer les parties"),
                                         tr("Recalculer le code ECO de toutes les parties de « %1 » d'après leurs coups ?").arg(QFileInfo(path).completeBaseName()))
                       != QMessageBox::Yes)
        return -1;
    const chess::eco::Book& book = openingsBook();
    int64_t changed = -1;
    if (auto* b = dynamic_cast<Hy64Source*>(model->indexed())) {
        runWithProgress(tr("Classer les parties"), tr("Classement ECO de « %1 »…").arg(QFileInfo(path).completeBaseName()),
                        [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                            std::string err;
                            changed = b->database().classifyEco(book, &err, [&](uint64_t d, uint64_t t) {
                                permille = t ? int(1000 * d / t) : 0;
                                return !cancel.load();
                            });
                            status = QString::fromStdString(err);
                            return changed >= 0;
                        },
                        true);
        emit model->dataChanged(model->index(0, 0), model->index(std::max(0, model->rowCount() - 1), GameListModel::ColumnCount - 1));
    } else {
        // PGN: ECO and Opening headers rewritten (file rewritten once)
        QString error;
        changed = model->rewriteGames(
            [&](Game& g) {
                const chess::eco::Opening* o = book.classify(g);
                if (!o || (g.header("ECO") == o->eco && g.header("Opening") == o->name)) return false;
                g.setHeader("ECO", o->eco);
                g.setHeader("Opening", o->name);
                return true;
            },
            &error);
        if (changed < 0) QMessageBox::warning(this, tr("Classer les parties"), error);
    }
    m_games->refreshCount();
    if (changed >= 0) statusBar()->showMessage(tr("Classement ECO : %n partie(s) mise(s) à jour", nullptr, int(changed)), 8000);
    return int(changed);
}

QString MainWindow::identifyOpening()
{
    int ply = 0;
    const chess::eco::Opening* o = openingsBook().classify(m_session->game(), &ply);
    if (!o) return tr("Ouverture non répertoriée");
    m_session->setHeader("ECO", o->eco);
    m_session->setHeader("Opening", o->name);
    return tr("Ouverture : %1 %2 (reconnue au demi-coup %3)").arg(QString::fromStdString(o->eco), QString::fromStdString(o->name)).arg(ply);
}

EventTableDialog* MainWindow::eventTable(int row)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b || row < 0 || row >= b->count()) {
        statusBar()->showMessage(tr("Le tableau croisé concerne les bases Hydra64 (Fichier → Convertir… pour un PGN)"), 6000);
        return nullptr;
    }
    const uint32_t event = b->database().record(b->idOf(row)).event;
    if (event == hy64db::NoName) return nullptr;
    const QString path = m_games->model()->filePath();
    auto* dlg = new EventTableDialog(b->database(), event, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &EventTableDialog::gamesRequested, this, [this, path](const std::vector<uint32_t>& ids, const QString& title) { showGameIds(path, ids, title); });
    connect(m_games, &GameListPanel::databasesChanged, dlg, [this, dlg, path] {
        if (!m_games->modelFor(path)) dlg->close();
    });
    dlg->show();
    return dlg;
}

PlayerReportDialog* MainWindow::playerReport(const QString& player)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("La fiche du joueur concerne les bases Hydra64 (Fichier → Convertir… pour un PGN)"), 6000);
        return nullptr;
    }
    const QString path = m_games->model()->filePath();
    auto* dlg = new PlayerReportDialog(b->database(), QFileInfo(path).completeBaseName(), player, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &PlayerReportDialog::gamesRequested, this, [this, path](const std::vector<uint32_t>& ids, const QString& name) {
        showGameIds(path, ids, tr("Parties de %1").arg(name));
    });
    connect(dlg, &PlayerReportDialog::preparationRequested, this, [this, path](const QString& name) {
        if (m_games->activate(path)) preparation(name);
    });
    // the database closed or reloaded: the profile no longer has a database behind it
    connect(m_games, &GameListPanel::databasesChanged, dlg, [this, dlg, path] {
        if (!m_games->modelFor(path)) dlg->close();
    });
    dlg->show();
    return dlg;
}

PreparationDialog* MainWindow::preparation(const QString& player, bool black)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("La préparation contre un joueur concerne les bases Hydra64 (Fichier → Convertir… pour un PGN)"), 6000);
        return nullptr;
    }
    const QString path = m_games->model()->filePath();
    auto* dlg = new PreparationDialog(b->database(), QFileInfo(path).completeBaseName(), player, black ? chess::Black : chess::White, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setRepertoireLoader([](chess::Color c, chess::Game& out) { return loadRepertoire(c, out); });
    dlg->setErrorCache(errorreport::cachePath(path));
    // A mistake of the error report: its game opened on the position where the move was played
    connect(dlg, &PreparationDialog::positionRequested, this, [this, path](uint32_t game, int ply) {
        showGameIds(path, {game}, tr("Partie n° %1").arg(game + 1));
        if (m_games->model()->rowCount() != 1) return;
        m_games->selectGame(0);
        loadGameFromDatabase(0);
        const std::vector<Node*> line = m_session->game().mainline();
        if (ply >= 2 && size_t(ply - 2) < line.size()) m_session->goTo(line[size_t(ply - 2)]);
        else m_session->goTo(m_session->game().root());
    });
    connect(dlg, &PreparationDialog::gamesRequested, this, [this, path](const std::vector<uint32_t>& ids, const QString& title) { showGameIds(path, ids, title); });
    // the database closed or reloaded: the report no longer has a database behind it
    connect(m_games, &GameListPanel::databasesChanged, dlg, [this, dlg, path] {
        if (!m_games->modelFor(path)) dlg->close();
    });
    dlg->show();
    return dlg;
}

void MainWindow::renameDialog()
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("La correction des noms concerne les bases Hydra64 (Fichier → Convertir… pour un PGN)"), 6000);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Corriger un nom"));
    dlg.resize(520, 460);
    auto* type = new QComboBox;
    const std::pair<hy64db::NameType, QString> types[] = {{hy64db::Player, tr("Joueur")}, {hy64db::Event, tr("Tournoi")}, {hy64db::Site, tr("Lieu")},
                                                         {hy64db::Round, tr("Ronde")},   {hy64db::Annotator, tr("Annotateur")}};
    for (const auto& [t, label] : types) type->addItem(label, int(t));
    auto* search = new QLineEdit;
    search->setPlaceholderText(tr("Début d'un mot du nom…"));
    search->setClearButtonEnabled(true);
    auto* list = new QListWidget;
    auto* target = new QLineEdit;
    target->setPlaceholderText(tr("Nom corrigé (un nom existant : les deux sont fusionnés)"));
    auto* note = new QLabel(tr("Le nom est remplacé dans toutes les parties de la base. Choisir un nom existant fusionne les deux (fautes de frappe, "
                               "« Carlsen, M » et « Carlsen, Magnus »…)."));
    note->setWordWrap(true);
    note->setObjectName("Faint");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Remplacer"));
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    auto* form = new QFormLayout;
    form->addRow(tr("Type"), type);
    form->addRow(tr("Chercher"), search);
    form->addRow(tr("Nom à corriger"), list);
    form->addRow(tr("Remplacer par"), target);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(note);
    layout->addLayout(form, 1);
    layout->addWidget(buttons);
    std::vector<uint32_t> usage;
    auto refresh = [&] {
        list->clear();
        const auto t = hy64db::NameType(type->currentData().toInt());
        if (usage.empty()) usage = b->database().nameUsage(t);
        const QString text = search->text().trimmed();
        if (text.isEmpty()) return;
        int shown = 0;
        for (uint32_t id : b->database().names().findPrefix(t, text.toStdString())) {
            if (id >= usage.size() || usage[id] == 0) continue;
            auto* it = new QListWidgetItem(tr("%1  (%n partie(s))", nullptr, int(usage[id])).arg(QString::fromStdString(b->database().names().name(t, id))));
            it->setData(Qt::UserRole, QString::fromStdString(b->database().names().name(t, id)));
            list->addItem(it);
            if (++shown >= 500) break;
        }
    };
    connect(type, &QComboBox::currentIndexChanged, &dlg, [&] {
        usage.clear();
        refresh();
    });
    connect(search, &QLineEdit::textChanged, &dlg, refresh);
    connect(list, &QListWidget::currentItemChanged, &dlg, [&](QListWidgetItem* it) {
        if (it) target->setText(it->data(Qt::UserRole).toString());
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        QListWidgetItem* it = list->currentItem();
        const QString to = target->text().trimmed();
        if (!it || to.isEmpty()) return;
        const QString from = it->data(Qt::UserRole).toString();
        if (from == to) return;
        if (QMessageBox::question(&dlg, tr("Corriger un nom"), tr("Remplacer « %1 » par « %2 » dans toute la base ?").arg(from, to)) != QMessageBox::Yes) return;
        renameInDatabase(hy64db::NameType(type->currentData().toInt()), from, to);
        usage.clear();
        refresh();
    });
    dlg.exec();
}

bool MainWindow::sortDatabase(bool confirm)
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("Le tri d'une base concerne les bases Hydra64 (une base PGN se trie à l'affichage)"), 6000);
        return false;
    }
    if (b->isFiltered()) {
        statusBar()->showMessage(tr("Effacez d'abord la recherche (Base → Toute la base) : le tri réécrit toute la base"), 6000);
        return false;
    }
    const QString path = m_games->model()->filePath();
    if (confirm
        && QMessageBox::question(this, tr("Trier la base"),
                                 tr("Réécrire « %1 » dans l'ordre affiché ?\nLes parties sont renumérotées dans cet ordre ; les parties supprimées sont retirées.")
                                     .arg(QFileInfo(path).completeBaseName()))
               != QMessageBox::Yes)
        return false;
    std::vector<uint32_t> order;
    order.reserve(size_t(b->count()));
    for (int row = 0; row < b->count(); ++row) order.push_back(b->idOf(row));
    const bool hadPositions = b->database().positions() != nullptr;
    const bool ok = runWithProgress(tr("Trier la base"), tr("Tri de « %1 »…").arg(QFileInfo(path).completeBaseName()),
                                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                                        std::string err;
                                        const bool done = b->database().compact(&err, [&](uint64_t d, uint64_t t) {
                                            permille = t ? int(1000 * d / t) : 0;
                                            return !cancel.load();
                                        }, &order);
                                        status = QString::fromStdString(err);
                                        return done;
                                    },
                                    true);
    m_games->reload(path);
    if (ok && hadPositions) rebuildPositionIndex(); // the numbers have changed: index rebuilt
    updateDatabaseTree();
    statusBar()->showMessage(ok ? tr("Base triée : %1 parties dans le nouvel ordre").arg(QLocale().toString(m_games->model()->totalGames())) : tr("Tri interrompu"), 6000);
    return ok;
}

void MainWindow::rebuildPositionIndex()
{
    auto* b = dynamic_cast<Hy64Source*>(m_games->model()->indexed());
    if (!b) {
        statusBar()->showMessage(tr("L'index des positions concerne les bases Hydra64"), 6000);
        return;
    }
    const QString path = m_games->model()->filePath();
    const bool ok = runWithProgress(tr("Index des positions"), tr("Index des positions de « %1 »…").arg(QFileInfo(path).completeBaseName()),
                                    [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString& status) {
                                        std::string err;
                                        const int depth = b->database().positions() ? b->database().positions()->depth() : hy64db::DefaultPositionDepth;
                                        const bool done = hy64db::buildPositionIndex(b->database(), depth, &err, [&](uint64_t d, uint64_t t) {
                                            permille = t ? int(1000 * d / t) : 0;
                                            return !cancel.load();
                                        });
                                        status = QString::fromStdString(err);
                                        return done;
                                    },
                                    true);
    updateDatabaseTree();
    statusBar()->showMessage(ok ? tr("Index des positions reconstruit") : tr("Index des positions interrompu"), 6000);
}

NotationView* MainWindow::notationView() const { return m_notationPanel->view(); }

void MainWindow::addGameToDatabase(const QString& path)
{
    GameListModel* model = m_games->modelFor(path);
    if (!model || model->isReadOnly()) return;
    // Game without players: we first offer to complete its information (like Ctrl+S).
    const Game& game = m_session->game();
    if (GameInfoDialog::isUnknown(game.header("White")) && GameInfoDialog::isUnknown(game.header("Black"))) {
        if (GameInfoDialog::isUnknown(game.header("Date")))
            m_session->setHeader("Date", QDate::currentDate().toString(QStringLiteral("yyyy.MM.dd")).toStdString());
        GameInfoDialog dlg(m_session->game().headers(), this);
        dlg.setWindowTitle(tr("Ajouter à la base — informations de la partie"));
        if (dlg.exec() != QDialog::Accepted) return;
        m_session->setHeaders(dlg.headers());
        updateTitle();
    }
    QString error;
    const int row = model->appendGame(pgn::writeGame(m_session->game()), &error);
    const QString name = QFileInfo(path).completeBaseName();
    if (row < 0) {
        QMessageBox::warning(this, tr("Ajouter à la base"), tr("Impossible d'ajouter la partie à « %1 » :\n%2").arg(name, error));
        return;
    }
    m_session->setOrigin({path, row, true}); // Ctrl+S will update this game
    if (QFileInfo(path) == QFileInfo(m_games->model()->filePath())) m_games->selectGame(row);
    statusBar()->showMessage(tr("Partie ajoutée à « %1 »").arg(name), 6000);
}

void MainWindow::updateDatabaseTree()
{
    IndexedGameSource* src = m_games->model()->indexed();
    if (src && src->hasPositions())
        m_book->setDatabaseTree(QFileInfo(m_games->model()->filePath()).completeBaseName(), [src](const chess::Position& p) { return src->tree(p); });
    else
        m_book->setDatabaseTree({}, {});
}

void MainWindow::loadGameFromDatabase(int row)
{
    // A game of the database opens on the Databases page, which has its own game:
    // neither the analysis nor the Play page's game is touched.
    if (m_page != PageDatabase) setPage(PageDatabase);
    Game g;
    QString error;
    const bool ok = m_games->model()->parseGame(row, g, &error);
    m_session->loadGame(std::move(g));
    m_session->setOrigin({m_games->model()->filePath(), row, false});
    if (!ok) statusBar()->showMessage(tr("Partie partiellement lue : %1").arg(error), 8000);
}

void MainWindow::annotateDialog()
{
    if (!m_session->game().root()->mainChild()) {
        statusBar()->showMessage(tr("Aucun coup à annoter"), 4000);
        return;
    }
    AnnotateDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString summary;
    if (annotateGame(dlg.engine(), dlg.options(), &summary)) statusBar()->showMessage(tr("Partie annotée : %1").arg(summary), 10000);
}

bool MainWindow::annotateGame(const EngineConfig& engine, const annotation::Options& options, QString* summary)
{
    // Positions of the main line, start included
    QStringList fens;
    QList<int> ids;
    fens << QString::fromStdString(m_session->game().root()->pos.fen());
    for (const Node* n : m_session->game().mainline()) fens << QString::fromStdString(n->pos.fen());
    for (const Node* n : m_session->game().mainline()) ids << n->id;
    if (fens.size() < 2) return false;

    Annotator annotator;
    QProgressDialog progress(tr("Analyse des coups…"), tr("Annuler"), 0, int(fens.size()), this);
    progress.setWindowTitle(tr("Annoter la partie"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QEventLoop loop;
    bool completed = false;
    QString failure;
    connect(&annotator, &Annotator::progress, &progress, [&](int done, int total) {
        progress.setValue(done);
        progress.setLabelText(tr("Analyse de la position %1 / %2…").arg(std::min(done + 1, total)).arg(total));
    });
    connect(&annotator, &Annotator::errorOccurred, &progress, [&](const QString& m) { failure = m; });
    connect(&annotator, &Annotator::finished, &loop, [&](bool ok) {
        completed = ok;
        loop.quit();
    });
    connect(&progress, &QProgressDialog::canceled, &annotator, &Annotator::cancel);
    QString error;
    if (!annotator.start(engine, fens, options.movetimeMs, &error)) {
        QMessageBox::warning(this, tr("Annoter la partie"), error);
        return false;
    }
    progress.show();
    loop.exec();
    progress.disconnect();
    progress.close();
    if (!completed) {
        if (!failure.isEmpty()) QMessageBox::warning(this, tr("Annoter la partie"), failure);
        return false;
    }
    // The game has not changed during the analysis (modal window); we check anyway.
    QList<int> now;
    for (const Node* n : m_session->game().mainline()) now << n->id;
    if (now != ids) return false;
    annotation::Summary sum;
    const annotation::Options withBook = bookOptions(m_session->game(), options);
    m_session->editGame([&](Game& g) { sum = annotation::apply(g, annotator.evaluations(), withBook, annotator.engineName()); });
    QString exErr;
    if (!annotation::appendExercises(options.exercisesFile, sum.exercises, &exErr))
        QMessageBox::warning(this, tr("Annoter la partie"), tr("Recueil d'exercices non enregistré : %1").arg(exErr));
    if (summary) *summary = sum.text();
    return true;
}

int MainWindow::annotateGames(const QList<int>& rows, const EngineConfig& engine, const annotation::Options& options, QString* summary)
{
    GameListModel* model = m_games->model();
    if (!model || model->isReadOnly() || model->filePath().isEmpty()) return -1;
    QProgressDialog progress(tr("Annotation des parties…"), tr("Annuler"), 0, int(rows.size()) * 100, this);
    progress.setWindowTitle(tr("Annoter les parties"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    bool cancelled = false;
    connect(&progress, &QProgressDialog::canceled, this, [&] { cancelled = true; });
    progress.show();
    annotation::Summary total;
    int done = 0, failed = 0;
    for (int k = 0; k < rows.size() && !cancelled; ++k) {
        Game g;
        QString error;
        if (!model->parseGame(rows[k], g, &error)) {
            ++failed;
            continue;
        }
        QStringList fens;
        fens << QString::fromStdString(g.root()->pos.fen());
        for (const Node* n : g.mainline()) fens << QString::fromStdString(n->pos.fen());
        if (fens.size() < 2) continue;
        Annotator annotator;
        QEventLoop loop;
        bool completed = false;
        connect(&annotator, &Annotator::progress, &progress, [&](int d, int t) {
            progress.setValue(k * 100 + (t ? d * 100 / t : 0));
            progress.setLabelText(tr("Partie %1 / %2 — position %3 / %4…").arg(k + 1).arg(rows.size()).arg(std::min(d + 1, t)).arg(t));
        });
        connect(&annotator, &Annotator::finished, &loop, [&](bool ok) {
            completed = ok;
            loop.quit();
        });
        connect(&progress, &QProgressDialog::canceled, &annotator, &Annotator::cancel);
        if (!annotator.start(engine, fens, options.movetimeMs, &error)) {
            progress.close();
            QMessageBox::warning(this, tr("Annoter les parties"), error);
            return done;
        }
        loop.exec();
        if (!completed) break;
        const annotation::Summary s = annotation::apply(g, annotator.evaluations(), bookOptions(g, options), annotator.engineName());
        total.blunders += s.blunders;
        total.mistakes += s.mistakes;
        total.inaccuracies += s.inaccuracies;
        total.missedMates += s.missedMates;
        total.exercises += s.exercises;
        annotation::appendExercises(options.exercisesFile, s.exercises);
        if (model->replaceGame(rows[k], pgn::writeGame(g), &error)) ++done;
        else ++failed;
    }
    progress.disconnect();
    progress.close();
    if (summary) {
        *summary = tr("%n partie(s) annotée(s) : %1", nullptr, done).arg(total.text());
        if (failed) *summary += tr(" ; %n partie(s) illisible(s) ou non enregistrée(s)", nullptr, failed);
    }
    return done;
}

SolutionTestDialog* MainWindow::solutionTestDialog()
{
    if (!m_solutionTest) {
        m_solutionTest = new SolutionTestDialog(this);
        connect(m_solutionTest, &SolutionTestDialog::analyseRequested, this, &MainWindow::analysePosition);
    }
    return m_solutionTest;
}

void MainWindow::analysePosition(const QString& fen)
{
    Position pos;
    if (!pos.setFen(fen.toStdString())) return;
    analyseGame(Game(pos));
}

void MainWindow::analyseGame(Game game)
{
    if (m_session->slotHasMoves(GameSession::AnalysisSlot)) {
        // Same game (line contained in the displayed game): we go there without replacing anything.
        if (m_session->slot() == GameSession::AnalysisSlot) {
            Node* cur = m_session->game().root();
            bool inside = cur->pos.fen() == game.root()->pos.fen();
            for (const Node* n : std::vector<Node*>(game.mainline())) {
                if (!inside) break;
                Node* next = nullptr;
                for (const auto& c : cur->children)
                    if (c->move == n->move) next = c.get();
                if (!next) inside = false;
                else cur = next;
            }
            if (inside) {
                m_session->goTo(cur);
                return;
            }
        }
        QMessageBox box(QMessageBox::Question, tr("Envoyer dans l'analyse"), tr("La page Analyse contient déjà une partie."), QMessageBox::NoButton, this);
        box.setInformativeText(tr("La remplacer par cette position ?"));
        QPushButton* replace = box.addButton(tr("Remplacer"), QMessageBox::DestructiveRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != replace) return;
    }
    setPage(PageAnalysis);
    Node* end = nullptr;
    const std::vector<int> path = [&] {
        game.toEnd();
        return GameSession::pathTo(game.current());
    }();
    m_session->loadGame(std::move(game));
    m_session->setOrigin({});
    end = m_session->game().root();
    for (int i : path)
        if (i >= 0 && i < int(end->children.size())) end = end->children[size_t(i)].get();
    m_session->goTo(end);
}

annotation::Options MainWindow::bookOptions(const Game& g, annotation::Options o)
{
    // Half-moves of the main line known to the open opening tree
    o.bookPlies = 0;
    if (!o.bookMarker || m_book->format() == BookPanel::NoBook) return o;
    o.bookName = m_book->bookName();
    for (const Node* n : g.mainline()) {
        const std::vector<Move> known = m_book->movesAt(n->parent->pos);
        if (std::find(known.begin(), known.end(), n->move) == known.end()) break;
        ++o.bookPlies;
    }
    return o;
}

int MainWindow::insertEngineLines(const Position& pos, const QList<std::vector<Move>>& lines)
{
    // Node of the game at this position: the displayed move first, otherwise anywhere in the game
    Node* at = nullptr;
    const std::string fen = pos.fen();
    if (m_session->current()->pos.fen() == fen) at = m_session->current();
    std::function<void(Node*)> find = [&](Node* n) {
        if (at) return;
        if (n->pos.fen() == fen) {
            at = n;
            return;
        }
        for (const auto& c : n->children) find(c.get());
    };
    if (!at) find(m_session->game().root());
    if (!at) {
        statusBar()->showMessage(tr("La position analysée n'est pas dans la notation"), 5000);
        return 0;
    }
    int added = 0;
    m_session->editGame([&](Game& g) {
        for (const std::vector<Move>& line : lines) {
            Node* cur = at;
            for (const Move& m : line) {
                if (!cur->pos.isLegal(m)) break;
                cur = g.addMove(cur, m);
            }
            if (cur != at) ++added;
        }
    });
    statusBar()->showMessage(tr("%n variante(s) ajoutée(s) à la notation", nullptr, added), 5000);
    return added;
}

Node* MainWindow::findNovelty(QString* message)
{
    // Reference database: the displayed database if it has the position index, otherwise another open database
    const IndexedGameSource* ref = nullptr;
    QString refName;
    auto consider = [&](const GameListModel* m) {
        if (!ref && m && m->isIndexed() && m->indexed()->hasPositions()) {
            ref = m->indexed();
            refName = QFileInfo(m->filePath()).completeBaseName();
        }
    };
    consider(m_games->model());
    for (const QString& path : m_games->openPaths()) consider(m_games->modelFor(path));
    if (!ref) {
        if (message) *message = tr("Nouveauté : ouvrez une base Hydra64 avec index des positions comme référence");
        return nullptr;
    }
    for (Node* n : m_session->game().mainline()) {
        const std::vector<treebook::BookMove> known = ref->tree(n->parent->pos);
        if (known.empty()) { // outside the index (beyond its depth) or unknown position
            if (message) *message = tr("Pas de nouveauté avant le coup %1 : la position sort de l'index de « %2 »").arg(n->parent->pos.fullmoveNumber()).arg(refName);
            return nullptr;
        }
        if (std::none_of(known.begin(), known.end(), [&](const treebook::BookMove& b) { return b.move == n->move; })) {
            if (std::find(n->nags.begin(), n->nags.end(), 146) == n->nags.end()) {
                m_session->editGame([&](Game&) { n->nags.push_back(146); }); // N: novelty
            }
            m_session->goTo(n);
            const Position& at = n->parent->pos;
            const QString san = QStringLiteral("%1%2%3").arg(at.fullmoveNumber()).arg(at.sideToMove() == White ? QStringLiteral(".") : QStringLiteral("…"))
                                    .arg(display::san(at.toSan(n->move)));
            if (message) *message = tr("Nouveauté : %1, jamais joué dans « %2 »").arg(san, refName);
            return n;
        }
    }
    if (message) *message = tr("Pas de nouveauté : tous les coups de la partie sont dans « %1 »").arg(refName);
    return nullptr;
}

void MainWindow::showHint()
{
    const Position pos = m_session->current()->pos;
    if (m_play->isActive() && pos.sideToMove() != m_play->humanColor()) {
        statusBar()->showMessage(tr("Indice : seulement quand c'est à vous de jouer"), 4000);
        return;
    }
    statusBar()->showMessage(tr("Recherche d'un indice…"));
    m_coach->hint(pos, [this, pos](Move m) {
        if (m.isNull() || m_session->current()->pos.fen() != pos.fen()) {
            statusBar()->clearMessage();
            return;
        }
        m_viewer->board()->setEngineArrow(QStringLiteral("hint"), m, QColor(0x6a, 0xbf, 0x69, 210));
        statusBar()->showMessage(tr("Indice : %1").arg(display::san(pos.toSan(m))), 6000);
        // The arrow disappears as soon as the position changes
        auto* conn = new QMetaObject::Connection;
        *conn = connect(m_session, &GameSession::currentChanged, this, [this, conn] {
            m_viewer->board()->setEngineArrow(QStringLiteral("hint"), {}, {});
            disconnect(*conn);
            delete conn;
        });
    });
}

void MainWindow::updateDeepArrow()
{
    const bool show = m_page == PageAnalysis && m_deepDock && !m_deepDock->isClosed();
    m_viewer->board()->setEngineArrow(QStringLiteral("deep"), show ? m_deepBest : Move{}, QColor(0xe6, 0x8a, 0x35, 200));
}

void MainWindow::sendDatabaseGameToAnalysis(int row)
{
    Game g;
    QString error;
    const bool ok = m_games->model()->parseGame(row, g, &error);
    if (m_session->slotHasMoves(GameSession::AnalysisSlot)) {
        QMessageBox box(QMessageBox::Question, tr("Envoyer dans l'analyse"), tr("La page Analyse contient déjà une partie."), QMessageBox::NoButton, this);
        box.setInformativeText(tr("La remplacer par la partie n° %1 de la base ?").arg(row + 1));
        QPushButton* replace = box.addButton(tr("Remplacer"), QMessageBox::DestructiveRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != replace) return;
    }
    setPage(PageAnalysis);
    m_session->loadGame(std::move(g));
    m_session->setOrigin({m_games->model()->filePath(), row, false});
    statusBar()->showMessage(ok ? tr("Partie n° %1 envoyée dans l'analyse").arg(row + 1) : tr("Partie partiellement lue : %1").arg(error), 5000);
}

void MainWindow::startEngineGame()
{
    if (EngineRegistry::instance().engines().isEmpty() && !m_engine->engineConfig().isValid()) {
        if (QMessageBox::question(this, tr("Jouer contre le moteur"),
                                  tr("Aucun moteur n'est configuré.\nOuvrir la gestion des moteurs pour en ajouter un ?"))
            == QMessageBox::Yes)
            manageEngines();
        return;
    }
    PlayDialog dlg(m_engine->engineConfig().id, this);
    const int r = dlg.exec();
    m_playEngine->refresh(); // the engine or its settings may have changed
    updateClocks();
    if (r != QDialog::Accepted) {
        updatePlayHint();
        return;
    }
    startEngineGame(dlg.settings());
}

void MainWindow::changeOpponent(const QString& engineId)
{
    const EngineConfig cfg = EngineRegistry::instance().engine(engineId);
    if (!cfg.isValid()) return;
    // Remembered for the next games (same setting as the "Play against the engine" dialog).
    QSettings().setValue("play/engine", cfg.id);
    if (m_play->isActive()) {
        QString error;
        if (!m_play->changeEngine(cfg, &error)) {
            QMessageBox::warning(this, tr("Changer d'adversaire"), error);
            return;
        }
        statusBar()->showMessage(tr("Nouvel adversaire : %1 — la partie continue").arg(cfg.name), 5000);
    } else {
        updatePlayHint();
    }
    m_playEngine->refresh();
}

void MainWindow::startEngineForSideToMove()
{
    // Play with no game in progress: the engine takes the side to move, you the other.
    if (m_page != PageBoard) setPage(PageBoard);
    if (continueFlaggedGame()) { // game ended on time: it continues, notation and clocks kept
        m_play->moveNow();
        return;
    }
    PlaySettings ps = PlayDialog::savedSettings();
    ps.side = m_session->current()->pos.sideToMove() == White ? PlaySettings::PlayBlack : PlaySettings::PlayWhite;
    ps.fromCurrentPosition = true;
    startEngineGame(ps);
}

bool MainWindow::startEngineGame(const PlaySettings& settings)
{
    // The game is played on the Play page (its own game), never over another page.
    if (m_page != PageBoard) setPage(PageBoard);
    // No help from the analysis engine during the game.
    m_engine->setAnalysing(false);
    QString error;
    EngineConfig engine = EngineRegistry::instance().engine(settings.engineId);
    if (!engine.isValid()) engine = m_engine->engineConfig(); // engine of the panel (e.g. --engine)
    if (!m_play->start(engine, settings, &error)) {
        QMessageBox::warning(this, tr("Jouer contre le moteur"), error);
        return false;
    }
    m_viewer->setFlipped(m_play->humanColor() == Black);
    m_viewer->hideBanner(); // banner of the previous game
    updatePlayHint();
    statusBar()->showMessage(tr("Partie lancée : vous avez les %1").arg(m_play->humanColor() == White ? tr("Blancs") : tr("Noirs")), 5000);
    return true;
}

bool MainWindow::continueFlaggedGame()
{
    if (!m_play->canContinue()) return false;
    GameEnd end;
    if (detectGameEnd(m_session->current(), end)) return false; // mate, stalemate…: nothing left to play
    QString error;
    if (!m_play->continueGame(&error)) {
        if (!error.isEmpty()) QMessageBox::warning(this, tr("Jouer contre le moteur"), error);
        return false;
    }
    m_engine->setAnalysing(false);
    m_viewer->hideBanner();
    updatePlayHint();
    statusBar()->showMessage(tr("La partie continue : la pendule tombée reste à 0:00"), 5000);
    return true;
}

bool MainWindow::rematch()
{
    PlaySettings ps = m_play->settings();
    ps.side = m_play->humanColor() == White ? PlaySettings::PlayBlack : PlaySettings::PlayWhite;
    if (m_play->engineConfig().isValid()) ps.engineId = m_play->engineConfig().id;
    // Same starting position as the finished game (initial position or edited position).
    const Position start = m_session->game().root()->pos;
    ps.fromCurrentPosition = start.fen() != Position::startPos().fen();
    if (ps.fromCurrentPosition) m_session->newGame(start);
    return startEngineGame(ps);
}

void MainWindow::onBoardMove(Move m)
{
    // On the Play page, with no game in progress, the first move starts a game against
    // the engine (last settings used): you take the side of the piece played.
    // Game ended on time: playing a move continues it (notation and clocks kept).
    if (m_page == PageBoard && !m_play->isActive() && m_session->current()->pos.isLegal(m)) continueFlaggedGame();
    if (m_page == PageBoard && !m_play->isActive()) {
        const Position& pos = m_session->current()->pos;
        GameEnd end;
        if (!detectGameEnd(m_session->current(), end) && pos.isLegal(m)) {
            PlaySettings ps = PlayDialog::savedSettings();
            ps.side = pos.sideToMove() == White ? PlaySettings::PlayWhite : PlaySettings::PlayBlack;
            ps.fromCurrentPosition = true;
            if (startEngineGame(ps)) {
                m_session->playMove(m); // same position as before the start: the move stays legal
                return;
            }
        }
    }
    // Tournament game being played: the board is read-only.
    if (m_page == PageTournament && m_followLive && m_tournament->currentIndex() >= 0) return;
    if (m_play->isActive() && m_page == PageBoard) m_play->playHumanMove(m);
    else m_session->playMove(m);
}

void MainWindow::updateClocks()
{
    if (m_page == PageBoard && m_play->isActive() && !m_play->isSuspended() && m_play->isTimed()) {
        const ChessClock* c = m_play->clock();
        m_viewer->setLiveClocks(true, c->remaining(White), c->remaining(Black), c->isRunning() ? int(c->runningSide()) : -1);
        return;
    }
    if (m_page == PageTournament && m_followLive && m_tournament->currentIndex() >= 0) {
        const ChessClock* c = m_tournament->clock();
        m_viewer->setLiveClocks(true, c->remaining(White), c->remaining(Black), c->isRunning() ? int(c->runningSide()) : -1);
        return;
    }
    m_viewer->setLiveClocks(false);
    // Play page with no game in progress: the clock of the next game is displayed.
    const PlaySettings ps = PlayDialog::savedSettings();
    if (m_page == PageBoard && ps.isTimed()) {
        const qint64 engineMs = ps.engineSeparate ? ps.engineBaseMs : ps.baseMs;
        const bool humanBlack = ps.side == PlaySettings::PlayBlack;
        m_viewer->setPreviewClocks(humanBlack ? engineMs : ps.baseMs, humanBlack ? ps.baseMs : engineMs);
    } else {
        m_viewer->setPreviewClocks(-1, -1);
    }
}

void MainWindow::sendToAnalysis(bool confirm)
{
    if (m_page == PageAnalysis) return;
    // Databases page with no game open: the game selected in the list.
    if (m_page == PageDatabase && !m_session->game().root()->mainChild() && !m_games->selectedRows().isEmpty()) {
        sendDatabaseGameToAnalysis(m_games->selectedRows().first());
        return;
    }
    if (!m_session->game().root()->mainChild()) {
        statusBar()->showMessage(tr("Aucun coup à envoyer dans l'analyse"), 4000);
        return;
    }
    if (confirm && m_session->slotHasMoves(GameSession::AnalysisSlot)) {
        QMessageBox box(QMessageBox::Question, tr("Envoyer dans l'analyse"), tr("La page Analyse contient déjà une partie."),
                        QMessageBox::NoButton, this);
        box.setInformativeText(tr("La remplacer par la partie affichée ?"));
        QPushButton* replace = box.addButton(tr("Remplacer"), QMessageBox::DestructiveRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != replace) return;
    }
    // Full copy (variations, comments, clock times) and same displayed move.
    const std::vector<int> path = GameSession::pathTo(m_session->current());
    Game copy = copyGame(m_session->game());
    const GameOrigin origin = m_session->origin(); // a game from the database stays linked to the database
    setPage(PageAnalysis); // the game of the page being left is put aside (paused if it is in progress)
    m_session->loadGame(std::move(copy));
    m_session->setOrigin(origin);
    Node* n = m_session->game().root();
    for (int i : path)
        if (size_t(i) < n->children.size()) n = n->children[size_t(i)].get();
    m_session->goTo(n);
    statusBar()->showMessage(tr("Partie envoyée dans l'analyse"), 4000);
}

void MainWindow::showLiveTournamentGame()
{
    m_followLive = true;
    m_session->replaceGameIn(GameSession::TournamentSlot, copyGame(m_tournament->liveGame()));
    updateBoardInteraction();
    updateClocks();
}

void MainWindow::setTournamentOverview(bool on)
{
    m_overviewOn = on;
    if (m_overviewAction && m_overviewAction->isChecked() != on) m_overviewAction->setChecked(on);
    m_tournamentPanel->setOverviewChecked(on);
    updateTournamentView();
}

void MainWindow::updateTournamentView()
{
    const bool overview = m_overviewOn && m_page == PageTournament;
    if (overview) m_overview->setFlipped(m_viewer->isFlipped());
    if (m_page == PageDatabase) return; // the centre is the game list, the board is in its panel
    m_centerStack->setCurrentWidget(overview ? static_cast<QWidget*>(m_overview) : static_cast<QWidget*>(m_viewer));
}

// Places the game list and the board according to the page. Databases page: the list in the centre,
// the board in the "Board" panel. Other pages: the board in the centre, the list in the
// "Databases" panel, under the list of open databases.
void MainWindow::arrangeCenter(Page page)
{
    const bool database = page == PageDatabase;
    QWidget* listHome = database ? m_gamesCenter : m_gamesSide;
    if (m_games->parentWidget() != listHome) {
        auto* l = static_cast<QVBoxLayout*>(listHome->layout());
        l->addWidget(m_games, 4);
        m_games->setVisible(true);
    }
    if (database) {
        if (m_viewer->parentWidget() != m_boardHolder) {
            m_boardHolder->layout()->addWidget(m_viewer);
            m_viewer->show();
        }
        m_centerStack->setCurrentWidget(m_gamesCenter);
    } else if (m_centerStack->indexOf(m_viewer) < 0) {
        m_centerStack->insertWidget(0, m_viewer);
        m_centerStack->setCurrentWidget(m_viewer);
    }
}

bool MainWindow::tournamentOverviewShown() const { return m_centerStack->currentWidget() == m_overview; }

void MainWindow::viewTournamentGame(int index)
{
    if (index < 0 || index >= m_tournament->games().size()) return;
    if (m_page != PageTournament) setPage(PageTournament);
    if (m_overviewOn) setTournamentOverview(false); // a requested game is displayed full size
    // Game in progress (simultaneous games: not necessarily the one being followed): it is followed.
    if (index == m_tournament->currentIndex() || m_tournament->showGame(index)) {
        showLiveTournamentGame();
        return;
    }
    const Tournament::GameRecord& g = m_tournament->games()[index];
    if (g.blackId.isEmpty() && !g.result.isEmpty()) {
        statusBar()->showMessage(tr("%1 est exempt à cette ronde (1 point, pas de partie)").arg(g.whiteName), 4000);
        return;
    }
    if (g.pgn.isEmpty()) {
        statusBar()->showMessage(tr("La partie %1 n'a pas encore été jouée").arg(index + 1), 4000);
        return;
    }
    Game game;
    pgn::parseGame(g.pgn.toStdString(), game);
    m_followLive = false;
    m_session->replaceGameIn(GameSession::TournamentSlot, std::move(game));
    m_session->toStart();
    updateBoardInteraction();
    updateClocks();
    if (m_tournament->isRunning()) statusBar()->showMessage(tr("Tournoi > Suivre la partie en cours pour revenir à la partie qui se joue"), 6000);
}

void MainWindow::updateBoardInteraction()
{
    BoardWidget* board = m_viewer->board();
    if (m_page == PageTournament && m_followLive && m_tournament->currentIndex() >= 0) {
        // Tournament game in progress: we watch, we do not touch.
        board->setMovableSides(false, false);
        const bool whiteToMove = m_session->current()->pos.sideToMove() == White;
        m_viewer->setPlayerStatus(whiteToMove, m_tournament->isRunning() ? tr("réfléchit…") : QString());
        return;
    }
    if (m_page != PageBoard || !m_play->isActive() || m_play->isSuspended() || m_play->isHalted()) {
        board->setMovableSides(true, true);
        m_viewer->setPlayerStatus(true, QString());
        m_viewer->setPlayerStatus(false, QString());
        if (m_page == PageBoard && m_play->isActive() && m_play->isHalted()) m_viewer->setPlayerStatus(m_play->humanColor() != White, tr("arrêté — Play pour reprendre"));
        return;
    }
    const Color human = m_play->humanColor();
    // Move deleted, engine waiting: its side is playable (playing it swaps the sides)
    const bool awaiting = m_play->isAwaiting() && m_play->isAtGameEnd();
    const bool myTurn = (m_session->current()->pos.sideToMove() == human || awaiting) && !m_play->isThinking();
    const Color toMove = m_session->current()->pos.sideToMove();
    board->setMovableSides(myTurn && toMove == White, myTurn && toMove == Black);
    QString engineStatus;
    if (m_play->isThinking()) engineStatus = tr("réfléchit…");
    else if (awaiting) engineStatus = tr("en attente — Play, ou jouez son coup");
    else if (!m_play->isAtGameEnd()) engineStatus = tr("en pause — Fin pour reprendre");
    m_viewer->setPlayerStatus(human != White, engineStatus);
}

void MainWindow::onCurrentChanged(bool animate)
{
    const Node* cur = m_session->current();
    m_viewer->board()->setPosition(cur->pos, cur->move, animate);
    m_viewer->board()->setGraphics(cur->graphics); // arrows and marked squares saved with this move
    m_viewer->evalBar()->setEvaluation(cur->eval);
    m_engine->setPosition(cur->pos);
    if (m_tablebaseDock && !m_tablebaseDock->isClosed()) m_tablebase->setPosition(cur->pos);
    for (const Spectator& s : std::as_const(m_spectators)) s.panel->setPosition(cur->pos);
    updateBoardInteraction();
}

void MainWindow::updateTitle()
{
    const Game& g = m_session->game();
    const QString white = QString::fromStdString(g.header("White"));
    const QString black = QString::fromStdString(g.header("Black"));
    const QString app = QString::fromLatin1(AppName);
    if (white.isEmpty() && black.isEmpty()) setWindowTitle(app);
    else setWindowTitle(QStringLiteral("%1 – %2 · %3").arg(white.isEmpty() ? QStringLiteral("?") : white, black.isEmpty() ? QStringLiteral("?") : black, app));
}

bool MainWindow::editGameInfo()
{
    GameInfoDialog dlg(m_session->game().headers(), this);
    if (dlg.exec() != QDialog::Accepted) return false;
    m_session->setHeaders(dlg.headers());
    updateTitle();
    // Game saved in the database: the database follows (update, or a question if it comes from elsewhere).
    if (m_session->origin().isValid() && m_games->activate(m_session->origin().path) && !m_games->model()->isReadOnly()) saveToDatabase();
    return true;
}

void MainWindow::editDatabaseGameInfo(int row)
{
    GameListModel* model = m_games->model();
    if (model->isReadOnly()) {
        statusBar()->showMessage(tr("Base en lecture seule : informations non modifiables"), 6000);
        return;
    }
    Game g;
    QString error;
    if (!model->parseGame(row, g, &error)) {
        // Rewriting a badly read game would truncate it: refused.
        QMessageBox::warning(this, tr("Modifier les informations"),
                             tr("La partie n° %1 contient un coup illisible (%2).\nElle n'est pas modifiée pour ne rien perdre.").arg(row + 1).arg(error));
        return;
    }
    GameInfoDialog dlg(g.headers(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    g.headers() = dlg.headers();
    if (!model->replaceGame(row, pgn::writeGame(g), &error)) {
        QMessageBox::warning(this, tr("Modifier les informations"), error);
        return;
    }
    // The displayed game is this one: its headers follow.
    const GameOrigin o = m_session->origin();
    if (o.isValid() && o.row == row && QFileInfo(o.path) == QFileInfo(model->filePath())) {
        m_session->setHeaders(g.headers());
        updateTitle();
    }
    m_games->selectGame(row);
    statusBar()->showMessage(tr("Informations de la partie n° %1 mises à jour").arg(row + 1), 5000);
}

void MainWindow::editPosition()
{
    if (m_play->isActive()
        && QMessageBox::question(this, tr("Éditer la position"), tr("Une partie contre le moteur est en cours. L'interrompre pour éditer la position ?"))
               != QMessageBox::Yes)
        return;
    PositionEditorDialog dlg(m_session->current()->pos, m_viewer->isFlipped(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_session->newGame(dlg.position());
    statusBar()->showMessage(tr("Nouvelle position : %1").arg(QString::fromStdString(dlg.position().fen())), 6000);
}

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (accepted) m_tablebase->setPosition(m_session->current()->pos); // the tables may have changed
    if (!accepted || !dlg.languageChanged()) return;
    QMessageBox box(QMessageBox::Question, tr("Préférences"), tr("Redémarrer Hydra64 maintenant pour changer de langue ?"), QMessageBox::NoButton, this);
    QPushButton* now = box.addButton(tr("Redémarrer"), QMessageBox::AcceptRole);
    box.addButton(tr("Plus tard"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != now) return;
    // The window closes normally (layout and settings saved), then we restart.
    if (close()) QProcess::startDetached(QApplication::applicationFilePath(), QApplication::arguments().mid(1));
}

void MainWindow::showAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("À propos de Hydra64"));
    box.setIconPixmap(theme::logo(96, devicePixelRatioF()));
    // Links in the accent colour: the author's GitHub page, Stockfish's site
    const QString link = QStringLiteral("<a href='%1' style='color:%2; text-decoration:none;'>%3</a>");
    const QString accent = QColor(theme::Accent).name();
    const QString sf = link.arg(QStringLiteral("https://stockfishchess.org"), accent, QStringLiteral("Stockfish"));
    const QString stockfish = QFileInfo::exists(EngineRegistry::builtinStockfishPath())
                                  ? tr("%1 embarqué pour l'évaluation automatique").arg(sf)
                                  : tr("%1 embarqué absent : évaluation par le Stockfish du système").arg(sf);
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<b style='font-size:16px;'>Hydra64</b>&nbsp;&nbsp;%1").arg(QApplication::applicationVersion()));
    box.setInformativeText(tr("<p>Interface d'échecs : bases de données PGN, analyse avec moteurs spectateurs, "
                              "parties contre un moteur et tournois de moteurs.</p>"
                              "<p>%1.<br>Qt %2 · Qt Advanced Docking System (LGPL 2.1) · %3 (GPL v3) · "
                              "Shuffle, moteur maison.</p>")
                               .arg(stockfish, QString::fromLatin1(qVersion()), sf)
                           + QStringLiteral("<p>© 2026 %1</p>").arg(link.arg(QStringLiteral("https://github.com/Antidote1911"), accent, QStringLiteral("Antidote1911"))));
    for (QLabel* l : box.findChildren<QLabel*>()) { // clickable links, opened in the browser
        l->setOpenExternalLinks(true);
        l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    }
    box.exec();
}

void MainWindow::resetApplication()
{
    QMessageBox box(QMessageBox::Warning, tr("Réinitialiser Hydra64"), tr("Revenir à Hydra64 comme après une installation neuve ?"),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("Seront effacés : préférences, dispositions des pages, moteurs ajoutés et leurs réglages, "
                              "moteurs spectateurs, parties de chaque page, partie en cours contre le moteur et tournoi.\n\n"
                              "Vos fichiers PGN ne sont pas touchés (seulement oubliés).\n\n"
                              "Hydra64 redémarre aussitôt."));
    QPushButton* reset = box.addButton(tr("Tout réinitialiser"), QMessageBox::DestructiveRole);
    box.addButton(tr("Annuler"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(box.buttons().last()));
    box.exec();
    if (box.clickedButton() != reset) return;
    m_resetRequested = true; // closeEvent saves nothing; main() wipes then restarts
    close();
}

void MainWindow::wipeUserData()
{
    // Settings (preferences, layouts…)
    {
        QSettings s;
        s.clear();
        s.sync();
        prefs::invalidateCache();
    }
    // Data: engines, session (games, tournament)… except the user's piece sets.
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    for (const QFileInfo& fi : data.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (fi.fileName() == QLatin1String("pieces")) continue;
        if (fi.isDir()) QDir(fi.absoluteFilePath()).removeRecursively();
        else QFile::remove(fi.absoluteFilePath());
    }
}

void MainWindow::manageEngines(const QString& engineId)
{
    EngineManagerDialog dlg(this, engineId);
    dlg.exec();
    m_playEngine->refresh();
}

void MainWindow::engineTabSettings(const QString& tab, const QString& tabTitle, const QString& engineId)
{
    if (!EngineRegistry::instance().engine(engineId).isValid()) {
        manageEngines(engineId); // engine outside the registry: no per-tab settings
        return;
    }
    // Tab titles are displayed in capitals ("SPECTATEUR 1"): ordinary name in the dialog
    const QString title = tabTitle.left(1).toUpper() + tabTitle.mid(1).toLower();
    EngineTabDialog dlg(tab, title, engineId, this);
    // Opponent tab during a game: strength and lines come from the game
    if (tab == QLatin1String(tabengine::PlayTab) && m_play->isActive() && m_play->engineConfig().id == engineId) dlg.setGameOptions(m_play->managedOptions());
    dlg.exec();
    m_playEngine->refresh();
    if (dlg.manageEnginesRequested()) manageEngines(engineId);
}

void MainWindow::pasteFenOrPgn()
{
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) return;
    Position pos;
    if (pos.setFen(text.toStdString())) {
        m_session->newGame(pos);
        statusBar()->showMessage(tr("Position collée"), 3000);
        return;
    }
    Game g;
    std::string err;
    const bool ok = pgn::parseGame(text.toStdString(), g, &err);
    if (!ok && !g.root()->mainChild()) {
        statusBar()->showMessage(tr("Le presse-papiers ne contient ni FEN ni PGN valide."), 5000);
        return;
    }
    m_session->loadGame(std::move(g));
    statusBar()->showMessage(ok ? tr("Partie collée") : tr("Partie partiellement collée : %1").arg(QString::fromStdString(err)), 5000);
}

void MainWindow::saveToDatabase(bool chooseFile)
{
    // The game comes from an open database: that is where it is saved.
    if (!chooseFile && m_session->origin().isValid()) m_games->activate(m_session->origin().path);
    GameListModel* model = m_games->model();
    if (!chooseFile && model->isReadOnly()) { // ChessBase database: we save elsewhere
        statusBar()->showMessage(tr("« %1 » est en lecture seule : choisissez une autre base").arg(QFileInfo(model->filePath()).fileName()), 6000);
        chooseFile = true;
    }
    QString path = model->filePath();
    if (chooseFile || path.isEmpty()) {
        QSettings s;
        QString filter = tr(PgnFilter);
        path = QFileDialog::getSaveFileName(this, tr("Enregistrer dans une base"), userdirs::startDir(userdirs::Kind::Database) + QStringLiteral("/Mes parties"),
                                            tr(PgnFilter) + QStringLiteral(";;") + tr(Hy64SingleFilter) + QStringLiteral(";;") + tr(Hy64Filter), &filter, QFileDialog::DontConfirmOverwrite);
        if (path.isEmpty()) return;
        path = withSuffix(path, filter);
        s.setValue("lastDir", QFileInfo(path).absolutePath());
        userdirs::remember(userdirs::Kind::Database, path);
        if (!QFileInfo::exists(path) && !createDatabaseFile(path)) return; // new empty database
        if (QFileInfo(path) != QFileInfo(model->filePath()) && !openDatabase(path)) return;
        path = model->filePath();
    }

    // Game without players: we first offer to complete its information.
    const Game& game = m_session->game();
    if (GameInfoDialog::isUnknown(game.header("White")) && GameInfoDialog::isUnknown(game.header("Black"))) {
        if (GameInfoDialog::isUnknown(game.header("Date")))
            m_session->setHeader("Date", QDate::currentDate().toString(QStringLiteral("yyyy.MM.dd")).toStdString());
        GameInfoDialog dlg(m_session->game().headers(), this);
        dlg.setWindowTitle(tr("Enregistrer dans la base — informations de la partie"));
        if (dlg.exec() != QDialog::Accepted) return;
        m_session->setHeaders(dlg.headers());
        updateTitle();
    }

    const std::string text = pgn::writeGame(m_session->game());
    const QString file = QFileInfo(path).fileName();
    GameOrigin origin = m_session->origin();
    const bool sameFile = origin.isValid() && QFileInfo(origin.path) == QFileInfo(path) && origin.row < model->rowCount();

    bool replace = sameFile && origin.savedHere;
    if (sameFile && !origin.savedHere) {
        QMessageBox box(QMessageBox::Question, tr("Enregistrer dans la base"),
                        tr("Cette partie vient de « %1 » (n° %2).").arg(file).arg(origin.row + 1), QMessageBox::NoButton, this);
        box.setInformativeText(tr("Remplacer la partie d'origine par la version actuelle, ou l'ajouter comme nouvelle partie ?"));
        QPushButton* rep = box.addButton(tr("Remplacer"), QMessageBox::DestructiveRole);
        QPushButton* add = box.addButton(tr("Ajouter"), QMessageBox::AcceptRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.setDefaultButton(add);
        box.exec();
        if (box.clickedButton() != rep && box.clickedButton() != add) return;
        replace = box.clickedButton() == rep;
    }

    QString error;
    int row = origin.row;
    if (replace) {
        if (!model->replaceGame(row, text, &error)) row = -1;
    } else {
        row = model->appendGame(text, &error);
    }
    if (row < 0) {
        QMessageBox::warning(this, tr("Enregistrer"), tr("Impossible d'écrire dans « %1 » :\n%2").arg(file, error));
        return;
    }
    m_session->setOrigin({path, row, true});
    m_games->selectGame(row);
    statusBar()->showMessage(replace ? tr("Partie n° %1 mise à jour dans %2").arg(row + 1).arg(file)
                                     : tr("Partie enregistrée dans %1 (n° %2)").arg(file).arg(row + 1),
                             6000);
}

int MainWindow::mergeGames(const QList<int>& rows, bool intoAnalysis)
{
    GameListModel* model = m_games->model();
    Game target;
    bool haveTarget = false;
    if (intoAnalysis) {
        target = copyGame(m_session->gameIn(GameSession::AnalysisSlot));
        haveTarget = true;
    }
    int merged = 0, added = 0, different = 0;
    for (int row : rows) {
        Game g;
        if (row < 0 || row >= model->rowCount() || !model->parseGame(row, g, nullptr)) continue;
        auto h = [&](const char* key) {
            const QString v = QString::fromStdString(g.header(key)).trimmed();
            return v == QLatin1String("?") ? QString() : v;
        };
        if (!haveTarget) { // the first game serves as the trunk
            target = std::move(g);
            haveTarget = true;
            ++merged;
            continue;
        }
        // Marker at the end of the line: "White – Black, site year, result"
        QStringList where{h("Site"), h("Date").left(4)};
        where.removeAll(QString());
        where.removeAll(QStringLiteral("????"));
        const QString label = QStringLiteral("%1 – %2%3%4").arg(h("White"), h("Black"), where.isEmpty() ? QString() : QStringLiteral(", ") + where.join(QLatin1Char(' ')),
                                                                 h("Result").isEmpty() || h("Result") == QLatin1String("*") ? QString() : QStringLiteral(", ") + h("Result"));
        const int n = target.merge(g, label.toStdString());
        if (n < 0) ++different;
        else ++merged, added += n;
    }
    if (!haveTarget || merged == 0) {
        statusBar()->showMessage(tr("Aucune partie fusionnée"), 5000);
        return -1;
    }
    if (!intoAnalysis && merged >= 2) { // an opening file is nobody's game
        target.setHeader("Event", tr("Parties fusionnées").toStdString());
        target.setHeader("White", "?");
        target.setHeader("Black", "?");
        target.setHeader("Result", "*");
        std::erase_if(target.headers(), [](const auto& h) {
            for (const char* key : {"WhiteElo", "BlackElo", "Date", "Round", "Site", "Annotator", "Termination", "ECO", "PlyCount", "EventDate"})
                if (h.first == key) return true;
            return false;
        });
    }
    if (m_page != PageAnalysis) setPage(PageAnalysis);
    if (intoAnalysis) {
        m_session->editGame([&](Game& current) { current = std::move(target); }); // undoable (Ctrl+Z)
    } else {
        m_session->loadGame(std::move(target));
    }
    m_session->toStart();
    statusBar()->showMessage(different ? tr("%1 partie(s) fusionnée(s), %2 coup(s) ajouté(s) ; %3 ignorée(s) : position de départ différente").arg(merged).arg(added).arg(different)
                                       : tr("%1 partie(s) fusionnée(s), %2 coup(s) ajouté(s)").arg(merged).arg(added),
                             8000);
    return merged;
}

// ---------------------------------------------------------------- clipboard database

QString MainWindow::clipboardBasePath()
{
    return userdirs::databases() + QStringLiteral("/Clipboard.hy64");
}

// Opens the clipboard database without changing the displayed database; created if needed.
static GameListModel* ensureClipboardBase(GameListPanel* games, QString* error)
{
    const QString path = MainWindow::clipboardBasePath();
    if (GameListModel* open = games->modelFor(path)) return open;
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!Hy64Source::create(path, error)) return nullptr;
    }
    const QStringList before = games->openPaths();
    const QString shown = games->model() ? games->model()->filePath() : QString();
    if (!games->openFile(path, error)) return nullptr;
    if (!shown.isEmpty() && before.contains(shown)) games->activate(shown); // we stay on the starting database
    return games->modelFor(path);
}

int MainWindow::copyToClipboardBase(const QList<int>& rows)
{
    const QString from = m_games->model()->filePath();
    QString error;
    if (from == clipboardBasePath()) {
        statusBar()->showMessage(tr("Ces parties sont déjà dans la base presse-papiers"), 5000);
        return -1;
    }
    if (!ensureClipboardBase(m_games, &error)) {
        QMessageBox::warning(this, tr("Base presse-papiers"), error);
        return -1;
    }
    const int copied = m_games->copyGames(from, rows, clipboardBasePath(), &error);
    if (copied < 0) QMessageBox::warning(this, tr("Base presse-papiers"), error);
    else statusBar()->showMessage(tr("%n partie(s) copiée(s) dans la base presse-papiers", nullptr, copied), 5000);
    return copied;
}

bool MainWindow::addCurrentGameToClipboardBase()
{
    QString error;
    GameListModel* clip = ensureClipboardBase(m_games, &error);
    if (!clip || clip->appendGame(pgn::writeGame(m_session->game()), &error) < 0) {
        QMessageBox::warning(this, tr("Base presse-papiers"), error);
        return false;
    }
    m_games->refreshCount();
    statusBar()->showMessage(tr("Partie ajoutée à la base presse-papiers (%1 parties)").arg(clip->rowCount()), 5000);
    return true;
}

bool MainWindow::openClipboardBase()
{
    QString error;
    if (!ensureClipboardBase(m_games, &error)) {
        QMessageBox::warning(this, tr("Base presse-papiers"), error);
        return false;
    }
    m_games->activate(clipboardBasePath());
    if (m_page != PageDatabase) setPage(PageDatabase);
    return true;
}

bool MainWindow::emptyClipboardBase()
{
    const QString path = clipboardBasePath();
    const bool wasOpen = m_games->modelFor(path) != nullptr;
    if (wasOpen && m_games->activate(path)) m_games->closeCurrent();
    QFile::remove(path);
    QString error;
    if (!Hy64Source::create(path, &error)) {
        QMessageBox::warning(this, tr("Base presse-papiers"), error);
        return false;
    }
    if (wasOpen) m_games->openFile(path, &error);
    statusBar()->showMessage(tr("Base presse-papiers vidée"), 4000);
    return true;
}

// ---------------------------------------------------------------- paper: PDF, printing

void MainWindow::fillGameDocument(QTextDocument& doc, const Game& game, int* diagramCount)
{
    auto h = [&](const char* key) {
        const QString v = QString::fromStdString(game.header(key)).trimmed();
        return v == QLatin1String("?") ? QString() : v;
    };
    // Board of a position, rendered off screen with the current pieces and colours
    int diagrams = 0;
    auto diagram = [&](const Node* n) {
        BoardWidget board;
        board.resize(360, 360);
        board.setPosition(n->pos, n->parent ? n->move : Move(), false);
        board.setGraphics(n->graphics);
        const QString name = QStringLiteral("diagram%1").arg(++diagrams);
        doc.addResource(QTextDocument::ImageResource, QUrl(name), board.grab().toImage());
        return QStringLiteral("<p align=center><img src=\"%1\" width=230 height=230></p>").arg(name);
    };
    const bool marked = [&] {
        std::function<bool(const Node*)> any = [&](const Node* n) {
            if (std::find(n->nags.begin(), n->nags.end(), pgn::DiagramNag) != n->nags.end()) return true;
            for (const auto& c : n->children)
                if (any(c.get())) return true;
            return false;
        };
        return any(game.root());
    }();
    auto moveText = [&](const Node* n, bool forceNumber) {
        const Position& before = n->parent->pos;
        QString t;
        if (before.sideToMove() == White) t = QStringLiteral("%1. ").arg(before.fullmoveNumber());
        else if (forceNumber) t = QStringLiteral("%1… ").arg(before.fullmoveNumber());
        t += display::san(n->san);
        for (int nag : n->nags)
            if (nag != pgn::DiagramNag) t += (nag <= 6 ? QString() : QStringLiteral(" ")) + QString::fromStdString(pgn::nagSymbol(nag));
        return t.toHtmlEscaped();
    };
    std::function<QString(const Node*, bool)> line = [&](const Node* from, bool mainline) {
        QString out;
        bool force = true;
        for (const Node* n = from; n->mainChild();) {
            const Node* m = n->mainChild();
            if (!m->preComment.empty()) { // pre-move comment
                out += QStringLiteral("<i>%1</i> ").arg(display::comment(QString::fromStdString(pgn::withoutCommands(m->preComment))).toHtmlEscaped());
                force = true;
            }
            out += mainline ? QStringLiteral("<b>%1</b> ").arg(moveText(m, force)) : moveText(m, force) + QLatin1Char(' ');
            force = false;
            if (!m->comment.empty()) {
                out += QStringLiteral("<i>%1</i> ").arg(display::comment(QString::fromStdString(pgn::withoutCommands(m->comment))).toHtmlEscaped());
                force = true;
            }
            for (size_t v = 1; v < n->children.size(); ++v) {
                const Node* alt = n->children[v].get();
                out += QStringLiteral("(%1%2").arg(alt->preComment.empty() ? QString() : QStringLiteral("<i>%1</i> ").arg(display::comment(QString::fromStdString(pgn::withoutCommands(alt->preComment))).toHtmlEscaped()),
                                                   moveText(alt, true));
                if (!alt->comment.empty()) out += QStringLiteral(" <i>%1</i>").arg(display::comment(QString::fromStdString(pgn::withoutCommands(alt->comment))).toHtmlEscaped());
                const QString rest = line(alt, false).trimmed();
                out += (rest.isEmpty() ? QString() : QLatin1Char(' ') + rest) + QStringLiteral(") ");
                force = true;
            }
            const bool wanted = std::find(m->nags.begin(), m->nags.end(), pgn::DiagramNag) != m->nags.end();
            const bool regular = mainline && !marked && m->ply() % 20 == 0 && m->mainChild(); // without a marker: every ten moves
            if (wanted || regular) {
                out += QStringLiteral("</p>") + diagram(m) + QStringLiteral("<p>");
                force = true;
            }
            n = m;
        }
        return out;
    };
    const QString white = h("White").isEmpty() ? QStringLiteral("?") : h("White"), black = h("Black").isEmpty() ? QStringLiteral("?") : h("Black");
    auto elo = [&](const char* key) { return h(key).isEmpty() || h(key) == QLatin1String("0") ? QString() : QStringLiteral(" (%1)").arg(h(key)); };
    QStringList where{h("Event"), h("Site"), h("Round").isEmpty() ? QString() : tr("ronde %1").arg(h("Round")), display::date(game.header("Date")), h("ECO")};
    where.removeAll(QString());
    QString html = QStringLiteral("<p style=\"font-size:16pt;font-weight:bold;margin-bottom:0\">%1%2 – %3%4</p><p>%5</p>").arg(white.toHtmlEscaped(), elo("WhiteElo"), black.toHtmlEscaped(), elo("BlackElo"), where.join(QStringLiteral(" · ")).toHtmlEscaped());
    if (const std::string intro = pgn::withoutCommands(game.root()->comment); !intro.empty()) html += QStringLiteral("<p><i>%1</i></p>").arg(QString::fromStdString(intro).toHtmlEscaped());
    if (game.startPosition().fen() != Position::startPos().fen()) html += diagram(game.root());
    QString result = h("Result");
    result.replace(QStringLiteral("1/2-1/2"), QStringLiteral("½-½"));
    html += QStringLiteral("<p>%1<b>%2</b></p>").arg(line(game.root(), true), result.toHtmlEscaped());
    if (!h("Termination").isEmpty()) html += QStringLiteral("<p>%1</p>").arg(h("Termination").toHtmlEscaped());
    doc.setDefaultStyleSheet(QStringLiteral("body{font-family:'DejaVu Serif',serif;font-size:11pt;color:#000} h2{font-size:15pt;margin-bottom:2px} p{margin:6px 0;line-height:140%} i{color:#333}"));
    doc.setHtml(QStringLiteral("<body>%1</body>").arg(html));
    if (diagramCount) *diagramCount = diagrams;
}

bool MainWindow::exportGamePdf(const Game& game, const QString& toPath)
{
    QSettings s;
    QString path = toPath;
    if (path.isEmpty()) {
        QString name = QStringLiteral("%1 - %2").arg(QString::fromStdString(game.header("White")), QString::fromStdString(game.header("Black")));
        name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
        path = QFileDialog::getSaveFileName(this, tr("Exporter la partie en PDF"), s.value("lastDir").toString() + QLatin1Char('/') + name + QStringLiteral(".pdf"), tr("Document PDF (*.pdf)"));
        if (path.isEmpty()) return false;
        s.setValue("lastDir", QFileInfo(path).absolutePath());
    }
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageMargins(QMarginsF(18, 18, 18, 18), QPageLayout::Millimeter);
    pdf.setTitle(QStringLiteral("%1 – %2").arg(QString::fromStdString(game.header("White")), QString::fromStdString(game.header("Black"))));
    pdf.setCreator(QStringLiteral("Hydra64"));
    QTextDocument doc;
    fillGameDocument(doc, game);
    doc.print(&pdf);
    if (!QFileInfo::exists(path) || QFileInfo(path).size() == 0) {
        QMessageBox::warning(this, tr("Enregistrer"), tr("Impossible d'écrire %1").arg(path));
        return false;
    }
    statusBar()->showMessage(tr("Partie exportée dans %1").arg(QDir::toNativeSeparators(path)), 6000);
    return true;
}

void MainWindow::printGame(const Game& game)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(18, 18, 18, 18), QPageLayout::Millimeter);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Imprimer la partie"));
    if (dialog.exec() != QDialog::Accepted) return;
    QTextDocument doc;
    fillGameDocument(doc, game);
    doc.print(&printer);
}

// ---------------------------------------------------------------- opening repertoire

QString MainWindow::repertoirePath(chess::Color color)
{
    return userdirs::repertoire() + QLatin1Char('/') + (color == White ? QStringLiteral("White.pgn") : QStringLiteral("Black.pgn"));
}

bool MainWindow::loadRepertoire(chess::Color color, Game& out)
{
    QFile f(repertoirePath(color));
    out = Game();
    out.setHeader("Event", color == White ? "Répertoire des Blancs" : "Répertoire des Noirs");
    if (!f.open(QIODevice::ReadOnly)) return false;
    Game read;
    if (!pgn::parseGame(f.readAll().toStdString(), read)) return false;
    out = std::move(read);
    return true;
}

bool MainWindow::saveRepertoire(chess::Color color, const Game& game, QString* error)
{
    const QString path = repertoirePath(color);
    QDir().mkpath(QFileInfo(path).absolutePath());
    Game copy = copyGame(game);
    copy.setHeader("Event", color == White ? "Répertoire des Blancs" : "Répertoire des Noirs");
    copy.setHeader("Result", "*");
    QSaveFile f(path); // the old repertoire stays intact if writing fails
    if (!f.open(QIODevice::WriteOnly) || f.write(QByteArray::fromStdString(pgn::writeGame(copy))) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

int MainWindow::addLineToRepertoire(chess::Color color)
{
    const QString side = color == White ? tr("Blancs") : tr("Noirs");
    const Node* upTo = m_session->current();
    if (!upTo->parent) { // starting position displayed: the whole main line
        upTo = nullptr;
        if (!m_session->game().root()->mainChild()) {
            statusBar()->showMessage(tr("Aucun coup à ajouter au répertoire"), 5000);
            return -1;
        }
    }
    Game rep;
    loadRepertoire(color, rep);
    const int added = chess::repertoire::addLine(rep, m_session->game(), upTo);
    QString error;
    if (added < 0) {
        statusBar()->showMessage(tr("Le répertoire part de la position initiale : cette partie commence ailleurs"), 6000);
        return -1;
    }
    if (!saveRepertoire(color, rep, &error)) {
        QMessageBox::warning(this, tr("Répertoire d'ouvertures"), error);
        return -1;
    }
    statusBar()->showMessage(added ? tr("Répertoire des %1 : %2 coup(s) ajouté(s)").arg(side).arg(added) : tr("Répertoire des %1 : cette ligne y est déjà").arg(side), 6000);
    return added;
}

void MainWindow::openRepertoire(chess::Color color)
{
    Game rep;
    const bool exists = loadRepertoire(color, rep);
    if (m_page != PageAnalysis) setPage(PageAnalysis);
    m_session->loadGame(std::move(rep));
    statusBar()->showMessage(exists ? tr("Répertoire ouvert : modifiez-le, puis Partie → Répertoire d'ouvertures → Enregistrer…")
                                    : tr("Répertoire encore vide : jouez vos lignes, puis Partie → Répertoire d'ouvertures → Enregistrer…"),
                             10000);
}

QString MainWindow::repertoireReport(chess::Color color, bool show)
{
    const QString side = color == White ? tr("Blancs") : tr("Noirs");
    Game rep;
    if (!loadRepertoire(color, rep) || !rep.root()->mainChild()) {
        if (show) QMessageBox::information(this, tr("Répertoire d'ouvertures"), tr("Le répertoire des %1 est vide. Ajoutez-y des lignes : Partie → Répertoire d'ouvertures.").arg(side));
        return {};
    }
    GameListModel* model = m_games->model();
    const QList<int> rows = m_games->visibleRows();
    chess::repertoire::Report report(rep, color);
    const bool done = runWithProgress(tr("Répertoire d'ouvertures"), tr("Lecture des parties affichées…"), [&](std::atomic<int>& permille, std::atomic<bool>& cancel, QString&) {
        for (int k = 0; k < rows.size() && !cancel.load(); ++k) {
            Game g;
            if (model->parseGame(rows[k], g, nullptr)) report.add(g, rows[k]);
            permille = int(qint64(k + 1) * 1000 / std::max<qsizetype>(1, rows.size()));
        }
        return true;
    }, true);
    if (!done) return {};
    const auto gaps = report.gaps();
    const QLocale loc;
    QString h = QStringLiteral("<h2>%1</h2>").arg(tr("Répertoire des %1 face à « %2 »").arg(side, QFileInfo(model->filePath()).completeBaseName()).toHtmlEscaped());
    h += tr("<p>%1 parties lues, %2 entièrement dans le répertoire, %3 écart(s) différents.</p>").arg(loc.toString(report.gamesSeen()), loc.toString(report.gamesInside())).arg(gaps.size());
    auto table = [&](chess::repertoire::Gap::Kind kind, const QString& title, const QString& note) {
        QString t;
        int shown = 0;
        for (const auto& g : gaps) {
            if (g.kind != kind) continue;
            if (++shown > 200) break;
            const int total = g.whiteWins + g.draws + g.blackWins;
            const double score = total ? 100.0 * (color == White ? g.whiteWins + 0.5 * g.draws : g.blackWins + 0.5 * g.draws) / total : 0;
            t += QStringLiteral("<tr><td>%1</td><td><b>%2</b></td><td>%3</td><td align=right>%4</td><td align=right>%5</td><td>%6</td></tr>")
                     .arg(QString::fromStdString(g.line).toHtmlEscaped(), QString::fromStdString(g.move).toHtmlEscaped(), QString::fromStdString(g.known).toHtmlEscaped())
                     .arg(g.games)
                     .arg(total ? QStringLiteral("%1 %").arg(loc.toString(score, 'f', 0)) : QStringLiteral("–"),
                          g.ids.empty() ? QString() : QStringLiteral("<a href=\"g:%1\">%2</a>").arg(g.ids.front()).arg(tr("voir")));
        }
        if (t.isEmpty()) return QString();
        return QStringLiteral("<h3>%1</h3><p>%2</p><table cellspacing=0 cellpadding=4><tr><th align=left>%3</th><th align=left>%4</th><th align=left>%5</th><th>%6</th><th>%7</th><th></th></tr>%8</table>")
            .arg(title, note, tr("Ligne"), tr("Coup joué"), tr("Prévu par le répertoire"), tr("Parties"), tr("Mon score"), t);
    };
    h += table(chess::repertoire::Gap::Uncovered, tr("Coups adverses non prévus"), tr("L'adversaire a joué un coup que le répertoire ne traite pas."));
    h += table(chess::repertoire::Gap::LineEnds, tr("Lignes à prolonger"), tr("Le répertoire s'arrête, les parties continuent."));
    h += table(chess::repertoire::Gap::Deviation, tr("Mes écarts"), tr("Le coup joué par les %1 n'est pas celui du répertoire.").arg(side));
    if (gaps.empty()) h += tr("<p>Aucun écart : toutes les parties lues suivent le répertoire.</p>");
    if (!show) return h;
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Répertoire d'ouvertures"));
    dlg->resize(900, 640);
    auto* view = new QTextBrowser;
    view->setOpenLinks(false);
    view->setHtml(h);
    connect(view, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == QLatin1String("g")) previewGame(url.path().toInt());
    });
    auto* layout = new QVBoxLayout(dlg);
    layout->addWidget(view);
    dlg->show();
    return h;
}

bool MainWindow::exportGameHtml(const Game& game, const QString& toPath)
{
    QSettings s;
    QString path = toPath;
    if (path.isEmpty()) {
        QString name = QStringLiteral("%1 - %2").arg(QString::fromStdString(game.header("White")), QString::fromStdString(game.header("Black")));
        name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
        path = QFileDialog::getSaveFileName(this, tr("Exporter la partie en page HTML"), s.value("lastDir").toString() + QLatin1Char('/') + name + QStringLiteral(".html"), tr("Page HTML (*.html)"));
        if (path.isEmpty()) return false;
        s.setValue("lastDir", QFileInfo(path).absolutePath());
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Enregistrer"), f.errorString());
        return false;
    }
    f.write(tournamentHtml::gamePage(game).toUtf8());
    statusBar()->showMessage(tr("Partie exportée dans %1").arg(QDir::toNativeSeparators(path)), 6000);
    return true;
}

void MainWindow::exportGame()
{
    QSettings s;
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter la partie"), s.value("lastDir").toString(), tr("Parties PGN (*.pgn)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Enregistrer"), f.errorString());
        return;
    }
    f.write(QByteArray::fromStdString(pgn::writeGame(m_session->game())));
    statusBar()->showMessage(tr("Partie enregistrée dans %1").arg(path), 5000);
}

namespace {

QString sessionFile() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/session.json"); }

QJsonObject slotToJson(const GameSession* session, GameSession::Slot slot)
{
    const Game& g = session->gameIn(slot);
    const GameOrigin& o = session->originIn(slot);
    QJsonArray path;
    for (int i : GameSession::pathTo(g.current())) path.append(i);
    return QJsonObject{{"pgn", QString::fromStdString(pgn::writeGame(g))},
                       {"path", path},
                       {"origin", QJsonObject{{"path", o.path}, {"row", o.row}, {"savedHere", o.savedHere}}}};
}

} // namespace

void MainWindow::saveSession() const
{
    // Version 2: one game per page and the engine tournament. The engines' state (analysis in
    // progress) is not kept: no engine starts when the application opens.
    QJsonObject slotData;
    for (int i = 0; i < PageCount; ++i) slotData[pageKeys[i]] = slotToJson(m_session, GameSession::Slot(i));
    QJsonObject root{{"version", 2}, {"slotData", slotData}};
    const QJsonObject playState = m_play->saveState();
    if (!playState.isEmpty()) root["engineGame"] = playState;
    const QJsonObject tournament = m_tournament->saveState();
    if (!tournament.isEmpty()) root["tournament"] = tournament;
    QDir().mkpath(QFileInfo(sessionFile()).absolutePath());
    QSaveFile f(sessionFile()); // atomic write: never a half-written file
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void MainWindow::restoreSession()
{
    if (!prefs::restoreSession()) return;
    QFile f(sessionFile());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const int version = root.value("version").toInt();
    if (version != 1 && version != 2) return;
    // Version 1: only the Analysis and Play pages, at the root.
    const QJsonObject slotData = version == 2 ? root.value("slotData").toObject() : root;
    for (int i = 0; i < PageCount; ++i) {
        const QString key = version == 2 ? QString::fromLatin1(pageKeys[i]) : i == PageAnalysis ? QStringLiteral("analysis") : i == PageBoard ? QStringLiteral("play") : QString();
        const QJsonObject o = slotData.value(key).toObject();
        if (key.isEmpty() || o.isEmpty()) continue;
        Game g;
        pgn::parseGame(o.value("pgn").toString().toStdString(), g); // partial reading accepted
        std::vector<int> path;
        for (const QJsonValue& v : o.value("path").toArray()) path.push_back(v.toInt());
        const QJsonObject origin = o.value("origin").toObject();
        GameOrigin go{origin.value("path").toString(), origin.value("row").toInt(-1), origin.value("savedHere").toBool()};
        if (!go.path.isEmpty() && !QFileInfo::exists(go.path)) go = {};
        m_session->restoreSlot(GameSession::Slot(i), std::move(g), go, path);
    }
    // Game against the engine in progress on closing: it resumes, engine stopped (Play
    // button or Space to restart it).
    const QJsonObject playState = root.value("engineGame").toObject();
    if (!playState.isEmpty()) {
        QString error;
        if (!m_play->restoreState(playState, &error)) statusBar()->showMessage(tr("Partie en cours non reprise : %1").arg(error), 8000);
    }
    // Tournament: results and games played. An interrupted game will be replayed on resumption.
    m_tournament->restoreState(root.value("tournament").toObject());
    m_followLive = false; // the Tournament page displays the game it was showing
}

void MainWindow::restoreSettings()
{
    QSettings s;
    if (!restoreGeometry(s.value("geometry").toByteArray())) resize(1600, 960);

    if (s.value("layoutVersion").toInt() == LayoutVersion)
        for (int i = 0; i < PageCount; ++i) m_pageStates[size_t(i)] = s.value(QStringLiteral("layout/") + pageKeys[i]).toByteArray();
    // Revision 3: new default layout of the Play page (notation, evaluation,
    // engine visible). Revision 4: tab of the opponent engine on the Play page.
    // Only that page is reset, the others are kept.
    if (s.value("layoutRevision", 2).toInt() < 4) m_pageStates[PageBoard].clear();
    // Revision 5: no more Evaluation panel on the Tournament page.
    if (s.value("layoutRevision", 2).toInt() < 5) m_pageStates[PageTournament].clear();
    // Revision 6: Databases page — open databases above the game list, board and notation on the right.
    if (s.value("layoutRevision", 2).toInt() < 6) m_pageStates[PageDatabase].clear();
    // Revision 7: Analysis page — deep analysis and evaluation on the left; tree, tablebases, notation, engines on the right.
    if (s.value("layoutRevision", 2).toInt() < 7) m_pageStates[PageAnalysis].clear();
    // Revision 8: Play page — clock above the notation.
    if (s.value("layoutRevision", 2).toInt() < 8) m_pageStates[PageBoard].clear();
    // Revision 9: Tournament page — same panels, proportions revised (notation 18%, tournament 35%).
    if (s.value("layoutRevision", 2).toInt() < 9) m_pageStates[PageTournament].clear();

    // Analysis engine: the last one chosen, otherwise the fallback engine (built-in Stockfish).
    const EngineRegistry& registry = EngineRegistry::instance();
    EngineConfig engine = registry.engine(s.value("analysisEngine").toString());
    m_engine->setEngine(engine.isValid() ? engine : registry.fallbackEngine());
    m_engineRemoved = s.value("firstEngineRemoved", false).toBool();
    m_engine->setSides(EnginePanel::Sides(std::clamp(s.value("analysisSides", 0).toInt(), 0, 2)));
    restoreSpectators(); // before the layouts are restored, which place them
    renumberSpectators();

    // Board orientation, page by page (old common key as a fallback).
    const bool flipped = s.value("flipped", false).toBool();
    for (int i = 0; i < PageCount; ++i) m_pageFlipped[size_t(i)] = s.value(QStringLiteral("flipped/") + pageKeys[i], flipped).toBool();

    // Databases open in the last session (missing files are ignored).
    const QString db = s.value("lastDatabase").toString();
    QStringList dbs = s.value("databases").toStringList();
    if (!s.contains("databases") && !db.isEmpty()) dbs << db; // old configuration: a single database
    m_restoringDatabases = true;
    for (const QString& path : dbs)
        if (QFileInfo::exists(path)) openDatabase(path);
    if (!db.isEmpty()) m_games->activate(db);
    m_restoringDatabases = false;

    restoreSession(); // before displaying the page: each page gets its game back
    setPage(Page(std::clamp(s.value("page", int(PageDatabase)).toInt(), 0, PageCount - 1)));

    // Automatic save (crash, power cut…)
    auto* autosave = new QTimer(this);
    connect(autosave, &QTimer::timeout, this, &MainWindow::saveSession);
    autosave->start(60 * 1000);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_resetRequested) { // reset: nothing is saved, the engines stop
        m_closing = true;
        m_engine->engine()->quit();
        for (const Spectator& s : std::as_const(m_spectators)) s.panel->engine()->quit();
        m_play->stop();
        m_tournament->stop();
        m_autoEval->setEnabled(false);
        disconnect(&prefs::Notifier::instance(), nullptr, this, nullptr);
        e->accept();
        m_dockManager->deleteLater();
        return;
    }
    m_pageStates[m_page] = m_dockManager->saveState(LayoutVersion);
    m_pageFlipped[m_page] = m_viewer->isFlipped();
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("layoutVersion", LayoutVersion);
    s.setValue("layoutRevision", LayoutRevision);
    for (int i = 0; i < PageCount; ++i) {
        s.setValue(QStringLiteral("layout/") + pageKeys[i], m_pageStates[size_t(i)]);
        s.setValue(QStringLiteral("flipped/") + pageKeys[i], m_pageFlipped[size_t(i)]);
    }
    s.remove("flipped"); // old key common to all the pages
    s.setValue("page", int(m_page));
    m_games->tidyAll(); // single-file databases: journal tidied if it has grown (everything is already saved)
    saveSession(); // before stopping the games: they will resume at the next launch
    saveSpectators();
    m_engine->engine()->quit();
    for (const Spectator& s : std::as_const(m_spectators)) s.panel->engine()->quit();
    m_play->stop();
    m_tournament->stop();
    m_closing = true;
    m_autoEval->setEnabled(false);
    // The panels disappear with the manager: no more display updates.
    disconnect(&prefs::Notifier::instance(), nullptr, this, nullptr);
    e->accept();
    // Recommended by ADS: destroying the manager also closes the floating windows.
    m_dockManager->deleteLater();
}
