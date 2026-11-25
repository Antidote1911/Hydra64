// Interface tests: the real main window, off screen, with an isolated configuration
// (XDG_CONFIG_HOME / XDG_DATA_HOME point into the build folder, see CMakeLists).
// Opponent: Shuffle (embedded engine), set to reply without delay.
#include "engine/engineregistry.h"
#include "ui/autoevaluator.h"
#include "ui/boardviewer.h"
#include "ui/openings.h"
#include "ui/boardwidget.h"
#include "ui/bookpanel.h"
#include "ui/notationview.h"
#include <QFont>
#include <QListWidget>
#include <QMimeData>
#include <QKeyEvent>
#include <QDataStream>
#include <QContextMenuEvent>
#include "chess/polyglot.h"
#include "chess/ctg.h"
#include "chess/treebook.h"
#include "ui/clockpanel.h"
#include "ui/evalbar.h"
#include "ui/chessclock.h"
#include "ui/enginegame.h"
#include "ui/enginepanel.h"
#include "ui/gamesession.h"
#include "ui/mainwindow.h"
#include "ui/enginemanager.h"
#include "ui/playenginepanel.h"
#include "ui/tabengine.h"
#include "ui/modulewidgets.h"
#include "ui/boardtheme.h"
#include "ui/defaultsets.h"
#include "ui/piecerenderer.h"
#include "ui/indexdialog.h"
#include "chessbase/cbtexts.h"
#include "ui/display.h"
#include "ui/tablebases.h"
#include "chess/tablebase.h"
#include "engine/uciengine.h"
#include "ui/preferences.h"
#include "ui/quizdialog.h"
#include <QSplitter>
#include <QTextDocument>
#include <QTextEdit>
#include "ui/hy64source.h"
#include "chessbase/chessbase.h"
#ifdef HYDRA64_TEST_JS
#include <QJSEngine>
#endif
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "ui/tournamentoverview.h"
#include "ui/playerencyclopedia.h"
#include <QDialogButtonBox>
#include <QCheckBox>
#include "ui/positioneditor.h"
#include "ui/tournament.h"
#include "chess/pgn.h"
#include "ui/baseicons.h"
#include "ui/bundledbases.h"
#include "ui/userdirs.h"
#include <QDirIterator>
#include "ui/gamelist.h"
#include "ui/conversion.h"
#include "ui/convertdialog.h"
#include "ui/solutiontest.h"
#include "ui/annotator.h"
#include "ui/coach.h"
#include "ui/playerreport.h"
#include "ui/preparationdialog.h"
#include "ui/errorreport.h"
#include "ui/guessdialog.h"
#include "ui/pieceheatdialog.h"
#include "ui/namefixdialog.h"
#include "ui/assistedanalysis.h"
#include "chess/repertoire.h"
#include "ui/eventtable.h"
#include "ui/gamepreview.h"
#include "ui/autoplayer.h"
#include "ui/training.h"
#include "ui/addexercise.h"
#include "ui/collectioncheck.h"
#include "ui/trainingprogress.h"
#include "db/materialquery.h"
#include "ui/headersearchdialog.h"
#include "ui/filtergraph.h"
#include "ui/ecobrowser.h"
#include "ui/surveybrowser.h"
#include "ui/playdialog.h"
#include "ui/piecetracker.h"
#include "ui/calcvar.h"
#include "ui/openingreport.h"
#include <QTreeWidget>
#include <QSpinBox>
#include <QSvgRenderer>
#include "ui/tablebasepanel.h"
#include "ui/enginepanel.h"
#include "ui/deepanalysis.h"
#include "ui/deeppanel.h"
#include "ui/deepdialogs.h"
#include "engine/engineregistry.h"
#include "ui/solutiontestdialog.h"
#include "ui/bookpanel.h"
#include "db/convert.h"
#include "db/database.h"
#include "ui/participantdialog.h"
#include "ui/tournamentpanel.h"
#include "ui/theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QPushButton>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <QWindow>
#include <DockWidget.h>

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSettings>
#include <QFile>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <QSet>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QTableView>
#include <QHeaderView>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QJsonArray>
#include <QJsonObject>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace chess;

namespace {

int failures = 0;

// HYDRA64_TEST_TIMES=1: each line is preceded by the time elapsed since the previous check (ms),
// to spot the slow tests.
void check(bool ok, const char* what)
{
    static const bool times = qEnvironmentVariableIsSet("HYDRA64_TEST_TIMES");
    static QElapsedTimer since;
    if (times) {
        std::printf("%7lld ms  ", since.isValid() ? static_cast<long long>(since.restart()) : 0LL);
        if (!since.isValid()) since.start();
    }
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++failures;
}

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Will answer the next modal dialog box by clicking the `text` button.
// Waits (at most `ms`) for the condition to be true.
template <typename F>
bool waitFor(F&& cond, int ms)
{
    for (int t = 0; t < ms && !cond(); t += 50) wait(50);
    return cond();
}

// Menu of the menu bar whose title (without "&") is `title`: visible on the displayed page?
bool menuVisible(QMainWindow& w, const QString& title)
{
    for (QAction* a : w.menuBar()->actions())
        if (a->text().remove('&') == title) return a->isVisible();
    return false;
}

void answerNextDialog(const QString& text, bool* seen)
{
    QTimer::singleShot(50, [text, seen] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            *seen = true;
            for (QAbstractButton* b : box->buttons())
                if (b->text() == text) b->click();
        }
    });
}

// Like answerNextDialog, for a box that opens later (after an analysis): we watch for it
// up to `timeoutMs`.
void answerDialogWhenShown(const QString& text, bool* seen, int timeoutMs = 20000)
{
    auto* timer = new QTimer;
    auto* clock = new QElapsedTimer;
    clock->start();
    QObject::connect(timer, &QTimer::timeout, [timer, clock, text, seen, timeoutMs] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box) {
            *seen = true;
            for (QAbstractButton* b : box->buttons())
                if (b->text() == text) b->click();
        }
        if (box || clock->elapsed() > timeoutMs) {
            timer->stop();
            timer->deleteLater();
            delete clock;
        }
    });
    timer->start(50);
}

} // namespace

int main(int argc, char** argv)
{
    // Safety: configuration and data ALWAYS isolated in the test's folder, even when run
    // by hand — the test erases these folders, it must never touch the user's.
    const QByteArray home = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toLocal8Bit() + "/ui_tests_home";
    qputenv("XDG_CONFIG_HOME", home + "/config");
    qputenv("XDG_DATA_HOME", home + "/data");
    qputenv("HYDRA64_HOME", home + "/hydra64"); // user folder (databases, books, themes…) isolated too
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Hydra64"));
    QApplication::setApplicationName(QStringLiteral("Hydra64"));
    // Fresh configuration at each run: the whole isolated folder ui_tests_home is emptied
    // (Hydra64.conf settings included), never anything outside it.
    const QString isolated = QString::fromLocal8Bit(home);
    if (!isolated.endsWith(QLatin1String("/ui_tests_home")) || !QSettings().fileName().startsWith(isolated)) {
        std::printf("[FAIL] configuration non isolée, rien n'est effacé : %s\n", qPrintable(QSettings().fileName()));
        return 1;
    }
    QDir(isolated).removeRecursively();
    // Documents folder isolated too (deep analyses, converted databases and trees…)
    QDir().mkpath(isolated + QStringLiteral("/config"));
    QDir().mkpath(isolated + QStringLiteral("/documents"));
    {
        QFile dirs(isolated + QStringLiteral("/config/user-dirs.dirs"));
        if (dirs.open(QIODevice::WriteOnly)) dirs.write(QByteArray("XDG_DOCUMENTS_DIR=\"") + isolated.toUtf8() + "/documents\"\n");
    }
    if (!QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).startsWith(isolated)) {
        std::printf("[FAIL] dossier Documents non isolé : %s\n", qPrintable(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)));
        return 1;
    }
    // Sets folder of a previous version, before any access to the sets (see "old folders moved")
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Hydra64/Pieces/svg/Ancien"));
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Hydra64/Boards/png/Ancien echiquier"));
    theme::apply(app);

    // Piece sets (SVG): each draws the twelve pieces
    check(PieceRenderer::currentSet() == QLatin1String(PieceRenderer::DefaultSet), "jeu de pièces par défaut : Hydra64");
    for (const PieceRenderer::PieceSet& set : PieceRenderer::availableSets()) {
        PieceRenderer r;
        r.setFixedSet(set.id);
        bool all = true;
        for (Color c : {White, Black})
            for (PieceType t : {King, Queen, Rook, Bishop, Knight, Pawn}) {
                const QImage img = r.pixmap(makePiece(c, t), 48, 1.0).toImage();
                bool drawn = false;
                for (int y = 0; y < img.height() && !drawn; ++y)
                    for (int x = 0; x < img.width() && !drawn; ++x) drawn = qAlpha(img.pixel(x, y)) > 0;
                all = all && drawn;
            }
        check(all, qPrintable(QStringLiteral("jeu de pièces %1 : douze pièces dessinées").arg(set.name)));
    }
    {
        // Folders of a previous version (directly in Documents/Hydra64): moved into Themes at launch
        const QString oldPieces = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Hydra64/Pieces/svg/Ancien"); // created at the beginning of the tests
        const int sets = int(PieceRenderer::availableSets().size());
        QFile page(defaultsets::themesDirectory() + QStringLiteral("/format.html"));
        const QString html = page.open(QIODevice::ReadOnly) ? QString::fromUtf8(page.readAll()) : QString();
        // As many sets as folders shipped in resources/sets, plus the default set
        const int shipped = int(QDir(QStringLiteral(HYDRA64_SOURCE_DIR "/resources/sets/Pieces")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
        check(shipped > 0 && sets == shipped + 1 && defaultsets::missing() == 0 && PieceRenderer::userDirectory().endsWith(QStringLiteral("/ui_tests_home/hydra64/themes/Pieces"))
                  && boardtheme::userDirectory().endsWith(QStringLiteral("/ui_tests_home/hydra64/themes/Boards")) && !QFileInfo::exists(oldPieces)
                  && QFileInfo(PieceRenderer::userDirectory() + QStringLiteral("/Ancien")).isDir() && QFileInfo(PieceRenderer::userDirectory() + QStringLiteral("/Celtic/wK.svg")).isFile() && !QFileInfo::exists(PieceRenderer::userDirectory() + QStringLiteral("/svg"))
                  && QFileInfo(boardtheme::userDirectory() + QStringLiteral("/Ancien echiquier")).isDir() && !QFileInfo::exists(boardtheme::userDirectory() + QStringLiteral("/png"))
                  && html.contains(QStringLiteral("data-lang=\"fr\"")) && html.contains(QStringLiteral("data-lang=\"en\"")) && html.contains(QStringLiteral("--accent:#e68a35"))
                  && html.contains(QStringLiteral("light.svg")) && html.contains(QStringLiteral("CREDITS.txt")),
              "jeux livrés : copiés dans le dossier de l'utilisateur (themes/) au premier lancement, avec format.html (FR/EN) ; anciens dossiers déplacés");
    }
    prefs::setPieceSet(QStringLiteral("user:Celtic"));
    const bool chosenSet = PieceRenderer::currentSet() == QLatin1String("user:Celtic");
    prefs::setPieceSet(QStringLiteral("fantasy")); // identifiers of old versions
    const bool oldId = PieceRenderer::currentSet() == QLatin1String("user:Fantasy");
    prefs::setPieceSet(QStringLiteral("user:svg/Spatial"));
    const bool oldSvg = PieceRenderer::currentSet() == QLatin1String("user:Spatial");
    prefs::setPieceSet(QStringLiteral("user:png/Papercut"));
    check(chosenSet && oldId && oldSvg && PieceRenderer::currentSet() == QLatin1String("user:Papercut"), "le jeu de pièces choisi est appliqué (anciens identifiants reconnus)");
    {
        // Eight-digit colour (#999999ff, Firi set): light grey, and not black as Qt reads it on its own
        PieceRenderer firi;
        firi.setFixedSet(QStringLiteral("user:Firi"));
        const QImage knight = firi.pixmap(makePiece(White, Knight), 240, 1.0).toImage();
        // Reference: the same file drawn by Qt without the fix (the gradient ends in black there)
        QImage raw(240, 240, QImage::Format_ARGB32_Premultiplied);
        raw.fill(Qt::transparent);
        {
            QSvgRenderer plain(PieceRenderer::userDirectory() + QStringLiteral("/Firi/wN.svg"));
            QPainter rp(&raw);
            plain.render(&rp, QRectF(0, 0, 240, 240));
        }
        auto darkShare = [](const QImage& img) { // share of the opaque pixels that are dark
            int dark = 0, opaque = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (const QColor c = img.pixelColor(x, y); c.alpha() > 200) ++opaque, dark += c.lightness() < 90;
            return opaque ? 100 * dark / opaque : 100;
        };
        const int fixed = darkShare(knight), unfixed = darkShare(raw);
        check(fixed + 5 < unfixed, qPrintable(QStringLiteral("jeu Firi : dégradé des pièces blanches rendu clair (%1 % de pixels sombres, %2 % sans correction)").arg(fixed).arg(unfixed)));
    }
    prefs::setPieceSet(QStringLiteral("inconnu"));
    check(PieceRenderer::currentSet() == QLatin1String(PieceRenderer::DefaultSet), "jeu de pièces inconnu : jeu par défaut");
    prefs::setPieceSet(QString::fromLatin1(PieceRenderer::DefaultSet));

    // Boards: each image is loaded, a8 light and b8 dark
    check(boardtheme::current() == QLatin1String(boardtheme::DefaultTheme), "échiquier par défaut : Hydra64");
    for (const boardtheme::Theme& t : boardtheme::available()) {
        const QColor light = boardtheme::lightColor(t.id), dark = boardtheme::darkColor(t.id);
        QPixmap board(64, 64);
        board.fill(Qt::magenta);
        QPainter bp(&board);
        boardtheme::paint(bp, QRectF(0, 0, 64, 64), 1.0, t.id);
        bp.end();
        const bool covered = board.toImage().pixelColor(60, 60) != QColor(Qt::magenta);
        check(covered && light.lightness() > dark.lightness(), qPrintable(QStringLiteral("échiquier %1 : image chargée, cases claires et foncées").arg(t.name)));
    }
    check(boardtheme::available().size() == 2 + QDir(QStringLiteral(HYDRA64_SOURCE_DIR "/resources/sets/Boards")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(),
          "échiquiers livrés : copiés dans Documents au premier lancement");
    prefs::setBoardTheme(QStringLiteral("user:Metal"));
    const bool chosenBoard = boardtheme::current() == QLatin1String("user:Metal");
    prefs::setBoardTheme(QStringLiteral("metal")); // identifiers of old versions
    const bool oldBoard = boardtheme::current() == QLatin1String("user:Metal");
    prefs::setBoardTheme(QStringLiteral("wood4"));
    const bool oldImage = boardtheme::current() == QLatin1String("user:Wood 4");
    prefs::setBoardTheme(QStringLiteral("user:png/Blue Marble"));
    check(chosenBoard && oldBoard && oldImage && boardtheme::current() == QLatin1String("user:Blue Marble"), "l'échiquier choisi est appliqué (anciens identifiants reconnus)");
    // Plain colours chosen in the Preferences
    prefs::setBoardTheme(QString::fromLatin1(boardtheme::ColorTheme));
    prefs::setBoardColors(QColor(0xee, 0xee, 0xd2), QColor(0x76, 0x96, 0x56));
    check(boardtheme::current() == QLatin1String(boardtheme::ColorTheme) && boardtheme::lightColor() == QColor(0xee, 0xee, 0xd2)
              && boardtheme::tile(1, 0, 8, 1.0).toImage().pixelColor(4, 4) == QColor(0x76, 0x96, 0x56),
          "échiquier aux couleurs unies : couleurs choisies appliquées");
    prefs::setBoardTheme(QStringLiteral("inconnu"));
    check(boardtheme::current() == QLatin1String(boardtheme::DefaultTheme), "échiquier inconnu : échiquier par défaut");
    prefs::setBoardTheme(QString::fromLatin1(boardtheme::DefaultTheme));

    // Embedded Shuffle (copied next to hydra64): fast opponent with no dependency
    EngineConfig shuffle;
    QString error;
    const QString shufflePath = QApplication::applicationDirPath() + QStringLiteral("/shuffle");
    if (!EngineRegistry::create(shufflePath, shuffle, &error)) {
        std::printf("[SKIP] Shuffle introuvable (%s) : tests d'interface ignorés\n", qPrintable(error));
        return 0;
    }
    shuffle.values[QStringLiteral("Move Delay")] = QStringLiteral("0");
    EngineRegistry::instance().add(shuffle);

    MainWindow w;
    w.resize(1400, 900);
    w.show();
    auto* session = w.findChild<GameSession*>();
    auto* board = w.findChild<BoardWidget*>();
    auto* game = w.findChild<EngineGame*>();
    auto play = [&](const char* uci) { emit board->moveRequested(session->current()->pos.parseUci(uci)); };

    auto plies = [&] { return session->game().mainline().size(); };
    auto firstMove = [&] { return plies() ? session->game().mainline().front()->san : std::string(); };

    // --- Analysis page: an analysis is prepared there
    w.setPage(MainWindow::PageAnalysis);
    play("e2e4");
    play("e7e5");
    check(plies() == 2 && firstMove() == "e4", "page Analyse : deux coups joués librement");

    // --- Play page: its own game, the analysis is not overwritten
    bool seen = false;
    answerNextDialog(QObject::tr("Continuer la partie"), &seen);
    w.setPage(MainWindow::PageBoard);
    wait(100);
    check(!seen && plies() == 0, "page Jouer : partie vierge, aucune question");
    PlaySettings ps;
    ps.engineId = shuffle.id;
    ps.baseMs = 60000; // clock: check the pause
    check(w.startEngineGame(ps), "partie chronométrée lancée contre Shuffle");
    wait(300);
    play("d2d4");
    wait(400);
    check(plies() == 2 && firstMove() == "d4", "Shuffle répond sur la page Jouer");

    // --- Back to Analysis: the analysis is intact, the game is paused
    w.setPage(MainWindow::PageAnalysis);
    wait(100);
    check(!seen, "changer de page ne demande rien");
    check(plies() == 2 && firstMove() == "e4", "page Analyse : l'analyse est intacte (1. e4 e5)");
    check(game->isActive() && game->isSuspended(), "la partie contre le moteur est en pause");
    const qint64 humanTime = game->clock()->remaining(White);
    wait(400);
    check(game->clock()->remaining(White) == humanTime && !game->clock()->isRunning(), "pendule arrêtée pendant la pause");
    play("g1f3");
    wait(400);
    check(plies() == 3 && session->current()->pos.sideToMove() == Black, "page Analyse : coup libre, personne ne répond");

    // --- Back to Play: the game resumes as it was
    w.setPage(MainWindow::PageBoard);
    wait(100);
    check(plies() == 2 && firstMove() == "d4" && !game->isSuspended(), "page Jouer : la partie est intacte");
    wait(300);
    check(!game->clock()->isRunning() && game->isPaused() && game->clock()->remaining(White) == humanTime && plies() == 2,
          "retour sur Jouer : rien ne repart seul, la pendule attend le coup du joueur");
    play("c2c4");
    wait(400);
    check(plies() == 4 && !game->isPaused(), "au coup du joueur la partie repart : Shuffle répond de nouveau");

    // --- Send to Analysis (the analysis already has moves: confirmation)
    seen = false;
    answerNextDialog(QObject::tr("Remplacer"), &seen);
    w.sendToAnalysis();
    check(seen, "« Envoyer dans Analyse » demande avant de remplacer l'analyse");
    check(plies() == 4 && firstMove() == "d4" && session->slot() == GameSession::AnalysisSlot,
          "la partie jouée est copiée dans Analyse");
    w.setPage(MainWindow::PageBoard);
    wait(100);
    check(plies() == 4 && game->isActive(), "la partie de la page Jouer est toujours là");

    // --- Closing then restarting: each page gets its game back, the game in progress resumes
    w.setPage(MainWindow::PageAnalysis);
    session->goTo(session->game().mainline()[1]); // move displayed in the analysis: 1… (2nd half-move)
    w.setPage(MainWindow::PageBoard);
    const qint64 whiteBefore = game->clock()->remaining(White), blackBefore = game->clock()->remaining(Black);
    w.close();
    wait(200);
    {
        MainWindow w2;
        w2.resize(1400, 900);
        w2.show();
        auto* s2 = w2.findChild<GameSession*>();
        auto* g2 = w2.findChild<EngineGame*>();
        auto* b2 = w2.findChild<BoardWidget*>();
        wait(300);
        check(s2->slot() == GameSession::PlaySlot && s2->game().mainline().size() == 4 && s2->game().mainline().front()->san == "d4",
              "relance : page Jouer restaurée avec sa partie");
        check(g2->isActive() && !g2->isSuspended() && g2->isTimed(), "relance : la partie contre Shuffle reprend");
        check(std::llabs(g2->clock()->remaining(White) - whiteBefore) < 1500 && std::llabs(g2->clock()->remaining(Black) - blackBefore) < 1500,
              "relance : pendules restituées");
        emit b2->moveRequested(s2->current()->pos.parseUci("b1c3"));
        wait(500);
        check(s2->game().mainline().size() == 6, "relance : Shuffle répond");
        {
            // End of game: banner with its choices, then rematch (colours reversed)
            auto* viewer = w2.findChild<BoardViewer*>();
            const Color before = g2->humanColor();
            g2->resign();
            wait(100);
            check(viewer->isBannerVisible(), "fin de partie : bandeau affiché");
            int actions = 0;
            for (QPushButton* b : viewer->findChildren<QPushButton*>())
                for (const char* t : {"Enregistrer dans la base", "Analyser la partie", "Revanche", "Nouvelle partie…"})
                    actions += b->isVisible() && b->text() == QString::fromUtf8(t);
            check(actions == 4, "fin de partie : enregistrer, analyser, revanche, nouvelle partie");
            check(w2.rematch() && g2->isActive() && g2->humanColor() != before, "revanche : nouvelle partie, couleurs inversées");
            wait(500);
            check(!viewer->isBannerVisible(), "revanche : le bandeau disparaît");
            check(s2->game().mainline().size() <= 1, "revanche : depuis la position de départ");
        }
        w2.setPage(MainWindow::PageAnalysis);
        check(s2->game().mainline().size() == 4 && s2->current() == s2->game().mainline()[1], "relance : page Analyse restaurée, même coup affiché");
        prefs::setRestoreSession(false);
        w2.close();
    }
    wait(200);
    {
        MainWindow w3;
        auto* s3 = w3.findChild<GameSession*>();
        check(s3->game().mainline().empty() && !w3.findChild<EngineGame*>()->isActive(), "option désactivée : rien n'est restauré");
        w3.close();
    }

    {
        const EngineConfig sf = AutoEvaluator::findStockfish();
        check(sf.path == QApplication::applicationDirPath() + QStringLiteral("/stockfish"), "évaluation automatique : Stockfish embarqué");
    }
    {
        // Built-in Stockfish: always in the engine manager, and not removable
        EngineRegistry& reg = EngineRegistry::instance();
        EngineConfig builtin;
        for (const EngineConfig& e : reg.engines())
            if (EngineRegistry::bundledKind(e) == QLatin1String("stockfish")) builtin = e;
        check(builtin.isValid() && builtin.name.contains(QStringLiteral("intégré")), "gestion des moteurs : Stockfish intégré présent");
        reg.remove(builtin.id);
        EngineConfig moved = reg.engine(builtin.id);
        check(moved.isValid(), "le Stockfish intégré ne peut pas être retiré");
        moved.path = QStringLiteral("/usr/bin/false");
        reg.update(moved);
        check(EngineRegistry::isBuiltin(reg.engine(builtin.id)), "le Stockfish intégré garde son exécutable");
        {
            EngineManagerDialog manager(nullptr, builtin.id);
            auto* about = manager.findChild<QLabel*>(QStringLiteral("EngineAbout"));
            const QString sf = about ? about->text() : QString();
            EngineConfig other = builtin;
            other.path = QStringLiteral("/usr/bin/false"); // an engine that is not shipped with Hydra64
            check(about && !about->isHidden() && sf.contains(QStringLiteral("moteur de référence")) && sf.contains(QStringLiteral("Force réglable (Elo) : oui"))
                      && EngineRegistry::bundledKind(builtin) == QLatin1String("stockfish") && EngineRegistry::bundledKind(other).isEmpty(),
                  "gestion des moteurs : descriptif du Stockfish intégré, aucun pour un moteur ajouté à la main");
        }
        // Shuffle, the other built-in engine: always listed, replies, cannot be removed; no other engine is shipped
        {
            const QString path = EngineRegistry::bundledPath(QStringLiteral("shuffle"));
            if (!QFileInfo::exists(path)) {
                std::printf("[SKIP] moteur intégré shuffle : non compilé\n");
            } else {
                EngineConfig shuffle;
                for (const EngineConfig& e : reg.engines())
                    if (EngineRegistry::bundledKind(e) == QLatin1String("shuffle")) shuffle = e;
                UciEngine engine;
                QString best;
                QObject::connect(&engine, &UciEngine::bestMoveReceived, [&](const QString& uci, const QString&) { best = uci; });
                const bool started = engine.start(path);
                engine.search(QString::fromStdString(chess::Position::startPos().fen()), QStringLiteral("depth 4"));
                const bool answered = started && waitFor([&] { return !best.isEmpty(); }, 20000) && !chess::Position::startPos().parseUci(best.toStdString()).isNull();
                engine.quit();
                const qsizetype before = reg.engines().size();
                reg.remove(shuffle.id);
                EngineManagerDialog manager(nullptr, shuffle.id);
                auto* about = manager.findChild<QLabel*>(QStringLiteral("EngineAbout"));
                check(shuffle.isValid() && EngineRegistry::isBuiltin(shuffle) && answered && reg.engines().size() == before &&
                          about && about->text().contains(QStringLiteral("moteur maison")),
                      qPrintable(QStringLiteral("moteur intégré Shuffle : listé (%1), joue %2, ne peut pas être retiré").arg(shuffle.name, best)));
            }
            // (the tests above registered Shuffle a second time themselves: entries are not counted)
            QStringList kinds;
            bool trial = false;
            for (const EngineConfig& e : reg.engines()) {
                if (EngineRegistry::isBuiltin(e) && !kinds.contains(EngineRegistry::bundledKind(e))) kinds << EngineRegistry::bundledKind(e);
                for (const char* old : {"berserk", "plentychess", "rodent", "shallowblue"}) trial = trial || QFileInfo(e.path).fileName().startsWith(QLatin1String(old));
            }
            kinds.sort();
            check(kinds == QStringList({QStringLiteral("shuffle"), QStringLiteral("stockfish")}) && !trial,
                  qPrintable(QStringLiteral("moteurs livrés : seuls Stockfish et Shuffle sont intégrés (%1), aucun moteur à l'essai").arg(kinds.join(QStringLiteral(", ")))));
        }
        // User folder: uci_engines/ for the user's engines (empty: the built-in ones stay next to the program), engines_tournaments/
        userdirs::install();
        check(QDir(userdirs::uciEngines()).exists() && QDir(userdirs::uciEngines()).isEmpty() && QDir(userdirs::tournaments()).exists() &&
                  userdirs::tournaments().endsWith(QStringLiteral("/engines_tournaments")) && Tournament::defaultPgnDir() == userdirs::tournaments(),
              "dossier utilisateur : uci_engines vide et engines_tournaments créés ; les tournois s'y enregistrent par défaut");
    }
    // Clock panel: time control label and format of the times
    check(ClockPanel::timeControlLabel(QStringLiteral("300+3")) == QStringLiteral("Blitz 5+3")
              && ClockPanel::timeControlLabel(QStringLiteral("60+0")) == QStringLiteral("Bullet 1+0")
              && ClockPanel::timeControlLabel(QStringLiteral("900+10")) == QStringLiteral("Rapide 15+10")
              && ClockPanel::timeControlLabel(QStringLiteral("?")).isEmpty(),
          "pendule : nom de la cadence (Bullet, Blitz, Rapide…)");
    check(ClockPanel::format(250000) == QStringLiteral("0:04:10") && ClockPanel::format(9400) == QStringLiteral("0:00:09.4"),
          "pendule : temps affichés 0:04:10, dixièmes sous 10 s");
    // ================= Independent pages, engine tab, tournament, menus =================
    prefs::setRestoreSession(true);
    QSettings().setValue("play/engine", shuffle.id); // "Play" and the analysis use Shuffle (fast)
    QSettings().setValue("analysisEngine", shuffle.id);
    const QString pgnPath = isolated + QStringLiteral("/base.pgn");
    {
        QFile f(pgnPath);
        if (!f.open(QIODevice::WriteOnly)) std::printf("[FAIL] impossible d'écrire %s\n", qPrintable(pgnPath));
        f.write("[White \"A\"]\n[Black \"B\"]\n[Result \"*\"]\n\n1. c4 e5 2. Nc3 *\n\n"
                "[White \"C\"]\n[Black \"D\"]\n[Result \"*\"]\n\n1. Nf3 d5 *\n");
    }
    // A second "engine" for the tournament (Shuffle, another configuration)
    EngineConfig shuffleB;
    EngineRegistry::create(shufflePath, shuffleB, &error);
    shuffleB.name = QStringLiteral("Shuffle B");
    shuffleB.values[QStringLiteral("Move Delay")] = QStringLiteral("0");
    EngineRegistry::instance().add(shuffleB);
    {
        MainWindow w4;
        w4.resize(1400, 900);
        w4.show();
        auto* s4 = w4.findChild<GameSession*>();
        auto* g4 = w4.findChild<EngineGame*>();
        auto* b4 = w4.findChild<BoardWidget*>();
        auto play4 = [&](const char* uci) { emit b4->moveRequested(s4->current()->pos.parseUci(uci)); };
        auto plies4 = [&] { return s4->game().mainline().size(); };
        auto first4 = [&] { return plies4() ? s4->game().mainline().front()->san : std::string(); };

        // --- Points 1 and 2: the Databases page has its own game
        w4.setPage(MainWindow::PageAnalysis);
        s4->newGame();
        play4("d2d4");
        check(w4.openDatabase(pgnPath), "base PGN ouverte");
        w4.loadGameFromDatabase(0);
        check(w4.page() == MainWindow::PageDatabase && plies4() == 3 && first4() == "c4", "partie de la base ouverte sur la page Bases de données");
        w4.setPage(MainWindow::PageAnalysis);
        check(plies4() == 1 && first4() == "d4", "aller sur Analyse n'écrase pas l'analyse");
        w4.setPage(MainWindow::PageDatabase);
        check(plies4() == 3 && first4() == "c4", "la page Bases de données a gardé sa partie");
        seen = false;
        answerNextDialog(QObject::tr("Remplacer"), &seen);
        w4.sendToAnalysis();
        check(seen && w4.page() == MainWindow::PageAnalysis && plies4() == 3 && first4() == "c4",
              "« Envoyer dans l'analyse » depuis la base (avec confirmation)");
        check(s4->origin().isValid() && s4->origin().row == 0, "la partie envoyée reste liée à la base");

        // --- Wheel on the board: navigation on all the pages
        {
            auto wheel = [&](int delta) {
                const QPointF at(b4->width() / 2.0, b4->height() / 2.0);
                QWheelEvent e(at, b4->mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(b4, &e);
            };
            for (MainWindow::Page p : {MainWindow::PageDatabase, MainWindow::PageAnalysis}) {
                w4.setPage(p);
                s4->toEnd();
                const int end = s4->current()->ply();
                wheel(120); // wheel up: previous move
                const bool back = s4->current()->ply() == end - 1;
                wheel(-120);
                check(back && s4->current()->ply() == end,
                      qPrintable(QStringLiteral("page %1 : la molette recule puis avance").arg(int(p))));
            }
        }

        // --- Several databases open (tabs), closing
        {
            auto* list = w4.gamesPanel();
            const QString second = QFileInfo(pgnPath).absolutePath() + QStringLiteral("/autre.pgn");
            QFile f(second);
            check(f.open(QIODevice::WriteOnly), "deuxième base créée");
            f.write("[White \"E\"]\n[Black \"F\"]\n[Result \"1-0\"]\n\n1. e4 e5 1-0\n");
            f.close();
            check(w4.openDatabase(second) && list->count() == 2 && list->model()->rowCount() == 1, "deux bases ouvertes, la nouvelle affichée");
            {
                auto* gamesDock = w4.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("games"));
                check(gamesDock && gamesDock->windowTitle() == QStringLiteral("BASES DE DONNÉES"),
                      "panneau : titre fixe « Bases de données » (le nom de la base est dans l'onglet)");
                QToolButton* plus = nullptr;
                for (QToolButton* b : list->basesWidget()->findChildren<QToolButton*>())
                    if (b->objectName() == QLatin1String("DatabaseOpen")) plus = b;
                bool asked = false;
                QObject::disconnect(list, &GameListPanel::openRequested, &w4, nullptr); // no real dialog box
                auto c = QObject::connect(list, &GameListPanel::openRequested, [&] { asked = true; });
                if (plus) plus->click();
                QObject::disconnect(c);
                check(plus && asked, "bouton « + » de la liste des bases : ouvrir une base");
                // Databases page: the open databases in a list (left panel), the game list in the
                // centre, the board in its panel; on another page the board goes back to the centre.
                auto* manager = w4.findChild<ads::CDockManager*>();
                auto* bases = list->basesWidget()->findChild<QListWidget*>(QStringLiteral("DatabaseList"));
                auto* boardDock = manager->findDockWidget(QStringLiteral("board"));
                auto* board = w4.findChild<BoardViewer*>();
                auto inside = [](const QWidget* w, const QWidget* ancestor) {
                    for (const QWidget* p = w; p; p = p->parentWidget())
                        if (p == ancestor) return true;
                    return false;
                };
                w4.setPage(MainWindow::PageDatabase);
                QApplication::processEvents();
                const bool listed = bases && bases->count() == 2 && bases->currentRow() == 1 && bases->item(0)->text() == QFileInfo(pgnPath).fileName();
                const bool onBases = boardDock && board && !boardDock->isClosed() && inside(board, boardDock) && inside(list, manager->centralWidget())
                                     && inside(list->basesWidget(), gamesDock) && !inside(list, gamesDock);
                if (bases) bases->setCurrentRow(0);
                QApplication::processEvents();
                const bool chosen = list->currentIndex() == 0 && list->model()->rowCount() == 2;
                if (bases) bases->setCurrentRow(1);
                w4.setPage(MainWindow::PageAnalysis);
                QApplication::processEvents();
                const bool onAnalysis = boardDock && board && boardDock->isClosed() && inside(board, manager->centralWidget()) && inside(list, gamesDock);
                w4.setPage(MainWindow::PageDatabase);
                QApplication::processEvents();
                const bool back = boardDock && board && inside(board, boardDock) && inside(list, manager->centralWidget()) && list->currentIndex() == 1;
                check(listed && onBases && chosen && onAnalysis && back,
                      qPrintable(QStringLiteral("page Bases : bases ouvertes en liste, parties au centre, échiquier en panneau%1%2%3%4%5")
                                     .arg(listed ? "" : " [liste]", onBases ? "" : " [page Bases]", chosen ? "" : " [choix]", onAnalysis ? "" : " [page Analyse]", back ? "" : " [retour]")));
            }
            check(w4.openDatabase(pgnPath) && list->count() == 2 && list->model()->rowCount() == 2, "rouvrir une base ouverte : son onglet, pas de doublon");
            list->closeDatabase(0);
            check(list->count() == 1 && QFileInfo(list->model()->filePath()) == QFileInfo(second), "fermer une base : l'autre reste affichée");
            list->closeCurrent();
            check(list->count() == 0 && list->model()->rowCount() == 0 && list->model()->filePath().isEmpty(), "dernière base fermée : liste vide");
            // Ctrl+S on the game linked to base.pgn, while base.pgn is closed and autre.pgn displayed:
            check(w4.openDatabase(pgnPath) && w4.openDatabase(second), "bases rouvertes");
            check(QFileInfo(list->model()->filePath()) == QFileInfo(second), "autre.pgn affichée");
            check(list->activate(second) && list->model()->rowCount() == 1 && list->activate(pgnPath), "activation par chemin");
            const QString basePgnForHy64Tests = pgnPath;
            {
                // PGN database: a click on a header sorts the list (White: A then C; second click: C then A)
                auto* view = list->findChild<QTableView*>();
                // (a click changes the header's sort indicator, which the table follows)
                view->horizontalHeader()->setSortIndicator(GameListModel::White, Qt::AscendingOrder);
                const QString first = view->model()->index(0, GameListModel::White).data().toString();
                view->horizontalHeader()->setSortIndicator(GameListModel::White, Qt::DescendingOrder);
                const QString firstAgain = view->model()->index(0, GameListModel::White).data().toString();
                view->sortByColumn(GameListModel::Number, Qt::AscendingOrder);
                check(view->isSortingEnabled() && first != firstAgain && QStringList{first, firstAgain}.contains(QStringLiteral("C")),
                      "base PGN : tri en cliquant sur un en-tête");
            }

            // --- ChessBase databases: no more direct opening (conversion offered). With a real database
            //     (HYDRA64_TEST_CBH) and its conversion (HYDRA64_TEST_HY64): same games.
            {
                const QString fakeCbh = QFileInfo(pgnPath).absolutePath() + QStringLiteral("/fausse.cbh");
                { QFile f(fakeCbh); check(f.open(QIODevice::WriteOnly), "fichier .cbh de test créé"); }
                const int before = list->count();
                bool asked = false;
                answerNextDialog(QObject::tr("Annuler"), &asked);
                check(!w4.openDatabase(fakeCbh) && asked && list->count() == before,
                      "base ChessBase : ouverture directe refusée, conversion proposée");
                // An opening tree or a binary file is not read as PGN (it gave unreadable rows)
                {
                    const QString dirT = QFileInfo(pgnPath).absolutePath();
                    const QString fakeTree = dirT + QStringLiteral("/faux.hy64book"), binary = dirT + QStringLiteral("/binaire.pgn"), noGame = dirT + QStringLiteral("/texte.pgn");
                    { QFile f(fakeTree); (void)f.open(QIODevice::WriteOnly); f.write(QByteArray("HY64BOOK\0\1\2[Event \"x\"]\n", 24)); }
                    { QFile f(binary); (void)f.open(QIODevice::WriteOnly); f.write(QByteArray("\x7f" "ELF\0\0\0[White \"x\"]\n", 20)); }
                    { QFile f(noGame); (void)f.open(QIODevice::WriteOnly); f.write("ceci n'est pas une partie\n"); }
                    bool askedTree = false;
                    answerNextDialog(QObject::tr("Annuler"), &askedTree);
                    check(!w4.openDatabase(fakeTree) && askedTree && list->count() == before, "arbre .hy64book : pas ouvert comme base, panneau Arbre proposé");
                    GameListModel refused;
                    QString whyT, whyB, whyN;
                    check(!refused.loadFile(fakeTree, &whyT) && !whyT.isEmpty() && !refused.loadFile(binary, &whyB) && !whyB.isEmpty()
                              && !refused.loadFile(noGame, &whyN) && !whyN.isEmpty() && refused.rowCount() == 0,
                          "arbre, fichier binaire, texte sans partie : refusés par la liste des parties");
                    for (const QString& p : {fakeTree, binary, noGame}) QFile::remove(p);
                }
                // Another file of the database (.cbg…) designates the .cbh beside it
                const QString fakeCbg = QFileInfo(pgnPath).absolutePath() + QStringLiteral("/fausse.cbg");
                { QFile f(fakeCbg); (void)f.open(QIODevice::WriteOnly); }
                ConvertDialog dlg(fakeCbg);
                check(QFileInfo(conversion::databaseFile(fakeCbg)) == QFileInfo(fakeCbh) && conversion::kindOf(fakeCbg) == conversion::Kind::Database
                          && conversion::isProprietary(fakeCbg) && QFileInfo(dlg.request().source) == QFileInfo(fakeCbh),
                      "Fichier → Convertir : un fichier .cbg de la base ChessBase désigne son .cbh");
                QFile::remove(fakeCbg);
                QFile::remove(fakeCbh);
            }
            const QString cbh = qEnvironmentVariable("HYDRA64_TEST_CBH");
            const QString converted = qEnvironmentVariable("HYDRA64_TEST_HY64");
            if (!ChessBaseSource::available()) {
                std::printf("[SKIP] lecture ChessBase non compilée\n");
            } else if (cbh.isEmpty() || converted.isEmpty() || !QFileInfo::exists(cbh)) {
                std::printf("[SKIP] base ChessBase : définir HYDRA64_TEST_CBH et HYDRA64_TEST_HY64 pour ces tests\n");
            } else {
                ChessBaseGameSource source;
                QString cbErr;
                hy64db::Database db;
                std::string err;
                check(source.open(cbh, &cbErr) && db.open(converted.toStdString(), &err), "base ChessBase réelle et sa conversion Hydra64 ouvertes");
                // No game is dropped any more: the numbers match one to one over the whole database.
                const int limit = std::min<int>(int(db.count()), qEnvironmentVariableIntValue("HYDRA64_TEST_HY64_ALIGNED") > 0
                                                                      ? qEnvironmentVariableIntValue("HYDRA64_TEST_HY64_ALIGNED") : int(db.count()));
                QRandomGenerator pickRng(9);
                int same = 0, tried = 0;
                QString firstBad;
                for (int i = 0; i < 20000; ++i) {
                    const int row = int(pickRng.bounded(quint32(limit)));
                    chess::Game a, b;
                    ++tried;
                    if (source.read(uint64_t(row), a, nullptr) && db.game(uint32_t(row), b) && chess::pgn::writeGame(a) == chess::pgn::writeGame(b)) ++same;
                    else if (firstBad.isEmpty()) firstBad = QString::number(row);
                }
                check(same == tried, qPrintable(QStringLiteral("conversion Hydra64 : %1/%2 parties identiques à la base ChessBase%3")
                                                   .arg(same).arg(tried).arg(firstBad.isEmpty() ? QString() : QStringLiteral(" (première différence : ligne %1)").arg(firstBad))));
            }

            // --- Hydra64 database (in-house format): conversion, list, sort, search, positions, tree,
            //     writing, deletion, compaction
            {
                const QString dir = QFileInfo(pgnPath).absolutePath() + QStringLiteral("/hy64");
                QDir().mkpath(dir);
                const QString hy64 = dir + QStringLiteral("/classiques.hy64i");
                hy64db::PgnSource src;
                std::string err;
                hy64db::ConvertResult cr;
                check(src.open(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn", &err) && hy64db::convert(src, hy64.toStdString(), {}, &cr, &err)
                          && cr.games == src.count(),
                      "Hydra64 : base convertie depuis un PGN");
                check(w4.openDatabase(hy64), "Hydra64 : base ouverte");
                GameListModel* m = list->model();
                const int n = m->totalGames();
                check(m->isIndexed() && !m->isReadOnly() && !m->isChessBase() && n == int(src.count()) && m->rowCount() == n,
                      "Hydra64 : base indexée, modifiable, toutes les parties");
                const auto cell = [&](int row, int col) { return m->data(m->index(row, col), Qt::DisplayRole); };
                chess::Game first;
                src.read(0, first, nullptr);
                check(cell(0, GameListModel::White).toString().toStdString() == first.header("White") && cell(0, GameListModel::Number).toInt() == 1,
                      "Hydra64 : en-tête de la première partie");
                w4.loadGameFromDatabase(0);
                check(chess::pgn::writeGame(s4->game()) == chess::pgn::writeGame(first), "Hydra64 : partie ouverte identique à la source");

                // Sort by header, search by name
                auto* header = list->findChild<QTableView*>()->horizontalHeader();
                emit header->sectionClicked(GameListModel::White);
                waitFor([&] { return !list->isSearching(); }, 10000);
                bool ascending = true;
                for (int r = 1; r < m->rowCount(); ++r)
                    if (cell(r - 1, GameListModel::White).toString().toLower() > cell(r, GameListModel::White).toString().toLower()) ascending = false;
                check(ascending, "Hydra64 : tri par Blancs en cliquant sur l'en-tête");
                emit header->sectionClicked(GameListModel::Number);
                emit header->sectionClicked(GameListModel::Number); // descending then…
                emit header->sectionClicked(GameListModel::Number); // …database order
                waitFor([&] { return !list->isSearching(); }, 10000);
                auto* filter = list->findChild<QLineEdit*>();
                const QString who = QString::fromStdString(first.header("White")).section(',', 0, 0);
                filter->setText(who);
                QMetaObject::invokeMethod(filter, "returnPressed");
                check(waitFor([&] { return !list->isSearching(); }, 10000) && m->rowCount() >= 1 && m->rowCount() <= n,
                      qPrintable(QStringLiteral("Hydra64 : recherche « %1 »").arg(who)));
                filter->clear();
                waitFor([&] { return !list->isSearching(); }, 10000);

                // Search by position: the position after 6 half-moves of the first game
                auto main = first.mainline();
                const chess::Position target = main.size() > 6 ? main[5]->pos : chess::Position::startPos();
                check(list->searchPosition(target) && waitFor([&] { return !list->isSearching(); }, 10000) && m->rowCount() >= 1
                          && cell(0, GameListModel::Number).toInt() == 1,
                      "Hydra64 : recherche de position (la partie d'origine est trouvée)");
                list->searchPosition(chess::Position::startPos());
                check(waitFor([&] { return !list->isSearching(); }, 10000) && m->rowCount() == n, "Hydra64 : position initiale = toutes les parties");
                filter->clear();
                waitFor([&] { return !list->isSearching(); }, 10000);

                // Tree in the Book panel
                auto* book = w4.bookPanel();
                s4->toStart();
                book->showDatabaseTree();
                int treeGames = 0;
                for (const auto& row : book->rows()) treeGames += row.games;
                check(book->format() == BookPanel::Database && !book->rows().isEmpty() && treeGames == n, "Hydra64 : arbre de la base (position initiale)");
                {
                    // Database → Advanced search: new search, AND, OR, invert, the whole database
                    using C = IndexedGameSource::Combine;
                    auto run = [&](const hy64db::HeaderQuery& q, C c) {
                        list->searchHeaders(q, c);
                        waitFor([&] { return !list->isSearching(); }, 10000);
                        return m->rowCount();
                    };
                    hy64db::HeaderQuery anderssen, kieser, paris;
                    anderssen.white = "anderssen";
                    kieser.black = "kieser";
                    paris.site = "paris";
                    const int r1 = run(anderssen, C::Replace), r2 = run(kieser, C::Within), r3 = run(paris, C::Add);
                    list->negateFilter();
                    waitFor([&] { return !list->isSearching(); }, 10000);
                    const int r4 = m->rowCount();
                    list->clearFilter();
                    waitFor([&] { return !list->isSearching(); }, 10000);
                    check(r1 == 2 && r2 == 1 && r3 == 2 && r4 == n - 2 && m->rowCount() == n,
                          qPrintable(QStringLiteral("recherche avancée (Hydra64) : nouvelle, ET, OU, inverser, toute la base (%1 %2 %3 %4)").arg(r1).arg(r2).arg(r3).arg(r4)));
                    {
                        // Search by material and patterns (tab of the advanced search): without queens
                        // for 2 half-moves, compared with replaying the games
                        HeaderSearchDialog dlg(&w4);
                        dlg.showMaterialTab();
                        dlg.findChild<QSpinBox*>(QStringLiteral("max0%1").arg(int(chess::Queen)))->setValue(0);
                        dlg.findChild<QSpinBox*>(QStringLiteral("max1%1").arg(int(chess::Queen)))->setValue(0);
                        hy64db::HeaderQuery mq = dlg.query();
                        mq = hy64db::HeaderQuery{}; // only the material criteria (the remembered headers are ignored)
                        mq.material = dlg.query().material;
                        mq.material.minPlies = 2;
                        int expected = 0;
                        for (int row = 0; row < n; ++row) {
                            chess::Game g;
                            if (!m->parseGame(row, g, nullptr)) continue;
                            hy64db::MaterialMatcher mm(mq.material);
                            int ply = 0;
                            if (mm.feed(g.root()->pos, 0))
                                for (const chess::Node* nd : g.mainline())
                                    if (!mm.feed(nd->pos, ++ply)) break;
                            expected += mm.matched();
                        }
                        const int got = run(mq, C::Replace);
                        list->clearFilter();
                        waitFor([&] { return !list->isSearching(); }, 10000);
                        // Filter charts: distribution by year and by Elo, all the games counted
                        {
                            std::vector<IndexedGameSource::Summary> sums;
                            for (int row = 0; row < m->rowCount(); ++row) {
                                IndexedGameSource::Summary su;
                                if (m->summary(row, su)) sums.push_back(su);
                            }
                            int noYear = 0, noElo = 0, inYears = 0, inElo = 0;
                            for (const auto& b : FilterGraphDialog::byYear(sums, &noYear)) inYears += b.total();
                            for (const auto& b : FilterGraphDialog::byElo(sums, &noElo)) inElo += b.total();
                            FilterGraphDialog* fg = w4.filterGraph();
                            const bool opened = fg && fg->findChildren<BucketChart*>().size() == 2;
                            if (fg) fg->close();
                            check(int(sums.size()) == n && inYears + noYear == n && inElo + noElo == n && inYears > 0 && opened,
                                  qPrintable(QStringLiteral("graphiques du filtre : %1 parties par année, %2 par Elo (%3 / %4 sans)").arg(inYears).arg(inElo).arg(noYear).arg(noElo)));
                        }
                        // Opening report: initial position, Hydra64 database (index), HTML export
                        {
                            s4->toStart();
                            OpeningReportDialog* rd = w4.openingReport(100);
                            const OpeningReport rep = rd ? rd->report() : OpeningReport{};
                            int continued = 0;
                            for (const auto& mv : rep.moves) continued += mv.games;
                            const QString out = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/rapport-test.html");
                            QString rerr;
                            const bool exported = rd && rd->exportHtml(out, &rerr) && QFileInfo(out).size() > 500;
                            QFile::remove(out);
                            if (rd) rd->close();
                            list->clearFilter();
                            waitFor([&] { return !list->isSearching(); }, 10000);
                            check(rd && rep.games == n && rep.sampled == n && continued == n && !rep.structures.empty() && !rep.whitePlayers.empty()
                                      && rd->html().contains(QStringLiteral("Coups suivants")) && exported,
                                  qPrintable(QStringLiteral("rapport d'ouverture : %1 parties, %2 coups suivants, %3 structures, export HTML")
                                                 .arg(rep.games).arg(rep.moves.size()).arg(rep.structures.size())));
                        }
                        // ECO browser: volumes A–E, codes, counts of the database, filter on a code
                        {
                            EcoBrowser* eb = w4.ecoBrowser();
                            QTreeWidget* tw = eb->tree();
                            int total = 0;
                            for (int i = 0; i < tw->topLevelItemCount(); ++i) total += tw->topLevelItem(i)->text(2).toInt();
                            QTreeWidgetItem* c = tw->topLevelItem(2)->child(4); // C4x
                            QTreeWidgetItem* c41 = nullptr;
                            for (int i = 0; c && i < c->childCount(); ++i)
                                if (c->child(i)->text(0) == QStringLiteral("C41")) c41 = c->child(i);
                            const auto range = EcoBrowser::rangeOf(c);
                            bool filtered = false;
                            QObject::connect(eb, &EcoBrowser::filterRequested, &w4, [&](int, int) { filtered = true; });
                            if (c) emit eb->filterRequested(range.first, range.second);
                            waitFor([&] { return !list->isSearching(); }, 10000);
                            const int inC4 = m->rowCount();
                            list->clearFilter();
                            waitFor([&] { return !list->isSearching(); }, 10000);
                            check(tw->topLevelItemCount() == 5 && total == n && c41 && c41->childCount() > 0 && !c41->text(1).isEmpty()
                                      && range == std::pair<int, int>{hy64db::parseEco("C40"), hy64db::parseEco("C49")} && filtered
                                      && inC4 == c->text(2).toInt(),
                                  qPrintable(QStringLiteral("navigateur ECO : 5 volumes, C41 « %1 », %2 partie(s) en C4x filtrées").arg(c41 ? c41->text(1) : QString()).arg(inC4)));
                            eb->close();
                        }
                        check(!mq.material.isEmpty() && got == expected,
                              qPrintable(QStringLiteral("recherche par matériel : parties sans dames (%1 sur %2, comme le rejeu)").arg(got).arg(n)));
                    }
                    // Same sequence on a PGN database (filter of the view)
                    const QString activeBefore = list->model()->filePath();
                    check(w4.openDatabase(QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn")), "recherche avancée : base PGN ouverte");
                    QAbstractItemModel* view = list->findChild<QTableView*>()->model();
                    {
                        // Site, Round, Moves columns (hidden by default, can be shown): PGN then Hydra64
                        GameListModel* pm = list->model();
                        // (by default: Site and Moves displayed, Round hidden)
                        const bool hiddenByDefault = list->isColumnVisible(GameListModel::Site) && list->isColumnVisible(GameListModel::Moves) && !list->isColumnVisible(GameListModel::Round);
                        list->setColumnVisible(GameListModel::Moves, false);
                        const bool hides = !list->isColumnVisible(GameListModel::Moves);
                        list->setColumnVisible(GameListModel::Moves, true);
                        list->setColumnVisible(GameListModel::Round, true);
                        const bool shown = hides && list->isColumnVisible(GameListModel::Moves) && list->isColumnVisible(GameListModel::Round);
                        {
                            // "Round" last takes the remaining width; hidden, it is Event. "Moves" stays narrow.
                            QHeaderView* hh = list->findChild<QTableView*>()->horizontalHeader();
                            const bool roundLast = hh->visualIndex(GameListModel::Round) == GameListModel::ColumnCount - 1 && hh->sectionSize(GameListModel::Moves) < 120
                                                   && !hh->stretchLastSection();
                            check(roundLast, "colonnes : Ronde en dernier, Coups reste étroite, aucune colonne étirée d'office");
                        }
                        list->setColumnVisible(GameListModel::Round, false);
                        const QString site = pm->data(pm->index(0, GameListModel::Site), Qt::DisplayRole).toString();
                        const int moves = pm->data(pm->index(0, GameListModel::Moves), Qt::DisplayRole).toInt();
                        const QString hy64Site = m->data(m->index(0, GameListModel::Site), Qt::DisplayRole).toString();
                        const int hy64Moves = m->data(m->index(0, GameListModel::Moves), Qt::DisplayRole).toInt();
                        check(hiddenByDefault && shown && site == QStringLiteral("Paris FRA") && moves == 17 && hy64Site == site && hy64Moves == 17,
                              qPrintable(QStringLiteral("colonnes Lieu / Coups : %1, %2 coups (PGN et Hydra64), affichables").arg(site).arg(moves)));
                    }
                    list->searchHeaders(anderssen, C::Replace);
                    const int p1 = view->rowCount();
                    list->searchHeaders(kieser, C::Within);
                    const int p2 = view->rowCount();
                    list->searchHeaders(paris, C::Add);
                    const int p3 = view->rowCount();
                    list->negateFilter();
                    const int p4 = view->rowCount();
                    list->clearFilter();
                    check(p1 == 2 && p2 == 1 && p3 == 2 && p4 == 1 && view->rowCount() == 3,
                          qPrintable(QStringLiteral("recherche avancée (PGN) : nouvelle, ET, OU, inverser, toute la base (%1 %2 %3 %4)").arg(p1).arg(p2).arg(p3).arg(p4)));
                    {
                        // Opening report on the PGN database (no index: replay of the games)
                        s4->toStart();
                        OpeningReportDialog* rp = w4.openingReport(100);
                        check(rp && rp->report().games == view->rowCount() && rp->report().sampled == rp->report().games,
                              qPrintable(QStringLiteral("rapport d'ouverture (PGN, rejeu) : %1 parties").arg(rp ? rp->report().games : -1)));
                        if (rp) rp->close();
                    }
                    list->closeCurrent();
                    list->activate(activeBefore);
                    m = list->model();
                }
                {
                    // Random game, game no. N (even if a search hides it)
                    const int r = list->randomRow();
                    hy64db::HeaderQuery only;
                    only.white = "anderssen";
                    list->searchHeaders(only, IndexedGameSource::Combine::Replace);
                    waitFor([&] { return !list->isSearching(); }, 10000);
                    const int hidden = list->rowOfNumber(1); // Morphy: outside the result → the whole database
                    check(r >= 0 && r < n && hidden == 0 && m->rowCount() == n && list->rowOfNumber(n) == n - 1 && list->rowOfNumber(n + 5) < 0,
                          "base : partie au hasard, partie n° N (recherche effacée si besoin)");
                    // Flags: "Tactique" on game no. 2, visible in the list, searched, removed
                    QString flagErr;
                    const bool set = m->setUserFlag({1}, 3, true, &flagErr);
                    const QString number = m->data(m->index(1, GameListModel::Number), Qt::DisplayRole).toString();
                    hy64db::HeaderQuery tactics;
                    tactics.userFlags = 1 << 3;
                    list->searchHeaders(tactics, IndexedGameSource::Combine::Replace);
                    waitFor([&] { return !list->isSearching(); }, 10000);
                    const int flagged = m->rowCount();
                    list->clearFilter();
                    waitFor([&] { return !list->isSearching(); }, 10000);
                    const bool cleared = m->setUserFlag({1}, 3, false, &flagErr) && m->userFlags(1) == 0;
                    check(set && number.endsWith(QStringLiteral(" T")) && flagged == 1 && cleared,
                          qPrintable(QStringLiteral("drapeaux : posé (%1), cherché (%2 partie), retiré").arg(number).arg(flagged)));
                    // Novelty: game no. 1 up to the 4th half-move, then a move never played in the database
                    w4.setPage(MainWindow::PageAnalysis);
                    auto* sn = w4.findChild<GameSession*>();
                    chess::Game keptNov;
                    chess::pgn::parseGame(chess::pgn::writeGame(sn->game()), keptNov);
                    const GameOrigin keptNovOrigin = sn->origin();
                    chess::Game ref;
                    m->parseGame(0, ref, nullptr);
                    chess::Game test;
                    const std::vector<chess::Node*> refLine = ref.mainline();
                    for (int k = 0; k < 4; ++k) test.playMove(refLine[size_t(k)]->move);
                    test.playMove(test.current()->pos.parseSan("a3")); // 3.a3: never played in the database
                    sn->loadGame(std::move(test));
                    QString novMsg;
                    const chess::Node* nov = w4.findNovelty(&novMsg);
                    check(nov && nov->ply() == 5 && std::find(nov->nags.begin(), nov->nags.end(), 146) != nov->nags.end() && novMsg.contains(QStringLiteral("3.a3")),
                          qPrintable(QStringLiteral("nouveauté trouvée au 5e demi-coup (%1)").arg(novMsg)));
                    sn->loadGame(std::move(keptNov));
                    sn->setOrigin(keptNovOrigin);
                    w4.setPage(MainWindow::PageDatabase);
                }
                {
                    // Database → Sort the database in displayed order: copy of the database, sorted by number of moves
                    const QString sortedPath = dir + QStringLiteral("/tri.hy64i");
                    hy64db::PgnSource srcSort;
                    hy64db::ConvertResult crSort;
                    std::string errSort;
                    const QString activeBefore = list->model()->filePath();
                    const bool made = srcSort.open(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn", &errSort)
                                      && hy64db::convert(srcSort, sortedPath.toStdString(), {}, &crSort, &errSort) && w4.openDatabase(sortedPath);
                    auto* header = list->findChild<QTableView*>()->horizontalHeader();
                    emit header->sectionClicked(GameListModel::Moves); // sort by moves (in the database)
                    waitFor([&] { return !list->isSearching(); }, 10000);
                    const bool sorted = made && w4.sortDatabase(false);
                    hy64db::Database check64;
                    bool ordered = sorted && check64.open(sortedPath.toStdString(), &errSort) && check64.count() == 3;
                    for (uint32_t i = 1; ordered && i < check64.count(); ++i) ordered = check64.record(i - 1).plies <= check64.record(i).plies;
                    check(ordered && check64.positions(), "base triée dans l'ordre affiché (par coups), index des positions refait");
                    {
                        // Duplicates (show, strike through) and name correction on a small separate database
                        const QString twinPgn = dir + QStringLiteral("/doublons.pgn");
                        {
                            QFile f(twinPgn);
                            (void)f.open(QIODevice::WriteOnly);
                            f.write("[White \"Anderssen\"]\n[Black \"Kieseritzky\"]\n[Date \"1851.06.21\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. f4 exf4 1-0\n\n"
                                    "[White \"Anderssen\"]\n[Black \"Kieseritzky\"]\n[Date \"1851.06.21\"]\n[Result \"1-0\"]\n\n1. e4 {joli} e5 2. f4 exf4 1-0\n\n"
                                    "[White \"Andersen\"]\n[Black \"Morphy\"]\n[Result \"0-1\"]\n\n1. d4 d5 0-1\n");
                        }
                        const QString twinBase = dir + QStringLiteral("/doublons.hy64i");
                        hy64db::PgnSource tsrc;
                        hy64db::ConvertResult tcr;
                        std::string terr;
                        const QString activeTw = list->model()->filePath();
                        const bool opened = tsrc.open(twinPgn.toStdString(), &terr) && hy64db::convert(tsrc, twinBase.toStdString(), {}, &tcr, &terr) &&
                                            w4.openDatabase(twinBase);
                        GameListModel* tm = list->model();
                        const int shown = opened ? w4.handleTwins({}, true, false) : -1;
                        const int rowsShown = tm->rowCount();
                        list->clearFilter();
                        waitFor([&] { return !list->isSearching(); }, 10000);
                        const int marked = w4.handleTwins({}, false, true);
                        const bool firstStruck = tm->isDeleted(0) && !tm->isDeleted(1); // the commented game (no. 2) is kept
                        const int64_t renamed = w4.renameInDatabase(hy64db::Player, QStringLiteral("Andersen"), QStringLiteral("Anderssen"));
                        const QString white3 = tm->data(tm->index(2, GameListModel::White), Qt::DisplayRole).toString();
                        check(shown == 1 && rowsShown == 2 && marked == 1 && firstStruck && renamed == 1 && white3 == QStringLiteral("Anderssen"),
                              qPrintable(QStringLiteral("doublons : affichés (%1), le moins riche rayé ; nom corrigé (%2)").arg(rowsShown).arg(white3)));
                        {
                            // Player profile: 2 games (the struck-through duplicate does not count), their games in the list
                            PlayerReportDialog* pr = w4.playerReport(QStringLiteral("Anderssen"));
                            const bool ok = pr && pr->stats().games.size() == 2 && pr->stats().asWhite.wins == 1 && pr->stats().asWhite.losses == 1 && pr->html().contains(QStringLiteral("Kieseritzky"));
                            if (pr) emit pr->gamesRequested(pr->stats().games, QStringLiteral("Anderssen"));
                            check(ok && tm->rowCount() == 2, "fiche du joueur : score, adversaires, ses parties affichées");
                            {
                                // Preparation against him: opened from his profile, tree of his moves, games of a line shown
                                if (pr) emit pr->preparationRequested(QStringLiteral("Anderssen"));
                                auto* prep = w4.findChild<PreparationDialog*>();
                                const bool opened = prep && prep->preparation().root.games == 2 && prep->preparation().root.children.empty()
                                                    && prep->html().contains(QStringLiteral("Anderssen"));
                                if (prep) prep->setOptions(12, 0, 1);
                                const bool tree = prep && prep->preparation().root.children.size() == 2 && prep->linkCount() >= 2
                                                  && prep->html().contains(QStringLiteral("<b>e4</b>")) && !prep->html(false).contains(QStringLiteral("games:"));
                                int lineLink = -1;
                                for (int i = 0; prep && i < prep->linkCount(); ++i)
                                    if (prep->linkedGames(i).size() == 1 && lineLink < 0) lineLink = i;
                                if (prep && lineLink >= 0) emit prep->gamesRequested(prep->linkedGames(lineLink), QStringLiteral("ligne"));
                                waitFor([&] { return tm->rowCount() == 1; }, 5000); // the view is made in the background
                                const int lineRows = tm->rowCount();
                                // His lines against a repertoire of the other colour: 1.e4 planned, 1.d4 is not
                                bool against = false;
                                if (prep) {
                                    const bool asked = prep->html().contains(QStringLiteral("Vous n'avez pas de répertoire")) && !prep->hasRepertoire();
                                    prep->setRepertoireLoader([](chess::Color c, chess::Game& out) {
                                        if (c != chess::Black) return false;
                                        chess::Game line;
                                        for (const char* san : {"e4", "e5", "f4", "exf4"}) line.playMove(line.current()->pos.parseSan(san));
                                        return chess::repertoire::addLine(out, line) == 4;
                                    });
                                    against = asked && prep->hasRepertoire() && prep->coverage().gaps.size() >= 1 && prep->html().contains(QStringLiteral("coup non prévu"))
                                              && prep->html().contains(QStringLiteral("dans votre répertoire"));
                                    if (!against) std::printf("       %d %d %d\n%s\n", asked, prep->hasRepertoire(), int(prep->coverage().gaps.size()), qPrintable(prep->html()));
                                }
                                check(against, "préparation contre un joueur : ses lignes face au répertoire (coup non prévu signalé)");
                                if (prep) prep->setSide(chess::Black);
                                check(opened && tree && lineRows == 1 && prep->preparation().root.games == 0,
                                      "préparation contre un joueur : ouverte depuis sa fiche, arbre de ses coups, parties d'une ligne affichées, autre couleur");
                                if (prep) prep->close();
                                {
                                    // Integrity test and pieces over the displayed games, from the window
                                    list->clearFilter();
                                    waitFor([&] { return !list->isSearching(); }, 10000);
                                    const QString verdict = w4.checkDatabaseIntegrity(false);
                                    PieceHeatDialog* heat = w4.pieceHeat();
                                    const bool counted = heat && heat->infoText().contains(QStringLiteral("positions"));
                                    if (heat) heat->setOrigin(chess::makeSquare(4, 1)); // the e2 pawn
                                    const bool first = heat && heat->shown()[size_t(chess::makeSquare(4, 3))] == 1000000 && heat->infoText().contains(QStringLiteral("e4"));
                                    if (heat) heat->setPiece(chess::makePiece(chess::Black, chess::King));
                                    if (heat) heat->setSlice(0);
                                    if (!(counted && first)) std::printf("       %d %d [%s] [%s]\n", counted, first, qPrintable(verdict), heat ? qPrintable(heat->infoText()) : "");
                                    check(verdict.contains(QStringLiteral("aucun problème")) && counted && first && heat->shown()[size_t(chess::makeSquare(4, 7))] == 1000000,
                                          "test d'intégrité depuis la fenêtre ; pièces sur les parties affichées (premier coup du pion e2, roi noir en e8)");
                                    if (heat) heat->close();
                                }
                                {
                                    // Player names improved from a reference spelling: the sure ones applied, in one pass
                                    const auto spell = [](const std::string& name) -> std::string {
                                        if (name == "Anderssen") return "Anderssen, Adolf";     // a first name added: to be checked by the user
                                        if (name == "Kieseritzky") return "Kieseritzky";        // already written that way
                                        if (name == "Morphy") return "MORPHY";                  // same word: sure
                                        return {};
                                    };
                                    auto* hy = dynamic_cast<Hy64Source*>(tm->indexed());
                                    const auto found = namefix::proposals(hy->database(), spell);
                                    NameFixDialog fix(found);
                                    const bool proposed = found.size() == 2 && found[0].from == "Anderssen" && !found[0].sure && found[1].from == "Morphy" && found[1].sure
                                                          && fix.rowCount() == 2 && fix.chosen().size() == 1 && fix.chosen()[0].second == "MORPHY";
                                    fix.setAllChecked(true);
                                    const bool all = fix.chosen().size() == 2;
                                    const int changed = w4.improvePlayerNames(spell, false);
                                    const auto& names4 = hy->database().names();
                                    check(proposed && all && changed == 1 && hy->database().nameUsage(hy64db::Player)[names4.find(hy64db::Player, "MORPHY")] == 1
                                              && hy->database().nameUsage(hy64db::Player)[names4.find(hy64db::Player, "Morphy")] == 0
                                              && namefix::sameWords("Kasparov Garry (RUS)", "Kasparov, Garry") && namefix::sameWords("KARPOV, anatoly", "Anatoly Karpov")
                                              && !namefix::sameWords("Kasparov, G", "Kasparov, Garry") && w4.improvePlayerNames(spell, false) == 0,
                                          "améliorer les noms des joueurs : propositions (sûres cochées, prénom ajouté à vérifier), écriture en une passe");
                                }
                                if (pr) emit pr->gamesRequested(pr->stats().games, QStringLiteral("Anderssen")); // the list as the next checks expect it
                            }
                            if (pr) pr->close();
                            {
                                // Clipboard database: games copied from the displayed database, displayed game added, emptying
                                {
                                    QFile::remove(MainWindow::clipboardBasePath());
                                    const QString shownBase = tm->filePath();
                                    const int copied = w4.copyToClipboardBase({0, 1});
                                    const bool stayed = list->model()->filePath() == shownBase;
                                    const bool addedCurrent = w4.addCurrentGameToClipboardBase();
                                    GameListModel* clip = list->modelFor(MainWindow::clipboardBasePath());
                                    const int inClip = clip ? clip->rowCount() : -1;
                                    const bool emptied = w4.emptyClipboardBase() && list->modelFor(MainWindow::clipboardBasePath()) && list->modelFor(MainWindow::clipboardBasePath())->rowCount() == 0;
                                    list->activate(MainWindow::clipboardBasePath());
                                    list->closeCurrent();
                                    list->activate(shownBase);
                                    check(copied == 2 && stayed && addedCurrent && inClip == 3 && emptied && list->model()->filePath() == shownBase,
                                          qPrintable(QStringLiteral("base presse-papiers : %1 parties copiées, %2 au total, vidée").arg(copied).arg(inClip)));
                                }
                                // Merge of games of the database, then repertoire: line added, report on the database
                                auto* session = w4.findChild<GameSession*>();
                                // (the Analysis page's game is put back at the end: other tests use it)
                                chess::Game keptAnalysis;
                                chess::pgn::parseGame(chess::pgn::writeGame(session->gameIn(GameSession::AnalysisSlot)), keptAnalysis);
                                const GameOrigin keptAnalysisOrigin = session->originIn(GameSession::AnalysisSlot);
                                const std::vector<int> keptPath = GameSession::pathTo(session->gameIn(GameSession::AnalysisSlot).current());
                                const MainWindow::Page keptPage = w4.page();
                                const int rowsBefore = tm->rowCount();
                                const int merged = w4.mergeGames({0, 1, 2}, false);
                                const chess::Game& tree = session->gameIn(GameSession::AnalysisSlot);
                                size_t nodes = 0, lines = 0;
                                std::function<void(const chess::Node*)> walk = [&](const chess::Node* n) {
                                    nodes += n->parent ? 1 : 0;
                                    lines += n->children.empty() ? 1 : 0;
                                    for (const auto& c : n->children) walk(c.get());
                                };
                                walk(tree.root());
                                const bool mergedOk = merged >= 2 && w4.page() == MainWindow::PageAnalysis && nodes > tree.mainline().size() && lines >= 2 && tree.header("Event") == "Parties fusionnées"
                                                      && tm->rowCount() == rowsBefore;
                                QFile::remove(MainWindow::repertoirePath(chess::White));
                                chess::Game line;
                                chess::pgn::parseGame("\n1. e4 e5 2. f4 *\n", line);
                                session->loadGame(std::move(line));
                                session->toEnd();
                                const int added = w4.addLineToRepertoire(chess::White);
                                const int addedAgain = w4.addLineToRepertoire(chess::White);
                                chess::Game rep;
                                const bool stored = MainWindow::loadRepertoire(chess::White, rep) && rep.mainline().size() == 3 && rep.header("Event") == "Répertoire des Blancs";
                                w4.setPage(MainWindow::PageDatabase);
                                list->clearFilter();
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                const QString report = w4.repertoireReport(chess::White, false);
                                session->restoreSlot(GameSession::AnalysisSlot, std::move(keptAnalysis), keptAnalysisOrigin, keptPath);
                                w4.setPage(keptPage);
                                check(mergedOk && added == 3 && addedAgain == 0 && stored && report.contains(QStringLiteral("Répertoire des Blancs face à")) && report.contains(QStringLiteral("parties lues")),
                                      qPrintable(QStringLiteral("fusion (%1 parties, %2 coups, %3 lignes) ; répertoire : %4 coups ajoutés, rapport de %5 caractères").arg(merged).arg(nodes).arg(lines).arg(added).arg(report.size())));
                            }
                            // Database index: players counted, filter, sort, games of an entry in the list
                            {
                                list->clearFilter();
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                const int all = tm->rowCount();
                                IndexDialog* index = w4.showIndex();
                                IndexModel* players = index ? index->model(hy64db::Player) : nullptr;
                                const int playerCount = players ? players->rowCount() : -1;
                                bool shownGames = false;
                                int morphyRows = -1, filteredCount = -1, eventCount = -1;
                                QString top;
                                if (players) {
                                    players->sort(IndexModel::Games, Qt::DescendingOrder);
                                    top = players->data(players->index(0, IndexModel::Name), Qt::DisplayRole).toString();
                                    players->setFilter(QStringLiteral("morph"));
                                    filteredCount = players->rowCount();
                                    const quint32 morphy = filteredCount == 1 ? players->entry(0).id : 0;
                                    eventCount = index->model(hy64db::Event)->rowCount();
                                    index->close();
                                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                                    shownGames = w4.showGamesOf(int(hy64db::Player), morphy, QStringLiteral("Morphy"));
                                    waitFor([&] { return !list->isSearching(); }, 10000);
                                    morphyRows = list->model()->rowCount();
                                    list->clearFilter();
                                    waitFor([&] { return !list->isSearching(); }, 10000);
                                }
                                check(playerCount >= 3 && filteredCount == 1 && shownGames && morphyRows == 1 && top.startsWith(QStringLiteral("Anders")) && tm->rowCount() == all,
                                      qPrintable(QStringLiteral("index : %1 joueurs (en tête : %2), %3 tournoi(s) ; « morph » → %4 entrée, %5 partie").arg(playerCount).arg(top).arg(eventCount).arg(filteredCount).arg(morphyRows)));
                            }
                            // Searches by replaying the games: manoeuvre, pawn structure, material of the board
                            {
                                list->clearFilter();
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                const int all = tm->rowCount();
                                hy64db::ManeuverQuery mq;
                                mq.piece = chess::Pawn, mq.color = 0, mq.from = chess::parseSquare("f2"), mq.to = chess::parseSquare("f4"), mq.maxMoves = 1;
                                const bool started = w4.searchManeuver(mq);
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                const int withF4 = list->model()->rowCount();
                                mq.from = chess::parseSquare("d2"), mq.to = chess::parseSquare("d4");
                                w4.searchManeuver(mq);
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                const int withD4 = list->model()->rowCount();
                                const bool structure = w4.searchSimilar(true);
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                const int sameStructure = list->model()->rowCount();
                                const bool material = w4.searchSimilar(false);
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                const int sameMaterial = list->model()->rowCount();
                                list->clearFilter();
                                waitFor([&] { return !list->isSearching(); }, 10000);
                                check(started && structure && material && withF4 >= 1 && withD4 >= 1 && withF4 + withD4 <= all && sameStructure <= sameMaterial && tm->rowCount() == all,
                                      qPrintable(QStringLiteral("recherches par rejeu : f2-f4 dans %1 parties, d2-d4 dans %2 (sur %3) ; structure %4, matériel %5")
                                                     .arg(withF4).arg(withD4).arg(all).arg(sameStructure).arg(sameMaterial)));
                            }
                            {
                                // Single-file Hydra64 database (.hy64): conversion from a PGN, opening in
                                // the list (editable), back to an ordinary database, destination folder created if needed
                                const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
                                conversion::Request one;
                                one.source = QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn");
                                one.destination = docs + QStringLiteral("/nouveau-dossier/un/classiques.hy64");
                                one.target = conversion::Target::Hydra64Single;
                                one.positionDepth = 12;
                                conversion::Report oneRep;
                                QString oneErr;
                                const bool made = conversion::run(one, &oneRep, &oneErr) && QFileInfo::exists(one.destination)
                                                  && QDir(docs + QStringLiteral("/nouveau-dossier/un")).entryList(QDir::Files) == QStringList{QStringLiteral("classiques.hy64")};
                                GameListModel packed;
                                QString loadErr, delErr;
                                const bool listed = made && packed.loadFile(one.destination, &loadErr) && packed.rowCount() == int(oneRep.items) && packed.isIndexed() && !packed.isReadOnly()
                                                    && packed.removeGames({0}, &delErr) && packed.isDeleted(0) && packed.restoreGames({0}, &delErr) && !packed.isDeleted(0);
                                chess::Game firstGame;
                                const bool readable = listed && packed.parseGame(0, firstGame, nullptr) && !firstGame.mainline().empty();
                                // Addition: it goes to the journal (shown under the list), then the tidying puts it back into the database
                                bool journalShown = false, journalTidied = false;
                                if (readable && list->openFile(one.destination, &loadErr)) {
                                    GameListModel* shownModel = list->model();
                                    auto* src64 = dynamic_cast<Hy64Source*>(shownModel->indexed());
                                    const int before = shownModel->rowCount();
                                    const bool appended = shownModel->appendGame(chess::pgn::writeGame(firstGame), &loadErr) >= 0;
                                    list->refreshCount();
                                    QString countText;
                                    for (const QLabel* l : list->findChildren<QLabel*>())
                                        if (l->text().contains(QStringLiteral("journal"))) countText = l->text() + QStringLiteral(" | ") + l->toolTip();
                                    journalShown = appended && src64 && src64->database().journalGames() == 1 && countText.contains(QStringLiteral("déjà enregistré"));
                                    journalTidied = journalShown && list->tidyDatabase(shownModel, true) && src64->database().journalBytes() == 0 && shownModel->rowCount() == before + 1
                                                    && src64->database().isPacked();
                                    list->closeCurrent();
                                }
                                conversion::Request back;
                                back.source = one.destination;
                                back.destination = docs + QStringLiteral("/nouveau-dossier/deux/classiques.hy64i");
                                back.target = conversion::Target::Hydra64Database;
                                conversion::Report backRep;
                                GameListModel plain;
                                const bool restored = conversion::run(back, &backRep, &oneErr) && backRep.items == oneRep.items + 1 && plain.loadFile(back.destination, &loadErr) && !plain.isReadOnly()
                                                      && plain.rowCount() == packed.rowCount() + 1; // the game added above is there
                                check(conversion::kindOf(QStringLiteral("x.hy64")) == conversion::Kind::Database && made && listed && readable && restored && journalShown && journalTidied
                                          && conversion::targetsFor(conversion::Kind::Database).first() == conversion::Target::Hydra64Single,
                                      qPrintable(QStringLiteral("base en un seul fichier : convertie (%1 parties), modifiée, journal indiqué puis rangé, reconvertie %2 %3").arg(oneRep.items).arg(oneErr, loadErr + QStringLiteral(" [%1%2%3%4%5%6]").arg(made).arg(listed).arg(readable).arg(restored).arg(journalShown).arg(journalTidied))));
                            }
                            // ChessBase 18 archive (.2cbv, a ZIP): real file in HYDRA64_TEST_2CBV
                            if (const QByteArray zip = qgetenv("HYDRA64_TEST_2CBV"); zip.isEmpty()) {
                                std::printf("[SKIP] archive ChessBase 18 : définir HYDRA64_TEST_2CBV\n");
                            } else {
                                const QString out = QDir::tempPath() + QStringLiteral("/hy64-2cbv-test");
                                QDir(out).removeRecursively();
                                QString zerr, bad;
                                const bool extracted = ChessBaseSource::extractArchive(QString::fromLocal8Bit(zip), out, &zerr);
                                const QFileInfoList found = QDir(out).entryInfoList({QStringLiteral("*.2cbh")}, QDir::Files);
                                ChessBaseSource cb;
                                chess::Game first;
                                const bool read = extracted && !found.isEmpty() && cb.open(found.first().absoluteFilePath(), &zerr) && cb.total() > 0 && cb.game(0, first, &zerr)
                                                  && !first.mainline().empty();
                                const bool refused = !ChessBaseSource::extractZip(QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn"), out + QStringLiteral("-non"), &bad) && !bad.isEmpty();
                                check(conversion::isArchive(QStringLiteral("x.2cbv")) && read && refused,
                                      qPrintable(QStringLiteral("archive ChessBase 18 (.2cbv) : extraite et lue (%1 parties, %2 fichiers) %3").arg(cb.total()).arg(QDir(out).entryList(QDir::Files).size()).arg(zerr)));
                                QDir(out).removeRecursively();
                            }
                            // ChessBase players encyclopedia (real folder: HYDRA64_PLAYBASE)
                            if (const QByteArray playbase = qgetenv("HYDRA64_PLAYBASE"); playbase.isEmpty()) {
                                std::printf("[SKIP] fiche du joueur avec l'encyclopédie : définir HYDRA64_PLAYBASE\n");
                            } else {
                                QString encErr;
                                const bool refused = !playerEncyclopedia::setDirectory(QDir::tempPath() + QStringLiteral("/pas-une-encyclopedie"), &encErr) && !encErr.isEmpty();
                                const bool opened = playerEncyclopedia::setDirectory(QString::fromLocal8Bit(playbase), &encErr);
                                PlayerReportDialog* enc = w4.playerReport(QStringLiteral("Anderssen"));
                                const QString html = enc ? enc->html() : QString();
                                const QLabel* status = enc ? enc->findChild<QLabel*>(QStringLiteral("EncyclopediaStatus")) : nullptr;
                                const QString statusOn = status ? status->text() : QString();
                                // Preferences → Databases: the folder is listed there; validated without change, it stays in place
                                PreferencesDialog prefsDlg;
                                const QLineEdit* prefDir = prefsDlg.findChild<QLineEdit*>(QStringLiteral("EncyclopediaDir"));
                                const bool inPrefs = prefDir && prefDir->text() == QString::fromLocal8Bit(playbase);
                                prefsDlg.accept();
                                check(statusOn.contains(QStringLiteral("525")) && inPrefs && playerEncyclopedia::get() != nullptr,
                                      qPrintable(QStringLiteral("encyclopédie des joueurs : état en bas de la fiche (« %1 »), dossier dans les Préférences").arg(statusOn)));
                                check(refused && opened && enc && enc->encyclopediaEntryFound() && html.contains(QStringLiteral("1818")) && html.contains(QStringLiteral("1879")),
                                      qPrintable(QStringLiteral("fiche du joueur : encyclopédie (Anderssen 1818–1879, %1 photo(s), %2 note(s) FIDE) %3")
                                                     .arg(enc ? enc->photoCount() : 0).arg(enc ? enc->fideRatingCount() : 0).arg(encErr)));
                                if (enc) enc->close();
                                playerEncyclopedia::setDirectory({}, nullptr);
                                PlayerReportDialog* off = w4.playerReport(QStringLiteral("Anderssen"));
                                const QLabel* offStatus = off ? off->findChild<QLabel*>(QStringLiteral("EncyclopediaStatus")) : nullptr;
                                check(playerEncyclopedia::get() == nullptr && offStatus && offStatus->text().contains(QStringLiteral("non chargée")) && !off->encyclopediaEntryFound(),
                                      "encyclopédie des joueurs : dossier oublié, la fiche l'indique");
                                if (off) off->close();
                            }
                            list->clearFilter();
                            waitFor([&] { return !list->isSearching(); }, 10000);
                        }
                        {
                            // ECO classification: Hydra64 database (index), tooltip of the name; displayed game identified
                            const int changed = w4.classifyDatabase(false);
                            const QString eco = tm->data(tm->index(1, GameListModel::Eco), Qt::DisplayRole).toString();
                            const QString tip = tm->data(tm->index(1, GameListModel::Eco), Qt::ToolTipRole).toString();
                            check(changed >= 1 && eco == QStringLiteral("C33") && tip.contains(openingName("King's Gambit Accepted")),
                                  qPrintable(QStringLiteral("classement ECO (Hydra64) : %1 — %2").arg(eco, tip)));
                            // Beauty: the action runs on the displayed Hydra64 database, the column is shown
                            const int rated = w4.rateBeauty(false);
                            check(rated >= 0 && list->isColumnVisible(GameListModel::Beauty) &&
                                      tm->headerData(GameListModel::Beauty, Qt::Horizontal, Qt::DisplayRole).toString() == QStringLiteral("Beauté"),
                                  qPrintable(QStringLiteral("beauté : Base → Évaluer la beauté des parties sur la base affichée (%1 changée(s)), colonne Beauté affichée").arg(rated)));
                            {
                                // One click on a game opens it; the Base menu is short, its tools in submenus
                                auto* table = list->findChild<QTableView*>();
                                // game of the Databases page and its origin put back afterwards (Ctrl+S depends on it)
                                chess::Game keptClick;
                                chess::pgn::parseGame(chess::pgn::writeGame(s4->gameIn(GameSession::DatabaseSlot)), keptClick);
                                const GameOrigin originClick = s4->originIn(GameSession::DatabaseSlot);
                                const std::vector<int> pathClick = GameSession::pathTo(s4->slot() == GameSession::DatabaseSlot ? s4->current() : s4->gameIn(GameSession::DatabaseSlot).current());
                                const auto pageClick = w4.page();
                                emit table->clicked(table->model()->index(1, GameListModel::White));
                                const bool opened = s4->origin().isValid() && s4->origin().row == 1;
                                emit table->clicked(table->model()->index(0, GameListModel::White));
                                const bool openedFirst = s4->origin().row == 0;
                                s4->restoreSlot(GameSession::DatabaseSlot, std::move(keptClick), originClick, pathClick);
                                w4.setPage(pageClick);
                                QMenu* base = nullptr;
                                for (QAction* a : w4.menuBar()->actions())
                                    if (a->text().remove('&') == QStringLiteral("Base")) base = a->menu();
                                int entries = 0, submenus = 0;
                                QStringList all;
                                if (base)
                                    for (QAction* a : base->actions()) {
                                        if (a->isSeparator() || !a->isVisible()) continue;
                                        ++entries;
                                        if (!a->menu()) continue;
                                        ++submenus;
                                        for (QAction* b : a->menu()->actions()) all << b->text();
                                    }
                                check(opened && openedFirst && base && entries <= 14 && submenus >= 5 && all.contains(QStringLiteral("Doublons…")) &&
                                          all.contains(QStringLiteral("Navigateur ECO…")) && all.contains(QStringLiteral("Rechercher une manœuvre…")),
                                      qPrintable(QStringLiteral("un clic ouvre la partie ; menu Base : %1 entrées dont %2 sous-menus").arg(entries).arg(submenus)));
                            }
                            // PGN database: ECO and Opening headers rewritten
                            const QString pgnCopy = dir + QStringLiteral("/eco.pgn");
                            QFile::copy(QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn"), pgnCopy);
                            const QString backTo = list->model()->filePath();
                            w4.openDatabase(pgnCopy);
                            const int pgnChanged = w4.classifyDatabase(false);
                            chess::Game first;
                            list->model()->parseGame(0, first, nullptr);
                            check(pgnChanged == 3 && first.header("ECO") == "C41" && first.header("Opening").find("Philidor") != std::string::npos,
                                  qPrintable(QStringLiteral("classement ECO (PGN) : %1 %2").arg(QString::fromStdString(first.header("ECO")), QString::fromStdString(first.header("Opening")))));
                            {
                                // Database → Export the displayed games: PGN, EPD (final position), HTML
                                const QString out = dir + QStringLiteral("/export");
                                const int nPgn = w4.exportVisibleGames(MainWindow::ExportFormat::Pgn, out + QStringLiteral(".pgn"));
                                const int nEpd = w4.exportVisibleGames(MainWindow::ExportFormat::Epd, out + QStringLiteral(".epd"));
                                const int nHtml = w4.exportVisibleGames(MainWindow::ExportFormat::Html, out + QStringLiteral(".html"));
                                QFile pf(out + QStringLiteral(".pgn")), ef(out + QStringLiteral(".epd")), hf(out + QStringLiteral(".html"));
                                const bool read = pf.open(QIODevice::ReadOnly) && ef.open(QIODevice::ReadOnly) && hf.open(QIODevice::ReadOnly);
                                const QByteArray epd = ef.readAll(), html = hf.readAll();
                                const auto entries = chess::pgn::indexDatabase(pf.readAll().toStdString());
                                chess::Position p0;
                                const bool fenOk = p0.setFen(epd.left(epd.indexOf(" id ")).toStdString() + " 0 1");
                                check(read && nPgn == 3 && nEpd == 3 && nHtml == 3 && entries.size() == 3 && epd.count('\n') == 3 && fenOk && epd.contains("c0 \"1-0\"")
                                          && html.contains("Paul Morphy") && html.contains("<h2>"),
                                      "exporter les parties affichées : PGN, EPD (positions finales), HTML");
                                // Preview of a game: navigation without loading it
                                GamePreviewDialog* pv = w4.previewGame(0);
                                bool nav = pv && pv->plies() == 33;
                                if (pv) {
                                    pv->goTo(5);
                                    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
                                    QApplication::sendEvent(pv, &right);
                                    nav = nav && pv->ply() == 6;
                                    pv->goTo(999);
                                    nav = nav && pv->ply() == 33;
                                    pv->close();
                                }
                                check(nav, "aperçu d'une partie : 33 demi-coups, navigation (flèches, fin)");
                            }
                            list->closeCurrent();
                            list->activate(backTo);
                        }
                        {
                            // Crosstable of the game's tournament: three players, games between two players
                            EventTableDialog* et = w4.eventTable(1);
                            const bool ok = et && et->table().players.size() == 3 && et->view()->rowCount() == 3 && et->table().ids.size() == 2;
                            if (et) emit et->view()->cellDoubleClicked(0, 1); // a name: their games
                            check(ok && tm->rowCount() >= 1, "tableau croisé d'un tournoi de la base : joueurs, double-clic vers les parties");
                            if (et) et->close();
                            list->clearFilter();
                            waitFor([&] { return !list->isSearching(); }, 10000);
                        }
                        list->closeCurrent();
                        list->activate(activeTw);
                    }
                    {
                        // Bookmark: game no. 2 at the 3rd half-move, found again from another game
                        auto* sb = w4.findChild<GameSession*>();
                        // game of the Databases page and its origin put back afterwards (Ctrl+S depends on it)
                        chess::Game dbKeptB;
                        chess::pgn::parseGame(chess::pgn::writeGame(sb->gameIn(GameSession::DatabaseSlot)), dbKeptB);
                        const GameOrigin dbOriginB = sb->originIn(GameSession::DatabaseSlot);
                        const std::vector<int> dbPathB = GameSession::pathTo(sb->slot() == GameSession::DatabaseSlot ? sb->current() : sb->gameIn(GameSession::DatabaseSlot).current());
                        const auto pageB = w4.page();
                        w4.loadGameFromDatabase(list->rowOfNumber(2));
                        for (int k = 0; k < 3; ++k) sb->forward();
                        const std::string white = sb->game().header("White");
                        const bool added = w4.addBookmark() && MainWindow::bookmarks().size() == 1;
                        w4.loadGameFromDatabase(list->rowOfNumber(1));
                        const bool reopened = w4.openBookmark(0) && sb->game().header("White") == white && sb->current()->ply() == 3;
                        w4.removeBookmark(0);
                        check(added && reopened && MainWindow::bookmarks().isEmpty(), "signet : partie et coup retrouvés, signet supprimé");
                        sb->restoreSlot(GameSession::DatabaseSlot, std::move(dbKeptB), dbOriginB, dbPathB);
                        w4.setPage(pageB);
                    }
                    check64.close();
                    list->closeCurrent();
                    list->activate(activeBefore);
                    m = list->model();
                }
                book->closeBook();

                // Writing: Ctrl+S of a modified game (addition), deletion, compaction
                s4->setHeader("Event", "Copie modifiée");
                bool asked = false;
                answerNextDialog(QObject::tr("Ajouter"), &asked);
                w4.saveToDatabase();
                check(asked && m->totalGames() == n + 1 && cell(m->rowCount() - 1, GameListModel::Event).toString() == QStringLiteral("Copie modifiée"),
                      "Hydra64 : Ctrl+S ajoute la partie modifiée");
                list->selectGame(m->rowCount() - 1);
                const int last = m->rowCount() - 1;
                list->deleteSelected();
                const QFont struck = m->data(m->index(last, GameListModel::White), Qt::FontRole).value<QFont>();
                check(m->totalGames() == n && m->rowCount() == n + 1 && m->isDeleted(last) && struck.strikeOut()
                          && m->indexed()->deletedCount() == 1,
                      "Hydra64 : partie supprimée, toujours affichée, rayée");
                list->deleteSelected(); // Del on a struck-through game: restored
                check(m->totalGames() == n + 1 && !m->isDeleted(last) && m->indexed()->deletedCount() == 0, "Hydra64 : partie rayée restaurée (Suppr)");
                list->deleteSelected();
                check(m->isDeleted(last) && m->totalGames() == n, "Hydra64 : partie de nouveau supprimée");
                {
                    // File → Convert: Hydra64 database → PGN → Hydra64 database, same games
                    conversion::Request req;
                    req.source = hy64;
                    req.destination = dir + QStringLiteral("/export.pgn");
                    req.target = conversion::Target::Pgn;
                    hy64db::PgnSource exported;
                    check(w4.runConversion(req, false, false) && exported.open(req.destination.toStdString(), &err) && int(exported.count()) == m->totalGames(),
                          "Fichier → Convertir : base Hydra64 → PGN (toutes les parties)");
                    req.source = req.destination;
                    req.destination = dir + QStringLiteral("/reimport.hy64i");
                    req.target = conversion::Target::Hydra64Database;
                    hy64db::Database again;
                    bool identical = w4.runConversion(req, false, false) && again.open(req.destination.toStdString(), &err) && int(again.count()) == m->totalGames();
                    for (int i = 0; identical && i < m->totalGames(); ++i) {
                        chess::Game a, b;
                        identical = exported.read(uint64_t(i), a, nullptr) && again.game(uint32_t(i), b) && chess::pgn::writeGame(a) == chess::pgn::writeGame(b);
                    }
                    check(identical, "Fichier → Convertir : PGN → base Hydra64, parties identiques");
                }
                QAction* compact = nullptr;
                for (QAction* a : w4.findChildren<QAction*>())
                    if (a->text() == QStringLiteral("Compacter la base")) compact = a;
                bool compactAsked = false;
                answerNextDialog(QObject::tr("Compacter"), &compactAsked); // confirmation: compaction is final
                if (compact) compact->trigger();
                check(compactAsked, "Hydra64 : confirmation demandée avant de compacter");
                check(compact && list->model()->totalGames() == n && list->model()->rowCount() == n && list->model()->indexed()->deletedCount() == 0
                          && hy64db::Database().open(hy64.toStdString(), &err),
                      "Hydra64 : base compactée (menu Base), parties rayées retirées");
                // Notation menu: "Save the game" → PGN, or one of the open Hydra64 databases
                {
                    const QString other = dir + QStringLiteral("/autre.hy64i");
                    check(hy64db::Database::create(hy64db::basePathOf(other.toStdString()), &err) && w4.openDatabase(other) && list->activate(hy64),
                          "Hydra64 : seconde base ouverte, la première active");
                    auto* view = w4.notationView();
                    QStringList entries;
                    bool added = false;
                    QTimer::singleShot(150, [&] {
                        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                        if (!menu) return;
                        for (QAction* a : menu->actions()) {
                            if (!a->menu() || a->text() != QStringLiteral("Enregistrer la partie")) continue;
                            for (QAction* b : a->menu()->actions()) {
                                if (!b->menu()) continue;
                                for (QAction* c : b->menu()->actions()) entries << c->text();
                                for (QAction* c : b->menu()->actions())
                                    if (c->text() == QStringLiteral("autre")) {
                                        c->trigger();
                                        added = true;
                                    }
                            }
                        }
                        menu->close();
                    });
                    if (view) {
                        QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(5, 5), view->mapToGlobal(QPoint(5, 5)));
                        QApplication::sendEvent(view->viewport(), &ev); // like a real right-click
                    }
                    wait(300);
                    check(view && entries == QStringList{QStringLiteral("classiques (active)"), QStringLiteral("autre")},
                          qPrintable(QStringLiteral("notation : « Ajouter à la base » propose les bases Hydra64 ouvertes, l'active en premier (%1)").arg(entries.join(QStringLiteral(", ")))));
                    GameListModel* otherModel = list->modelFor(other);
                    check(added && otherModel && otherModel->totalGames() == 1 && s4->origin().isValid() && QFileInfo(s4->origin().path) == QFileInfo(other),
                          "notation : partie ajoutée à la base choisie (et liée à elle pour Ctrl+S)");
                    {
                        // Dragging two games of the active database onto the other database's tab
                        list->activate(hy64);
                        GameListModel* src = list->model();
                        std::unique_ptr<QMimeData> mime(src->mimeData({src->index(0, 0), src->index(1, 0), src->index(1, 3)}));
                        // (Qt does not deliver simulated drop events outside a real drag: we pass
                        // the rows carried by the drag directly to the addition.)
                        QDataStream in(mime ? mime->data(GameListModel::MimeType) : QByteArray());
                        QString from;
                        QList<int> rows;
                        in >> from >> rows;
                        QString dropErr;
                        const bool own = list->copyGames(from, rows, hy64, &dropErr) < 0;
                        const int added = list->copyGames(from, rows, other, &dropErr);
                        chess::Game a, b;
                        check(mime && QFileInfo(from) == QFileInfo(hy64) && rows == QList<int>{0, 1} && own && added == 2 && otherModel->totalGames() == 3
                                  && src->parseGame(1, a, nullptr) && otherModel->parseGame(2, b, nullptr) && chess::pgn::writeGame(a) == chess::pgn::writeGame(b)
                                  && mime->text().count(QStringLiteral("[Event ")) == 2,
                              "bases : parties glissées sur l'onglet d'une autre base, ajoutées (pas sur leur propre onglet)");
                    }
                    list->activate(other);
                    list->closeCurrent();
                    list->activate(hy64);
                }
                list->closeCurrent();
                list->activate(basePgnForHy64Tests);
                w4.loadGameFromDatabase(0);
                w4.setPage(MainWindow::PageAnalysis);
            }
        }

        // --- Point 11: menus specific to each page
        check(!menuVisible(w4, QStringLiteral("Base")) && !menuVisible(w4, QStringLiteral("Tournoi")), "Analyse : ni menu Base ni menu Tournoi");
        w4.setPage(MainWindow::PageDatabase);
        check(menuVisible(w4, QStringLiteral("Base")) && !menuVisible(w4, QStringLiteral("Tournoi")), "Bases de données : menu Base");
        w4.setPage(MainWindow::PageTournament);
        check(menuVisible(w4, QStringLiteral("Tournoi")) && !menuVisible(w4, QStringLiteral("Base")), "Tournoi : menu Tournoi");

        // --- Point 10: hideable menu bar
        prefs::setHideMenuBar(true);
        check(!w4.menuBar()->isVisible(), "barre de menu masquée");
        {
            // Bar hidden: a click on the hydra head opens the page's menus
            QToolButton* logo = nullptr;
            for (QToolButton* b : w4.findChildren<QToolButton*>())
                if (b->objectName() == QLatin1String("BrandLogo")) logo = b;
            QStringList titles;
            QTimer::singleShot(150, [&titles] {
                if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    for (QAction* a : menu->actions())
                        if (a->isVisible()) titles << a->text().remove('&');
                    menu->close();
                }
            });
            if (logo) logo->click();
            check(titles.contains(QStringLiteral("Fichier")) && titles.contains(QStringLiteral("Aide")) && titles.contains(QStringLiteral("Tournoi")),
                  "barre masquée : la tête d'hydre ouvre les menus de la page");
        }
        prefs::setHideMenuBar(false);
        check(w4.menuBar()->isVisible(), "barre de menu réaffichée");

        // --- Point 3: engine tab, Play / Stop / lines
        w4.setPage(MainWindow::PageBoard);
        s4->newGame();
        auto* panel = w4.findChild<PlayEnginePanel*>();
        check(panel && panel->engine().id == shuffle.id, "onglet du moteur : moteur actif affiché");
        emit panel->startRequested(); // Play without a game: the engine takes the side to move
        check(waitFor([&] { return plies4() == 1; }, 3000) && g4->isActive() && g4->humanColor() == Black, "Play : le moteur joue les Blancs");
        g4->halt();
        check(g4->isHalted(), "Stop : moteur arrêté");
        const Move reply = s4->current()->pos.legalMoves().front();
        emit b4->moveRequested(reply);
        wait(300);
        emit b4->moveRequested(s4->current()->pos.legalMoves().front()); // white move played by hand
        wait(300);
        check(plies4() == 3, "moteur arrêté : coups libres, pas de réponse");
        g4->moveNow(); // it takes the side to move (Black)
        check(waitFor([&] { return plies4() == 4; }, 3000) && g4->humanColor() == White && !g4->isHalted(), "Play : le moteur reprend avec les Noirs");
        // Single button: Stop during the search, Play when the engine is waiting.
        g4->halt();
        check(!g4->isThinking(), "moteur arrêté : le bouton affiche Play");
        panel->togglePlay(); // Play: the engine plays the side to move
        check(waitFor([&] { return plies4() == 5; }, 3000) && !g4->isHalted() && !g4->isThinking(),
              "bouton Play : le moteur joue son coup puis attend (Play de nouveau)");
        // Options changed in the engine manager during the game: applied right away.
        {
            g4->halt();
            g4->moveNow(); // the engine plays, then it is the player's turn
            waitFor([&] { return !g4->isThinking() && s4->current()->pos.sideToMove() == g4->humanColor(); }, 3000);
            EngineConfig slow = EngineRegistry::instance().engine(shuffle.id);
            slow.values[QStringLiteral("Move Delay")] = QStringLiteral("900");
            EngineRegistry::instance().update(slow);
            const size_t before = plies4();
            emit b4->moveRequested(s4->current()->pos.legalMoves().front());
            wait(400);
            const bool notYet = plies4() == before + 1;
            check(notYet && waitFor([&] { return plies4() == before + 2; }, 3000), "option modifiée en cours de partie : appliquée au moteur");
            slow.values[QStringLiteral("Move Delay")] = QStringLiteral("0");
            EngineRegistry::instance().update(slow);
        }
        {
            // Changing opponent from the engine tab: the game goes on
            waitFor([&] { return !g4->isThinking(); }, 3000);
            const size_t before = plies4();
            const Color human = g4->humanColor();
            emit panel->engineSelected(shuffleB.id);
            const std::string opponent = s4->game().header(human == White ? "Black" : "White");
            check(g4->isActive() && plies4() == before && g4->humanColor() == human && opponent.find("Shuffle B") != std::string::npos,
                  "changer d'adversaire : même partie, mêmes coups, nouvel adversaire");
            if (s4->current()->pos.sideToMove() != human) waitFor([&] { return plies4() == before + 1; }, 3000);
            const size_t now = plies4();
            emit b4->moveRequested(s4->current()->pos.legalMoves().front());
            check(waitFor([&] { return plies4() == now + 2; }, 3000), "le nouvel adversaire répond");
        }
        g4->setMultiPv(3);
        check(g4->multiPv() == 3, "nombre de variantes réglable");
        g4->setMultiPv(1);

        // --- Point 7: engine tournament
        w4.setPage(MainWindow::PageTournament);
        Tournament* t = w4.tournament();
        Tournament::Settings ts;
        ts.participants = {Tournament::Participant::fromEngine(shuffle), Tournament::Participant::fromEngine(shuffleB)};
        ts.baseMs = 3000;
        ts.incMs = 50;
        check(t->setup(ts, &error) && t->games().size() == 2, "tournoi aller-retour : 2 parties");
        t->start();
        check(waitFor([&] { return t->isComplete(); }, 30000), "les deux parties du tournoi sont jouées");
        // This first tournament played with the normal pause between two games (800 ms, to see the final position);
        // the following ones shorten it: about sixty games, hence nearly a minute of pure waiting.
        t->setGamePauseMs(20);
        double total = 0;
        for (const Tournament::Standing& st : t->standings()) total += st.points;
        check(total == 2.0 && !t->games()[0].pgn.isEmpty() && !t->isRunning(), "classement cohérent (2 points distribués)");
        {
            // Elo ± margin and LOS (cutechess formulas); gauntlet; time per move, evaluations as
            // comments and automatic adjudication (draw from the first move with a very wide threshold)
            double elo = 0, margin = 0, los = 0;
            const bool defined = Tournament::eloStats(6, 2, 2, &elo, &margin, &los);
            check(defined && std::abs(elo - 147.19) < 0.1 && margin > 100 && std::abs(los - 0.9214) < 0.001 && !Tournament::eloStats(5, 0, 0, &elo, &margin, &los),
                  qPrintable(QStringLiteral("tournoi : Elo %1 ± %2, LOS %3").arg(elo, 0, 'f', 1).arg(margin, 0, 'f', 1).arg(los, 0, 'f', 4)));
            const EngineConfig sfCfg = EngineRegistry::instance().fallbackEngine();
            Tournament::Settings gs;
            gs.participants = {Tournament::Participant::fromEngine(shuffle), Tournament::Participant::fromEngine(shuffleB), Tournament::Participant::fromEngine(sfCfg)};
            for (int k = 0; k < gs.participants.size(); ++k) gs.participants[k].id = QStringLiteral("p%1").arg(k);
            gs.schedule = Tournament::Settings::Schedule::Gauntlet;
            const bool gauntlet = t->setup(gs, &error) && t->games().size() == 4
                                  && std::all_of(t->games().begin(), t->games().end(), [](const Tournament::GameRecord& g) { return g.whiteId == "p0" || g.blackId == "p0"; })
                                  && Tournament::gameCount(3, gs.schedule, true, 1) == 4 && Tournament::gameCount(3, Tournament::Settings::Schedule::RoundRobin, true, 1) == 6;
            check(gauntlet, "tournoi : gauntlet, le premier participant contre chacun des autres (4 parties)");
            {
                // Swiss system: 5 participants, 3 rounds, draws adjudicated right away
                Tournament::Settings ws;
                for (int k = 0; k < 5; ++k) {
                    Tournament::Participant pp = Tournament::Participant::fromEngine(sfCfg);
                    pp.id = QStringLiteral("s%1").arg(k);
                    pp.name = QStringLiteral("Suisse %1").arg(k);
                    ws.participants << pp;
                }
                ws.schedule = Tournament::Settings::Schedule::Swiss;
                ws.cycles = 3;
                ws.doubleRound = false;
                ws.limit = Tournament::Settings::Limit::MoveTime;
                ws.moveTimeMs = 20;
                ws.drawAdjudication = true;
                ws.drawFromMove = 1;
                ws.drawMoves = 1;
                ws.drawScoreCp = 30000;
                const bool first = t->setup(ws, &error) && t->games().size() == 3 && t->swissRound() == 1 && !t->isComplete();
                t->start();
                const bool done = waitFor([&] { return t->isComplete(); }, 60000);
                QSet<QString> seen, byes;
                bool noRematch = true, oneBye = true;
                for (const Tournament::GameRecord& g : t->games()) {
                    if (g.blackId.isEmpty()) {
                        oneBye = oneBye && !byes.contains(g.whiteId);
                        byes << g.whiteId;
                        continue;
                    }
                    const QString key = std::min(g.whiteId, g.blackId) + QLatin1Char('|') + std::max(g.whiteId, g.blackId);
                    noRematch = noRematch && !seen.contains(key);
                    seen << key;
                }
                check(first && done && t->games().size() == 9 && t->swissRound() == 3 && noRematch && oneBye && byes.size() == 3
                          && Tournament::gameCount(5, ws.schedule, false, 3) == 6,
                      qPrintable(QStringLiteral("tournoi : système suisse, 3 rondes sans revanche, un exempt par ronde (%1 parties)").arg(t->games().size())));
                {
                    // Games played one after the other: both engines of a game are those of the previous one, whatever the
                    // speed of the machine (with several games at once, the number depends on which ends first).
                    Tournament::Settings qs = ws;
                    qs.schedule = Tournament::Settings::Schedule::RoundRobin;
                    qs.participants = ws.participants.mid(0, 2);
                    qs.cycles = 2;
                    qs.doubleRound = true;
                    qs.concurrency = 1;
                    const bool setQ = t->setup(qs, &error) && t->games().size() >= 2;
                    t->start();
                    const bool doneQ = waitFor([&] { return t->isComplete(); }, 60000);
                    const int expected = 2 * (int(t->games().size()) - 1); // both engines of every game but the first
                    check(setQ && doneQ && t->enginesReused() == expected,
                          qPrintable(QStringLiteral("tournoi : parties à la suite, les deux moteurs repris à chaque partie (%1 reprises, %2 attendues)").arg(t->enginesReused()).arg(expected)));
                }
                // Simultaneous games: 5 participants in a round robin (10 games), 3 at a time
                Tournament::Settings cs = ws;
                // Normal pause here: the number of engines reused from one game to the next depends on it (a game started
                // at once does not yet find the engines of the neighbouring games free)
                t->setGamePauseMs(800);
                cs.schedule = Tournament::Settings::Schedule::RoundRobin;
                cs.cycles = 1;
                cs.concurrency = 3;
                int most = 0;
                bool shownValid = true;
                bool overviewTried = false, overviewOk = false, switched = false;
                const bool set = t->setup(cs, &error) && t->games().size() == 10;
                t->start();
                const bool all = waitFor([&] {
                    most = std::max(most, t->runningCount());
                    // the displayed game, if it is in progress, is a game without a result
                    if (const int i = t->currentIndex(); i >= 0) shownValid = shownValid && i < t->games().size() && t->games()[i].result.isEmpty();
                    // Overview: one board per game in progress; a click on one of them shows it full size
                    if (!overviewTried && t->runningCount() == 3) {
                        overviewTried = true;
                        w4.setPage(MainWindow::PageTournament);
                        w4.showTournamentOverview(true);
                        QApplication::processEvents();
                        const QList<int> running = t->runningGames();
                        overviewOk = w4.tournamentOverviewShown() && w4.tournamentOverview()->boardCount() == 3 && w4.tournamentOverview()->shownGames() == running;
                        int other = -1;
                        for (int g : running)
                            if (g != t->currentIndex()) other = g;
                        // Click: the game goes into the notation, the overview stays; double-click: full size
                        w4.tournamentOverview()->select(other);
                        QApplication::processEvents();
                        const chess::Game* live = t->runningGame(other);
                        const bool selected = other >= 0 && w4.tournamentOverviewShown() && t->currentIndex() == other && live
                                              && w4.findChild<GameSession*>()->game().header("Round") == live->header("Round");
                        w4.tournamentOverview()->choose(other);
                        QApplication::processEvents();
                        live = t->runningGame(other);
                        switched = selected && !w4.tournamentOverviewShown() && t->currentIndex() == other && live
                                   && w4.findChild<GameSession*>()->game().header("White") == live->header("White") && w4.findChild<GameSession*>()->game().header("Round") == live->header("Round");
                    }
                    return t->isComplete();
                }, 60000);
                bool complete = true;
                for (const Tournament::GameRecord& g : t->games()) complete = complete && !g.result.isEmpty() && !g.pgn.isEmpty();
                check(set && all && complete && most == 3 && shownValid && !t->isRunning() && t->runningCount() == 0,
                      qPrintable(QStringLiteral("tournoi : parties simultanées (10 parties, jusqu'à %1 à la fois)").arg(most)));
                check(overviewOk && switched, qPrintable(QStringLiteral("tournoi : vue d'ensemble (3 échiquiers : %1), clic → notation, double-clic → en grand : %2").arg(overviewOk).arg(switched)));
                // With several games at once, the number of engines reused depends on which games end first (8 or more
                // here, 7 seen on a slower machine): only "some" is checked. The exact count is checked above, on
                // games played one after the other.
                check(t->enginesReused() >= 1, qPrintable(QStringLiteral("tournoi : moteurs gardés d'une partie à l'autre (%1 reprises pour 10 parties simultanées)").arg(t->enginesReused())));
                t->setGamePauseMs(20);
                {
                    // HTML page of the tournament: standings, all the games; its script replays each
                    // game up to the expected final position (run by Qt's JavaScript engine).
                    const QString htmlPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/tournoi.html");
                    QString htmlErr;
                    const bool written = w4.tournamentPanel()->writeHtml(htmlPath, &htmlErr);
                    QFile hf(htmlPath);
                    (void)hf.open(QIODevice::ReadOnly);
                    const QString page = QString::fromUtf8(hf.readAll());
                    const qsizetype d0 = page.indexOf(QStringLiteral("id=\"data\">")), d1 = page.indexOf(QStringLiteral("</script>"), d0);
                    const QJsonObject data = QJsonDocument::fromJson(page.mid(d0 + 10, d1 - d0 - 10).toUtf8()).object();
                    const QJsonArray gamesJson = data.value("games").toArray();
                    int withMoves = 0;
                    for (const QJsonValue& g : gamesJson) withMoves += !g.toObject().value("m").toArray().isEmpty();
                    check(written && page.contains(QStringLiteral("data-i=standings")) && page.contains(QStringLiteral("Tableau croisé")) && page.contains(QStringLiteral("Cross table"))
                              && page.contains(QStringLiteral("data-lang=en")) && data.value("info").toObject().value("players").toInt() == 5 && gamesJson.size() == 10 && withMoves == 10
                              && !data.value("pgn").toString().isEmpty() && page.contains(QStringLiteral("data:image/png;base64,")) && !page.mid(d0, d1 - d0).contains(QStringLiteral("</")),
                          qPrintable(QStringLiteral("tournoi : page HTML (%1 Ko, %2 parties, %3 avec coups) %4").arg(page.size() / 1024).arg(gamesJson.size()).arg(withMoves).arg(htmlErr)));
#ifdef HYDRA64_TEST_JS
                    {
                        const qsizetype s0 = page.indexOf(QStringLiteral("<script>"), d1), s1 = page.indexOf(QStringLiteral("</script>"), s0);
                        QJSEngine js;
                        const QJSValue loaded = js.evaluate(page.mid(s0 + 8, s1 - s0 - 8));
                        js.globalObject().setProperty(QStringLiteral("DATA"), js.evaluate(QStringLiteral("(") + page.mid(d0 + 10, d1 - d0 - 10) + QStringLiteral(")")));
                        // Tournament games, plus castling, en passant and promotion made by hand
                        const QJSValue replay = js.evaluate(QStringLiteral(
                            "(function(){let bad=0,n=0;for(const g of DATA.games){n++;if(finalPlacement(g)!==g.e)bad++;}"
                            "const t={f:'r3k2r/1P6/8/3pP3/8/8/8/R3K2R',m:[['O-O',4,6,'',[7,5]],['O-O-O',60,58,'',[56,59]],['exd6',36,43,'',[35]],['Kb8',58,57,'',0],['bxa8=Q',49,49+7,'Q',0]]};"
                            "const end=finalPlacement(t);"
                            "return [n,bad,end,evalText(23),evalText(-150),evalText('#8'),evalValue('#-3'),evalValue(950),"
                            "sanText('Nxe5+','fr'),sanText('O-O-O','fr'),sanText('bxa8=Q#','fr'),sanText('Kb1','en'),"
                            "reasonText('Échec et mat','en'),reasonText('Checkmate','fr'),reasonText('Arbitrage : nulle (évaluation proche de 0 pendant 8 coup(s))','en'),"
                            "reasonText('Limite de 60 demi-coups','en'),reasonText('autre chose','en'),fill(TEXTS.en.playedOf,3,4),"
                            "dateText(2026,11,20,'fr'),dateText(2026,11,20,'en'),pgnDateText('1851.??.?\?','fr'),pgnDateText('2026.03.01','fr'),nagText([1,16]),clockText(57500)].join('|');})()"));
                        const QStringList r = replay.toString().split(QLatin1Char('|'));
                        check(!loaded.isError() && !replay.isError() && r.value(0) == QStringLiteral("10") && r.value(1) == QStringLiteral("0")
                                  && r.value(2) == QStringLiteral("Qk1r3r/8/3P4/8/8/8/8/R4RK1") && r.value(3) == QStringLiteral("+0.23") && r.value(4) == QStringLiteral("−1.50")
                                  && r.value(5) == QStringLiteral("#8") && r.value(6) == QStringLiteral("-6") && r.value(7) == QStringLiteral("6")
                                  && r.mid(8) == QStringList{QStringLiteral("Cxe5+"), QStringLiteral("O-O-O"), QStringLiteral("bxa8=D#"), QStringLiteral("Kb1"), QStringLiteral("Checkmate"),
                                                             QStringLiteral("Échec et mat"), QStringLiteral("Adjudication: draw"), QStringLiteral("Limit of 60 plies"), QStringLiteral("autre chose"),
                                                             QStringLiteral("3 games played out of 4"), QStringLiteral("20 novembre 2026"), QStringLiteral("November 20, 2026"), QStringLiteral("1851"),
                                                             QStringLiteral("1er mars 2026"), QStringLiteral("! ±"), QStringLiteral("0:57")},
                              qPrintable(QStringLiteral("tournoi : script de la page HTML — %1 %2").arg(replay.toString(), loaded.isError() ? loaded.toString() : QString())));
                    }
                    {
                        // Single game, with a variation, comments and a symbol: the page's tree follows the game's
                        chess::Game annotated;
                        chess::pgn::parseGame("[Event \"Test\"]\n[Site \"Paris\"]\n[Date \"1851.06.21\"]\n[White \"Anderssen, Adolf\"]\n[Black \"Kieseritzky, Lionel\"]\n[Result \"1-0\"]\n\n"
                                              "{Partie immortelle} 1. e4 {[%csl Rd5][%cal Ge2e4]} e5 2. f4 $1 exf4 (2... d5 {contre-gambit} 3. exd5 e4) 3. Bc4 Qh4+ {[%clk 0:10:00] échec} 4. Kf1 {\xEE\x80\x8A \xEE\x80\xA8" "f3} 1-0\n", annotated);
                        const QString gamePath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/partie.html");
                        const bool exported = w4.exportGameHtml(annotated, gamePath);
                        QFile gf(gamePath);
                        (void)gf.open(QIODevice::ReadOnly);
                        const QString gp = QString::fromUtf8(gf.readAll());
                        const qsizetype g0 = gp.indexOf(QStringLiteral("id=\"data\">")), g1 = gp.indexOf(QStringLiteral("</script>"), g0);
                        QJSEngine js;
                        const qsizetype s0 = gp.indexOf(QStringLiteral("<script>"), g1), s1 = gp.indexOf(QStringLiteral("</script>"), s0);
                        js.evaluate(gp.mid(s0 + 8, s1 - s0 - 8));
                        js.globalObject().setProperty(QStringLiteral("DATA"), js.evaluate(QStringLiteral("(") + gp.mid(g0 + 10, g1 - g0 - 10) + QStringLiteral(")")));
                        const QJSValue tree = js.evaluate(QStringLiteral(
                            "(function(){const g=DATA.games[0],t=gameTree(g);const v=g.m.findIndex(m=>m[0]==='d5');"
                            "return [DATA.mode,g.m.length,t.main.length,t.main.map(i=>g.m[i][0]).join(' '),t.par[v],t.depth[v],t.kids[t.par[v]+1].length,g.m[v][5].c,"
                            "nagText(g.m[t.main[2]][5].g),g.m[t.main[5]][5].c,g.m[t.main[5]][5].k,g.c,finalPlacement(g)===g.e,boardPlacement(t.boards[v+3]),DATA.head.site,g.w,"
                            "JSON.stringify(g.m[0][5].h),(graphicsSvg(g.m[0][5].h,false).match(/<circle|<polygon/g)||[]).length,graphicsSvg([],false)].join('|');})()"));
                        // Comments: ChessBase private-use characters translated on export, quoted moves rewritten in the page's language
                        const QJSValue cited = js.evaluate(QStringLiteral(
                            "(function(){const g=DATA.games[0],t=gameTree(g);return [g.m[t.main[6]][5].c,cmtText('Qh5 et Nbd7, exd8=Q ; B2B Nf3s','fr'),cmtText('Qh5','en')].join('|');})()"));
                        check(cited.toString() == QStringLiteral("± Nf3|Dh5 et Cbd7, exd8=D ; B2B Nf3s|Qh5"),
                              qPrintable(QStringLiteral("page HTML : signes de ChessBase traduits, coups cités réécrits — %1").arg(cited.toString())));
                        check(exported && gp.contains(QStringLiteral("data-i=gameBrand")) && !gp.contains(QStringLiteral("id=games"))
                                  && tree.toString() == QStringLiteral("game|10|7|e4 e5 f4 exf4 Bc4 Qh4+ Kf1|2|4|2|contre-gambit|!|échec|600000|Partie immortelle|true|"
                                                                       "rnbqkbnr/ppp2ppp/8/3P4/4pP2/8/PPPP2PP/RNBQKBNR|Paris|Anderssen, Adolf|[[35,-1,\"R\"],[12,28,\"G\"]]|2|"),
                              qPrintable(QStringLiteral("partie en page HTML : arbre, variante, commentaires — %1").arg(tree.toString())));
                    }
#else
                    std::printf("[SKIP] script de la page HTML : moteur JavaScript de Qt (Qt6Qml) absent\n");
#endif
                }
                {
                    // Panel buttons depending on the state: "New tournament" only next to "Resume"
                    TournamentPanel* panel = w4.tournamentPanel();
                    auto* newButton = panel->findChild<QPushButton*>(QStringLiteral("TournamentNew"));
                    QPushButton* runButton = nullptr;
                    for (QPushButton* b : panel->findChildren<QPushButton*>())
                        if (b->property("role").toString() == QLatin1String("run")) runButton = b;
                    auto state = [&] { QApplication::processEvents(); return runButton ? runButton->text() + (newButton && !newButton->isHidden() ? QStringLiteral(" + nouveau") : QString()) : QString(); };
                    const QString done = state();
                    t->setup(cs, &error);
                    t->start();
                    const QString running = state();
                    t->stop();
                    const QString paused = state();
                    check(done == QStringLiteral("Nouveau tournoi") && running == QStringLiteral("Interrompre") && paused == QStringLiteral("Reprendre le tournoi + nouveau"),
                          qPrintable(QStringLiteral("tournoi : boutons selon l'état — terminé « %1 », en cours « %2 », interrompu « %3 »").arg(done, running, paused)));
                }
                // Swiss system with simultaneous games: the next round waits for the end of the current round
                Tournament::Settings sw = ws;
                sw.concurrency = 2;
                const bool swissSet = t->setup(sw, &error);
                t->start();
                const bool swissDone = waitFor([&] { return t->isComplete(); }, 60000);
                check(swissSet && swissDone && t->games().size() == 9 && t->swissRound() == 3, "tournoi : système suisse avec deux parties simultanées");
            }
            Tournament::Settings as;
            as.participants = {Tournament::Participant::fromEngine(sfCfg), Tournament::Participant::fromEngine(sfCfg)};
            as.participants[0].id = QStringLiteral("a");
            as.participants[1].id = QStringLiteral("b");
            as.participants[1].name += QStringLiteral(" (2)");
            as.doubleRound = false;
            as.limit = Tournament::Settings::Limit::MoveTime;
            as.moveTimeMs = 40;
            as.drawAdjudication = true;
            as.drawFromMove = 1;
            as.drawMoves = 1;
            as.drawScoreCp = 30000;
            const bool adjudicated = t->setup(as, &error) && (t->start(), waitFor([&] { return t->isComplete(); }, 20000))
                                     && t->games()[0].result == QStringLiteral("1/2-1/2") && t->games()[0].reason.startsWith(QStringLiteral("Arbitrage"));
            chess::Game played;
            const bool commented = chess::pgn::parseGame(t->games()[0].pgn.toStdString(), played) && !played.mainline().empty()
                                   && played.mainline().front()->comment.find('/') != std::string::npos && played.mainline().size() == 2;
            {
                // Adjudication by the endgame tablebases: the game stops at the first move with the tables' result
                tablebases::setSources({QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/syzygy")});
                tablebases::apply();
                const QString endings = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/finales-arbitrage.epd");
                {
                    QFile f(endings);
                    (void)f.open(QIODevice::WriteOnly);
                    f.write("8/8/8/4k3/8/8/8/3QK3 w - - 0 1;\n"); // queen against king: White wins
                }
                Tournament::Settings tbs = as;
                tbs.drawAdjudication = false;
                tbs.tablebaseAdjudication = true;
                tbs.openingsFile = endings;
                tbs.doubleRound = true; // the same position with colours reversed: still a win for White
                const bool playedTb = t->setup(tbs, &error) && (t->start(), waitFor([&] { return t->isComplete(); }, 20000));
                bool allWhite = playedTb && t->games().size() == 2;
                size_t longest = 0;
                for (const Tournament::GameRecord& g : t->games()) {
                    chess::Game parsed;
                    chess::pgn::parseGame(g.pgn.toStdString(), parsed);
                    longest = std::max(longest, parsed.mainline().size());
                    allWhite = allWhite && g.result == QStringLiteral("1-0") && g.reason == QStringLiteral("Arbitrage : tables de finales (gain des Blancs)");
                }
                // direct verdicts: draw, Black wins, position outside the tables
                chess::Position drawn, blackWins, outside;
                drawn.setFen("8/8/8/4k3/8/4K3/4P3/8 w - - 0 1");
                blackWins.setFen("3qk3/8/8/8/8/8/8/4K3 w - - 3 9");
                outside.setFen("4k3/8/8/8/8/8/3PP3/4K3 w - - 0 1");
                QString r1, why1, r2, why2, r3, why3;
                const bool v1 = Tournament::tablebaseVerdict(drawn, r1, why1), v2 = Tournament::tablebaseVerdict(blackWins, r2, why2), v3 = Tournament::tablebaseVerdict(outside, r3, why3);
                tbs.tablebaseAdjudication = false; // without the option, the same game is not adjudicated by the tables
                tbs.doubleRound = false;
                tbs.drawAdjudication = true;
                const bool playedWithout = t->setup(tbs, &error) && (t->start(), waitFor([&] { return t->isComplete(); }, 20000)) && !t->games()[0].reason.contains(QStringLiteral("tables"));
                check(allWhite && longest == 1 && v1 && r1 == QStringLiteral("1/2-1/2") && v2 && r2 == QStringLiteral("0-1") && !v3 && playedWithout,
                      qPrintable(QStringLiteral("tournoi : arbitrage par les tables de finales (%1 demi-coup joué, « %2 »)").arg(longest).arg(t->games().isEmpty() ? QString() : why2)));
                tablebases::setSources({});
                tablebases::apply();
            }
            {
                // Pondering by both engines (draw adjudicated at move 8)
                Tournament::Settings ps2 = as;
                ps2.ponder = true;
                ps2.drawFromMove = 8;
                const bool played = t->setup(ps2, &error) && (t->start(), waitFor([&] { return t->isComplete(); }, 30000));
                check(played && t->ponderHits() > 0, qPrintable(QStringLiteral("tournoi : réflexion permanente des moteurs (%1 coups prévus)").arg(t->ponderHits())));
                t->setup(as, &error);
                t->start();
                waitFor([&] { return t->isComplete(); }, 20000);
            }
            {
                // Position editor: a Chess960 position goes in and comes out with its castling rights
                {
                    const chess::Position start960 = chess::Position::chess960Start(300); // QBNRKRBN
                    PositionEditorDialog editor(start960, false);
                    int enabled = 0, checked = 0;
                    for (QCheckBox* box : editor.findChildren<QCheckBox*>())
                        if (box->text().contains(QStringLiteral("O-O"))) enabled += box->isEnabled(), checked += box->isChecked();
                    if (auto* box = editor.findChild<QDialogButtonBox*>()) box->button(QDialogButtonBox::Ok)->click();
                    check(editor.result() == QDialog::Accepted && editor.position().fen() == start960.fen() && editor.position().isChess960()
                              && !editor.position().castlingMove(true).isNull(),
                          qPrintable(QStringLiteral("Chess960 : éditeur de position (%1 droits cochés sur %2 possibles) — %3").arg(checked).arg(enabled).arg(QString::fromStdString(editor.position().fen()))));
                }
                // Castling right on a rook that is not the outermost (two rooks on the same side of the king): kept
                {
                    chess::Position inner;
                    const bool read = inner.setFen("4k3/8/8/8/8/8/8/RR2K3 w B - 0 1");
                    PositionEditorDialog editor(inner, false);
                    if (auto* box = editor.findChild<QDialogButtonBox*>()) box->button(QDialogButtonBox::Ok)->click();
                    check(read && inner.castlingRook(chess::White, false) == chess::parseSquare("b1") && editor.result() == QDialog::Accepted
                              && editor.position().castlingRights() == inner.castlingRights() && editor.position().castlingRook(chess::White, false) == chess::parseSquare("b1"),
                          qPrintable(QStringLiteral("Chess960 : éditeur de position, droit de roque sur la tour intérieure gardé — %1").arg(QString::fromStdString(editor.position().fen()))));
                }
                // Chess960: the engine receives UCI_Chess960 and understands our castling ("the king captures its rook");
                // its reply is a legal move of the position after castling.
                chess::Position p960;
                p960.setFen("r1k2r1q/p1pppppp/8/8/8/8/P1PPPPPP/R1K2R1Q w KQkq - 0 1");
                const chess::Move castle = p960.parseSan("O-O");
                const chess::Position after = p960.after(castle);
                UciEngine engine960;
                QString reply;
                QObject::connect(&engine960, &UciEngine::bestMoveReceived, [&](const QString& uci, const QString&) { reply = uci; });
                const bool started = engine960.start(sfCfg.path);
                engine960.search(QString::fromStdString(p960.fen() + " moves " + chess::Position::toUci(castle)), QStringLiteral("depth 6"));
                const bool answered = started && waitFor([&] { return !reply.isEmpty(); }, 15000);
                const chess::Move m = after.parseUci(reply.toStdString());
                check(p960.isChess960() && chess::Position::toUci(castle) == "c1f1" && answered && !m.isNull() && after.isLegal(m),
                      qPrintable(QStringLiteral("Chess960 : le moteur suit le roque c1f1 et répond %1").arg(reply)));
                engine960.quit();
                // Tournament from a Chess960 position: no game lost on an illegal move
                const QString openings960 = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/ouvertures960.epd");
                {
                    QFile f(openings960);
                    (void)f.open(QIODevice::WriteOnly);
                    f.write("nrk1brnb/pp1ppppp/2p5/3q4/5P2/PP6/1KPPP1PP/NR1QBRNB w fb - 1 9;\nr1k2r1q/p1pppppp/8/8/8/8/P1PPPPPP/R1K2R1Q w KQkq - 0 1;\n");
                }
                Tournament::Settings fs = as;
                fs.openingsFile = openings960;
                fs.doubleRound = true;
                fs.cycles = 2;
                fs.concurrency = 2;
                fs.drawFromMove = 14;
                const bool played960 = t->setup(fs, &error) && (t->start(), waitFor([&] { return t->isComplete(); }, 60000));
                bool legal960 = played960 && t->games().size() == 4;
                for (const Tournament::GameRecord& g : t->games()) {
                    chess::Game parsed;
                    legal960 = legal960 && !g.reason.contains(QStringLiteral("illégal")) && chess::pgn::parseGame(g.pgn.toStdString(), parsed)
                               && parsed.startPosition().isChess960() && parsed.mainline().size() >= 10;
                }
                check(legal960, qPrintable(QStringLiteral("Chess960 : tournoi de 4 parties depuis des positions Chess960 (%1)").arg(error)));
                t->setup(as, &error);
                t->start();
                waitFor([&] { return t->isComplete(); }, 20000);
            }
            {
                // Engine book: 1.e4 e5 drawn from the book, then the engines play
                const QString bookPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/livre.hy64book");
                const chess::Position s0 = chess::Position::startPos();
                const chess::Position s1 = s0.after(s0.parseSan("e4"));
                const chess::treebook::MoveReader reader = [s0, s1](const chess::Position& p) {
                    std::vector<chess::treebook::BookMove> out;
                    chess::treebook::BookMove bm;
                    bm.hasStats = false;
                    bm.gameCount = 10;
                    if (p.fen() == s0.fen()) bm.move = s0.parseSan("e4"), out.push_back(bm);
                    else if (p.fen() == s1.fen()) bm.move = s1.parseSan("e5"), out.push_back(bm);
                    return out;
                };
                Tournament::Settings bs = as;
                bs.bookFile = bookPath;
                bs.drawFromMove = 4;
                const bool written = chess::treebook::writeTree(reader, {s0}, bookPath.toStdString(), chess::treebook::TreeFormat::Hy64Book);
                const bool played = written && t->setup(bs, &error) && (t->start(), waitFor([&] { return t->isComplete(); }, 30000));
                chess::Game bg;
                const bool parsed = played && chess::pgn::parseGame(t->games()[0].pgn.toStdString(), bg) && bg.mainline().size() >= 3;
                check(parsed && bg.mainline()[0]->san == "e4" && bg.mainline()[1]->san == "e5" && bg.mainline()[0]->comment == "livre" && t->bookMoves() == 2,
                      qPrintable(QStringLiteral("tournoi : livre des moteurs (%1 coups du livre)").arg(t->bookMoves())));
                t->setup(as, &error);
                t->start();
                waitFor([&] { return t->isComplete(); }, 20000);
            }
            check(adjudicated && commented,
                  qPrintable(QStringLiteral("tournoi : temps par coup, évaluations en commentaire, nulle arbitrée (%1, « %2 »)").arg(t->games()[0].result, t->games()[0].reason)));
        }
        check(s4->slot() == GameSession::TournamentSlot && plies4() > 0 && s4->game().header("Result") != "*",
              "page Tournoi : la dernière partie est affichée avec son résultat");
        // Participants: engine + settings specific to the tournament, without touching the engine manager
        {
            const QString delayBefore = EngineRegistry::instance().engine(shuffleB.id).value(QStringLiteral("Move Delay"));
            ParticipantDialog dlg(Tournament::Participant{});
            dlg.selectEngine(shuffleB.id);
            dlg.setOptionValue(QStringLiteral("Move Delay"), QStringLiteral("50"));
            dlg.setOptionValue(QStringLiteral("Style"), QStringLiteral("Random"));
            const Tournament::Participant tuned = dlg.participant();
            check(tuned.engineId == shuffleB.id && tuned.name == shuffleB.name && tuned.values.value(QStringLiteral("Move Delay")) == QStringLiteral("50")
                      && tuned.values.value(QStringLiteral("Style")) == QStringLiteral("Random"),
                  "fenêtre du participant : moteur, nom et réglages choisis");
            check(EngineRegistry::instance().engine(shuffleB.id).value(QStringLiteral("Move Delay")) == delayBefore
                      && !EngineRegistry::instance().engine(shuffleB.id).values.contains(QStringLiteral("Style")),
                  "réglages du participant : la gestion des moteurs n'est pas modifiée");
            ParticipantDialog again(tuned);
            check(again.participant().values == tuned.values, "fenêtre rouverte : réglages du participant conservés");
            check(tuned.config().value(QStringLiteral("Move Delay")) == QStringLiteral("50") && tuned.config().name == shuffleB.name,
                  "le tournoi lance le moteur avec les réglages du participant");

            // Panel: "+" adds, names made unique, "−" removes
            auto* tp = w4.tournamentPanel();
            check(tp != nullptr, "panneau Tournoi trouvé");
            if (tp) {
                while (!tp->participants().isEmpty()) tp->removeParticipant(0);
                tp->addParticipant(Tournament::Participant::fromEngine(shuffleB));
                tp->addParticipant(tuned);
                check(tp->participants().size() == 2 && tp->participants()[1].name == shuffleB.name + QStringLiteral(" (2)"),
                      "même moteur deux fois : le second est renommé « (2) »");
                tp->removeParticipant(0);
                check(tp->participants().size() == 1 && tp->participants()[0].values == tuned.values, "« − » retire le participant choisi");
                tp->removeParticipant(0);
            }

            // The same engine, two different settings, one against the other
            Tournament::Settings same;
            Tournament::Participant a = Tournament::Participant::fromEngine(shuffleB), b = tuned;
            b.name = QStringLiteral("Shuffle B hasard");
            same.participants = {a, b};
            same.baseMs = 3000;
            same.incMs = 50;
            same.drawAdjudication = true; // only the participants and the standings are checked: games stopped early
            same.drawFromMove = 1;
            same.drawMoves = 1;
            same.drawScoreCp = 30000;
            check(t->setup(same, &error) && t->games().size() == 2 && t->games()[0].whiteName == shuffleB.name
                      && t->games()[0].blackName == QStringLiteral("Shuffle B hasard"),
                  "même moteur avec deux réglages : deux participants distincts");
            t->start();
            check(waitFor([&] { return t->isComplete(); }, 30000) && t->standings().size() == 2, "même moteur avec deux réglages : parties jouées, deux lignes au classement");

            // Old saved format (list of engines): reread as participants
            QJsonObject legacy = t->saveState();
            legacy.remove(QStringLiteral("participants"));
            legacy[QStringLiteral("engines")] = QJsonArray{shuffle.id, shuffleB.id};
            QJsonArray games;
            games.append(QJsonObject{{"white", shuffle.id}, {"black", shuffleB.id}, {"whiteName", shuffle.name}, {"blackName", shuffleB.name}, {"result", "1-0"}});
            legacy[QStringLiteral("games")] = games;
            t->restoreState(legacy);
            check(t->settings().participants.size() == 2 && t->settings().participants[0].engineId == shuffle.id && t->standings().first().points == 1.0,
                  "ancien format de tournoi : moteurs relus comme participants, résultats conservés");
        }

        // Starting positions: EPD (operations ignored) and PGN (moves of the main line)
        {
            const QString dir = QDir::tempPath() + QStringLiteral("/hydra64-openings-%1").arg(QCoreApplication::applicationPid());
            QDir().mkpath(dir);
            const QString epd = dir + QStringLiteral("/start.epd");
            QFile f(epd);
            check(f.open(QIODevice::WriteOnly), "fichier EPD créé");
            f.write("# commentaire\n"
                    "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - bm Nf3; id \"e4e5\";\n"
                    "not a fen\n"
                    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3\n");
            f.close();
            check(Tournament::loadOpenings(epd, &error).size() == 2, "EPD : deux positions valides lues");
            const QString pgnPath = dir + QStringLiteral("/start.pgn");
            QFile g(pgnPath);
            check(g.open(QIODevice::WriteOnly), "fichier PGN créé");
            g.write("[Event \"a\"]\n\n1. d4 d5 2. c4 *\n\n[Event \"b\"]\n\n1. e4 c5 *\n");
            g.close();
            const QStringList fromPgn = Tournament::loadOpenings(pgnPath, &error);
            check(fromPgn.size() == 2 && fromPgn[0].endsWith(QStringLiteral(" moves d2d4 d7d5 c2c4")), "PGN : position et coups de chaque partie");
            {
                // Hydra64 database: same openings as the PGN; proprietary format refused (conversion first)
                hy64db::PgnSource src;
                hy64db::ConvertResult cr;
                std::string err;
                const QString hy64 = dir + QStringLiteral("/ouvertures.hy64i");
                const bool converted = src.open(pgnPath.toStdString(), &err) && hy64db::convert(src, hy64.toStdString(), {}, &cr, &err);
                check(converted && Tournament::loadOpenings(hy64, &error) == fromPgn, "base Hydra64 : positions et coups de chaque partie, comme le PGN");
                const QString cbh = dir + QStringLiteral("/ouvertures.cbh");
                { QFile f(cbh); (void)f.open(QIODevice::WriteOnly); }
                error.clear();
                check(Tournament::loadOpenings(cbh, &error).isEmpty() && error.contains(QStringLiteral("Convertir")),
                      "positions de départ : format propriétaire refusé, renvoi vers la conversion");
            }

            ts.openingsFile = epd;
            check(t->setup(ts, &error) && t->games().size() == 2 && t->games()[0].opening == t->games()[1].opening
                      && t->games()[0].opening.startsWith(QStringLiteral("rnbqkbnr/pppp1ppp/8/4p3/4P3/")),
                  "tournoi : les deux couleurs partent de la même position du fichier");
            t->start();
            check(waitFor([&] { return t->isComplete(); }, 30000), "tournoi depuis des positions de départ : parties jouées");
            check(t->games()[0].pgn.contains(QStringLiteral("[FEN \"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq")),
                  "PGN de la partie : en-tête FEN de la position de départ");
            check(t->games()[0].pgn.contains(QStringLiteral("{Position de départ de la base (sans coups) : fin des coups de la base}")),
                  "EPD : repère avant le premier coup des moteurs");
            ts.openingsFile = pgnPath;
            check(t->setup(ts, &error), "tournoi depuis un PGN");
            t->start();
            check(waitFor([&] { return t->isComplete(); }, 30000) && t->games()[0].pgn.contains(QStringLiteral("1. d4 d5 2. c4 {Fin des coups de la base}")),
                  "PGN : les coups d'ouverture précèdent ceux des moteurs, repère « Fin des coups de la base »");
            check(s4->slot() == GameSession::TournamentSlot && s4->game().mainline().size() > 3
                      && s4->game().mainline()[2]->comment == "Fin des coups de la base" && s4->game().mainline()[2]->san == "c4",
                  "notation : les coups de la base, puis le repère, puis ceux des moteurs");
            // Full distribution: 3 engines, reversed colours, 2 cycles (12 games, 6 pairings),
            // 10 distinct positions; in order then at random.
            {
                EngineConfig shuffleC;
                EngineRegistry::create(shufflePath, shuffleC, &error);
                shuffleC.name = QStringLiteral("Shuffle C");
                shuffleC.values[QStringLiteral("Move Delay")] = QStringLiteral("0");
                EngineRegistry::instance().add(shuffleC);
                const QStringList fens = Tournament::loadOpenings(QStringLiteral(HYDRA64_SOURCE_DIR "/databases/tournament-openings/Drawkiller_balanced_big.hy64"), &error).mid(0, 10);
                const QString ten = dir + QStringLiteral("/ten.epd");
                QFile tf(ten);
                check(tf.open(QIODevice::WriteOnly) && fens.size() == 10, "fichier de 10 positions créé");
                tf.write((fens.join('\n') + '\n').toUtf8());
                tf.close();

                Tournament::Settings rs;
                rs.participants = {Tournament::Participant::fromEngine(shuffleB), Tournament::Participant::fromEngine(shuffleC),
                                   Tournament::Participant::fromEngine(shuffle)};
                rs.cycles = 2;
                rs.doubleRound = true;
                rs.baseMs = 3000;
                rs.incMs = 50;
                rs.openingsFile = ten;
                // This test checks the distribution of the positions and the start of each game, not its course:
                // the games are stopped by adjudication in the first moves (whole games of random
                // moves took 16 s; full games with a clock are played by other tests)
                rs.drawAdjudication = true;
                rs.drawFromMove = 1;
                rs.drawMoves = 1;
                rs.drawScoreCp = 30000;
                // Checks the distribution; returns the positions of the pairings in order.
                auto pairings = [&](const char* label) {
                    QStringList used;
                    const QList<Tournament::GameRecord>& gs = t->games();
                    bool ok = gs.size() == 12;
                    for (int i = 0; ok && i + 1 < gs.size(); i += 2) {
                        const Tournament::GameRecord &a = gs[i], &b = gs[i + 1];
                        // reversed colours: same engines, colours reversed, same position
                        ok = a.whiteId == b.blackId && a.blackId == b.whiteId && a.opening == b.opening && fens.contains(a.opening);
                        used << a.opening;
                    }
                    check(ok, qPrintable(QStringLiteral("%1 : chaque appariement joue la même position avec les deux couleurs").arg(QLatin1String(label))));
                    check(QSet<QString>(used.begin(), used.end()).size() == 6,
                          qPrintable(QStringLiteral("%1 : six appariements, six positions différentes").arg(QLatin1String(label))));
                    return used;
                };
                check(t->setup(rs, &error), "tournoi 3 moteurs, 2 tours, positions dans l'ordre");
                check(pairings("ordre") == fens.mid(0, 6), "ordre : les positions sont prises dans l'ordre du fichier");

                rs.openingsRandom = true;
                QList<QStringList> draws;
                for (int k = 0; k < 3; ++k) {
                    check(t->setup(rs, &error), "tournoi, positions au hasard");
                    draws << pairings("hasard");
                }
                check(draws[0] != fens.mid(0, 6) || draws[1] != fens.mid(0, 6), "hasard : pas l'ordre du fichier");
                check(draws[0] != draws[1] || draws[1] != draws[2], "hasard : un tirage différent à chaque tournoi");

                // The games played do start from their position: FEN header, and the
                // engines played from it (moves readable from that position).
                t->start();
                check(waitFor([&] { return t->isComplete(); }, 120000), "tournoi au hasard : 12 parties jouées");
                bool fromOpening = true;
                for (const Tournament::GameRecord& g : t->games()) {
                    chess::Game parsed;
                    const bool read = chess::pgn::parseGame(g.pgn.toStdString(), parsed);
                    const bool sameStart = Position::startPos().fen() != g.opening.toStdString() && parsed.startPosition().fen() == g.opening.toStdString();
                    const bool played = !parsed.mainline().empty() || g.reason.contains(QStringLiteral("mat"), Qt::CaseInsensitive);
                    if (!read || !sameStart || !played || g.reason.contains(QStringLiteral("Impossible"))) {
                        fromOpening = false;
                        std::printf("       partie %s – %s : %s (%s)\n", qPrintable(g.whiteName), qPrintable(g.blackName), qPrintable(g.result), qPrintable(g.reason));
                    }
                }
                check(fromOpening, "chaque partie jouée part de la position tirée pour son appariement");
                EngineRegistry::instance().remove(shuffleC.id);
            }

            ts.openingsFile.clear();
            // PGN database of the tournament: written with the first finished game (no empty file), each game added as soon as it is over.
            // (This is also the state expected by the restart test: a finished two-game tournament.)
            ts.pgnFile = Tournament::newPgnPath(dir + QStringLiteral("/Tournois"));
            check(QFileInfo(ts.pgnFile).fileName().startsWith(QStringLiteral("Tournoi ")) && ts.pgnFile.endsWith(QStringLiteral(".hy64")),
                  "base du tournoi : « Tournoi <date heure>.hy64 », au format Hydra64");
            check(t->setup(ts, &error) && !QFileInfo::exists(ts.pgnFile) && QDir(dir + QStringLiteral("/Tournois")).exists(),
                  "base du tournoi : aucun fichier vide au lancement, seulement son dossier");
            auto* list = w4.gamesPanel();
            const QString basePgn = isolated + QStringLiteral("/base.pgn"); // (pgnPath designates start.pgn here)
            t->start();
            check(waitFor([&] { return QFileInfo::exists(ts.pgnFile) && QFileInfo(ts.pgnFile).size() > 0; }, 30000), "base du tournoi créée avec la première partie finie");
            check(Tournament::newPgnPath(dir + QStringLiteral("/Tournois")) != ts.pgnFile, "base du tournoi : jamais d'écrasement d'un fichier existant");
            check(w4.openDatabase(ts.pgnFile) && list->model()->rowCount() >= 1, "base du tournoi ouverte pendant le tournoi");
            list->activate(basePgn); // the tournament database stays open, in another tab
            check(waitFor([&] { return t->isComplete(); }, 30000), "tournoi de deux parties rejoué");
            {
                GameListModel saved;
                QString savedError;
                check(saved.loadFile(ts.pgnFile, &savedError) && saved.isIndexed(), "base du tournoi lisible (Hydra64)");
                const QString firstResult = saved.data(saved.index(0, GameListModel::Result), Qt::DisplayRole).toString().replace(QStringLiteral("½"), QStringLiteral("1/2"));
                Game firstGame;
                check(saved.rowCount() == 2 && firstResult == t->games()[0].result && saved.parseGame(0, firstGame, &savedError) && !firstGame.mainline().empty(),
                      "base du tournoi : les deux parties, avec leurs résultats et leurs coups");
            }
            check(QFileInfo(list->model()->filePath()) == QFileInfo(basePgn), "partie ajoutée : l'onglet affiché ne change pas");
            check(list->activate(ts.pgnFile) && list->model()->rowCount() == 2, "base du tournoi ouverte : mise à jour au fil des parties");
            list->closeCurrent();
            list->activate(basePgn);
            {
                // A game ended before any move (engine that cannot be started) counts in the standings
                // but is not saved: no game without moves in the database, nor in the export, and no empty file.
                Tournament::Settings broken = ts;
                Tournament::Participant ghost = ts.participants[1];
                ghost.id = QStringLiteral("ghost");
                ghost.engineId = QStringLiteral("no-such-engine"); // gone from the engine manager
                ghost.name = QStringLiteral("Fantôme");
                broken.participants[1] = ghost;
                broken.pgnFile = Tournament::newPgnPath(dir + QStringLiteral("/Tournois"));
                const bool set = t->setup(broken, &error);
                t->start();
                const bool done = set && waitFor([&] { return t->isComplete(); }, 30000);
                int forfeits = 0;
                for (const Tournament::GameRecord& g : t->games()) forfeits += g.noMoves && !g.result.isEmpty();
                check(done && forfeits == t->games().size() && forfeits > 0 && !QFileInfo::exists(broken.pgnFile) && t->exportPgn().isEmpty(),
                      qPrintable(QStringLiteral("tournoi : %1 partie(s) sans coups (moteur introuvable) comptée(s) au classement mais ni enregistrée(s) ni exportée(s) ; le tournoi va au bout").arg(forfeits)));
                t->setup(ts, &error); // back to the finished two-game tournament expected below
                t->start();
                waitFor([&] { return t->isComplete(); }, 30000);
            }
            ts.pgnFile.clear();
            QDir(dir).removeRecursively();

            // Bundled collections (databases/tournament-openings/, single-file databases): each position is readable
            const QString books = QStringLiteral(HYDRA64_SOURCE_DIR "/databases/tournament-openings/");
            const std::pair<const char*, int> expected[] = {{"UHO_2022_6mvs_+110_+119.hy64", 3317}, {"UHO_2022_8mvs_+110_+119.hy64", 8939},
                                                            {"UHO_2022_8mvs_big_+100_+129.hy64", 27073}, {"8moves_v3.hy64", 34700},
                                                            {"2moves_v2.hy64", 12092},                {"Drawkiller_balanced_big.hy64", 15962},
                                                            {"Nunn_10.hy64", 10},                     {"Nunn2_25.hy64", 25}};
            // A path saved before the folders were renamed finds today's collection.
            check(Tournament::loadOpenings(QStringLiteral("/ancien/databases/positions-depart-tournois/Nunn_10.hy64i"), &error).size() == 10
                      && Tournament::loadOpenings(QStringLiteral("/ancien/databases/positions-depart-tournois/2moves_v2.pgn"), &error).size() == 12092,
                  "recueils livrés : ancien chemin (dossier en français, PGN ou base en plusieurs fichiers) reconnu");
            for (const auto& [name, count] : expected) {
                const qsizetype n = Tournament::loadOpenings(books + QLatin1String(name), &error).size();
                check(n == count, QStringLiteral("tournament-openings/%1 : %2 positions sur %3").arg(QLatin1String(name)).arg(n).arg(count).toStdString().c_str());
                // All except Drawkiller (positions only): each opening has its moves, from the initial position.
                if (!QLatin1String(name).startsWith(QLatin1String("Drawkiller"))) {
                    const QStringList list = Tournament::loadOpenings(books + QLatin1String(name), &error);
                    const QString start = QString::fromStdString(Position::startPos().fen()) + QStringLiteral(" moves ");
                    const bool allMoves = std::all_of(list.begin(), list.end(), [&](const QString& o) { return o.startsWith(start); });
                    check(allMoves, qPrintable(QStringLiteral("tournament-openings/%1 : toutes les ouvertures ont leurs coups").arg(QLatin1String(name))));
                }
            }
        }
        // --- Engine panel: move excluded from the search, lock on a position, variations in the notation
        {
            EnginePanel ep;
            ep.setEngine(EngineRegistry::instance().fallbackEngine());
            ep.setMultiPv(1);
            const chess::Position start = chess::Position::startPos();
            ep.setPosition(start);
            ep.setAnalysing(true);
            auto firstMove = [&] {
                const auto lines = ep.lineMoves();
                return lines.isEmpty() || lines.front().empty() ? chess::Move{} : lines.front().front();
            };
            check(waitFor([&] { return !firstMove().isNull(); }, 10000), "panneau moteur : analyse de la position de départ");
            const chess::Move best = firstMove();
            ep.excludeMove(best);
            check(waitFor([&] { return !firstMove().isNull() && !(firstMove() == best); }, 10000) && ep.excluded().size() == 1,
                  "panneau moteur : coup exclu, le moteur cherche les autres (searchmoves)");
            ep.setLocked(true);
            const chess::Position other = start.after(start.parseSan("d4"));
            ep.setPosition(other);
            const bool locked = ep.analysedPosition().fen() == start.fen() && ep.excluded().size() == 1;
            ep.setLocked(false);
            check(locked && ep.analysedPosition().fen() == other.fen() && ep.excluded().isEmpty(),
                  "panneau moteur : verrouillé, il garde sa position ; déverrouillé, il suit l'échiquier");
            ep.setAnalysing(false);
            // Variations added to the notation of the Analysis page
            w4.setPage(MainWindow::PageAnalysis);
            auto* sv = w4.findChild<GameSession*>();
            chess::Game keptLines;
            chess::pgn::parseGame(chess::pgn::writeGame(sv->game()), keptLines);
            const GameOrigin keptLinesOrigin = sv->origin();
            sv->newGame();
            const chess::Position s0 = chess::Position::startPos();
            const std::vector<chess::Move> l1{s0.parseSan("e4"), s0.after(s0.parseSan("e4")).parseSan("c5")};
            const std::vector<chess::Move> l2{s0.parseSan("d4")};
            check(w4.insertEngineLines(s0, {l1, l2}) == 2 && sv->game().root()->children.size() == 2 && sv->game().root()->mainChild()->mainChild(),
                  "panneau moteur : variantes ajoutées à la notation");
            sv->loadGame(std::move(keptLines));
            sv->setOrigin(keptLinesOrigin);
        }
        // --- Game → Annotate game: rules (given evaluations), then Stockfish on a blunder
        {
            using annotation::PositionEval;
            chess::Game g;
            for (const char* san : {"e4", "e5", "Nf3"}) g.playMove(g.current()->pos.parseSan(san));
            // start +0.30; after e4 (Black to move) −0.30; after e5: +5.00 for White → e5 is a blunder;
            // after Nf3 (Black to move) −4.90: engine's move, nothing
            const QList<PositionEval> evals = {{true, false, 30, 20, {QStringLiteral("e2e4")}},
                                               {true, false, -30, 20, {QStringLiteral("c7c5"), QStringLiteral("g1f3")}},
                                               {true, false, 500, 20, {QStringLiteral("g1f3")}},
                                               {true, false, -490, 20, {QStringLiteral("b8c6")}}};
            annotation::Options o;
            const annotation::Summary sum = annotation::apply(g, evals, o, QStringLiteral("Moteur"));
            const std::vector<chess::Node*> line = g.mainline();
            const chess::Node* e5 = line[1];
            const chess::Node* alt = e5->parent->children.size() == 2 ? e5->parent->children[1].get() : nullptr;
            check(sum.blunders == 1 && sum.mistakes == 0 && sum.inaccuracies == 0 && e5->nags == std::vector<int>{4} && line[0]->nags.empty()
                      && line[2]->nags.empty() && alt && alt->san == "c5" && alt->mainChild() && alt->mainChild()->comment == "+0.30"
                      && e5->comment == "+5.00" && g.header("Annotator") == "Moteur" && line[1]->eval.valid && line[1]->eval.value == 500,
                  "annotation : gaffe marquée ??, meilleure suite en variante, évaluations, en-tête Annotator");
            chess::Game g2;
            for (const char* san : {"e4", "e5", "Nf3"}) g2.playMove(g2.current()->pos.parseSan(san));
            o.side = annotation::Options::WhiteOnly;
            check(annotation::apply(g2, evals, o, {}).blunders == 0 && g2.mainline()[1]->nags.empty(), "annotation : camp choisi (Blancs seulement)");
            {
                // Tactical exercises: after the blunder e5, Nf3 is White's exercise; EPD collection reread
                chess::Game g4;
                for (const char* san : {"e4", "e5", "Nf3"}) g4.playMove(g4.current()->pos.parseSan(san));
                g4.setHeader("White", "A");
                g4.setHeader("Black", "B");
                annotation::Options eo;
                eo.markExercises = true;
                const annotation::Summary es = annotation::apply(g4, evals, eo, {});
                const QString epd = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/exercices-test.epd");
                QFile::remove(epd);
                QString err;
                const bool written = annotation::appendExercises(epd, es.exercises, &err);
                const QList<TestPosition> back = solutiontest::load(epd, &err);
                QFile::remove(epd);
                check(es.exercises.size() == 1 && es.exercises.first().contains(QStringLiteral(" bm Nf3;")) && written && back.size() == 1
                          && g4.mainline()[1]->comment.find("Exercice") != std::string::npos,
                      qPrintable(QStringLiteral("annotation : exercice tactique marqué et ajouté au recueil (%1)").arg(es.exercises.value(0))));
            }
            {
                // Book exit: the first two half-moves are "book", not judged, marker on the 2nd
                chess::Game g3;
                for (const char* san : {"e4", "e5", "Nf3"}) g3.playMove(g3.current()->pos.parseSan(san));
                annotation::Options bo;
                bo.bookPlies = 2;
                bo.bookName = QStringLiteral("Fritz");
                const annotation::Summary bs = annotation::apply(g3, evals, bo, {});
                check(bs.blunders == 0 && g3.mainline()[1]->nags.empty() && g3.mainline()[1]->comment.find("Dernier coup du livre (Fritz)") != std::string::npos,
                      "annotation : repère de sortie du livre, coups du livre non jugés");
            }

            // Embedded Stockfish on 1.e4 e5 2.Qh5 Nc6 3.Bc4 Nf6?? 4.Qxf7#
            w4.setPage(MainWindow::PageAnalysis);
            auto* sa = w4.findChild<GameSession*>();
            // game of the Analysis page put back after the test (the restart test depends on it)
            chess::Game kept;
            chess::pgn::parseGame(chess::pgn::writeGame(sa->game()), kept);
            const GameOrigin keptOrigin = sa->origin();
            chess::Game mate;
            for (const char* san : {"e4", "e5", "Qh5", "Nc6", "Bc4", "Nf6", "Qxf7#"}) mate.playMove(mate.current()->pos.parseSan(san));
            sa->loadGame(std::move(mate));
            annotation::Options fast;
            fast.movetimeMs = 200;
            QString summary;
            const bool done = w4.annotateGame(AutoEvaluator::findStockfish(), fast, &summary);
            const std::vector<chess::Node*> ml = sa->game().mainline();
            check(done && ml.size() == 7 && std::find(ml[5]->nags.begin(), ml[5]->nags.end(), 4) != ml[5]->nags.end() && ml[5]->parent->children.size() >= 2
                      && ml[6]->nags.empty() && sa->game().header("Annotator").size() > 0,
                  qPrintable(QStringLiteral("annotation par Stockfish : 3...Nf6 ?? avec la défense en variante, 4.Qxf7# sans marque (%1)").arg(summary)));
            {
                // Batch annotation: two games of a PGN database, saved again annotated
                const QString batch = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/lot.pgn");
                QFile f(batch);
                check(f.open(QIODevice::WriteOnly), "base PGN de l'annotation par lot créée");
                f.write("[Event \"a\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0\n\n"
                        "[Event \"b\"]\n[Result \"1-0\"]\n\n1. f3 e5 2. g4 Qh4# 0-1\n");
                f.close();
                auto* gl = w4.gamesPanel();
                // active database and game of the Databases page put back afterwards (the restart test depends on it)
                const QString activeBefore = gl->model()->filePath();
                chess::Game dbKept;
                chess::pgn::parseGame(chess::pgn::writeGame(sa->gameIn(GameSession::DatabaseSlot)), dbKept);
                const GameOrigin dbOrigin = sa->originIn(GameSession::DatabaseSlot);
                QString batchSummary;
                const int annotated = w4.openDatabase(batch) ? w4.annotateGames({0, 1}, AutoEvaluator::findStockfish(), fast, &batchSummary) : -1;
                chess::Game ga, gb;
                const bool read = gl->model()->parseGame(0, ga, nullptr) && gl->model()->parseGame(1, gb, nullptr);
                auto hasBlunder = [](const chess::Game& g) {
                    for (const chess::Node* n : g.mainline())
                        if (std::find(n->nags.begin(), n->nags.end(), 4) != n->nags.end()) return true;
                    return false;
                };
                check(annotated == 2 && read && !ga.header("Annotator").empty() && !gb.header("Annotator").empty() && hasBlunder(ga) && hasBlunder(gb),
                      qPrintable(QStringLiteral("annotation par lot : deux parties annotées et réenregistrées (%1)").arg(batchSummary)));
                gl->closeCurrent();
                if (!activeBefore.isEmpty()) gl->activate(activeBefore);
                sa->restoreSlot(GameSession::DatabaseSlot, std::move(dbKept), dbOrigin, {});
            }
            {
                // Board as an image (PNG)
                const QString png = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/echiquier-test.png");
                QString ierr;
                const QImage img = w4.boardImage();
                const bool saved = w4.saveBoardImage(png, &ierr);
                const QImage back(png);
                QFile::remove(png);
                check(!img.isNull() && img.width() > 100 && saved && back.size() == img.size(),
                      qPrintable(QStringLiteral("échiquier en image : %1 × %2, PNG enregistré").arg(img.width()).arg(img.height())));
            }
            {
                // Piece tracking: h1 rook (castling), e2 pawn (captured), white knights
                chess::Game tg;
                chess::pgn::parseGame("1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. O-O Nxe4 5. Re1 *", tg);
                using PT = PieceTracker;
                int positions = 0;
                const auto rook = PT::track(tg, {chess::parseSquare("h1"), chess::NoPiece}, 0, -1, &positions);
                const auto pawn = PT::track(tg, {chess::parseSquare("e2"), chess::NoPiece}, 0, -1);
                const auto knights = PT::track(tg, {chess::NoSquare, chess::makePiece(chess::White, chess::Knight)}, 0, -1);
                int pawnTotal = 0, knightTotal = 0;
                for (int c : pawn) pawnTotal += c;
                for (int c : knights) knightTotal += c;
                const auto sq = [](const char* n) { return size_t(chess::parseSquare(n)); };
                auto* dlg = new PieceTracker(tg, &w4);
                dlg->setSpec({chess::parseSquare("h1"), chess::NoPiece});
                const bool same = dlg->counts() == rook;
                delete dlg;
                check(positions == 10 && rook[sq("h1")] == 7 && rook[sq("f1")] == 2 && rook[sq("e1")] == 1 && pawn[sq("e4")] == 7 && pawnTotal == 8
                          && knightTotal == 20 && same,
                      "suivi des pièces : tour du roque h1 → f1 → e1, pion e4 pris, cavaliers blancs");
            }
            {
                // Calculation of variations: Rd8# evaluated +− (right), Rd2 evaluated = (to review), engine lines
                chess::Position cp;
                cp.setFen("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
                CalcVarDialog cv(cp, &w4);
                cv.playMove(cp.parseSan("Rd8"));
                cv.finishVariation(300);
                cv.playMove(cp.parseSan("Rd2"));
                cv.finishVariation(0);
                cv.judge(150);
                const bool judged = waitFor([&] { return !cv.isJudging(); }, 20000);
                const auto& vs = cv.variations();
                check(judged && vs.size() == 2 && vs[0].judged && vs[0].engineMate && vs[1].judged && vs[1].engineEval > 200 && !cv.startLines().isEmpty(),
                      qPrintable(QStringLiteral("calcul de variantes : variantes jugées (%1, %2), lignes du moteur").arg(
                          CalcVarDialog::evalText(vs.value(0).engineEval, vs.value(0).engineMate), CalcVarDialog::evalText(vs.value(1).engineEval))));
            }
            {
                // Imposed opening: ECO code → line, invalid moves refused
                const QString b90 = PlayDialog::openingLine(QStringLiteral("B90"));
                const QString typed = PlayDialog::openingLine(QStringLiteral("1.e4 e5 2.Nf3 Nc6"));
                check(b90.startsWith(QStringLiteral("1. e4 c5 2. Nf3 d6")) && typed == QStringLiteral("1. e4 e5 2. Nf3 Nc6")
                          && PlayDialog::openingLine(QStringLiteral("1. e4 e4")).isEmpty() && PlayDialog::openingLine(QStringLiteral("Z99")).isEmpty(),
                      qPrintable(QStringLiteral("ouverture imposée : B90 = « %1 »").arg(b90)));
            }
            {
                // Arrows and marked squares: drawn with the right-click, they belong to the displayed move,
                // come back when one returns to it and go into the PGN
                {
                    chess::Game plain;
                    chess::Node* m1 = plain.addMove(plain.root(), plain.root()->pos.parseSan("e4"));
                    plain.addMove(m1, m1->pos.parseSan("e5"));
                    sa->loadGame(std::move(plain));
                    sa->goTo(sa->game().mainline()[0]);
                    QApplication::processEvents();
                    BoardWidget* bw = w4.findChild<BoardViewer*>()->board();
                    chess::Node* firstMove = sa->game().mainline()[0];
                    auto drag = [&](const char* a, const char* b, Qt::KeyboardModifiers mods) {
                        const QPointF pa = bw->squareCenter(chess::parseSquare(a)), pb = bw->squareCenter(chess::parseSquare(b));
                        QMouseEvent press(QEvent::MouseButtonPress, pa, bw->mapToGlobal(pa), Qt::RightButton, Qt::RightButton, mods);
                        QMouseEvent release(QEvent::MouseButtonRelease, pb, bw->mapToGlobal(pb), Qt::RightButton, Qt::NoButton, mods);
                        QApplication::sendEvent(bw, &press);
                        QApplication::sendEvent(bw, &release);
                    };
                    drag("g1", "f3", Qt::NoModifier);      // green arrow
                    drag("d5", "d5", Qt::ShiftModifier);   // red square
                    drag("a2", "a4", Qt::ControlModifier); // yellow arrow…
                    drag("a2", "a4", Qt::ControlModifier); // … removed by the same gesture
                    const chess::Node* shown = sa->current();
                    const bool drawn = shown->graphics.size() == 2 && shown->graphics[0] == chess::Graphic{chess::parseSquare("g1"), chess::parseSquare("f3"), 'G'}
                                       && shown->graphics[1] == chess::Graphic{chess::parseSquare("d5"), chess::NoSquare, 'R'};
                    sa->goTo(sa->game().mainline()[1]);
                    QApplication::processEvents();
                    const bool cleared = bw->graphics().empty();
                    sa->goTo(sa->game().mainline()[0]);
                    QApplication::processEvents();
                    const bool back = bw->graphics().size() == 2;
                    const std::string saved = chess::pgn::writeGame(sa->game());
                    check(drawn && cleared && back && saved.find("{[%csl Rd5][%cal Gg1f3]}") != std::string::npos,
                          qPrintable(QStringLiteral("flèches et cases marquées : dessinées, attachées au coup, enregistrées (%1 sur le coup)").arg(shown->graphics.size())));
                }
                // ChessBase texts: HTML brought down to plain text; with HYDRA64_CB_TEXTS, texts of a real database
                {
                    const QString plain = cbtexts::htmlToPlain(QStringLiteral("<html><head><title>x</title><style>p{color:red}</style></head><body><p align=\"left\">Caf&eacute; &amp; th&#233;,\n suite</p><p>fin&nbsp;&mdash; <b>ici</b><br>l&#xE0;</p></body></html>"));
                    check(plain == QStringLiteral("Café & thé, suite\nfin — ici\nlà"), qPrintable(QStringLiteral("textes ChessBase : HTML en texte nu (« %1 »)").arg(QString(plain).replace('\n', '|'))));
                    const QString cbh = qEnvironmentVariable("HYDRA64_CB_TEXTS");
                    if (!cbh.isEmpty() && ChessBaseSource::available()) {
                        ChessBaseGameSource src;
                        QString err;
                        const bool opened = src.open(cbh, &err);
                        const int texts = src.textCount(), viaScan = cbtexts::count(cbh);
                        int asText = 0, withMoves = 0, emptyText = 0, cyrillicComments = 0, garbled = 0;
                        QString firstTitle;
                        for (uint64_t i = 0; opened && i < src.count(); ++i) {
                            chess::Game g;
                            if (!src.readForEncoding(i, g, nullptr)) continue;
                            if (g.header("Event") == "Texte" && !g.root()->mainChild()) {
                                if (!asText++) firstTitle = QString::fromStdString(g.header("White"));
                                emptyText += g.root()->comment.empty();
                            } else if (g.root()->mainChild()) {
                                ++withMoves;
                                // comments in Windows-1251: read as Cyrillic, not as "Äåáþò"
                                std::function<void(const chess::Node*)> scan = [&](const chess::Node* n) {
                                    const QString c = QString::fromStdString(n->comment);
                                    for (const QChar ch : c) {
                                        if (ch.unicode() >= 0x0410 && ch.unicode() <= 0x044F) { ++cyrillicComments; break; }
                                    }
                                    garbled += c.contains(QStringLiteral("îñ")) || c.contains(QStringLiteral("åð"));
                                    for (const auto& child : n->children) scan(child.get());
                                };
                                scan(g.root());
                            }
                        }
                        if (cbh.contains(QStringLiteral("My system"))) check(cyrillicComments > 100 && garbled == 0, qPrintable(QStringLiteral("commentaires ChessBase en cyrillique : %1 lus, %2 en charabia").arg(cyrillicComments).arg(garbled)));
                        check(opened && texts > 0 && texts <= viaScan && asText == texts && emptyText == 0 && withMoves > 0 && uint64_t(texts) < src.count(),
                              qPrintable(QStringLiteral("textes ChessBase : %1 textes gardés parmi %2 enregistrements (%3 parties avec coups), premier : « %4 »").arg(texts).arg(src.count()).arg(withMoves).arg(firstTitle)));
                    }
                }
                // Moves quoted in a comment: rewritten in the chosen notation, ordinary words untouched
                {
                    const QString text = QStringLiteral("Better was 18...Qh5! or Nbd7, Bxf7+ and exd8=Q#; not B2B, Kings, e4 or Rome. O-O is fine, Qd1-h5 too.");
                    const QString fr = display::comment(text, prefs::Notation::French);
                    const QString fig = display::comment(text, prefs::Notation::Figurine);
                    // ChessBase private-use figurines and signs (U+E0xx)
                    const QString cb = QString::fromUtf8("Anticipe g4g5 mais le \xEE\x80\xA8" "d7 g\xC3\xAAne le \xEE\x80\xA7" "f8, \xEE\x80\xA4" "g1 \xEE\x80\x8A");
                    const QString cbFr = display::comment(cb, prefs::Notation::French), cbEn = display::comment(cb, prefs::Notation::English), cbFig = display::comment(cb, prefs::Notation::Figurine);
                    check(cbFr == QStringLiteral("Anticipe g4g5 mais le Cd7 gêne le Ff8, Rg1 ±") && cbEn == QStringLiteral("Anticipe g4g5 mais le Nd7 gêne le Bf8, Kg1 ±") && cbFig.contains(QChar(0x2658)) && !cbFig.contains(QChar(0xE028)),
                          qPrintable(QStringLiteral("symboles privés de ChessBase dans les commentaires : « %1 »").arg(cbFr)));
                    check(fr == QStringLiteral("Better was 18...Dh5! or Cbd7, Fxf7+ and exd8=D#; not B2B, Kings, e4 or Rome. O-O is fine, Dd1-h5 too.")
                              && display::comment(text, prefs::Notation::English) == text && fig.contains(QChar(0x265B)) && !fig.contains(QStringLiteral("Qh5")) && fig.contains(QStringLiteral("Kings")),
                          qPrintable(QStringLiteral("coups dans les commentaires : « %1 »").arg(fr)));
                }
                // ChessBase commands in comments: never displayed; the profile feeds the graph
                {
                    auto* session = w4.findChild<GameSession*>();
                    chess::Game kept;
                    chess::pgn::parseGame(chess::pgn::writeGame(session->game()), kept);
                    const GameOrigin keptOrigin = session->origin();
                    const std::vector<int> keptPath = GameSession::pathTo(session->current());
                    chess::Game evp;
                    chess::pgn::parseGame("[White \"A\"]\n[Black \"B\"]\n\n{[%evp 0,3,21,30,-49,61] Ronde 3} 1. e4 {[%emt 0:00:12]} e5 2. Nf3 {[%eval 55,20] naturel} *\n", evp);
                    session->loadGame(std::move(evp));
                    QCoreApplication::processEvents();
                    QString shown;
                    for (QTextEdit* e : w4.findChildren<QTextEdit*>())
                        if (e->toPlainText().contains(QStringLiteral("naturel"))) shown = e->toPlainText();
                    const auto line = session->game().mainline();
                    const bool evals = session->game().root()->eval.valid && line.size() == 3 && line[1]->eval.value == -49 && line[2]->eval.valid;
                    const bool stored = session->game().root()->comment.find("[%evp") != std::string::npos;
                    check(!shown.isEmpty() && !shown.contains(QStringLiteral("[%")) && shown.contains(QStringLiteral("Ronde 3")) && evals && stored
                              && (!prefs::showEngineComments() || shown.contains(QStringLiteral("12 s"))), // time spent ([%emt]) shown as a time
                          "commandes ChessBase ([%evp], [%emt], [%eval]) : masquées dans la notation, gardées dans la partie, évaluations reportées");
                    session->restoreSlot(session->slot(), std::move(kept), keptOrigin, keptPath);
                }
                // Training: two questions, one found on the second attempt, the other through the solution
                {
                    chess::Game quiz;
                    chess::pgn::parseGame("[White \"A\"]\n[Black \"B\"]\n\n1. e4 e5 2. Nf3 {[%tqu \"\",\"Défendez e5.\",\"\",\"\",\"b8c6\",\"\",10] bien} Nc6 3. Bb5 {[%tqu \"\",\"\",\"\",\"\",\"f1b5\",\"\",10]} a6 *\n", quiz);
                    QuizDialog training(quiz);
                    const int count = training.questionCount();
                    // The question bears on the move it gives as the solution (b8c6), not on the move it follows
                    const bool firstIsNf3 = training.expected() && training.expected()->san == "Nc6";
                    const bool wrong = training.answer({chess::parseSquare("d7"), chess::parseSquare("d6")});
                    const bool right = training.answer({chess::parseSquare("b8"), chess::parseSquare("c6")});
                    const int afterFirst = training.score();
                    training.next();
                    training.showSolution();
                    const int afterSecond = training.score();
                    training.next();
                    chess::Game none;
                    chess::pgn::parseGame("\n1. e4 e5 *\n", none);
                    QuizDialog empty(none);
                    check(count == 2 && firstIsNf3 && !wrong && right && afterFirst == 5 && afterSecond == 5 && training.finished() && empty.questionCount() == 0 && empty.finished(),
                          qPrintable(QStringLiteral("entraînement : %1 questions, score %2 / %3").arg(count).arg(training.score()).arg(training.maxScore())));
                }
                // PDF: game with a variation, a comment and a diagram at the "▦" marker
                {
                    chess::Game paper;
                    chess::pgn::parseGame("[White \"Anderssen, Adolf\"]\n[Black \"Kieseritzky, Lionel\"]\n[Site \"Londres\"]\n[Date \"1851.06.21\"]\n[Result \"1-0\"]\n\n"
                                          "1. e4 e5 2. f4 exf4 3. Bc4 $220 {le gambit du fou} Qh4+ (3... d5 4. Bxd5) 4. Kf1 b5 1-0\n", paper);
                    QTextDocument doc;
                    int diagrams = 0;
                    w4.fillGameDocument(doc, paper, &diagrams);
                    const QString text = doc.toPlainText();
                    const QString pdfPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/partie.pdf");
                    QFile::remove(pdfPath);
                    const bool written = w4.exportGamePdf(paper, pdfPath);
                    QFile pf(pdfPath);
                    (void)pf.open(QIODevice::ReadOnly);
                    const QByteArray head = pf.read(5);
                    check(diagrams == 1 && text.contains(QStringLiteral("Anderssen, Adolf – Kieseritzky, Lionel")) && text.contains(QStringLiteral("le gambit du fou")) && text.contains(QStringLiteral("("))
                              && written && head == "%PDF-" && pf.size() > 5000,
                          qPrintable(QStringLiteral("PDF d'une partie : %1 diagramme(s), %2 octets").arg(diagrams).arg(pf.size())));
                }
                // Undo / redo: variation deleted, comment, move added, arrow; the origin of the
                // game is kept, the history starts from scratch when another game is loaded
                {
                    chess::Game edited;
                    chess::pgn::parseGame("[Event \"Annuler\"]\n\n1. e4 e5 (1... c5 2. Nf3 {sicilienne}) 2. Nf3 Nc6 *\n", edited);
                    sa->loadGame(std::move(edited));
                    GameOrigin from;
                    from.path = QStringLiteral("/tmp/base.pgn");
                    from.row = 4;
                    sa->setOrigin(from);
                    const std::string original = chess::pgn::writeGame(sa->game());
                    const bool clean = !sa->canUndo() && !sa->canRedo();
                    sa->deleteNode(sa->game().root()->children[0]->children[1].get()); // the variation 1... c5
                    const std::string withoutVariation = chess::pgn::writeGame(sa->game());
                    sa->setComment(sa->game().mainline()[0], QStringLiteral("ouverture du pion roi"));
                    sa->goTo(sa->game().mainline().back());
                    sa->playMove(sa->current()->pos.parseSan("Bb5"));
                    sa->setGraphics(sa->current(), {chess::Graphic{chess::parseSquare("b5"), chess::parseSquare("c6"), 'R'}});
                    const std::string final = chess::pgn::writeGame(sa->game());
                    const bool changed = withoutVariation.find("c5") == std::string::npos && final.find("Bb5 {[%cal Rb5c6]}") != std::string::npos && sa->canUndo();
                    int undone = 0;
                    while (sa->undo()) ++undone;
                    const bool restored = undone == 4 && chess::pgn::writeGame(sa->game()) == original && sa->origin().row == 4 && sa->canRedo() && !sa->canUndo();
                    int redone = 0;
                    while (sa->redo()) ++redone;
                    const bool again = redone == 4 && chess::pgn::writeGame(sa->game()) == final && sa->current()->san == "Bb5";
                    sa->undo();
                    sa->playMove(sa->current()->pos.parseSan("a6")); // a new change erases what was left to redo
                    const bool branch = !sa->canRedo() && sa->canUndo();
                    sa->loadGame(chess::Game());
                    check(clean && changed && restored && again && branch && !sa->canUndo() && !sa->canRedo(),
                          qPrintable(QStringLiteral("annuler / rétablir : %1 modifications annulées puis %2 rétablies").arg(undone).arg(redone)));
                }
                // Notation: engine notes (tournament games) hidden on request, kept in the game
                {
                    chess::Game noted;
                    chess::Node* n1 = noted.addMove(noted.root(), noted.root()->pos.parseSan("e4"));
                    n1->comment = "+0.23/31 2.6s";
                    chess::Node* n2 = noted.addMove(n1, n1->pos.parseSan("e5"));
                    n2->comment = "#-8/63 1.3s une idée";
                    sa->loadGame(std::move(noted));
                    auto* notation = w4.findChild<NotationView*>();
                    auto* toggle = w4.findChild<QToolButton*>(QStringLiteral("EngineNotes"));
                    QApplication::processEvents();
                    const QString with = notation ? notation->toPlainText() : QString();
                    if (toggle) toggle->setChecked(false);
                    QApplication::processEvents();
                    const QString without = notation ? notation->toPlainText() : QString();
                    const bool kept = sa->game().root()->mainChild()->comment == "+0.23/31 2.6s" && chess::pgn::writeGame(sa->game()).find("+0.23/31 2.6s") != std::string::npos;
                    if (toggle) toggle->setChecked(true);
                    check(toggle && with.contains(QStringLiteral("+0.23/31")) && with.contains(QStringLiteral("#-8/63")) && !without.contains(QStringLiteral("/31")) && !without.contains(QStringLiteral("/63"))
                              && without.contains(QStringLiteral("une idée")) && kept,
                          "notation : notes des moteurs masquées par le bouton, gardées dans la partie et le PGN");
                }
                // Notation: the pre-move comment is displayed in front of its move (inside the variation for the
                // first move of a variation), can be edited and undone
                {
                    chess::Game pre;
                    chess::pgn::parseGame("1. e4 e5 {APRES} {AVANT1} 2. Nf3 ({AVANT2} 2. Bc4) *", pre);
                    sa->loadGame(std::move(pre));
                    auto* notation = w4.findChild<NotationView*>();
                    QApplication::processEvents();
                    const QString shown = notation ? notation->toPlainText() : QString();
                    const auto line = sa->game().mainline();
                    const bool model = line.size() == 3 && line[2]->preComment == "AVANT1" && line[1]->children.size() == 2 && line[1]->children[1]->preComment == "AVANT2";
                    const bool order = shown.indexOf(QStringLiteral("APRES")) >= 0 && shown.indexOf(QStringLiteral("APRES")) < shown.indexOf(QStringLiteral("AVANT1"))
                                       && shown.indexOf(QStringLiteral("AVANT1")) < shown.indexOf(QStringLiteral("f3")) && shown.indexOf(QLatin1Char('(')) < shown.indexOf(QStringLiteral("AVANT2"))
                                       && shown.indexOf(QStringLiteral("AVANT2")) < shown.indexOf(QStringLiteral("c4"));
                    bool edited = false, undone = false;
                    if (model) {
                        sa->setPreComment(line[2], QStringLiteral("CHANGE"));
                        QApplication::processEvents();
                        edited = notation && notation->toPlainText().contains(QStringLiteral("CHANGE")) && !notation->toPlainText().contains(QStringLiteral("AVANT1"));
                        undone = sa->undo() && sa->game().mainline().size() == 3 && sa->game().mainline()[2]->preComment == "AVANT1";
                    }
                    sa->loadGame(chess::Game());
                    check(model && order && edited && undone, qPrintable(QStringLiteral("notation : commentaire d'avant-coup affiché devant son coup, modifié, annulé — « %1 »").arg(shown.simplified().right(90))));
                }
                // Game → Finish game. The tests' analysis engine is Shuffle, which plays almost at
                // random: with king and queen against king it mates, stalemates, loses its queen… or goes round in circles
                // up to the limit. Two things are therefore checked separately:
                // 1) position where the only legal move (Kxg1) leaves king against king: certain end by rule
                chess::Position forced;
                forced.setFen("8/8/8/8/8/7k/8/6rK w - - 0 1");
                sa->loadGame(chess::Game(forced));
                const bool started1 = w4.finishGame(100, 60);
                const bool done1 = started1 && waitFor([&] { return !w4.autoPlayer()->isRunning(); }, 20000);
                chess::GameEnd ge;
                check(done1 && w4.autoPlayer()->played() == 1 && chess::detectGameEnd(sa->current(), ge) && ge.kind == chess::GameEnd::InsufficientMaterial
                          && sa->current()->pos.fen() == "8/8/8/8/8/7k/8/6K1 b - - 0 1",
                      qPrintable(QStringLiteral("finir la partie : coup forcé puis fin réglementaire (%1 demi-coup, %2)").arg(w4.autoPlayer()->played()).arg(w4.autoPlayer()->lastReason())));
                // 2) king and queen against king: the engine plays both sides, legal moves, and stops
                //    on an end by rule or on the half-move limit, never beyond
                chess::Position kq;
                kq.setFen("7k/8/5K2/8/8/8/8/1Q6 w - - 0 1");
                sa->loadGame(chess::Game(kq));
                const bool started = w4.finishGame(100, 60);
                const bool done = started && waitFor([&] { return !w4.autoPlayer()->isRunning(); }, 40000);
                const int played = w4.autoPlayer()->played();
                const bool ended = chess::detectGameEnd(sa->current(), ge);
                check(done && played > 0 && played <= 60 && int(sa->game().mainline().size()) == played && (ended || played == 60),
                      qPrintable(QStringLiteral("finir la partie : le moteur joue les deux camps jusqu'à la fin ou la limite (%1 demi-coups, %2)").arg(played).arg(w4.autoPlayer()->lastReason())));
            }
            sa->loadGame(std::move(kept));
            sa->setOrigin(keptOrigin);
        }
        // --- Deep analysis (in the manner of IDeA): embedded Stockfish, 2 instances, 0.1 s per task
        {
            w4.setPage(MainWindow::PageAnalysis);
            auto* sd = w4.findChild<GameSession*>();
            chess::Game keptGame;
            chess::pgn::parseGame(chess::pgn::writeGame(sd->game()), keptGame);
            const GameOrigin keptOrigin = sd->origin();
            sd->newGame();
            DeepAnalysis* da = w4.deepAnalysis();
            DeepPanel* dp = w4.deepPanel();
            DeepAnalysis::EngineSlot slot;
            slot.engineId = EngineRegistry::instance().fallbackEngine().id;
            slot.count = 2;
            slot.threads = 1;
            slot.hashMb = 16;
            da->setEngines({slot});
            deep::Options o;
            o.quality.minTimeMs = 100;
            o.maxPlies = 8;
            o.widthCp = 40;
            QString err;
            const int pi = da->createProject(QStringLiteral("Essai"), chess::Position::startPos(), o, {}, &err);
            da->setDefaultProject(pi);
            deep::Tree& tree = da->project(pi).tree;
            const uint32_t root = tree.roots()[0].node;
            const bool started = pi >= 0 && da->start(&err);
            const bool grew = started && waitFor([&] { return tree.size() > 25 && tree.node(root).edges.size() >= 2; }, 90000);
            check(grew && QFileInfo::exists(da->info(pi).path),
                  qPrintable(QStringLiteral("analyse approfondie : l'arbre grandit (%1 positions, %2 coups à la racine)").arg(tree.size()).arg(tree.node(root).edges.size())));
            // the panel refreshes in a deferred way (250 ms) while the tree grows
            check(waitFor([&] { return dp->treeTable()->rowCount() >= 2 && dp->treeTable()->rowCount() <= int(tree.node(root).edges.size()); }, 5000),
                  "analyse approfondie : le panneau montre les coups de l'arbre (position de départ)");

            // Red move: its branch no longer grows
            const deep::Edge* best = tree.bestEdge(root);
            chess::Move redMove;
            for (const deep::Edge& e : tree.node(root).edges)
                if (&e != best) redMove = e.move;
            dp->setMark(redMove, deep::Mark::Exclude);
            const uint32_t redChild = tree.edge(root, redMove)->child;
            // A search already started on this branch at the time of marking still finishes (and may add
            // moves to it): the reference is taken once the branch has been stable for a second, not at the moment of marking.
            size_t redEdges = tree.node(redChild).edges.size();
            for (int stable = 0, spent = 0; stable < 20 && spent < 10000; spent += 50) {
                wait(50);
                const size_t now = tree.node(redChild).edges.size();
                stable = now == redEdges ? stable + 1 : 0;
                redEdges = now;
            }
            wait(1500);
            check(tree.node(redChild).edges.size() == redEdges && tree.edge(root, redMove)->mark == deep::Mark::Exclude,
                  "analyse approfondie : un coup marqué rouge n'est plus prolongé");

            // Manual task: 1.h4 h5, analysed first
            sd->playMove(sd->current()->pos.parseSan("h4"));
            sd->playMove(sd->current()->pos.parseSan("h5"));
            const chess::Position manualPos = sd->current()->pos;
            check(dp->sendCurrentPosition() && waitFor([&] {
                      const uint32_t n = tree.find(manualPos);
                      return n != deep::NoNode && tree.node(n).eval.valid;
                  }, 15000),
                  "analyse approfondie : « Position » envoie une tâche manuelle, traitée en premier");

            {
                // Game → Identify the opening (game of the Analysis page: 1.h4 h5)
                const QString id = w4.identifyOpening();
                check(id.contains(QStringLiteral("A00")) && sd->game().header("ECO") == "A00", qPrintable(QStringLiteral("identifier l'ouverture : %1").arg(id)));
            }
            // F4: best line of the tree into the notation
            sd->toStart();
            const std::vector<chess::Move> line = tree.bestLine(root);
            dp->copyBestLine();
            bool inNotation = !line.empty();
            const chess::Node* cur = sd->game().root();
            for (const chess::Move& m : line) {
                const chess::Node* next = nullptr;
                for (const auto& c : cur->children)
                    if (c->move == m) next = c.get();
                if (!next) inNotation = false;
                if (!next) break;
                cur = next;
            }
            check(inNotation, "analyse approfondie : F4 copie la meilleure ligne dans la notation");

            // Spectator lines: sent if the project asks for them
            da->project(pi).options.insertSpectators = true;
            da->project(pi).options.spectatorMinTimeMs = 0;
            da->project(pi).options.spectatorMinDepth = 0;
            EngineLine spec;
            spec.depth = 12;
            spec.pv = {QStringLiteral("g2g3")};
            da->spectatorLine(sd->game(), sd->current(), spec);
            const std::vector<chess::Move> g3Path{chess::Position::startPos().parseSan("g3")};
            bool sent = false;
            for (const deep::Task& t : da->project(pi).queue()) sent |= t.path == g3Path;
            for (const DeepAnalysis::Running& r : da->running()) sent |= r.project == pi && r.task.path == g3Path;
            check(sent, "analyse approfondie : ligne d'un spectateur envoyée à l'arbre");
            da->project(pi).options.insertSpectators = false;

            // Stop then reread: identical tree and queue
            da->stop();
            const size_t nodes = tree.size(), pending = da->project(pi).queue().size();
            {
                DeepAnalysis other;
                other.loadAll();
                const int j = other.indexOf(da->info(pi).path);
                check(j >= 0 && other.project(j).tree.size() == nodes && other.project(j).queue().size() == pending && other.info(j).name == QStringLiteral("Essai"),
                      qPrintable(QStringLiteral("analyse approfondie : projet relu à l'identique (%1 positions, %2 tâches en attente)").arg(nodes).arg(pending)));
            }

            // Exports: PGN, Hydra64 tree (evaluations), Polyglot (red move at 0)
            const QString exportDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/export");
            QDir().mkpath(exportDir);
            const QString pgnOut = exportDir + QStringLiteral("/essai.pgn"), bookOut = exportDir + QStringLiteral("/essai.hy64book"),
                          binOut = exportDir + QStringLiteral("/essai.bin");
            chess::Game exported;
            QFile pf(pgnOut);
            const bool pgnOk = da->exportPgn(pi, pgnOut, &err) && pf.open(QIODevice::ReadOnly) && chess::pgn::parseGame(pf.readAll().toStdString(), exported)
                               && exported.root()->children.size() >= 2;
            check(pgnOk, "analyse approfondie : export PGN (meilleurs coups et variantes)");
            chess::treebook::Book book;
            bool evals = da->exportBook(pi, bookOut, chess::treebook::TreeFormat::Hy64Book, &err) && book.open(bookOut.toStdString());
            const auto bookMoves = evals ? book.moves(chess::Position::startPos()) : std::vector<chess::treebook::BookMove>{};
            evals = evals && !bookMoves.empty() && std::all_of(bookMoves.begin(), bookMoves.end(), [](const auto& m) { return m.hasEval; });
            check(evals, "analyse approfondie : export en arbre d'ouverture Hydra64, avec les évaluations");
            chess::polyglot::Book bin;
            bool zero = false;
            if (da->exportBook(pi, binOut, chess::treebook::TreeFormat::Polyglot, &err) && bin.open(binOut.toStdString()))
                for (const auto& m : bin.moves(chess::Position::startPos())) zero |= m.move == redMove && m.weight == 0;
            check(zero, "analyse approfondie : export Polyglot, poids 0 pour le coup rouge");
            {
                auto* bp = w4.bookPanel();
                bp->openBook(bookOut);
                check(bp->format() == BookPanel::Tree && !bp->rows().isEmpty() && bp->rows().front().hasEval,
                      "analyse approfondie : l'arbre exporté s'ouvre dans le panneau Arbre d'ouverture (colonne Éval)");
                bp->closeBook();
            }

            // EPD mode (no expansion) and two active projects at 70 / 30%
            deep::Options epdOptions = o;
            epdOptions.autoExpand = false;
            const int pe = da->createProject(QStringLiteral("EPD"), chess::Position::startPos(), epdOptions, {}, &err);
            da->project(pe).tree.roots()[0].enabled = false;
            const int nEpd = da->project(pe).addEpd("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -\n"
                                                    "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -\n"
                                                    "8/8/8/4k3/8/8/4P3/4K3 w - -\n",
                                                    false);
            da->setStatus(pi, DeepAnalysis::Status::Inactive);
            da->start(&err);
            const deep::Tree& et = da->project(pe).tree;
            const bool epdDone = waitFor([&] {
                int evaluated = 0;
                for (size_t r = 1; r < et.roots().size(); ++r) evaluated += et.node(et.roots()[r].node).eval.valid;
                return evaluated == 3;
            }, 20000);
            bool expanded = false;
            for (size_t r = 1; r < et.roots().size(); ++r) expanded |= !et.node(et.roots()[r].node).edges.empty();
            check(nEpd == 3 && epdDone && !expanded, "analyse approfondie : mode EPD, trois positions évaluées sans expansion");
            da->stop();
            const chess::Position d4 = chess::Position::startPos().after(chess::Position::startPos().parseSan("d4"));
            const int pb = da->createProject(QStringLiteral("Trente"), d4, o, {}, &err);
            da->setStatus(pi, DeepAnalysis::Status::Share, 70);
            da->setStatus(pb, DeepAnalysis::Status::Share, 30);
            da->setStatus(pe, DeepAnalysis::Status::Inactive);
            da->start(&err);
            // Enough cumulated processor time to judge the distribution (rather than a fixed 5 s wait)
            waitFor([&] { return da->info(pi).sessionCpuMs + da->info(pb).sessionCpuMs >= 5000; }, 10000);
            const qint64 a = da->info(pi).sessionCpuMs, b = da->info(pb).sessionCpuMs;
            da->stop();
            check(a > b && b > 0, qPrintable(QStringLiteral("analyse approfondie : deux projets à 70 / 30 % du CPU (%1 / %2 ms)").arg(a).arg(b)));

            if (const int seconds = qEnvironmentVariableIntValue("HYDRA64_TEST_DEEP_SECONDS"); seconds > 0) {
                // Real (long) trial: Sicilian, half the cores, 1 s per position
                da->setEngines(DeepAnalysis::defaultEngines());
                deep::Options real;
                real.quality.minTimeMs = 1000;
                chess::Position sic = chess::Position::startPos();
                for (const char* san : {"e4", "c5"}) sic.makeMove(sic.parseSan(san));
                const int pr = da->createProject(QStringLiteral("Sicilienne"), sic, real, {}, &err);
                for (int k : {pi, pb, pe}) da->setStatus(k, DeepAnalysis::Status::Inactive);
                da->setDefaultProject(pr);
                da->start(&err);
                wait(seconds * 1000);
                da->stop();
                deep::Tree& rt = da->project(pr).tree;
                rt.minimaxAll();
                const uint32_t r0 = rt.roots()[0].node;
                QStringList moves;
                for (const deep::Edge& e : rt.node(r0).edges)
                    moves << QStringLiteral("%1 %2").arg(QString::fromStdString(sic.toSan(e.move))).arg(deepScoreText(rt.moveScore(e), sic.sideToMove()));
                std::vector<chess::Move> bl = rt.bestLine(r0);
                QStringList san;
                chess::Position walk = sic;
                for (const chess::Move& m : bl) {
                    san << QString::fromStdString(walk.toSan(m));
                    walk.makeMove(m);
                }
                std::printf("Essai réel %d s, %d instances : %zu positions, %lld tâches, %d étapes, racine %s\n  coups : %s\n  meilleure ligne : %s\n", seconds,
                            DeepAnalysis::defaultEngines()[0].count, rt.size(), (long long)da->info(pr).sessionTasks, da->project(pr).stage(),
                            qPrintable(deepScoreText(rt.node(r0).score, sic.sideToMove())), qPrintable(moves.join(QStringLiteral(", "))),
                            qPrintable(san.join(QLatin1Char(' '))));
                da->deleteProject(pr, &err);
            }
            for (int k : {pb, pe}) da->deleteProject(da->indexOf(da->info(k).path), &err);
            sd->loadGame(std::move(keptGame));
            sd->setOrigin(keptOrigin);
        }
        // --- Engine → Solution test: EPD / Hydra64 collections, engine on a few positions
        {
            const QString dir = QDir::tempPath() + QStringLiteral("/hydra64-solutions");
            QDir().mkpath(dir);
            const QString epd = dir + QStringLiteral("/test.epd");
            {
                QFile f(epd);
                check(f.open(QIODevice::WriteOnly), "fichier EPD de test créé");
                f.write("2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - bm Qg6; id \"WAC.001\";\n"
                        "r1b1kb1r/3q1ppp/pBp1pn2/8/Np3P2/5B2/PPP3PP/R2Q1RK1 w kq - am Nc5; id \"am\";\n"
                        "8/8/8/8/8/8/8/K6k w - -\n");
            }
            QString err;
            const QList<TestPosition> fromEpd = solutiontest::load(epd, &err);
            check(fromEpd.size() == 2 && fromEpd[0].id == QStringLiteral("WAC.001") && fromEpd[0].best == QStringList{QStringLiteral("g3g6")}
                      && fromEpd[0].solution == QStringLiteral("Qg6") && fromEpd[1].avoid == QStringList{QStringLiteral("a4c5")}
                      && solutiontest::isCorrect(fromEpd[1], QStringLiteral("d1d7")) && !solutiontest::isCorrect(fromEpd[1], QStringLiteral("a4c5")),
                  "test de solutions : EPD lu (bm, am, id ; position sans solution ignorée)");
            const QString marathon = QStringLiteral(HYDRA64_SOURCE_DIR "/databases/engine-tests/Tactics_Marathon_210.hy64");
            const QList<TestPosition> suite = solutiontest::load(marathon, &err);
            const bool withAvoid = std::any_of(suite.begin(), suite.end(), [](const TestPosition& p) { return !p.avoid.isEmpty(); });
            check(suite.size() == 210 && suite[0].id == QStringLiteral("946 ECM") && suite[0].solution == QStringLiteral("Bg4") && withAvoid,
                  "test de solutions : Tactics Marathon (210 positions, coups à éviter des variantes « ? »)");
            const QString fakeCbh = dir + QStringLiteral("/m.cbh");
            { QFile f(fakeCbh); (void)f.open(QIODevice::WriteOnly); }
            check(solutiontest::load(fakeCbh, &err).isEmpty() && err.contains(QStringLiteral("Convertir")), "test de solutions : format propriétaire refusé");

            // Engine (embedded Stockfish) on 6 positions, 0.3 s each, from the dialog
            SolutionTestDialog* dlg = w4.solutionTestDialog();
            const QString six = dir + QStringLiteral("/six.pgn");
            {
                QFile f(six);
                check(f.open(QIODevice::WriteOnly), "recueil PGN de test créé");
                // mates in one and simple wins
                f.write("[Event \"mat1\"]\n[SetUp \"1\"]\n[FEN \"6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1\"]\n\n1. Rd8# *\n\n"
                        "[Event \"mat2\"]\n[SetUp \"1\"]\n[FEN \"3r2k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1\"]\n\n1... Rd1# *\n\n"
                        "[Event \"dame\"]\n[SetUp \"1\"]\n[FEN \"4k3/8/8/3q4/8/8/8/3QK3 w - - 0 1\"]\n\n1. Qxd5 *\n");
            }
            dlg->setFile(six);
            dlg->setMovetime(0.3);
            const EngineConfig sf = AutoEvaluator::findStockfish();
            const bool started = sf.isValid() && dlg->test()->start(sf, solutiontest::load(six, &err), 300, &err);
            bool finishedAll = false;
            QObject::connect(dlg->test(), &SolutionTest::finished, dlg, [&](bool completed) { finishedAll = completed; });
            check(started && waitFor([&] { return finishedAll; }, 20000) && dlg->test()->solvedCount() == 3,
                  "test de solutions : le moteur résout les 3 positions");
            const auto& r = dlg->test()->results();
            check(r.size() == 3 && r[0].foundMs >= 0 && r[0].foundMs <= 300, "test de solutions : temps de solution noté");
            if (const int ms = qEnvironmentVariableIntValue("HYDRA64_TEST_MARATHON_MS"); ms > 0) {
                // Full Tactics Marathon (long): score displayed, missed positions listed
                finishedAll = false;
                check(dlg->test()->start(sf, suite, ms, &err) && waitFor([&] { return finishedAll; }, 400 * (ms + 500)),
                      "test de solutions : Tactics Marathon passé en entier");
                QStringList missed;
                for (int i = 0; i < r.size(); ++i)
                    if (!r[i].solved) missed << QStringLiteral("%1 (%2 ; joué %3)").arg(suite[i].id, suite[i].solution, r[i].move);
                std::printf("Tactics Marathon, %d ms : %d / %d\n  manquées : %s\n", ms, dlg->test()->solvedCount(), int(suite.size()),
                            qPrintable(missed.join(QStringLiteral(", "))));
            }
            dlg->close();
            QDir(dir).removeRecursively();
        }
        // --- Clock panel (View menu), Play page during a game with a clock
        {
            w4.setPage(MainWindow::PageBoard);
            PlaySettings ps;
            ps.engineId = shuffle.id;
            ps.baseMs = 180000;
            ps.incMs = 2000;
            w4.startEngineGame(ps);
            w4.showPanel(QStringLiteral("clock"));
            wait(200);
            auto* clockDock = w4.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("clock"));
            auto* panel = w4.findChild<ClockPanel*>();
            check(clockDock && !clockDock->isClosed() && panel->isVisible(), "panneau Pendule ouvert depuis le menu Affichage");
            check(clockDock->windowTitle() == QStringLiteral("PENDULE : BLITZ 3+2"), "titre du panneau : la cadence (Pendule : Blitz 3+2)");
            QStringList texts;
            for (QLabel* l : panel->findChildren<QLabel*>())
                if (l->objectName() == QLatin1String("BigClock")) texts << l->text();
            const QRegularExpression clockText(QStringLiteral("^0:0[23]:[0-5][0-9]$"));
            check(texts.size() == 2 && clockText.match(texts[0]).hasMatch() && texts[1] == QStringLiteral("0:03:00"),
                  "les deux pendules affichées (0:03:00 au départ)");
            w4.findChild<EngineGame*>()->stop();
            clockDock->closeDockWidget();
        }
        // --- Tablebases panel: local Syzygy tables (3 pieces of the tests), online service as an option
        {
            TablebasePanel* tbp = w4.tablebasePanel();
            const bool opened = tbp->setTablesPath(QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/syzygy"));
            chess::Position kq;
            kq.setFen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
            tbp->setPosition(kq);
            const auto& sh = tbp->shown();
            check(opened && sh.source == QStringLiteral("local") && sh.wdl == 2 && !sh.moves.isEmpty() && sh.moves.front().wdl == 2,
                  "panneau Finales : roi et dame contre roi, gain (tables locales), coups classés");
            if (qEnvironmentVariableIsSet("HYDRA64_TEST_ONLINE")) {
                QSettings().setValue("tablebase/online", true);
                chess::Position krk;
                krk.setFen("4k3/8/8/8/8/8/3P4/R3K3 w - - 0 1"); // 4 pieces: outside the local tables
                tbp->setPosition(krk);
                check(waitFor([&] { return tbp->shown().source == QStringLiteral("en ligne"); }, 15000) && tbp->shown().wdl == 2,
                      "panneau Finales : service en ligne de Lichess (4 pièces)");
                QSettings().setValue("tablebase/online", false);
            }
            tbp->setTablesPath(QString());

            // Table sources: a folder counts for its subfolders; summary; Preferences page
            {
                const QString data = QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data");
                const tablebases::Summary viaParent = tablebases::scan({data});
                const tablebases::Summary direct = tablebases::scan({data + QStringLiteral("/syzygy"), data + QStringLiteral("/syzygy")});
                TablebasePage page;
                page.addSource(data);
                page.addSource(data); // twice: kept once
                check(viaParent.files == 6 && viaParent.wdl[3] == 3 && viaParent.dtz[3] == 3 && viaParent.largest() == 3 && viaParent.folders.size() == 1 && direct.files == 6
                          && page.shownSources().size() == 1 && page.summaryText().contains(QStringLiteral("3 pièces : 3 finales sur 5")),
                      qPrintable(QStringLiteral("tables de finales : sources lues (« %1 »)").arg(page.summaryText())));
                page.commit();
                const bool applied = tablebases::effectivePath().endsWith(QStringLiteral("/tests/data/syzygy")) && chess::tb::largest() == 3;
                // Engines: the folder is given according to the role and the settings
                auto sent = [&](UciEngine::TablebaseRole role, bool allowed) {
                    UciEngine e;
                    e.setTablebaseRole(role, allowed);
                    bool started = false;
                    QObject::connect(&e, &UciEngine::started, [&] { started = true; });
                    e.start(QApplication::applicationDirPath() + QStringLiteral("/stockfish"));
                    waitFor([&] { return started; }, 5000);
                    const QString path = e.tablebasePathSent();
                    e.quit();
                    return path;
                };
                const QString analysis = sent(UciEngine::TablebaseRole::Analysis, true);
                const QString weakOpponent = sent(UciEngine::TablebaseRole::Play, false);
                tablebases::setEngineUse(UciEngine::TablebaseRole::Tournament, false);
                tablebases::apply();
                const QString tournamentOff = sent(UciEngine::TablebaseRole::Tournament, true);
                tablebases::setEngineUse(UciEngine::TablebaseRole::Tournament, true);
                check(applied && analysis == tablebases::effectivePath() && weakOpponent.isEmpty() && tournamentOff.isEmpty(),
                      "tables de finales : dossier transmis à Stockfish en analyse, pas à un adversaire limité ni à un rôle décoché");
                tablebases::setSources({});
                tablebases::apply();
            }
            // A win read from the tables by the engine: "+Tables", not "+200.00"; probes counted
            {
                tablebases::setSources({QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/syzygy")});
                tablebases::apply();
                UciEngine e;
                EngineLine last;
                qint64 hits = 0;
                bool best = false;
                QObject::connect(&e, &UciEngine::infoReceived, [&](const EngineLine& l) { last = l, hits = std::max(hits, l.tbHits); });
                QObject::connect(&e, &UciEngine::bestMoveReceived, [&] { best = true; });
                e.start(QApplication::applicationDirPath() + QStringLiteral("/stockfish"));
                e.search(QStringLiteral("8/8/8/4k3/8/8/8/3QK3 w - - 0 1"), QStringLiteral("depth 12")); // queen against lone king
                waitFor([&] { return best; }, 15000);
                e.quit();
                const QString shownWin = EvalBar::format({true, false, last.score, last.depth}), shownLoss = EvalBar::format({true, false, -19990, 20});
                check(best && hits > 0 && (last.mate || last.score >= TablebaseScore) && (last.mate || shownWin == QStringLiteral("+Tables")) && shownLoss == QStringLiteral("−Tables")
                          && EvalBar::format({true, false, 250, 20}) == QStringLiteral("+2.50"),
                      qPrintable(QStringLiteral("moteur et tables : %1 consultations, score affiché « %2 »").arg(hits).arg(shownWin)));
                tablebases::setSources({});
                tablebases::apply();
            }
            // "In analysis" checkbox unticked then ticked again: an engine already started follows, without being restarted
            {
                tablebases::setSources({QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/syzygy")});
                tablebases::apply();
                UciEngine e;
                qint64 hits = 0;
                bool best = false;
                QObject::connect(&e, &UciEngine::infoReceived, [&](const EngineLine& l) { hits = std::max(hits, l.tbHits); });
                QObject::connect(&e, &UciEngine::bestMoveReceived, [&] { best = true; });
                e.start(QApplication::applicationDirPath() + QStringLiteral("/stockfish"));
                auto run = [&] {
                    hits = 0, best = false;
                    e.search(QStringLiteral("8/8/8/4k3/8/8/8/3QK3 w - - 0 1"), QStringLiteral("depth 12"));
                    waitFor([&] { return best; }, 15000);
                    return hits;
                };
                const qint64 with = run();
                tablebases::setEngineUse(UciEngine::TablebaseRole::Analysis, false);
                tablebases::apply();
                const qint64 without = run();
                const bool cleared = e.tablebasePathSent().isEmpty();
                tablebases::setEngineUse(UciEngine::TablebaseRole::Analysis, true);
                tablebases::apply();
                const qint64 again = run();
                const bool restored = e.tablebasePathSent() == tablebases::effectivePath();
                // A SyzygyPath set by hand is not replaced by that of the Preferences
                e.setOption(QStringLiteral("SyzygyPath"), QStringLiteral("/nulle/part"));
                tablebases::setEngineUse(UciEngine::TablebaseRole::Analysis, false);
                tablebases::apply();
                const qint64 manual = run();
                const bool kept = e.tablebasePathSent() == tablebases::effectivePath();
                e.quit();
                tablebases::setEngineUse(UciEngine::TablebaseRole::Analysis, true);
                tablebases::apply();
                // Per-engine setting: "<empty>" in its SyzygyPath, it never receives the tables
                {
                    UciEngine none;
                    qint64 noneHits = 0;
                    bool noneBest = false;
                    QObject::connect(&none, &UciEngine::infoReceived, [&](const EngineLine& l) { noneHits = std::max(noneHits, l.tbHits); });
                    QObject::connect(&none, &UciEngine::bestMoveReceived, [&] { noneBest = true; });
                    none.setOption(QStringLiteral("SyzygyPath"), QStringLiteral("<empty>"));
                    none.start(QApplication::applicationDirPath() + QStringLiteral("/stockfish"));
                    none.search(QStringLiteral("8/8/8/4k3/8/8/8/3QK3 w - - 0 1"), QStringLiteral("depth 12"));
                    const bool done = waitFor([&] { return noneBest; }, 15000);
                    none.quit();
                    check(done && noneHits == 0, qPrintable(QStringLiteral("tables par moteur : SyzygyPath « <empty> », aucune consultation (%1)").arg(noneHits)));
                }
                check(with > 0 && without == 0 && cleared && again > 0 && restored && manual == 0 && kept,
                      qPrintable(QStringLiteral("tables et moteur déjà lancé : %1 consultations, %2 case décochée, %3 recochée").arg(with).arg(without).arg(again)));
                tablebases::setSources({});
                tablebases::apply();
            }
            // Download: files copied, checksum checked, partial file resumed, wrong checksum refused
            {
                const QString from = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/tables-source");
                const QString to = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/tables-recues");
                QDir(from).removeRecursively();
                QDir(to).removeRecursively();
                QDir().mkpath(from);
                QDir().mkpath(to);
                QList<TablebaseDownloader::File> files;
                QByteArray big;
                for (int i = 0; i < 3; ++i) {
                    QByteArray content;
                    for (int k = 0; k < 40000 * (i + 1); ++k) content += char('a' + (k * 7 + i) % 26);
                    if (i == 2) big = content;
                    const QString name = QStringLiteral("K%1vK.rtbw").arg(QStringLiteral("QRP").at(i));
                    QFile f(from + QLatin1Char('/') + name);
                    (void)f.open(QIODevice::WriteOnly);
                    f.write(content);
                    files.append({name, QUrl::fromLocalFile(f.fileName()), content.size(), QCryptographicHash::hash(content, QCryptographicHash::Blake2b_512).toHex()});
                }
                { // a start of file already received
                    QFile part(to + QStringLiteral("/KPvK.rtbw.part"));
                    (void)part.open(QIODevice::WriteOnly);
                    part.write(big.left(50000));
                }
                TablebaseDownloader dl;
                bool done = false, ok = false;
                QObject::connect(&dl, &TablebaseDownloader::finished, [&](bool good, const QString&) { done = true, ok = good; });
                dl.startFiles(files, to);
                waitFor([&] { return done; }, 10000);
                QFile got(to + QStringLiteral("/KPvK.rtbw"));
                (void)got.open(QIODevice::ReadOnly);
                const bool received = ok && dl.filesDone() == 3 && got.readAll() == big && !QFileInfo::exists(to + QStringLiteral("/KPvK.rtbw.part")) && dl.doneBytes() == dl.totalBytes();
                // second pass: everything is already there; then a wrong checksum
                done = false;
                dl.startFiles(files, to);
                waitFor([&] { return done; }, 10000);
                const bool nothingToDo = ok && dl.filesDone() == 3;
                QFile::remove(to + QStringLiteral("/KQvK.rtbw"));
                files[0].blake2b = QByteArray(128, '0');
                done = false;
                dl.startFiles(files, to);
                waitFor([&] { return done; }, 10000);
                check(received && nothingToDo && !ok && !QFileInfo::exists(to + QStringLiteral("/KQvK.rtbw")),
                      "téléchargement des tables : fichiers reçus et contrôlés, fichier partiel repris, empreinte fausse refusée");
            }
            // Real mirror (HYDRA64_TEST_ONLINE): index read, first tables received and checked, then interruption
            if (qEnvironmentVariableIsSet("HYDRA64_TEST_ONLINE")) {
                const QString to = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/tables-miroir");
                QDir(to).removeRecursively();
                TablebaseDownloader dl;
                bool done = false;
                QObject::connect(&dl, &TablebaseDownloader::finished, [&](bool, const QString&) { done = true; });
                dl.start(tablebases::sets().first().directories, to);
                waitFor([&] { return done || (dl.filesDone() >= 12 && dl.bytesPerSecond() > 0); }, 60000);
                const qint64 rate = dl.bytesPerSecond(), left = dl.secondsLeft();
                check(rate > 0 && left >= 0 && left < 24 * 3600, qPrintable(QStringLiteral("miroir des tables : débit mesuré (%1 ko/s), reste estimé à %2").arg(rate / 1000).arg(TablebaseDownloader::durationText(left))));
                const int total = dl.filesTotal(), got = dl.filesDone();
                const qint64 bytes = dl.totalBytes();
                dl.cancel();
                const tablebases::Summary sum = tablebases::scan({to});
                check(total == 290 && got >= 12 && bytes > 900'000'000LL && bytes < 1'100'000'000LL && sum.files >= 12 && chess::tb::init(QFile::encodeName(to).toStdString()) >= 3,
                      qPrintable(QStringLiteral("miroir des tables : %1 fichiers annoncés (%2 Mo), %3 reçus et contrôlés, lisibles").arg(total).arg(bytes / 1000000).arg(got)));
                chess::tb::init(std::string());
                QDir(to).removeRecursively();
            }
            // Estimate of what is left: the tables already in the folder are not counted again
            {
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/tables-estimation");
                QDir(dir).removeRecursively();
                QDir().mkpath(dir);
                const auto sets = tablebases::sets();
                const tablebases::Remaining empty345 = tablebases::remaining(sets[0], dir), emptyAll = tablebases::remaining(sets[2], dir);
                auto put = [&](const QString& name, qint64 size) {
                    QFile f(dir + QLatin1Char('/') + name);
                    (void)f.open(QIODevice::WriteOnly);
                    f.resize(size);
                };
                // the complete 3-4-5 set (290 dummy files), and one 6-piece table
                int made = 0;
                const QString letters = QStringLiteral("QRBNP");
                for (int a = 0; a < 5 && made < 145; ++a)
                    for (int b = 0; b < 5 && made < 145; ++b)
                        for (int c = 0; c < 6 && made < 145; ++c, ++made) {
                            // dummy but well-formed names, all different: 4 or 5 pieces
                            const QString name = QStringLiteral("K%1%2vK%3").arg(letters[a]).arg(letters[b]).arg(c < 5 ? QString(letters[c]) : QString());
                            put(name + QStringLiteral(".rtbw"), 2'700'000);
                            put(name + QStringLiteral(".rtbz"), 4'000'000);
                        }
                put(QStringLiteral("KRPvKRP.rtbw"), 500'000'000); // a 6-piece table already there
                const tablebases::Remaining full345 = tablebases::remaining(sets[0], dir), then6 = tablebases::remaining(sets[2], dir);
                TablebasePage page;
                page.setDownloadChoice(0, dir);
                const QString complete = page.spaceText();
                page.setDownloadChoice(2, dir);
                const QString partial = page.spaceText();
                check(made == 145 && empty345.files == 290 && empty345.present == 0 && emptyAll.bytes > 160'000'000'000LL && full345.complete() && then6.files == 729 && then6.present == 291
                          && then6.bytes == 72'830'000'000LL - 500'000'000LL + 87'360'000'000LL && complete.contains(QStringLiteral("déjà complet")) && partial.contains(QStringLiteral("291 fichiers")) && partial.contains(QStringLiteral("729 fichiers")),
                      qPrintable(QStringLiteral("estimation du téléchargement : « %1 »").arg(partial)));
                QDir(dir).removeRecursively();
            }
            check(TablebaseDownloader::durationText(7500) == QStringLiteral("2 h 05 min") && TablebaseDownloader::durationText(200) == QStringLiteral("3 min 20 s")
                      && TablebaseDownloader::durationText(12) == QStringLiteral("12 s") && TablebaseDownloader::durationText(-1).isEmpty(),
                  "téléchargement des tables : durées lisibles (2 h 05 min, 3 min 20 s, 12 s)");
            // Disc images (HYDRA64_TEST_ISO: folder of .iso images): read as they are
            if (const QString isoDir = qEnvironmentVariable("HYDRA64_TEST_ISO"); !isoDir.isEmpty() && tablebases::imagesSupported()) {
                const tablebases::Summary sum = tablebases::scan({isoDir});
                tablebases::setSources({isoDir});
                const QString path = tablebases::apply();
                chess::Position six;
                six.setFen("8/8/4k3/3r1p2/8/3R1P2/4K3/8 w - - 0 1"); // rook and pawn against rook and pawn
                const chess::tb::Probe probe = chess::tb::probe(six);
                UciEngine e;
                bool started = false;
                QObject::connect(&e, &UciEngine::started, [&] { started = true; });
                e.start(QApplication::applicationDirPath() + QStringLiteral("/stockfish"));
                waitFor([&] { return started; }, 5000);
                const bool engineHasIt = e.tablebasePathSent() == path;
                e.quit();
                const QString mount = path.section(QLatin1Char(':'), -1);
                const int mounted = int(QDir(mount).entryList({QStringLiteral("*.rtb?")}, QDir::Files).size());
                tablebases::setSources({});
                tablebases::apply();
                check(sum.images.size() >= 1 && sum.wdl[5] == 110 && mounted == sum.files && chess::tb::largest() == 0 && probe.ok && engineHasIt && !QFileInfo::exists(mount),
                      qPrintable(QStringLiteral("images ISO de tables : %1 image(s), %2 — présentées (%3 fichiers), sondées (6 pièces), puis retirées").arg(sum.images.size()).arg(sum.text()).arg(mounted)));
            }
        }
        // --- User piece sets and boards: SVG folders of the user's themes folder, read without recompiling
        {
            const QString pieces = PieceRenderer::userDirectory(true), boards = boardtheme::userDirectory(true);
            const QStringList names = {"wK", "wQ", "wR", "wB", "wN", "wP", "bK", "bQ", "bR", "bB", "bN", "bP"};
            auto svg = [](const QString& file, const QString& fill, int inset = 5) {
                QFile f(file);
                if (f.open(QIODevice::WriteOnly))
                    f.write(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 45 45'><rect x='%2' y='%2' width='%3' height='%3' fill='%1'/></svg>").arg(fill).arg(inset).arg(45 - 2 * inset).toUtf8());
            };
            QDir().mkpath(pieces + "/Essai");
            QDir().mkpath(pieces + "/Incomplet");
            for (const QString& n : names) svg(pieces + "/Essai/" + n + ".svg", n.startsWith('w') ? "#ff0000" : "#0000ff");
            svg(pieces + "/Incomplet/wK.svg", "#00ff00");
            QDir().mkpath(boards + "/Uni");
            QDir().mkpath(boards + "/Deux cases");
            QDir().mkpath(boards + "/Vide");
            QDir().mkpath(boards + "/Image");
            svg(boards + "/Uni/echiquier.svg", "#0ac81e", 0); // a single SVG: the whole board
            svg(boards + "/Deux cases/light.svg", "#ffffff");
            svg(boards + "/Deux cases/dark.svg", "#000000");
            {
                QImage wood(64, 64, QImage::Format_RGB32); // a board as an image
                wood.fill(QColor(120, 80, 40));
                wood.save(boards + "/Image/bois.png");
            }

            QStringList setIds, setNames, boardIds;
            for (const PieceRenderer::PieceSet& s : PieceRenderer::availableSets()) setIds << s.id, setNames << s.name;
            for (const boardtheme::Theme& t : boardtheme::available()) boardIds << t.id;
            PieceRenderer r;
            r.setFixedSet("user:Essai");
            const QColor red = r.pixmap(chess::makePiece(chess::White, chess::King), 90, 1.0).toImage().pixelColor(45, 45);
            // A folder of PNGs is a set too: images scaled to the square, proportions kept and centred
            // (a 40 × 80 green image leaves the sides of the square transparent). A piece in both
            // formats is drawn from its SVG.
            QDir().mkpath(pieces + "/Images");
            for (const QString& n : names) {
                QImage tall(40, 80, QImage::Format_ARGB32);
                tall.fill(QColor(0, 200, 0));
                tall.save(pieces + "/Images/" + n + ".png");
            }
            svg(pieces + "/Images/bQ.svg", "#ff00ff", 0);
            bool pngListed = false;
            for (const PieceRenderer::PieceSet& s : PieceRenderer::availableSets()) pngListed = pngListed || s.id == QLatin1String("user:Images");
            PieceRenderer images;
            images.setFixedSet("user:Images");
            const QImage pngKing = images.pixmap(chess::makePiece(chess::White, chess::King), 90, 1.0).toImage();
            const bool pngRead = pngListed && pngKing.pixelColor(45, 45) == QColor(0, 200, 0) && pngKing.pixelColor(45, 3) == QColor(0, 200, 0)
                                 && pngKing.pixelColor(5, 45).alpha() == 0 && pngKing.pixelColor(84, 45).alpha() == 0
                                 && images.pixmap(chess::makePiece(chess::Black, chess::Queen), 90, 1.0).toImage().pixelColor(45, 45) == QColor(255, 0, 255);
            const QColor uni = boardtheme::lightColor("user:Uni");
            const QColor imageBoard = boardtheme::lightColor("user:Image");
            const QColor light = boardtheme::tile(0, 0, 40, 1.0, "user:Deux cases").toImage().pixelColor(20, 20);
            const QColor dark = boardtheme::tile(1, 0, 40, 1.0, "user:Deux cases").toImage().pixelColor(20, 20);
            // Chosen then removed from disk: back to the default set on reread
            PieceRenderer::setCurrentSet("user:Essai");
            boardtheme::setCurrent("user:Uni");
            const bool chosen = PieceRenderer::currentSet() == "user:Essai" && boardtheme::current() == "user:Uni";
            // The set appears in the Preferences without restarting; restore button hidden as long as nothing is missing
            bool listed = false, restoreHidden = false, restoreOk = false;
            {
                PreferencesDialog prefsDlg(&w4);
                for (QComboBox* c : prefsDlg.findChildren<QComboBox*>()) listed |= c->findData(QStringLiteral("user:Essai")) >= 0;
                restoreHidden = prefsDlg.findChild<QPushButton*>("RestoreDefaultSets")->isHidden();
            }
            // A deleted shipped set does not come back by itself; the button appears and puts it back, without touching the others
            if (pieces.contains(QStringLiteral("/ui_tests_home/"))) {
                QDir(pieces + "/Celtic").removeRecursively();
                QDir(boards + "/Wood").removeRecursively();
                defaultsets::installOnce();
                bool celticListed = false;
                for (const PieceRenderer::PieceSet& s : PieceRenderer::availableSets()) celticListed |= s.id == "user:Celtic";
                PreferencesDialog prefsDlg(&w4);
                auto* restore = prefsDlg.findChild<QPushButton*>("RestoreDefaultSets");
                const bool offered = !celticListed && defaultsets::missing() == 2 && !restore->isHidden();
                restore->click();
                bool back = false;
                for (QComboBox* c : prefsDlg.findChildren<QComboBox*>()) back |= c->findData(QStringLiteral("user:Celtic")) >= 0;
                restoreOk = offered && back && defaultsets::missing() == 0 && restore->isHidden() && QFileInfo(pieces + "/Celtic/wK.svg").isWritable()
                            && QFileInfo::exists(pieces + "/Essai/wK.svg");
            }
            // Only in the tests' isolated Documents folder, never in the user's
            if (pieces.contains(QStringLiteral("/ui_tests_home/")) && boards.contains(QStringLiteral("/ui_tests_home/"))) {
                QDir(pieces).removeRecursively();
                QDir(boards).removeRecursively();
            }
            PieceRenderer::setCurrentSet("user:Essai");
            PieceRenderer::availableSets();
            boardtheme::available();
            boardtheme::setCurrent("user:Uni");
            const bool gone = PieceRenderer::currentSet() == PieceRenderer::DefaultSet && boardtheme::current() == boardtheme::DefaultTheme;
            PieceRenderer::setCurrentSet(prefs::pieceSet());
            boardtheme::setCurrent(prefs::boardTheme());
            check(setIds.contains("user:Essai") && !setIds.contains("user:Incomplet") && setNames.contains("Essai") && pngRead
                      && boardIds.contains("user:Uni") && boardIds.contains("user:Deux cases") && !boardIds.contains("user:Vide") && boardIds.contains("user:Image") && imageBoard == QColor(120, 80, 40) && red == QColor(255, 0, 0)
                      && uni == QColor(10, 200, 30) && light == QColor(255, 255, 255) && dark == QColor(0, 0, 0) && chosen && listed && restoreHidden && restoreOk && gone,
                  qPrintable(QStringLiteral("jeux personnels : %1 jeu(x) de pièces et %2 échiquier(s) lus dans Documents/Hydra64/Themes (pièces SVG, échiquiers image ou SVG), incomplets ignorés, retirés → jeu par défaut")
                                 .arg(setIds.filter("user:").size()).arg(boardIds.filter("user:").size())));
        }
        // --- Game → Training: opening with the tree, best move, mate in 1
        {
            const QString tdir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/entrainement");
            QDir().mkpath(tdir);
            const QString tree = tdir + QStringLiteral("/arbre.hy64book");
            const chess::Position s0 = chess::Position::startPos();
            const chess::Position s1 = s0.after(s0.parseSan("e4"));
            const chess::treebook::MoveReader reader = [s0, s1](const chess::Position& p) {
                std::vector<chess::treebook::BookMove> out;
                chess::treebook::BookMove bm;
                bm.hasStats = false;
                bm.gameCount = 10;
                if (p.fen() == s0.fen()) bm.move = s0.parseSan("e4"), out.push_back(bm);
                else if (p.fen() == s1.fen()) bm.move = s1.parseSan("e5"), out.push_back(bm);
                return out;
            };
            chess::treebook::writeTree(reader, {s0}, tree.toStdString(), chess::treebook::TreeFormat::Hy64Book);
            auto* bp = w4.bookPanel();
            bp->openBook(tree);
            TrainingDialog* tr = w4.trainingDialog();
            tr->setSpacedRepetition(false); // file order for these tests (spaced repetition is tested further down)
            tr->startOpening(chess::White);
            tr->playMove(s0.parseSan("e4"));
            const bool followed = waitFor([&] { return !tr->isWaiting(); }, 3000) && tr->status().contains(QStringLiteral("Fin de l'arbre"));
            tr->startOpening(chess::White);
            tr->playMove(s0.parseSan("d4"));
            const bool left = tr->status().contains(QStringLiteral("sort de l'arbre")) && tr->status().contains(QStringLiteral("e4"));
            check(followed && left, "entraînement d'ouverture : l'arbre répond, la sortie de l'arbre est signalée");
            bp->closeBook();
            // Best move: a good move then a bad one
            const QString epd = tdir + QStringLiteral("/coups.epd");
            {
                QFile f(epd);
                (void)f.open(QIODevice::WriteOnly);
                f.write("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - bm Rd8#; id \"mat1\";\n"
                        "4k3/8/8/3q4/8/8/8/3QK3 w - - bm Qxd5; id \"dame\";\n");
            }
            QString err;
            const bool loaded = tr->startPositions(TrainingDialog::BestMove, epd, &err);
            tr->playMove(tr->position().parseSan("Rd8"));
            QApplication::processEvents();
            const bool good = tr->solved() == 1;
            // next position, bad move
            tr->startPositions(TrainingDialog::BestMove, epd, &err);
            tr->playMove(tr->position().parseSan("Rd7"));
            check(loaded && good && tr->solved() == 0 && tr->tried() == 1, "entraînement « meilleur coup » : bonne et mauvaise réponse comptées");
            // Mate in 1: Stockfish measures the distance, the player mates
            const bool mateLoaded = tr->startPositions(TrainingDialog::Mate, epd, &err);
            const bool measured = mateLoaded && waitFor([&] { return !tr->isWaiting(); }, 15000);
            tr->playMove(tr->position().parseSan("Rd8"));
            check(measured && tr->solved() == 1 && tr->status().contains(QStringLiteral("Mat")),
                  qPrintable(QStringLiteral("entraînement « mats en N » : mat en 1 réussi (%1)").arg(tr->status())));
            {
                // EPD file opened as a database, editable: each write rewrites it as EPD
                const QString editable = tdir + QStringLiteral("/modifiable.epd");
                QFile::remove(editable);
                QFile::copy(epd, editable);
                GameListModel model;
                QString why;
                const bool opened = model.loadFile(editable, &why);
                GameListModel* m = &model;
                chess::Game g;
                const bool readable = opened && m->rowCount() == 2 && !m->isReadOnly() && m->parseGame(0, g, nullptr) && !g.mainline().empty()
                                      && g.mainline()[0]->san == "Rd8#";
                // Addition of an exercise, deletion of the first position, change of the name
                exercise::Exercise ex;
                ex.pos.setFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
                ex.line = {ex.pos.parseSan("Ra8#")};
                ex.name = QStringLiteral("Mat du couloir");
                ex.mateIn = 1;
                ex.comment = QStringLiteral("Classique; à connaître");
                const bool appended = exercise::append(editable, ex, m, &why) && m->rowCount() == 3;
                const bool removed = m->removeGames({0}, &why) && m->rowCount() == 2;
                chess::Game first;
                m->parseGame(0, first, nullptr);
                first.setHeader("Event", "Dame en prise");
                const bool replaced = m->replaceGame(0, chess::pgn::writeGame(first), &why);
                QFile f(editable);
                (void)f.open(QIODevice::ReadOnly);
                const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                f.close();
                const bool epdText = lines.size() == 2 && !lines.join(QString()).contains(QLatin1Char('[')) && lines[0].contains(QLatin1String("id \"Dame en prise\""))
                                     && lines[0].contains(QLatin1String("bm Qxd5")) && lines[1].contains(QLatin1String("bm Ra8#"))
                                     && lines[1].contains(QLatin1String("dm 1")) && lines[1].contains(QStringLiteral("c0 \"Classique; à connaître\""));
                // Reread by Training: the same thing
                const QList<TestPosition> again = solutiontest::load(editable, &why);
                const bool trainable = again.size() == 2 && again[1].mateIn == 1 && again[1].id == QLatin1String("Mat du couloir");
                check(readable && appended && removed && replaced && epdText && trainable,
                      qPrintable(QStringLiteral("fichier EPD modifiable : ajout, suppression, modification, réécrit en EPD (%1 | %2)").arg(why, lines.join(QStringLiteral(" / ")))));
                // Add to collection: the three formats, reread by Training (dm, continuation)
                {
                    exercise::Exercise mate;
                    mate.pos.setFen("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4");
                    std::vector<chess::Move> following = {mate.pos.parseSan("Qxf7#")};
                    int formatsOk = 0;
                    QStringList made;
                    for (const char* ext : {"epd", "pgn", "hy64i"}) {
                        const QString path = exercise::collectionsDir() + QStringLiteral("/Essai test.%1").arg(QLatin1String(ext));
                        made << path;
                        AddExerciseDialog dlg(mate.pos, following, QStringLiteral("Coup du berger"));
                        dlg.setCollectionPath(path);
                        const exercise::Exercise e = dlg.exercise();
                        QString err;
                        const bool added = e.mateIn == 1 && e.line.size() == 1 && exercise::append(path, e, nullptr, &err) && exercise::append(path, e, nullptr, &err);
                        const QList<TestPosition> ps = solutiontest::load(path, &err);
                        if (added && ps.size() == 2 && ps[0].mateIn == 1 && ps[0].id == QLatin1String("Coup du berger") && solutiontest::isCorrect(ps[0], QStringLiteral("h5f7")))
                            ++formatsOk;
                        else
                            qWarning() << ext << err << ps.size();
                    }
                    int listed = 0;
                    for (const TrainingDialog::Collection& c : TrainingDialog::userCollections())
                        if (made.contains(c.path) && c.name.startsWith(QStringLiteral("Mes recueils"))) ++listed;
                    check(formatsOk == 3 && listed == 3, qPrintable(QStringLiteral("Ajouter au recueil : EPD, PGN et Hydra64 relus (%1/3), listés dans l'Entraînement (%2/3)").arg(formatsOk).arg(listed)));
                    // Dialog: mate detected in the rest of the notation (cut at the mate), continuation kept
                    const chess::Position p2 = chess::Position::startPos();
                    std::vector<chess::Move> seq;
                    chess::Position q = p2;
                    for (const char* san : {"e4", "e5"}) { // not a mate: ordinary continuation
                        seq.push_back(q.parseSan(san));
                        q.makeMove(seq.back());
                    }
                    AddExerciseDialog plain(p2, seq, QString());
                    plain.setKeepLine(true);
                    const bool plainOk = plain.exercise().mateIn == 0 && plain.exercise().line.size() == 2;
                    plain.setKeepLine(false);
                    const bool firstOnly = plain.exercise().line.size() == 1;
                    AddExerciseDialog none(p2, {}, QString());
                    check(plainOk && firstOnly && !none.hasSolution(), "Ajouter au recueil : solution = coup suivant, suite gardée ou non, sans coup suivant refusé");
                    // From the main window: displayed position and next move of the notation
                    auto* sess = w4.findChild<GameSession*>();
                    const MainWindow::Page pageBefore = w4.page();
                    w4.setPage(MainWindow::PageAnalysis);
                    const std::string analysisBefore = chess::pgn::writeGame(sess->game()); // put back afterwards (restart test)
                    const GameOrigin originBefore = sess->origin();
                    sess->newGame();
                    for (const char* san : {"e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6", "Qxf7#"}) sess->playMove(sess->current()->pos.parseSan(san));
                    sess->back();
                    auto dlg = w4.addExerciseDialog();
                    const exercise::Exercise fromBoard = dlg->exercise();
                    const QString target = exercise::collectionsDir() + QStringLiteral("/Essai test.epd");
                    const int before = solutiontest::load(target, nullptr).size();
                    const bool viaWindow = dlg->hasSolution() && fromBoard.mateIn == 1 && fromBoard.line.size() == 1 && w4.addExercise(target, fromBoard)
                                           && solutiontest::load(target, nullptr).size() == before + 1;
                    check(viaWindow, "Partie → Ajouter au recueil : position de l'échiquier, mat en 1 détecté, ajouté au recueil");
                    {
                        chess::Game restored;
                        chess::pgn::parseGame(analysisBefore, restored);
                        sess->loadGame(std::move(restored));
                        sess->setOrigin(originBefore);
                        w4.setPage(pageBefore);
                    }
                    for (const QString& path : made) {
                        if (path.endsWith(QLatin1String(".hy64i"))) {
                            const QString stem = path.left(path.size() - 5);
                            for (const char* e : {".hy64i", ".hy64n", ".hy64g", ".hy64p"}) QFile::remove(stem + QLatin1String(e));
                        } else {
                            QFile::remove(path);
                        }
                    }
                }
                // EPD → Hydra64 database conversion (editable)
                conversion::Request req;
                req.source = epd;
                req.destination = tdir + QStringLiteral("/coups.hy64i");
                conversion::Report rep;
                QString convErr;
                const bool converted = conversion::run(req, &rep, &convErr);
                GameListModel hy64;
                chess::Game g2;
                check(converted && rep.items == 2 && hy64.loadFile(req.destination, &convErr) && !hy64.isReadOnly() && hy64.rowCount() == 2
                          && hy64.parseGame(1, g2, nullptr) && g2.mainline().size() == 1 && g2.mainline()[0]->san == "Qxd5",
                      qPrintable(QStringLiteral("Fichier → Convertir : EPD → base Hydra64 modifiable (%1)").arg(convErr)));
            }
            // Built-in databases (databases/training, Hydra64 format): each category can be read, with
            // solution, rating and dm for the mates
            int builtIn = 0, builtInBad = 0;
            QStringList sizes;
            for (TrainingDialog::Mode m : {TrainingDialog::Mate, TrainingDialog::BestMove}) {
                int n = 0;
                for (const TrainingDialog::Collection& c : TrainingDialog::builtInCollections(m)) {
                    ++n;
                    QString e;
                    if (!c.path.endsWith(QLatin1String(".hy64")) || !QFileInfo::exists(c.path)) ++builtInBad;
                    int inCategory = 0;
                    for (const TestPosition& p : solutiontest::load(c.path, &e)) {
                        if (p.id != c.category) continue;
                        ++inCategory;
                        ++builtIn;
                        if (p.best.isEmpty() || p.line.isEmpty() || p.rating <= 0 || (m == TrainingDialog::Mate && p.mateIn != n)) ++builtInBad;
                    }
                    if (inCategory < 150) ++builtInBad;
                    sizes << QString::number(inCategory);
                }
            }
            // Built-in mate in 1: the distance comes from the database (no wait), the solution move mates
            const QString matesPath = TrainingDialog::builtInDir() + QStringLiteral("/mates/mate_in_1.hy64");
            const bool builtInMate = tr->startPositions(TrainingDialog::Mate, matesPath, &err, true, QStringLiteral("Mat en 1"));
            const bool noWait = !tr->isWaiting();
            const bool onlyCategory = tr->positionCount() == 300;
            {
                const QString fen = QString::fromStdString(tr->position().fen());
                for (const TestPosition& p : solutiontest::load(matesPath, &err))
                    if (p.fen == fen) {
                        tr->playMove(tr->position().parseUci(p.best.first().toStdString()));
                        break;
                    }
            }
            check(builtInBad == 0 && builtIn == 3565 && builtInMate && noWait && onlyCategory && tr->solved() == 1,
                  qPrintable(QStringLiteral("bases intégrées (Hydra64) : %1 positions (%2), %3 défaut(s), mat en 1 intégré réussi (%4)")
                                 .arg(builtIn).arg(sizes.join(QLatin1Char('/'))).arg(builtInBad).arg(tr->status())));
            {
                // Spaced repetition: boxes, working order, statistics, persistence
                const QString store = tdir + QStringLiteral("/progress.json");
                const QDate today(2026, 11, 16);
                TrainingProgress pr(store);
                const QString coll = tdir + QStringLiteral("/recueil.epd");
                pr.record(coll, QStringLiteral("a"), false, today);
                pr.record(coll, QStringLiteral("b"), true, today);
                const QStringList keys{QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a")};
                const QList<int> o1 = pr.order(coll, keys, today);           // a (missed), c (new), b (tomorrow)
                const QList<int> o2 = pr.order(coll, keys, today.addDays(1)); // a (most overdue), b, then c
                for (int i = 0; i < 6; ++i) pr.record(coll, QStringLiteral("b"), true, today);
                const TrainingProgress::Entry eb = pr.entry(coll, QStringLiteral("b"));
                const TrainingProgress::Stats st = pr.stats(coll, keys, today);
                const bool saved = pr.save();
                TrainingProgress reread(store);
                const bool persisted = reread.entry(coll, QStringLiteral("a")).tries == 1 && reread.entry(coll, QStringLiteral("b")).box == TrainingProgress::MaxBox;
                reread.reset(coll);
                check(o1 == QList<int>({2, 0, 1}) && o2 == QList<int>({2, 1, 0}) && eb.box == TrainingProgress::MaxBox && eb.due == today.addDays(30)
                          && st.due == 1 && st.fresh == 1 && st.learned == 1 && st.tries == 8 && st.successes == 7 && saved && persisted
                          && !reread.entry(coll, QStringLiteral("a")).seen(),
                      "révision espacée : boîtes 1/3/7/14/30 jours, ratées d'abord puis nouvelles, statistiques, enregistrée");
            }
            {
                // Training with spaced repetition: a missed position comes back further on, then first
                const QString path = tdir + QStringLiteral("/revision.epd");
                {
                    QFile f(path);
                    (void)f.open(QIODevice::WriteOnly);
                    f.write("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - bm Rd8#; id \"un\";\n"
                            "4k3/8/8/3q4/8/8/8/3QK3 w - - bm Qxd5; id \"deux\";\n");
                }
                tr->setSpacedRepetition(true);
                QString err;
                const bool started = tr->startPositions(TrainingDialog::BestMove, path, &err);
                const QString firstFen = tr->positionAt(0).fen;
                tr->playMove(tr->position().parseSan("Rd7")); // missed
                const bool requeued = tr->positionCount() == 3 && tr->positionAt(2).fen == firstFen;
                tr->nextPosition();
                tr->playMove(tr->position().parseSan("Qxd5")); // solved
                const TrainingProgress::Entry e1 = tr->progress().entry(path, firstFen);
                // Next session: the missed one (to review today) comes before the solved one (tomorrow)
                tr->startPositions(TrainingDialog::BestMove, path, &err);
                const bool failedFirst = tr->positionAt(0).fen == firstFen && tr->positionAt(1).id == QLatin1String("deux");
                check(started && requeued && e1.tries == 1 && e1.box == 0 && failedFirst,
                      qPrintable(QStringLiteral("entraînement en révision espacée : ratée remise plus loin, puis en tête à la séance suivante (%1)").arg(tr->status())));
                tr->progress().reset(path);
                tr->progress().save();
            }
            {
                // Check of a collection: judging rules
                using namespace collectioncheck;
                TestPosition mate;
                mate.mateIn = 2;
                mate.best = {QStringLiteral("d1d8")};
                auto mateLine = [](int n) { return Line{QStringLiteral("d1d8"), n, true}; };
                const bool mates = judge(mate, {mateLine(2)}, std::nullopt).verdict == Verdict::Ok
                                   && judge(mate, {mateLine(3)}, std::nullopt).verdict == Verdict::MateNotForced
                                   && judge(mate, {mateLine(1)}, std::nullopt).verdict == Verdict::MateShorter
                                   && judge(mate, {Line{QStringLiteral("d1d8"), 250, false}}, std::nullopt).verdict == Verdict::MateNotForced;
                TestPosition best;
                best.best = {QStringLiteral("e2e4")};
                best.avoid = {QStringLiteral("f2f3")};
                const Line sol{QStringLiteral("e2e4"), 150, false};
                const bool bests = judge(best, {sol, Line{QStringLiteral("d2d4"), 20, false}}, std::nullopt).verdict == Verdict::Ok
                                   && judge(best, {sol, Line{QStringLiteral("d2d4"), 120, false}}, std::nullopt).verdict == Verdict::Ambiguous
                                   && judge(best, {Line{QStringLiteral("e2e4"), 900, false}, Line{QStringLiteral("d2d4"), 500, false}}, std::nullopt).verdict == Verdict::Ambiguous
                                   && judge(best, {Line{QStringLiteral("d2d4"), 160, false}}, Line{QStringLiteral("e2e4"), 140, false}).verdict == Verdict::Ambiguous
                                   && judge(best, {Line{QStringLiteral("d2d4"), 300, false}}, Line{QStringLiteral("e2e4"), 0, false}).verdict == Verdict::Doubtful
                                   && judge(best, {Line{QStringLiteral("f2f3"), 50, false}}, std::nullopt).verdict == Verdict::Avoided
                                   && needsSolutionSearch(best, {Line{QStringLiteral("d2d4"), 300, false}}) && !needsSolutionSearch(best, {sol});
                check(mates && bests, "vérification d'un recueil : mats (forcé, plus long, plus court), solution confirmée, ambiguë, douteuse, coup à éviter");

                // With Stockfish: a good exercise, a wrong solution, an announced mate that is too long
                const QString path = tdir + QStringLiteral("/a-verifier.epd");
                {
                    QFile f(path);
                    (void)f.open(QIODevice::WriteOnly);
                    f.write("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - bm Rd8#; dm 1; id \"bon\";\n"
                            "4k3/8/8/3q4/8/8/8/3QK3 w - - bm Kf2; id \"faux\";\n"
                            "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - bm Rd8#; dm 3; id \"trop long\";\n");
                }
                auto* checker = w4.collectionCheckDialog();
                QString cerr;
                const bool running = checker->start(path, 300, &cerr);
                const bool done = running && waitFor([&] { return !checker->isRunning(); }, 30000);
                const auto& res = checker->results();
                const bool verdicts = res.size() == 3 && res[0].verdict == Verdict::Ok && res[1].verdict == Verdict::Doubtful && res[2].verdict == Verdict::MateShorter;
                QStringList seen;
                for (const auto& r : res) seen << verdictText(r.verdict) + QStringLiteral(" : ") + r.detail;
                check(done && verdicts, qPrintable(QStringLiteral("vérification par Stockfish : %1 (%2)").arg(seen.join(QStringLiteral(" | ")), cerr)));
                checker->close();
            }
            tr->close();
            QDir(tdir).removeRecursively();
        }
        // --- Under the board: name of the opening (the deepest recognised), otherwise the last move
        {
            auto play = [](chess::Game& g, std::initializer_list<const char*> sans) {
                chess::Node* n = g.root();
                for (const char* san : sans) n = g.addMove(n, n->pos.parseSan(san));
                return n;
            };
            chess::Game najdorf;
            chess::Node* end = play(najdorf, {"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6", "Be3", "e5"});
            const QString deep = BoardViewer::openingAt(end);       // outside the known lines: the last one recognised
            const QString taken = BoardViewer::openingAt(end->parent->parent); // moves taken back: 5…a6 (Najdorf)
            const QString early = BoardViewer::openingAt(najdorf.mainline()[1]);       // 1…c5: Sicilian
            chess::Game transposed; // 1.Nf3 c5 2.e4: Sicilian too, by transposition
            const QString viaTransposition = BoardViewer::openingAt(play(transposed, {"Nf3", "c5", "e4"}));
            chess::Position odd;
            odd.setFen("8/8/4k3/8/8/4K3/4P3/8 w - - 0 1");
            chess::Game endgame(odd);
            const QString none = BoardViewer::openingAt(play(endgame, {"Kd3"}));
            const QString sicilian = openingName("Sicilian Defense"); // in the interface language
            check(taken.startsWith(QLatin1String("B90")) && deep.startsWith(QLatin1String("B9")) && early.startsWith(QLatin1String("B2")) && early.contains(sicilian)
                      && viaTransposition.contains(sicilian) && none.isEmpty() && BoardViewer::openingAt(najdorf.root()).isEmpty(),
                  qPrintable(QStringLiteral("ouverture sous l'échiquier : %1 | %2 | %3 | %4").arg(early, taken, deep, viaTransposition)));
            // Names in French (French interface): established families, main word first, agreements
            const struct {
                const char* en;
                const char* fr;
            } names[] = {
                {"Sicilian Defense: Najdorf Variation, English Attack", "Défense sicilienne : variante Najdorf, attaque anglaise"},
                {"Ruy Lopez: Open, Classical Defense, Main Line", "Partie espagnole : ouverte, défense classique, ligne principale"},
                {"Queen's Gambit Declined: Tartakower Defense, Exchange Variation", "Gambit dame refusé : défense Tartakower, variante d'échange"},
                {"King's Indian Defense: Fianchetto Variation, Classical Variation", "Défense est-indienne : variante du fianchetto, variante classique"},
                {"Italian Game: Two Knights Defense, Steinitz Variation", "Partie italienne : défense des deux cavaliers, variante Steinitz"},
                {"Petrov's Defense: Modern Attack, Tal Gambit", "Défense Petrov : attaque moderne, gambit Tal"},
                {"Queen's Indian Defense, with e3", "Défense ouest-indienne, avec e3"},
                {"Caro-Kann Defense: Gurgenidze Counterattack", "Défense Caro-Kann : contre-attaque Gurgenidze"},
            };
            QStringList wrong;
            for (const auto& n : names)
                if (frenchOpeningName(QString::fromUtf8(n.en)) != QString::fromUtf8(n.fr)) wrong << frenchOpeningName(QString::fromUtf8(n.en));
            check(wrong.isEmpty(), qPrintable(QStringLiteral("noms d'ouvertures en français %1").arg(wrong.join(QStringLiteral(" | ")))));
            // In the window: the text follows the displayed position (opening, otherwise the last move)
            auto* viewer = w4.findChild<BoardViewer*>();
            auto* sess = w4.findChild<GameSession*>();
            const QString expectedOpening = BoardViewer::openingAt(sess->current());
            const QString shown = viewer->moveInfoText();
            check(expectedOpening.isEmpty() ? (shown.startsWith(QStringLiteral("Coup")) || shown.startsWith(QStringLiteral("Position"))) : shown.startsWith(expectedOpening),
                  qPrintable(QStringLiteral("sous l'échiquier : « %1 »").arg(shown)));
        }
        // --- Panel detached then page changes: no native child window (ADS
        //     fix, cmake/patch-qtads.cmake; otherwise "must be a top level window" and frozen page)
        {
            auto nativeChildren = [] {
                int n = 0;
                for (QWindow* win : QGuiApplication::allWindows())
                    if (win->parent() && QString::fromLatin1(win->metaObject()->className()) == QLatin1String("QWidgetWindow")
                        && (win->objectName().startsWith(QLatin1String("ads::")) || win->objectName().startsWith(QLatin1String("QWidget"))
                            || win->objectName().startsWith(QLatin1String("qt_splithandle"))))
                        ++n;
                return n;
            };
            auto settle = [] {
                for (int i = 0; i < 10; ++i) {
                    QApplication::processEvents();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                }
            };
            w4.setPage(MainWindow::PageBoard);
            settle();
            ads::CDockWidget* bookDock = w4.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("book"));
            bookDock->toggleView(true);
            bookDock->setFloating();
            settle();
            int worst = nativeChildren();
            for (MainWindow::Page p : {MainWindow::PageDatabase, MainWindow::PageBoard, MainWindow::PageAnalysis, MainWindow::PageBoard,
                                       MainWindow::PageTournament, MainWindow::PageBoard}) {
                w4.setPage(p);
                settle();
                worst = std::max(worst, nativeChildren());
            }
            const bool floating = bookDock->isFloating();
            w4.setPage(MainWindow::PageBoard);
            settle();
            check(worst == 0 && floating, qPrintable(QStringLiteral("panneau détaché et changements de page : %1 fenêtre(s) native(s) enfant(s), arbre toujours flottant sur Jouer : %2")
                                                         .arg(worst).arg(floating)));
            // The tree goes back to the right column for the rest of the tests
            auto* dm = w4.findChild<ads::CDockManager*>();
            dm->addDockWidgetTabToArea(bookDock, dm->findDockWidget(QStringLiteral("notation"))->dockAreaWidget());
            settle();
        }
        // --- Play page: coach (weak move reported, taken back) and hint
        {
            w4.setPage(MainWindow::PageBoard);
            auto* sj = w4.findChild<GameSession*>();
            Coach* coach = w4.coach();
            chess::Position before = chess::Position::startPos();
            for (const char* san : {"e4", "e5", "Qh5", "Nc6", "Bc4"}) before.makeMove(before.parseSan(san));
            int answers = 0;
            bool blunderWeak = false, quietWeak = true;
            coach->check(before, before.parseSan("Nf6"), 0.2, [&](const Coach::Verdict& v) {
                blunderWeak = v.weak;
                ++answers;
                coach->check(before, before.parseSan("g6"), 0.2, [&](const Coach::Verdict& w) {
                    quietWeak = w.weak;
                    ++answers;
                });
            });
            check(waitFor([&] { return answers == 2; }, 15000) && blunderWeak && !quietWeak, "coach : 3…Nf6?? signalé, 3…g6 accepté");
            chess::Move hinted;
            bool gotHint = false;
            coach->hint(chess::Position::startPos(), [&](chess::Move m) {
                hinted = m;
                gotHint = true;
            });
            check(waitFor([&] { return gotHint; }, 10000) && chess::Position::startPos().isLegal(hinted), "indice : un coup légal proposé");

            {
                // Game with an imposed opening (B90): the moves are played, the ECO recorded, the engine replies
                PlaySettings po;
                po.engineId = shuffle.id;
                po.side = PlaySettings::PlayWhite; // after 5…a6, it is White's turn: the player
                po.moveTimeMs = 100;
                po.openingMoves = PlayDialog::openingLine(QStringLiteral("B90"));
                EngineGame* og = w4.engineGame();
                const bool started = w4.startEngineGame(po) && og->isActive();
                const auto line = sj->game().mainline();
                const bool played = line.size() == 10 && sj->game().header("ECO") == "B90";
                og->playHumanMove(sj->current()->pos.parseSan("Be3"));
                const bool answered = waitFor([&] { return sj->game().mainline().size() == 12; }, 10000);
                og->stop();
                check(started && played && answered,
                      qPrintable(QStringLiteral("partie avec ouverture imposée : 10 demi-coups de la B90, puis le moteur répond (%1)").arg(sj->game().mainline().size())));
            }
            {
                // Beginner level: weighted draw among the lines, tolerated loss depending on the level
                auto line = [](int mpv, int cp) {
                    EngineLine l;
                    l.multipv = mpv;
                    l.score = cp;
                    l.pv = {QStringLiteral("e2e4")};
                    return l;
                };
                const QList<EngineLine> ls = {line(1, 50), line(2, 20), line(3, -100), line(4, -500)};
                int picks1[4] = {}, picks3[4] = {};
                for (int k = 0; k < 400; ++k) {
                    ++picks1[EngineGame::pickBeginnerLine(ls, 1, k / 400.0)];
                    ++picks3[EngineGame::pickBeginnerLine(ls, 3, k / 400.0)];
                }
                check(picks1[0] > picks1[1] && picks1[2] > 0 && picks1[3] == 0 && picks3[2] == 0 && picks3[0] > 0 && picks3[1] > 0,
                      qPrintable(QStringLiteral("niveau débutant : coups tirés (niveau 1 : %1/%2/%3/%4, niveau 3 : %5/%6/%7/%8)")
                                     .arg(picks1[0]).arg(picks1[1]).arg(picks1[2]).arg(picks1[3]).arg(picks3[0]).arg(picks3[1]).arg(picks3[2]).arg(picks3[3])));
                // Real game at beginner level against Stockfish: the engine replies
                PlaySettings pb;
                pb.engineId = QString();
                pb.side = PlaySettings::PlayBlack;
                pb.moveTimeMs = 100;
                pb.beginnerLevel = 1;
                EngineGame* bg = w4.engineGame();
                const bool started = w4.startEngineGame(pb) && bg->isActive();
                const bool replied = started && waitFor([&] { return sj->game().mainline().size() == 1; }, 10000);
                const QString label = bg->engineLabel();
                bg->stop();
                check(replied && label.contains(QStringLiteral("débutant 1")), qPrintable(QStringLiteral("niveau débutant : partie lancée (%1)").arg(label)));
            }
            // Game with the coach: the blunder is taken back before the engine's reply
            chess::Game start(before);
            sj->loadGame(std::move(start));
            PlaySettings ps;
            ps.engineId = shuffle.id;
            ps.side = PlaySettings::PlayBlack;
            ps.fromCurrentPosition = true;
            ps.moveTimeMs = 100;
            ps.coach = true;
            ps.coachThreshold = 0.2;
            EngineGame* game = w4.engineGame();
            check(w4.startEngineGame(ps) && game->isActive() && game->settings().coach, "partie avec coach lancée (Noirs)");
            wait(300);
            bool asked = false;
            answerDialogWhenShown(QObject::tr("Reprendre"), &asked);
            game->playHumanMove(sj->current()->pos.parseSan("Nf6"));
            const std::string beforeFen = before.fen();
            check(waitFor([&] { return asked && !game->isCoachChecking() && sj->current()->pos.fen() == beforeFen && sj->current()->children.empty(); }, 15000),
                  "coach : gaffe signalée, coup repris avant la réponse du moteur");
            const size_t plies = sj->game().mainline().size();
            game->playHumanMove(sj->current()->pos.parseSan("g6"));
            check(waitFor([&] { return sj->game().mainline().size() == plies + 2; }, 15000), "coach : bon coup accepté, le moteur répond");
            game->stop();
            // Pondering: after its move, Stockfish thinks about the expected reply;
            // playing it makes that search go on (ponderhit)
            sj->newGame();
            PlaySettings pp;
            pp.engineId = EngineRegistry::instance().fallbackEngine().id;
            pp.side = PlaySettings::PlayBlack;
            pp.fullStrength = true;
            pp.moveTimeMs = 300;
            pp.ponder = true;
            check(w4.startEngineGame(pp), "partie avec réflexion permanente lancée (Stockfish a les Blancs)");
            const bool pondering = waitFor([&] { return game->isPondering(); }, 15000);
            chess::Move expected;
            for (const chess::Move& mv : sj->current()->pos.legalMoves())
                if (sj->current()->pos.after(mv).fen() == game->ponderFen()) expected = mv;
            const size_t pliesBefore = sj->game().mainline().size();
            if (!expected.isNull()) game->playHumanMove(expected);
            check(pondering && !expected.isNull() && waitFor([&] { return sj->game().mainline().size() == pliesBefore + 2; }, 15000) && game->ponderHits() == 1,
                  "réflexion permanente : le coup attendu est joué, le moteur enchaîne (ponderhit)");
            game->stop();
            // Endgame tablebases in a game: probed at full strength (and displayed), never at a limited level
            {
                tablebases::setSources({QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/syzygy")});
                tablebases::apply();
                qint64 hits = 0;
                const auto conn = QObject::connect(game, &EngineGame::engineInfo, [&](const EngineLine& l) { hits = std::max(hits, l.tbHits); });
                auto played = [&](bool full) {
                    chess::Position kqk;
                    kqk.setFen("8/8/8/4k3/8/8/8/3QK3 w - - 0 1");
                    sj->newGame(kqk);
                    hits = 0;
                    PlaySettings pt;
                    pt.engineId = EngineRegistry::instance().fallbackEngine().id;
                    pt.side = PlaySettings::PlayBlack;
                    pt.fullStrength = full;
                    pt.elo = 2000;
                    pt.moveTimeMs = 500;
                    pt.fromCurrentPosition = true;
                    const bool ok = w4.startEngineGame(pt) && waitFor([&] { return !sj->game().mainline().empty(); }, 15000);
                    const QString shown = w4.findChild<QLabel*>("PlayDepth")->text();
                    game->stop();
                    return std::tuple(ok, hits, shown);
                };
                const auto [okFull, hitsFull, shownFull] = played(true);
                const auto [okWeak, hitsWeak, shownWeak] = played(false);
                // Last move deleted while it is the engine's turn: it waits instead of replaying at once;
                // Play restarts it, and playing a move of its side swaps the sides (it replies with the other one)
                {
                    chess::Position kqk;
                    kqk.setFen("8/8/8/4k3/8/8/8/3QK3 w - - 0 1");
                    sj->newGame(kqk);
                    PlaySettings pt;
                    pt.engineId = EngineRegistry::instance().fallbackEngine().id;
                    pt.side = PlaySettings::PlayBlack;
                    pt.fullStrength = true;
                    pt.moveTimeMs = 300;
                    pt.fromCurrentPosition = true;
                    const bool ok = w4.startEngineGame(pt) && waitFor([&] { return sj->game().mainline().size() == 1; }, 15000);
                    sj->deleteNode(sj->game().mainline().front());
                    const bool replayed = waitFor([&] { return !sj->game().mainline().empty(); }, 1500); // must not happen
                    const bool waits = !replayed && game->isAwaiting() && !game->isThinking() && game->humanColor() == chess::Black
                                       && w4.findChild<BoardViewer*>()->board()->isMovable(chess::White) && !w4.findChild<BoardViewer*>()->board()->isMovable(chess::Black);
                    game->moveNow(); // Play: it plays again, still with White
                    const bool resumed = waitFor([&] { return sj->game().mainline().size() == 1; }, 15000) && !game->isAwaiting() && game->humanColor() == chess::Black;
                    sj->deleteNode(sj->game().mainline().front());
                    const bool waitsAgain = !waitFor([&] { return !sj->game().mainline().empty(); }, 1000) && game->isAwaiting();
                    game->playHumanMove(sj->current()->pos.parseSan("Qd4+")); // a move of its side: the sides are swapped
                    const bool swapped = game->humanColor() == chess::White && !game->isAwaiting() && waitFor([&] { return sj->game().mainline().size() == 2; }, 15000);
                    // Deleting your own last move (it is then your turn) does not put the engine on hold
                    sj->deleteNode(sj->game().mainline().back());
                    const bool mine = waitFor([&] { return sj->game().mainline().size() == 1; }, 3000) && !game->isAwaiting();
                    game->stop();
                    check(ok && waits && resumed && waitsAgain && swapped,
                          qPrintable(QStringLiteral("coup supprimé en partie : le moteur attend (%1), Play le relance (%2), jouer son camp échange les camps (%3)").arg(waits && waitsAgain).arg(resumed).arg(swapped)));
                    Q_UNUSED(mine);
                }
                // Strength changed during the game from the Opponent panel: nothing in the notation,
                // header and button up to date, tables probed from the next move
                {
                    chess::Position kqk;
                    kqk.setFen("8/8/8/4k3/8/8/8/3QK3 w - - 0 1");
                    sj->newGame(kqk);
                    PlaySettings pt;
                    pt.engineId = EngineRegistry::instance().fallbackEngine().id;
                    pt.side = PlaySettings::PlayBlack;
                    pt.elo = 2000;
                    pt.moveTimeMs = 500;
                    pt.fromCurrentPosition = true;
                    const bool ok = w4.startEngineGame(pt) && waitFor([&] { return sj->game().mainline().size() == 1; }, 15000);
                    auto* panel = w4.findChild<PlayEnginePanel*>();
                    auto* button = w4.findChild<QToolButton*>("PlayStrength");
                    const QString buttonBefore = button->text();
                    hits = 0;
                    // No comment is written in the notation, whatever the setting
                    panel->setStrength(false, 1700);
                    panel->setStrength(false, 1500, 1);
                    const QString merged = QString::fromStdString(sj->game().mainline().front()->comment);
                    panel->setStrength(false, 2000);
                    const bool mergedOk = merged.isEmpty() && sj->game().mainline().front()->comment.empty();
                    panel->setStrength(true, 2000);
                    const chess::Node* first = sj->game().mainline().front();
                    const QString comment = QString::fromStdString(first->comment);
                    const QString white = QString::fromStdString(sj->game().header("White"));
                    const chess::Position& now = sj->current()->pos;
                    game->playHumanMove(now.legalMoves().front());
                    const bool replied = waitFor([&] { return sj->game().mainline().size() == 3; }, 15000);
                    const bool saved = PlayDialog::savedSettings().fullStrength && PlayDialog::savedSettings().tablebases && button->text() == QStringLiteral("Max. + tables");
                    const bool skillMax = game->managedOptions().value(QStringLiteral("Skill Level")) == QStringLiteral("20")
                                          && game->managedOptions().value(QStringLiteral("UCI_Elo")) == QStringLiteral("3190") // no effect, but at the maximum like the slider
                                          && game->managedOptions().value(QStringLiteral("UCI_LimitStrength")) == QStringLiteral("false");
                    // "Maximum strength" level without the tables: the engine no longer probes them
                    const qint64 hitsWith = hits;
                    panel->setStrength(true, 2000, 0, false);
                    const QString noTables = QString::fromStdString(sj->game().mainline().back()->comment);
                    hits = 0;
                    game->playHumanMove(sj->current()->pos.legalMoves().front());
                    const bool repliedAgain = waitFor([&] { return sj->game().mainline().size() == 5; }, 15000);
                    const bool tablesOff = repliedAgain && hits == 0 && button->text() == QStringLiteral("Max.") && !PlayDialog::savedSettings().tablebases
                                           && noTables.isEmpty();
                    hits = hitsWith;
                    panel->setStrength(false, 1500);
                    const QString last = QString::fromStdString(sj->game().mainline().back()->comment);
                    // Settings of the Opponent tab during the game: the game's Elo, greyed out; elsewhere, the same engine
                    // keeps its settings (Engine management, Spectator tab) and is not subject to the game's
                    bool managerOk = false;
                    {
                        const QString id = game->engineConfig().id;
                        const EngineConfig stored = EngineRegistry::instance().engine(id);
                        EngineManagerDialog global(&w4, id);
                        auto* eloGlobal = global.findChild<QSpinBox*>("Option_UCI_Elo");
                        const bool globalFree = eloGlobal && eloGlobal->isEnabled() && QString::number(eloGlobal->value()) == stored.value(QStringLiteral("UCI_Elo"));
                        EngineTabDialog spectatorTab(QLatin1String(tabengine::AnalysisTab), QStringLiteral("Spectateur 1"), id, &w4);
                        auto* eloSpectator = spectatorTab.findChild<QSpinBox*>("Option_UCI_Elo");
                        const bool spectatorFree = eloSpectator && eloSpectator->isEnabled() && QString::number(eloSpectator->value()) == stored.value(QStringLiteral("UCI_Elo"));
                        EngineTabDialog dlg(QLatin1String(tabengine::PlayTab), QStringLiteral("Adversaire"), id, &w4);
                        dlg.setGameOptions(game->managedOptions());
                        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); // old editors of the list
                        auto* elo = dlg.findChild<QSpinBox*>("Option_UCI_Elo");
                        auto* limit = dlg.findChild<QCheckBox*>("Option_UCI_LimitStrength");
                        auto* threads = dlg.findChild<QSpinBox*>("Option_Threads");
                        // "Skill Level": no effect as long as Elo limits the strength, hence greyed out; at the maximum at full strength
                        auto* skill = dlg.findChild<QSpinBox*>("Option_Skill Level");
                        const bool skillOk = skill && !skill->isEnabled() && game->managedOptions().value(QStringLiteral("Skill Level")).isNull()
                                             && game->managedOptions().contains(QStringLiteral("Skill Level"));
                        const bool greyed = skillOk && elo && limit && threads && elo->value() == 1500 && !elo->isEnabled() && limit->isChecked() && !limit->isEnabled() && threads->isEnabled();
                        // Threads changed in the Opponent tab: the game takes it, neither the engine nor the Spectator tab moves
                        const QString before = stored.value(QStringLiteral("Threads"));
                        const int mine = before.toInt() == 2 ? 3 : 2;
                        if (threads) threads->setValue(mine);
                        const bool isolated = game->engineConfig().value(QStringLiteral("Threads")).toInt() == mine
                                              && EngineRegistry::instance().engine(id).value(QStringLiteral("Threads")) == before
                                              && tabengine::effective(QLatin1String(tabengine::AnalysisTab), stored).value(QStringLiteral("Threads")) == before
                                              && tabengine::overrides(QLatin1String(tabengine::PlayTab), id).value(QStringLiteral("Threads")).toInt() == mine;
                        // A spectator has its own settings too, with no effect on the game
                        tabengine::setValue(QStringLiteral("spectator-test"), stored, QStringLiteral("Hash"), QStringLiteral("64"));
                        const bool spectatorOwn = tabengine::effective(QStringLiteral("spectator-test"), stored).value(QStringLiteral("Hash")) == QStringLiteral("64")
                                                  && game->engineConfig().value(QStringLiteral("Hash")) == stored.value(QStringLiteral("Hash"));
                        tabengine::forget(QStringLiteral("spectator-test"));
                        // A Spectator panel takes its own settings (engine restarted with them), with no effect on the game
                        bool panelOwn = false;
                        {
                            EnginePanel spectator;
                            spectator.setTabKey(QStringLiteral("spectator-panel-test"));
                            spectator.setEngine(stored);
                            const QString hashBefore = spectator.engineConfig().value(QStringLiteral("Hash"));
                            tabengine::setValue(spectator.tabKey(), stored, QStringLiteral("Hash"), QStringLiteral("48"));
                            const bool taken = spectator.engineConfig().value(QStringLiteral("Hash")) == QStringLiteral("48") && game->engineConfig().value(QStringLiteral("Hash")) == stored.value(QStringLiteral("Hash"));
                            tabengine::clear(spectator.tabKey(), stored.id);
                            panelOwn = taken && hashBefore == stored.value(QStringLiteral("Hash")) && spectator.engineConfig().value(QStringLiteral("Hash")) == hashBefore;
                            tabengine::forget(spectator.tabKey());
                        }
                        // "Use the engine's settings again": the tab goes back to the engine's values
                        dlg.findChild<QPushButton*>("TabEngineReset")->click();
                        const bool reset = tabengine::overrides(QLatin1String(tabengine::PlayTab), id).isEmpty() && game->engineConfig().value(QStringLiteral("Threads")) == before;
                        managerOk = globalFree && spectatorFree && greyed && isolated && spectatorOwn && panelOwn && reset
                                    && EngineRegistry::instance().engine(id).values == stored.values && stored.value(QStringLiteral("UCI_Elo")) != QStringLiteral("1500");
                        if (!managerOk) std::printf("ONGLETS %d %d %d %d %d %d %d\n", globalFree, spectatorFree, greyed, isolated, spectatorOwn, panelOwn, reset);
                    }
                    panel->setStrength(false, 1500, 2); // beginner level, also during a game
                    const QString beginner = QString::fromStdString(sj->game().mainline().back()->comment);
                    const bool beginnerOk = beginner.isEmpty() && button->text() == QStringLiteral("Débutant 2")
                                            && game->settings().beginnerLevel == 2 && PlayDialog::savedSettings().beginnerLevel == 2;
                    panel->setStrength(false, 1500);
                    game->stop();
                    check(ok && replied && hits > 0 && skillMax && mergedOk && tablesOff && managerOk && beginnerOk && buttonBefore == QStringLiteral("Elo 2000") && button->text() == QStringLiteral("Elo 1500") && saved
                              && comment.isEmpty() && !white.contains(QStringLiteral("Elo"))
                              && last.isEmpty() && !PlayDialog::savedSettings().fullStrength,
                          qPrintable(QStringLiteral("force changée en cours de partie sans commentaire dans la notation : « %1 », %2 consultations ensuite, puis « %3 »").arg(comment).arg(hits).arg(last)));
                }
                // Tablebases can be shown on the Play and Tournament pages; icon of the Opponent tab according to the engine's state;
                // a click on the evaluation line during the search makes the engine play right away
                {
                    auto* tbDock = w4.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("tablebase"));
                    const bool onPlay = tbDock && tbDock->toggleViewAction()->isVisible();
                    // Without handing control back to the event loop between the steps: that is what caused the crash
                    // (QtADS floating window awaiting deletion, see MainWindow::flushFloatingContainers)
                    tbDock->toggleView(true);
                    const bool shown = !tbDock->isClosed() && !tbDock->isFloating() && tbDock->dockAreaWidget();
                    tbDock->toggleView(false);
                    w4.setPage(MainWindow::PageTournament);
                    const bool onTournament = tbDock->toggleViewAction()->isVisible();
                    w4.setPage(MainWindow::PageBoard);
                    sj->newGame(chess::Position::startPos());
                    PlaySettings pm;
                    pm.engineId = EngineRegistry::instance().fallbackEngine().id;
                    pm.side = PlaySettings::PlayWhite;
                    pm.fullStrength = true;
                    pm.moveTimeMs = 60000; // it would think for a minute
                    const bool ok = w4.startEngineGame(pm);
                    auto* panel = w4.findChild<PlayEnginePanel*>();
                    auto* oppDock = w4.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("playEngine"));
                    if (game->useBook()) game->setUseBook(false);
                    const qint64 idleKey = oppDock->icon().cacheKey();
                    const QString depthBefore = w4.findChild<PlayEnginePanel*>()->findChild<QLabel*>(QStringLiteral("PlayDepth"))->text();
                    emit b4->moveRequested(sj->current()->pos.parseUci("e2e4"));
                    // "Thinking" starts before the engine has begun its search (options, isready): the click is
                    // only meant for a search under way, so wait for its first line (a depth is displayed)
                    auto* depthLabel = panel->findChild<QLabel*>(QStringLiteral("PlayDepth"));
                    const bool thinking = waitFor([&] { return game->isThinking(); }, 15000)
                                          && waitFor([&] { return depthLabel && depthLabel->text() != depthBefore && depthLabel->text().contains(QRegularExpression(QStringLiteral("\\d"))); }, 30000);
                    wait(300);
                    const bool red = oppDock->icon().cacheKey() != idleKey;
                    auto* stats = panel->findChild<QWidget*>(QStringLiteral("PlayStats"));
                    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(5, 5), stats->mapToGlobal(QPointF(5, 5)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(stats, &release);
                    const bool forced = waitFor([&] { return sj->game().mainline().size() == 2; }, 20000); // wide: loaded machine
                    const bool green = waitFor([&] { return oppDock->icon().cacheKey() == idleKey; }, 10000);
                    game->setUseBook(true);
                    game->stop();
                    if (!(onPlay && shown && onTournament && ok && thinking && red && forced && green)) std::printf("ASTUCE %d %d %d %d %d %d %d %d\n", onPlay, shown, onTournament, ok, thinking, red, forced, green);
                    check(onPlay && shown && onTournament, "panneau Finales : affichable sur les pages Jouer et Tournoi");
                    check(ok && thinking && red && green, "onglet Adversaire : icône rouge pendant la réflexion, verte ensuite (plus de pastille)");
                    check(forced, "clic sur la ligne d'évaluation pendant la réflexion : le moteur joue tout de suite");
                }
                // Flag fallen then move played: the same game continues, notation and clocks kept,
                // the fallen clock stays at 0:00 and no longer falls
                {
                    sj->newGame(chess::Position::startPos());
                    PlaySettings pf;
                    pf.engineId = EngineRegistry::instance().fallbackEngine().id;
                    pf.side = PlaySettings::PlayWhite;
                    pf.elo = 2000;
                    pf.baseMs = 1500; // you: 1.5 s; the engine: 30 s (it replies fast)
                    pf.engineSeparate = true;
                    pf.engineBaseMs = 30000;
                    const bool ok = w4.startEngineGame(pf);
                    auto* board = b4;
                    emit board->moveRequested(sj->current()->pos.parseUci("e2e4"));
                    const bool replied = waitFor([&] { return sj->game().mainline().size() == 2; }, 15000);
                    const bool fell = waitFor([&] { return !game->isActive(); }, 15000) && game->canContinue() && sj->game().header("Result") == "0-1";
                    const qint64 engineMs = game->clock()->remaining(chess::Black);
                    emit board->moveRequested(sj->current()->pos.parseUci("d2d4"));
                    const bool continued = game->isActive() && !game->canContinue() && sj->game().header("Result") == "*" && game->humanColor() == chess::White
                                           && sj->game().mainline().size() >= 3 && sj->game().mainline().front()->move == sj->game().root()->pos.parseUci("e2e4");
                    const bool answered = waitFor([&] { return sj->game().mainline().size() == 4; }, 15000);
                    waitFor([] { return false; }, 400); // player to move, clock at zero: it no longer falls
                    const bool noFlag = game->isActive() && game->clock()->remaining(chess::White) == 0 && game->clock()->isFlagless(chess::White)
                                        && game->clock()->remaining(chess::Black) > 0 && game->clock()->remaining(chess::Black) <= engineMs;
                    game->stop();
                    if (!(ok && replied && fell && continued && answered && noFlag)) std::printf("DRAPEAU %d %d %d %d %d %d\n", ok, replied, fell, continued, answered, noFlag);
                    check(ok && replied && fell && continued && answered && noFlag,
                          qPrintable(QStringLiteral("drapeau tombé puis coup joué : la partie continue (%1), notation gardée (%2 demi-coups), pendule à 0:00 sans rechute (%3)")
                                         .arg(continued).arg(sj->game().mainline().size()).arg(noFlag)));
                }
                // "Play against the engine" dialog: a single stepped slider, from the beginner levels to maximum strength with tables
                {
                    PlayDialog dlg(EngineRegistry::instance().fallbackEngine().id);
                    auto* strength = dlg.findChild<StrengthSlider*>();
                    auto* slider = strength ? strength->findChild<QSlider*>() : nullptr;
                    auto* label = strength ? strength->findChild<QLabel*>() : nullptr;
                    auto at = [&](int value) {
                        slider->setValue(value);
                        const PlaySettings p = dlg.settings();
                        return QStringLiteral("%1%2/%3/%4/%5").arg(p.fullStrength).arg(p.fullStrength && p.tablebases).arg(p.beginnerLevel).arg(p.elo).arg(label->text());
                    };
                    strength->setStrength(false, 1493, 0, true); // Off-level Elo: the nearest
                    const QString nearest = at(slider->value());
                    const int last = slider->maximum();
                    const QString d1 = at(0), d3 = at(2), e1320 = at(3), e1350 = at(4), e3150 = at(last - 2), max = at(last - 1), tables = at(last);
                    strength->setStrength(true, 1500, 0, false);
                    const QString back = at(slider->value());
                    check(dlg.findChildren<QSlider*>().size() == 1 && slider->singleStep() == 1 && last == 42 && (label->alignment() & Qt::AlignHCenter)
                              && nearest == QStringLiteral("00/0/1500/Elo 1500") && d1 == QStringLiteral("00/1/1500/Débutant 1 — le plus faible, gaffes fréquentes")
                              && d3 == QStringLiteral("00/3/1500/Débutant 3 — sous 1320 Elo, erreurs rares") && e1320 == QStringLiteral("00/0/1320/Elo 1320")
                              && e1350 == QStringLiteral("00/0/1350/Elo 1350") && e3150 == QStringLiteral("00/0/3150/Elo 3150") && max == QStringLiteral("10/0/3150/Force maximale")
                              && tables.startsWith(QStringLiteral("11/0/3150/Force maximale + Tables de finales")) && back == QString(max).replace(QStringLiteral("3150"), QStringLiteral("1500")),
                          qPrintable(QStringLiteral("fenêtre de partie : curseur de force à %1 paliers (%2 · %3 · %4 · %5)").arg(last + 1).arg(d1, e1350, max, tables)));
                }
                QObject::disconnect(conn);
                check(okFull && hitsFull > 0 && shownFull.contains(QStringLiteral("tables")) && okWeak && hitsWeak == 0 && !shownWeak.contains(QStringLiteral("tables")),
                      qPrintable(QStringLiteral("partie et tables : %1 consultations à pleine force (« %2 »), %3 à niveau limité").arg(hitsFull).arg(shownFull).arg(hitsWeak)));
                tablebases::setSources({});
                tablebases::apply();
            }
        }

        // --- Evaluation panel: closed, the automatic evaluation stops (on each page)
        {
            auto* autoEval = w4.findChild<AutoEvaluator*>();
            ads::CDockWidget* graphDock = w4.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("graph"));
            const char* names[] = {"Bases de données", "Analyse", "Jouer"};
            for (int p = 0; p < 3; ++p) {
                w4.setPage(MainWindow::Page(p));
                graphDock->toggleView(true);
                const bool on = autoEval->isEnabled();
                graphDock->closeDockWidget(); // the panel's cross
                const bool off = !autoEval->isEnabled();
                graphDock->toggleView(true);
                check(on && off && autoEval->isEnabled(),
                      qPrintable(QStringLiteral("page %1 : fermer le panneau Évaluation coupe l'évaluation, le rouvrir la relance").arg(names[p])));
            }
            w4.setPage(MainWindow::PageTournament);
            check(!autoEval->isEnabled() && graphDock->isClosed() && !w4.findChild<EvalBar*>()->isVisible(),
                  "page Tournoi : pas d'évaluation (panneau, barre et Stockfish)");
            w4.setPage(MainWindow::PageAnalysis);
            graphDock->closeDockWidget();
            w4.setPage(MainWindow::PageBoard);
            const bool boardOn = autoEval->isEnabled();
            w4.setPage(MainWindow::PageAnalysis);
            check(boardOn && !autoEval->isEnabled(), "chaque page garde son choix (fermé sur Analyse, ouvert sur Jouer)");
            graphDock->toggleView(true);
            // The embedded Stockfish really evaluates the displayed position (Databases page,
            // whose database game is reopened afterwards)
            w4.setPage(MainWindow::PageDatabase);
            graphDock->toggleView(true);
            s4->newGame();
            play4("e2e4");
            check(waitFor([&] { return s4->current()->eval.valid; }, 5000), "Stockfish embarqué évalue la position (barre et courbe)");
            w4.loadGameFromDatabase(0);
        }

        // --- Spectator engines of the Analysis page
        w4.setPage(MainWindow::PageAnalysis);
        auto analysisPanels = [&] {
            QList<EnginePanel*> l;
            for (EnginePanel* p : w4.findChildren<EnginePanel*>())
                if (p != w4.enginePanel()) l << p;
            return l;
        };
        [&] { for (QAction* a : w4.findChildren<QAction*>()) if (a->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+K"))) { a->trigger(); break; } }();
        [&] { for (QAction* a : w4.findChildren<QAction*>()) if (a->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+K"))) { a->trigger(); break; } }();
        QList<EnginePanel*> spectators = analysisPanels();
        check(spectators.size() == 2 && !spectators[0]->isAnalysing() && !spectators[1]->isAnalysing(), "deux moteurs spectateurs ajoutés, à l'arrêt");
        {
            auto title = [&](const char* name) {
                auto* d = w4.findChild<ads::CDockWidget*>(QLatin1String(name));
                return d ? d->windowTitle() : QString();
            };
            check(title("engine") == QLatin1String("SPECTATEUR 1") && title("spectator1") == QLatin1String("SPECTATEUR 2")
                      && title("spectator2") == QLatin1String("SPECTATEUR 3"),
                  "spectateurs numérotés : Spectateur 1, 2, 3");
        }
        spectators[0]->setAnalysing(true); // Play in its panel
        spectators[1]->setAnalysing(true);
        check(spectators[0]->engineConfig().id != spectators[1]->engineConfig().id, "chaque spectateur a son propre moteur");
        check(spectators[0]->arrowColor() != spectators[1]->arrowColor() && spectators[0]->arrowColor() != w4.enginePanel()->arrowColor(),
              "chaque moteur a sa couleur de flèche");
        w4.setPage(MainWindow::PageBoard);
        check(!spectators[0]->isAnalysing() && !spectators[0]->isVisible(), "page Jouer : spectateurs arrêtés et masqués");
        {
            // "+" button of the opponent's panel: a spectator of the Play page, shown under the opponent
            {
                // Clock and Evaluation: strips of fixed height (their area cannot be made taller)
                QApplication::processEvents();
                bool locked = true;
                for (const char* name : {"clock", "graph"}) {
                    auto* d = w4.findChild<ads::CDockWidget*>(QLatin1String(name));
                    auto* area = d && !d->isClosed() ? d->dockAreaWidget() : nullptr;
                    if (!area) continue;
                    const bool alone = area->openedDockWidgets().size() == 1;
                    locked = locked && (alone ? area->maximumHeight() <= 120 && area->maximumHeight() >= area->minimumSizeHint().height() : area->maximumHeight() == QWIDGETSIZE_MAX);
                }
                check(locked, "page Jouer : hauteur de la pendule et de l'évaluation verrouillée");
            }
            bool inMenu = false;
            for (QAction* a : w4.findChildren<QAction*>())
                if (a->menu() && a->text() == QObject::tr("Ajouter un moteur spectateur")) inMenu = a->isVisible();
            check(inMenu, "page Jouer : « Ajouter un moteur spectateur » présent dans le menu Moteur");
            auto* invite = w4.findChild<QToolButton*>("PlayInviteSpectator");
            check(invite && invite->menu(), "page Jouer : bouton d'invitation d'un spectateur");
            if (invite && invite->menu()) {
                emit invite->menu()->aboutToShow();
                const QList<QAction*> engines = invite->menu()->actions();
                check(!engines.isEmpty() && engines.first()->isEnabled(), "page Jouer : le bouton liste les moteurs");
                if (!engines.isEmpty()) engines.first()->trigger();
                QApplication::processEvents();
                EnginePanel* guest = nullptr;
                for (EnginePanel* p : analysisPanels())
                    if (!spectators.contains(p)) guest = p;
                auto* guestDock = guest ? w4.findChild<ads::CDockWidget*>(guest->tabKey()) : nullptr;
                check(guest && guestDock && guest->isVisible() && !guest->isAnalysing() && guestDock->windowTitle() == QLatin1String("SPECTATEUR 1"),
                      "page Jouer : spectateur invité, affiché, à l'arrêt");
                if (guest) {
                    guest->setAnalysing(true);
                    check(guest->isAnalysing(), "page Jouer : le spectateur invité analyse");
                }
                if (guestDock) emit guestDock->closeRequested(); // its cross removes it
                QApplication::processEvents();
                check(analysisPanels().size() == spectators.size(), "page Jouer : spectateur retiré par sa croix");
            }
        }
        w4.setPage(MainWindow::PageAnalysis);
        check(!spectators[0]->isAnalysing() && !spectators[1]->isAnalysing() && !w4.enginePanel()->isAnalysing() && spectators[0]->isVisible(),
              "retour sur Analyse : les spectateurs sont affichés, aucun moteur ne redémarre seul");
        spectators[0]->setAnalysing(true); // Play again, by hand
        {
            // Analysed side: only the other side → waiting (no arrow); both → resumes
            EnginePanel* p = spectators[0];
            Move best;
            int bestCount = 0;
            auto c = QObject::connect(p, &EnginePanel::bestMoveChanged, [&](Move m) {
                best = m;
                ++bestCount;
            });
            const Color toMove = w4.findChild<GameSession*>()->current()->pos.sideToMove();
            p->setSides(toMove == White ? EnginePanel::BlackOnly : EnginePanel::WhiteOnly);
            wait(400);
            check(p->isAnalysing() && best.isNull(), "camp non analysé : le moteur attend, sans flèche");
            p->setSides(EnginePanel::BothSides);
            check(waitFor([&] { return !best.isNull(); }, 3000), "les deux camps : le moteur analyse de nouveau");
            p->setSides(toMove == White ? EnginePanel::WhiteOnly : EnginePanel::BlackOnly);
            best = {};
            check(waitFor([&] { return !best.isNull(); }, 3000), "camp au trait analysé : le moteur analyse");
            QObject::disconnect(c);
            Q_UNUSED(bestCount);
        }
        {
            // Single-line engine (no MultiPV option, like Fruit): no list of lines.
            EngineConfig single = shuffleB;
            single.options.erase(std::remove_if(single.options.begin(), single.options.end(),
                                                [](const UciOption& o) { return o.name == QLatin1String("MultiPV"); }),
                                 single.options.end());
            EnginePanel* p = spectators[0];
            const EngineConfig before = p->engineConfig();
            p->setEngine(single);
            check(p->findChild<module::VariantsStepper*>()->isHidden(), "moteur sans MultiPV : liste des variantes masquée");
            p->setEngine(before);
            check(!p->findChild<module::VariantsStepper*>()->isHidden(), "moteur avec MultiPV : liste des variantes affichée");
        }
        auto* dock = qobject_cast<ads::CDockWidget*>(w4.findChild<ads::CDockWidget*>(QStringLiteral("spectator2")));
        check(dock != nullptr, "panneau du deuxième spectateur trouvé");
        if (dock) emit dock->closeRequested(); // the panel's cross
        wait(100);
        check(analysisPanels().size() == 1, "la croix retire le spectateur");
        // The first engine panel can be removed too, then comes back with Add a spectator
        auto* firstDock = w4.findChild<ads::CDockWidget*>(QStringLiteral("engine"));
        w4.enginePanel()->setAnalysing(true);
        if (firstDock) emit firstDock->closeRequested();
        wait(100);
        check(firstDock && firstDock->isClosed() && !w4.enginePanel()->isAnalysing(), "la croix retire aussi le premier panneau moteur");
        w4.setPage(MainWindow::PageBoard);
        w4.setPage(MainWindow::PageAnalysis);
        check(firstDock->isClosed(), "premier panneau retiré : il ne revient pas en changeant de page");
        [&] { for (QAction* a : w4.findChildren<QAction*>()) if (a->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+K"))) { a->trigger(); break; } }();
        check(!firstDock->isClosed() && analysisPanels().size() == 1 && !w4.enginePanel()->isAnalysing(),
              "Ajouter un spectateur (Ctrl+Maj+K) : le premier panneau revient, à l'arrêt");
        analysisPanels().first()->setSides(EnginePanel::BlackOnly); // restored on restart

        // --- Spectator engine of the Play page (Engine menu, Ctrl+Shift+K)
        w4.setPage(MainWindow::PageBoard);
        const QList<EnginePanel*> beforePlay = analysisPanels();
        [&] { for (QAction* a : w4.findChildren<QAction*>()) if (a->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+K"))) { a->trigger(); break; } }();
        EnginePanel* playSpectator = nullptr;
        for (EnginePanel* p : analysisPanels())
            if (!beforePlay.contains(p)) playSpectator = p;
        check(playSpectator && playSpectator->isVisible() && !playSpectator->isAnalysing(), "page Jouer : spectateur ajouté, visible et à l'arrêt");
        {
            ads::CDockWidget* d = nullptr;
            for (auto* dw : w4.findChildren<ads::CDockWidget*>())
                if (dw->widget() == playSpectator) d = dw;
            check(d && d->windowTitle() == QLatin1String("SPECTATEUR 1"), "page Jouer : numérotation propre à la page (Spectateur 1)");
            auto* opp = w4.findChild<ads::CDockWidget*>(QStringLiteral("playEngine"));
            check(opp && opp->windowTitle() == QLatin1String("ADVERSAIRE"), "page Jouer : onglet « Adversaire »");
            check(opp && !opp->features().testFlag(ads::CDockWidget::DockWidgetClosable), "l'onglet « Adversaire » ne peut pas être fermé");
            w4.setPage(MainWindow::PageAnalysis);
            check(opp && opp->isClosed(), "page Analyse : l'adversaire n'est pas affiché");
            w4.setPage(MainWindow::PageBoard);
            check(opp && !opp->isClosed() && opp->isVisible(), "retour sur Jouer : l'adversaire est affiché");
        }
        if (playSpectator) {
            playSpectator->setAnalysing(true);
            w4.setPage(MainWindow::PageAnalysis);
            check(!playSpectator->isVisible() && !playSpectator->isAnalysing(), "page Analyse : le spectateur de Jouer est masqué et en pause");
            check(analysisPanels().first()->isVisible() || w4.enginePanel()->isVisible(), "page Analyse : ses propres moteurs restent affichés");
            w4.setPage(MainWindow::PageBoard);
            check(playSpectator->isVisible() && !playSpectator->isAnalysing(), "retour sur Jouer : son spectateur est affiché mais ne redémarre pas seul");
            int visibleOnPlay = 0;
            for (EnginePanel* p : analysisPanels()) visibleOnPlay += p->isVisible();
            check(visibleOnPlay == 1, "page Jouer : seuls ses spectateurs sont affichés");
        }
        w4.setPage(MainWindow::PageTournament);
        w4.close();
    }
    wait(200);
    {
        MainWindow w5;
        w5.resize(1400, 900);
        w5.show();
        auto* s5 = w5.findChild<GameSession*>();
        if (!(w5.page() == MainWindow::PageTournament && w5.tournament()->playedCount() == 2)) { // diagnostics of a rare failure
            QFile sf(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/session.json"));
            const bool opened = sf.open(QIODevice::ReadOnly);
            const QByteArray raw = sf.readAll();
            std::printf("RELANCE page=%d parties=%d session.json ouvert=%d octets=%lld restaurer=%d début=%s\n", int(w5.page()), w5.tournament()->playedCount(), opened,
                        static_cast<long long>(raw.size()), prefs::restoreSession(), raw.left(300).constData());
        }
        check(w5.page() == MainWindow::PageTournament && w5.tournament()->playedCount() == 2, "relance : tournoi restauré");
        w5.setPage(MainWindow::PageDatabase);
        check(!s5->game().mainline().empty() && s5->game().mainline().front()->san == "c4", "relance : partie de la page Bases de données restaurée");
        {
            auto* list = w5.gamesPanel();
            check(list->count() == 2 && QFileInfo(list->model()->filePath()).fileName() == QStringLiteral("base.pgn"),
                  "relance : les deux bases rouvertes, la dernière affichée de nouveau");
        }
        w5.setPage(MainWindow::PageAnalysis);
        int restored = 0;
        for (EnginePanel* p : w5.findChildren<EnginePanel*>()) restored += p != w5.enginePanel() && p->isVisible();
        check(restored == 1, "relance : le moteur spectateur est restauré sur la page Analyse");
        {
            int sidesKept = 0;
            for (EnginePanel* p : w5.findChildren<EnginePanel*>())
                sidesKept += p != w5.enginePanel() && p->isVisible() && p->sides() != EnginePanel::BothSides;
            check(sidesKept == 1, "relance : le camp analysé par le spectateur est restauré");
        }
        w5.setPage(MainWindow::PageBoard);
        int restoredPlay = 0;
        for (EnginePanel* p : w5.findChildren<EnginePanel*>()) restoredPlay += p != w5.enginePanel() && p->isVisible();
        check(restoredPlay == 1, "relance : le spectateur de la page Jouer est restauré sur sa page");
        w5.setPage(MainWindow::PageAnalysis);
        for (EnginePanel* p : w5.findChildren<EnginePanel*>())
            if (p != w5.enginePanel()) p->setAnalysing(true); // restarted at the next opening
        check(!s5->game().mainline().empty() && s5->game().mainline().front()->san == "c4", "relance : partie de la page Analyse restaurée");
        w5.close();
    }


    // ================= Polyglot opening book
    {
        // Start → e4 (weight 3), d4 (weight 1); entries sorted by key, as in a real .bin.
        const QString bookPath = QDir::temp().filePath(QStringLiteral("hydra64_ui_book.bin"));
        {
            QFile f(bookPath);
            check(f.open(QIODevice::WriteOnly), "fichier de test écrit");
            const uint64_t key = polyglot::key(Position::startPos());
            auto entry = [&](const char* from, const char* to, uint16_t weight) {
                const Square a = parseSquare(from), b = parseSquare(to);
                const uint16_t mv = uint16_t(fileOf(b) | rankOf(b) << 3 | fileOf(a) << 6 | rankOf(a) << 9);
                char e[16] = {};
                for (int i = 0; i < 8; ++i) e[i] = char(key >> (56 - 8 * i));
                e[8] = char(mv >> 8), e[9] = char(mv), e[10] = char(weight >> 8), e[11] = char(weight);
                f.write(e, 16);
            };
            entry("d2", "d4", 1);
            entry("e2", "e4", 3);
        }
        const QString badPath = QDir::temp().filePath(QStringLiteral("hydra64_ui_book_bad.bin"));
        {
            QFile f(badPath);
            check(f.open(QIODevice::WriteOnly), "fichier de test écrit");
            f.write("pas un livre");
        }
        MainWindow w6;
        w6.resize(1400, 900);
        w6.show();
        w6.setPage(MainWindow::PageAnalysis);
        auto* s6 = w6.findChild<GameSession*>();
        s6->newGame();
        // Tab not displayed: ADS detaches it from the window, it is found through the manager.
        auto* bookDock = w6.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("book"));
        auto* book = bookDock ? qobject_cast<BookPanel*>(bookDock->widget()) : nullptr;
        check(bookDock && !bookDock->isClosed(), "page Analyse : panneau Bibliothèque présent (onglet)");
        QString bookError;
        const bool opened = book && book->openBook(bookPath, &bookError);
        check(opened, "bibliothèque Polyglot ouverte dans le panneau");
        if (!opened) return 1;
        const auto rows = book->rows();
        check(rows.size() == 2 && Position::toUci(rows[0].move) == "e2e4" && std::abs(rows[0].share - 0.75) < 1e-9,
              "panneau Bibliothèque : coups de la position et leur part (e4 75 %)");
        check(!book->openBook(badPath) && book->bookPath() == QFileInfo(bookPath).absoluteFilePath(),
              "fichier invalide refusé, la bibliothèque ouverte est gardée");
        emit book->movePicked(rows[0].move);
        check(s6->game().mainline().size() == 1 && s6->game().mainline().front()->san == "e4" && book->rows().isEmpty(),
              "double-clic : le coup est joué, la nouvelle position est hors bibliothèque");
        s6->back();
        check(book->rows().size() == 2, "retour au départ : les coups de la bibliothèque réapparaissent");
        // Play page: the opponent engine plays (or not) the moves of the open book
        {
            w6.setPage(MainWindow::PageBoard);
            auto* game6 = w6.findChild<EngineGame*>();
            int bookMoves = 0;
            QObject::connect(game6, &EngineGame::bookMovePlayed, game6, [&] { ++bookMoves; });
            PlaySettings ps6;
            ps6.engineId = shuffle.id;
            ps6.side = PlaySettings::PlayBlack; // the engine has White: it plays the first move
            ps6.moveTimeMs = 100;
            game6->setUseBook(true);
            check(w6.startEngineGame(ps6) && waitFor([&] { return !s6->game().mainline().empty(); }, 5000),
                  "page Jouer : le moteur joue son premier coup");
            const std::string first = s6->game().mainline().empty() ? "" : s6->game().mainline().front()->san;
            check(bookMoves == 1 && (first == "e4" || first == "d4"), "bibliothèque activée : le moteur joue un coup de la bibliothèque");
            game6->stop();
            game6->setUseBook(false);
            bookMoves = 0;
            check(w6.startEngineGame(ps6) && waitFor([&] { return !s6->game().mainline().empty(); }, 5000) && bookMoves == 0,
                  "bibliothèque désactivée : le moteur réfléchit lui-même");
            game6->stop();
            game6->setUseBook(true);
            check(QSettings().value("play/useBook").toBool(), "choix de la bibliothèque mémorisé");
        }
        w6.close();
        {
            MainWindow w7;
            auto* d7 = w7.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("book"));
            check(qobject_cast<BookPanel*>(d7->widget())->bookPath() == QFileInfo(bookPath).absoluteFilePath(), "relance : la bibliothèque est rouverte");
            check(BookPanel::recentBooks().value(0) == QFileInfo(bookPath).absoluteFilePath(), "bibliothèque dans la liste des récentes");
            w7.close();
        }
        QFile::remove(bookPath);
        QFile::remove(badPath);

        // ChessBase book: start → e4 ! (recommended, 100 games), d4 (to avoid)
        {
            const QString base = QDir::temp().filePath(QStringLiteral("hydra64_ui_ctg"));
            const Position start = Position::startPos();
            const Move e4 = start.parseSan("e4"), d4 = start.parseSan("d4");
            auto be = [](QByteArray& out, uint32_t v, int bytes) {
                for (int i = bytes - 1; i >= 0; --i) out += char((v >> (8 * i)) & 0xff);
            };
            auto entry = [&](const Position& pos, QList<Move> moves, int w, int d, int l, uint8_t rec) {
                const ctg::Signature sig = ctg::signature(pos);
                QByteArray e(reinterpret_cast<const char*>(sig.bytes.data()), sig.length);
                e += char(1 + 2 * moves.size());
                for (const Move& m : moves) {
                    e += char(ctg::encodeMove(pos, m));
                    e += char(m == e4 ? ctg::Good : ctg::NoAnnotation);
                }
                for (uint32_t v : {uint32_t(w + d + l), uint32_t(l), uint32_t(w), uint32_t(d)}) be(e, v, 3);
                e += QByteArray(4 + 7 + 7, '\0');
                e += char(rec);
                e += QByteArray(2, '\0');
                return e;
            };
            QByteArray page;
            be(page, 3, 2);
            be(page, 0, 2);
            page += entry(start, {e4, d4}, 0, 0, 0, 0);
            page += entry(start.after(e4), {}, 30, 50, 20, ctg::Recommended); // wins of the side that played e4
            page += entry(start.after(d4), {}, 1, 0, 3, ctg::Avoid);
            page.resize(4096, '\0');
            QByteArray cto(16, '\0'), ctb;
            for (int k = 0; k < 3; ++k) be(cto, 0, 4);
            be(ctb, 0, 4);
            be(ctb, 0, 4);
            be(ctb, 2, 4);
            for (const auto& [ext, data] : {std::pair{".ctg", QByteArray(4096, '\0') + page}, std::pair{".cto", cto}, std::pair{".ctb", ctb}}) {
                QFile f(base + QLatin1String(ext));
                check(f.open(QIODevice::WriteOnly) && f.write(data) == data.size(), "fichier CTG de test écrit");
            }
            MainWindow w8;
            auto* d8 = w8.findChild<ads::CDockManager*>()->findDockWidget(QStringLiteral("book"));
            auto* panel = qobject_cast<BookPanel*>(d8->widget());
            // ChessBase books no longer open directly: they go through conversion.
            QString refused;
            check(!panel->openBook(base + QStringLiteral(".ctb"), &refused) && panel->format() == BookPanel::NoBook && refused.contains(QStringLiteral("convertir")),
                  "livre ChessBase : ouverture directe refusée, avec renvoi vers la conversion");
            w8.findChild<GameSession*>()->newGame();
            conversion::Request req;
            req.source = base + QStringLiteral(".ctg");
            req.destination = base + QStringLiteral(".hy64book");
            req.target = conversion::Target::Hydra64Tree;
            check(w8.runConversion(req, true, false) && panel->format() == BookPanel::Tree, "Fichier → Convertir : livre ChessBase → arbre Hydra64, ouvert dans le panneau");
            const auto rows = panel->rows();
            check(rows.size() == 2 && rows[0].move == e4 && rows[0].annotation == QStringLiteral("!") && rows[0].games == 100
                      && std::abs(rows[0].score() - 55.0) < 1e-9 && rows[0].recommendation == ctg::Recommended && rows[1].recommendation == ctg::Avoid,
                  "arbre converti : annotation, parties, score, coup recommandé et coup à éviter");
            req.destination = base + QStringLiteral(".bin");
            req.target = conversion::Target::Polyglot;
            check(w8.runConversion(req, true, false) && panel->format() == BookPanel::Polyglot && panel->rows().size() == 2
                      && panel->rows()[0].move == e4 && panel->rows()[0].weight == 100 && panel->rows()[1].weight == 0,
                  "Fichier → Convertir : livre ChessBase → Polyglot (poids = parties, 0 pour le coup à éviter)");

            // Aquarium tree exported as text: conversion then opening
            const QString txt = QDir::temp().filePath(QStringLiteral("hydra64_ui_tree.txt"));
            const QString hy64 = QDir::temp().filePath(QStringLiteral("hydra64_ui_tree.hy64book"));
            {
                QFile f(txt);
                check(f.open(QIODevice::WriteOnly)
                          && f.write("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -\te2e4|e4|50|30|20|3115|2023|0\td2d4|d4|10|5|5|2800|2020|0\r\n") > 0,
                      "export Aquarium de test écrit");
            }
            QString errTree;
            check(!panel->openBook(txt, &errTree) && errTree.contains(QStringLiteral("convertir")), "export Aquarium ouvert directement : il faut le convertir");
            req.source = txt;
            req.destination = hy64;
            req.target = conversion::Target::Hydra64Tree;
            check(w8.runConversion(req, true, false) && panel->format() == BookPanel::Tree, "export Aquarium converti puis ouvert");
            const auto treeRows = panel->rows();
            check(treeRows.size() == 2 && treeRows[0].move == e4 && treeRows[0].games == 100 && treeRows[0].maxElo == 3115
                      && treeRows[0].year == 2023 && std::abs(treeRows[0].score() - 65.0) < 1e-9,
                  "panneau Arbre d'ouverture (arbre Aquarium converti) : parties, score, Elo maximum, année");
            QFile::remove(base + QStringLiteral(".hy64book"));
            QFile::remove(base + QStringLiteral(".bin"));
            QFile::remove(base + QStringLiteral(".cto"));
            QFile::remove(txt);
            panel->closeBook();
            check(panel->format() == BookPanel::NoBook && panel->rows().isEmpty(), "bibliothèque fermée");
            w8.close();
            QFile::remove(base + QStringLiteral(".ctg"));
            QFile::remove(base + QStringLiteral(".ctb"));
            QFile::remove(hy64);
        }
    }

    // ================= Database icons: all drawn, list or icon view, chosen icon kept
    {
        // HYDRA64_SHOTS=folder: control images (icon sheet, panel in both views)
        const QString shots = qEnvironmentVariable("HYDRA64_SHOTS");
        const auto& all = baseicons::entries();
        QStringList empty, themeless;
        QSet<QString> ids;
        QImage sheet(16 * 44 + 8, (int(all.size()) / 16 + 1) * 44 + 8, QImage::Format_ARGB32);
        sheet.fill(theme::Panel);
        {
            QPainter painter(&sheet);
            int n = 0;
            for (const baseicons::Entry& e : all) {
                const QString id = QString::fromLatin1(e.id);
                const QImage drawn = baseicons::icon(id).pixmap(32, 32).toImage().convertToFormat(QImage::Format_ARGB32);
                int inked = 0;
                for (int y = 0; y < drawn.height(); ++y)
                    for (int x = 0; x < drawn.width(); ++x) inked += qAlpha(drawn.pixel(x, y)) > 96;
                if (inked < 30) empty << id;
                if (e.theme < 0 || e.theme >= baseicons::themes().size()) themeless << id;
                ids << id;
                painter.drawImage(8 + (n % 16) * 44, 8 + (n / 16) * 44, drawn);
                ++n;
            }
        }
        if (!shots.isEmpty()) sheet.save(shots + QStringLiteral("/icones.png"));
        for (const QString& id : std::as_const(empty)) std::printf("       icône vide : %s\n", qPrintable(id));
        check(all.size() >= 100 && empty.isEmpty() && themeless.isEmpty() && ids.size() == int(all.size()) && ids.contains(QLatin1String(baseicons::DefaultId)),
              "icônes des bases : une centaine, toutes dessinées, classées, sans doublon");

        const QString pgn = QDir::tempPath() + QStringLiteral("/hydra64-icones.pgn");
        QFile::copy(QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn"), pgn);
        baseicons::setChosen(pgn, QStringLiteral("chess-knight"));
        check(baseicons::chosen(pgn) == QLatin1String("chess-knight") && baseicons::chosen(pgn + QStringLiteral("x")) == QLatin1String(baseicons::DefaultId),
              "icônes des bases : icône choisie gardée par base, icône par défaut sinon");
        MainWindow w10;
        w10.resize(1400, 900);
        w10.show();
        auto* list10 = w10.gamesPanel();
        check(w10.openDatabase(pgn), "icônes des bases : base ouverte");
        w10.setPage(MainWindow::PageDatabase);
        wait(150);
        if (!shots.isEmpty()) w10.grab().save(shots + QStringLiteral("/page-bases.png")); // default layout
        {
            // Evaluation bar: hidden by default on the Databases page, can be shown through the View menu (choice specific to the page)
            QAction* barAction = nullptr;
            for (QAction* a : w10.findChildren<QAction*>())
                if (a->text() == QObject::tr("Barre d'évaluation")) barAction = a;
            auto* bar10 = w10.findChild<EvalBar*>();
            const bool hiddenFirst = barAction && bar10 && !bar10->isVisible() && !barAction->isChecked() && prefs::showEvalBar();
            if (barAction) barAction->trigger();
            wait(50);
            const bool shownOnBases = bar10 && bar10->isVisible() && prefs::showEvalBarOnBases() && prefs::showEvalBar();
            if (barAction) barAction->trigger();
            wait(50);
            check(hiddenFirst && shownOnBases && bar10 && !bar10->isVisible() && !prefs::showEvalBarOnBases() && prefs::showEvalBar(),
                  "page Bases : barre d'évaluation masquée par défaut, affichable par le menu Affichage sans toucher aux autres pages");
        }
        auto* bases10 = list10->basesWidget()->findChild<QListWidget*>(QStringLiteral("DatabaseList"));
        QToolButton* asList = nullptr;
        QToolButton* asIcons = nullptr;
        for (QToolButton* b : list10->basesWidget()->findChildren<QToolButton*>()) {
            if (b->toolTip() == QObject::tr("Afficher les bases en liste")) asList = b;
            if (b->toolTip() == QObject::tr("Afficher les bases en icônes")) asIcons = b;
        }
        check(bases10 && asList && asIcons && bases10->viewMode() == QListView::IconMode && asIcons->isChecked() && !asList->isChecked(),
              "bases ouvertes : en icônes par défaut");
        if (asList) asList->click();
        wait(50);
        if (bases10 && asList && asIcons) {
            if (!shots.isEmpty()) list10->basesWidget()->grab().save(shots + QStringLiteral("/bases-liste.png"));
            asIcons->click();
            wait(100);
            check(bases10->viewMode() == QListView::IconMode && asIcons->isChecked() && !asList->isChecked() && bases10->count() == list10->count()
                      && bases10->viewport()->acceptDrops() && QSettings().value("gameList/basesAsIcons").toBool(),
                  "bases ouvertes : vue en icônes (retenue, dépôt de parties toujours possible)");
            if (!shots.isEmpty()) list10->basesWidget()->grab().save(shots + QStringLiteral("/bases-icones.png"));
            asList->click();
            wait(50);
            check(bases10->viewMode() == QListView::ListMode && bases10->viewport()->acceptDrops(), "bases ouvertes : retour à la liste");
            // Game list: whatever the window's width, all the columns fit
            // (no scroll bar), and the text columns shrink when room is short.
            auto* table10 = list10->findChild<QTableView*>();
            QHeaderView* hh = table10->horizontalHeader();
            bool fits = true;
            int wideWhite = 0, narrowWhite = 0;
            for (int width : {1900, 1250, 1000}) {
                w10.resize(width, 900);
                wait(250);
                const int over = hh->length() - table10->viewport()->width();
                if (over != 0) { // neither a scroll bar nor a gap on the right
                    fits = false;
                    std::printf("       fenêtre de %d px : colonnes %d px pour %d px\n", width, hh->length(), table10->viewport()->width());
                    for (int c = 0; c < GameListModel::ColumnCount; ++c)
                        std::printf("         colonne %d : %d px%s%s\n", c, hh->sectionSize(c), hh->isSectionHidden(c) ? " (masquée)" : "", hh->sectionResizeMode(c) == QHeaderView::Stretch ? " (étirée)" : "");
                }
                (width == 1900 ? wideWhite : narrowWhite) = hh->sectionSize(GameListModel::White);
            }
            check(fits, "liste des parties : les colonnes remplissent exactement la largeur, fenêtre large, étroite ou très étroite");
            check(narrowWhite > 0 && narrowWhite <= wideWhite, "liste des parties : les colonnes de texte se réduisent quand la liste rétrécit");
        }
        // A column width set by hand on the header survives the later layouts; a double-click
        // on its edge gives it back to the automatic width.
        if (auto* table10 = list10->findChild<QTableView*>()) {
            QHeaderView* hh = table10->horizontalHeader();
            w10.resize(1900, 900);
            wait(250);
            const int automatic = hh->sectionSize(GameListModel::White);
            const int byHand = automatic + 37;
            const QPoint off(-5, -5); // outside any section: no sort started
            QMouseEvent press(QEvent::MouseButtonPress, off, hh->viewport()->mapToGlobal(off), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(hh->viewport(), &press);
            hh->resizeSection(GameListModel::White, byHand);
            QMouseEvent release(QEvent::MouseButtonRelease, off, hh->viewport()->mapToGlobal(off), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(hh->viewport(), &release);
            const bool filled = hh->length() == table10->viewport()->width();
            w10.resize(1700, 900);
            wait(250);
            const bool kept = hh->sectionSize(GameListModel::White) == byHand && hh->length() == table10->viewport()->width();
            if (!kept) std::printf("       Blancs : %d px automatique, %d px à la main, %d px après\n", automatic, byHand, hh->sectionSize(GameListModel::White));
            check(filled && kept && QSettings().value("gameList/columnWidths").toMap().size() == 1,
                  "liste des parties : une largeur de colonne réglée à la main est gardée (et retenue), la liste reste remplie");
            emit hh->sectionHandleDoubleClicked(GameListModel::White);
            wait(50);
            check(hh->sectionSize(GameListModel::White) != byHand && QSettings().value("gameList/columnWidths").toMap().isEmpty()
                      && hh->length() == table10->viewport()->width(),
                  "liste des parties : double-clic sur le bord d'une colonne, largeur automatique reprise");
        }
        // Open databases: a close cross on each icon, and the order changed with the mouse.
        if (bases10 && asIcons) {
            const QString pgn2 = QDir::tempPath() + QStringLiteral("/hydra64-icones-2.pgn");
            QFile::copy(QStringLiteral(HYDRA64_SOURCE_DIR "/tests/data/classics.pgn"), pgn2);
            const bool opened = w10.openDatabase(pgn2);
            asIcons->click();
            wait(100);
            const int n = bases10->count();
            int crosses = 0;
            for (QToolButton* b : bases10->findChildren<QToolButton*>(QStringLiteral("DatabaseClose"))) crosses += !b->isVisible(); // shown under the pointer only
            check(opened && n >= 2 && crosses == n, "bases ouvertes en icônes : une croix de fermeture par base, masquée hors survol");
            auto names = [bases10] {
                QStringList l;
                for (int i = 0; i < bases10->count(); ++i) l << bases10->item(i)->text();
                return l;
            };
            auto drag = [bases10](int from, int to) {
                QWidget* vp = bases10->viewport();
                const QPoint a = bases10->visualItemRect(bases10->item(from)).center(), b = bases10->visualItemRect(bases10->item(to)).center();
                QMouseEvent press(QEvent::MouseButtonPress, a, vp->mapToGlobal(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &press);
                for (const QPoint& p : {(a + b) / 2, b}) {
                    QMouseEvent move(QEvent::MouseMove, p, vp->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &move);
                }
                QMouseEvent release(QEvent::MouseButtonRelease, b, vp->mapToGlobal(b), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &release);
            };
            for (bool icons : {true, false}) {
                (icons ? asIcons : asList)->click();
                wait(100);
                const QStringList before = names();
                // The dragged database (pressed, hence displayed) goes first. In list view the panel
                // shows two rows only: the second one is taken, the others would scroll the list.
                const int from = icons ? n - 1 : 1;
                drag(from, 0);
                wait(50);
                QStringList expected = before;
                expected.move(from, 0);
                const bool moved = names() == expected && bases10->currentRow() == 0 && list10->currentIndex() == 0
                                   && QFileInfo(list10->model()->filePath()).fileName() == expected.first() && list10->model()->rowCount() > 0;
                drag(0, from);
                wait(50);
                if (!(moved && names() == before && list10->currentIndex() == from))
                    std::printf("       déplacée : %d, avant [%s], après [%s], base affichée n° %d\n", moved, qPrintable(before.join(',')), qPrintable(names().join(',')), list10->currentIndex());
                check(moved && names() == before && list10->currentIndex() == from,
                      icons ? "bases ouvertes en icônes : ordre changé à la souris, la base affichée reste la même"
                            : "bases ouvertes en liste : ordre changé à la souris, la base affichée reste la même");
            }
            // The cross closes its database
            const QString last = bases10->item(n - 1)->text();
            if (auto* cross = bases10->itemWidget(bases10->item(n - 1))->findChild<QToolButton*>(QStringLiteral("DatabaseClose"))) cross->click();
            wait(100);
            check(bases10->count() == n - 1 && !names().contains(last), "bases ouvertes : la croix ferme sa base");
            QFile::remove(pgn2);
        }
        {
            // The clock says why it stays empty on a game without recorded times
            ClockPanel panel;
            ClockPanel* clock10 = &panel;
            const auto displays = clock10->findChildren<QLabel*>(QStringLiteral("BigClock"));
            clock10->setClocks(-1, -1, -1, false);
            const bool explained = displays.size() == 2 && displays[0]->toolTip().contains(QStringLiteral("[%clk]")) && displays[1]->toolTip() == displays[0]->toolTip();
            clock10->setClocks(60000, 60000, 0, true);
            check(explained && displays[0]->toolTip() == QObject::tr("Pendule des Blancs"), "pendule : dit pourquoi elle reste vide sur une partie sans temps enregistrés");
        }
        baseicons::setChosen(pgn, QString::fromLatin1(baseicons::DefaultId));
        w10.close();
        QFile::remove(pgn);
    }

    // ================= Notation editing: move replaced (with or without what followed), variation moved down, null move
    {
        auto line = [](const chess::Game& g) {
            QStringList l;
            for (const chess::Node* n : g.mainline()) l << QString::fromStdString(n->san);
            return l.join(' ');
        };
        auto build = [](GameSession& s) {
            s.newGame();
            for (const char* san : {"e4", "e5", "Nf3", "Nc6", "Bb5", "a6"}) s.playMove(s.current()->pos.parseSan(san));
            s.game().mainline()[3]->comment = "défense";
        };
        GameSession s;
        s.switchSlot(GameSession::AnalysisSlot);
        build(s);
        // "Insert": 2.Nf3 replaced with 2.Nc3 — …Nc6, Bb5 and …a6 still go, their comment follows
        int armedSeen = 0;
        QObject::connect(&s, &GameSession::replaceModeChanged, &s, [&](bool on) { armedSeen += on; });
        s.armReplace(s.game().mainline()[2], true);
        const bool armed = s.replaceArmed() && s.current() == s.game().mainline()[1] && armedSeen == 1;
        s.playMove(s.current()->pos.parseSan("Nc3"));
        const bool inserted = !s.replaceArmed() && line(s.game()) == QLatin1String("e4 e5 Nc3 Nc6 Bb5 a6") && s.game().mainline()[3]->comment == "défense"
                              && s.current() == s.game().mainline()[2] && s.canUndo();
        s.undo();
        const bool undone = line(s.game()) == QLatin1String("e4 e5 Nf3 Nc6 Bb5 a6");
        // "Overwrite": what followed goes
        s.armReplace(s.game().mainline()[2], false);
        s.playMove(s.current()->pos.parseSan("f4"));
        const bool overwritten = line(s.game()) == QLatin1String("e4 e5 f4");
        // Kept only as long as legal: 1.e4 replaced with 1.d4, …e5 goes on, 2.Nf3 too, …Nc6, then Bb5 is no longer possible
        build(s);
        s.armReplace(s.game().mainline()[0], true);
        s.playMove(s.current()->pos.parseSan("d4"));
        const bool truncated = line(s.game()) == QLatin1String("d4 e5 Nf3 Nc6");
        // Going elsewhere gives up; a move that is already another line is refused
        build(s);
        s.goTo(s.game().mainline()[1]);
        s.playMove(s.current()->pos.parseSan("f4")); // variation 2.f4
        s.armReplace(s.game().mainline()[2], false);
        s.goTo(s.game().mainline()[4]);
        const bool givenUp = !s.replaceArmed();
        int refused = 0;
        QObject::connect(&s, &GameSession::replaceRefused, &s, [&] { ++refused; });
        s.armReplace(s.game().mainline()[2], false);
        s.playMove(s.current()->pos.parseSan("f4"));
        const bool kept = refused == 1 && line(s.game()) == QLatin1String("e4 e5 Nf3 Nc6 Bb5 a6");
        // Variation moved down: the main line 2.Nf3 becomes the variation, 2.f4 the main line; then back up
        chess::Node* afterE5 = s.game().mainline()[1];
        s.demoteVariation(s.game().mainline()[2]);
        const bool demoted = afterE5->children.size() == 2 && afterE5->children[0]->san == "f4" && afterE5->children[1]->san == "Nf3";
        s.promoteVariation(afterE5->children[1].get());
        const bool promoted = afterE5->children[0]->san == "Nf3" && !s.game().demoteVariation(afterE5->children[1].get());
        // Null move: played, written "--" and read back; refused in check
        s.toEnd();
        const bool passed = s.playNullMove() && s.current()->san == "--" && s.current()->pos.sideToMove() == chess::Black;
        chess::Game reread;
        chess::pgn::parseGame(chess::pgn::writeGame(s.game()), reread);
        GameSession c;
        c.newGame();
        for (const char* san : {"e4", "e5", "Qh5", "Nc6", "Qxf7+"}) c.playMove(c.current()->pos.parseSan(san));
        check(armed && inserted && undone && overwritten && truncated && givenUp && kept && demoted && promoted && passed
                  && line(reread) == QLatin1String("e4 e5 Nf3 Nc6 Bb5 a6 --") && !c.playNullMove(),
              "édition de la notation : coup remplacé (suite gardée tant qu'elle est légale, ou supprimée), variante descendue, coup nul");
        if (!(armed && inserted && undone && overwritten && truncated && givenUp && kept && demoted && promoted && passed))
            std::printf("       %d %d %d %d %d %d %d %d %d %d [%s]\n", armed, inserted, undone, overwritten, truncated, givenUp, kept, demoted, promoted, passed, qPrintable(line(s.game())));
    }

    // ================= Guessing the moves of a game: the game's move, another one, hint, engine verdict
    {
        using chess::makeSquare;
        chess::Game scholar;
        scholar.setHeader("White", "Blancs");
        scholar.setHeader("Black", "Noirs");
        scholar.setHeader("Result", "1-0");
        for (const char* san : {"e4", "e5", "Qh5", "Nc6", "Bc4", "Nf6", "Qxf7#"}) scholar.playMove(scholar.current()->pos.parseSan(san));
        const chess::Move qh5{makeSquare(3, 0), makeSquare(7, 4)}, nf3{makeSquare(6, 0), makeSquare(5, 2)}, qxf7{makeSquare(7, 4), makeSquare(5, 6)},
            qxe5{makeSquare(7, 4), makeSquare(4, 4)};
        GuessDialog guess(scholar, chess::White, 2);
        guess.show();
        const bool start = guess.questionCount() == 3 && guess.maxScore() == 9 && guess.expected() && guess.expected()->san == "Qh5";
        const bool exact = guess.answer(qh5) == GuessDialog::Result::Exact && guess.score() == 3 && guess.currentQuestion() == 1 && guess.expected()->san == "Bc4";
        const bool missed = guess.answer(nf3) == GuessDialog::Result::Missed && guess.score() == 3 && guess.currentQuestion() == 1
                            && guess.answer(nf3) == GuessDialog::Result::Ignored; // the move of the game is shown: no second try
        guess.proceed();
        guess.showHint();
        const bool hinted = guess.currentQuestion() == 2 && guess.answer(qxf7) == GuessDialog::Result::Exact && guess.finished() && guess.score() == 4 && guess.exactCount() == 2;
        check(start && exact && missed && hinted && GuessDialog::pointsFor(0.5, 0.5) == 2 && GuessDialog::pointsFor(0.42, 0.5) == 1 && GuessDialog::pointsFor(0.2, 0.5) == 0
                  && GuessDialog::pointsFor(0.9, 0.5) == 2 && GuessDialog(scholar, chess::Black, 9).questionCount() == 0,
              "deviner les coups : coup de la partie (3 points), autre coup montré, indice (1 point), partie terminée");
        // With the engine: the queen given away instead of the mate is judged much worse
        GuessDialog judgedGuess(scholar, chess::White, 4);
        judgedGuess.setEngine(AutoEvaluator::findStockfish());
        judgedGuess.show();
        int verdict = -1;
        QObject::connect(&judgedGuess, &GuessDialog::judged, &judgedGuess, [&](int points) { verdict = points; });
        const bool judging = judgedGuess.questionCount() == 1 && judgedGuess.answer(qxe5) == GuessDialog::Result::Judging && judgedGuess.waiting();
        waitFor([&] { return verdict >= 0; }, 20000);
        if (const QString shots = qEnvironmentVariable("HYDRA64_SHOTS"); !shots.isEmpty()) judgedGuess.grab().save(shots + QStringLiteral("/deviner.png"));
        judgedGuess.proceed();
        check(judging && verdict == 0 && judgedGuess.score() == 0 && judgedGuess.finished(), "deviner les coups : un autre coup jugé par le moteur (dame donnée au lieu du mat : 0 point)");
    }

    // ================= Assisted analysis: target squares coloured by the quality of the move, threat as an arrow
    {
        using assisted::Quality;
        auto ev = [](int cp) { return annotation::PositionEval{true, false, cp, 20, {}}; };
        const auto classes = assisted::classify({{1, ev(50)}, {2, ev(20)}, {3, ev(-30)}, {4, ev(-250)}, {5, annotation::PositionEval{}}});
        const annotation::PositionEval mateForHim{true, true, 1, 20, {}};
        check(classes.size() == 4 && classes.value(1) == Quality::Good && classes.value(2) == Quality::Good && classes.value(3) == Quality::Playable
                  && classes.value(4) == Quality::Bad && assisted::isThreat(ev(0), mateForHim) && assisted::isThreat(ev(0), ev(150)) && !assisted::isThreat(ev(0), ev(40))
                  && !assisted::isThreat(ev(-300), ev(320)),
              "analyse assistée : coups classés d'après les chances perdues (bon, jouable, faute) ; menace seulement si passer coûte");
        MainWindow w11;
        w11.resize(1400, 900);
        w11.show();
        w11.setPage(MainWindow::PageAnalysis);
        auto* session = w11.findChild<GameSession*>();
        auto* board = w11.findChild<BoardWidget*>();
        AssistedAnalysis* help = w11.assistedAnalysis();
        const bool offFirst = help && !help->squaresEnabled() && !help->threatEnabled() && !prefs::assistedAnalysis() && !prefs::threatArrow();
        session->newGame();
        for (const char* san : {"e4", "e5", "Bc4", "Nc6", "Qh5"}) session->playMove(session->current()->pos.parseSan(san));
        prefs::setAssistedAnalysis(true);
        prefs::setThreatArrow(true);
        QElapsedTimer timer;
        timer.start();
        const bool gone = help && waitFor([&] { return help->isIdle() && !help->qualities().isEmpty(); }, 20000);
        const qint64 ms = timer.elapsed();
        using chess::makeSquare;
        const chess::Move g6{makeSquare(6, 6), makeSquare(6, 5)}, nf6{makeSquare(6, 7), makeSquare(5, 5)}, mate{makeSquare(7, 4), makeSquare(5, 6)};
        const auto q = help ? help->qualities() : QHash<int, Quality>();
        const bool judged = gone && q.size() == int(session->current()->pos.legalMoves().size()) && q.value(assisted::moveKey(g6)) == Quality::Good
                            && q.value(assisted::moveKey(nf6)) == Quality::Bad && help->threat() == mate;
        if (!judged && help)
            std::printf("       %d coups jugés sur %d, g6 %d, Cf6 %d, menace %s\n", int(q.size()), int(session->current()->pos.legalMoves().size()), int(q.value(assisted::moveKey(g6))),
                        int(q.value(assisted::moveKey(nf6))), chess::Position::toUci(help->threat()).c_str());
        check(offFirst && judged, qPrintable(QStringLiteral("analyse assistée : après 3.Dh5, …g6 bon, …Cf6 faute, menace Dxf7 en flèche (%1 ms)").arg(ms)));
        if (const QString shots = qEnvironmentVariable("HYDRA64_SHOTS"); !shots.isEmpty() && board) {
            const QPoint at = board->squareCenter(makeSquare(6, 7)).toPoint(); // the g8 knight picked up
            QMouseEvent press(QEvent::MouseButtonPress, at, board->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, at, board->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(board, &press);
            QApplication::sendEvent(board, &release);
            wait(50);
            board->grab().save(shots + QStringLiteral("/analyse-assistee.png"));
        }
        // A move played: the marks of the old position go at once, the new one is gone through
        session->playMove(g6);
        const bool cleared = help && help->qualities().isEmpty() && help->threat().isNull();
        const bool again = help && waitFor([&] { return help->isIdle() && !help->qualities().isEmpty(); }, 20000);
        {
            // Saved layouts: a panel closed, the layout saved under a name, the default one back, then the saved one again
            ads::CDockWidget* notation = nullptr;
            for (ads::CDockWidget* d : w11.findChildren<ads::CDockWidget*>())
                if (d->objectName() == QLatin1String("notation")) notation = d;
            const bool none = w11.savedLayouts().isEmpty() && !w11.applySavedLayout(QStringLiteral("absente")) && !w11.saveLayoutAs(QStringLiteral("  "));
            if (notation) notation->toggleView(false);
            wait(50);
            const bool saved = w11.saveLayoutAs(QStringLiteral("sans notation / essai")) && w11.savedLayouts() == QStringList{QStringLiteral("sans notation / essai")};
            if (notation) notation->toggleView(true);
            wait(50);
            const bool reopened = notation && !notation->isClosed();
            const bool applied = w11.applySavedLayout(QStringLiteral("sans notation / essai"));
            wait(50);
            const bool closedAgain = notation && notation->isClosed();
            w11.setPage(MainWindow::PageDatabase);
            const bool perPage = w11.savedLayouts().isEmpty(); // another page: its own layouts
            w11.setPage(MainWindow::PageAnalysis);
            const bool kept = notation && notation->isClosed();
            w11.removeSavedLayout(QStringLiteral("sans notation / essai"));
            if (notation) notation->toggleView(true);
            check(none && saved && reopened && applied && closedAgain && perPage && kept && w11.savedLayouts().isEmpty(),
                  "dispositions enregistrées : enregistrée sous un nom, reprise, propre à sa page, supprimée");
        }
        // Never on the Play page; switched off, nothing is left on the board
        w11.setPage(MainWindow::PageBoard);
        const bool notOnPlay = help && !help->squaresEnabled() && !help->threatEnabled() && help->qualities().isEmpty();
        w11.setPage(MainWindow::PageAnalysis);
        const bool back = help && help->squaresEnabled() && help->threatEnabled();
        prefs::setAssistedAnalysis(false);
        prefs::setThreatArrow(false);
        check(cleared && again && notOnPlay && back && help && !help->squaresEnabled() && help->qualities().isEmpty(),
              "analyse assistée : effacée au coup joué puis refaite, jamais sur la page Jouer, éteinte par le menu");
        w11.close();
    }

    // ================= User folder: bundled databases installed, empty folders created, file choosers
    {
        const QString root = userdirs::root();
        userdirs::install();
        const bool made = QFileInfo(userdirs::openingBooks()).isDir() && QFileInfo(userdirs::tablebases()).isDir()
                          && QDir(userdirs::openingBooks()).isEmpty() && QDir(userdirs::tablebases()).isEmpty();
        check(root.endsWith(QStringLiteral("/ui_tests_home/hydra64")) && made, "dossier de l'utilisateur : openingbooks/ et tablebases/ créés, vides");
        int shippedBases = 0, installed = 0;
        QDirIterator it(QStringLiteral(HYDRA64_SOURCE_DIR "/databases"), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString rel = QDir(QStringLiteral(HYDRA64_SOURCE_DIR "/databases")).relativeFilePath(it.next());
            ++shippedBases;
            installed += QFileInfo::exists(userdirs::databases() + QLatin1Char('/') + rel);
        }
        check(shippedBases >= 23 && installed == shippedBases && bundled::databasesDir() == userdirs::databases(),
              qPrintable(QStringLiteral("dossier de l'utilisateur : %1 fichiers livrés sur %2 copiés dans databases/, lus de là").arg(installed).arg(shippedBases)));
        // A database deleted by the user in an already installed folder does not come back.
        const QString gone = userdirs::databases() + QStringLiteral("/games/Masters_1000.hy64");
        QFile::remove(gone);
        userdirs::install();
        check(!QFileInfo::exists(gone), "dossier de l'utilisateur : une base supprimée n'est pas recopiée");
        QDir(userdirs::databases() + QStringLiteral("/games")).removeRecursively();
        userdirs::install();
        check(QFileInfo::exists(gone), "dossier de l'utilisateur : un dossier livré manquant est réinstallé");
        {
            // Preparation against the most frequent player of a bundled database: a real tree, quickly
            hy64db::Database masters;
            std::string merr;
            if (masters.open(gone.toStdString(), &merr)) {
                const auto use = masters.nameUsage(hy64db::Player);
                const uint32_t top = uint32_t(std::max_element(use.begin(), use.end()) - use.begin());
                const QString name = QString::fromStdString(masters.names().name(hy64db::Player, top));
                QElapsedTimer timer;
                timer.start();
                PreparationDialog dlg(masters, QStringLiteral("Masters_1000"), name);
                const qint64 ms = timer.elapsed();
                const auto& pr = dlg.preparation();
                dlg.show();
                wait(100);
                if (const QString shots = qEnvironmentVariable("HYDRA64_SHOTS"); !shots.isEmpty()) {
                    dlg.grab().save(shots + QStringLiteral("/preparation.png"));
                    dlg.setRepertoireLoader([](chess::Color c, chess::Game& out) { // a small Black repertoire, for the image
                        if (c != chess::Black) return false;
                        chess::Game line;
                        for (const char* san : {"d4", "Nf6", "c4", "e6", "Nf3", "d5"}) line.playMove(line.current()->pos.parseSan(san));
                        return chess::repertoire::addLine(out, line) > 0;
                    });
                    dlg.resize(1000, 1500);
                    wait(100);
                    dlg.grab().save(shots + QStringLiteral("/preparation-repertoire.png"));
                    QFile f(shots + QStringLiteral("/preparation.html"));
                    if (f.open(QIODevice::WriteOnly)) f.write(dlg.html(false).toUtf8());
                }
                {
                    // Error report, without an engine: the costliest move of a lost game, from given evaluations
                    chess::Game lost;
                    for (const char* san : {"e4", "e5", "Qh5", "Ke7", "Qxe5#"}) lost.playMove(lost.current()->pos.parseSan(san));
                    QList<annotation::PositionEval> evals{{true, false, 30, 20, {QStringLiteral("e2e4")}},  {true, false, -30, 20, {QStringLiteral("e7e5")}},
                                                          {true, false, 30, 20, {QStringLiteral("g1f3")}},  {true, false, -10, 20, {QStringLiteral("b8c6")}},
                                                          {true, true, 1, 20, {QStringLiteral("h5e5")}},    {true, true, 0, 99, {}}};
                    const errorreport::GameErrors ge = errorreport::analyse(lost, evals, 0, chess::Black);
                    const errorreport::GameErrors none = errorreport::analyse(lost, evals, 0, chess::White);
                    chess::Position bare;
                    bare.setFen("4k3/8/8/8/8/8/8/R3K3 w - - 0 40");
                    check(ge.hasTurning && ge.turning.ply == 4 && ge.turning.san == QLatin1String("Ke7") && ge.turning.best == QLatin1String("Nc6") && ge.turning.drop > 0.9
                              && ge.blunders == 1 && ge.mistakes == 1 && ge.turning.phase == errorreport::Phase::Opening && ge.turning.standing == errorreport::Standing::Equal
                              && ge.turning.before == QLatin1String("+0.10") && ge.turning.after == QLatin1String("#1") && !none.hasTurning && none.mistakes == 0
                              && errorreport::phaseOf(bare, 79) == errorreport::Phase::Endgame && errorreport::phaseOf(chess::Position::startPos(), 41) == errorreport::Phase::Middlegame,
                          "rapport d'erreur : faute la plus coûteuse d'une partie (coup, meilleur coup, évaluations, phase), rien pour le vainqueur");
                    // Results kept next to the database; an entry whose game no longer matches is dropped
                    const QString cache = QDir::tempPath() + QStringLiteral("/hydra64-test.errors.json");
                    QFile::remove(cache);
                    errorreport::GameErrors kept = ge, stale = ge;
                    kept.game = 3;
                    kept.fingerprint = errorreport::fingerprint(masters, 3);
                    kept.engine = QStringLiteral("moteur");
                    kept.movetimeMs = 200;
                    stale.game = 4;
                    stale.fingerprint = QStringLiteral("autre partie");
                    const bool saved = errorreport::saveCache(cache, {{3, kept}, {4, stale}});
                    const auto back = errorreport::loadCache(cache, masters);
                    check(saved && back.size() == 1 && back.count(3) && back.at(3).turning.san == QLatin1String("Ke7") && back.at(3).turning.ply == 4 && back.at(3).movetimeMs == 200
                              && back.at(3).turning.phase == errorreport::Phase::Opening && back.at(3).blunders == 1
                              && errorreport::cachePath(QStringLiteral("/a/b/Mega.hy64")) == QLatin1String("/a/b/Mega.errors.json"),
                          "rapport d'erreur : résultats gardés dans un fichier, relus ; partie qui ne correspond plus écartée");
                    // With the engine: the most recent loss of the player analysed in the background, shown and kept
                    QFile::remove(cache);
                    dlg.setErrorCache(cache);
                    dlg.setErrorOptions(1, 0.1);
                    const bool invited = dlg.html().contains(QStringLiteral("Analyser ses défaites")) && dlg.errors().empty() && dlg.recentLosses().size() == 1;
                    bool finished = false, completed = false;
                    QObject::connect(&dlg, &PreparationDialog::scanFinished, &dlg, [&](bool ok) { finished = true, completed = ok; });
                    QElapsedTimer scanTimer;
                    scanTimer.start();
                    const bool started = dlg.startErrorScan();
                    waitFor([&] { return finished; }, 120000);
                    const qint64 scanMs = scanTimer.elapsed();
                    const uint32_t lossId = dlg.recentLosses().empty() ? 0 : dlg.recentLosses().front();
                    const bool shown = dlg.errors().count(lossId) && dlg.html().contains(QStringLiteral("défaites les plus récentes analysées")) && !dlg.isScanning();
                    const bool again = !dlg.startErrorScan(); // nothing left to analyse
                    const bool keptOnDisk = errorreport::loadCache(cache, masters).size() == 1;
                    if (const QString shots = qEnvironmentVariable("HYDRA64_SHOTS"); !shots.isEmpty()) {
                        dlg.setErrorOptions(5, 0.1);
                        finished = false;
                        if (dlg.startErrorScan()) waitFor([&] { return finished; }, 300000);
                        QFile f(shots + QStringLiteral("/preparation-fautes.html"));
                        if (f.open(QIODevice::WriteOnly)) f.write(dlg.html(false).toUtf8());
                    }
                    check(invited && started && finished && completed && shown && again && keptOnDisk,
                          qPrintable(QStringLiteral("rapport d'erreur : dernière défaite de %1 analysée par le moteur en %2 s, affichée et gardée").arg(name).arg(scanMs / 1000.0, 0, 'f', 1)));
                    QFile::remove(cache);
                }
                if (const QString shots = qEnvironmentVariable("HYDRA64_SHOTS"); !shots.isEmpty()) {
                    PieceHeatDialog heatDlg(hy64db::pieceHeat(masters, masters.allGames()), QStringLiteral("Masters_1000"));
                    heatDlg.show();
                    wait(100);
                    heatDlg.grab().save(shots + QStringLiteral("/pieces.png"));
                }
                check(pr.root.games >= 10 && !pr.root.children.empty() && pr.root.children[0].byPlayer && !pr.openings.empty() && dlg.linkCount() > 3 && ms < 3000,
                      qPrintable(QStringLiteral("préparation contre un joueur : %1, %2 parties avec les Blancs, rapport en %3 ms").arg(name).arg(pr.root.games).arg(ms)));
            } else {
                check(false, "préparation contre un joueur : base livrée ouverte");
            }
        }
        // File choosers: the folder of the kind first, then the last folder chosen for that kind.
        const bool defaults = userdirs::startDir(userdirs::Kind::Database) == userdirs::databases() && userdirs::startDir(userdirs::Kind::OpeningBook) == userdirs::openingBooks()
                              && userdirs::startDir(userdirs::Kind::Tablebase) == userdirs::tablebases();
        userdirs::remember(userdirs::Kind::OpeningBook, QDir::tempPath() + QStringLiteral("/un-arbre.hy64book"));
        const bool remembered = userdirs::startDir(userdirs::Kind::OpeningBook) == QDir::tempPath() && userdirs::startDir(userdirs::Kind::Database) == userdirs::databases();
        QSettings().remove(QStringLiteral("dirs"));
        check(defaults && remembered, "fenêtres de choix : dossier du genre demandé d'abord, puis le dernier utilisé pour ce genre");
    }

    // ================= Splitters: dragged all the way, none makes a panel disappear
    {
        MainWindow w9;
        w9.resize(1400, 900);
        w9.show();
        auto* viewer9 = w9.findChild<BoardViewer*>();
        QStringList crushed;
        const char* names[] = {"Bases", "Analyse", "Jouer", "Tournoi"};
        for (MainWindow::Page p : {MainWindow::PageDatabase, MainWindow::PageAnalysis, MainWindow::PageBoard, MainWindow::PageTournament}) {
            bool seen = false;
            answerNextDialog(QObject::tr("Continuer la partie"), &seen);
            w9.setPage(p);
            wait(200);
            if ((p == MainWindow::PageAnalysis || p == MainWindow::PageBoard) && !qEnvironmentVariable("HYDRA64_SHOTS").isEmpty()) {
                // Control image of the default layout of the Analysis and Play pages
                for (QAction* a : w9.findChildren<QAction*>())
                    if (a->text() == QObject::tr("Réinitialiser la disposition de la page")) a->trigger();
                wait(300);
                w9.grab().save(qEnvironmentVariable("HYDRA64_SHOTS") + (p == MainWindow::PageAnalysis ? QStringLiteral("/page-analyse.png") : QStringLiteral("/page-jouer.png")));
            }
            for (QSplitter* sp : w9.findChildren<QSplitter*>()) {
                if (!sp->isVisible()) continue;
                // Panels of the window: at least 100 px; other splitters: nothing collapsed.
                const int floor = sp->property("ads-splitter").isValid() ? 100 : 20;
                const QList<int> before = sp->sizes();
                for (int i = 0; i < sp->count(); ++i) {
                    QList<int> sizes;
                    for (int j = 0; j < sp->count(); ++j) sizes << (j == i ? 0 : 10000);
                    sp->setSizes(sizes);
                    wait(20);
                    QWidget* c = sp->widget(i);
                    const int extent = sp->orientation() == Qt::Horizontal ? c->width() : c->height();
                    // The board stays whole (player strips and transport bar included).
                    if (viewer9->isVisible() && viewer9->height() < viewer9->minimumSizeHint().height())
                        crushed << QStringLiteral("%1 : échiquier rogné à %2 px de haut").arg(QString::fromUtf8(names[int(p)])).arg(viewer9->height());
                    if (!c->isHidden() && extent < floor)
                        crushed << QStringLiteral("%1 : %2 n° %3 réduit à %4 px").arg(QString::fromUtf8(names[int(p)]), sp->orientation() == Qt::Horizontal ? "colonne" : "rangée").arg(i).arg(extent);
                }
                sp->setSizes(before);
            }
        }
        for (const QString& c : std::as_const(crushed)) std::printf("       %s\n", qPrintable(c));
        check(crushed.isEmpty(), "séparations : aucun panneau ne disparaît, sur les quatre pages");
        w9.close();
    }

    // ================= Opening surveys: index next to a database, list sorted by ECO, opening
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        dir.mkpath(QStringLiteral("surveys"));
        auto makeBase = [&](const QString& rel, const QStringList& pgns) {
            const QString path = dir.filePath(rel);
            std::string err;
            if (!hy64db::Database::create(hy64db::basePathOf(path.toStdString()), &err)) return false;
            hy64db::Database db;
            if (!db.open(path.toStdString(), &err)) return false;
            for (const QString& text : pgns) {
                Game g;
                uint32_t id = 0;
                if (!pgn::parseGame(text.toStdString(), g) || !db.append(g, &id, &err)) return false;
            }
            db.sync();
            return true;
        };
        auto text = [](const QString& title) { return QStringLiteral("[Event \"Texte\"]\n[White \"%1\"]\n[Black \"?\"]\n[Result \"*\"]\n\n{Survey} *\n").arg(title); };
        auto game = [](const QString& annotator) {
            return QStringLiteral("[Event \"T\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"1-0\"]\n[Annotator \"%1\"]\n\n1. e4 e6 2. d4 d5 1-0\n").arg(annotator);
        };
        const QString mainBase = dir.filePath(QStringLiteral("Encyclo.hy64i"));
        const bool made = makeBase(QStringLiteral("Encyclo.hy64i"), {game(QStringLiteral("X"))})
                          && makeBase(QStringLiteral("surveys/C11_2019-03_French-Steinitz.hy64i"),
                                      {text(QStringLiteral("C11 - 7...Be7 8.Qd2")), game(QStringLiteral("Szabo,Krisztian")), game(QStringLiteral("Szabo,Krisztian")), game(QStringLiteral("Other"))})
                          && makeBase(QStringLiteral("surveys/A00_2001-09_Sokolsky-1b4.hy64i"), {text(QStringLiteral("A00 - Sokolski Opening 1.b4")), game(QStringLiteral("Leisebein,Peter"))})
                          && makeBase(QStringLiteral("surveys/C55-56_2000-05_Canal-Gambit.hy64i"), {game(QStringLiteral("Y"))})
                          && makeBase(QStringLiteral("surveys/Inventors-Part-1.hy64i"), {text(QStringLiteral("Inventors of Modern Chess"))});
        QString error;
        const int built = made ? surveyindex::build(mainBase, dir.filePath(QStringLiteral("surveys")), &error) : -1;
        const QList<surveyindex::Survey> list = surveyindex::load(surveyindex::pathFor(mainBase), &error);
        check(built == 4 && list.size() == 4 && list[0].eco == QLatin1String("A00") && list[0].title == QLatin1String("Sokolski Opening 1.b4")
                  && list[0].author == QLatin1String("Leisebein,Peter") && list[0].games == 2 && list[1].eco == QLatin1String("C11")
                  && list[1].title == QStringLiteral("French Steinitz — 7...Be7 8.Qd2") && list[1].author == QLatin1String("Szabo,Krisztian")
                  && list[1].date == QLatin1String("2019-03") && list[2].ecoLabel() == QStringLiteral("C55–C56") && list[2].title == QLatin1String("Canal Gambit")
                  && list[3].eco.isEmpty() && list[3].title == QLatin1String("Inventors of Modern Chess") && QFileInfo::exists(list[3].file),
              qPrintable(QStringLiteral("synthèses : index de 4 bases trié par ECO (titre, auteur, tranche C55–C56, sans code en dernier) %1").arg(error)));
        check(surveyindex::find(list.value(1).file) == surveyindex::pathFor(mainBase) && surveyindex::find(dir.filePath(QStringLiteral("x/y/z.hy64i"))).isEmpty(),
              "synthèses : l'index se retrouve depuis une synthèse, pas depuis un autre dossier");
        MainWindow ws;
        ws.show();
        check(ws.surveyBrowser() == nullptr, "synthèses : pas de liste sans base affichée");
        ws.openDatabase(mainBase);
        SurveyBrowser* sb = ws.surveyBrowser();
        check(sb && sb->tree()->topLevelItemCount() == 4 && sb->tree()->topLevelItem(0)->text(0) == QLatin1String("A00")
                  && sb->tree()->topLevelItem(3)->text(0) == QStringLiteral("—"),
              "synthèses : liste des 4 synthèses de la base, triée par ECO");
        if (sb) {
            sb->show();
            sb->setSearch(QStringLiteral("c"));
            const int withC = sb->visibleCount(); // C11, C55–C56 ("Canal"), "Chess": all but A00
            sb->setSearch(QStringLiteral("szabo"));
            const int bySzabo = sb->visibleCount();
            sb->setSearch(QStringLiteral("C11 steinitz"));
            const bool one = sb->visibleCount() == 1 && sb->tree()->currentItem() && sb->tree()->currentItem()->text(0) == QLatin1String("C11");
            if (const QString shots = qEnvironmentVariable("HYDRA64_SHOTS"); !shots.isEmpty()) {
                sb->setSearch(QString());
                wait(100);
                sb->grab().save(shots + QStringLiteral("/syntheses.png"));
                sb->setSearch(QStringLiteral("C11 steinitz"));
            }
            emit sb->tree()->itemActivated(sb->tree()->currentItem(), 0);
            GameListPanel* panel = ws.findChild<GameListPanel*>();
            check(withC == 3 && bySzabo == 1 && one && panel && panel->model()->filePath() == list.value(1).file && panel->model()->totalGames() == 4
                      && ws.surveyBrowser() != nullptr,
                  qPrintable(QStringLiteral("synthèses : recherche par code, titre et auteur (%1, %2, %3) ; l'activation ouvre la synthèse (%4, %5 entrées), d'où la liste reste accessible")
                                 .arg(withC).arg(bySzabo).arg(one).arg(panel ? QFileInfo(panel->model()->filePath()).fileName() : QString()).arg(panel ? panel->model()->totalGames() : -1)));
            sb->close();
        }
        ws.close();
    }

    // ================= Opening tree made from the games of a database (File → Convert, db/gametree)
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        const QString base = dir.filePath(QStringLiteral("games.hy64i"));
        std::string err;
        bool made = hy64db::Database::create(hy64db::basePathOf(base.toStdString()), &err);
        {
            hy64db::Database db;
            made = made && db.open(base.toStdString(), &err);
            for (const char* text : {"[White \"A\"]\n[Black \"B\"]\n[Date \"2010.01.01\"]\n[WhiteElo \"2500\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0\n",
                                     "[White \"C\"]\n[Black \"D\"]\n[Date \"2012.01.01\"]\n[WhiteElo \"2400\"]\n[BlackElo \"2300\"]\n[Result \"0-1\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 0-1\n",
                                     "[White \"E\"]\n[Black \"F\"]\n[Result \"1/2-1/2\"]\n\n1. d4 d5 2. c4 e6 1/2-1/2\n",
                                     // alone from the first move, but its position after 3. Nb1 is the one after 1. a3:
                                     // visited twice, so the game is followed one move beyond it (3…h6)
                                     "[White \"G\"]\n[Black \"H\"]\n[Result \"*\"]\n\n1. a3 Nf6 2. Nc3 Ng8 3. Nb1 h6 4. h3 *\n"}) {
                Game g;
                uint32_t id = 0;
                made = made && pgn::parseGame(text, g) && db.append(g, &id, &err);
            }
            db.sync();
        }
        auto build = [&](const QString& name, int plies, int minGames, conversion::Report* report) {
            conversion::Request r;
            r.source = base;
            r.destination = dir.filePath(name);
            r.target = conversion::Target::Hydra64Tree;
            r.treePlies = plies;
            r.treeMinGames = minGames;
            QString error;
            return conversion::run(r, report, &error) && QFileInfo::exists(r.destination);
        };
        auto movesAfter = [&](const QString& name, const char* line) {
            Game g;
            pgn::parseGame(std::string(line) + " *", g);
            const Position pos = g.mainline().empty() ? g.root()->pos : g.mainline().back()->pos;
            chess::treebook::Book book;
            QStringList out;
            if (!book.open(dir.filePath(name).toStdString())) return out;
            for (const auto& m : book.moves(pos))
                out << QStringLiteral("%1 %2/%3/%4 %5 %6").arg(QString::fromStdString(pos.toSan(m.move))).arg(m.wins).arg(m.draws).arg(m.losses).arg(m.maxElo).arg(m.year);
            return out;
        };
        conversion::Report shared, all, shallow;
        const bool built = made && build(QStringLiteral("shared.hy64book"), 50, 2, &shared) && build(QStringLiteral("all.hy64book"), 50, 1, &all)
                           && build(QStringLiteral("shallow.hy64book"), 2, 1, &shallow);
        const QStringList first = movesAfter(QStringLiteral("shared.hy64book"), "");
        check(built && shared.kind == conversion::Kind::Tree && first == QStringList{QStringLiteral("e4 1/0/1 2500 2012"), QStringLiteral("d4 0/1/0 0 0"), QStringLiteral("a3 0/0/0 0 0")}
                  && movesAfter(QStringLiteral("shared.hy64book"), "1. e4") == QStringList{QStringLiteral("e5 1/0/1 2300 2012")},
              qPrintable(QStringLiteral("arbre d'une base : statistiques du camp qui joue le coup, Elo le plus élevé, dernière année (%1)").arg(first.join(QStringLiteral(", ")))));
        check(movesAfter(QStringLiteral("shared.hy64book"), "1. e4 e5 2. Nf3 Nc6").size() == 2 && movesAfter(QStringLiteral("shared.hy64book"), "1. e4 e5 2. Nf3 Nc6 3. Bb5").isEmpty()
                  && movesAfter(QStringLiteral("shared.hy64book"), "1. d4").isEmpty() && shared.items == 13,
              qPrintable(QStringLiteral("arbre d'une base : chaque partie suivie jusqu'à un coup après sa dernière position commune (%1 coups)").arg(shared.items)));
        check(movesAfter(QStringLiteral("shared.hy64book"), "1. a3 Nf6 2. Nc3 Ng8") == QStringList{QStringLiteral("Nb1 0/0/0 0 0")}
                  && movesAfter(QStringLiteral("shared.hy64book"), "1. a3").size() == 2 && movesAfter(QStringLiteral("shared.hy64book"), "1. a3 Nf6 2. Nc3 Ng8 3. Nb1 h6").isEmpty(),
              "arbre d'une base : une position qui revient dans la même partie compte comme commune (règle des arbres ChessOK)");
        check(all.items == 19 && movesAfter(QStringLiteral("all.hy64book"), "1. d4 d5 2. c4") == QStringList{QStringLiteral("e6 0/1/0 0 0")} && shallow.items == 6
                  && movesAfter(QStringLiteral("shallow.hy64book"), "1. e4 e5").isEmpty(),
              qPrintable(QStringLiteral("arbre d'une base : toutes les parties jusqu'au bout (%1 coups), ou limité à 2 demi-coups (%2 coups)").arg(all.items).arg(shallow.items)));
        ConvertDialog dlg(base);
        dlg.setTarget(conversion::Target::Polyglot);
        check(dlg.request().target == conversion::Target::Polyglot && dlg.request().destination.endsWith(QLatin1String(".bin")) && dlg.request().treePlies == 50
                  && dlg.request().treeMinGames == 2,
              "Convertir : une base de parties peut devenir un arbre (Hydra64 ou Polyglot), réglages des arbres ChessOK par défaut");
    }

    // ================= Help → Reset: like a fresh installation
    {
        const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(data + QStringLiteral("/pieces"));
        QFile marker(data + QStringLiteral("/pieces/wK.svg"));
        if (marker.open(QIODevice::WriteOnly)) marker.write("<svg/>");
        marker.close();
        const bool hadData = QFileInfo::exists(data + QStringLiteral("/engines.json")) && QFileInfo::exists(data + QStringLiteral("/session.json"));
        MainWindow::wipeUserData();
        QSettings s;
        check(hadData && !QFileInfo::exists(data + QStringLiteral("/engines.json")) && !QFileInfo::exists(data + QStringLiteral("/session.json")),
              "réinitialisation : moteurs et parties effacés");
        check(s.allKeys().isEmpty(), "réinitialisation : réglages effacés (dispositions, préférences…)");
        check(QFileInfo::exists(data + QStringLiteral("/pieces/wK.svg")), "réinitialisation : jeux de pièces de l'utilisateur conservés");
    }

    // ================= A panel moved on the Databases page stays where it was put when a database is opened
    {
        MainWindow w;
        w.resize(1400, 900);
        w.show();
        w.setPage(MainWindow::PageDatabase);
        waitFor([] { return false; }, 400);
        auto* dm = w.findChild<ads::CDockManager*>();
        ads::CDockWidget* games = dm->findDockWidget(QStringLiteral("games"));
        ads::CDockWidget* notation = dm->findDockWidget(QStringLiteral("notation"));
        const bool sameAreaBefore = games->dockAreaWidget() == notation->dockAreaWidget();
        dm->addDockWidgetTabToArea(games, notation->dockAreaWidget()); // the user drags the panel next to the notation
        waitFor([] { return false; }, 100);
        const bool moved = games->dockAreaWidget() == notation->dockAreaWidget();
        w.setPage(MainWindow::PageDatabase); // what opening a database does
        waitFor([] { return false; }, 100);
        check(!sameAreaBefore && moved && games->dockAreaWidget() == notation->dockAreaWidget(),
              "page Bases : un panneau déplacé reste à sa place quand on ouvre une autre base (la disposition n'est plus restaurée d'un ancien état)");
        // Clock and evaluation panels: thin strips (their height is locked, see lockStripHeights)
        ads::CDockWidget* clock = dm->findDockWidget(QStringLiteral("clock"));
        ads::CDockWidget* graph = dm->findDockWidget(QStringLiteral("graph"));
        check(clock && graph && clock->widget()->minimumHeight() == 60 && graph->widget()->minimumHeight() == 80,
              qPrintable(QStringLiteral("panneaux Pendule et Évaluation : bandes de hauteur fixe (%1 et %2 px)").arg(clock->widget()->minimumHeight()).arg(graph->widget()->minimumHeight())));
    }

    // ================= Beauty column of the game list (db/beauty.h)
    {
        QTemporaryDir tmp;
        const QString base = QDir(tmp.path()).filePath(QStringLiteral("beaute.hy64i"));
        std::string err;
        bool made = hy64db::Database::create(hy64db::basePathOf(base.toStdString()), &err);
        {
            hy64db::Database db;
            made = made && db.open(base.toStdString(), &err);
            for (const char* text : {"[White \"Calme\"]\n[Black \"Plat\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Bxc6 dxc6 5. O-O f6 6. d4 exd4 7. Nxd4 c5 8. Nb3 Qxd1 "
                                     "9. Rxd1 Bg4 10. f3 Be6 11. Nc3 Bd6 12. Be3 b6 1-0\n",
                                     "[White \"Morphy\"]\n[Black \"Duc\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 d6 3. d4 Bg4 4. dxe5 Bxf3 5. Qxf3 dxe5 6. Bc4 Nf6 7. Qb3 Qe7 8. Nc3 c6 "
                                     "9. Bg5 b5 10. Nxb5 cxb5 11. Bxb5+ Nbd7 12. O-O-O Rd8 13. Rxd7 Rxd7 14. Rd1 Qe6 15. Bxd7+ Nxd7 16. Qb8+ Nxb8 17. Rd8# 1-0\n"}) {
                Game g;
                uint32_t id = 0;
                made = made && pgn::parseGame(text, g) && db.append(g, &id, &err);
            }
            db.sync();
        }
        GameListModel model;
        QString error;
        const bool loaded = made && model.loadFile(base, &error) && model.rowCount() == 2;
        const QString quiet = loaded ? model.data(model.index(0, GameListModel::Beauty), Qt::DisplayRole).toString() : QString();
        const QString opera = loaded ? model.data(model.index(1, GameListModel::Beauty), Qt::DisplayRole).toString() : QString();
        check(loaded && quiet.isEmpty() && opera == QString(3, QChar(0x2605)) && model.columnCount() == GameListModel::ColumnCount &&
                  !model.data(model.index(1, GameListModel::Beauty), Qt::ToolTipRole).toString().isEmpty() &&
                  !GameListModel::defaultHiddenColumns().contains(GameListModel::Beauty),
              qPrintable(QStringLiteral("beauté : une partie ajoutée à une base est notée d'office — partie de l'Opéra « %1 », partie calme sans étoile ; colonne affichée par défaut").arg(opera)));
    }

    std::printf("\n%s\n", failures ? "ÉCHECS DÉTECTÉS" : "Tous les tests d'interface passent.");
    return failures ? 1 : 0;
}
